/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __PROCESS_MM_RECLAIM_H__
#define __PROCESS_MM_RECLAIM_H__

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
#include <linux/pagewalk.h>
#endif
#define PR_PASS		0
#define PR_SEM_OUT	1
#define PR_TASK_FG	2
#define PR_TIME_OUT	3
#define PR_ADDR_OVER	4
#define PR_FULL		5
#define PR_TASK_RUN	6
#define PR_TASK_DIE	7

#define RECLAIM_TIMEOUT_JIFFIES (HZ/3)
#define RECLAIM_PAGE_NUM 1024ul

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
/* 
 * qualcomm has define the sturct in mm.h,
 * mtk do not define it, used for transfer param during scan pages.
 */
struct reclaim_param {
	struct vm_area_struct *vma;
	/* Number of pages scanned */
	int nr_scanned;
	/* max pages to reclaim */
	int nr_to_reclaim;
	/* pages reclaimed */
	int nr_reclaimed;
	/* 
	 * flag that relcaim inactive pages only 
         */
	bool inactive_lru;
	/*
	 * the target reclaimed process
	 */
	struct task_struct *reclaimed_task;
};
#endif

extern int is_reclaim_should_cancel(struct mm_walk *walk);
extern int is_reclaim_addr_over(struct mm_walk *walk, unsigned long addr);
extern int __weak  create_process_reclaim_enable_proc(struct proc_dir_entry *parent);
#endif /* __PROCESS_MM_RECLAIM_H__ */
