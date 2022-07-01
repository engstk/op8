/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2020 Oplus. All rights reserved.
*/

#ifndef _TASK_OVERTIME_H
#define _TASK_OVERTIME_H

#include <linux/kernel_stat.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>

/* abnormal task detection period*/
#define PERIOD (180)
#define MAX_ACCOUNT_NUM  (PERIOD * 250)

#define CPU_NUM (8)

struct task_time {
	pid_t       pid;
	pid_t       uid;
	u32         freq;
	u32         load;
	u64         exec_start;
	u64         end;
	u64         delta;
	char        comm[TASK_COMM_LEN];
};

struct kernel_task_overtime {
	unsigned int idx;
	struct task_time task[MAX_ACCOUNT_NUM];
};

struct abnormal_tsk_info {
	int   pid;
	int   uid;
	char  comm[TASK_COMM_LEN];
	u64   runtime;
	u32   avg_freq;
	u32   avg_load;
	long  date;
};

void get_all_atd_info(int *cnt, struct abnormal_tsk_info *val);

#endif /* _TASK_OVERTIME_H */
