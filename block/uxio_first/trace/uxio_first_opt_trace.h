/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../block/uxio_first/trace
#define TRACE_SYSTEM uxio_first_opt_trace

#if !defined(_OPLUS_UXIO_FIRST_OPT_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _OPLUS_UXIO_FIRST_OPT_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(block_uxio_first_peek_req,
	TP_PROTO(struct task_struct *task, long req_addr, \
		 char * group, unsigned int ux,unsigned int fg,unsigned int bg),

	TP_ARGS(task, req_addr, group, ux,fg,bg),

	TP_STRUCT__entry(
			__array(char, comm, TASK_COMM_LEN)
			__field(pid_t, pid)
			__field(long, req_addr)
			__array(char, group, 3)
			__field(unsigned int, ux)
			__field(unsigned int, fg)
			__field(unsigned int, bg)
	),

	TP_fast_assign(
			memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
			__entry->pid = task->pid;
			__entry->req_addr = req_addr;
			memcpy(__entry->group, group, 3);
			__entry->ux = ux;
			__entry->fg = fg;
			__entry->bg = bg;
	),

	TP_printk("%s (%d), req_addr %x task_group:%s, inflight(%u,%u,%u)",
		__entry->comm, __entry->pid, __entry->req_addr,
		__entry->group, __entry->ux,__entry->fg,__entry->bg)
);
#endif /*_OPLUS_FOREGROUND_IO_OPT_TRACE_H*/
#include <trace/define_trace.h>
