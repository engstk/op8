// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <uapi/linux/sched/types.h>

#include "jank_hotthread.h"
#include "jank_topology.h"
#include "jank_tasktrack.h"

static struct task_track_cpu task_track[CPU_NUMS];

#define get_oplus_task_struct(t)		(t)

static struct task_record *get_task_record(struct task_struct *t,
			u32 cpu)
{
	struct task_record *rc = NULL;

	rc = (struct task_record *) (&(get_oplus_task_struct(t)->record));
	return (struct task_record *) (&rc[cpu]);
}

/* updated in each tick */
void jank_hotthread_update_tick(struct task_struct *p, u64 now)
{
	struct task_record *record_p;
	struct task_record *record_b;
	u64 timestamp;
	u32 now_idx;
	u32 cpu;

	if (!p)
		return;

	cpu = get_start_cpu(p->cpu);
	record_p = get_task_record(p, cpu);

	if (record_p->winidx == get_record_winidx(now))
		record_p->count++;
	else
		record_p->count = 1;

	record_p->winidx = get_record_winidx(now);

	now_idx = time2winidx(now);
	record_b = &task_track[cpu].track[now_idx].record;
	timestamp = task_track[cpu].track[now_idx].timestamp;

	if (!is_same_idx(timestamp, now) ||
		(record_p->count > record_b->count)) {
		task_track[cpu].track[now_idx].pid = p->pid;
		task_track[cpu].track[now_idx].tgid = p->tgid;

		memcpy(task_track[cpu].track[now_idx].comm,
			p->comm, TASK_COMM_LEN);
		memcpy(record_b, record_p, sizeof(struct task_record));

		task_track[cpu].track[now_idx].timestamp = now;
	}
}

void jank_hotthread_show(struct seq_file *m, u32 win_idx, u64 now)
{
	u32 i, idx, now_index;
	u64 timestamp;
	struct task_track *track_p;
	struct task_struct *leader;
	char *comm;
	bool nospace = false;

	now_index =  time2winidx(now);

	for (i = 0; i < CPU_NUMS; i++) {
		nospace = (i == CPU_NUMS-1) ? true : false;

		if (!is_cluster_cpu(i))
			continue;

		idx = winidx_sub(now_index, win_idx);
		track_p = &task_track[i].track[idx];
		timestamp = track_p->timestamp;

		leader = jank_find_get_task_by_vpid(track_p->tgid);
		comm = leader ? leader->comm : track_p->comm;

		/*
		 * The following situations indicate that this thread is not hot
		 *  a) task is null, which means no suitable task, or task is dead
		 *  b) the time stamp is overdue
		 *  c) count did not reach the threshold
		 */
		if (!timestamp_is_valid(timestamp, now))
			seq_printf(m, "-%s", nospace ? "" : " ");
		else
			seq_printf(m, "%s$%d%s", comm,
						track_p->record.count,
						nospace ? "" : " ");

		if (leader)
			put_task_struct(leader);
	}
}

