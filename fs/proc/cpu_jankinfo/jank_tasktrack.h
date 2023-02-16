/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPU_JANK_TASKTRACK_H__
#define __OPLUS_CPU_JANK_TASKTRACK_H__

#include <uapi/linux/android/binderfs.h>

#include "jank_base.h"
#include "../../../drivers/android/binder_internal.h"

#define TASK_TRACK_NUM					4
#define INVALID_PID						(-1)

enum trace_type {
	INVALID_TRACE_TYPE = -1,
	TRACE_RUNNING = 0,	/* returned by ct_state() if unknown */
	TRACE_RUNNABLE,
	TRACE_SLEEPING,
	TRACE_SLEEPING_INBINDER,
	TRACE_SLEEPING_INFUTEX,
	TRACE_DISKSLEEP,
	TRACE_DISKSLEEP_INIOWAIT,

	TRACE_IRQ,
	TRACE_OTHER,
	TRACE_CNT,
};

struct state_time {
	u64 val[TRACE_CNT];
};

#define CALL_STACK_CNT_SHIFT		2
#define CALL_STACK_CNT				(1 << CALL_STACK_CNT_SHIFT)
#define CALL_STACK_CNT_MASK			(CALL_STACK_CNT-1)
#define CALL_STACK_LEVEL			4
#define SKIP_LEVEL					1

#ifdef JANK_DEBUG
#define CALL_STACK_THRESHOLD		(1*1000*1000)
#else
#define CALL_STACK_THRESHOLD		(50*1000*1000)
#endif

struct callstacks {
	unsigned long func[CALL_STACK_CNT][CALL_STACK_LEVEL];
	u32 id;
	u64 last_update_time;
};

struct jank_task_info {
	pid_t pid;
	u64 delta[TRACE_CNT];
	u64 delta_lastwin[TRACE_CNT];
	u64 delta_win_align[TRACE_CNT];
	u64 borrowed_time[TRACE_CNT];
	u64 last_update_time;
	unsigned long now_type;
	u32 winidx;
	struct state_time time[JANK_WIN_CNT];

	/* records the call stack information */
	struct callstacks cs[JANK_WIN_CNT];
};

struct task_track_info {
	u32 task_num;
	struct jank_task_info task_info[TASK_TRACK_NUM];
};

void tasktrack_init(void);
void tasktrack_deinit(void);
struct task_struct *jank_find_get_task_by_vpid(pid_t nr);
struct proc_dir_entry *jank_tasktrack_proc_init(
		struct proc_dir_entry *pde);
void jank_tasktrack_proc_deinit(struct proc_dir_entry *pde);

void jankinfo_tasktrack_update_time(struct task_struct *task,
			unsigned long type, u64 time_in_ns);
void android_vh_futex_sleep_start_handelr(void *unused,
			struct task_struct *p);
void android_vh_binder_wait_for_work_hanlder(void *unused,
		bool do_proc_work, struct binder_thread *tsk,
		struct binder_proc *proc);


#endif  /* endif */
