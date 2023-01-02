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

#include "jank_hotthread.h"
#include "jank_freq.h"
#include "jank_loadindicator.h"
#include "jank_cpuload.h"
#include "jank_cputime.h"
#include "jank_topology.h"
#include "jank_tasktrack.h"
#include "jank_onlinecpu.h"

struct cgropu_delta {
	u64 delta;
	u64 delta_a;
	u64 delta_cnt;
	u64 delta_b;
};

struct cputime cputime[CPU_NUMS];
struct cputime cputime_snap[CPU_NUMS];
static u64 cpustat_bak[CPU_NUMS][NR_STATS];
static u64 cgropu_times[CPU_NUMS][CGROUP_NRS];
static u64 cgropu_times_bak[CPU_NUMS][CGROUP_NRS];
static struct cgropu_delta cgropu_times_delta[CPU_NUMS][CGROUP_NRS];

static void account_cgropu_time(struct task_struct *p, u64 time)
{
	u32 cpu;

	if (!p || !time)
		return;

	cpu = p->cpu;

	if (is_default(p))
		cgropu_times[cpu][CGROUP_DEFAULT] += time;

	if (is_foreground(p))
		cgropu_times[cpu][CGROUP_FOREGROUND] += time;

	if (is_background(p))
		cgropu_times[cpu][CGROUP_BACKGROUND] += time;

	if (is_topapp(p))
		cgropu_times[cpu][CGROUP_TOP_APP] += time;
}

static void update_cgroup_delta(u64 now, u32 cpu)
{
	u32 grp;
	u64 delta, *delta_a, *delta_cnt, *delta_b;

	for (grp = 0; grp < CGROUP_NRS; grp++) {
		delta_a = &cgropu_times_delta[cpu][grp].delta_a;
		delta_cnt = &cgropu_times_delta[cpu][grp].delta_cnt;
		delta_b = &cgropu_times_delta[cpu][grp].delta_b;

		delta = cgropu_times[cpu][grp] - cgropu_times_bak[cpu][grp];
		cgropu_times_delta[cpu][grp].delta = delta;
		cgropu_times_bak[cpu][grp] = cgropu_times[cpu][grp];

		split_window(now, delta, delta_a, delta_cnt, delta_b);
	}
}

static __maybe_unused void get_cgroup_delta(u64 now,
			u32 cpu, u32 grp, struct cgropu_delta *d)
{
	u64 delta;

	delta = cgropu_times[cpu][grp] - cgropu_times_bak[cpu][grp];
	d->delta = delta;

	split_window(now, delta, &d->delta_a, &d->delta_cnt, &d->delta_b);
}

#if IS_ENABLED(CONFIG_SCHED_WALT) && IS_ENABLED(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	return (delta * rq->task_exec_scale) >> 10;
#else
	return (delta * rq->wrq.task_exec_scale) >> 10;
#endif
}
#else
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;
	return (delta * wrq->task_exec_scale) >> 10;
}
#endif
#else
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	return delta;
}
#endif

static DEFINE_SPINLOCK(jankinfo_update_time_info_last_calltime_lock);
u64 jankinfo_update_time_info_last_calltime;
void jankinfo_update_time_info(struct rq *rq,
				struct task_struct *p, u64 time)
{
	struct kernel_cpustat *kcs = NULL;
	u64 now;
	u64 last_update_time;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 runtime = 0, runtime_a, runtime_cnt, runtime_b;
	u64 total_time;
	u32 now_idx, last_idx, idx;
	u32 i = 0;
	u32 cpu;
	bool fg = false, compat = false;
	u64 runtime_scale, runtime_scale_a, runtime_scale_cnt, runtime_scale_b;
	u64 calldelta;

	if (!p)
		return;

	cpu = p->cpu;
	fg = is_fg_or_topapp(p);

	last_update_time = cputime[cpu].last_update_time;
	last_idx = cputime[cpu].last_index;

	/*
	 * Note: the jiffies value will overflow 5 minutes after boot
	 */
	now = jiffies_to_nsecs(jiffies);
	if (unlikely(now < last_update_time)) {
		cputime[cpu].last_update_time = now;
		delta = 0;
	} else {
		delta = now - last_update_time;
	}

	/*
	 * This function will be called on each cpu, we need to
	 * make sure that this function is called once per tick
	 */
	spin_lock(&jankinfo_update_time_info_last_calltime_lock);
	calldelta = now - jankinfo_update_time_info_last_calltime;
	if (calldelta < TICK_NSEC) {
		spin_unlock(&jankinfo_update_time_info_last_calltime_lock);
		return;
	}

	jank_dbg("q3: cpu=%d, time=%lld(%lld), calldelta=%lld(%lld)\n",
		smp_processor_id(),
		time/NSEC_PER_MSEC, time,
		(calldelta)/NSEC_PER_MSEC, calldelta);

	jankinfo_update_time_info_last_calltime = now;
	spin_unlock(&jankinfo_update_time_info_last_calltime_lock);

	account_cgropu_time(p, time);


	/* Identify hot processes */
	jank_hotthread_update_tick(p, now);

	/*
	 * The following statistics are performed only when
	 * delta is greater than at least one time window
	 *
	 * Note:
	 *   When CONFIG_NO_HZ is enabled, delta may be
	 *   greater than JANK_WIN_SIZE_IN_NS
	 */
	if (delta < JANK_WIN_SIZE_IN_NS)
		return;

	compat = is_compat_thread(task_thread_info(p));

	kcs = &kcpustat_cpu(cpu);
	runtime  = kcs->cpustat[CPUTIME_USER] -
				cpustat_bak[cpu][CPUTIME_USER];			/* in ns */
	runtime += kcs->cpustat[CPUTIME_NICE] -
				cpustat_bak[cpu][CPUTIME_NICE];
	runtime += kcs->cpustat[CPUTIME_SYSTEM] -
				cpustat_bak[cpu][CPUTIME_SYSTEM];
	runtime += kcs->cpustat[CPUTIME_IRQ] -
				cpustat_bak[cpu][CPUTIME_IRQ];
	runtime += kcs->cpustat[CPUTIME_SOFTIRQ] -
				cpustat_bak[cpu][CPUTIME_SOFTIRQ];
	runtime += kcs->cpustat[CPUTIME_STEAL] -
				cpustat_bak[cpu][CPUTIME_STEAL];
	runtime += kcs->cpustat[CPUTIME_GUEST] -
				cpustat_bak[cpu][CPUTIME_GUEST];
	runtime += kcs->cpustat[CPUTIME_GUEST_NICE] -
				cpustat_bak[cpu][CPUTIME_GUEST_NICE];

	total_time = runtime + kcs->cpustat[CPUTIME_IDLE] +
				kcs->cpustat[CPUTIME_IOWAIT];

	cpustat_bak[cpu][CPUTIME_USER] = kcs->cpustat[CPUTIME_USER];
	cpustat_bak[cpu][CPUTIME_NICE] = kcs->cpustat[CPUTIME_NICE];
	cpustat_bak[cpu][CPUTIME_SYSTEM] = kcs->cpustat[CPUTIME_SYSTEM];
	cpustat_bak[cpu][CPUTIME_IRQ] = kcs->cpustat[CPUTIME_IRQ];
	cpustat_bak[cpu][CPUTIME_SOFTIRQ] = kcs->cpustat[CPUTIME_SOFTIRQ];
	cpustat_bak[cpu][CPUTIME_STEAL] = kcs->cpustat[CPUTIME_STEAL];
	cpustat_bak[cpu][CPUTIME_GUEST] = kcs->cpustat[CPUTIME_GUEST];
	cpustat_bak[cpu][CPUTIME_GUEST_NICE] =
					kcs->cpustat[CPUTIME_GUEST_NICE];

	/*
	 * Note:
	 * CPUTIME_IRQ and CPUTIME_SOFTIRQ were driven by sched_clock,
	 * while CPUTIME_USER and CPUTIME_NICE were driven by tick, they
	 * have different precision, so it is possible to have runtime
	 * greater than delta
	 */
	runtime = min_t(u64, delta, runtime);
	if (!runtime)
		return;

	total_time = min_t(u64, delta, total_time);

	now_idx = time2winidx(now);

	/* cpu freq */
	jank_currfreq_update_win(now);

	jank_onlinecpu_update(cpu_active_mask);

	update_cgroup_delta(now, cpu);

	runtime_scale = scale_exec_time(runtime, rq);

	split_window(now, delta, &delta_a, &delta_cnt, &delta_b);
	split_window(now, runtime, &runtime_a, &runtime_cnt, &runtime_b);
	split_window(now, runtime_scale,
			&runtime_scale_a, &runtime_scale_cnt, &runtime_scale_b);

	if (!delta_cnt && last_idx == now_idx) {
		/* In the same time window */
		cputime[cpu].running_time[now_idx] += runtime_b;
		cputime[cpu].running_time32[now_idx] += compat ? runtime_b : 0;
		cputime[cpu].running_time32_scale[now_idx] +=
					compat ? runtime_scale_b : 0;

		cputime[cpu].fg_running_time[now_idx] +=
				is_foreground(p) ?
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b : 0;
		cputime[cpu].fg_running_time[now_idx] +=
				is_topapp(p) ?
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b : 0;

		cputime[cpu].default_runtime[now_idx] +=
				is_default(p) ?
				cgropu_times_delta[cpu][CGROUP_DEFAULT].delta_b : 0;
		cputime[cpu].foreground_runtime[now_idx] +=
				is_foreground(p) ?
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b : 0;
		cputime[cpu].background_runtime[now_idx] +=
				is_background(p) ?
				cgropu_times_delta[cpu][CGROUP_BACKGROUND].delta_b : 0;
		cputime[cpu].topapp_runtime[now_idx] +=
				is_topapp(p) ?
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b  : 0;

	} else if (!delta_cnt &&
				((last_idx+1) & JANK_WIN_CNT_MASK) == now_idx) {
		/* Spanning two time Windows */
		cputime[cpu].running_time[now_idx] = runtime_b;
		cputime[cpu].running_time32[now_idx] = compat ? runtime_b : 0;
		cputime[cpu].running_time32_scale[now_idx] =
			compat ? runtime_scale_b : 0;

		if (is_foreground(p)) {
			cputime[cpu].fg_running_time[now_idx] =
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b;
		} else if (is_topapp(p)) {
			cputime[cpu].fg_running_time[now_idx] =
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b;
		} else {
			cputime[cpu].fg_running_time[now_idx] = 0;
		}

		cputime[cpu].running_time[last_idx] += runtime_a;
		cputime[cpu].running_time32[last_idx] += compat ? runtime_a : 0;
		cputime[cpu].running_time32_scale[last_idx] +=
			compat ? runtime_scale_a : 0;

		cputime[cpu].fg_running_time[last_idx] +=
			is_foreground(p) ?
			cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_a : 0;
		cputime[cpu].fg_running_time[last_idx] +=
			is_topapp(p) ?
			cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_a : 0;

		/* for CPU load indicator */
		cputime[cpu].default_runtime[now_idx] =
			is_default(p) ?
			cgropu_times_delta[cpu][CGROUP_DEFAULT].delta_b : 0;
		cputime[cpu].foreground_runtime[now_idx] =
			is_foreground(p) ?
			cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b : 0;
		cputime[cpu].background_runtime[now_idx] =
			is_background(p) ?
			cgropu_times_delta[cpu][CGROUP_BACKGROUND].delta_b : 0;
		cputime[cpu].topapp_runtime[now_idx] =
			is_topapp(p) ?
			cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b : 0;

		cputime[cpu].default_runtime[last_idx] +=
			is_default(p) ?
			cgropu_times_delta[cpu][CGROUP_DEFAULT].delta_a : 0;
		cputime[cpu].foreground_runtime[last_idx] +=
			is_foreground(p) ?
			cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_a : 0;
		cputime[cpu].background_runtime[last_idx] +=
			is_background(p) ?
			cgropu_times_delta[cpu][CGROUP_BACKGROUND].delta_a : 0;
		cputime[cpu].topapp_runtime[last_idx] +=
			is_topapp(p) ?
			cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_a : 0;

	} else {
		/* Spanning multiple time Windows */
		for (i = 0; i < min_t(u64, delta_cnt, JANK_WIN_CNT); i++) {
			/* clear new windows */
			idx = (last_idx + 1 + i + JANK_WIN_CNT) & JANK_WIN_CNT_MASK;
			cputime[cpu].running_time[idx] = 0;
			cputime[cpu].running_time32[idx] = 0;
			cputime[cpu].running_time32_scale[idx] = 0;
			cputime[cpu].fg_running_time[idx] = 0;


			/* for CPU load indicator */
			cputime[cpu].default_runtime[idx] = 0;
			cputime[cpu].foreground_runtime[idx] = 0;
			cputime[cpu].background_runtime[idx] = 0;
			cputime[cpu].topapp_runtime[idx] = 0;
		}

		cputime[cpu].running_time[now_idx] = runtime_b;
		cputime[cpu].running_time32[now_idx] = compat ? runtime_b : 0;
		cputime[cpu].running_time32_scale[now_idx] =
					compat ? runtime_scale_b : 0;
		if (is_foreground(p)) {
			cputime[cpu].fg_running_time[now_idx] =
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b;
		} else if (is_topapp(p)) {
			cputime[cpu].fg_running_time[now_idx] =
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b;
		} else {
			cputime[cpu].fg_running_time[now_idx] = 0;
		}

		/* for CPU load indicator */
		cputime[cpu].default_runtime[now_idx] =
			is_default(p) ?
			cgropu_times_delta[cpu][CGROUP_DEFAULT].delta_b : 0;
		cputime[cpu].foreground_runtime[now_idx] =
			is_foreground(p) ?
			cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_b : 0;
		cputime[cpu].background_runtime[now_idx] =
			is_background(p) ?
			cgropu_times_delta[cpu][CGROUP_BACKGROUND].delta_b : 0;
		cputime[cpu].topapp_runtime[now_idx] =
			is_topapp(p) ?
			cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_b : 0;

		for (i = 0; i < min_t(u64, runtime_cnt, (JANK_WIN_CNT-1)); i++) {
			idx = (now_idx - 1 - i + JANK_WIN_CNT) & JANK_WIN_CNT_MASK;

			cputime[cpu].running_time[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].running_time32[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].running_time32_scale[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].fg_running_time[idx] = JANK_WIN_SIZE_IN_NS;

			cputime[cpu].default_runtime[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].foreground_runtime[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].background_runtime[idx] = JANK_WIN_SIZE_IN_NS;
			cputime[cpu].topapp_runtime[idx] = JANK_WIN_SIZE_IN_NS;
		}

		if (runtime_cnt < JANK_WIN_CNT-1 && runtime_a) {
			idx = (now_idx + JANK_WIN_CNT - 1 -
				min_t(u64, runtime_cnt, JANK_WIN_CNT)) & JANK_WIN_CNT_MASK;
			cputime[cpu].running_time[idx] += runtime_a;
			cputime[cpu].running_time32[idx] += compat ? runtime_a : 0;
			cputime[cpu].running_time32_scale[idx] +=
				compat ? runtime_scale_a : 0;
			cputime[cpu].fg_running_time[idx] +=
				is_foreground(p) ?
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_a : 0;
			cputime[cpu].fg_running_time[idx] +=
				is_topapp(p) ?
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_a : 0;

			cputime[cpu].default_runtime[idx] +=
				is_default(p) ?
				cgropu_times_delta[cpu][CGROUP_DEFAULT].delta_a : 0;
			cputime[cpu].foreground_runtime[idx] +=
				is_foreground(p) ?
				cgropu_times_delta[cpu][CGROUP_FOREGROUND].delta_a : 0;
			cputime[cpu].background_runtime[idx] +=
				is_background(p) ?
				cgropu_times_delta[cpu][CGROUP_BACKGROUND].delta_a : 0;
			cputime[cpu].topapp_runtime[idx] +=
				is_topapp(p) ?
				cgropu_times_delta[cpu][CGROUP_TOP_APP].delta_a : 0;
		}
	}

	/* update */
	cputime[cpu].last_index = now_idx;
	cputime[cpu].last_update_time = now;

	/* Update the CPU load indicator */
	jank_loadindicator_update_win(&cputime[0], p, now);
}

u64 get_kcpustat_increase(u32 cpu)
{
	u64 runtime;
	struct kernel_cpustat *kcs = NULL;

	kcs = &kcpustat_cpu(cpu);
	runtime  = kcs->cpustat[CPUTIME_USER] -
		cpustat_bak[cpu][CPUTIME_USER];		/* in ns */
	runtime += kcs->cpustat[CPUTIME_NICE] -
		cpustat_bak[cpu][CPUTIME_NICE];
	runtime += kcs->cpustat[CPUTIME_SYSTEM] -
		cpustat_bak[cpu][CPUTIME_SYSTEM];
	runtime += kcs->cpustat[CPUTIME_IRQ] -
		cpustat_bak[cpu][CPUTIME_IRQ];
	runtime += kcs->cpustat[CPUTIME_SOFTIRQ] -
		cpustat_bak[cpu][CPUTIME_SOFTIRQ];
	runtime += kcs->cpustat[CPUTIME_STEAL] -
		cpustat_bak[cpu][CPUTIME_STEAL];
	runtime += kcs->cpustat[CPUTIME_GUEST] -
		cpustat_bak[cpu][CPUTIME_GUEST];
	runtime += kcs->cpustat[CPUTIME_GUEST_NICE] -
		cpustat_bak[cpu][CPUTIME_GUEST_NICE];

	return runtime;
}

u32 get_cpu_load32_scale(u32 win_cnt, struct cpumask *mask)
{
	u32 i, j;
	u32 now_index, last_index, last_index_r, idx;
	u32 cpu_nr;
	u64 now, now_a, now_b;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time, last_runtime, last_runtime_r;
	u64 last_win_delta, last_win_delta_r;
	u64 increase, increase_a, increase_cnt, increase_b;
	u64 tmp_time = 0, run_time = 0, total_time = 0;
	u32 ret;

	if (!win_cnt || !mask)
		return 0;

	cpu_nr = cpumask_weight(mask);
	if (!cpu_nr)
		return 0;

	now = jiffies_to_nsecs(jiffies);
	now_index = time2winidx(now);

	if (win_cnt == 1) {
		now_b = now & JANK_WIN_SIZE_IN_NS_MASK;
		now_a = JANK_WIN_SIZE_IN_NS - now_b;

		for_each_cpu(i, mask) {
			last_update_time = cputime[i].last_update_time;
			last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
			last_win_delta_r = JANK_WIN_SIZE_IN_NS - last_win_delta;

			last_index = cputime[i].last_index;
			last_runtime = cputime[i].running_time32_scale[last_index];

			last_index_r =
				(last_index + JANK_WIN_CNT - 1) & JANK_WIN_CNT_MASK;
			last_runtime_r =
				cputime[i].running_time32_scale[last_index_r];

			delta = (now > last_update_time) ?
					now - last_update_time : 0;
			split_window(now, delta, &delta_a, &delta_cnt, &delta_b);


			increase = get_kcpustat_increase(i);
			increase = min_t(u64, delta, increase);
			increase = scale_exec_time(increase, cpu_rq(i));
			split_window(now, increase,
				&increase_a, &increase_cnt, &increase_b);

			if (!delta_cnt && last_index == now_index) {
				tmp_time = (last_runtime + increase);
				tmp_time +=
					(last_runtime_r * now_a) >> JANK_WIN_SIZE_SHIFT_IN_NS;
			} else if (!delta_cnt &&
					((last_index+1) & JANK_WIN_CNT_MASK) == now_index) {
				tmp_time = increase_b;
				tmp_time +=
					((last_runtime + increase_a) * now_a) >>
					JANK_WIN_SIZE_SHIFT_IN_NS;
			} else {
				tmp_time = increase_b;
				tmp_time += (JANK_WIN_SIZE_IN_NS * now_a) >>
					JANK_WIN_SIZE_SHIFT_IN_NS;
			}
			tmp_time = min_t(u64, tmp_time, JANK_WIN_SIZE_IN_NS);
			run_time += tmp_time;
			total_time += JANK_WIN_SIZE_IN_NS;
		}
	} else {
		for (j = 0; j < win_cnt; j++) {
			for_each_cpu(i, mask) {
				last_update_time = cputime[i].last_update_time;
				last_win_delta =
					last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
				last_index = cputime[i].last_index;
				last_runtime = cputime[i].running_time32_scale[last_index];

				delta = (now > last_update_time) ?
						now - last_update_time : 0;
				split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

				if (!delta_cnt && last_index == now_index) {
					if (j == 0) {
						run_time += last_runtime;
						total_time += last_win_delta;
					} else {
						idx = (last_index + JANK_WIN_CNT - j) &
							JANK_WIN_CNT_MASK;
						run_time += cputime[i].running_time32_scale[idx];
						total_time += JANK_WIN_SIZE_IN_NS;
					}
				} else if (delta_cnt < JANK_WIN_CNT-1) {
					if (j <= delta_cnt) {
						run_time += 0;
						total_time += (j == 0) ?
							last_win_delta : JANK_WIN_SIZE_IN_NS;
					} else {
						idx = (now_index + JANK_WIN_CNT - j) &
							JANK_WIN_CNT_MASK;
						if (idx == last_index) {
							run_time += last_runtime;
							total_time += last_win_delta;
						} else {
							run_time +=
							cputime[i].running_time32_scale[idx];
							total_time += JANK_WIN_SIZE_IN_NS;
						}
					}
				} else {
					run_time += 0;
					total_time += JANK_WIN_SIZE_IN_NS;
				}
			}
		}
	}

	/* Prevents the error of dividing by zero */
	if (!total_time)
		total_time = JANK_WIN_SIZE_IN_NS * cpu_nr * win_cnt;

	ret = (run_time * 100) / total_time;
	return ret;
}

u32 get_cpu_load32(u32 win_cnt, struct cpumask *mask)
{
	u32 i, j;
	u32 now_index, last_index, last_index_r, idx;
	u32 cpu_nr;
	u64 now, now_a, now_b;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time, last_runtime, last_runtime_r;
	u64 last_win_delta, last_win_delta_r;
	u64 increase, increase_a, increase_cnt, increase_b;
	u64 tmp_time = 0, run_time = 0, total_time = 0;
	u32 ret;

	if (!win_cnt || !mask)
		return 0;

	cpu_nr = cpumask_weight(mask);
	if (!cpu_nr)
		return 0;

	now = jiffies_to_nsecs(jiffies);
	now_index = time2winidx(now);

	if (win_cnt == 1) {
		now_b = now & JANK_WIN_SIZE_IN_NS_MASK;
		now_a = JANK_WIN_SIZE_IN_NS - now_b;

		for_each_cpu(i, mask) {
			last_update_time = cputime[i].last_update_time;
			last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
			last_win_delta_r = JANK_WIN_SIZE_IN_NS - last_win_delta;

			last_index = cputime[i].last_index;
			last_runtime = cputime[i].running_time32[last_index];

			last_index_r =
				(last_index + JANK_WIN_CNT - 1) & JANK_WIN_CNT_MASK;
			last_runtime_r = cputime[i].running_time32[last_index_r];

			delta = (now > last_update_time) ? now - last_update_time : 0;
			split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

			increase = get_kcpustat_increase(i);
			increase = min_t(u64, delta, increase);
			split_window(now, increase,
				&increase_a, &increase_cnt, &increase_b);

			if (!delta_cnt && last_index == now_index) {
				tmp_time = (last_runtime + increase);
				tmp_time += (last_runtime_r * now_a) >>
				JANK_WIN_SIZE_SHIFT_IN_NS;
			} else if (!delta_cnt &&
					((last_index+1) & JANK_WIN_CNT_MASK) == now_index) {
				tmp_time = increase_b;
				tmp_time += ((last_runtime + increase_a) * now_a) >>
					JANK_WIN_SIZE_SHIFT_IN_NS;
			} else {
				tmp_time = increase_b;
				tmp_time += (JANK_WIN_SIZE_IN_NS * now_a) >>
					JANK_WIN_SIZE_SHIFT_IN_NS;
			}
			tmp_time = min_t(u64, tmp_time, JANK_WIN_SIZE_IN_NS);
			run_time += tmp_time;
			total_time += JANK_WIN_SIZE_IN_NS;
		}
	} else {
		for (j = 0; j < win_cnt; j++) {
			for_each_cpu(i, mask) {
				last_update_time = cputime[i].last_update_time;
				last_win_delta = last_update_time &
					JANK_WIN_SIZE_IN_NS_MASK;
				last_index = cputime[i].last_index;
				last_runtime = cputime[i].running_time32[last_index];

				delta = (now > last_update_time) ?
					now - last_update_time : 0;
				split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

				if (!delta_cnt && last_index == now_index) {
					if (j == 0) {
						run_time += last_runtime;
						total_time += last_win_delta;
					} else {
						idx = (last_index + JANK_WIN_CNT - j) &
							JANK_WIN_CNT_MASK;
						run_time += cputime[i].running_time32[idx];
						total_time += JANK_WIN_SIZE_IN_NS;
					}
				} else if (delta_cnt < JANK_WIN_CNT-1) {
					if (j <= delta_cnt) {
						run_time += 0;
						total_time += (j == 0) ?
							last_win_delta : JANK_WIN_SIZE_IN_NS;
					} else {
						idx = (now_index + JANK_WIN_CNT - j) &
								JANK_WIN_CNT_MASK;
						if (idx == last_index) {
							run_time += last_runtime;
							total_time += last_win_delta;
						} else {
							run_time += cputime[i].running_time32[idx];
							total_time += JANK_WIN_SIZE_IN_NS;
						}
					}
				} else {
					run_time += 0;
					total_time += JANK_WIN_SIZE_IN_NS;
				}
			}
		}
	}

	/* Prevents the error of dividing by zero */
	if (!total_time)
		total_time = JANK_WIN_SIZE_IN_NS * cpu_nr * win_cnt;

	ret = (run_time * 100) / total_time;
	return ret;
}

u32 get_cpu_load(u32 win_cnt, struct cpumask *mask)
{
	u32 i, j;
	u32 now_index, last_index, last_index_r, idx;
	u32 cpu_nr;
	u64 now, now_a, now_b;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time, last_runtime, last_runtime_r;
	u64 last_win_delta, last_win_delta_r;
	u64 increase, increase_a, increase_cnt, increase_b;
	u64 tmp_time = 0, run_time = 0, total_time = 0;
	u32 ret;

	if (!win_cnt || !mask)
		return 0;

	cpu_nr = cpumask_weight(mask);
	if (!cpu_nr)
		return 0;

	now = jiffies_to_nsecs(jiffies);
	now_index = time2winidx(now);

	if (win_cnt == 1) {
		now_b = now & JANK_WIN_SIZE_IN_NS_MASK;
		now_a = JANK_WIN_SIZE_IN_NS - now_b;

		for_each_cpu(i, mask) {
			last_update_time = cputime[i].last_update_time;
			last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
			last_win_delta_r = JANK_WIN_SIZE_IN_NS - last_win_delta;

			last_index = cputime[i].last_index;
			last_runtime = cputime[i].running_time[last_index];

			last_index_r = (last_index + JANK_WIN_CNT - 1) &
							JANK_WIN_CNT_MASK;
			last_runtime_r = cputime[i].running_time[last_index_r];

			delta = (now > last_update_time) ? now - last_update_time : 0;
			split_window(now, delta, &delta_a, &delta_cnt, &delta_b);


			increase = get_kcpustat_increase(i);
			increase = min_t(u64, delta, increase);
			split_window(now, increase,
						&increase_a, &increase_cnt, &increase_b);

			if (!delta_cnt && last_index == now_index) {
				tmp_time = (last_runtime + increase);
				tmp_time += (last_runtime_r * now_a) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
			} else if (!delta_cnt &&
					((last_index+1) & JANK_WIN_CNT_MASK) == now_index) {
				tmp_time = increase_b;
				tmp_time += ((last_runtime + increase_a) * now_a) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
			} else {
				tmp_time = increase_b;
				tmp_time += (JANK_WIN_SIZE_IN_NS * now_a) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
			}

			jank_dbg("r1:cpu%d, tmp_time=%llu, true1=%d, "
				"d=%llu, d_cnt=%d, li=%d, ni=%d, "
				"lr=%llu, true2=%d lr_r=%llu, "
				"ic=%llu, ic_a=%llu, ic_n=%llu, ic_b=%llu, "
				"na=%llu, nb=%llu, "
				"lwd=%llu, lwd_r=%llu, "
				"n=%llu, la=%llu, true3=%d\n",
				i, tmp_time>>20, tmp_time > JANK_WIN_SIZE_IN_NS,
				delta>>20, delta_cnt, last_index, now_index,
				last_runtime>>20, last_runtime > JANK_WIN_SIZE_IN_NS,
				last_runtime_r>>20,
				increase>>20, increase_a>>20,
				increase_cnt, increase_b>>20,
				now_a>>20, now_b>>20,
				last_win_delta>>20, last_win_delta_r>>20,
				now, last_update_time, now < last_update_time);

			tmp_time = min_t(u64, tmp_time, JANK_WIN_SIZE_IN_NS);

			jank_dbg("r2: tmp_time=%llu, true=%d\n",
				tmp_time>>20, tmp_time > JANK_WIN_SIZE_IN_NS);

			run_time += tmp_time;
			total_time += JANK_WIN_SIZE_IN_NS;
		}
	} else {
		for (j = 0; j < win_cnt; j++) {
			for_each_cpu(i, mask) {
				last_update_time = cputime[i].last_update_time;
				last_win_delta =
					last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
				last_index = cputime[i].last_index;
				last_runtime = cputime[i].running_time[last_index];

				delta = (now > last_update_time) ?
							now - last_update_time : 0;
				split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

				if (!delta_cnt && last_index == now_index) {
					if (j == 0) {
						run_time += last_runtime;
						total_time += last_win_delta;
					} else {
						idx = (last_index + JANK_WIN_CNT - j) &
									JANK_WIN_CNT_MASK;
						run_time += cputime[i].running_time[idx];
						total_time += JANK_WIN_SIZE_IN_NS;
					}
				} else if (delta_cnt < JANK_WIN_CNT-1) {
					if (j <= delta_cnt) {
						run_time += 0;
						total_time += (j == 0) ?
							last_win_delta : JANK_WIN_SIZE_IN_NS;
					} else {
						idx = (now_index + JANK_WIN_CNT - j) &
								JANK_WIN_CNT_MASK;
						if (idx == last_index) {
							run_time += last_runtime;
							total_time += last_win_delta;
						} else {
							run_time += cputime[i].running_time[idx];
							total_time += JANK_WIN_SIZE_IN_NS;
						}
					}
				} else {
					run_time += 0;
					total_time += JANK_WIN_SIZE_IN_NS;
				}
			}
		}
	}

	/* Prevents the error of dividing by zero */
	if (!total_time)
		total_time = JANK_WIN_SIZE_IN_NS * cpu_nr * win_cnt;

	ret = (run_time * 100) / total_time;
	return ret;
}

void jank_group_ratio_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u32 i;
	u64 tmp_avg;
	u64 last_fgtime, last_fgtime_r, tmp_fgtime;
	u64 last_df_time, last_df_time_r, tmp_df_time, tmp_avg_df;
	u64 last_fg_time, last_fg_time_r, tmp_fg_time, tmp_avg_fg;
	u64 last_bg_time, last_bg_time_r, tmp_bg_time, tmp_avg_bg;
	u64 last_topapp_time, last_topapp_time_r;
	u64 tmp_topapp_time, tmp_avg_topapp;
	u64 last_runtime, last_runtime_r, tmp_runtime = 0;
	u32 cluster_id, start_cpu, end_cpu, cpu_nr;
	u32 now_index, idx, last_index, last_index_r;
	u64 now_delta;
	u64 last_update_time, last_win_delta, last_win_delta_r;
	u64 increase, increase_a, increase_cnt, increase_b;

	u64 delta, delta_a, delta_cnt, delta_b;
	struct cgropu_delta delta_df, delta_fg, delta_bg, delta_top;
	u64 avg_df[CPU_NUMS], avg_fg[CPU_NUMS];
	u64 avg_bg[CPU_NUMS], avg_topapp[CPU_NUMS];

	now_index = time2winidx(now);
	now_delta = now & JANK_WIN_SIZE_IN_NS_MASK;

	for (i = 0; i < CPU_NUMS; i++) {
		/* ignore little core
		 *if(!get_start_cpu(i))
		 *	continue;
		 */

		get_cgroup_delta(now, i, CGROUP_DEFAULT, &delta_df);
		get_cgroup_delta(now, i, CGROUP_FOREGROUND, &delta_fg);
		get_cgroup_delta(now, i, CGROUP_BACKGROUND, &delta_bg);
		get_cgroup_delta(now, i, CGROUP_TOP_APP, &delta_top);

		last_update_time = cputime_snap[i].last_update_time;
		last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
		last_win_delta_r = JANK_WIN_SIZE_IN_NS - last_win_delta;

		last_index = cputime_snap[i].last_index;
		last_fgtime = cputime_snap[i].fg_running_time[last_index];
		last_df_time = cputime_snap[i].default_runtime[last_index];
		last_fg_time = cputime_snap[i].foreground_runtime[last_index];
		last_bg_time = cputime_snap[i].background_runtime[last_index];
		last_topapp_time = cputime_snap[i].topapp_runtime[last_index];
		last_runtime = cputime_snap[i].running_time[last_index];

		last_index_r = winidx_sub(last_index, 1);
		last_fgtime_r = cputime_snap[i].fg_running_time[last_index_r];
		last_df_time_r = cputime_snap[i].default_runtime[last_index_r];
		last_fg_time_r = cputime_snap[i].foreground_runtime[last_index_r];
		last_bg_time_r = cputime_snap[i].background_runtime[last_index_r];
		last_topapp_time_r = cputime_snap[i].topapp_runtime[last_index_r];
		last_runtime_r = cputime_snap[i].running_time[last_index_r];

		/*
		 * Note:
		 * When the bury point is triggered while the read operation is
		 * being performed, it is possible that 'now' is less than
		 * 'last_update_time', which can be resolved by using a mutex,
		 * but for performance reasons we will ignore it here
		 */
		delta = (now > last_update_time) ? now - last_update_time : 0;
		split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

		increase = get_kcpustat_increase(i);
		split_window(now, increase,
					&increase_a, &increase_cnt, &increase_b);

		idx = winidx_sub(now_index, win_idx);
		if (!delta_cnt && last_index == now_index) {
			if (win_idx == 0) {
				if (now_delta < JANK_WIN_SIZE_THRESHOLD) {
					tmp_fgtime = last_fgtime_r;
					tmp_df_time = last_df_time_r;
					tmp_fg_time = last_fg_time_r;
					tmp_bg_time = last_bg_time_r;
					tmp_topapp_time = last_topapp_time_r;
					tmp_runtime = last_runtime_r;
				} else {
					tmp_fgtime =
						last_fgtime + delta_top.delta + delta_fg.delta;
					tmp_df_time = last_df_time + delta_df.delta;
					tmp_fg_time = last_fg_time + delta_fg.delta;
					tmp_bg_time = last_bg_time + delta_bg.delta;
					tmp_topapp_time = last_topapp_time + delta_top.delta;
					tmp_runtime = last_runtime + increase;
				}
			} else {
				tmp_fgtime = cputime_snap[i].fg_running_time[idx];
				tmp_df_time = cputime_snap[i].default_runtime[idx];
				tmp_fg_time = cputime_snap[i].foreground_runtime[idx];
				tmp_bg_time = cputime_snap[i].background_runtime[idx];
				tmp_topapp_time = cputime_snap[i].topapp_runtime[idx];
				tmp_runtime = cputime_snap[i].running_time[idx];
			}
		} else if (!delta_cnt &&
				((last_index+1) & JANK_WIN_CNT_MASK) == now_index) {
			if (win_idx == 0) {
				if (now_delta < JANK_WIN_SIZE_THRESHOLD) {
					tmp_fgtime = last_fgtime;
					tmp_df_time = last_df_time;
					tmp_fg_time = last_fg_time;
					tmp_bg_time = last_bg_time;
					tmp_topapp_time = last_topapp_time;
					tmp_runtime = last_runtime;
				} else {
					tmp_fgtime = cputime_snap[i].fg_running_time[idx] +
							delta_top.delta_b + delta_fg.delta_b;
					tmp_df_time = cputime_snap[i].default_runtime[idx] +
							delta_df.delta_b;
					tmp_fg_time = cputime_snap[i].foreground_runtime[idx] +
							delta_fg.delta_b;
					tmp_bg_time = cputime_snap[i].background_runtime[idx] +
							delta_bg.delta_b;
					tmp_topapp_time = cputime_snap[i].topapp_runtime[idx] +
							delta_top.delta_b;
					tmp_runtime = cputime_snap[i].running_time[idx] +
							increase_b;
				}
			} else {
				if (idx == last_index) {
					if (last_win_delta < JANK_WIN_SIZE_THRESHOLD) {
						tmp_fgtime = last_fgtime_r;
						tmp_df_time = last_df_time_r;
						tmp_fg_time = last_fg_time_r;
						tmp_bg_time = last_bg_time_r;
						tmp_topapp_time = last_topapp_time_r;
						tmp_runtime = last_runtime_r;
					} else {
						tmp_fgtime = last_fgtime + delta_top.delta_a +
								delta_fg.delta_a;
						tmp_df_time = last_df_time + delta_df.delta_a;
						tmp_fg_time = last_fg_time + delta_fg.delta_a;
						tmp_bg_time = last_bg_time + delta_bg.delta_a;
						tmp_topapp_time = last_topapp_time +
								delta_top.delta_a;
						tmp_runtime = last_runtime + increase_a;
					}
				} else {
					tmp_fgtime = cputime_snap[i].fg_running_time[idx];
					tmp_df_time = cputime_snap[i].default_runtime[idx];
					tmp_fg_time = cputime_snap[i].foreground_runtime[idx];
					tmp_bg_time = cputime_snap[i].background_runtime[idx];
					tmp_topapp_time = cputime_snap[i].topapp_runtime[idx];
					tmp_runtime = cputime_snap[i].running_time[idx];
				}
			}
		} else if (delta_cnt < JANK_WIN_CNT) {
			if (win_idx <= delta_cnt) {
				tmp_fgtime = 0;
				tmp_df_time = 0;
				tmp_fg_time = 0;
				tmp_bg_time = 0;
				tmp_topapp_time = 0;
			} else if (idx == last_index) {
				tmp_fgtime = last_fgtime;
				tmp_df_time = last_df_time;
				tmp_fg_time = last_fg_time;
				tmp_bg_time = last_bg_time;
				tmp_topapp_time = last_topapp_time;
				tmp_runtime = last_runtime;
			} else {
				tmp_fgtime = cputime_snap[i].fg_running_time[idx];
				tmp_df_time = cputime_snap[i].default_runtime[idx];
				tmp_fg_time = cputime_snap[i].foreground_runtime[idx];
				tmp_bg_time = cputime_snap[i].background_runtime[idx];
				tmp_topapp_time = cputime_snap[i].topapp_runtime[idx];
				tmp_runtime = cputime_snap[i].running_time[idx];
			}
		} else {
			tmp_fgtime = 0;
			tmp_df_time = 0;
			tmp_fg_time = 0;
			tmp_bg_time = 0;
			tmp_topapp_time = 0;
		}

		/* Prevents the error of dividing by zero */
		tmp_runtime = (tmp_runtime == 0) ?
				JANK_WIN_SIZE_IN_NS : tmp_runtime;

		/* Deal with less than half a cycle */
		tmp_fgtime = min_t(u64, tmp_fgtime, tmp_runtime);
		tmp_df_time = min_t(u64, tmp_df_time, tmp_runtime);
		tmp_fg_time = min_t(u64, tmp_fg_time, tmp_runtime);
		tmp_bg_time = min_t(u64, tmp_bg_time, tmp_runtime);
		tmp_topapp_time = min_t(u64, tmp_topapp_time, tmp_runtime);

		tmp_avg = (tmp_fgtime * 100) / tmp_runtime;
		tmp_avg_df = (tmp_df_time * 100) / tmp_runtime;
		tmp_avg_fg = (tmp_fg_time * 100) / tmp_runtime;
		tmp_avg_bg = (tmp_bg_time * 100) / tmp_runtime;
		tmp_avg_topapp = (tmp_topapp_time * 100) / tmp_runtime;

		avg_df[i] = tmp_avg_df;
		avg_fg[i] = tmp_avg_fg;
		avg_bg[i] = tmp_avg_bg;
		avg_topapp[i] = tmp_avg_topapp;
	}

	for (cluster_id = 0; cluster_id < cluster_num; cluster_id++) {
		u64 rlt_df, rlt_fg, rlt_bg, rlt_top;

		start_cpu = cluster[cluster_id].start_cpu;
		end_cpu = start_cpu + cluster[cluster_id].cpu_nr - 1;
		cpu_nr = end_cpu - start_cpu + 1;

		for (i = start_cpu; i < end_cpu+1; i++) {
			if (i == start_cpu) {
				rlt_df = avg_df[i];
				rlt_fg = avg_fg[i];
				rlt_bg = avg_bg[i];
				rlt_top = avg_topapp[i];
			} else {
				rlt_df += avg_df[i];
				rlt_fg += avg_fg[i];
				rlt_bg += avg_bg[i];
				rlt_top += avg_topapp[i];
			}
		}
		seq_printf(m, "%d %d %d %d ",
			rlt_df/cpu_nr, rlt_fg/cpu_nr, rlt_bg/cpu_nr, rlt_top/cpu_nr);
	}
}

void jank_busy_ratio_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u32 i;
	u32 now_index, last_index, last_index_r, idx;
	u64 now_delta;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time, last_win_delta;
	u32 cls, start_cpu, end_cpu;
	u64 increase, increase_a, increase_cnt, increase_b;
	u64 tmp_avg, tmp_runtime;
	u64 last_runtime, last_runtime_r;
	u64 avg[CPU_NUMS];

	now_index = time2winidx(now);
	now_delta = now & JANK_WIN_SIZE_IN_NS_MASK;

	for (i = 0; i < CPU_NUMS; i++) {
		last_update_time = cputime_snap[i].last_update_time;
		last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;


		last_index = cputime_snap[i].last_index;
		last_runtime = cputime_snap[i].running_time[last_index];

		last_index_r = (last_index - 1 + JANK_WIN_CNT) & JANK_WIN_CNT_MASK;
		last_runtime_r = cputime_snap[i].running_time[last_index_r];

		/*
		 * Note:
		 * When the bury point is triggered while the read operation is
		 * being performed, it is possible that 'now' is less than
		 * 'last_update_time', which can be resolved by using a mutex,
		 * but for performance reasons we will ignore it here
		 */
		delta = (now > last_update_time) ? now - last_update_time : 0;
		split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

		/* Prevents the error of dividing by zero */
		last_win_delta = (last_win_delta == 0) ?
				JANK_WIN_SIZE_IN_NS : last_win_delta;

		increase = get_kcpustat_increase(i);
		split_window(now, increase,
				&increase_a, &increase_cnt, &increase_b);

		idx = winidx_sub(now_index, win_idx);
		if (!delta_cnt && last_index == now_index) {
			if (win_idx == 0) {
				if (now_delta < JANK_WIN_SIZE_THRESHOLD) {
					tmp_runtime = last_runtime_r;
					tmp_avg = (tmp_runtime * 100) >>
						JANK_WIN_SIZE_SHIFT_IN_NS;
				} else {
					tmp_runtime = last_runtime + increase;
					tmp_avg = (tmp_runtime * 100) / now_delta;
				}
			} else {
				tmp_avg = (cputime_snap[i].running_time[idx] * 100) >>
						JANK_WIN_SIZE_SHIFT_IN_NS;
			}
		} else if (!delta_cnt &&
			((last_index+1) & JANK_WIN_CNT_MASK) == now_index) {
			if (win_idx == 0) {
				if (now_delta < JANK_WIN_SIZE_THRESHOLD) {
					tmp_runtime = last_runtime + increase_a;
					tmp_avg = (tmp_runtime * 100) >>
						JANK_WIN_SIZE_SHIFT_IN_NS;
				} else {
					tmp_runtime = increase_b;
					tmp_avg = (tmp_runtime * 100) / now_delta;
				}
			} else {
				if (idx == last_index) {
					tmp_runtime = last_runtime + increase_a;
					tmp_avg = (tmp_runtime * 100) >>
						JANK_WIN_SIZE_SHIFT_IN_NS;
				} else {
					tmp_avg = (cputime_snap[i].running_time[idx] * 100) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
				}
			}
		} else if (delta_cnt < JANK_WIN_CNT) {
			if (win_idx <= delta_cnt) {
				tmp_avg = 0;
			} else if (idx == last_index) {
				if (last_win_delta < JANK_WIN_SIZE_THRESHOLD) {
					tmp_runtime = last_runtime_r;
					tmp_avg = (tmp_runtime * 100) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
				} else {
					tmp_runtime = last_runtime;
					tmp_avg = (tmp_runtime * 100) / last_win_delta;
				}
			} else {
				tmp_avg = (cputime_snap[i].running_time[idx] * 100) >>
							JANK_WIN_SIZE_SHIFT_IN_NS;
			}
		} else {
			tmp_avg = 0;
		}
		/* seq_printf(m, "%d ", tmp_avg); */
		avg[i] = tmp_avg;
	}

	for (cls = 0; cls < cluster_num; cls++) {
		u64 result;

		start_cpu = cluster[cls].start_cpu;
		end_cpu = start_cpu + cluster[cls].cpu_nr - 1;

		for (i = start_cpu; i < end_cpu+1; i++) {
			if (i == start_cpu)
				result = avg[i];
			else
				result += avg[i];
		}
		seq_printf(m, "%d ", result/(end_cpu-start_cpu+1));
	}
}

static int cpu_info_dump_win(struct seq_file *m, void *v, u32 win_cnt)
{
	u32 i;
	u64 now = jiffies_to_nsecs(jiffies);

	memcpy(cputime_snap, cputime, sizeof(struct cputime)*CPU_NUMS);

	for (i = 0; i < win_cnt; i++) {
		/* busy/total */
		jank_busy_ratio_show(m, i, now);

		/* fg/busy */
		jank_group_ratio_show(m, i, now);

		/* cpu_frep */
		jank_curr_freq_show(m, i, now);

		/* target_freq > policy->max */
		jank_burst_freq_show(m, i, now);

		/* target_freq > policy->cur */
		jank_increase_freq_show(m, i, now);

		/* online cpu */
		jank_onlinecpu_show(m, i, now);

		/* hot thread */
		jank_hotthread_show(m, i, now);

		seq_puts(m, "\n");
	}
#ifdef JANK_DEBUG
	/* seq_printf(m, "\n"); */
#endif

	return 0;
}

static int proc_cpu_info_show(struct seq_file *m, void *v)
{
	return cpu_info_dump_win(m, v, JANK_WIN_CNT);
}

static int proc_cpu_info_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_cpu_info_show, inode);
}

static int proc_cpu_info_sig_show(struct seq_file *m, void *v)
{
	return cpu_info_dump_win(m, v, 1);
}

static int proc_cpu_info_sig_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_cpu_info_sig_show, inode);
}

static int proc_cpu_load32_show(struct seq_file *m, void *v)
{
	u32 i;
	struct cpumask mask;

	for (i = 0; i < CPU_NUMS; i++)
		cpumask_set_cpu(i, &mask);

	seq_printf(m, "%d\n", get_cpu_load32(1, &mask));
	return 0;
}

static int proc_cpu_load32_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_cpu_load32_show, inode);
}

static int proc_cpu_load32_scale_show(struct seq_file *m, void *v)
{
	u32 i;
	struct cpumask mask;

	for (i = 0; i < CPU_NUMS; i++)
		cpumask_set_cpu(i, &mask);

	seq_printf(m, "%d\n", get_cpu_load32_scale(1, &mask));
	return 0;
}

static int proc_cpu_load32_scale_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_cpu_load32_scale_show, inode);
}

static int proc_cpu_load_show(struct seq_file *m, void *v)
{
	u32 i;
	struct cpumask mask;

	for (i = 0; i < CPU_NUMS; i++)
		cpumask_set_cpu(i, &mask);

	seq_printf(m, "%d\n", get_cpu_load(1, &mask));
	return 0;
}

static int proc_cpu_load_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_cpu_load_show, inode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_cpu_info_operations = {
	.open = proc_cpu_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_cpu_info_sig_operations = {
	.open = proc_cpu_info_sig_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_cpu_load_operations = {
	.open = proc_cpu_load_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_cpu_load32_operations = {
	.open = proc_cpu_load32_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_cpu_load32_scale_operations = {
	.open = proc_cpu_load32_scale_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
static const struct proc_ops proc_cpu_info_operations = {
	.proc_open	=	proc_cpu_info_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release =	single_release,
};

static const struct proc_ops proc_cpu_info_sig_operations = {
	.proc_open	=	proc_cpu_info_sig_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release =	single_release,
};

static const struct proc_ops proc_cpu_load_operations = {
	.proc_open	=	proc_cpu_load_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release =	single_release,
};

static const struct proc_ops proc_cpu_load32_operations = {
	.proc_open	=	proc_cpu_load32_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release =	single_release,
};

static const struct proc_ops proc_cpu_load32_scale_operations = {
	.proc_open	=	proc_cpu_load32_scale_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release =	single_release,
};
#endif

struct proc_dir_entry *jank_cpuload_proc_init(
			struct proc_dir_entry *pde)
{
	struct proc_dir_entry *entry = NULL;

	entry = proc_create("cpu_info", S_IRUGO,
				pde, &proc_cpu_info_operations);
	if (!entry) {
		pr_err("create cpu_info fail\n");
		goto err_cpu_info;
	}

	entry = proc_create("cpu_info_sig", S_IRUGO,
				pde, &proc_cpu_info_sig_operations);
	if (!entry) {
		pr_err("create cpu_info_sig fail\n");
		goto err_cpu_info_sig;
	}

	entry = proc_create("cpu_load", S_IRUGO, pde,
				&proc_cpu_load_operations);
	if (!entry) {
		pr_err("create cpu_load fail\n");
		goto err_cpu_load;
	}

	entry = proc_create("cpu_load32", S_IRUGO, pde,
				&proc_cpu_load32_operations);
	if (!entry) {
		pr_err("create cpu_load32 fail\n");
		goto err_cpu_load32;
	}

	entry = proc_create("cpu_load32_scale", S_IRUGO, pde,
				&proc_cpu_load32_scale_operations);
	if (!entry) {
		pr_err("create cpu_load32_scale fail\n");
		goto err_cpu_load32_scale;
	}
	return entry;

err_cpu_load32_scale:
	remove_proc_entry("cpu_load32", pde);

err_cpu_load32:
	remove_proc_entry("cpu_load", pde);

err_cpu_load:
	remove_proc_entry("cpu_info_sig", pde);

err_cpu_info_sig:
	remove_proc_entry("cpu_info", pde);

err_cpu_info:
	return NULL;
}

void jank_cpuload_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("cpu_load32_scale", pde);
	remove_proc_entry("cpu_load32", pde);
	remove_proc_entry("cpu_load", pde);
	remove_proc_entry("cpu_info_sig", pde);
	remove_proc_entry("cpu_info", pde);
}

void jank_cpuload_init(void)
{
	struct kernel_cpustat *kcs = NULL;
	u32 cpu;

	memset(cputime, 0, sizeof(struct cputime)*CPU_NUMS);
	memset(cputime_snap, 0, sizeof(struct cputime)*CPU_NUMS);

	for (cpu = 0; cpu < CPU_NUMS; cpu++) {
		kcs = &kcpustat_cpu(cpu);
		cpustat_bak[cpu][CPUTIME_USER] =
				kcs->cpustat[CPUTIME_USER];
		cpustat_bak[cpu][CPUTIME_NICE] =
				kcs->cpustat[CPUTIME_NICE];
		cpustat_bak[cpu][CPUTIME_SYSTEM] =
				kcs->cpustat[CPUTIME_SYSTEM];
		cpustat_bak[cpu][CPUTIME_IRQ] =
				kcs->cpustat[CPUTIME_IRQ];
		cpustat_bak[cpu][CPUTIME_SOFTIRQ] =
				kcs->cpustat[CPUTIME_SOFTIRQ];
		cpustat_bak[cpu][CPUTIME_STEAL] =
				kcs->cpustat[CPUTIME_STEAL];
		cpustat_bak[cpu][CPUTIME_GUEST] =
				kcs->cpustat[CPUTIME_GUEST];
		cpustat_bak[cpu][CPUTIME_GUEST_NICE] =
				kcs->cpustat[CPUTIME_GUEST_NICE];
	}
}

