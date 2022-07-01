// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _VA_FEATURE_NODE_
#define _VA_FEATURE_NODE_
#include <linux/random.h>
#include <linux/reserve_area.h>

static ssize_t proc_va_feature_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct task_struct *task;
	struct mm_struct *mm;
	char buffer[32];
	int ret;
	unsigned long heapsize;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	ret = -EINVAL;
	mm = get_task_mm(task);
	if (mm) {
		heapsize = (mm->zygoteheap_in_MB > ZYGOTE_HEAP_DEFAULT_SIZE) ? \
			   mm->zygoteheap_in_MB : ZYGOTE_HEAP_DEFAULT_SIZE;

		ret = snprintf(buffer, sizeof(buffer), "0x%lx\n",
				(heapsize & HEAP_SIZE_MASK) + mm->va_feature);
		if (ret > 0)
			ret = simple_read_from_buffer(buf, count, ppos,
					buffer, ret);
		mmput(mm);
	}
	put_task_struct(task);

	return ret;
}

static ssize_t proc_va_feature_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct task_struct *task;
	struct mm_struct *mm;
	int ret;
	unsigned int heapsize;
	unsigned int value;

	ret = kstrtouint_from_user(buf, count, 16, &value);
	if (ret)
		return ret;
	heapsize = value & HEAP_SIZE_MASK;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	if (!test_ti_thread_flag(task_thread_info(task), TIF_32BIT)) {
		put_task_struct(task);
		return -ENOTTY;
	}

	mm = get_task_mm(task);
	if (mm) {
		unsigned long old_mmap_base = mm->mmap_base;

		mm->va_feature = get_va_feature_value(value & ~HEAP_SIZE_MASK);
		if (mm->va_feature & ANTI_FRAGMENT_AREA) {
			mm->va_feature_rnd = (ANTI_FRAGMENT_AREA_BASE_SIZE +
					(get_random_long() % ANTI_FRAGMENT_AREA_MASK));
			mm->va_feature_rnd &= ANTI_FRAGMENT_AREA_ALIGN;
			special_arch_pick_mmap_layout(mm);
		}

		if ((mm->va_feature & ZYGOTE_HEAP) &&
				(mm->zygoteheap_in_MB == 0))
			mm->zygoteheap_in_MB = heapsize;

		if (mm->va_feature & ANTI_FRAGMENT_AREA)
			pr_info("%s (%d): rnd val is 0x%llx, mmap_base 0x%llx -> 0x%llx\n",
					current->group_leader->comm, current->pid,
					mm->va_feature_rnd, old_mmap_base, mm->mmap_base);
		mmput(mm);
	}
	put_task_struct(task);

	return count;
}

static const struct file_operations proc_va_feature_operations = {
	.read           = proc_va_feature_read,
	.write          = proc_va_feature_write,
};
#endif
