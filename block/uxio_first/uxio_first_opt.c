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

#define CREATE_TRACE_POINTS
#include <trace/uxio_first_opt_trace.h>

static inline bool should_get_req_from_ux_or_fg(struct request_queue *q)
{
	int uxfg_max_depth = q->queue_tags->max_depth - BLK_MIN_BG_DEPTH;
	if (((q->in_flight[BLK_RW_UX] + q->in_flight[BLK_RW_FG]) < uxfg_max_depth) ||
		list_empty(&q->bg_head))
		return true;
	return false;
}


static inline bool should_get_req_from_ux(struct request_queue *q)
{
	int ux_max_depth = q->queue_tags->max_depth - BLK_MIN_BG_DEPTH - BLK_MIN_FG_DEPTH;
	if ((q->in_flight[BLK_RW_UX] < ux_max_depth) || list_empty(&q->fg_head))
		return true;
	return false;
}

static inline bool should_get_req_from_fg(struct request_queue *q)
{
	int fg_max_depth = q->queue_tags->max_depth - BLK_MIN_BG_DEPTH - BLK_MIN_UX_DEPTH;
	if ((q->in_flight[BLK_RW_FG]< fg_max_depth) || list_empty(&q->ux_head))
		return true;
	return false;
}

static inline bool should_get_req_from_bg(struct request_queue *q)
{
	if (q->in_flight[BLK_RW_BG] < q->queue_tags->bg_max_depth)
		return true;
	return false;
}
static struct request * get_request_from_queue(struct request_queue *q, struct list_head *list_head)
{
	struct request *rq = NULL;

	if (!list_empty(list_head)) {
		list_for_each_entry(rq, list_head, ux_fg_bg_list) {
			if (blk_pm_allow_request(rq))
			   return rq;

		if (rq->rq_flags & RQF_SOFTBARRIER)
		   break;
		}
	}
	return NULL;
}
static bool blk_queue_depth_check(struct request_queue *q)
{
	struct blk_queue_tag *bqt = NULL;

	if (blk_queue_tagged(q))
		bqt = q->queue_tags;

	if (bqt && bqt->bg_max_depth > 0 &&
		bqt->max_depth >= BLK_MIN_DEPTH_ON)
			return true;

	return false;

}
static struct request *get_req_by_inflight(struct request_queue *q)
{
	struct request *rq = NULL;

	if (should_get_req_from_ux_or_fg(q)) {
		if (should_get_req_from_ux(q)) {
			rq = get_request_from_queue(q, &q->ux_head);
			if (rq)
				trace_block_uxio_first_peek_req(current,(long)rq,"UX\0",
					q->in_flight[BLK_RW_UX],q->in_flight[BLK_RW_FG],
					q->in_flight[BLK_RW_BG]);
		}
		if (!rq && should_get_req_from_fg(q)) {
			rq = get_request_from_queue(q, &q->fg_head);
			if (rq)
				trace_block_uxio_first_peek_req(current,(long)rq,"FG\0",
					q->in_flight[BLK_RW_UX],q->in_flight[BLK_RW_FG],
					q->in_flight[BLK_RW_BG]);
		}
	}
	if (!rq && should_get_req_from_bg(q)) {
		rq = get_request_from_queue(q, &q->bg_head);
		if (rq)
			trace_block_uxio_first_peek_req(current,(long)rq,"BG\0",
				q->in_flight[BLK_RW_UX],q->in_flight[BLK_RW_FG],
				q->in_flight[BLK_RW_BG]);
	}

	return rq;
}

void queue_throtl_add_request(struct request_queue *q,
					    struct request *rq, bool front)
{
	struct list_head *head;

	if (unlikely(!sysctl_uxio_io_opt))
		return;

	if (rq->cmd_flags & REQ_UX)
		head = &q->ux_head;
	else if (rq->cmd_flags & REQ_FG)
		head = &q->fg_head;
	else
		head = &q->bg_head;

	if (front)
		list_add(&rq->ux_fg_bg_list, head);
	else
		list_add_tail(&rq->ux_fg_bg_list, head);
}

void ohm_ioqueue_add_inflight(struct request_queue *q,
                                            struct request *rq)
{
       if (rq->cmd_flags & REQ_UX)
               q->in_flight[BLK_RW_UX]++;
       else if (rq->cmd_flags & REQ_FG)
               q->in_flight[BLK_RW_FG]++;
       else
               q->in_flight[BLK_RW_BG]++;
}

void ohm_ioqueue_dec_inflight(struct request_queue *q,
                                            struct request *rq)
{
       if (rq->cmd_flags & REQ_UX)
               q->in_flight[BLK_RW_UX]--;
       else if (rq->cmd_flags & REQ_FG)
               q->in_flight[BLK_RW_FG]--;
       else
               q->in_flight[BLK_RW_BG]--;
}

struct request * smart_peek_request(struct request_queue *q)
{
	struct request *rq = NULL;

	if (blk_queue_depth_check(q))
		 rq = get_req_by_inflight(q);

	return rq;

}
