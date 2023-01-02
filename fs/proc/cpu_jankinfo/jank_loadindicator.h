/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_LOADINDICATOR_H__
#define __OPLUS_CPU_JANK_LOADINDICATOR_H__

#include "jank_base.h"
#include "jank_cputime.h"

struct score {
	u32 total;
	u32 def;
	u32 fg;
	u32 background;
	u32 topapp;
};

struct load_record {
	u64 total;
	u64 def;
	u64 fg;
	u64 background;
	u64 topapp;
};

#define HIGH_LOAD_TOTAL					95

#define HIGH_LOAD_DEFAULT				50			/* SYS */
#define HIGH_LOAD_FOREGROUND			50
#define HIGH_LOAD_BACKGROUND			50
#define HIGH_LOAD_TOPAPP				80

#define MONITOR_WIN_CNT					10

void jank_loadindicator_update_win(struct cputime *cputime,
			struct task_struct *p, u64 now);
struct proc_dir_entry *jank_load_indicator_proc_init(
			struct proc_dir_entry *pde);
void jank_load_indicator_proc_deinit(
			struct proc_dir_entry *pde);
#endif  /* endif */
