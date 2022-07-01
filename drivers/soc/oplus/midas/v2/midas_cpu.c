/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/workqueue.h>
#include "midas_dev.h"
#include "midas_cpu.h"

#define WQ_NAME_LEN         24

enum {
        TYPE_NONE = 0,
        TYPE_PROCESS,
        TYPE_APP,
        TYPE_TOTAL,
};

int midas_ioctl_get_och(void *kdata, void *priv_info)
{
	unsigned long flags;
	struct midas_priv_info *info = priv_info;
	struct cpu_och_data *buf = NULL;

	if (IS_ERR_OR_NULL(info) ||
		IS_ERR_OR_NULL(info->mmap_addr))
		return -EINVAL;

	buf = info->mmap_addr;
	spin_lock_irqsave(&info->lock, flags);
	memset(buf, 0, sizeof(struct cpu_och_data));
	get_cpufreq_health_info(&buf->cnt, buf->info);
	spin_unlock_irqrestore(&info->lock, flags);

	return 0;
}

static void update_cpu_entry_locked(struct task_struct *p, u64 cputime,
					unsigned int state, struct cpu_mmap_data *buf)
{
	unsigned int cpu = p->cpu;

	if (cpu >= CPU_MAX)
		return;

	buf->cpu_entrys[cpu].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
}

static void query_pid_name(struct task_struct *p, char *name)
{
	char buf[WQ_NAME_LEN] = {0};

	if (!(p->flags & PF_WQ_WORKER)) {
		strncpy(name, p->comm, TASK_COMM_LEN);
	} else {
		get_worker_info(p, buf);
		if (buf[0]) {
			strncpy(name, buf, TASK_COMM_LEN - 1);
			name[TASK_COMM_LEN - 1] = '\0';
		} else {
			strncpy(name, p->comm, TASK_COMM_LEN);
		}
	}
}

static void update_entry_locked(uid_t uid, struct task_struct *p, u64 cputime,
				unsigned int state, struct cpu_mmap_data *buf, unsigned int type)
{
	unsigned int id_cnt = buf->cnt;
	unsigned int index = (type == TYPE_PROCESS) ? ID_PID : ID_UID;
	unsigned int id = (type == TYPE_PROCESS) ? task_pid_nr(p) : uid;
	struct task_struct *task;
	int i;

	for (i = 0; i < id_cnt; i++) {
		if (buf->entrys[i].type == type
			&& buf->entrys[i].id[index] == id) {
			if ((type == TYPE_PROCESS && buf->entrys[i].id[ID_UID] == uid)
				|| (type == TYPE_APP)) {
				/* the unit of time_in_state is ms */
				buf->entrys[i].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
				return;
			}
		}
	}

	if (i >= ENTRY_MAX)
		return;

	/* We didn't find id_entry exsited, should create a new one */
	buf->entrys[i].id[ID_UID] = uid;
	buf->entrys[i].type = type;
	if (type == TYPE_PROCESS) {
		buf->entrys[i].id[ID_PID] = task_pid_nr(p);
		buf->entrys[i].id[ID_TGID] = task_tgid_nr(p);
		query_pid_name(p, buf->entrys[i].name[ID_PID]);
		/* Get tgid name */
		rcu_read_lock();
		task = find_task_by_vpid(buf->entrys[i].id[ID_TGID]);
		rcu_read_unlock();
		strncpy(buf->entrys[i].name[ID_TGID], task->comm, TASK_COMM_LEN);
	}
	/* the unit of time_in_state is ms */
	buf->entrys[i].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
	buf->cnt = ((id_cnt + 1) > ENTRY_MAX) ? ENTRY_MAX : (id_cnt + 1);
}

void midas_record_task_times(void *data, u64 cputime, struct task_struct *p,
					unsigned int state)
{
	unsigned long flags, user_flags;
	uid_t uid = from_kuid_munged(current_user_ns(), task_uid(p));
	struct midas_priv_info *info;
	unsigned int avail;
	struct list_head *head = get_user_list();
	struct spinlock *user_lock = get_user_lock();
	struct cpu_mmap_pool *pool;

	spin_lock_irqsave(user_lock, user_flags);
	list_for_each_entry(info, head, list) {
		if (!info)
			continue;

		spin_lock_irqsave(&info->lock, flags);
		if (info->removing || !info->mmap_addr ||
			info->type != MMAP_CPU) {
			spin_unlock_irqrestore(&info->lock, flags);
			continue;
		}
		avail = info->avail;
		pool = info->mmap_addr;

		update_entry_locked(uid, p, cputime, state, &pool->buf[avail], TYPE_APP);
		update_entry_locked(uid, p, cputime, state, &pool->buf[avail], TYPE_PROCESS);
		update_cpu_entry_locked(p, cputime, state, &pool->buf[avail]);

		spin_unlock_irqrestore(&info->lock, flags);
	}
	spin_unlock_irqrestore(user_lock, user_flags);
}

int midas_ioctl_get_time_in_state(void *kdata, void *priv_info)
{
	unsigned long flags;
	struct midas_priv_info *info = priv_info;
	unsigned int *tmp = (unsigned int *)kdata;
	struct cpu_mmap_pool *pool;

	if (IS_ERR_OR_NULL(info) ||
            IS_ERR_OR_NULL(info->mmap_addr))
		return -EINVAL;

	spin_lock_irqsave(&info->lock, flags);

	*tmp = info->avail;
	info->avail = !info->avail;
	pool = info->mmap_addr;
	memset(&pool->buf[info->avail], 0, sizeof(struct cpu_mmap_data));

	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}
