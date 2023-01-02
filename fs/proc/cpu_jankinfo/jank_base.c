// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "jank_base.h"

#ifdef JANK_SYSTRACE_DEBUG
#ifdef JANK_DEBUG
u32 systrace_enable = 1;
#else
u32 systrace_enable;
#endif
module_param(systrace_enable, int, 0644);
MODULE_PARM_DESC(systrace_enable,
		"This variable controls whether systrace information is output");

static void __maybe_unused tracing_mark_write(char *buf)
{
	preempt_disable();
	trace_printk(buf);
	preempt_enable();
}

#define JANK_TRACE_LEN			256
void __maybe_unused jank_systrace_print(u32 pid, char *tag, u64 val)
{
	char buf[JANK_TRACE_LEN];

	if (likely(!systrace_enable))
		return;

	snprintf(buf, sizeof(buf), "C|%d|%s|%llu\n", pid, tag, val);
	tracing_mark_write(buf);
}

void __maybe_unused jank_systrace_print_idx(u32 pid,
		char *tag, u32 idx, u64 val)
{
	char buf[JANK_TRACE_LEN];

	if (likely(!systrace_enable))
		return;

	snprintf(buf, sizeof(buf), "C|%d|%s$%d|%llu\n", pid, tag, idx, val);
	tracing_mark_write(buf);
}
#endif

#if defined(CONFIG_CGROUP_SCHED)
#if defined(CONFIG_SCHED_TUNE)
int task_cgroup_id(struct task_struct *p)
{
	struct cgroup_subsys_state *css = task_css(p, schedtune_cgrp_id);

	return css ? css->id : -EFAULT;
}
#else
int task_cgroup_id(struct task_struct *task)
{
	struct cgroup_subsys_state *css = task_css(task, cpu_cgrp_id);

	return css ? css->id : -EFAULT;
}
#endif
#else
int task_cgroup_id(struct task_struct *p)
{
	return -EFAULT;
}
#endif

bool is_fg_or_topapp(struct task_struct *p)
{
	int class;

	if (!p)
		return false;

	class = task_cgroup_id(p);
	if (class == CGROUP_FOREGROUND || class == CGROUP_TOP_APP)
		return true;

	return false;
}

bool is_default(struct task_struct *p)
{
	if (!p)
		return false;

	if (task_cgroup_id(p) == CGROUP_DEFAULT)
		return true;

	return false;
}

bool is_foreground(struct task_struct *p)
{
	if (!p)
		return false;

	if (task_cgroup_id(p) == CGROUP_FOREGROUND)
		return true;

	return false;
}

bool is_background(struct task_struct *p)
{
	if (!p)
		return false;

	if (task_cgroup_id(p) == CGROUP_BACKGROUND)
		return true;

	return false;
}

bool is_topapp(struct task_struct *p)
{
	if (!p)
		return false;

	if (task_cgroup_id(p) == CGROUP_TOP_APP)
		return true;

	return false;
}

bool is_same_idx(u64 timestamp, u64 now)
{
	return time2idx(now) == time2idx(timestamp);
}

bool is_same_window(u64 timestamp, u64 now)
{
	return time2winidx(now) == time2winidx(timestamp);
}

/*
 * Determine if the data is valid
 * Invalid when data exceeds 16 window cycles
 */
bool timestamp_is_valid(u64 timestamp, u64 now)
{
	u64 win_cnt;

	if (now < timestamp)
		return false;

	win_cnt = time2idx(now) - time2idx(timestamp);

	if (win_cnt >= JANK_WIN_CNT)
		return false;
	else
		return true;
}

u32 split_window(u64 now, u64 delta,
		u64 *win_a, u64 *win_cnt, u64 *win_b)
{
	if (!win_a || !win_cnt || !win_b)
		return 0;

	if (!delta) {
		*win_a = *win_cnt = *win_b = 0;
		return 0;
	}

	*win_b = min_t(u64, delta, (now & JANK_WIN_SIZE_IN_NS_MASK));
	*win_cnt = (delta - *win_b) >> JANK_WIN_SIZE_SHIFT_IN_NS;
	*win_a = delta - *win_b - (*win_cnt << JANK_WIN_SIZE_SHIFT_IN_NS);

	return *win_cnt + !!(*win_b) + !!(*win_a);
}

