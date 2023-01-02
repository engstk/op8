/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SA_CPUJANKINFO_H_
#define _OPLUS_SA_CPUJANKINFO_H_
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
struct rq;
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CPU_JANKINFO)
typedef void (*jank_callback_t)(struct task_struct *p,
			unsigned long type);
extern jank_callback_t jank_update_task_status;

void jankinfo_android_rvh_enqueue_task_handler(void *unused,
			struct rq *rq, struct task_struct *p, int flags);
void jankinfo_android_rvh_schedule_handler(void *unused,
			struct task_struct *prev, struct task_struct *next, struct rq *rq);

#endif
#endif  /* endif */

