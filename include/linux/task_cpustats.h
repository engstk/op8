/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_TASK_CPUSTATS_H
#define _OPLUS_TASK_CPUSTATS_H
#include <linux/kernel_stat.h>
#include <linux/cpufreq.h>

#define MAX_CTP_WINDOW (10 * NSEC_PER_SEC / TICK_NSEC)

struct task_cpustat {
	pid_t pid;
	pid_t tgid;
	enum cpu_usage_stat type;
	int freq;
	unsigned long begin;
	unsigned long end;
	char comm[TASK_COMM_LEN];
};

struct kernel_task_cpustat {
	unsigned int idx;
	struct task_cpustat cpustat[MAX_CTP_WINDOW];
};

DECLARE_PER_CPU(struct kernel_task_cpustat, ktask_cpustat);

extern unsigned int sysctl_task_cpustats_enable;
extern void account_task_time(struct task_struct *p, unsigned int ticks,
		enum cpu_usage_stat type);
#endif
