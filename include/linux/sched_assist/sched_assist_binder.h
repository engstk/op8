/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_BINDER_H_
#define _OPLUS_SCHED_BINDER_H_
#include "sched_assist_common.h"

extern const struct sched_class rt_sched_class;
static inline void binder_set_inherit_ux(struct task_struct *thread_task, struct task_struct *from_task)
{
	if (from_task && test_set_inherit_ux(from_task)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, from_task->ux_state);
		else
			reset_inherit_ux(thread_task, from_task, INHERIT_UX_BINDER);
	} else if (from_task && test_task_identify_ux(from_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	} else if (from_task && (from_task->sched_class == &rt_sched_class)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	}
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	else if (from_task && (is_audio_task(from_task))) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	}
#endif
}

static inline void binder_unset_inherit_ux(struct task_struct *thread_task)
{
	if (test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
		unset_inherit_ux(thread_task, INHERIT_UX_BINDER);
	}
}
#endif
