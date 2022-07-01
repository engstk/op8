/***********************************************************
** Copyright (C), 2008-2019, Oplus., All rights reserved.
** File: oplus_process.h
** Description: Add for dump android critical process log
**
** Version: 1.0
****************************************************************/

#ifndef _OPLUS_PROCESS_H
#define _OPLUS_PROCESS_H

static inline bool is_zygote_process(struct task_struct *t)
{
	const struct cred *tcred = __task_cred(t);

	struct task_struct * first_child = NULL;
	if (t->children.next && t->children.next != (struct list_head*)&t->children.next) {
		first_child = container_of(t->children.next, struct task_struct, sibling);
	}
	if (!strcmp(t->comm, "main") && (tcred->uid.val == 0) && (t->parent != 0 && !strcmp(t->parent->comm, "init"))) {
		return true;
	} else {
		return false;
	}
	return false;
}

static inline bool is_systemserver_process(struct task_struct *t) {
	if (!strcmp(t->comm, "system_server")) {
		return true;
	} else {
		return false;
	}
}

static inline bool is_key_process(struct task_struct *t) {
	struct pid *pgrp;
	struct task_struct *taskp;

	if (t->pid == t->tgid) {
		if (is_systemserver_process(t) || is_zygote_process(t)) {
			return true;
		}
	} else {
		pgrp = task_pgrp(t);
		if (pgrp != NULL) {
			taskp = pid_task(pgrp, PIDTYPE_PID);
			if (taskp != NULL && (is_systemserver_process(taskp) || is_zygote_process(taskp))) {
				return true;
			}
		}
	}

	return false;
}

static inline bool oplus_is_android_core_group(struct pid *pgrp)
{
	struct task_struct *p;

	do_each_pid_task(pgrp, PIDTYPE_PGID, p) {
		if ((!strcmp(p->comm, "zygote")) || (!strcmp(p->comm, "main"))) {
			printk("oplus_is_android_core_group: find zygote will be hungup, ignore it \n");
			return true;
		}
	} while_each_pid_task(pgrp, PIDTYPE_PGID, p);

	return false;
}

static inline bool is_critial_process(struct task_struct *t) {
	if (t->group_leader && (!strcmp(t->group_leader->comm, "system_server")
		|| is_zygote_process(t) || !strcmp(t->group_leader->comm, "surfaceflinger") || !strcmp(t->group_leader->comm, "servicemanager"))) {
		if (t->pid == t->tgid) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}
#endif /*_OPLUS_PROCESS_H */
