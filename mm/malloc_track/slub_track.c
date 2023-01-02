// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _SLUB_TRACK_
#define _SLUB_TRACK_
#include <linux/sort.h>
#include <linux/jhash.h>
#include <linux/version.h>

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
extern void dump_meminfo_to_logger(const char *tag, char *msg, size_t len);
#endif

/*
 * sort the locations with count from more to less.
 */
#define LOCATIONS_TRACK_BUF_SIZE(s) ((s->object_size == 128) ? (PAGE_SIZE << 10) : (PAGE_SIZE * 128))
#define KD_SLABTRACE_STACK_CNT TRACK_ADDRS_COUNT
#define KD_BUFF_LEN(total, len) (total - len - 101)
#define KD_BUFF_LEN_MAX(total) (total - 101 - 100)
#define KD_BUFF_LEN_EXT(total, len) (total - len - 55)

struct kd_location {
	unsigned long count;
	unsigned long addr;
	unsigned long addrs[KD_SLABTRACE_STACK_CNT]; /* caller address */
	u32 depth;
	u32 hash;
	long long sum_time;
	long min_time;
	long max_time;
	long min_pid;
	long max_pid;
};

struct kd_loc_track {
	unsigned long max;
	unsigned long count;
	struct kd_location *loc;
};

/*
 * kmalloc_debug_info add debug to slab name.
 */
const struct kmalloc_info_struct kmalloc_debug_info[] = {
	{NULL,				  0}, {"kmalloc-debug-96",             96},
	{"kmalloc-debug-192",           192}, {"kmalloc-debug-8",               8},
	{"kmalloc-debug-16",             16}, {"kmalloc-debug-32",             32},
	{"kmalloc-debug-64",             64}, {"kmalloc-debug-128",           128},
	{"kmalloc-debug-256",           256}, {"kmalloc-debug-512",           512},
	{"kmalloc-debug-1k",           1024}, {"kmalloc-debug-2k",           2048},
	{"kmalloc-debug-4k",           4096}, {"kmalloc-debug-8k",           8192},
	{"kmalloc-debug-16k",         16384}, {"kmalloc-debug-32k",         32768},
	{"kmalloc-debug-64k",         65536}, {"kmalloc-debug-128k",       131072},
	{"kmalloc-debug-256k",       262144}, {"kmalloc-debug-512k",       524288},
	{"kmalloc-debug-1M",        1048576}, {"kmalloc-debug-2M",        2097152},
	{"kmalloc-debug-4M",        4194304}, {"kmalloc-debug-8M",        8388608},
	{"kmalloc-debug-16M",      16777216}, {"kmalloc-debug-32M",      33554432},
	{"kmalloc-debug-64M",      67108864}
};

/*
 * kmalloc_debug_caches store the kmalloc caches with debug flag.
 */
atomic64_t kmalloc_debug_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1] = {{ATOMIC64_INIT(0)}};
EXPORT_SYMBOL(kmalloc_debug_caches);

int kmalloc_debug_enable = 0;
EXPORT_SYMBOL(kmalloc_debug_enable);

struct task_struct *memleak_detect_task = NULL;
static DEFINE_MUTEX(debug_mutex);

extern unsigned long calculate_kmalloc_slab_size(struct kmem_cache *s);

static int kd_location_cmp(const void *la, const void *lb)
{
	return ((struct kd_location *)lb)->count - ((struct kd_location *)la)->count;
}

static void kd_location_swap(void *la, void *lb, int size)
{
	struct kd_location l_tmp;

	memcpy(&l_tmp, la, size);
	memcpy(la, lb, size);
	memcpy(lb, &l_tmp, size);
}

static void kd_free_loc_track(struct kd_loc_track *t)
{
	if (t->max)
		vfree(t->loc);
}

static int kd_alloc_loc_track(struct kd_loc_track *t, int buff_size)
{
	struct kd_location *l;

	l = (void *)vmalloc(buff_size);
	if (!l) {
		buff_size >>= 1;
		l = (void *)vmalloc(buff_size);
		if (!l)
			return -ENOMEM;
	}

	t->count = 0;
	t->max = buff_size / sizeof(struct kd_location);
	t->loc = l;
	return 0;
}

static int kd_add_location(struct kd_loc_track *t, struct kmem_cache *s,
		const struct track *track)
{
	long start, end, pos;
	struct kd_location *l;
	/*
         * save the stack depth and hash.
	 */
	u32 hash;
	unsigned long age;

	if (track->hash == 0)
		return -EINVAL;

	age = jiffies - track->when;
	start = -1;
	end = t->count;

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		/*
		 * There is nothing at "end". If we end up there
		 * we need to add something to before end.
		 */
		if (pos == end)
			break;

		hash = t->loc[pos].hash;
		if (track->hash == hash) {
			l = &t->loc[pos];
			l->count++;
			if (track->when) {
				l->sum_time += age;
				if (age < l->min_time)
					l->min_time = age;
				if (age > l->max_time)
					l->max_time = age;

				if (track->pid < l->min_pid)
					l->min_pid = track->pid;
				if (track->pid > l->max_pid)
					l->max_pid = track->pid;
			}
			return 0;
		}
		/*
		 * use hash value to record the stack.
		 */
		if (track->hash < hash)
			end = pos;
		else
			start = pos;
	}

	/*
	 * Not found. Insert new tracking element.
	 */
	if (t->count >= t->max)
		return -ENOMEM;

	l = t->loc + pos;
	if (pos < t->count)
		memmove(l + 1, l,
				(t->count - pos) * sizeof(struct kd_location));
	t->count++;
	l->count = 1;
	l->addr = track->addr;
	l->sum_time = age;
	l->min_time = age;
	l->max_time = age;
	l->min_pid = track->pid;
	l->max_pid = track->pid;
	l->depth = min_t(u32, (u32)(sizeof(l->addrs)/sizeof(l->addrs[0])),
			track->depth);
	l->hash = track->hash;
#ifdef COMPACT_OPLUS_SLUB_TRACK
	{
		int i;
		for (i = 0; i < l->depth; i++)
			l->addrs[i] = track->addrs[i] + MODULES_VADDR;
	}
#else
	memcpy(l->addrs, track->addrs, sizeof(l->addrs[0])*l->depth);
#endif
	return 0;
}

static int kd_process_slab(struct kd_loc_track *t, struct kmem_cache *s,
		struct page *page, unsigned long *map)
{
	void *addr = page_address(page);
	void *p;
	unsigned int dropped = 0;

	bitmap_zero(map, page->objects);
	get_map(s, page, map);

	for_each_object(p, s, addr, page->objects)
		if (!test_bit(slab_index(p, s, addr), map))
			if (kd_add_location(t, s, get_track(s, p, TRACK_ALLOC)))
				dropped++;
	return dropped;
}

static int kd_list_locations(struct kmem_cache *s, char *buf, int buff_len)
{
	int len = 0;
	unsigned long i, j;
	struct kd_loc_track t = { 0, 0, NULL };
	int node;
	unsigned long *map = vmalloc(BITS_TO_LONGS(oo_objects(s->max)) * sizeof(unsigned long));
	struct kmem_cache_node *n;
	int ret;
	int dropped = 0;

	if (!map || kd_alloc_loc_track(&t, LOCATIONS_TRACK_BUF_SIZE(s))) {
		vfree(map);
		len = sprintf(buf, "Out of memory\n");
		return len;
	}

	/* Push back cpu slabs */
	flush_all(s);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long flags;
		struct page *page;

		if (!atomic_long_read(&n->nr_slabs))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(page, &n->partial, lru) {
			ret = kd_process_slab(&t, s, page, map);
			if (ret)
				dropped += ret;
		}
		list_for_each_entry(page, &n->full, lru) {
			ret = kd_process_slab(&t, s, page, map);
			if (ret)
				dropped += ret;
		}
		spin_unlock_irqrestore(&n->list_lock, flags);
	}
	vfree(map);

	/*
	 * sort the locations with count from more to less.
	 */
	sort(&t.loc[0], t.count, sizeof(struct kd_location), kd_location_cmp,
			kd_location_swap);

	for (i = 0; i < t.count; i++) {
		struct kd_location *l = &t.loc[i];

		if (len >= KD_BUFF_LEN_MAX(buff_len))
			break;

		len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len), "%7ld ",
				l->count);

		if (l->addr)
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len), "%pS",
					(void *)l->addr);
		else
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					"<not-available>");

		if (l->sum_time != l->min_time)
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					" age=%ld/%ld/%ld",
					l->min_time,
					(long)div_u64(l->sum_time, l->count),
					l->max_time);
		else
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					" age=%ld", l->min_time);

		if (l->min_pid != l->max_pid)
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					" pid=%ld-%ld", l->min_pid, l->max_pid);
		else
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					" pid=%ld", l->min_pid);
		len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len), "\n");

		for (j = 0; j < l->depth; j++)
			len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len),
					"%pS\n", (void *)l->addrs[j]);
		len += scnprintf(buf + len, KD_BUFF_LEN(buff_len, len), "\n");
	}
	if (t.count && (buf[len -1] != '\n'))
		buf[len++] = '\n';
	kd_free_loc_track(&t);

	if (!t.count)
		len += scnprintf(buf + len, KD_BUFF_LEN_EXT(buff_len, len),
				"%s no data\n", s->name);
	if (dropped)
		len += scnprintf(buf + len, KD_BUFF_LEN_EXT(buff_len, len),
				"%s dropped %d %lu %lu\n",
				s->name, dropped, t.count, t.max);
	if (buf[len -1] != '\n')
		buf[len++] = '\n';
	return len;
}

int kbuf_dump_kmalloc_debug(struct kmem_cache *s, char *kbuf, int buff_len)
{
	memset(kbuf, 0, buff_len);
	return kd_list_locations(s, kbuf, buff_len);
}

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
#define KMALLOC_DEBUG_MIN_WATERMARK 100u
#define KMALLOC_DEBUG_DUMP_STEP 20u
#define BUFLEN(total, len) (total - len - 81)
#define BUFLEN_EXT(total, len) (total - len - 1)
#define KMALLOC_LOG_TAG "kmalloc_debug"

static unsigned int kmalloc_debug_watermark[KMALLOC_SHIFT_HIGH + 1];

void kmalloc_debug_watermark_init(void)
{
	int i;
	unsigned int water;

	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		if (kmalloc_debug_info[i].size <= 128)
			water = KMALLOC_DEBUG_MIN_WATERMARK;
		else
			water = KMALLOC_DEBUG_MIN_WATERMARK >> 1;
		kmalloc_debug_watermark[i] = water;
	}
}

static void dump_locations(struct kmem_cache *s, int slab_size, int index,
		char *dump_buff, int len)
{
	unsigned long i, j;
	struct kd_loc_track t = { 0, 0, NULL };
	unsigned long *map;
	int node;
	struct kmem_cache_node *n;
	int ret, dropped = 0;
	int dump_buff_len = 0;

	map = vmalloc(BITS_TO_LONGS(oo_objects(s->max)) * sizeof(unsigned long));
	if (!map || kd_alloc_loc_track(&t, LOCATIONS_TRACK_BUF_SIZE(s))) {
		vfree(map);
		pr_err("[kmalloc_debug] Out of memory\n");
		return;
	}

	/* Push back cpu slabs */
	flush_all(s);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long flags;
		struct page *page;

		if (!atomic_long_read(&n->nr_slabs))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(page, &n->partial, lru) {
			ret = kd_process_slab(&t, s, page, map);
			if (ret)
				dropped += ret;
		}
		list_for_each_entry(page, &n->full, lru) {
			ret = kd_process_slab(&t, s, page, map);
			if (ret)
				dropped += ret;
		}
		spin_unlock_irqrestore(&n->list_lock, flags);
	}
	vfree(map);

	sort(&t.loc[0], t.count, sizeof(struct kd_location), kd_location_cmp,
			kd_location_swap);

	dump_buff_len = scnprintf(dump_buff + dump_buff_len,
			len - dump_buff_len - 2,
			"%s used %u MB Water %u MB:\n", s->name, slab_size,
			kmalloc_debug_watermark[index]);

	for (i = 0; i < t.count; i++) {
		struct kd_location *l = &t.loc[i];

		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len),
				"%ld KB %pS age=%ld/%ld/%ld pid=%ld-%ld\n",
				(l->count * s->object_size) >> 10,
				(void *)l->addr,
				l->min_time,
				(long)div_u64(l->sum_time, l->count),
				l->max_time,
				l->min_pid, l->max_pid);

		for (j = 0; j < l->depth; j++)
			dump_buff_len += scnprintf(dump_buff + dump_buff_len,
					BUFLEN(len, dump_buff_len),
					"%pS\n", (void *)l->addrs[j]);

		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len), "-\n");
	}

	kd_free_loc_track(&t);
	if (!t.count)
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN_EXT(len, dump_buff_len),
				"[kmalloc_debug]%s no data\n", s->name);

	if (dropped)
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN_EXT(len, dump_buff_len),
				"%s dropped %d %lu %lu\n",
				s->name, dropped, t.count, t.max);
	dump_buff[dump_buff_len++] = '\n';
	dump_meminfo_to_logger(KMALLOC_LOG_TAG, dump_buff, dump_buff_len);
}

void dump_kmalloc_debug_info(struct kmem_cache *s, int index, char *dump_buff,
		int len)
{
	unsigned int slab_size;

	slab_size = calculate_kmalloc_slab_size(s) >> 20;
	if (slab_size < kmalloc_debug_watermark[index]) {
		pr_warn("[kmalloc_debug]slab %s size %uMB is not over %uMB, ignore it.\n",
				s->name, slab_size, kmalloc_debug_watermark[index]);
		return;
	}

	if (!dump_buff) {
		pr_err("[kmalloc_debug] dump_buff is NULL.\n");
		return;
	}

	kmalloc_debug_watermark[index] += KMALLOC_DEBUG_DUMP_STEP;
	dump_locations(s, slab_size, index, dump_buff, len);
}
#endif

static inline const char *
kmalloc_debug_cache_name(const char *prefix, unsigned int size)
{
	static const char units[3] = "\0kM";
	int idx = 0;

	while (size >= 1024 && (size % 1024 == 0)) {
		size /= 1024;
		idx++;
	}

	return kasprintf(GFP_NOWAIT, "%s-%u%c", prefix, size, units[idx]);
}

static struct kmem_cache *create_kmalloc_debug_caches(size_t size,
		unsigned long flags, enum kmalloc_cache_type kmalloc_type)
{
	unsigned int index = kmalloc_index(size);
	struct kmem_cache *s;
	const char *name;

	if ((!index) || (index < KMALLOC_SHIFT_LOW) ||
			(index > KMALLOC_SHIFT_HIGH)) {
		pr_warn("kmalloc debug cache create failed size %lu index %d\n",
				size, index);
		return NULL;
	}

	s = kmalloc_caches[kmalloc_type][index];
	if (!s) {
		pr_warn("kmalloc-%lu slab is NULL, do not create debug one\n",
				size);
		return NULL;
	}

	if (s->flags & SLAB_STORE_USER) {
		pr_warn("%s slab is enable SLAB_STORE_USER, do not "\
				"create a new debug slab and size %lu.\n",
				s->name, size);
		return NULL;
	}

	if (kmalloc_type == KMALLOC_RECLAIM) {
		flags |= SLAB_RECLAIM_ACCOUNT;
		name = kmalloc_debug_cache_name("kmalloc-debug-rcl",
				kmalloc_debug_info[index].size);
		if (!name)
			return NULL;
	} else
		name = kmalloc_debug_info[index].name;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	s = kmem_cache_create(name, kmalloc_debug_info[index].size,
			ARCH_KMALLOC_MINALIGN, flags, NULL);
#else
	s = kmem_cache_create_usercopy(name, kmalloc_debug_info[index].size,
			ARCH_KMALLOC_MINALIGN, flags, 0,
			kmalloc_debug_info[index].size, NULL);
#endif
	if (kmalloc_type == KMALLOC_RECLAIM)
		kfree(name);

	return s;
}

#define KMALLOC_DEBUG_R_LEN 1024

static ssize_t kmalloc_debug_create_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char *kbuf;
	struct kmem_cache *s;
	int i, type;
	int len = 0;

	kbuf = kmalloc(KMALLOC_DEBUG_R_LEN, GFP_KERNEL);
	if (!kbuf) {
		pr_warn("[kmalloc_debug] %s allo kbuf failed.\n", __func__);
		return -ENOMEM;
	}

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
			s = (struct kmem_cache *)atomic64_read(
					&kmalloc_debug_caches[type][i]);
			if (s)
				len += scnprintf(kbuf+len, KMALLOC_DEBUG_R_LEN - len - 1,
						"%s\n", s->name);
		}
	}

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
			s = kmalloc_caches[type][i];
			if (s && (s->flags & SLAB_STORE_USER))
				len += scnprintf(kbuf+len, KMALLOC_DEBUG_R_LEN - len - 1,
						"%s\n", s->name);
		}
	}

	if ((len > 0) && (kbuf[len - 1] != '\n'))
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf, (len < count ? len : count))) {
		kfree(kbuf);
		return -EFAULT;
	}
	kfree(kbuf);

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static ssize_t kmalloc_debug_create_write(struct file *file, const char __user *buff,
		size_t len, loff_t *ppos)
{
	char kbuf[64] = {'0'};
	long size;
	int ret, type;
	unsigned int index;
	struct kmem_cache *s;

	if (!kmalloc_debug_enable)
		return -EPERM;

	if (len > 63)
		len = 63;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 10, &size);
	if (ret)
		return -EINVAL;

	index = kmalloc_index(size);
	mutex_lock(&debug_mutex);
	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		s = (struct kmem_cache *)atomic64_read(&kmalloc_debug_caches[type][index]);
		if (s) {
			pr_warn("slab %s has been created, addr is %p, size %lu.\n",
					kmalloc_debug_info[index].name, s, size);
			mutex_unlock(&debug_mutex);
			return -EEXIST;
		}
	}

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		s = create_kmalloc_debug_caches((size_t)size, SLAB_STORE_USER, type);
		if (!s) {
			mutex_unlock(&debug_mutex);
			return -ENOMEM;
		}
		atomic64_set(&kmalloc_debug_caches[type][index], (unsigned long)s);
	}
	mutex_unlock(&debug_mutex);

	return len;
}

static const struct file_operations kmalloc_debug_create_operations = {
	.read		= kmalloc_debug_create_read,
	.write          = kmalloc_debug_create_write,
};

#if (defined(CONFIG_KMALLOC_DEBUG) || defined(CONFIG_VMALLOC_DEBUG))
#define MEMLEAK_DETECT_SLEEP_SEC (5400 * HZ)

#ifdef CONFIG_VMALLOC_DEBUG
#ifdef CONFIG_MEMLEAK_DETECT_THREAD
extern void dump_vmalloc_debug(char *dump_buff, int len);
#endif
extern void enable_vmalloc_debug(void);
extern void disable_vmalloc_debug(void);
#endif

#ifdef CONFIG_KMALLOC_DEBUG
#ifdef CONFIG_MEMLEAK_DETECT_THREAD
extern void kmalloc_debug_watermark_init(void);
extern void dump_kmalloc_debug_info(struct kmem_cache *s, int index,
		char *dump_buff, int len);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
extern void dump_ion_info(char *dump_buff, int len);
#endif
#endif

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
static const struct file_operations memleak_detect_thread_operations;
#endif

extern int kbuf_dump_kmalloc_debug(struct kmem_cache *s, char *kbuf, int buff_len);

#define KD_VALUE_LEN (32)
#define DATA_LEN (PAGE_SIZE)
#define ALL_KMALLOC_HIGH (KMALLOC_SHIFT_HIGH * 2 + 1)
#define CACHE_INDEX(index) ((index) % (KMALLOC_SHIFT_HIGH + 1))
#define CACHE_TYPE(index) ((index) / (KMALLOC_SHIFT_HIGH + 1))

static inline void
memleak_accumulate_slabinfo(struct kmem_cache *s, struct slabinfo *info)
{
	struct kmem_cache *c;
	struct slabinfo sinfo;

	if (!is_root_cache(s))
		return;

	for_each_memcg_cache(c, s) {
		memset(&sinfo, 0, sizeof(sinfo));
		get_slabinfo(c, &sinfo);

		info->active_slabs += sinfo.active_slabs;
		info->num_slabs += sinfo.num_slabs;
		info->shared_avail += sinfo.shared_avail;
		info->active_objs += sinfo.active_objs;
		info->num_objs += sinfo.num_objs;
	}
}

unsigned long calculate_kmalloc_slab_size(struct kmem_cache *s)
{
	struct slabinfo sinfo;

	memset(&sinfo, 0, sizeof(sinfo));
	get_slabinfo(s, &sinfo);
	memleak_accumulate_slabinfo(s, &sinfo);
	return sinfo.num_objs * s->object_size;
}

static void *kmo_start(struct seq_file *m, loff_t *pos)
{
	struct kmem_cache *s = NULL;

	while (*pos <= ALL_KMALLOC_HIGH) {
		s = kmalloc_caches[CACHE_TYPE(*pos)][CACHE_INDEX(*pos)];
		if (s && (s->flags & SLAB_STORE_USER))
			return (void *)s;
		++*pos;
	}

	return NULL;
}

static void *kmo_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct kmem_cache *s = NULL;

	++*pos;
	while (*pos <= ALL_KMALLOC_HIGH) {
		s = kmalloc_caches[CACHE_TYPE(*pos)][CACHE_INDEX(*pos)];
		if (s && (s->flags & SLAB_STORE_USER))
			return (void *)s;
		++*pos;
	}

	return NULL;
}

static void *kmd_start(struct seq_file *m, loff_t *pos)
{
	struct kmem_cache *s = NULL;

	while (*pos <= ALL_KMALLOC_HIGH) {
		s = (struct kmem_cache *)atomic64_read(&kmalloc_debug_caches[CACHE_TYPE(*pos)][CACHE_INDEX(*pos)]);
		if (s && (s->flags & SLAB_STORE_USER))
			return (void *)s;
		++*pos;
	}

	return NULL;
}

static void *kmd_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct kmem_cache *s = NULL;

	++*pos;
	while (*pos <= ALL_KMALLOC_HIGH) {
		s = (struct kmem_cache *)atomic64_read(&kmalloc_debug_caches[CACHE_TYPE(*pos)][CACHE_INDEX(*pos)]);
		if (s && (s->flags & SLAB_STORE_USER))
			return (void *)s;
		++*pos;
	}

	return NULL;
}

static void kmd_stop(struct seq_file *m, void *p)
{
}

static int kmd_show(struct seq_file *m, void *p)
{
	struct kmem_cache *s = (struct kmem_cache *)p;

	(void)kbuf_dump_kmalloc_debug(s, (char *)m->private, DATA_LEN);
	seq_printf(m, "=== slab %s debug info:\n", s->name);
	seq_printf(m, "%s\n", (char *)m->private);

	return 0;
}

static const struct seq_operations kmalloc_debug_op = {
	.start	= kmd_start,
	.next	= kmd_next,
	.show	= kmd_show,
	.stop	= kmd_stop
};

static const struct seq_operations kmalloc_origin_op = {
	.start	= kmo_start,
	.next	= kmo_next,
	.show	= kmd_show,
	.stop	= kmd_stop
};

static int kmalloc_debug_open(struct inode *inode, struct file *file)
{
	void *priv = __seq_open_private(file, &kmalloc_debug_op, DATA_LEN);

	if (!priv)
		return -ENOMEM;

	return 0;
}

static int kmalloc_origin_open(struct inode *inode, struct file *file)
{
	void *priv = __seq_open_private(file, &kmalloc_origin_op, DATA_LEN);

	if (!priv)
		return -ENOMEM;

	return 0;
}

static ssize_t kmalloc_debug_enable_write(struct file *file,
		const char __user *buff, size_t len, loff_t *ppos)
{
	char kbuf[KD_VALUE_LEN] = {'0'};
	long val;
	int ret;

	if (len > (KD_VALUE_LEN - 1))
		len = KD_VALUE_LEN - 1;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 10, &val);
	if (ret)
		return -EINVAL;

	kmalloc_debug_enable = val ? 1 : 0;
	return len;
}

static ssize_t kmalloc_debug_enable_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[KD_VALUE_LEN] = {'0'};
	int len;

	len = scnprintf(kbuf, KD_VALUE_LEN - 1, "%d\n", kmalloc_debug_enable);
	if (kbuf[len - 1] != '\n')
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static ssize_t kmalloc_used_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[512];
	int len = 0;
	int i, type;

	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		unsigned long slab_size = 0;

		for(type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
			struct kmem_cache *s;

			s = kmalloc_caches[type][i];
			if (!s)
				continue;
			slab_size += calculate_kmalloc_slab_size(s);

			s = (struct kmem_cache *)atomic64_read(&kmalloc_debug_caches[type][i]);
			if (!s)
				continue;
			slab_size += calculate_kmalloc_slab_size(s);
		}

		if (slab_size == 0)
			continue;

		len += scnprintf(&kbuf[len], 511 - len, "%-8u %lu\n",
				kmalloc_debug_info[i].size, slab_size >> 10);
		if (len >= 511)
			break;
	}

	if ((len > 0) && (kbuf[len - 1] != '\n'))
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static const struct file_operations kmalloc_used_ops = {
	.read		= kmalloc_used_read,
};

static const struct file_operations kmalloc_debug_enable_operations = {
	.write          = kmalloc_debug_enable_write,
	.read		= kmalloc_debug_enable_read,
};

static const struct file_operations kmalloc_debug_operations = {
	.open		= kmalloc_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static const struct file_operations kmalloc_origin_operations = {
	.open		= kmalloc_origin_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

int __init create_kmalloc_debug(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *dpentry;
	struct proc_dir_entry *opentry;
	struct proc_dir_entry *cpentry;
	struct proc_dir_entry *epentry;
	struct proc_dir_entry *upentry;
#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
	struct proc_dir_entry *mpentry;
#endif

	dpentry = proc_create("kmalloc_debug", S_IRUGO, parent,
			&kmalloc_debug_operations);
	if (!dpentry) {
		pr_err("create kmalloc_debug proc failed.\n");
		return -ENOMEM;
	}

	opentry = proc_create("kmalloc_origin", S_IRUGO, parent,
			&kmalloc_origin_operations);
	if (!opentry) {
		pr_err("create kmalloc_origin proc failed.\n");
		proc_remove(dpentry);
		return -ENOMEM;
	}

	epentry = proc_create("kmalloc_debug_enable",
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
			parent,	&kmalloc_debug_enable_operations);
	if (!epentry) {
		pr_err("create kmalloc_debug_enable proc failed.\n");
		proc_remove(opentry);
		proc_remove(dpentry);
		return -ENOMEM;
	}

	upentry = proc_create("kmalloc_used", S_IRUGO, parent,
			&kmalloc_used_ops);
	if (!upentry) {
		pr_err("create kmalloc_used proc failed.\n");
		proc_remove(epentry);
		proc_remove(opentry);
		proc_remove(dpentry);
		return -ENOMEM;
	}

	/* add new proc interface for create kmalloc debug caches.  */
	cpentry = proc_create("kmalloc_debug_create", S_IRUGO|S_IWUGO, parent,
			&kmalloc_debug_create_operations);
	if (!cpentry) {
		pr_err("create kmalloc_debug_create proc failed.\n");
		proc_remove(upentry);
		proc_remove(epentry);
		proc_remove(opentry);
		proc_remove(dpentry);
		return -ENOMEM;
	}

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
	mpentry = proc_create("memleak_detect_thread", S_IRUGO|S_IWUGO, parent,
			&memleak_detect_thread_operations);
	if (!cpentry) {
		pr_err("create memleak_detect_thread_operations proc failed.\n");
		proc_remove(cpentry);
		proc_remove(upentry);
		proc_remove(epentry);
		proc_remove(opentry);
		proc_remove(dpentry);
		return -ENOMEM;
	}
#endif
	return 0;
}
EXPORT_SYMBOL(create_kmalloc_debug);
#endif

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
#define DUMP_BUFF_LEN (PAGE_SIZE << 1)

static int memleak_detect_thread(void *arg)
{
	long ret = 0;
	long sleep_jiffies = MEMLEAK_DETECT_SLEEP_SEC;
	int i, type;

	do {
		char *dump_buff = NULL;

		current->state = TASK_INTERRUPTIBLE;
		ret = schedule_timeout(sleep_jiffies);
		if (ret) {
			sleep_jiffies = ret;
			continue;
		}
		sleep_jiffies = MEMLEAK_DETECT_SLEEP_SEC;

		dump_buff = (char *)vmalloc(DUMP_BUFF_LEN);
		if (!dump_buff) {
			pr_err("[%s] vmalloc dump_buff failed.\n", __func__);
			continue;
		}

#ifdef CONFIG_VMALLOC_DEBUG
		dump_vmalloc_debug(dump_buff, DUMP_BUFF_LEN);
#endif

#ifdef CONFIG_KMALLOC_DEBUG

		for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
			for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
				struct kmem_cache *s = kmalloc_caches[type][i];

				if (!s)
					continue;
				if (s->flags & SLAB_STORE_USER)
					dump_kmalloc_debug_info(s, i, dump_buff,
							DUMP_BUFF_LEN);
			}
		}

		for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
			for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
				struct kmem_cache *s = (struct kmem_cache *)atomic64_read(&kmalloc_debug_caches[type][i]);

				if (!s)
					continue;
				if (s->flags & SLAB_STORE_USER)
					dump_kmalloc_debug_info(s, i, dump_buff,
							DUMP_BUFF_LEN);
			}
		}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
		dump_ion_info(dump_buff, DUMP_BUFF_LEN);
#endif
		vfree(dump_buff);
	} while (!kthread_should_stop());

	return 0;
}

static inline void init_kmalloc_debug_caches(unsigned long flags)
{
	int i, type;
	struct kmem_cache *s;

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++) {
			if (atomic64_read(&kmalloc_debug_caches[type][i]))
				continue;

			s = create_kmalloc_debug_caches(kmalloc_debug_info[i].size,
					flags, type);

			/*
			 * Caches that are not of the two-to-the-power-of size.
			 * These have to be created immediately after the
			 * earlier power of two caches
			 */
			if (KMALLOC_MIN_SIZE <= 32 && !kmalloc_caches[type][1] && i == 6)
				s = create_kmalloc_debug_caches(kmalloc_debug_info[i].size,
						flags, type);
			if (KMALLOC_MIN_SIZE <= 64 && !kmalloc_caches[type][2] && i == 7)
				s = create_kmalloc_debug_caches(kmalloc_debug_info[i].size,
						flags, type);

			if (!s)
				break;

			atomic64_set(&kmalloc_debug_caches[type][i], (unsigned long)s);
		}
	}
	kmalloc_debug_enable = 1;
}

static ssize_t memleak_detect_thread_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[16];
	int len = 0;

	if (count > 15)
		count = 15;

	len = scnprintf(kbuf, count, "%d\n", memleak_detect_task ? 1 : 0);
	kbuf[len] = '\0';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf, len))
		return -EFAULT;

	*off += len;
	return len;
}

static ssize_t memleak_detect_thread_write(struct file *file, const char __user *buff,
		size_t len, loff_t *ppos)
{
	char kbuf[16] = {'0'};
	long val;
	int ret;

	if (len > 15)
		len = 15;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 10, &val);
	if (ret)
		return -EINVAL;

	mutex_lock(&debug_mutex);
	if (val > 0) {
		if (memleak_detect_task)
			goto out;

#ifdef CONFIG_VMALLOC_DEBUG
		enable_vmalloc_debug();
#endif
#ifdef CONFIG_KMALLOC_DEBUG
		init_kmalloc_debug_caches(SLAB_STORE_USER);
		kmalloc_debug_watermark_init();
#endif

		memleak_detect_task = kthread_create(memleak_detect_thread, NULL,
				"memleak_detect");
		if (IS_ERR(memleak_detect_task)) {
			pr_warn("[kmalloc_debug][vmalloc_debug] memleak_detect_init failed.\n");
			memleak_detect_task = NULL;
		} else
			wake_up_process(memleak_detect_task);
	} else if (memleak_detect_task) {
#ifdef CONFIG_VMALLOC_DEBUG
		disable_vmalloc_debug();
#endif
#ifdef CONFIG_KMALLOC_DEBUG
		kmalloc_debug_enable = 0;
#endif
		kthread_stop(memleak_detect_task);
		memleak_detect_task = NULL;
	}

out:
	mutex_unlock(&debug_mutex);
	return len;
}

static const struct file_operations memleak_detect_thread_operations = {
	.read		= memleak_detect_thread_read,
	.write          = memleak_detect_thread_write,
};

static int __init memleak_detect_init(void)
{
#ifdef CONFIG_VMALLOC_DEBUG
        enable_vmalloc_debug();
#endif

#ifdef CONFIG_KMALLOC_DEBUG
        init_kmalloc_debug_caches(SLAB_STORE_USER);
        kmalloc_debug_watermark_init();
#endif

	memleak_detect_task = kthread_create(memleak_detect_thread, NULL, "memleak_detect");
	if (IS_ERR(memleak_detect_task)) {
		pr_warn("[kmalloc_debug][vmalloc_debug] memleak_detect_init failed.\n");
		memleak_detect_task = NULL;
	} else
		wake_up_process(memleak_detect_task);

        return 0;
}
#else
static int __init memleak_detect_init(void)
{
        return 0;
}
#endif /* CONFIG_MEMLEAK_DETECT_THREAD */
module_init(memleak_detect_init);
#endif /* CONFIG_KMALLOC_DEBUG || CONFIG_VMALLOC_DEBUG */
#endif /* _SLUB_TRACK_ */
