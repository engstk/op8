/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_CPULOAD_H__
#define __OPLUS_CPU_JANK_CPULOAD_H__

#include "jank_base.h"

u32 get_cpu_load(u32 win_cnt, struct cpumask *mask);
u32 get_cpu_load32(u32 win_cnt, struct cpumask *mask);
void jankinfo_update_time_info(struct rq *rq,
				struct task_struct *p, u64 time);
u32 get_cpu_load(u32 win_cnt, struct cpumask *mask);
struct proc_dir_entry *jank_cpuload_proc_init(
			struct proc_dir_entry *pde);
void jank_cpuload_proc_deinit(struct proc_dir_entry *pde);
void jank_cpuload_init(void);

#endif  /* endif */
