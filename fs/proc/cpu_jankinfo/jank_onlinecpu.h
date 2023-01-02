/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_ONLINECPU_H__
#define __OPLUS_CPU_JANK_ONLINECPU_H__

#include "jank_base.h"

struct nr {
	u64 timestamp;
	u32 nb;
	struct cpumask mask;
};

struct number_cpus {
	u64 last_update_time;
	u32 last_index;
	struct nr nr[JANK_WIN_CNT];
};

void jank_onlinecpu_reset(void);
void jank_onlinecpu_update(const struct cpumask *mask);
void jank_onlinecpu_show(struct seq_file *m,
			u32 win_idx, u64 now);

#endif  /* endif */
