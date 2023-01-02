/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_MUTEX_H_
#define _OPLUS_SCHED_MUTEX_H_

extern void mutex_list_add(struct task_struct *task, struct list_head *entry, struct list_head *head, struct mutex *lock);
extern void mutex_set_inherit_ux(struct mutex *lock, struct task_struct *task);
extern void mutex_unset_inherit_ux(struct mutex *lock, struct task_struct *task);

#endif
