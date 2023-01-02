/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_JANK_MONITOR_H_
#define _OPLUS_JANK_MONITOR_H_

enum {
    JANK_TRACE_RUNNABLE = 0,
    JANK_TRACE_DSTATE,
    JANK_TRACE_SSTATE,
    JANK_TRACE_RUNNING,
};

struct jank_d_state {
	u64 iowait_ns;
	u64 downread_ns;
	u64 downwrite_ns;
	u64 mutex_ns;
	u64 other_ns;
	int cnt;
};

struct jank_s_state{
	u64 binder_ns;
	u64 epoll_ns;
	u64 futex_ns;
	u64 other_ns;
	int cnt;
};

struct jank_monitor_info {
	u64 runnable_state;
	u64 ltt_running_state; /* ns */
	u64 big_running_state; /* ns */
	struct jank_d_state d_state;
	struct jank_s_state s_state;
};

extern const struct file_operations proc_jank_trace_operations;

#endif /*_OPLUS_JANK_MONITOR_H_*/
