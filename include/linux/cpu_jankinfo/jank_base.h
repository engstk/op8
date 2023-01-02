/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_BASE_H__
#define __OPLUS_CPU_JANK_BASE_H__

#include <linux/cgroup.h>
#include <linux/string.h>
#include <linux/kernel_stat.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include <linux/cpufreq.h>
#include <linux/processor.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/cpumask.h>
#include <linux/kallsyms.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/trace_events.h>
#include <linux/compat.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/sched.h>

#include "../../../kernel/sched/sched.h"

#if IS_ENABLED(CONFIG_JANK_DEBUG)
#define JANK_DEBUG
#define JANK_SYSTRACE_DEBUG
#endif

#ifdef JANK_DEBUG
#define jank_dbg(fmt, args...)  pr_info("[BYHP:%s:%d] " fmt, __func__, __LINE__, ##args)
#else
#define jank_dbg(fmt, args...)
#endif

#ifdef JANK_USE_HOOK
#include "../sched_assist/sa_common.h"
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/cpufreq.h>
#include <trace/events/task.h>
#include <trace/events/sched.h>

#define REGISTER_TRACE_VH(vender_hook, handler) { \
		ret = register_trace_##vender_hook(handler, NULL); \
		if (ret) { \
			pr_err("failed to register_trace_"#vender_hook", ret=%d\n", ret); \
			return ret; \
		} \
	}

#define UNREGISTER_TRACE_VH(vender_hook, handler) { \
		unregister_trace_##vender_hook(handler, NULL); \
	}

#define REGISTER_TRACE_RVH		REGISTER_TRACE_VH


#ifdef VENDOR_DEBUG
#define UNREGISTER_TRACE_RVH	UNREGISTER_TRACE_VH
#else
#define UNREGISTER_TRACE_RVH(vender_hook, handler)
#endif

#endif


#define CPU_NUMS						8
#define INVALID_CPUID					(-1)


/* 1024*1024 ns in a ms */
#define JANK_NS_PER_MS_SHIFT			20
#define JANK_NS_PER_MS_MASK				((1 << JANK_NS_PER_MS_SHIFT)-1)


/* window size : 128ms */
#define JANK_WIN_SIZE_SHIFT				7
#define JANK_WIN_SIZE					(1 << JANK_WIN_SIZE_SHIFT)
#define JANK_WIN_SIZE_MASK				((1 << JANK_WIN_SIZE_SHIFT)-1)

#define JANK_WIN_SIZE_SHIFT_IN_NS \
				(JANK_WIN_SIZE_SHIFT + JANK_NS_PER_MS_SHIFT)
#define JANK_WIN_SIZE_IN_NS				(1 << JANK_WIN_SIZE_SHIFT_IN_NS)
#define JANK_WIN_SIZE_IN_NS_MASK \
				((1 << JANK_WIN_SIZE_SHIFT_IN_NS)-1)


/* Half of the window */
#define JANK_WIN_TICKS					(JANK_WIN_SIZE_IN_NS/TICK_NSEC)
#define JANK_RECORD_THRESHOLD			(JANK_WIN_TICKS >> 1)

/* thresholdis 1/2 */
#define JANK_WIN_SIZE_THRESHOLD_SHIFT	(1)
#define JANK_WIN_SIZE_THRESHOLD \
			(JANK_WIN_SIZE_IN_NS >> JANK_WIN_SIZE_THRESHOLD_SHIFT)


/* window count : 64 */
#define JANK_WIN_CNT_SHIFT				6
#define JANK_WIN_CNT					(1 << JANK_WIN_CNT_SHIFT)
#define JANK_WIN_CNT_MASK				((1 << JANK_WIN_CNT_SHIFT)-1)


#define time2idx(timestamp) \
			(((timestamp) >> JANK_WIN_SIZE_SHIFT_IN_NS) + \
			!!((timestamp) & JANK_WIN_SIZE_IN_NS_MASK))
#define time2winidx(timestamp) \
			((time2idx(timestamp)) & JANK_WIN_CNT_MASK)
#define time2wincnt(now, last) \
			(min_t(u32, (time2idx(now) - time2idx(last)), JANK_WIN_CNT))

#define winidx_add(idx, n) \
			((idx + n) & JANK_WIN_CNT_MASK)
#define winidx_sub(idx, n) \
			(((idx) + JANK_WIN_CNT - (n)) & JANK_WIN_CNT_MASK)



#define get_record_winidx(timestamp) \
		(((timestamp) >> JANK_WIN_SIZE_SHIFT_IN_NS) & RECOED_WINIDX_MASK)



#ifdef JANK_SYSTRACE_DEBUG
#define JANK_SYSTRACE_PID				12345
#define JANK_SYSTRACE_TASKTRACK			99999
#define JANK_SYSTRACE_CPUFREQ			88888
void jank_systrace_print(u32 pid, char *tag, u64 val);
void jank_systrace_print_idx(u32 pid,
			char *tag, u32 idx, u64 val);
#else
#define jank_systrace_print(pid, tag, val)
#define jank_systrace_print_idx(pid, tag, idx, val)
#endif

enum {
	CGROUP_RESV = 0,
	CGROUP_DEFAULT = 1,			/* sys */
	CGROUP_FOREGROUND,
	CGROUP_BACKGROUND,
	CGROUP_TOP_APP,

	CGROUP_NRS,
};

bool is_fg_or_topapp(struct task_struct *p);
bool is_default(struct task_struct *p);
bool is_foreground(struct task_struct *p);
bool is_background(struct task_struct *p);
bool is_topapp(struct task_struct *p);
u32 split_window(u64 now, u64 delta,
			u64 *win_a, u64 *win_cnt, u64 *win_b);
bool is_same_idx(u64 timestamp, u64 now);
bool is_same_window(u64 timestamp, u64 now);
bool timestamp_is_valid(u64 timestamp, u64 now);

#endif  /* endif */
