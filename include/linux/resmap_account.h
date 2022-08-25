/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef RESMAP_ACCOUNT_H
#define RESMAP_ACCOUNT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_OPLUS_HEALTHINFO
#include <soc/oplus/oplus_healthinfo.h>
#endif

#define VM_UNMAPPED_AREA_RESERVED 0x2

#define STACK_RLIMIT_OVERFFLOW (32<<20)

#define NS_PER_SEC (1000000000LLU)
#define TRIGGER_TIME_NS (300*NS_PER_SEC)

enum resmap_item {
	RESMAP_ACTION,
	RESMAP_SUCCESS,
	RESMAP_FAIL,
	RESMAP_TEXIT,
	RESMAP_ITEMS
};

struct resmap_event_state {
	unsigned int event[RESMAP_TEXIT];
};

DECLARE_PER_CPU(struct resmap_event_state, resmap_event_states);

extern int reserved_area_enable;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
extern unsigned long gpu_compat_high_limit_addr;
#endif
#define RESERVE_VMAP_AREA_SIZE (SZ_32M + SZ_64M)
#define RESERVE_AREA_ALIGN_SIZE SZ_2M

/* maybe somebody use MAP_BACKUP_CREATE, that is a mistake of create a
 * reserved area in this case; to avoid it check flags andd addr same time.
 * Todo will optimize it soon.
 */
#define RESERVE_VMAP_ADDR 0xDEADDEAD

/* create reserved area depend on do_reserve_mmap value,
 * but need check the env, only 32bit process can used reserved area
 */
#define DONE_RESERVE_MMAP 0xDE
#define DOING_RESERVE_MMAP 0xDA

static inline int check_reserve_mmap_doing(struct mm_struct *mm)
{
	return (mm && (mm->do_reserve_mmap == DOING_RESERVE_MMAP));
}

static inline void reserve_mmap_doing(struct mm_struct *mm)
{
	mm->do_reserve_mmap = DOING_RESERVE_MMAP;
}

static inline void reserve_mmap_done(struct mm_struct *mm)
{
	mm->do_reserve_mmap = DONE_RESERVE_MMAP;
}

static inline int is_backed_addr(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	return (mm && mm->reserve_vma &&
			start >= mm->reserve_vma->vm_start &&
			end <= mm->reserve_vma->vm_end);
}

static inline int start_is_backed_addr(struct mm_struct *mm,
		unsigned long start)
{
	return (mm && mm->reserve_vma &&
			start >= mm->reserve_vma->vm_start &&
			start < mm->reserve_vma->vm_end);
}

static inline int check_general_addr(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	unsigned long range_start, range_end;

	if (mm && mm->reserve_vma) {
		range_start = mm->reserve_vma->vm_start;
		range_end = mm->reserve_vma->vm_end;

		if ((start < range_start) && (end <= range_start))
			return 1;
		if ((start >= range_end) && (end > range_end))
			return 1;
		return 0;
	}

	return 1;
}

static inline int check_valid_reserve_addr(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	unsigned long range_start, range_end;

	if (mm && mm->reserve_vma) {
		range_start = mm->reserve_vma->vm_start;
		range_end = mm->reserve_vma->vm_end;

		if ((start < range_start) && (end <= range_start))
			return 1;
		if ((start >= range_end) && (end > range_end))
			return 1;
		if (start >= range_start && end <= range_end)
			return 1;
		return 0;
	}

	return 1;
}

static inline void __count_resmap_event(enum resmap_item item)
{
	raw_cpu_inc(resmap_event_states.event[item]);
}

static inline void count_resmap_event(enum resmap_item item)
{
	this_cpu_inc(resmap_event_states.event[item]);
}

extern int svm_oom_pid;
extern unsigned long svm_oom_jiffies;
extern int rlimit_svm_log;

extern void init_reserve_mm(struct mm_struct* mm);
extern inline unsigned long vm_mmap_pgoff_with_check(struct file *file,
		unsigned long addr, unsigned long len, unsigned long prot,
		unsigned long flags, unsigned long pgoff);
extern void exit_reserved_mmap(struct mm_struct *mm);
extern int dup_reserved_mmap(struct mm_struct *mm, struct mm_struct *oldmm, struct kmem_cache *vm_area_cachep);
extern int reserved_area_checking(struct mm_struct *mm, unsigned long *vm_flags, unsigned long flags, unsigned long addr, unsigned long len);
extern void trigger_stack_limit_changed(long value);
extern int create_reserved_area_enable_proc(struct proc_dir_entry *parent);
extern void trigger_svm_oom_event(struct mm_struct *mm, bool brk_risk, bool is_locked);
#endif
