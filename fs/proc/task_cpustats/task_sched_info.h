/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Oplus. All rights reserved.
*/

#ifndef _TASK_SCHED_INFO_H
#define _TASK_SCHED_INFO_H

#include <linux/cpufreq.h>

#define sched_err(fmt, ...) \
        printk(KERN_ERR "[SCHED_INFO_ERR][%s]"fmt, __func__, ##__VA_ARGS__)

enum {
	task_sched_info_running = 0,
	task_sched_info_runnable,
	task_sched_info_IO,
	task_sched_info_D,
	task_sched_info_S,
	task_sched_info_freq,
	task_sched_info_freq_limit,
	task_sched_info_isolate,
	task_sched_info_backtrace,
};

enum {
	other_runnable = 1,
	running_runnable,
};

enum {
	cpu_unisolate = 0,
	cpu_isolate,
};

struct task_sched_info {
	u64 sched_info_one;
	u64 sched_info_two;
};

extern void update_task_sched_info(struct task_struct *p, u64 delay, int type, int cpu);
extern void update_freq_info(struct cpufreq_policy *policy);
extern void update_freq_limit_info(struct cpufreq_policy *policy);
extern void update_cpu_isolate_info(int cpu, u64 type);
extern void update_wake_tid(struct task_struct *p, struct task_struct *cur, unsigned int type);
extern void update_running_start_time(struct task_struct *prev, struct task_struct *next);
extern void sched_action_trig(void);
extern void get_target_thread_pid(struct task_struct *p);

extern bool ctp_send_message;

#endif /* _TASK_SCHED_INFO_H */
