// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/init.h>
#include <linux/list_sort.h>
#include <linux/sched.h>
#include "uxio_first_opt.h"

#ifdef CONFIG_FG_TASK_UID
#include <linux/healthinfo/fg.h>
#endif /*CONFIG_FG_TASK_UID*/

#define SYSTEM_APP_UID 1000

static bool is_system_uid(struct task_struct *t)
{
	int cur_uid;
	cur_uid = task_uid(t).val;
	if (cur_uid ==  SYSTEM_APP_UID)
		return true;

	return false;
}

static bool is_zygote_process(struct task_struct *t)
{
	const struct cred *tcred = __task_cred(t);

	struct task_struct * first_child = NULL;
	if(t->children.next && t->children.next != (struct list_head*)&t->children.next)
		first_child = container_of(t->children.next, struct task_struct, sibling);
	if(!strcmp(t->comm, "main") && (tcred->uid.val == 0) && (t->parent != 0 && !strcmp(t->parent->comm,"init"))  )
		return true;
	else
		return false;
	return false;
}

static bool is_system_process(struct task_struct *t)
{
	if (is_system_uid(t)) {
		if (t->group_leader  && (!strncmp(t->group_leader->comm,"system_server", 13) ||
			!strncmp(t->group_leader->comm, "surfaceflinger", 14) ||
			!strncmp(t->group_leader->comm, "servicemanager", 14) ||
			!strncmp(t->group_leader->comm, "ndroid.systemui", 15)))
				return true;
	}
	return false;
}

bool is_critial_process(struct task_struct *t)
{
	if( is_zygote_process(t) || is_system_process(t))
		return true;

	return false;
}

bool is_filter_process(struct task_struct *t)
{
	if(!strncmp(t->comm,"logcat", TASK_COMM_LEN) )
		 return true;

	return false;
}
static inline bool is_fg_task_without_sysuid(struct task_struct *t)
{
		if(!is_system_uid(t)
#ifdef CONFIG_FG_TASK_UID
		&&is_fg(task_uid(t).val)
#endif /*CONFIG_FG_TASK_UID*/
		)
		return true;

	return false;

}

bool high_prio_for_task(struct task_struct *t)
{
	if (likely(!sysctl_uxio_io_opt))
		return false;

	if ((is_fg_task_without_sysuid(t) && !is_filter_process(t))
		|| is_critial_process(t))
		return true;

	return false;
}
