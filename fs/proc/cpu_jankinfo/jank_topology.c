// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <uapi/linux/sched/types.h>

#include "jank_topology.h"

struct cluster_info cluster[CPU_NUMS];
u32 cluster_num;
struct cpumask all_cpu;
struct cpumask silver_cpu;
struct cpumask gold_cpu;

void cluster_init(void)
{
	u32 i, j = -1;
	struct cpufreq_policy *policy;

	cluster_num = 0;
	memset(&cluster, 0, sizeof(struct cluster_info) * CPU_NUMS);

	for (i = 0; i < CPU_NUMS; i++) {
		policy = cpufreq_cpu_get_raw(i);
		if (!policy)
			continue;

		if (policy->cpu == i) {
			cluster[++j].start_cpu = i;
			cluster_num++;
		}
		cpumask_set_cpu(i, &all_cpu);
		cluster[j].cpu_nr++;

		if (policy->cpu == 0)
			cpumask_set_cpu(i, &silver_cpu);
		else
			cpumask_set_cpu(i, &gold_cpu);
	}
}

u32 get_start_cpu(u32 cpu)
{
	u32 cluster_id, start_cpu, end_cpu;

	for (cluster_id = 0; cluster_id < cluster_num; cluster_id++) {
		start_cpu = cluster[cluster_id].start_cpu;
		end_cpu = start_cpu + cluster[cluster_id].cpu_nr;
		if (start_cpu <= cpu && end_cpu > cpu)
			return start_cpu;
	}
	return 0;
}

bool is_cluster_cpu(u32 cpu)
{
	u32 cluster_id, start_cpu;

	for (cluster_id = 0; cluster_id < cluster_num; cluster_id++) {
		start_cpu = cluster[cluster_id].start_cpu;
		if (cpu == start_cpu)
			return true;
	}
	return false;
}

