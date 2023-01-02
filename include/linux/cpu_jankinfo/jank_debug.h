/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_DEBUG_H__
#define __OPLUS_CPU_JANK_DEBUG_H__

#include "jank_base.h"

struct proc_dir_entry *jank_debug_proc_init(
			struct proc_dir_entry *pde);
void jank_debug_proc_deinit(
			struct proc_dir_entry *pde);

#endif  /* endif */
