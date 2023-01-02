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

#include "jank_onlinecpu.h"
#include "jank_topology.h"

struct number_cpus nb_cpus;

void jank_onlinecpu_reset(void)
{
	u32 i;
	u32 nr_cpus = num_active_cpus();

	for (i = 0; i < JANK_WIN_CNT; i++) {
		nb_cpus.nr[i].nb = nr_cpus;
		cpumask_copy(&nb_cpus.nr[i].mask, &all_cpu);
		nb_cpus.nr[i].timestamp = jiffies_to_nsecs(jiffies);
	}
}

/* unused */
void jank_onlinecpu_update_for_isolate(const struct cpumask *mask)
{
	u64 now;
	u32 now_idx, last_idx, idx;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time;
	u32 last_nb;
	u32 i;
	u32 nr_cpus = CPU_NUMS - cpumask_weight(mask);

	last_update_time = nb_cpus.last_update_time;

	/*
	 * Note: the jiffies value will overflow 5 minutes after boot
	 */
	now = jiffies_to_nsecs(jiffies);
	if (unlikely(now < last_update_time)) {
		nb_cpus.last_update_time = now;
		delta = 0;
	} else {
		delta = now - last_update_time;
	}

	if (!delta)
		return;

	now_idx = time2winidx(now);
	last_idx = nb_cpus.last_index;
	last_nb = nb_cpus.nr[last_idx].nb;

	split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

	if (!delta_cnt && last_idx == now_idx) {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);
		nb_cpus.nr[now_idx].timestamp = now;
	} else if (!delta_cnt && ((last_idx+1) & JANK_WIN_CNT_MASK) == now_idx) {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);
		nb_cpus.nr[now_idx].timestamp = now;

		nb_cpus.nr[last_idx].nb = last_nb; /* do nothing */
		cpumask_xor(&nb_cpus.nr[last_idx].mask, mask, &all_cpu);
		if (now > JANK_WIN_SIZE_IN_NS)
			nb_cpus.nr[last_idx].timestamp = now-JANK_WIN_SIZE_IN_NS;
		else
			nb_cpus.nr[last_idx].timestamp = 0;
	} else {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);
		nb_cpus.nr[now_idx].timestamp = now;

		for (i = 0; i < min_t(u64, delta_cnt, (JANK_WIN_CNT-1)); i++) {
			idx = (now_idx - 1 - i + JANK_WIN_CNT) & JANK_WIN_CNT_MASK;
			nb_cpus.nr[idx].nb = last_nb;
			cpumask_xor(&nb_cpus.nr[idx].mask, mask, &all_cpu);
			if (now > JANK_WIN_SIZE_IN_NS*(i+1))
				nb_cpus.nr[idx].timestamp = now-JANK_WIN_SIZE_IN_NS*(i+1);
			else
				nb_cpus.nr[idx].timestamp = 0;
		}

		if (delta_a) {
			idx = (now_idx + JANK_WIN_CNT - 1 -
					min_t(u64, delta_cnt, JANK_WIN_CNT)) & JANK_WIN_CNT_MASK;
			nb_cpus.nr[idx].nb = last_nb;
			cpumask_xor(&nb_cpus.nr[idx].mask, mask, &all_cpu);
			if (now > JANK_WIN_SIZE_IN_NS*(i+1))
				nb_cpus.nr[idx].timestamp = now-JANK_WIN_SIZE_IN_NS*(i+1);
			else
				nb_cpus.nr[idx].timestamp = 0;
		}
	}

	/* update */
	nb_cpus.last_index = now_idx;
	nb_cpus.last_update_time = now;
}

void jank_onlinecpu_update(const struct cpumask *mask)
{
	u64 now;
	u32 now_idx, last_idx, idx;
	u64 delta, delta_a, delta_cnt, delta_b;
	u64 last_update_time;
	u32 last_nb;
	u32 i;
	u32 nr_cpus = cpumask_weight(mask);

	last_update_time = nb_cpus.last_update_time;

	/*
	 * Note: the jiffies value will overflow 5 minutes after boot
	 */
	now = jiffies_to_nsecs(jiffies);
	if (unlikely(now < last_update_time)) {
		nb_cpus.last_update_time = now;
		delta = 0;
	} else {
		delta = now - last_update_time;
	}

	if (!delta)
		return;

	now_idx = time2winidx(now);
	last_idx = nb_cpus.last_index;
	last_nb = nb_cpus.nr[last_idx].nb;

	split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

	if (!delta_cnt && last_idx == now_idx) {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		/*cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);*/
		cpumask_copy(&nb_cpus.nr[now_idx].mask, mask);
		nb_cpus.nr[now_idx].timestamp = now;
	} else if (!delta_cnt && ((last_idx+1) & JANK_WIN_CNT_MASK) == now_idx) {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		/*cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);*/
		cpumask_copy(&nb_cpus.nr[now_idx].mask, mask);
		nb_cpus.nr[now_idx].timestamp = now;

		nb_cpus.nr[last_idx].nb = last_nb; /* do nothing */
		/*cpumask_xor(&nb_cpus.nr[last_idx].mask, mask, &all_cpu);*/
		cpumask_copy(&nb_cpus.nr[last_idx].mask, mask);
		if (now > JANK_WIN_SIZE_IN_NS)
			nb_cpus.nr[last_idx].timestamp = now-JANK_WIN_SIZE_IN_NS;
		else
			nb_cpus.nr[last_idx].timestamp = 0;
	} else {
		nb_cpus.nr[now_idx].nb = nr_cpus;
		/*cpumask_xor(&nb_cpus.nr[now_idx].mask, mask, &all_cpu);*/
		cpumask_copy(&nb_cpus.nr[now_idx].mask, mask);
		nb_cpus.nr[now_idx].timestamp = now;

		for (i = 0; i < min_t(u64, delta_cnt, (JANK_WIN_CNT-1)); i++) {
			idx = (now_idx - 1 - i + JANK_WIN_CNT) & JANK_WIN_CNT_MASK;
			nb_cpus.nr[idx].nb = last_nb;
			/*cpumask_xor(&nb_cpus.nr[idx].mask, mask, &all_cpu);*/
			cpumask_copy(&nb_cpus.nr[idx].mask, mask);
			if (now > JANK_WIN_SIZE_IN_NS*(i+1))
				nb_cpus.nr[idx].timestamp = now-JANK_WIN_SIZE_IN_NS*(i+1);
			else
				nb_cpus.nr[idx].timestamp = 0;
		}

		if (delta_a) {
			idx = (now_idx + JANK_WIN_CNT - 1 -
					min_t(u64, delta_cnt, JANK_WIN_CNT)) & JANK_WIN_CNT_MASK;
			nb_cpus.nr[idx].nb = last_nb;
			/*cpumask_xor(&nb_cpus.nr[idx].mask, mask, &all_cpu);*/
			cpumask_copy(&nb_cpus.nr[idx].mask, mask);
			if (now > JANK_WIN_SIZE_IN_NS*(i+1))
				nb_cpus.nr[idx].timestamp = now-JANK_WIN_SIZE_IN_NS*(i+1);
			else
				nb_cpus.nr[idx].timestamp = 0;
		}
	}

	/* update */
	nb_cpus.last_index = now_idx;
	nb_cpus.last_update_time = now;
}


void jank_onlinecpu_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u64 timestamp;
	struct cpumask mask, last_mask;
	u32 last_index;
	u64 last_update_time, last_win_delta;

	u64 delta, delta_a, delta_cnt, delta_b;
	u32 idx, now_index;
	u32 nr_cpus, last_nr_cpus;

	now_index =  time2winidx(now);

	last_index = nb_cpus.last_index;
	last_update_time = nb_cpus.last_update_time;
	last_win_delta = last_update_time & JANK_WIN_SIZE_IN_NS_MASK;
	last_nr_cpus = nb_cpus.nr[last_index].nb;
	cpumask_copy(&last_mask, &nb_cpus.nr[last_index].mask);

	/*
	 * Note:
	 * When the bury point is triggered while the read operation is
	 * being performed, it is possible that 'now' is less than
	 * 'last_update_time', which can be resolved by using a mutex,
	 * but for performance reasons we will ignore it here
	 */
	delta = (now > last_update_time) ? now - last_update_time : 0;
	split_window(now, delta, &delta_a, &delta_cnt, &delta_b);

	idx = (now_index + JANK_WIN_CNT - win_idx) & JANK_WIN_CNT_MASK;
	timestamp = nb_cpus.nr[idx].timestamp;
	if (!timestamp_is_valid(timestamp, now)) {
		seq_printf(m, "{%*pbl} ", cpumask_pr_args(&last_mask));
	} else {
		nr_cpus = nb_cpus.nr[idx].nb;
		cpumask_copy(&mask, &nb_cpus.nr[idx].mask);
		seq_printf(m, "{%*pbl} ", cpumask_pr_args(&mask));
	}
}
