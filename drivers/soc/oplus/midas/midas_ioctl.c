/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && defined(CONFIG_ENERGY_MODEL)
#include <linux/energy_model.h>
#endif

#include "midas_dev.h"

#define MIDAS_IOCTL_DEF(ioctl, _func) \
	[MIDAS_IOCTL_NR(ioctl)] = { \
		.cmd = ioctl, \
		.func = _func, \
	}

#define MIDAS_IOCTL_BASE		'm'
#define MIDAS_IO(nr)			_IO(MIDAS_IOCTL_BASE, nr)
#define MIDAS_IOR(nr, type)		_IOR(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOW(nr, type)		_IOW(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOWR(nr, type)		_IOWR(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOCTL_NR(n)		_IOC_NR(n)
#define MIDAS_CORE_IOCTL_CNT		ARRAY_SIZE(midas_ioctls)

#define MIDAS_IOCTL_GET_TIME_IN_STATE	MIDAS_IOR(0x1, unsigned int)
#define MIDAS_IOCTL_SET_TRACK_UID	MIDAS_IOR(0x2, unsigned int)
#define MIDAS_IOCTL_REMOVE_TRACK_UID	MIDAS_IOR(0x3, unsigned int)
#define MIDAS_IOCTL_CLEAR_TRACK_UID	MIDAS_IOR(0x4, unsigned int)
#define MIDAS_IOCTL_GET_EM		MIDAS_IOR(0x10, struct em_data)

#define SYS_UID				1000
#define ROOT_UID			0
#define WQ_NAME_LEN			24
#define TRACK_UID_NUM			10

enum {
        TYPE_NONE = 0,
        TYPE_PROCESS,
        TYPE_APP,
        TYPE_TOTAL,
};

static struct midas_mmap_data midas_mmap_buf;
static DEFINE_SPINLOCK(midas_data_lock);
static unsigned int g_track_uid[TRACK_UID_NUM] = {0};

static int check_track_uid(int uid)
{
	int i;

	if (uid == ROOT_UID || uid == SYS_UID)
		return 0;

	for (i = 0; i < TRACK_UID_NUM; i++) {
		if (uid == g_track_uid[i])
			return 0;
	}

	return -1;
}

static void update_cpu_entry_locked(struct task_struct *p, u64 cputime,
					unsigned int state)
{
	unsigned int cpu = p->cpu;

	if (cpu >= CPU_MAX)
		return;

	midas_mmap_buf.cpu_entrys[cpu].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
}

static void query_pid_name(struct task_struct *p, char *pid_name)
{
	char buf[WQ_NAME_LEN] = {0};

	if (!(p->flags & PF_WQ_WORKER)) {
		strncpy(pid_name, p->comm, TASK_COMM_LEN);
	} else {
		get_worker_info(p, buf);
		if (buf[0]) {
			strncpy(pid_name, buf, TASK_COMM_LEN - 1);
			pid_name[TASK_COMM_LEN - 1] = '\0';
		} else {
			strncpy(pid_name, p->comm, TASK_COMM_LEN);
		}
	}
}

static void update_or_create_entry_locked(uid_t uid, struct task_struct *p, u64 cputime,
				unsigned int state, unsigned int type)
{
	unsigned int id_cnt = midas_mmap_buf.cnt;
	unsigned int index = (type == TYPE_PROCESS) ? ID_PID : ID_UID;
	unsigned int id = (type == TYPE_PROCESS) ? task_pid_nr(p) : uid;
	struct task_struct *task;
	int i;

	for (i = 0; i < id_cnt; i++) {
		if (midas_mmap_buf.entrys[i].type == type
			&& midas_mmap_buf.entrys[i].id[index] == id) {
			/* the unit of time_in_state is ms */
			midas_mmap_buf.entrys[i].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
			return;
		}
	}

	if (i >= CNT_MAX)
		return;

	/* We didn't find id_entry exsited, should create a new one */
	midas_mmap_buf.entrys[i].id[ID_UID] = uid;
	midas_mmap_buf.entrys[i].type = type;
	if (type == TYPE_PROCESS) {
		midas_mmap_buf.entrys[i].id[ID_PID] = task_pid_nr(p);
		midas_mmap_buf.entrys[i].id[ID_TGID] = task_tgid_nr(p);
		query_pid_name(p, midas_mmap_buf.entrys[i].pid_name);
		/* Get tgid name */
		rcu_read_lock();
		task = find_task_by_vpid(midas_mmap_buf.entrys[i].id[ID_TGID]);
		rcu_read_unlock();
		if (task != NULL) {
		    strncpy(midas_mmap_buf.entrys[i].tgid_name, task->comm, TASK_COMM_LEN);
		}
	}
	/* the unit of time_in_state is ms */
	midas_mmap_buf.entrys[i].time_in_state[state] += DIV_ROUND_CLOSEST(cputime, NSEC_PER_MSEC);
	midas_mmap_buf.cnt = ((id_cnt + 1) > CNT_MAX) ? CNT_MAX : (id_cnt + 1);
}

void midas_record_task_times(uid_t uid, u64 cputime, struct task_struct *p,
					unsigned int state) {
	unsigned long flags;

	spin_lock_irqsave(&midas_data_lock, flags);

	update_or_create_entry_locked(uid, p, cputime, state, TYPE_APP);

	if (!check_track_uid(uid))
		update_or_create_entry_locked(uid, p, cputime, state, TYPE_PROCESS);

	update_cpu_entry_locked(p, cputime, state);

	spin_unlock_irqrestore(&midas_data_lock, flags);
}
EXPORT_SYMBOL(midas_record_task_times);

static int midas_ioctl_get_time_in_state(void *kdata, void *priv_info)
{
	unsigned long flags;
	struct midas_priv_info *info = priv_info;

	if (IS_ERR_OR_NULL(info->mmap_addr))
		return -EINVAL;

	spin_lock_irqsave(&midas_data_lock, flags);

	memcpy(info->mmap_addr, &midas_mmap_buf, sizeof(struct midas_mmap_data));
	memset(&midas_mmap_buf, 0, sizeof(struct midas_mmap_data));

	spin_unlock_irqrestore(&midas_data_lock, flags);
	return 0;
}

static int midas_ioctl_remove_track_uid(void *kdata, void *priv_info)
{
	unsigned int *uid = kdata;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&midas_data_lock, flags);

	for (i = 0; i < TRACK_UID_NUM; i++) {
		if (g_track_uid[i] == *uid) {
			g_track_uid[i] = 0;
			break;
		}
	}

	spin_unlock_irqrestore(&midas_data_lock, flags);
	return 0;
}

static int midas_ioctl_clear_track_uid(void *kdata, void *priv_info)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&midas_data_lock, flags);

	for (i = 0; i < TRACK_UID_NUM; i++)
		g_track_uid[i] = 0;

	spin_unlock_irqrestore(&midas_data_lock, flags);
	return 0;
}

static int midas_ioctl_set_track_uid(void *kdata, void *priv_info)
{
	unsigned int *uid = kdata;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&midas_data_lock, flags);

	for (i = 0; i < TRACK_UID_NUM; i++) {
		if (g_track_uid[i] == 0) {
			g_track_uid[i] = *uid;
			break;
		} else if (g_track_uid[i] == *uid) {
			break;
		}
	}

	spin_unlock_irqrestore(&midas_data_lock, flags);
	return 0;
}

static int midas_ioctl_get_em(void *kdata, void *priv_info)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && defined(CONFIG_ENERGY_MODEL)
	struct em_perf_domain *em_pd = NULL;
	struct em_data data;
	int index = 0, i, cpu;

	memset(&data, 0, sizeof(struct em_data));

	for_each_possible_cpu(cpu) {
		if ((em_pd != NULL) && cpumask_test_cpu(cpu, to_cpumask((em_pd)->cpus)))
			continue;

		em_pd = em_cpu_get(cpu);
		if (!em_pd) {
			pr_err("find %d em_pd failed!\n", cpu);
			return -EINVAL;
		}

		for (i = 0; i < em_pd->nr_cap_states; i++) {
			data.power[index] = em_pd->table[i].power;
			index++;
		}
		data.cnt += em_pd->nr_cap_states;
	}

	memcpy(kdata, &data, sizeof(struct em_data));

	return 0;
#else
	return -EINVAL;
#endif
}

/* Ioctl table */
static const struct midas_ioctl_desc midas_ioctls[] = {
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_TIME_IN_STATE, midas_ioctl_get_time_in_state),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_SET_TRACK_UID, midas_ioctl_set_track_uid),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_REMOVE_TRACK_UID, midas_ioctl_remove_track_uid),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_CLEAR_TRACK_UID, midas_ioctl_clear_track_uid),
    MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_EM, midas_ioctl_get_em),
};

#define KDATA_SIZE 	512
long midas_dev_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	struct midas_priv_info *info = filp->private_data;
	const struct midas_ioctl_desc *ioctl = NULL;
	midas_ioctl_t *func;
	unsigned int nr = MIDAS_IOCTL_NR(cmd);
	int ret = -EINVAL;
	char kdata[KDATA_SIZE] = { };
	unsigned int in_size, out_size;

	if (nr >=  MIDAS_CORE_IOCTL_CNT) {
		pr_err("out of array\n");
		return -EINVAL;
	}

	ioctl = &midas_ioctls[nr];

	out_size = in_size = _IOC_SIZE(cmd);
	if ((cmd & IOC_IN) == 0)
		in_size = 0;
	if ((cmd & IOC_OUT) == 0)
		out_size = 0;

	if (out_size > KDATA_SIZE || in_size > KDATA_SIZE) {
		pr_err("out of memory\n");
		ret = -EINVAL;
		goto err_out_of_mem;
	}

	func = ioctl->func;
	if (unlikely(!func)) {
		pr_err("no func\n");
		ret = -EINVAL;
		goto err_no_func;
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size)) {
		pr_err("copy_from_user failed\n");
		ret = -EFAULT;
		goto err_fail_cp;
	}

	ret = func(kdata, info);

	if (copy_to_user((void __user *)arg, kdata, out_size)) {
		pr_err("copy_to_user failed\n");
		ret = -EFAULT;
	}

err_fail_cp:
err_no_func:
err_out_of_mem:
	return ret;
}
