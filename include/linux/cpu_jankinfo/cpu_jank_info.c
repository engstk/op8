// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */
#include "jank_base.h"
#include "jank_tasktrack.h"
#include "jank_hotthread.h"
#include "jank_topology.h"
#include "jank_loadindicator.h"
#include "jank_freq.h"
#include "jank_onlinecpu.h"
#include "jank_cputime.h"
#include "jank_cpuload.h"
#include "jank_cpuset.h"
#include "jank_version.h"
#include "jank_enable.h"
#include "jank_debug.h"
#include "jank_cpuloadmonitor.h"

static struct proc_dir_entry *d_task_info;
static struct proc_dir_entry *d_cpu_jank_info;

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
void android_vh_irqtime_account_process_tick_handler(
			void *unused,
			struct task_struct *p, struct rq *rq, int user_tick, int ticks)
{
	jankinfo_update_time_info(rq, p, ticks*TICK_NSEC);
}
#else
void android_vh_account_task_time_handler(void *unused,
			struct task_struct *p, struct rq *rq, int user_tick)
{
	jankinfo_update_time_info(rq, p, TICK_NSEC);
}
#endif

void android_vh_cpufreq_resolve_freq_handler(void *unused,
			struct cpufreq_policy *policy, unsigned int target_freq,
			unsigned int old_target_freq)
{
	jankinfo_update_freq_reach_limit_count(policy,
				old_target_freq, target_freq, DO_CLAMP);
}

void android_vh_cpufreq_fast_switch_handler(void *unused,
			struct cpufreq_policy *policy, unsigned int target_freq,
			unsigned int old_target_freq)
{
	jankinfo_update_freq_reach_limit_count(policy,
		old_target_freq, target_freq, DO_CLAMP | DO_INCREASE);
}

void android_vh_cpufreq_target_handler(void *unused,
			struct cpufreq_policy *policy, unsigned int target_freq,
			unsigned int old_target_freq)
{
	jankinfo_update_freq_reach_limit_count(policy,
		old_target_freq, target_freq, DO_CLAMP);
}

#ifdef JANK_USE_HOOK
static int jank_register_hook(void)
{
	int ret = 0;

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	REGISTER_TRACE_VH(android_vh_irqtime_account_process_tick,
			android_vh_irqtime_account_process_tick_handler);
#else
	REGISTER_TRACE_VH(android_vh_account_task_time,
			android_vh_account_task_time_handler);
#endif

	REGISTER_TRACE_VH(android_vh_cpufreq_resolve_freq,
			android_vh_cpufreq_resolve_freq_handler);
	REGISTER_TRACE_VH(android_vh_cpufreq_fast_switch,
			android_vh_cpufreq_fast_switch_handler);
	REGISTER_TRACE_VH(android_vh_cpufreq_target,
			android_vh_cpufreq_target_handler);

	return ret;
}

static int jank_unregister_hook(void)
{
	int ret = 0;

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	UNREGISTER_TRACE_VH(android_vh_irqtime_account_process_tick,
			android_vh_irqtime_account_process_tick_handler);
#else
	UNREGISTER_TRACE_VH(android_vh_account_task_time,
			android_vh_account_task_time_handler);
#endif

	UNREGISTER_TRACE_VH(android_vh_cpufreq_resolve_freq,
			android_vh_cpufreq_resolve_freq_handler);
	UNREGISTER_TRACE_VH(android_vh_cpufreq_fast_switch,
			android_vh_cpufreq_fast_switch_handler);
	UNREGISTER_TRACE_VH(android_vh_cpufreq_target,
			android_vh_cpufreq_target_handler);

	return ret;
}
#endif

#define JANK_INFO_DIR				"jank_info"
#define JANK_INFO_PROC_NODE			"cpu_jank_info"

static int __init jank_info_init(void)
{
	struct proc_dir_entry *proc_entry;

	cluster_init();
	jank_cpuload_init();
	jank_onlinecpu_reset();

	d_task_info = proc_mkdir(JANK_INFO_DIR, NULL);
	if (!d_task_info) {
		pr_err("create task_info fail\n");
		goto err_task_info;
	}

	d_cpu_jank_info = proc_mkdir(JANK_INFO_PROC_NODE, d_task_info);
	if (!d_cpu_jank_info) {
		pr_err("create cpu_jank_info fail\n");
		goto err_jank_info;
	}

	proc_entry = jank_enable_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create jank_info_enable fail\n");
		goto err_jank_info_enable;
	}

	proc_entry = jank_load_indicator_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create load_indicator fail\n");
		goto err_load_indicator;
	}

	proc_entry = jank_debug_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create debug fail\n");
		goto err_debug;
	}

	proc_entry = jank_tasktrack_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create task_track fail\n");
		goto err_task_track;
	}

	proc_entry = jank_cpuload_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create cpu_load fail\n");
		goto err_cpu_load;
	}

	proc_entry = jank_version_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create version fail\n");
		goto err_cpu_version;
	}

	proc_entry = jank_calcload_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create calcload fail\n");
		goto err_calcload;
	}

#if IS_ENABLED(CONFIG_JANK_CPUSET)
	proc_entry = jank_cpuset_threshold_proc_init(d_cpu_jank_info);
	if (!proc_entry) {
		pr_err("create cpuset_threshold fail\n");
		goto err_cpuset_threshold;
	}
#endif

#ifdef JANK_USE_HOOK
	jank_register_hook();
#endif
	tasktrack_init();
	jank_calcload_init();

	return 0;

#if IS_ENABLED(CONFIG_JANK_CPUSET)
err_cpuset_threshold:
	jank_calcload_proc_deinit(d_cpu_jank_info);
#endif

err_calcload:
	jank_version_proc_deinit(d_cpu_jank_info);

err_cpu_version:
	jank_cpuload_proc_deinit(d_cpu_jank_info);

err_cpu_load:
	jank_tasktrack_proc_deinit(d_cpu_jank_info);

err_task_track:
	jank_debug_proc_deinit(d_cpu_jank_info);

err_debug:
	jank_load_indicator_proc_deinit(d_cpu_jank_info);

err_load_indicator:
	jank_enable_proc_deinit(d_cpu_jank_info);

err_jank_info_enable:
	remove_proc_entry(JANK_INFO_PROC_NODE, d_task_info);

err_jank_info:
	remove_proc_entry(JANK_INFO_DIR, NULL);

err_task_info:
	return -ENOMEM;
}

void __exit __maybe_unused jank_info_exit(void)
{
#ifdef JANK_USE_HOOK
	jank_unregister_hook();
#endif
	tasktrack_deinit();
	jank_calcload_exit();

	jank_calcload_proc_deinit(d_cpu_jank_info);

#if IS_ENABLED(CONFIG_JANK_CPUSET)
	jank_cpuset_threshold_proc_deinit(d_cpu_jank_info);
#endif

	jank_version_proc_deinit(d_cpu_jank_info);
	jank_cpuload_proc_deinit(d_cpu_jank_info);

	jank_tasktrack_proc_deinit(d_cpu_jank_info);
	jank_debug_proc_deinit(d_cpu_jank_info);
	jank_load_indicator_proc_deinit(d_cpu_jank_info);
	jank_enable_proc_deinit(d_cpu_jank_info);

	remove_proc_entry(JANK_INFO_PROC_NODE, d_task_info);
	remove_proc_entry(JANK_INFO_DIR, NULL);
}

module_init(jank_info_init);
module_exit(jank_info_exit);

MODULE_DESCRIPTION("cpu_jankinfo");
MODULE_LICENSE("GPL v2");

