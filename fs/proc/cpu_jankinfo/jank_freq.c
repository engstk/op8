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
#include "jank_freq.h"

struct cur_freq {
	u64 last_update_time;
	u32 freq[JANK_WIN_CNT];
};

struct count {
	u32 cnt;
	u64 timestamp;
};

struct freq_cnt {
	u64 last_update_time;
	struct count increase[JANK_WIN_CNT];
	struct count clamp[JANK_WIN_CNT];
};

struct cur_freq cur_freq[CPU_NUMS];
struct freq_cnt freq_cnt[CPU_NUMS];

void jank_currfreq_update_win(u64 now)
{
	u64 last_update_time;
	u32 idx, now_idx, last_idx;
	u32 freq;
	u32 win_cnt, i;
	u32 cpu;

	jank_dbg("e0\n");
	jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ, "0update-cur", 0, 1);

	now_idx = time2winidx(now);

	for (cpu = 0; cpu < CPU_NUMS; cpu++) {
		last_update_time = cur_freq[cpu].last_update_time;

		if (now < last_update_time) {
			cur_freq[cpu].last_update_time = now;
			continue;
		}

		win_cnt = time2wincnt(now, last_update_time);
		last_idx = time2winidx(last_update_time);
		freq = cur_freq[cpu].freq[last_idx];

		for (i = 0; i < win_cnt; i++) {
			idx = winidx_sub(now_idx, i);
			if (i == 0)
				freq = cpufreq_quick_get(cpu);

			cur_freq[cpu].freq[idx] = freq;

			jank_dbg("e1: cpu=%d, now_idx=%d, last_idx=%d, win_cnt=%d, "
						"idx=%d, i=%d, freq=%d\n",
						cpu, now_idx, last_idx, win_cnt,
						idx, i, freq);
		}
		cur_freq[cpu].last_update_time = now;
	}
}

void jank_curr_freq_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u32 cls;
	u32 start_cpu;
	u32 now_index, idx;


	now_index = time2winidx(now);
	idx = winidx_sub(now_index, win_idx);

	for (cls = 0; cls < cluster_num; cls++) {
		start_cpu = cluster[cls].start_cpu;
		seq_printf(m, "%d ", cur_freq[start_cpu].freq[idx]);

		jank_dbg("e3: cls=%d, start_cpu=%d, "
				"now_index=%d, win_idx=%d, idx=%d "
				"freq=%d\n",
				cls, start_cpu,
				now_index, win_idx, idx,
				cur_freq[start_cpu].freq[idx]);
	}
}

/* target_freq > policy->max */
static void update_freq_count(struct cpufreq_policy *policy,
			u32 old_freq, u32 new_freq, u32 flags)
{
	u64 last_update_time, now, delta, timestamp;
	u32 i, now_idx, win_cnt, idx;
	u32 cpu;
	bool freq_increase, freq_clamp;
	u32 target_freq;
	int index;

	if (!policy)
		return;

	index = policy->cached_resolved_idx;
	if (index < 0)
		return;

	target_freq = policy->freq_table[index].frequency;

	freq_increase = target_freq > policy->cur;
	freq_clamp = new_freq < old_freq;

#ifdef JANKINFO_DEBUG
	if (policy->cpu == 0) {
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"0flags", 0, flags);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"0increase", 0, freq_increase);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"1clamp", 0, freq_clamp);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"2cpu", 0, policy->cpu);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"3cur", 0, policy->cur);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"4min", 0, policy->min);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"5max", 0, policy->max);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"6target_freq", 0, target_freq);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"7new_freq", 0, new_freq);
		jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
			"8old_freq", 0, old_freq);
	}
#endif

	if (!freq_increase  && !freq_clamp)
		return;

	cpu = policy->cpu;

	last_update_time = freq_cnt[cpu].last_update_time;

	/*
	 * Note: the jiffies value will overflow 5 minutes after boot
	 */
	now = jiffies_to_nsecs(jiffies);
	if (unlikely(now < last_update_time)) {
		freq_cnt[cpu].last_update_time = now;
		delta = 0;
	} else {
		delta = now - last_update_time;
	}

	if (!delta)
		return;

	now_idx = time2winidx(now);
	win_cnt = time2wincnt(now, last_update_time) + 1;

	jank_dbg("m1: cpu=%d, old_freq=%d, new_freq=%d "
		"cur=%d, min=%d, max=%d "
		"freq_increase=%d, freq_clamp=%d "
		"now_idx=%d, last_idx=%d, win_cnt=%d\n",
		cpu, old_freq, new_freq,
		policy->cur, policy->min, policy->max,
		freq_increase, freq_clamp,
		now_idx, time2winidx(last_update_time), win_cnt);

	for (i = 0; i < win_cnt; i++) {
		idx = winidx_sub(now_idx, i);

		if (freq_increase) {
			timestamp = freq_cnt[cpu].increase[idx].timestamp;

			if (policy->cpu == 0) {
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"1win_cnt", 0, win_cnt);
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"2now_idx", 0, now_idx);
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"3idx", 0, idx);
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"4valid", 0, timestamp_is_valid(timestamp, now));
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"5cnt", 0, freq_cnt[cpu].increase[idx].cnt);
			}

			jank_dbg("m2: cpu=%d, i=%d, idx=%d, now_idx=%d, "
						"valid=%d, cnt=%d\n",
					cpu, i, idx, now_idx,
					timestamp_is_valid(timestamp, now),
					freq_cnt[cpu].clamp[idx].cnt);

			if (timestamp_is_valid(timestamp, now)) {
				if (i == 0) {
					freq_cnt[cpu].increase[idx].cnt++;
					freq_cnt[cpu].increase[idx].timestamp =  now;
				}
			} else {
				if (i == 0)
					freq_cnt[cpu].increase[idx].cnt = 1;
				else
					freq_cnt[cpu].increase[idx].cnt = 0;

				freq_cnt[cpu].increase[idx].timestamp =  now;
			}

			if (policy->cpu == 0) {
				jank_systrace_print_idx(JANK_SYSTRACE_CPUFREQ,
					"6cnt", 0, freq_cnt[cpu].increase[idx].cnt);
			}
		}

		if (freq_clamp) {
			timestamp = freq_cnt[cpu].clamp[idx].timestamp;

			jank_dbg("m3: cpu=%d, i=%d, idx=%d, now_idx=%d, "
					"valid=%d, cnt=%d\n",
					cpu, i, idx, now_idx,
					timestamp_is_valid(timestamp, now),
					freq_cnt[cpu].clamp[idx].cnt);

			if (timestamp_is_valid(timestamp, now)) {
				if (i == 0) {
					freq_cnt[cpu].clamp[idx].cnt++;
					freq_cnt[cpu].clamp[idx].timestamp =  now;
				}
			} else {
				if (i == 0)
					freq_cnt[cpu].clamp[idx].cnt = 1;
				else
					freq_cnt[cpu].clamp[idx].cnt = 0;

				freq_cnt[cpu].clamp[idx].timestamp =  now;
			}
		}
	}
	freq_cnt[cpu].last_update_time = now;
}

void jankinfo_update_freq_reach_limit_count(
			struct cpufreq_policy *policy,
			u32 old_target_freq, u32 new_target_freq, u32 flags)
{
	update_freq_count(policy, old_target_freq, new_target_freq, flags);
}

/* target_freq > policy->max */
void jank_burst_freq_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u64 timestamp;
	u32 tmp_cnt;
	u32 now_index, idx, i;
	struct cpufreq_policy *policy;

	now_index = time2winidx(now);
	idx = winidx_sub(now_index, win_idx);

	for (i = 0; i < CPU_NUMS; i++) {
		policy = cpufreq_cpu_get_raw(i);
		if (!policy)
			continue;

		if (policy->cpu != i)
			continue;

		timestamp = freq_cnt[i].clamp[idx].timestamp;
		if (!timestamp_is_valid(timestamp, now)) {
			seq_printf(m, "%d ", 0);
		} else {
			tmp_cnt = freq_cnt[i].clamp[idx].cnt;
			seq_printf(m, "%d ", tmp_cnt);
		}
	}
}

/* target_freq > policy->cur */
void jank_increase_freq_show(struct seq_file *m,
			u32 win_idx, u64 now)
{
	u32 tmp_cnt;
	u64 timestamp;
	u32 now_index, idx, i;
	struct cpufreq_policy *policy;

	now_index = time2winidx(now);
	idx = winidx_sub(now_index, win_idx);

	for (i = 0; i < CPU_NUMS; i++) {
		policy = cpufreq_cpu_get_raw(i);
		if (!policy)
			continue;

		if (policy->cpu != i)
			continue;

		timestamp = freq_cnt[i].increase[idx].timestamp;
		if (!timestamp_is_valid(timestamp, now)) {
			seq_printf(m, "%d ", 0);
		} else {
			tmp_cnt = freq_cnt[i].increase[idx].cnt;
			seq_printf(m, "%d ", tmp_cnt);
		}
	}
}

