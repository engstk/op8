/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _LINUX_MEMLEAK_STACKDEPOT_H
#define _LINUX_MEMLEAK_STACKDEPOT_H

typedef u32 ml_depot_stack_handle_t;

struct stack_trace;

ml_depot_stack_handle_t ml_depot_save_stack(struct stack_trace *trace, gfp_t flags);
void ml_depot_fetch_stack(ml_depot_stack_handle_t handle, struct stack_trace *trace);
int ml_depot_init(void);
int ml_get_depot_index(void);
#endif /* _LINUX_MEMLEAK_STACKDEPOT_H */
