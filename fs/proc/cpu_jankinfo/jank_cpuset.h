/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_CPUSET_H__
#define __OPLUS_CPU_JANK_CPUSET_H__

#define DEBUG_CPUSET
#define CPUSTE_PARA_CNT			6

#ifdef DEBUG_CPUSET
/* bg:cpu0~7 */
#define S_DSTATE_THRESHOLD		2
#define S_SILVER_USAGE			60
#define S_GOLD_USAGE			100

/* bg:cpu0~3 */
#define C_DSTATE_THRESHOLD		1
#define C_SILVER_USAGE			40
#define C_GOLD_USAGE			80

#else
/* bg:cpu0~7 */
#define S_DSTATE_THRESHOLD		100
#define S_SILVER_USAGE			90
#define S_GOLD_USAGE			60

/* bg:cpu0~3 */
#define C_DSTATE_THRESHOLD		40
#define C_SILVER_USAGE			60
#define C_GOLD_USAGE			90
#endif

struct proc_dir_entry *jank_cpuset_threshold_proc_init(
			struct proc_dir_entry *pde);
void jank_cpuset_threshold_proc_deinit(struct proc_dir_entry *pde);
void jank_cpuset_adjust(struct task_track_info *trace_info);

#endif  /* endif */
