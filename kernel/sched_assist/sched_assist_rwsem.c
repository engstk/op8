// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/sched.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include "sched_assist_common.h"

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

#define RWSEM_READER_OWNED	((struct task_struct *)1UL)

static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && owner != RWSEM_READER_OWNED;
}

static void rwsem_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;
	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
#ifdef CONFIG_RWSEM_PRIO_AWARE
		if (!test_task_ux(waiter->task) && waiter->task->prio > MAX_RT_PRIO) {
#else
		if (!test_task_ux(waiter->task)) {
#endif
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	if (pos == head) {
		list_add_tail(entry, head);
	}
}

#ifndef CONFIG_MTK_TASK_TURBO
#ifdef CONFIG_RWSEM_PRIO_AWARE
bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head, struct rw_semaphore *sem)
{
	bool is_ux = test_task_ux(tsk);
	if (!entry || !head) {
		return false;
	}
	if (is_ux) {
		rwsem_list_add_ux(entry, head);
		sem->m_count++;
		return true;
	} else {
		return false;
	}
	return false;
}
#else /* CONFIG_RWSEM_PRIO_AWARE */
void rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head)
{
	bool is_ux = test_task_ux(tsk);
	if (!entry || !head) {
		return;
	}
	if (is_ux) {
		rwsem_list_add_ux(entry, head);
	} else {
		list_add_tail(entry, head);
	}
}
#endif /* CONFIG_RWSEM_PRIO_AWARE */
#endif /* CONFIG_MTK_TASK_TURBO */

void rwsem_set_inherit_ux(struct task_struct *tsk, struct task_struct *waiter_task, struct task_struct *owner, struct rw_semaphore *sem)
{
	bool is_ux = test_set_inherit_ux(tsk);
	if (waiter_task && is_ux) {
		if (rwsem_owner_is_writer(owner) && sem && !sem->ux_dep_task) {
			int type = get_ux_state_type(owner);
			if ((UX_STATE_NONE == type) || (UX_STATE_INHERIT == type)) {
				set_inherit_ux(owner, INHERIT_UX_RWSEM, tsk->ux_depth, tsk->ux_state);
				sem->ux_dep_task = owner;
			}
		}
	}
}

void rwsem_unset_inherit_ux(struct rw_semaphore *sem, struct task_struct *tsk)
{
	if (tsk && sem && sem->ux_dep_task == tsk) {
		unset_inherit_ux(tsk, INHERIT_UX_RWSEM);
		sem->ux_dep_task = NULL;
	}
}
