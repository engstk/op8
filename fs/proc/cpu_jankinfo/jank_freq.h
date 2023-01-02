/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_FREQ_H__
#define __OPLUS_CPU_JANK_FREQ_H__

#include "jank_base.h"
#include "jank_cputime.h"

#define DO_CLAMP		BIT(0)
#define DO_INCREASE		BIT(1)

void jank_currfreq_update_win(u64 now);
void jankinfo_update_freq_reach_limit_count(
			struct cpufreq_policy *policy,
			u32 old_target_freq, u32 new_target_freq, u32 flags);

void jank_curr_freq_show(struct seq_file *m,
			u32 win_idx, u64 now);
void jank_burst_freq_show(struct seq_file *m,
			u32 win_idx, u64 now);
void jank_increase_freq_show(struct seq_file *m,
			u32 win_idx, u64 now);
#endif  /* endif */
