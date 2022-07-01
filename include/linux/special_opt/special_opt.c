// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/sched.h>
#include <linux/reciprocal_div.h>
#include <../kernel/sched/sched.h>
#include "special_opt.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_UIFRIST_HEAVYLOAD)
#include <trace/hooks/sched.h>
static int sysctl_cpu_multi_thread = 0;
static int ux_prefer_cpu[NR_CPUS] = { 0 };
module_param_named(enable, sysctl_cpu_multi_thread, uint, 0644);
#endif  /* IS_ENABLED(CONFIG_OPLUS_FEATURE_UIFRIST_HEAVYLOAD) */

bool specopt_skip_balance(void)
{
	return (1 == sysctl_cpu_multi_thread) ? true : false;
}

bool is_heavy_load_task(struct task_struct *p)
{
	int cpu;
	unsigned long thresh_load;
	struct reciprocal_value spc_rdiv = reciprocal_value(100);
	if (!sysctl_cpu_multi_thread || !p)
		return false;
	cpu = task_cpu(p);
	thresh_load = capacity_orig_of(cpu) * HEAVY_LOAD_SCALE;
	if (task_util(p) > reciprocal_divide(thresh_load, spc_rdiv))
		return true;
	return false;
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_UIFRIST_HEAVYLOAD)

static int ux_init_cpu_data(void)
{
	int i = 0;
	int min_cpu = 0, ux_cpu = 0;

	for (; i < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = -1;
	}

	ux_cpu = cpumask_weight(topology_core_cpumask(min_cpu));
	if (ux_cpu == 0) {
		pr_err("failed to init ux cpu data\n");
		return -1;
	}

	for (i = 0; i < nr_cpu_ids && ux_cpu < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = ux_cpu++;
	}

	return 0;
}

static bool test_ux_task_cpu(int cpu)
{
	return (cpu >= ux_prefer_cpu[0]);
}

static void check_preempt_wakeup_handler(void *data, struct task_struct *p,
				  int *ignore)
{
	if (sysctl_cpu_multi_thread == 0)
		return;

	if (is_heavy_load_task(p))
		*ignore = 1;
}

static void check_preempt_tick_handler(void *data, struct task_struct *p,
				unsigned long *ideal_runtime)
{
	if (sysctl_cpu_multi_thread == 0)
		return;

	if (is_heavy_load_task(p))
		*ideal_runtime = HEAVY_LOAD_RUNTIME;
}

static void find_best_target_handler(void *data, struct task_struct *p, int cpu,
			      int *ignore)
{
	if (sysctl_cpu_multi_thread == 0)
		return;

	if (is_heavy_load_task(p)) {
		if (!test_ux_task_cpu(cpu))
			*ignore = 1;
	} else {
		if (test_ux_task_cpu(cpu))
			*ignore = 1;
	}
}

static void cpupri_find_fitness_handler(void *data, struct task_struct *p, struct cpumask *lowest_mask)
{
	unsigned int cpu;
	if (sysctl_cpu_multi_thread == 0)
		return;

	if (!lowest_mask || cpumask_empty(lowest_mask)) {
		return;
	}

	cpu = cpumask_first(lowest_mask);
	while (cpu < nr_cpu_ids) {
		if (test_ux_task_cpu(cpu)) {
			cpumask_clear_cpu(cpu, lowest_mask);
		}

		cpu = cpumask_next(cpu, lowest_mask);
	}
}

static int register_vendor_hooks()
{
	int rc = 0;
	rc = register_trace_android_rvh_check_preempt_wakeup(
		check_preempt_wakeup_handler, NULL);
	if (rc != 0) {
		pr_err("uifirst: register_trace_android_vh_scheduler_tick failed! rc=%d\n",
		       rc);
		return rc;
	}

	rc = register_trace_android_rvh_check_preempt_tick(
		check_preempt_tick_handler, NULL);
	if (rc != 0) {
		pr_err("uifirst: register_trace_android_vh_scheduler_tick failed! rc=%d\n",
		       rc);
		return rc;
	}

	rc = register_trace_android_rvh_find_best_target(
		find_best_target_handler, NULL);
	if (rc != 0) {
		pr_err("uifirst: register_trace_android_vh_scheduler_tick failed! rc=%d\n",
		       rc);
		return rc;
	}

	rc = register_trace_android_rvh_cpupri_find_fitness(
		cpupri_find_fitness_handler, NULL);
	if (rc != 0) {
		pr_err("uifirst: register_trace_android_rvh_cpupri_find_fitness failed! rc=%d\n",
		       rc);
		return rc;
	}
	return 0;
}

static int __init init_uifirst_sepecial_opt(void)
{
	int rc;

	rc = ux_init_cpu_data();
	if (rc != 0) {
		return rc;
	}

	rc = register_vendor_hooks();
	if (rc != 0) {
		return rc;
	}

	return 0;
}

device_initcall(init_uifirst_sepecial_opt);
MODULE_LICENSE("GPL v2");
#endif  /* IS_ENABLED(CONFIG_OPLUS_FEATURE_UIFRIST_HEAVYLOAD) */
