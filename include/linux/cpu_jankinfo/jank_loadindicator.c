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

#include "jank_loadindicator.h"

struct load_record recorder;

#define time_ratio(time, win)	(time*100 / win / CPU_NUMS)
static void update_load_info(struct cputime *cputime,
				struct task_struct *p, u64 now)
{
	u32 i, idx, now_idx;
	u32 cpu;
	u64 span_time = 0;
	u64 running_time = 0;
	u64 topapp_runtime = 0;
	u64 background_runtime = 0;
	u64 default_runtime = 0;
	u64 foreground_runtime = 0;
	struct score score;

	if (!p)
		return;

	memset(&score, 0, sizeof(struct score));

	now_idx = time2winidx(now);
	span_time = now & JANK_WIN_SIZE_IN_NS_MASK;

	for (i = 0; i < MONITOR_WIN_CNT; i++) {
		idx = (now_idx + JANK_WIN_CNT - i) & JANK_WIN_CNT_MASK;

		running_time = 0;
		default_runtime = 0;
		foreground_runtime = 0;
		background_runtime = 0;
		topapp_runtime = 0;

		for (cpu = 0; cpu < CPU_NUMS; cpu++) {
			running_time += cputime[cpu].running_time[idx];

			default_runtime += cputime[cpu].default_runtime[idx];
			foreground_runtime += cputime[cpu].foreground_runtime[idx];
			background_runtime += cputime[cpu].background_runtime[idx];
			topapp_runtime += cputime[cpu].topapp_runtime[idx];
		}

		span_time = (i == 0) ?
				(now & JANK_WIN_SIZE_IN_NS_MASK) : JANK_WIN_SIZE_IN_NS;

		/* Prevents the error of dividing by zero */
		if (!span_time)
			span_time = JANK_WIN_SIZE_IN_NS;

		if (time_ratio(running_time, span_time) >=
			HIGH_LOAD_TOTAL)
			score.total++;
		else
			score.total = 0;

		if (time_ratio(default_runtime, span_time) >=
			HIGH_LOAD_DEFAULT)
			score.def++;
		else
			score.def = 0;

		if (time_ratio(foreground_runtime, span_time) >=
			HIGH_LOAD_FOREGROUND)
			score.fg++;
		else
			score.fg = 0;

		if (time_ratio(background_runtime, span_time) >=
			HIGH_LOAD_BACKGROUND)
			score.background++;
		else
			score.background = 0;

		if (time_ratio(topapp_runtime, span_time) >=
			HIGH_LOAD_TOPAPP)
			score.topapp++;
		else
			score.topapp = 0;
	}

	if (score.total >= MONITOR_WIN_CNT)
		recorder.total++;

	if (score.def >= MONITOR_WIN_CNT)
		recorder.def++;

	if (score.fg >= MONITOR_WIN_CNT)
		recorder.fg++;

	if (score.background >= MONITOR_WIN_CNT)
		recorder.background++;

	if (score.topapp >= MONITOR_WIN_CNT)
		recorder.topapp++;
}

void jank_loadindicator_update_win(struct cputime *cputime,
			struct task_struct *p, u64 now)
{
	update_load_info(cputime, p, now);
}

static int proc_load_indicator_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%llu %llu %llu %llu %llu\n",
		recorder.total, recorder.def, recorder.fg,
		recorder.background, recorder.topapp);

	return 0;
}

static int proc_load_indicator_open(struct inode *inode,
			struct file *file)
{
	return single_open(file, proc_load_indicator_show, inode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_load_indicator_operations = {
	.open = proc_load_indicator_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
static const struct proc_ops proc_load_indicator_operations = {
	.proc_open = proc_load_indicator_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

struct proc_dir_entry *jank_load_indicator_proc_init(
			struct proc_dir_entry *pde)
{
	return proc_create("load_indicator", S_IRUGO,
				pde, &proc_load_indicator_operations);
}

void jank_load_indicator_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("load_indicator", pde);
}

