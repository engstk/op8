/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_RWSEM_H_
#define _OPLUS_SCHED_RWSEM_H_

#ifndef CONFIG_MTK_TASK_TURBO
#ifdef CONFIG_RWSEM_PRIO_AWARE
extern bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head, struct rw_semaphore *sem);
#else /* CONFIG_RWSEM_PRIO_AWARE */
extern void rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head);
#endif /* CONFIG_RWSEM_PRIO_AWARE */
#endif /* CONFIG_MTK_TASK_TURBO */

extern void rwsem_set_inherit_ux(struct task_struct *tsk, struct task_struct *waiter_task, struct task_struct *owner, struct rw_semaphore *sem);
extern void rwsem_unset_inherit_ux(struct rw_semaphore *sem, struct task_struct *tsk);
#endif
