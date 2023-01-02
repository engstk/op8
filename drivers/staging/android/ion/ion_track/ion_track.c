// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifdef CONFIG_DUMP_TASKS_MEM
#include <linux/sort.h>

#define ION_DEBUG_LOG_TAG "ion_debug"
#define BUFLEN(total, len) (total - len - 1)
#define ION_DEBUG_MIN_WATERMARK 220lu
#define ION_DEBUG_MAX_WATERMARK 650lu
#define ION_DEBUG_DUMP_STEP 30lu
#define MAX_TASK_NUM 124
#define MAX_HEAP_CNT 5

struct ion_heap_info {
	struct ion_heap *heap;
	unsigned long size;
	unsigned long jfs_max;
	unsigned long jfs_min;
	unsigned long jfs_sum;
	unsigned int count;
};

struct ion_buff_info {
	struct task_struct *tsk;
	unsigned long size;
	unsigned long jfs_max;
	unsigned long jfs_min;
	unsigned long jfs_sum;
	unsigned int count;
	struct ion_heap_info heaps[MAX_HEAP_CNT];
	int heap_cnt;
};

extern void dump_meminfo_to_logger(const char *tag, char *msg, size_t len);

static int ion_buff_cmp(const void *la, const void *lb)
{
	return ((struct ion_buff_info *)lb)->size - ((struct ion_buff_info *)la)->size;
}

static void ion_buff_swap(void *la, void *lb, int size)
{
	struct ion_buff_info l_tmp;

	memcpy(&l_tmp, la, size);
	memcpy(la, lb, size);
	memcpy(lb, &l_tmp, size);
}

static int array_add_ion_info(struct ion_buff_info *array, int *len,
		struct ion_buffer *entry)
{
	long start, end, pos;
	struct ion_buff_info *l;
	unsigned long age;
	struct task_struct *tsk;
	int i;
	struct ion_heap_info *heap;

	age = jiffies - entry->jiffies;
	start = -1;
	end = *len;

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		if (pos == end)
			break;

		tsk = array[pos].tsk;
		if (entry->tsk == tsk) {
			l = &array[pos];
			l->size += entry->size;
			l->count++;
			l->jfs_sum += age;
			if (age < l->jfs_min)
				l->jfs_min = age;
			if (age > l->jfs_max)
				l->jfs_max = age;

			for (i = 0; i < l->heap_cnt; i++) {
				heap = &l->heaps[i];

				if (heap->heap == entry->heap) {
					heap->jfs_sum += age;
					if (age < heap->jfs_min)
						heap->jfs_min = age;
					if (age >heap->jfs_max)
						heap->jfs_max = age;
					heap->count++;
					heap->size += entry->size;
					break;
				}
			}

			if ((i == l->heap_cnt) && (i < MAX_HEAP_CNT)) {
				heap = &l->heaps[l->heap_cnt];
				heap->heap = entry->heap;
				heap->jfs_sum = age;
				heap->jfs_min = age;
				heap->jfs_max = age;
				heap->size = entry->size;
				heap->count = 1;
				l->heap_cnt++;
			}
			return 0;
		}

		if (entry->tsk < tsk)
			end = pos;
		else
			start = pos;
	}

	if (*len >= MAX_TASK_NUM)
		return -ENOMEM;

	l = &array[pos];
	if (pos < *len)
		memmove(l + 1, l, (*len - pos) * sizeof(struct ion_buff_info));
	(*len)++;
	l->count = 1;
	l->tsk = entry->tsk;
	l->jfs_sum = age;
	l->jfs_min = age;
	l->jfs_max = age;
	l->size = entry->size;
	l->heap_cnt = 0;
	memset(&l->heaps[0], 0, sizeof(l->heaps));
	heap = &l->heaps[l->heap_cnt];
	heap->size = entry->size;
	heap->jfs_sum = age;
	heap->jfs_min = age;
	heap->jfs_max = age;
	heap->count = 1;
	heap->heap = entry->heap;
	l->heap_cnt++;
	return 0;
}

void dump_ion_info(char *dump_buff, int len)
{
	struct ion_device *dev = internal_dev;
	struct ion_heap_info *heap;
	struct rb_root *root;
	struct ion_buffer *entry, *entry_next;
	struct task_struct *tsk;
	int dump_buff_len = 0;
	static unsigned long last_ion_size = ION_DEBUG_MIN_WATERMARK;
	unsigned long ion_size;
	struct ion_buff_info *array;
	int buff_index = 0, i, j;
	int ret;
	unsigned long record_sum;

	if (!ion_cnt_enable) {
		pr_warn("[ion_debug] ion_cnt_enable is disabled.\n");
		return;
	}

	if (!dump_buff) {
		pr_warn("[ion_debug] dump_buff is NULL.\n");
		return;
	}

	ion_size = (unsigned long)atomic_long_read(&ion_total_size) >> 20;
	if (ion_size < last_ion_size) {
		if (last_ion_size < ION_DEBUG_MAX_WATERMARK)
			return;
	} else {
		/* 
		 * ION have many instantaneous value, so upate with step, it is
		 * differenet with kmalloc and vmalloc.
		 */
		last_ion_size += ION_DEBUG_DUMP_STEP;
	}

	if (!dev) {
		pr_warn("[ion_debug] internal_dev is NULL.\n");
		return;
	}

	array = vmalloc(sizeof(struct ion_buff_info) * MAX_TASK_NUM);
	if (!array) {
		pr_err("[ion_debug] vmalloc array failed.\n");
		return;
	}
	memset(array, 0, sizeof(struct ion_buff_info) * MAX_TASK_NUM);

	memset(dump_buff, 0, len);
	dump_buff_len = scnprintf(dump_buff + dump_buff_len,
			BUFLEN(len, dump_buff_len),
			"used %lu MB , show all buffer:\n", ion_size);

	root = &dev->buffers;
	mutex_lock(&dev->buffer_lock);
	if (RB_EMPTY_ROOT(root)){
		pr_err("[ion_debug] dev->buffers is empty.\n");
		mutex_unlock(&dev->buffer_lock);
		goto out;
	}

	rbtree_postorder_for_each_entry_safe(entry, entry_next, root, node) {
		if (unlikely(!entry->tsk))
			continue;

		ret = array_add_ion_info(array, &buff_index, entry);
		if (ret)
			break;
	}

	sort((void *)array, buff_index, sizeof(struct ion_buff_info),
			ion_buff_cmp, ion_buff_swap);

	record_sum = 0;
	for (i = 0; i < buff_index; i++) {
		struct ion_buff_info *ion_buff_i = &array[i];

		tsk = ion_buff_i->tsk;
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len),
				"%s %d %d %lu %u age %lu/%lu/%lu\n",
				tsk->comm, tsk->pid, tsk->tgid, ion_buff_i->size,
				ion_buff_i->count,
				ion_buff_i->jfs_min, ion_buff_i->jfs_sum / ion_buff_i->count,
				ion_buff_i->jfs_max);
		record_sum += ion_buff_i->size;

		for (j = 0; j < ion_buff_i->heap_cnt; j++) {
			heap = &ion_buff_i->heaps[j];
			dump_buff_len += scnprintf(dump_buff + dump_buff_len,
					BUFLEN(len, dump_buff_len),
					"%s %lu %u age %lu/%lu/%lu\n",
					heap->heap->name, heap->size, heap->count,
					heap->jfs_min, heap->jfs_sum / heap->count, heap->jfs_max);
		}
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len), "-\n");

		if (BUFLEN(len, dump_buff_len) == 0)
			break;
	}
	mutex_unlock(&dev->buffer_lock);

	dump_buff_len += scnprintf(dump_buff + dump_buff_len,
			BUFLEN(len, dump_buff_len), "record_sum %lu MB\n",
			record_sum >> 20);
	dump_buff[dump_buff_len++] = '\n';
	dump_meminfo_to_logger(ION_DEBUG_LOG_TAG, dump_buff, dump_buff_len);

out:
	vfree(array);
}
#else
void dump_ion_info(char *dump_buff, int len)
{
	pr_warn("[ion_debug] CONFIG_DUMP_TASKS_MEM is not config.\n");
}
#endif
EXPORT_SYMBOL(dump_ion_info);
