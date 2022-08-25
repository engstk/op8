/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_STATUS_H_
#define _OPLUS_SCHED_STATUS_H_

enum task_stats_type {
	TST_SLEEP = 0,           /* update sleeping time when enq-wakeup */
	TST_RUNNABLE,            /* update runnable time when enq_deq */
	TST_EXEC,                /* update exec time when deq-sleep */
	TST_SCHED_TYPE_TATOL,    /* total type*/
};

#define TASK_INFO_SAMPLE (4)
struct task_info {
	u64 sa_info[TST_SCHED_TYPE_TATOL][TASK_INFO_SAMPLE];
	bool im_small;
};

extern int sysctl_sched_impt_tgid;

#endif /* _OPLUS_SCHED_STATUS_H_ */
