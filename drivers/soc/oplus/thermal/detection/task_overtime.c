// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/time64.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/timex.h>
#include "../../kernel/sched/sched.h"

#include "task_overtime.h"
#include <../../fs/proc/internal.h>
#include <trace/events/sched.h>
#include <trace/hooks/sched.h>

#define MAX_PID (32768)
#define CLUSTER_NUM (3)

/* the max size of abnormal task in every collection period for midas*/
#define MAX_SIZE (2048)


/* UP_THRESH: the upper time percent for power abnormal task */
#define UP_THRESH (5)

/* UP_FREQ: the upper cpufreq for abnormal task, specific for SM8450 */
#define UP_FREQ (1728000)

static DEFINE_SPINLOCK(atd_lock);

/* add for midas collection */
static int atd_count = 0;
static struct abnormal_tsk_info task_info[MAX_SIZE];
static struct kernel_task_overtime ktask_overtime[CPU_NUM];

static struct proc_dir_entry *atd = NULL;

static struct acct_tasktime *tasktimes;
static struct workqueue_struct *queue;
static struct delayed_work work;

static int sysctl_atd_enable = 1;
static int sysctl_atd_level = 1;

static inline unsigned long task_util(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_avg);
}

/* kernel sample function */
void get_task_time_handler(void *data, struct task_struct *task, struct rq *rq, int tick)
{
	int idx;
	int cpu = task_cpu(task);

	if (!sysctl_atd_enable || !rq || !task || task == rq->idle)
		return;

	idx = ktask_overtime[cpu].idx % MAX_ACCOUNT_NUM;
	ktask_overtime[cpu].task[idx].pid = task->pid;
	ktask_overtime[cpu].task[idx].uid = from_kuid_munged(current_user_ns(), task_uid(task));
	ktask_overtime[cpu].task[idx].freq = cpufreq_quick_get(task->cpu);
	ktask_overtime[cpu].task[idx].load = task_util(task);
	ktask_overtime[cpu].task[idx].exec_start = jiffies_to_nsecs(jiffies - tick);
	ktask_overtime[cpu].task[idx].end = jiffies;
	ktask_overtime[cpu].task[idx].delta = jiffies_to_nsecs(tick);
	memcpy(ktask_overtime[cpu].task[idx].comm, task->comm, TASK_COMM_LEN);
	ktask_overtime[cpu].idx = idx + 1;
}
EXPORT_SYMBOL_GPL(get_task_time_handler);

struct acct_tasktime {
	pid_t  uid;
	u64    count;
	u64    sum_exec_runtime;
	u64    sum_load;
	u64    sum_freq;
	char   comm[TASK_COMM_LEN];
};

void handle_func(u64 start, u64 end)
{
	int j, i = 0;
	while (i < CPU_NUM) {
		for (j = 0; j < MAX_ACCOUNT_NUM; j++) {
			if (ktask_overtime[i].task[j].pid >= MAX_PID || ktask_overtime[i].task[j].pid <= 0)
				continue;

			if (ktask_overtime[i].task[j].exec_start >= start && ktask_overtime[i].task[j].end <= end) {
				struct acct_tasktime *act = tasktimes + ktask_overtime[i].task[j].pid;

				act->sum_exec_runtime += ktask_overtime[i].task[j].delta;
				act->uid = ktask_overtime[i].task[j].uid;
				act->sum_freq += ktask_overtime[i].task[j].freq;
				act->sum_load += ktask_overtime[i].task[j].load;
				act->count++;
				memcpy(act->comm, ktask_overtime[i].task[j].comm, TASK_COMM_LEN);
			}
		}
		i++;
	}
}

/*********** userspace interface for midas**********/
void get_all_atd_info(int *cnt, struct abnormal_tsk_info *val)
{
	unsigned long flags;
	if (!val)
		return;

	spin_lock_irqsave(&atd_lock, flags);
	*cnt = atd_count;
	memcpy(val, task_info, sizeof(struct abnormal_tsk_info) * atd_count);
	atd_count = 0;
	spin_unlock_irqrestore(&atd_lock, flags);
}

static int tsk_show(struct seq_file *m, void *v)
{
	int idx;
	unsigned long flags;

	seq_printf(m, "%s\t%-5s\t%-10s\t%-10s\t%-10s\t%-10s\n",
        "pid", "uid", "exec_time", "avg_freq", "avg_load", "ts");

	spin_lock_irqsave(&atd_lock, flags);
	for (idx = 0; idx < atd_count; idx++) {
		struct abnormal_tsk_info *tsk = task_info + idx;
		seq_printf(m, "%-5d\t%-5d\t%-10d\t%-10d\t%-10d\t%-10d\n", tsk->pid, tsk->uid,
			tsk->runtime, tsk->avg_freq, tsk->avg_load, tsk->date);
	}
	spin_unlock_irqrestore(&atd_lock, flags);
	return 0;
}

static int tsk_open(struct inode *inode, struct file *file)
{
	return single_open(file, tsk_show, NULL);
}

static const struct proc_ops tsk_proc_fops = {
	.proc_open     = tsk_open,
	.proc_read     = seq_read,
	.proc_lseek    = seq_lseek,
	.proc_release  = single_release,
};

static inline ssize_t data_to_user(char __user *buff, size_t count, loff_t *off, char *format_str, int len)
{
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buff, format_str, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t proc_enable_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1) {
		count = sizeof(buffer) - 1;
	}

	if(copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	err = kstrtouint(strstrip(buffer), 0, &sysctl_atd_enable);
	if (err) {
		return err;
	}

	return count;
}

static ssize_t proc_enable_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[16] = {0};
	int len = 0;
	len = sprintf(page, "%d\n", sysctl_atd_enable);
	return data_to_user(buff, count, off, page, len);
}

static const struct proc_ops atd_enable_proc_fops = {
	.proc_read  = proc_enable_read,
	.proc_write = proc_enable_write,
};

static ssize_t proc_level_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1) {
		count = sizeof(buffer) - 1;
	}

	if(copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	err = kstrtouint(strstrip(buffer), 0, &sysctl_atd_level);
	if (err) {
		return err;
	}

	return count;
}

static ssize_t proc_level_read(struct file *flip, char __user *buff, size_t count, loff_t *off)
{
	char page[16] = {0};
	int len = 0;
	len = sprintf(page, "%d\n", sysctl_atd_level);
	return data_to_user(buff, count, off, page, len);
}

static const struct proc_ops atd_level_proc_fops = {
	.proc_read  = proc_level_read,
	.proc_write = proc_level_write,
};

/*****************driver internals **************************/

static void do_getboottime(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_boottime_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}

static u64 last;

static void delayed_work_func(struct work_struct *work)
{
	int idx;
	u64 now = ktime_get_ns();
	struct timeval boot_time;
	struct delayed_work *dwork = to_delayed_work(work);
	unsigned long flags;
	struct abnormal_tsk_info *tsk;

	tasktimes = (struct acct_tasktime *)vzalloc(MAX_PID * sizeof(struct acct_tasktime));
	if (!tasktimes) {
		pr_err("vmalloc failed\n");
		goto again;
	}

	handle_func(last, now);

	for (idx = 0; idx < MAX_PID; idx++) {
		struct acct_tasktime *act = tasktimes + idx;

		if (div64_u64(act->sum_exec_runtime, NSEC_PER_MSEC) >= div64_u64(PERIOD * MSEC_PER_SEC * UP_THRESH, 100)
			&& div64_u64(act->sum_freq, act->count) >= UP_FREQ) {
			if (sysctl_atd_enable) {
				spin_lock_irqsave(&atd_lock, flags);
				tsk = task_info + atd_count;

				do_getboottime(&boot_time);

				tsk->pid = idx;
				tsk->uid = act->uid;
				tsk->runtime = div64_u64(act->sum_exec_runtime, NSEC_PER_MSEC);
				tsk->avg_freq = div64_u64(act->sum_freq, act->count);
				tsk->avg_load = div64_u64(act->sum_load, act->count);

				memcpy(tsk->comm, act->comm, TASK_COMM_LEN);
				tsk->date = boot_time.tv_sec * 1000 + boot_time.tv_usec/1000;
				atd_count = (atd_count + 1) % MAX_SIZE;

				spin_unlock_irqrestore(&atd_lock, flags);
			}
		}
	}
again:
	queue_delayed_work(queue, dwork, PERIOD * HZ);
	last = ktime_get_ns();
	vfree(tasktimes);
}

/***************** register hooks **************************/
static int register_atd_vendor_hooks(void)
{
	int ret = 0;

	/* register vendor hook in kernel/sched/cputime.c */
	ret = register_trace_android_vh_account_task_time(get_task_time_handler, NULL);
	if (ret != 0) {
		pr_err(" ATD: register vendor hook failed!\n");
		return ret;
	}
	return 0;
}

static int unregister_atd_vendor_hooks(void)
{
	int ret = 0;

	unregister_trace_android_vh_account_task_time(get_task_time_handler, NULL);
	if (ret != 0) {
		pr_err(" ATD: unregister vendor hook failed!\n");
		return ret;
	}
	return 0;
}

static int __init proc_task_overtime_init(void)
{
	int ret;
	struct proc_dir_entry *pentry;

	ret = register_atd_vendor_hooks();
	if (ret != 0)
		return ret;

	atd = proc_mkdir("atd", NULL);
	if (!atd) {
		pr_err("can't create atd dir\n");
		goto ERROR_INIT_DIR;
	}

	pentry = proc_create("abnormal_task", 0, atd, &tsk_proc_fops);
	if (!pentry) {
		pr_err("create abnormal_task proc failed\n");
		goto ERROR_INIT_PROC;
	}

	pentry = proc_create("atd_enable", 0, atd, &atd_enable_proc_fops);
	if (!pentry) {
		pr_err("create atd_enable proc failed\n");
		goto ERROR_INIT_PROC;
	}

	pentry = proc_create("atd_level", 0, atd, &atd_level_proc_fops);
	if (!pentry) {
		pr_err("create atd_level proc failed\n");
		goto ERROR_INIT_PROC;
	}

	queue = create_workqueue("detect abnormal task");
	if (!queue) {
		pr_err("create workqueue failed!\n");
		goto ERROR_INIT_PROC;
	}

	last = ktime_get_ns();

	INIT_DELAYED_WORK(&work, delayed_work_func);
	queue_delayed_work(queue, &work, PERIOD * HZ);

	return 0;

ERROR_INIT_PROC:
	remove_proc_entry("abnormal_task", NULL);
ERROR_INIT_DIR:
	remove_proc_entry("atd", NULL);
	return -ENOENT;
}

static void __exit proc_task_overtime_exit(void)
{
	unregister_atd_vendor_hooks();

	if (atd) {
		remove_proc_entry("atd", NULL);
		atd = NULL;
	}
}

module_init(proc_task_overtime_init);
module_exit(proc_task_overtime_exit);
MODULE_LICENSE("GPL v2");
