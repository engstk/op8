// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kallsyms.h>
#include <linux/trace_events.h>

#include "jank_tasktrack.h"
#include "sa_jankinfo.h"

#if IS_ENABLED(CONFIG_JANK_CPUSET)
#include "jank_cpuset.h"
#endif

static struct hrtimer tasktrack_timer;
static struct task_track_info *trace_info;
static struct task_track_info *trace_info_snap;

#ifdef JANK_DEBUG
u32 tasktrack_enable = 1;
#else
u32 tasktrack_enable;
#endif

bool is_running(unsigned long nowtype)
{
	return !nowtype;
}

bool is_sleeping(unsigned long nowtype)
{
	return test_bit(TRACE_SLEEPING, &nowtype);
}

bool is_inbinder(unsigned long nowtype)
{
	return test_bit(TRACE_SLEEPING_INBINDER, &nowtype);
}

bool is_infutex(unsigned long nowtype)
{
	return test_bit(TRACE_SLEEPING_INFUTEX, &nowtype);
}

void mark_binder(unsigned long *nowtype)
{
	set_bit(TRACE_SLEEPING_INBINDER, nowtype);
}

void mark_futex(unsigned long *nowtype)
{
	set_bit(TRACE_SLEEPING_INFUTEX, nowtype);
}

bool is_task_traced(struct task_struct *p, u32 *idx)
{
	pid_t pid;
	u32 task_num, i;

	if (!trace_info || !p)
		return false;

	task_num = trace_info->task_num;
	if (!task_num)
		return false;

	pid = p->pid;

	for (i = 0; i < TASK_TRACK_NUM; i++) {
		if (pid == trace_info->task_info[i].pid)
			break;
	}

	if (i >= TASK_TRACK_NUM)
		return false;

	if (idx)
		*idx = i;

	return true;
}

struct task_struct *jank_find_get_task_by_vpid(pid_t nr)
{
	struct task_struct *task = NULL;

	rcu_read_lock();
	task = find_task_by_vpid(nr);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	return task;
}

bool is_current_type(unsigned long nr, unsigned long nowtype)
{
	bool ret = false;

	if (!nr && !(nowtype & ~BIT(TRACE_RUNNING)))
		ret = true;
	else
		ret = test_bit(nr, &nowtype);

	jank_dbg("d3: nr=%d, nowtype=%d, ret=%d\n", nr, nowtype, ret);
	return ret;
}

/***************************************************************
 * The following function is called at each window boundary
 * to record the time in the IRQ state within that window
 ***************************************************************/
u64 get_irq_time(int update)
{
	int i;
	u64 ret;
	u64 t_irq = 0;
	static u64 t_irq_lastwin;
	struct kernel_cpustat *kcs;

	for (i = 0; i < CPU_NUMS; i++) {
		kcs = &kcpustat_cpu(i);

		t_irq = kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		t_irq += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
	}

	/* Consider the first window */
	if (likely(t_irq_lastwin))
		ret = t_irq - t_irq_lastwin;
	else
		ret = 0;

	if (update)
		t_irq_lastwin = t_irq;

	return ret;
}

/* The following functions will be executed in each window */
void update_tasktrack_time_win(void)
{
	u32 task_num, idx, winidx;
	u64 delta;
	unsigned long type, nowtype;
	u64 last_update_time;
	u64 now;
	u64 unprocessed_delta;
	struct jank_task_info *ti = NULL;
	u64 delta_snap[TRACE_CNT];
	pid_t pid;
	u64 t_irq;

#ifdef JANK_DEBUG
	u64 dbg_delta = 0;
	u64 dbg_unprocessed_delta;

	u64 dbg_delta1, dbg_delta2;
	u64 dbg_task_info_delta, dbg_task_info_delta_bak;
	u64 dbg_borrowed_time;
	u64 dbg_delta_win_align;
	u64 dbg_delta_win_align_bak;
	static u64 dbg_tick;

	jank_dbg("we3: execute once for each window\n");
	jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK, "debug0_tick", 0, dbg_tick);
	dbg_tick = !dbg_tick;
#endif

	if (!tasktrack_enable || !trace_info)
		return;

	task_num = trace_info->task_num;
	if (!task_num)
		return;

	t_irq = get_irq_time(1);

	now = jiffies_to_nsecs(jiffies);
	for (idx = 0; idx < TASK_TRACK_NUM; idx++) {
		ti = &trace_info->task_info[idx];

		pid = ti->pid;
		if (!pid || pid == INVALID_PID)
			continue;

		/*************************************************
		 * NOTE:
		 *   ti->delta may be updated in
		 *   jankinfo_tasktrack_update_time,
		 *   so we take a snapshot here
		 *************************************************/
		memcpy(&delta_snap, &ti->delta, sizeof(u64)*TRACE_CNT);
		winidx = ti->winidx;
		winidx = winidx_add(winidx, 1);

		memset(&ti->time[winidx], 0, sizeof(struct state_time));

		nowtype = ti->now_type;
		last_update_time = ti->last_update_time;

		/*************************************************
		 * TODO: BUG HERE
		 *    Because no lock is used, there may
		 *    be cases where the value is less than 0
		 *************************************************/
		if (now < last_update_time) {
			unprocessed_delta = 0;
			jank_dbg("WARNING1: an unexpected thing happened!!!\n");
		} else
			unprocessed_delta = now - last_update_time;

#ifdef JANK_DEBUG
		dbg_unprocessed_delta = unprocessed_delta;
#endif

		jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
				"debug5_unprocessed_delta",
				idx, unprocessed_delta/1000/1000);
		jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
				"debug5_nowtype", idx, nowtype);

		for (type = TRACE_RUNNING; type < TRACE_CNT; type++) {
			if (type == TRACE_IRQ) {
				ti->time[winidx].val[type] = t_irq;
#ifdef JANK_DEBUG
				jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
					"debug5_irq", idx, nowtype);
				jank_dbg("debug5_irq: %llu(%llu)\n", t_irq/1000/1000, t_irq);
#endif
				continue;
			}

			jank_dbg("m0: type=%d, dbg_borrowed_time=%llu\n",
					type, ti->borrowed_time[type]);

			/*****************************************************
			 * the following branche will be executed, when a task
			 * is in a state for a long time, eg sleep
			 *****************************************************/
			if (unprocessed_delta > JANK_WIN_SIZE_IN_NS) {
				if (is_current_type(type, nowtype)) {
					if (ti->delta_lastwin[type] == 0) {
						/*************************************************
						 * NOTE:
						 *    This is a temporary solution, for the first
						 *    window after the beginning of the tracking
						 *************************************************/
						delta = 0;
					} else {
						delta = JANK_WIN_SIZE_IN_NS;
						ti->borrowed_time[type] += JANK_WIN_SIZE_IN_NS;
					}
					jank_dbg("m1: type=%d, dbg_borrowed_time=%llu\n",
						type,
						ti->borrowed_time[type]);

				} else {
					delta = 0;
					ti->borrowed_time[type] = 0;
					jank_dbg("m2: type=%d, dbg_borrowed_time=%llu\n",
						type,
						ti->borrowed_time[type]);
				}
			} else {
				delta = delta_snap[type] - ti->delta_lastwin[type];

				jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
							"debug6_delta", idx, delta/1000/1000);

#ifdef JANK_DEBUG
				dbg_delta1 = delta;
				dbg_task_info_delta = delta_snap[type];
				dbg_task_info_delta_bak = ti->delta_lastwin[type];
#endif

				if ((ti->delta_lastwin[type]) == 0) {
					ti->borrowed_time[type] = 0;

					/****************************************************
					 * NOTE:
					 *     This is a temporary solution, for the first
					 *     window after the beginning of the tracking
					 ****************************************************/
					delta = min_t(u64, delta, JANK_WIN_SIZE_IN_NS_MASK);
				} else {
					if (delta < ti->borrowed_time[type]) {
						delta  = 0;
						jank_dbg("WARNING2: an unexpected thing happened!!!\n");
					} else
						delta -= ti->borrowed_time[type];
				}
				jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
					"debug7_delta", idx, delta);

				ti->delta_lastwin[type] = delta_snap[type];

#ifdef JANK_DEBUG
				dbg_delta2 = delta;
				dbg_borrowed_time = ti->borrowed_time[type];
				jank_dbg("m3: type=%d, dbg_borrowed_time=%llu\n",
						type, ti->borrowed_time[type]);
#endif

				ti->borrowed_time[type] = 0;
				if (is_current_type(type, nowtype)) {
					delta += unprocessed_delta;
					ti->borrowed_time[type] += unprocessed_delta;
					jank_dbg("m4: type=%d, dbg_borrowed_time=%llu\n",
						type, ti->borrowed_time[type]);
				}
			}

#ifdef JANK_DEBUG
			dbg_delta_win_align_bak = ti->delta_win_align[type];
#endif

			ti->delta_win_align[type] =
				delta_snap[type] + ti->borrowed_time[type];
#ifdef JANK_DEBUG
			dbg_delta_win_align = ti->delta_win_align[type];
#endif

			ti->time[winidx].val[type] = delta;
			jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
					"debug8_delta", idx, delta);

#ifdef JANK_DEBUG
			dbg_delta += delta;
			jank_dbg("m5: winidx=%d, type=%d, nowtype=%d, "
					"dbg_unprocessed_delta=%llu(%llu), delta=%llu(%llu), "
					"dbg_delta1=%llu, dbg_task_info_delta=%llu, "
					"dbg_task_info_delta_bak=%llu, "
					"dbg_delta2=%llu, dbg_borrowed_time=%llu, "
					"dbg_delta_win_align=%llu, "
					"\n",
					winidx, type, nowtype,
					dbg_unprocessed_delta, dbg_unprocessed_delta/1000/1000,
					delta, delta/1000/1000,
					dbg_delta1, dbg_task_info_delta,
					dbg_task_info_delta_bak,
					dbg_delta2, dbg_borrowed_time,
					(dbg_delta_win_align-dbg_delta_win_align_bak)/1000/1000);
#endif
		}
		ti->winidx = winidx;
		jank_dbg("m6: winidx=%d, dbg_delta=%llu(%llu)\n",
				winidx, dbg_delta/1000/1000, dbg_delta);
	}
#if IS_ENABLED(CONFIG_JANK_CPUSET)
	jank_cpuset_adjust(trace_info);
#endif
}

static enum hrtimer_restart tasktrack_timer_handler(
			struct hrtimer *timer)
{
	update_tasktrack_time_win();

	hrtimer_forward_now(timer, ns_to_ktime(JANK_WIN_SIZE_IN_NS));
	return HRTIMER_RESTART;
}

static void jankinfo_update_task_status_cb(struct task_struct *p,
			unsigned long type)
{
	u32 idx;
	unsigned long nowtype;
	unsigned long tmptype;

	if (!trace_info || !p || !is_task_traced(p, &idx))
		return;

	/* Ignore illegal types */
	if (type >= TRACE_CNT)
		return;

	tmptype = BIT(type);

	/*****************************************************
	 * NOTE:
	 *   trace_info->task_info[idx].now_type
	 *   This variable may be updated in
	 *   android_vh_futex_sleep_start_handelr
	 *   or android_vh_binder_wait_for_work_hanlder
	 *****************************************************/
	nowtype = trace_info->task_info[idx].now_type;
#ifdef JANK_DEBUG
	jank_systrace_print(p->pid, "dbg992_lasttype", nowtype);
#endif

	/*****************************************************
	 * NOTE:
	 *   iowait will update in sched_stat_iowait_handler
	 *****************************************************/
	if (type == TRACE_SLEEPING) {
		if (is_inbinder(nowtype))
			tmptype = (BIT(TRACE_SLEEPING) | BIT(TRACE_SLEEPING_INBINDER));
		else if (is_infutex(nowtype))
			tmptype = (BIT(TRACE_SLEEPING) | BIT(TRACE_SLEEPING_INFUTEX));
		else
			tmptype = BIT(TRACE_SLEEPING);
	}
	trace_info->task_info[idx].now_type = tmptype;

#ifdef JANK_DEBUG
	jank_dbg("cb: comm=%s, type=%d, nowtype=%d, tmptype=%d\n",
			p->comm, type, nowtype, tmptype);
	jank_systrace_print(p->pid, "dbg990_nowtype", type);
	jank_systrace_print(p->pid, "dbg991_nowtype", tmptype);
#endif
}

/*
 * TRACE_SLEEPING_INBINDER
 * TRACE_SLEEPING_INFUTEX
 */
void __maybe_unused jankinfo_mark_taskstatus(struct task_struct *p,
			unsigned long type, u32 enable)
{
	u32 idx;
	unsigned long nowtype;

	if (!trace_info || !p || !is_task_traced(p, &idx))
		return;

	nowtype = trace_info->task_info[idx].now_type;

	clear_bit(TRACE_SLEEPING_INBINDER, &nowtype);
	clear_bit(TRACE_SLEEPING_INFUTEX, &nowtype);
	if (enable)
		set_bit(type, &nowtype);
	else
		clear_bit(type, &nowtype);

	trace_info->task_info[idx].now_type = nowtype;

	jank_dbg("mk: comm=%s, type=%d, nowtype=%d, enable=%d\n",
			p->comm, type, nowtype, enable);
}

static void gerrit_check_dummy(void)
{
	/* TODO: for gerrit check */
}

#ifdef JANK_DEBUG
void dump_callstacks(struct task_struct *task, unsigned long *cs, u64 delta)
{
	int i;

	if (!task)
		return;

	pr_info("[CALL_STACK] ====== [task:%s - %llu] ======\n", task->comm, delta);
	pr_info("[CALL_STACK] ====== tracing start ======\n");
	for (i = 0; i < CALL_STACK_LEVEL; i++) {
		if (cs[i]) {
			pr_info("[CALL_STACK] level-%d: %pS\n", i, (void *)cs[i]);
		} else {
			break;
		}
	}
	pr_info("[CALL_STACK] ====== tracing end ======\n");
}
#endif

void record_callstacks(struct task_struct *p, struct callstacks *csp, u64 delta)
{
	u32 level;
	u32 id;
	unsigned long *cs;
	u64 timestamp, now;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	struct stack_trace record_trace;
#endif
	if (!p || !csp)
		return;

	timestamp = csp->last_update_time;
	now = jiffies_to_nsecs(jiffies);

	if (!timestamp_is_valid(timestamp, now)) {
		memset(csp, 0, sizeof(struct callstacks));
	}

	id = csp->id;
	cs = (unsigned long *)&csp->func[id];
	memset(cs, 0, sizeof(csp->func));
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	record_trace.nr_entries = 0;
	record_trace.max_entries = CALL_STACK_LEVEL;
	record_trace.entries = cs;
	record_trace.skip = SKIP_LEVEL;
	save_stack_trace_tsk(p, &record_trace);
	level = record_trace.nr_entries;
#else
	level = stack_trace_save_tsk(p, cs, CALL_STACK_LEVEL, SKIP_LEVEL);
#endif
	if (level < CALL_STACK_LEVEL)
		cs[level] = 0;

	csp->id =  (id + 1) & CALL_STACK_CNT_MASK;
	csp->last_update_time = now;

#ifdef JANK_DEBUG
	jank_dbg("cs1: task=%s, id=%d, level, sz=%d\n",
			p->comm, id, level, sizeof(csp->func));
	dump_callstacks(p, cs, delta);
#endif
}

void jankinfo_tasktrack_update_time(struct task_struct *task,
			unsigned long type, u64 time_in_ns)
{
	u32 idx;
	unsigned long nowtype;
	struct jank_task_info *ti = NULL;
	struct callstacks *cs;
	u32 winidx;

	if (!trace_info || !task ||
		!is_task_traced(task, &idx) || !time_in_ns)
		return;

	ti = &trace_info->task_info[idx];
	winidx = ti->winidx;

	nowtype = ti->now_type;
	ti->delta[type] += time_in_ns;
	if ((type == TRACE_SLEEPING) && is_inbinder(nowtype)) {
		ti->delta[TRACE_SLEEPING_INBINDER] += time_in_ns;
	} else if ((type == TRACE_SLEEPING) && is_infutex(nowtype)) {
		ti->delta[TRACE_SLEEPING_INFUTEX] += time_in_ns;
	} else if (type == TRACE_DISKSLEEP &&
		time_in_ns >= CALL_STACK_THRESHOLD) {
		cs = &ti->cs[winidx];
#ifdef JANK_DEBUG
		jank_dbg("cs0: task=%s, delta=%llu, winidx=%d\n",
				task->comm, time_in_ns, winidx);
		trace_printk("cs0: task=%s, delta=%llu, winidx=%d\n",
				task->comm, time_in_ns, winidx);
#endif
		record_callstacks(task, cs, time_in_ns);
	} else if (type == TRACE_DISKSLEEP_INIOWAIT) {
		/* do nothing */
		gerrit_check_dummy();
	}
	ti->last_update_time = jiffies_to_nsecs(jiffies);

#ifdef JANK_DEBUG
	jank_dbg("e3: type=%d, nowtype=%d, is_inbinder=%d, is_infutex=%d, "
		"tis=%llu(%llu), delta=%llu(%llu)\n",
		type, nowtype, is_inbinder(nowtype), is_infutex(nowtype),
		time_in_ns/1000/1000, time_in_ns,
		ti->delta[type]/1000/1000, ti->delta[type]);

	jank_systrace_print(task->pid, "dbg0_type", type);
	jank_systrace_print(task->pid, "dbg1_idx", idx);
	jank_systrace_print(task->pid, "dbg3_delta2", time_in_ns);
	jank_systrace_print(task->pid, "dbg5_nowtype", nowtype);
#endif
}

void sched_stat_runtime_handler(void *unused,
			struct task_struct *tsk, u64 runtime, u64 vruntime)
{
	jankinfo_tasktrack_update_time(tsk, TRACE_RUNNING, runtime);
}

void sched_stat_wait_handler(void *unused,
			struct task_struct *tsk, u64 delta)
{
	jankinfo_tasktrack_update_time(tsk, TRACE_RUNNABLE, delta);
}

void sched_stat_sleep_handler(void *unused,
			struct task_struct *tsk, u64 delta)
{
	jankinfo_tasktrack_update_time(tsk, TRACE_SLEEPING, delta);
}

void sched_stat_blocked_handler(void *unused,
			struct task_struct *tsk, u64 delta)
{
	jankinfo_tasktrack_update_time(tsk, TRACE_DISKSLEEP, delta);
}

void sched_stat_iowait_handler(void *unused,
			struct task_struct *tsk, u64 delta)
{
	jankinfo_tasktrack_update_time(tsk, TRACE_DISKSLEEP_INIOWAIT, delta);
}

void android_vh_futex_sleep_start_handelr(void *unused,
			struct task_struct *p)
{
	u32 idx;
	unsigned long *now_type;

	if (!trace_info || !p || !is_task_traced(p, &idx))
		return;

	now_type = &trace_info->task_info[idx].now_type;
	mark_futex(now_type);
}

void android_vh_binder_wait_for_work_hanlder(void *unused,
		bool do_proc_work, struct binder_thread *tsk,
		struct binder_proc *proc)
{
	u32 idx;
	unsigned long *now_type;
	struct task_struct *p;

	if (!trace_info || !tsk)
		return;

	p = tsk->task;
	if (!p || !is_task_traced(p, &idx))
		return;

	now_type = &trace_info->task_info[idx].now_type;
	mark_binder(now_type);
}

#ifdef JANK_USE_HOOK
int tasktrack_register_hook(void)
{
	int ret = 0;

	REGISTER_TRACE_VH(sched_stat_runtime, sched_stat_runtime_handler);
	REGISTER_TRACE_VH(sched_stat_wait, sched_stat_wait_handler);
	REGISTER_TRACE_VH(sched_stat_sleep, sched_stat_sleep_handler);
	REGISTER_TRACE_VH(sched_stat_blocked, sched_stat_blocked_handler);
	REGISTER_TRACE_VH(sched_stat_iowait, sched_stat_iowait_handler);

	REGISTER_TRACE_VH(android_vh_futex_sleep_start,
				android_vh_futex_sleep_start_handelr);
	REGISTER_TRACE_VH(android_vh_binder_wait_for_work,
				android_vh_binder_wait_for_work_hanlder);

	return ret;
}

int tasktrack_unregister_hook(void)
{
	int ret = 0;

	UNREGISTER_TRACE_VH(sched_stat_runtime, sched_stat_runtime_handler);
	UNREGISTER_TRACE_VH(sched_stat_wait, sched_stat_wait_handler);
	UNREGISTER_TRACE_VH(sched_stat_sleep, sched_stat_sleep_handler);
	UNREGISTER_TRACE_VH(sched_stat_blocked, sched_stat_blocked_handler);
	UNREGISTER_TRACE_VH(sched_stat_iowait, sched_stat_iowait_handler);

	UNREGISTER_TRACE_VH(android_vh_futex_sleep_start,
			android_vh_futex_sleep_start_handelr);
	UNREGISTER_TRACE_VH(android_vh_binder_wait_for_work,
			android_vh_binder_wait_for_work_hanlder);
	return ret;
}
#endif

static void tasktrack_timer_start(void)
{
	if (hrtimer_active(&tasktrack_timer))
		return;

	hrtimer_start(&tasktrack_timer,
			ns_to_ktime(JANK_WIN_SIZE_IN_NS), HRTIMER_MODE_REL);
}

static void tasktrack_timer_stop(void)
{
	hrtimer_cancel(&tasktrack_timer);
}

void tasktrack_init(void)
{
	u32 i;

	trace_info = kzalloc(ALIGN(sizeof(struct task_track_info),
					cache_line_size()), GFP_KERNEL);
	if (!trace_info)
		goto err_kzalloc_trace_info;


	for (i = 0; i < TASK_TRACK_NUM; i++)
		trace_info->task_info[i].pid = INVALID_PID;

	trace_info_snap = kzalloc(ALIGN(sizeof(struct task_track_info),
					cache_line_size()), GFP_KERNEL);
	if (!trace_info_snap)
		goto err_kzalloc_trace_info_snap;

#ifdef JANK_USE_HOOK
	tasktrack_register_hook();
#endif

	hrtimer_init(&tasktrack_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tasktrack_timer.function = tasktrack_timer_handler;

	jank_update_task_status = jankinfo_update_task_status_cb;

	tasktrack_timer_start();

	return;

err_kzalloc_trace_info_snap:
	kfree(trace_info);

err_kzalloc_trace_info:
	return;
}

void tasktrack_deinit(void)
{
	jank_update_task_status = NULL;
	tasktrack_timer_stop();

#ifdef JANK_USE_HOOK
	tasktrack_unregister_hook();
#endif

	kfree(trace_info);
	kfree(trace_info_snap);
}

static int proc_task_track_read(struct seq_file *m, void *v)
{
	u32 task_count;
	u32 i, j, k, winidx, idx;
	pid_t pid;
	u64 time;
	u64 last_update_time;
	u64 unprocessed_delta = 0;
	unsigned long nowtype;
	struct task_struct *task;
	u64 now, now_b;
	struct jank_task_info *ti = NULL;

#ifdef JANK_DEBUG
	u32 cs_id = 0, lv = 0;
	struct callstacks *cs;
	u32 cs_num;
	u64 cs_timestamp;
#endif

#ifdef JANK_DEBUG
	u64 dbg_total = 0;
	u64 dbg_task_info_delta;
	u64 dbg_task_info_delta_win_align;
#endif

#ifdef FULLWIN
	u32 idx_r;
	u64 time_r;
#endif

	if (!tasktrack_enable) {
		seq_puts(m, "Please enable this feature first!!!\n");
		return 0;
	}

	if (!trace_info)
		return 0;

	task_count = trace_info->task_num;
	if (!task_count) {
		seq_puts(m, "<NULL>\n");
		return 0;
	}

	jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK, "debug1_read", 0, 0);

	now = jiffies_to_nsecs(jiffies);
	now_b = now & JANK_WIN_SIZE_IN_NS_MASK;

	/* Take a snapshot */
	memcpy(trace_info_snap, trace_info, sizeof(struct task_track_info));

	for (i = 0; i < TASK_TRACK_NUM; i++) {
		ti = &trace_info_snap->task_info[i];

		pid = ti->pid;
		if (!pid || pid == INVALID_PID)
			continue;

		task = jank_find_get_task_by_vpid(pid);
		if (!task)
			continue;

		seq_printf(m, "PID: %d (%s)\n", pid, task ? task->comm : "<NULL>");
		put_task_struct(task);

		winidx = ti->winidx;
		winidx = winidx_add(winidx, 1);

		last_update_time = ti->last_update_time;

		/*************************************************
		 * TODO: BUG HERE
		 *    Because no lock is used, there may
		 *    be cases where the value is less than 0
		 *************************************************/
		if (now < last_update_time || !last_update_time)
			unprocessed_delta = 0;
		else
			unprocessed_delta = now - last_update_time;

		jank_dbg("i=%d, now=%d, last_update_time=%d, unprocessed_delta=%d\n",
			i, now>>20, last_update_time>>20, unprocessed_delta>>20);

		nowtype = ti->now_type;

		for (j = 0; j < JANK_WIN_CNT; j++) {
			idx = winidx_sub(winidx, j);
			for (k = 0; k < TRACE_CNT; k++) {
				if (j == 0) {
					if (k == TRACE_IRQ) {
						time = get_irq_time(0);
					} else {
						if (unprocessed_delta > JANK_WIN_SIZE_IN_NS) {
							if (is_current_type(k, nowtype))
								time = min_t(u64, now_b, JANK_WIN_SIZE_IN_NS);
							else
								time = 0;
						} else {
							/*********************************************
							 * TODO: BUG HERE
							 *    Because no lock is used, there may
							 *    be cases where the value is less than 0
							 *********************************************/
							if (ti->delta[k] < ti->delta_win_align[k])
								time = 0;
							else
								time = ti->delta[k] - ti->delta_win_align[k];

#ifdef JANK_DEBUG
							dbg_task_info_delta = ti->delta[k];
							dbg_task_info_delta_win_align =
								ti->delta_win_align[k];
#endif

							if (is_current_type(k, nowtype))
								time += unprocessed_delta;

#ifdef FULLWIN
							idx_r = winidx_sub(idx, 1);
							time_r = ti->time[idx_r].val[k];

							time = ((time + time_r) <<
								JANK_WIN_SIZE_SHIFT_IN_NS) /
								(JANK_WIN_SIZE_IN_NS + now_b);
							time = min_t(u64, time, JANK_WIN_SIZE_IN_NS);
#endif
						}
					}
				} else {
					time = ti->time[idx].val[k];
				}
#ifdef JANK_DEBUG
				if (k == TRACE_RUNNING ||
					k == TRACE_RUNNABLE ||
					k == TRACE_SLEEPING ||
					k == TRACE_DISKSLEEP)
					dbg_total += time;
#endif

				seq_printf(m, "%llu ", time/1000/1000);

				jank_dbg("j1=%d, idx=%d, winidx=%d, type=%d, nowtype=%d, "
					"unprocessed_delta=%llu, time=%llu, "
					"dbg_task_info_delta=%llu, "
					"dbg_task_info_delta_win_align=%llu\n",
					j, idx, winidx, k, nowtype,
					unprocessed_delta/1000/1000, time/1000/1000,
					dbg_task_info_delta, dbg_task_info_delta_win_align);
			}

#ifdef JANK_DEBUG
			/* call stack information  */
			cs = &ti->cs[idx];
			cs_num = cs->id;
			cs_timestamp = cs->last_update_time;

			if (timestamp_is_valid(cs_timestamp, now)) {
				for (cs_id = 0; cs_id < cs_num; cs_id++) {
					seq_printf(m, "[");

					for (lv = 0; lv < CALL_STACK_LEVEL; lv++) {
						if (lv && cs->func[cs_id][lv])
							seq_printf(m, "<-");

						if (cs->func[cs_id][lv]) {
							seq_printf(m, "%pS", (void *)cs->func[cs_id][lv]);
						} else {
							break;
						}
					}
					seq_printf(m, "] ");
				}
			}
#endif

#ifdef JANK_DEBUG
			seq_printf(m, "[%llu, %d]\n",
				dbg_total/1000/1000,
				(dbg_total/1000/1000)-134);

			jank_dbg("j2=%d, idx=%d, winidx=%d, "
					"unprocessed_delta=%llu, dbg_total=%llu\n",
					j, idx, winidx,
					unprocessed_delta/1000/1000, dbg_total/1000/1000);

			dbg_total = 0;
#else
			seq_puts(m, "\n");
#endif
		}
#ifdef JANK_DEBUG
		/* seq_puts(m, "\n"); */
#endif
	}

	return 0;
}

static ssize_t proc_task_track_write(struct file *file,
					const char __user *buf, size_t count, loff_t *offset)
{
	char buffer[512];
	char *token, *p = buffer;
	int i, j, pid_count = 0;
	int pid[TASK_TRACK_NUM];
	int tmp_pid;
	bool match = false;

	if (!trace_info)
		return count;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	jank_dbg("p1: buffer=%s\n", buffer);

	while ((token = strsep(&p, " ")) != NULL) {
		if (pid_count >= TASK_TRACK_NUM)
			break;

		if (kstrtoint(strstrip(token), 10, &pid[pid_count]))
			return -EINVAL;

		pid_count++;
	}

	for (i = 0; i < pid_count; i++) {
		jank_dbg("p2: task_num=%d, pid_count=%d, pid[%d]=%d\n",
					trace_info->task_num, pid_count, i, pid[i]);
		jank_systrace_print_idx(JANK_SYSTRACE_TASKTRACK,
					"debug2_write", i, pid[i]);

		if (pid[i] < 0) {
			if (trace_info->task_num <= 0)
				continue;

			for (j = 0; j < TASK_TRACK_NUM; j++) {
				tmp_pid = trace_info->task_info[j].pid;
				if (tmp_pid + pid[i] == 0) {
					memset(&trace_info->task_info[j],
								0, sizeof(struct jank_task_info));
					trace_info->task_info[j].pid = INVALID_PID;

					trace_info->task_num--;
					break;
				}
			}

		} else {
			if (trace_info->task_num >= TASK_TRACK_NUM)
				continue;

			for (j = 0; j < TASK_TRACK_NUM; j++) {
				tmp_pid = trace_info->task_info[j].pid;

				if (tmp_pid == pid[i]) {
					match = true;
					break;
				}
				match = false;
			}

			if (!match) {
				for (j = 0; j < TASK_TRACK_NUM; j++) {
					tmp_pid = trace_info->task_info[j].pid;

					if (tmp_pid == INVALID_PID) {
						trace_info->task_info[j].pid = pid[i];
						trace_info->task_num++;

						break;
					}
				}
			}
		}
	}
	return count;
}

static int proc_task_track_open(struct inode *inode,
			struct file *file)
{
	return single_open(file, proc_task_track_read, inode);
}

static int proc_task_track_enable_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", tasktrack_enable);
	return 0;
}

static ssize_t proc_task_track_enable_write(struct file *file,
					const char __user *buf, size_t count, loff_t *offset)
{
	char buffer[512];
	char *token, *p = buffer;
	int err = 0;
	int enable;
	u32 i;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		err = -EFAULT;
		goto out;
	}

	while ((token = strsep(&p, " ")) != NULL) {
		if (kstrtoint(strstrip(token), 10, &enable))
			return -EINVAL;
	}

	if (tasktrack_enable == (!!enable))
		goto out;

	tasktrack_enable = !!enable;

	if (tasktrack_enable) {
		tasktrack_timer_start();
	} else {
		tasktrack_timer_stop();
		memset(trace_info, 0,
				ALIGN(sizeof(struct task_track_info), cache_line_size()));

		for (i = 0; i < TASK_TRACK_NUM; i++)
			trace_info->task_info[i].pid = INVALID_PID;

		memset(trace_info_snap, 0,
				ALIGN(sizeof(struct task_track_info), cache_line_size()));
	}
out:
	return err < 0 ? err : count;
}

static int proc_task_track_enable_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_task_track_enable_read, inode);
}

static int dump_callstack(struct seq_file *m, void *v)
{
	u32 task_count;
	u32 i, j, winidx, idx;
	pid_t pid;
	struct task_struct *task;
	u64 now;
	struct jank_task_info *ti = NULL;
	u32 cs_id = 0, lv = 0;
	struct callstacks *cs;
	u32 cs_num;
	u64 cs_timestamp;
	struct timespec64 ts;

	if (!tasktrack_enable) {
		seq_puts(m, "Please enable this feature first!!!\n");
		return 0;
	}

	if (!trace_info)
		return 0;

	task_count = trace_info->task_num;
	if (!task_count) {
		seq_puts(m, "<NULL>\n");
		return 0;
	}

	now = jiffies_to_nsecs(jiffies);
	ktime_get_real_ts64(&ts);

	/* Take a snapshot */
	memcpy(trace_info_snap, trace_info, sizeof(struct task_track_info));

	for (i = 0; i < TASK_TRACK_NUM; i++) {
		ti = &trace_info_snap->task_info[i];

		pid = ti->pid;
		if (!pid || pid == INVALID_PID)
			continue;

		task = jank_find_get_task_by_vpid(pid);
		if (!task)
			continue;

		winidx = ti->winidx;
		winidx = winidx_add(winidx, 1);

		for (j = 0; j < JANK_WIN_CNT; j++) {
			idx = winidx_sub(winidx, j);

			/* call stack information  */
			cs = &ti->cs[idx];
			cs_num = cs->id;
			cs_timestamp = cs->last_update_time;

			if (!timestamp_is_valid(cs_timestamp, now))
				continue;

			seq_printf(m, "[%llu.%lu] [%d] ", (u64)ts.tv_sec, ts.tv_nsec, pid);

			for (cs_id = 0; cs_id < cs_num; cs_id++) {
				seq_printf(m, "[");

				for (lv = 0; lv < CALL_STACK_LEVEL; lv++) {
					if (lv && cs->func[cs_id][lv])
						seq_printf(m, "<-");

					if (cs->func[cs_id][lv]) {
						seq_printf(m, "%pS", (void *)cs->func[cs_id][lv]);
					} else {
						break;
					}
				}
				seq_printf(m, "] ");
			}

			seq_puts(m, "\n");
		}
	}
	return 0;
}

static int proc_callstack_show(struct seq_file *m, void *v)
{
	return dump_callstack(m, v);
}

static int proc_callstack_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, proc_callstack_show, inode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_task_track_operations = {
	.open = proc_task_track_open,
	.read = seq_read,
	.write = proc_task_track_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_task_track_enable_operations = {
	.open = proc_task_track_enable_open,
	.read = seq_read,
	.write = proc_task_track_enable_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations proc_callstack_operations = {
	.open = proc_callstack_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
static const struct proc_ops proc_task_track_operations = {
	.proc_open = proc_task_track_open,
	.proc_read = seq_read,
	.proc_write = proc_task_track_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops proc_task_track_enable_operations = {
	.proc_open = proc_task_track_enable_open,
	.proc_read = seq_read,
	.proc_write = proc_task_track_enable_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops proc_callstack_operations = {
	.proc_open = proc_callstack_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

struct proc_dir_entry *jank_tasktrack_proc_init(
			struct proc_dir_entry *pde)
{
	struct proc_dir_entry *entry = NULL;

	entry = proc_create("task_track", S_IRUGO | S_IWUGO,
				pde, &proc_task_track_operations);
	if (!entry) {
		pr_err("create task_track fail\n");
		goto err_task_track;
	}

	entry = proc_create("task_track_enable", S_IRUGO | S_IWUGO,
				pde, &proc_task_track_enable_operations);
	if (!entry) {
		pr_err("create task_track_neable fail\n");
		goto err_task_track_enable;
	}

	entry = proc_create("callstack", S_IRUGO,
				pde, &proc_callstack_operations);
	if (!entry) {
		pr_err("create callstack fail\n");
		goto err_callstack_sig;
	}
	return entry;

err_callstack_sig:
	remove_proc_entry("task_track_enable", pde);

err_task_track_enable:
	remove_proc_entry("task_track", pde);

err_task_track:
	return NULL;
}

void jank_tasktrack_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("callstack", pde);
	remove_proc_entry("task_track_enable", pde);
	remove_proc_entry("task_track", pde);
}

