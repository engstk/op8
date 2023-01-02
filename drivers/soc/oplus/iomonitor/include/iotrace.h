/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _IO_TRACE_H
#define _IO_TRACE_H

#include<linux/tracepoint.h>

DECLARE_TRACE(syscall_read_timeout,
	      TP_PROTO(struct file *file, u64 delay), TP_ARGS(file, delay)
);

DECLARE_TRACE(syscall_write_timeout,
	      TP_PROTO(struct file *file, u64 delay), TP_ARGS(file, delay)
);

DECLARE_TRACE(syscall_sync_timeout,
	      TP_PROTO(struct file *file, u64 delay), TP_ARGS(file, delay)
);

#endif
