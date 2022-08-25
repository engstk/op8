// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gfp.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/memleak_stackdepot.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#define DEPOT_STACK_BITS (sizeof(ml_depot_stack_handle_t) * 8)

#define STACK_ALLOC_NULL_PROTECTION_BITS 1
#define STACK_ALLOC_ORDER 0 /* 'Slab' size order for stack depot, 1 pages */
#define STACK_ALLOC_SIZE (1LL << (PAGE_SHIFT + STACK_ALLOC_ORDER))
#define STACK_ALLOC_ALIGN 4
#define STACK_ALLOC_OFFSET_BITS (STACK_ALLOC_ORDER + PAGE_SHIFT - \
					STACK_ALLOC_ALIGN) //9
#define STACK_ALLOC_INDEX_BITS (DEPOT_STACK_BITS - \
		STACK_ALLOC_NULL_PROTECTION_BITS - STACK_ALLOC_OFFSET_BITS) //22
#define STACK_ALLOC_SLABS_CAP 10240
#define STACK_ALLOC_MAX_SLABS \
	(((1LL << (STACK_ALLOC_INDEX_BITS)) < STACK_ALLOC_SLABS_CAP) ? \
	 (1LL << (STACK_ALLOC_INDEX_BITS)) : STACK_ALLOC_SLABS_CAP)

/* The compact structure to store the reference to stacks. */
union ml_handle_parts {
	ml_depot_stack_handle_t handle;
	struct {
		u32 slabindex : STACK_ALLOC_INDEX_BITS;
		u32 offset : STACK_ALLOC_OFFSET_BITS;
		u32 valid : STACK_ALLOC_NULL_PROTECTION_BITS;
	};
};

struct ml_stack_record {
	struct ml_stack_record *next;	/* Link in the hashtable */
	u32 hash;			/* Hash in the hastable */
	u32 size;			/* Number of frames in the stack */
	union ml_handle_parts handle;
	unsigned long entries[1];	/* Variable-sized array of entries. */
};

static void **ml_stack_slabs = NULL;

static int ml_depot_index;
static int ml_next_slab_inited;
static size_t ml_depot_offset;
static DEFINE_SPINLOCK(ml_depot_lock);
static DEFINE_MUTEX(ml_depot_init_mutex);
static atomic_t ml_stack_depot_inited = ATOMIC_INIT(0);

static bool ml_init_stack_slab(void **prealloc)
{
	if (!*prealloc)
		return false;
	/*
	 * This smp_load_acquire() pairs with smp_store_release() to
	 * |ml_next_slab_inited| below and in ml_depot_alloc_stack().
	 */
	if (smp_load_acquire(&ml_next_slab_inited))
		return true;
	if (ml_stack_slabs[ml_depot_index] == NULL) {
		ml_stack_slabs[ml_depot_index] = *prealloc;
	} else {
		ml_stack_slabs[ml_depot_index + 1] = *prealloc;
		/*
		 * This smp_store_release pairs with smp_load_acquire() from
		 * |ml_next_slab_inited| above and in ml_depot_save_stack().
		 */
		smp_store_release(&ml_next_slab_inited, 1);
	}
	*prealloc = NULL;
	return true;
}

/* Allocation of a new stack in raw storage */
static struct ml_stack_record *ml_depot_alloc_stack(unsigned long *entries, int size,
		u32 hash, void **prealloc, gfp_t alloc_flags)
{
	int required_size = offsetof(struct ml_stack_record, entries) +
		sizeof(unsigned long) * size;
	struct ml_stack_record *stack;

	required_size = ALIGN(required_size, 1 << STACK_ALLOC_ALIGN);

	if (unlikely(ml_depot_offset + required_size > STACK_ALLOC_SIZE)) {
		if (unlikely(ml_depot_index + 1 >= STACK_ALLOC_MAX_SLABS)) {
			WARN_ONCE(1, "Stack depot reached limit capacity");
			return NULL;
		}
		ml_depot_index++;
		ml_depot_offset = 0;
		/*
		 * smp_store_release() here pairs with smp_load_acquire() from
		 * |ml_next_slab_inited| in ml_depot_save_stack() and
		 * ml_init_stack_slab().
		 */
		if (ml_depot_index + 1 < STACK_ALLOC_MAX_SLABS)
			smp_store_release(&ml_next_slab_inited, 0);
	}
	ml_init_stack_slab(prealloc);
	if (ml_stack_slabs[ml_depot_index] == NULL)
		return NULL;

	stack = ml_stack_slabs[ml_depot_index] + ml_depot_offset;

	stack->hash = hash;
	stack->size = size;
	stack->handle.slabindex = ml_depot_index;
	stack->handle.offset = ml_depot_offset >> STACK_ALLOC_ALIGN;
	stack->handle.valid = 1;
	memcpy(stack->entries, entries, size * sizeof(unsigned long));
	ml_depot_offset += required_size;

	return stack;
}

#define STACK_HASH_SIZE (1L << 19)
#define STACK_HASH_MASK (STACK_HASH_SIZE - 1)
#define STACK_HASH_SEED 0x9747b28c

static struct ml_stack_record **ml_stack_table = NULL;

/* Calculate hash for a stack */
static inline u32 ml_hash_stack(unsigned long *entries, unsigned int size)
{
	return jhash2((u32 *)entries,
			       size * sizeof(unsigned long) / sizeof(u32),
			       STACK_HASH_SEED);
}

/* Use our own, non-instrumented version of memcmp().
 *
 * We actually don't care about the order, just the equality.
 */
static inline
int ml_stackdepot_memcmp(const unsigned long *u1, const unsigned long *u2,
			unsigned int n)
{
	for ( ; n-- ; u1++, u2++) {
		if (*u1 != *u2)
			return 1;
	}
	return 0;
}

/* Find a stack that is equal to the one stored in entries in the hash */
static inline struct ml_stack_record *ml_find_stack(struct ml_stack_record *bucket,
					     unsigned long *entries, int size,
					     u32 hash)
{
	struct ml_stack_record *found;

	for (found = bucket; found; found = found->next) {
		if (found->hash == hash &&
		    found->size == size &&
		    !ml_stackdepot_memcmp(entries, found->entries, size))
			return found;
	}
	return NULL;
}

void ml_depot_fetch_stack(ml_depot_stack_handle_t handle, struct stack_trace *trace)
{
	union ml_handle_parts parts = { .handle = handle };
	void *slab;
	size_t offset;
	struct ml_stack_record *stack;

	if (atomic_read(&ml_stack_depot_inited) == 0) {
		pr_err("ml_stack_depot_inited is not inited\n");
		return;
	}

	slab = ml_stack_slabs[parts.slabindex];
	offset = parts.offset << STACK_ALLOC_ALIGN;
	stack = slab + offset;
	trace->nr_entries = trace->max_entries = stack->size;
	trace->entries = stack->entries;
	trace->skip = 0;
}
EXPORT_SYMBOL_GPL(ml_depot_fetch_stack);

/**
 * ml_depot_save_stack - save stack in a stack depot.
 * @trace - the stacktrace to save.
 * @alloc_flags - flags for allocating additional memory if required.
 *
 * Returns the handle of the stack struct stored in depot.
 */
ml_depot_stack_handle_t ml_depot_save_stack(struct stack_trace *trace,
				    gfp_t alloc_flags)
{
	u32 hash;
	ml_depot_stack_handle_t retval = 0;
	struct ml_stack_record *found = NULL, **bucket;
	unsigned long flags;
	struct page *page = NULL;
	void *prealloc = NULL;

	if (atomic_read(&ml_stack_depot_inited) == 0) {
		pr_err("ml_stack_depot_inited is not inited\n");
		goto fast_exit;
	}

	if (unlikely(trace->nr_entries == 0))
		goto fast_exit;

	hash = ml_hash_stack(trace->entries, trace->nr_entries);
	bucket = &ml_stack_table[hash & STACK_HASH_MASK];

	/*
	 * Fast path: look the stack trace up without locking.
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |bucket| below.
	 */
	found = ml_find_stack(smp_load_acquire(bucket), trace->entries,
			   trace->nr_entries, hash);
	if (found)
		goto exit;

	/*
	 * Check if the current or the next stack slab need to be initialized.
	 * If so, allocate the memory - we won't be able to do that under the
	 * lock.
	 *
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |ml_next_slab_inited| in ml_depot_alloc_stack() and ml_init_stack_slab().
	 */
	if (unlikely(!smp_load_acquire(&ml_next_slab_inited))) {
		/*
		 * Zero out zone modifiers, as we don't have specific zone
		 * requirements. Keep the flags related to allocation in atomic
		 * contexts and I/O.
		 */
		alloc_flags &= ~GFP_ZONEMASK;
		alloc_flags &= (GFP_ATOMIC | GFP_KERNEL);
		alloc_flags |= __GFP_NOWARN;
		page = alloc_pages(alloc_flags, STACK_ALLOC_ORDER);
		if (page)
			prealloc = page_address(page);
	}

	spin_lock_irqsave(&ml_depot_lock, flags);
	found = ml_find_stack(*bucket, trace->entries, trace->nr_entries, hash);
	if (!found) {
		struct ml_stack_record *new =
			ml_depot_alloc_stack(trace->entries, trace->nr_entries,
					  hash, &prealloc, alloc_flags);
		if (new) {
			new->next = *bucket;
			/*
			 * This smp_store_release() pairs with
			 * smp_load_acquire() from |bucket| above.
			 */
			smp_store_release(bucket, new);
			found = new;
		}
	} else if (prealloc) {
		/*
		 * We didn't need to store this stack trace, but let's keep
		 * the preallocated memory for the future.
		 */
		WARN_ON(!ml_init_stack_slab(&prealloc));
	}
	spin_unlock_irqrestore(&ml_depot_lock, flags);

	if (prealloc) {
		/* Nobody used this memory, ok to free it. */
		free_pages((unsigned long)prealloc, STACK_ALLOC_ORDER);
	}

exit:
	if (found)
		retval = found->handle.handle;
fast_exit:
	return retval;
}
EXPORT_SYMBOL_GPL(ml_depot_save_stack);

int ml_depot_init(void)
{
	int inited;
	unsigned int size;

	mutex_lock(&ml_depot_init_mutex);
	inited = atomic_read(&ml_stack_depot_inited);
	if (inited != 0) {
		mutex_unlock(&ml_depot_init_mutex);
		pr_err("ml_stack_depot_inited is inited, val %d\n", inited);
		return 0;
	}

	size = sizeof(void *) * STACK_ALLOC_MAX_SLABS;
	ml_stack_slabs = (void **)vmalloc(size);
	if (!ml_stack_slabs) {
		mutex_unlock(&ml_depot_init_mutex);
		pr_err("vmalloc ml_stack_slabs %d failed.\n", size);
		return -1;
	}
	memset(ml_stack_slabs, 0, size);

	size = sizeof(struct ml_stack_record *) * STACK_HASH_SIZE;
	ml_stack_table = (struct ml_stack_record **)vmalloc(size);
	if (!ml_stack_table) {
		mutex_unlock(&ml_depot_init_mutex);
		pr_err("vmalloc ml_stack_table %d failed.\n", size);
		vfree(ml_stack_slabs);
		ml_stack_slabs = NULL;
		return -1;
	}
	memset(ml_stack_table, 0, size);

	atomic_set(&ml_stack_depot_inited, 1);
	mutex_unlock(&ml_depot_init_mutex);
	printk("ml_stack_depot_inited is inited.\n");
	return 0;
}
EXPORT_SYMBOL_GPL(ml_depot_init);

int ml_get_depot_index(void)
{
	return ml_depot_index;
}
EXPORT_SYMBOL_GPL(ml_get_depot_index);
