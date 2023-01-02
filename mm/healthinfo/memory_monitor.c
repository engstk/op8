// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include <soc/oplus/healthinfo.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <../../mm/internal.h>
#include  <linux/healthinfo/memory_monitor.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>


#ifdef CONFIG_OPLUS_HEALTHINFO
struct alloc_wait_para allocwait_para =
{
.ux_alloc_wait.max_ms = 0,
.ux_alloc_wait.high_cnt = 0,
.ux_alloc_wait.low_cnt = 0,
.ux_alloc_wait.total_ms = 0,
.ux_alloc_wait.total_cnt = 0,
.fg_alloc_wait.max_ms = 0,
.fg_alloc_wait.high_cnt = 0,
.fg_alloc_wait.low_cnt = 0,
.fg_alloc_wait.total_ms = 0,
.fg_alloc_wait.total_cnt = 0,
.total_alloc_wait.max_ms = 0,
.total_alloc_wait.high_cnt = 0,
.total_alloc_wait.low_cnt = 0,
.total_alloc_wait.total_ms = 0,
.total_alloc_wait.total_cnt = 0
};
struct ion_wait_para ionwait_para =
{
.ux_ion_wait.max_ms = 0,
.ux_ion_wait.high_cnt = 0,
.ux_ion_wait.low_cnt = 0,
.ux_ion_wait.total_ms = 0,
.ux_ion_wait.total_cnt = 0,
.fg_ion_wait.max_ms = 0,
.fg_ion_wait.high_cnt = 0,
.fg_ion_wait.low_cnt = 0,
.fg_ion_wait.total_ms = 0,
.fg_ion_wait.total_cnt = 0,
.total_ion_wait.max_ms = 0,
.total_ion_wait.high_cnt = 0,
.total_ion_wait.low_cnt = 0,
.total_ion_wait.total_ms = 0,
.total_ion_wait.total_cnt = 0
};
extern bool ohm_memmon_ctrl;
extern bool ohm_memmon_logon;
extern bool ohm_memmon_trig;
extern void ohm_action_trig(int type);

extern bool ohm_ionmon_ctrl;
extern bool ohm_ionmon_logon;
extern bool ohm_ionmon_trig;

#else
static bool ohm_memmon_ctrl = false;
static bool ohm_memmon_logon = false;
static bool ohm_memmon_trig = false;

static bool ohm_ionmon_ctrl = false;
static bool ohm_ionmon_logon = false;
static bool ohm_ionmon_trig = false;

void ohm_action_trig(int type)
{
        return;
}
#endif

static int alloc_wait_h_ms = 500;
static int alloc_wait_l_ms = 100;
static int alloc_wait_log_ms = 1000;
static int alloc_wait_trig_ms = 10000;

static int ion_wait_h_ms = 500;
static int ion_wait_l_ms = 100;
static int ion_wait_log_ms = 1000;
static int ion_wait_trig_ms = 10000;

#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif
void memory_alloc_monitor(gfp_t gfp_mask, unsigned int order, u64 wait_ms)
{
	struct long_wait_record *plwr;
	struct timespec64 ts;
	u32 index;
	int fg = 0;
	int ux = 0;
        if (unlikely(!ohm_memmon_ctrl))
		return;
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	ux = test_task_ux(current);
#endif
	fg = current_is_fg();
	if (ux) {
		if (wait_ms > allocwait_para.ux_alloc_wait.max_ms ) {
			allocwait_para.ux_alloc_wait.max_ms = wait_ms;
			allocwait_para.ux_alloc_wait_max_order = order;
		}
		if (wait_ms >= alloc_wait_h_ms) {
			allocwait_para.ux_alloc_wait.high_cnt++;
		}else if (wait_ms >= alloc_wait_l_ms){
			allocwait_para.ux_alloc_wait.low_cnt++;
		}
	}
	if (fg) {
		if (wait_ms > allocwait_para.fg_alloc_wait.max_ms ) {
			allocwait_para.fg_alloc_wait.max_ms = wait_ms;
			allocwait_para.fg_alloc_wait_max_order = order;
		}
		if (wait_ms >= alloc_wait_h_ms) {
			allocwait_para.fg_alloc_wait.high_cnt++;
		}else if (wait_ms >= alloc_wait_l_ms){
			allocwait_para.fg_alloc_wait.low_cnt++;
		}
	}
	if (wait_ms > allocwait_para.total_alloc_wait.max_ms) {
			allocwait_para.total_alloc_wait.max_ms = wait_ms;
			allocwait_para.total_alloc_wait_max_order = order;
	}

	if(wait_ms >= alloc_wait_h_ms) {
		allocwait_para.total_alloc_wait.high_cnt++;
		if (ohm_memmon_logon && (wait_ms >= alloc_wait_log_ms)) {
			ohm_debug("[alloc_wait / %s] long, order %d, wait %lld ms!\n", (fg ? "fg":"bg"), order, wait_ms);
			warn_alloc(gfp_mask, NULL,"page allocation stalls for %lld ms, order: %d",wait_ms, order);
		}
		if (ohm_memmon_trig && wait_ms >= alloc_wait_trig_ms) {
			/* Trig Uevent */
			ohm_action_trig(OHM_MEM_MON);
		}
	}else if (wait_ms >= alloc_wait_l_ms) {
		allocwait_para.total_alloc_wait.low_cnt++;
	}

	if (unlikely(wait_ms >= alloc_wait_l_ms)) {
		index = (u32)atomic_inc_return(&allocwait_para.lwr_index);
		plwr = &allocwait_para.last_n_lwr[index & LWR_MASK];
		plwr->pid = (u32)current->pid;
		plwr->priv = order;

		ktime_get_real_ts64(&ts);
		plwr->timestamp = (u64)ts.tv_sec;
		plwr->timestamp_ns = (u64)ts.tv_nsec;

		plwr->ms = wait_ms;
	}
}

void ionwait_monitor(u64 wait_ms)
{
	struct long_wait_record *plwr;
	struct timespec64 ts;
	u32 index;
	int fg = 0;
	int ux = 0;
	if (unlikely(!ohm_ionmon_ctrl))
		return;
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	ux = test_task_ux(current);
#endif
	fg = current_is_fg();
	if (ux) {
		if (wait_ms >= ion_wait_h_ms) {
			ionwait_para.ux_ion_wait.high_cnt++;
		} else if (wait_ms >= ion_wait_l_ms) {
			ionwait_para.ux_ion_wait.low_cnt++;
		}
	}
	if (fg) {
		if (wait_ms >= ion_wait_h_ms) {
			ionwait_para.fg_ion_wait.high_cnt++;
		} else if (wait_ms >= ion_wait_l_ms) {
			ionwait_para.fg_ion_wait.low_cnt++;
		}
	}
	if (wait_ms > ionwait_para.total_ion_wait.max_ms) {
		ionwait_para.total_ion_wait.max_ms = wait_ms;
	}
	if (wait_ms >= ion_wait_h_ms) {
		ionwait_para.total_ion_wait.high_cnt++;
		if (ohm_ionmon_logon && (wait_ms >= ion_wait_log_ms)) {
			ohm_debug("[ion_wait / %s] long, wait %lld ms!\n", (fg ? "fg":"bg"), wait_ms);
		}
		if (ohm_ionmon_trig && wait_ms >= ion_wait_trig_ms) {
			/* Trig Uevent */
			ohm_action_trig(OHM_ION_MON);
		}
	}else if (wait_ms >= ion_wait_l_ms) {
    		ionwait_para.total_ion_wait.low_cnt++;
	}

	if (unlikely(wait_ms >= ion_wait_l_ms)) {
		index = (u32)atomic_inc_return(&ionwait_para.lwr_index);
		plwr = &ionwait_para.last_n_lwr[index & LWR_MASK];
		plwr->pid = (u32)current->pid;

		ktime_get_real_ts64(&ts);
		plwr->timestamp = (u64)ts.tv_sec;
		plwr->timestamp_ns = (u64)ts.tv_nsec;

		plwr->ms = wait_ms;
	}
}

module_param_named(ion_wait_h_ms, ion_wait_h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ion_wait_l_ms, ion_wait_l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ion_wait_log_ms, ion_wait_log_ms, int, S_IRUGO | S_IWUSR);

