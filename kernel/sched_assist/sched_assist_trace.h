/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#if !defined(_OPLUS_SCHED_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _OPLUS_SCHED_TRACE_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM oplus_sched
#define TRACE_INCLUDE_FILE sched_assist_trace

TRACE_EVENT(oplus_tp_sched_change_ux,

	TP_PROTO(int chg_ux, int target_cpu),

	TP_ARGS(chg_ux, target_cpu),

	TP_STRUCT__entry(
		__field(int, chg_ux)
		__field(int, target_cpu)
	),

	TP_fast_assign(
		__entry->chg_ux = chg_ux;
		__entry->target_cpu = target_cpu;
	),

	TP_printk("chg_ux=%d target_cpu=%d", __entry->chg_ux, __entry->target_cpu)
);

TRACE_EVENT(oplus_tp_sched_switch_ux,

	TP_PROTO(int chg_ux, int target_cpu),

	TP_ARGS(chg_ux, target_cpu),

	TP_STRUCT__entry(
		__field(int, chg_ux)
		__field(int, target_cpu)
	),

	TP_fast_assign(
		__entry->chg_ux = chg_ux;
		__entry->target_cpu = target_cpu;
	),

	TP_printk("chg_ux=%d target_cpu=%d", __entry->chg_ux, __entry->target_cpu)
);

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
extern void printf_cpu_spread_nr_info(unsigned int cpu, char *nr_info, int info_size);
TRACE_EVENT(sched_assist_spread_tasks,

	TP_PROTO(struct task_struct *p, int sched_type, int lowest_nr_cpu),

	TP_ARGS(p, sched_type, lowest_nr_cpu),

	TP_STRUCT__entry(
		__field(int,	pid)
		__array(char,	comm, TASK_COMM_LEN)
		__field(int,	sched_type)
		__field(int,	lowest_nr_cpu)
		__array(char,	nr_info_0,	32)
		__array(char,	nr_info_1,	32)
		__array(char,	nr_info_2,	32)
		__array(char,	nr_info_3,	32)
		__array(char,	nr_info_4,	32)
		__array(char,	nr_info_5,	32)
		__array(char,	nr_info_6,	32)
		__array(char,	nr_info_7,	32)),

	TP_fast_assign(
		__entry->pid			= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->sched_type		= sched_type;
		__entry->lowest_nr_cpu	= lowest_nr_cpu;
		printf_cpu_spread_nr_info(0, __entry->nr_info_0, sizeof(__entry->nr_info_0));
		printf_cpu_spread_nr_info(1, __entry->nr_info_1, sizeof(__entry->nr_info_1));
		printf_cpu_spread_nr_info(2, __entry->nr_info_2, sizeof(__entry->nr_info_2));
		printf_cpu_spread_nr_info(3, __entry->nr_info_3, sizeof(__entry->nr_info_3));
		printf_cpu_spread_nr_info(4, __entry->nr_info_4, sizeof(__entry->nr_info_4));
		printf_cpu_spread_nr_info(5, __entry->nr_info_5, sizeof(__entry->nr_info_5));
		printf_cpu_spread_nr_info(6, __entry->nr_info_6, sizeof(__entry->nr_info_6));
		printf_cpu_spread_nr_info(7, __entry->nr_info_7, sizeof(__entry->nr_info_7));),

	TP_printk("comm=%-12s pid=%d sched_type=%d lowest_nr_cpu=%d nr_info=%s%s%s%s%s%s%s%s",
		__entry->comm, __entry->pid, __entry->sched_type, __entry->lowest_nr_cpu,
		__entry->nr_info_0, __entry->nr_info_1, __entry->nr_info_2, __entry->nr_info_3,
		__entry->nr_info_4, __entry->nr_info_5, __entry->nr_info_6, __entry->nr_info_7)
);
#endif /* CONFIG_OPLUS_FEATURE_SCHED_SPREAD */
#endif /* _OPLUS_SCHED_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
