// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/topology.h>
#include <linux/version.h>
#include <../kernel/sched/sched.h>

#include "sched_assist_common.h"
#include "sched_assist_slide.h"

#define UX_LOAD_WINDOW 8000000
#define DEFAULT_INPUT_BOOST_DURATION 1000
u64 ux_task_load[NR_CPUS] = {0};
u64 ux_load_ts[NR_CPUS] = {0};

int sysctl_slide_boost_enabled = 0;
int sysctl_boost_task_threshold = 51;
int sysctl_frame_rate = 60;

extern bool task_is_sf_group(struct task_struct *p);
u64 tick_change_sf_load(struct task_struct *tsk , u64 timeline)
{
	char comm_now[TASK_COMM_LEN];
	int len = 0;
	int i = 0;
	u64 sf_sum_load = 0;

	if(!tsk || !task_is_sf_group(tsk))
		return timeline;

	memset(comm_now, '\0', sizeof(comm_now));
	strcpy(comm_now, tsk->comm);

	len = strlen(comm_now);
	for(i = 0; i < SF_GROUP_COUNT ;i++) {
		if (!strncmp(comm_now, sf_target[i].val, len))
			sf_target[i].ux_load = timeline;
		sf_sum_load += sf_target[i].ux_load;
	}

	sched_assist_systrace(sf_sum_load, "sf_load_%d", task_cpu(tsk));
	return sf_sum_load;
}

void sched_assist_adjust_slide_param(unsigned int *maxtime) {
	/*give each scene with default boost value*/
	if (sched_assist_scene(SA_SLIDE)) {
		if (sysctl_frame_rate <= 90)
			*maxtime = 5;
		else if (sysctl_frame_rate <= 120)
			*maxtime = 4;
		else
			*maxtime = 3;
	} else if (sched_assist_scene(SA_INPUT)) {
		if (sysctl_frame_rate <= 90)
			*maxtime = 8;
		else if (sysctl_frame_rate <= 120)
			*maxtime = 7;
		else
			*maxtime = 6;
	}
}
static u64 calc_freq_ux_load(struct task_struct *p, u64 wallclock)
{
	unsigned int maxtime = 0;
	u64 timeline = 0, freq_exec_load = 0, freq_ravg_load = 0;
	u64 wakeclock = p->wts.last_wake_ts;
	u64 maxload = (sched_ravg_window >> 1) + (sched_ravg_window >> 4);
	if (wallclock < wakeclock)
		return 0;
	sched_assist_adjust_slide_param(&maxtime);
	maxtime = maxtime * NSEC_PER_MSEC;
	timeline = wallclock - wakeclock;
	timeline = tick_change_sf_load(p, timeline);
	if (timeline >= maxtime) {
		freq_exec_load = maxload;
	}
	freq_ravg_load = (p->wts.prev_window + p->wts.curr_window) << 1;
	if (freq_ravg_load > maxload)
		freq_ravg_load = maxload;
	if (task_is_sf_group(p)) {
                sched_assist_systrace(timeline, "timeline_true_%d", task_cpu(p));
                sched_assist_systrace(freq_exec_load, "freq_exec_true_%d", task_cpu(p));
                sched_assist_systrace(freq_ravg_load, "freq_ravg_true_%d", task_cpu(p));
	}
	return max(freq_exec_load, freq_ravg_load);
}

void _slide_find_start_cpu(struct root_domain *rd, struct task_struct *p, int *start_cpu)
{
	if (task_util(p) >= sysctl_boost_task_threshold ||
		scale_demand(p->wts.sum) >= sysctl_boost_task_threshold) {
		*start_cpu = rd->wrd.mid_cap_orig_cpu == -1 ?
			rd->wrd.max_cap_orig_cpu : rd->wrd.mid_cap_orig_cpu;
	}
}

bool _slide_task_misfit(struct task_struct *p, int cpu)
{
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
	if ((scale_demand(p->wts.sum) >= sysctl_boost_task_threshold ||
	     task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
		return true;
	return false;
}

u64 _slide_get_boost_load(int cpu) {
	u64 wallclock = sched_ktime_clock();
	u64 timeline = 0;
	if (slide_scene() && ux_task_load[cpu]) {
		timeline = wallclock - ux_load_ts[cpu];
		if  (timeline >= UX_LOAD_WINDOW)
			ux_task_load[cpu] = 0;
		return ux_task_load[cpu];
	} else {
		return 0;
	}
}
void adjust_sched_assist_input_ctrl() {
	static int input_boost_old = 0;
	if (sysctl_input_boost_enabled && sysctl_slide_boost_enabled) {
		sysctl_input_boost_enabled = 0;
	}
	if (input_boost_old != sysctl_input_boost_enabled) {
		input_boost_old = sysctl_input_boost_enabled;
		sched_assist_systrace(sysctl_input_boost_enabled, "ux input");
	}
}

void slide_calc_boost_load(struct rq *rq, unsigned int *flag, int cpu) {
	u64 wallclock = sched_ktime_clock();
	adjust_sched_assist_input_ctrl();
	if (slide_scene()) {
		if(rq->curr && (is_heavy_ux_task(rq->curr) || rq->curr->sched_class == &rt_sched_class) && !oplus_task_misfit(rq->curr, rq->cpu)) {
			ux_task_load[cpu] = calc_freq_ux_load(rq->curr, wallclock);
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_BOOST);
		}
		else if (ux_task_load[cpu] != 0) {
			ux_task_load[cpu] = 0;
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_RESET);
		}
	} else {
		ux_task_load[cpu] = 0;
		ux_load_ts[cpu] = 0;
	}
	sched_assist_systrace(ux_task_load[cpu], "ux_load_%d", cpu);
}
bool should_adjust_slide_task_placement(struct task_struct *p, int cpu)
{
        struct rq *rq = NULL;

        if (!slide_scene())
                return false;

        if (!is_heavy_ux_task(p))
                return false;

        /* slide task running on silver core with util over threshold, boost it */
        if (oplus_task_misfit(p, cpu))
                return true;

        /* slide task should not share it's core with another slide task */
        rq = cpu_rq(cpu);
        if (rq->curr && is_heavy_ux_task(rq->curr))
                return true;

        return false;
}

int sched_frame_rate_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	return ret;
}

int sysctl_sched_slide_boost_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (write)
		sched_assist_systrace(sysctl_slide_boost_enabled, "ux slide");

	return ret;
}

#define INPUT_BOOST_DURATION 1500000000 /* ns*/
static struct hrtimer ibtimer;
static int intput_boost_duration;
static ktime_t ib_last_time;
void enable_input_boost_timer(void)
{
	ktime_t ktime;
	ib_last_time = ktime_get();

	ktime = ktime_set(0, intput_boost_duration);
	hrtimer_start(&ibtimer, ktime, HRTIMER_MODE_REL);
}

void disable_input_boost_timer(void)
{
	hrtimer_cancel(&ibtimer);
}

enum hrtimer_restart input_boost_timeout(struct hrtimer *timer)
{
	ktime_t now, delta;
	now = ktime_get();

	delta = ktime_sub(now, ib_last_time);
	if (ktime_to_ns(delta) > intput_boost_duration) {
		ib_last_time = now;
		sysctl_input_boost_enabled = 0;
	}
	return HRTIMER_NORESTART;
}

int sysctl_sched_assist_input_boost_ctrl_handler(struct ctl_table * table, int write,
	void __user * buffer, size_t * lenp, loff_t * ppos)
{
	int result;
	static DEFINE_MUTEX(sa_boost_mutex);
	mutex_lock(&sa_boost_mutex);
	result = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write)
		goto out;

	/*orms write just write this proc to tell us update input window*/
	disable_input_boost_timer();
	enable_input_boost_timer();
out:
	mutex_unlock(&sa_boost_mutex);
	return result;
}

int oplus_input_boost_init(void)
{
	int ret = 0;
	ib_last_time = ktime_get();
	intput_boost_duration = INPUT_BOOST_DURATION;

	hrtimer_init(&ibtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ibtimer.function = &input_boost_timeout;
	return ret;
}
device_initcall(oplus_input_boost_init);
