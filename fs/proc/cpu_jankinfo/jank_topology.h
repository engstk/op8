/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_TOPOLOGY_H__
#define __OPLUS_CPU_JANK_TOPOLOGY_H__

#include "jank_base.h"

struct cluster_info {
	u32 start_cpu;
	u32 cpu_nr;
};

extern struct cluster_info cluster[];
extern u32 cluster_num;
extern struct cpumask all_cpu;
extern struct cpumask silver_cpu;
extern struct cpumask gold_cpu;

void cluster_init(void);
u32 get_start_cpu(u32 cpu);
bool is_cluster_cpu(u32 cpu);

#endif  /* endif */
