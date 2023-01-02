// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifdef OPLUS_FEATURE_SCHED_ASSIST
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM

#include <linux/workqueue.h>
#include <linux/sched.h>

int is_uxwork(struct work_struct *work)
{
	if(!sysctl_sched_assist_enabled)
		return 0;

	return work->ux_work;
}

inline int set_uxwork(struct work_struct *work)
{
	if(!sysctl_sched_assist_enabled)
		return false;
	return work->ux_work = 1;
}

inline int unset_uxwork(struct work_struct *work)
{
	if(!sysctl_sched_assist_enabled)
		return false;
	return work->ux_work = 0;
}

inline void set_ux_worker_task(struct task_struct *task)
{
	task->ux_state = 1;
}

inline void reset_ux_worker_task(struct task_struct *task)
{
	task->ux_state = 0;
}

#endif /* CONFIG_OPLUS_SYSTEM_KERNEL_QCOM */
#endif
