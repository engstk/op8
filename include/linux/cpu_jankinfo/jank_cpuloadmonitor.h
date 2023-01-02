/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_CPULOADMONITOR_H__
#define __OPLUS_CPU_JANK_CPULOADMONITOR_H__

#include "jank_base.h"

void jank_calcload_init(void);
void jank_calcload_exit(void);
struct proc_dir_entry *jank_calcload_proc_init(struct proc_dir_entry *pde);
void jank_calcload_proc_deinit(struct proc_dir_entry *pde);


#endif  /* endif */
