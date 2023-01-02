/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_FUTEX_H_
#define _OPLUS_SCHED_FUTEX_H_
extern bool test_task_ux(struct task_struct *task);
extern struct task_struct* get_futex_owner(u32 __user *uaddr2);
extern struct task_struct* get_futex_owner_by_pid(u32 owner_tid);
extern struct task_struct* get_futex_owner_by_pid_v2(u32 owner_tid);
extern void futex_set_inherit_ux(struct task_struct *owner, struct task_struct *task);
extern void futex_unset_inherit_ux(struct task_struct *task);
extern void futex_set_inherit_ux_refs(struct task_struct *owner, struct task_struct *task);
extern int futex_set_inherit_ux_refs_v2(struct task_struct *owner, struct task_struct *task);
extern void futex_unset_inherit_ux_refs(struct task_struct *task, int value);
#endif
