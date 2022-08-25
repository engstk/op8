/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_UXIO_FIRST_H
#define _OPLUS_UXIO_FIRST_H

#define BLK_MIN_BG_DEPTH	2
#define BLK_MIN_UX_DEPTH	5
#define BLK_MIN_FG_DEPTH	5
#define BLK_MIN_DEPTH_ON	16
#define BLK_MAX_BG_DEPTH	24

extern bool sysctl_uxio_io_opt;
extern bool blk_pm_allow_request(struct request *rq);
extern struct request * smart_peek_request(struct request_queue *q);
extern void queue_throtl_add_request(struct request_queue *q,
						    struct request *rq, bool front);
extern bool high_prio_for_task(struct task_struct *t);
extern bool test_ux_task_cpu(int cpu);
extern bool test_task_ux(struct task_struct *task);
#endif /*_OPLUS_UXIO_FIRST_H*/
