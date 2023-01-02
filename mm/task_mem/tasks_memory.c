// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/sched/clock.h>
#include <linux/oom.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/version.h>

/*get gpu mem both qcom and mtk*/
#define MEM_TYPE_GL_AND_DEV (0)
#define MEM_TYPE_EGL (1)
#if defined(CONFIG_MTK_GPU_SUPPORT)
/* Mtk: get gpu mem func*/
extern unsigned long get_gpumem_by_pid(pid_t pid, int mem_type);
#elif defined(CONFIG_QCOM_KGSL)
/* Qcom: get gpu mem func*/
#include "../drivers/gpu/msm/kgsl_device.h"
static unsigned long get_gpumem_by_pid(pid_t pid, int mem_type)
{
	struct kgsl_process_private *private = NULL;
	unsigned long ret = 0;

	private = kgsl_process_private_find(pid);
	if (!private)
		return 0;

	if (mem_type == MEM_TYPE_GL_AND_DEV)
		ret = atomic_long_read(&(private->stats[KGSL_MEM_ENTRY_KERNEL].cur)) >> 10;
	else if (mem_type == MEM_TYPE_EGL)
		ret = atomic_long_read(&(private->stats[KGSL_MEM_ENTRY_ION].cur)) >> 10;

	kgsl_process_private_put(private);
	return ret;
}
#else
#define get_gpumem_by_pid(pid, mem_type) (0)
#endif

#define JUST_IGNORE (-1000)
static int show_uid_limit = 0;
static int just_show_one_uid = JUST_IGNORE;
static int just_show_one_pid = JUST_IGNORE;

struct process_mem {
	char comm[TASK_COMM_LEN];
	int pid;
	int ppid;
	int oom_score_adj;
	unsigned long rss;
	unsigned long rssfile;
	unsigned long swapents_ori;
	unsigned int uid;
	unsigned long ions;
	unsigned long gl_dev;
	unsigned long egl;
};
static struct process_mem pmem[1024];

void update_user_tasklist(struct task_struct *tsk)
{
}

static int memory_monitor_show(struct seq_file *m, void *p)
{
	struct task_struct *tsk;
	unsigned long start = 0, rcu_lock_end = 0, end = 0, task_lock_start = 0,
		      task_lock_end = 0, task_lock_time = 0, gpu_read_time = 0, gpu_read_start = 0;
	int record_tasks = 0, i = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	unsigned long ptes, pmds;
#endif

	seq_printf(m, "-------------------------------------------------------\n");
	seq_printf(m, "memory monitor[node_version:2] for ns\n");
	seq_printf(m, "-------------------------------------------------------\n");

	start = sched_clock();
	rcu_read_lock();

	seq_printf(m, "%-24s\t%-24s\t%-24s\t%-24s\t%-24s\t%-24s\t%-24s\t%-24s\t%-24s\t%-18s\n",
			"task_name", "pid", "uid", "oom_score_adj", "rss", "swapents_ori", "ion", "GL+dev", "rssfile", "ppid");
	for_each_process(tsk) {
		if (!tsk->signal)
			continue;

		if (tsk->flags & PF_KTHREAD)
			continue;

		if (just_show_one_uid != JUST_IGNORE &&
				task_uid(tsk).val != just_show_one_uid)
			continue;

		if (just_show_one_pid != JUST_IGNORE &&
				tsk->pid != just_show_one_pid)
			continue;

		if (show_uid_limit > task_uid(tsk).val)
			continue;

		task_lock(tsk);
		if (!tsk->mm) {
			task_unlock(tsk);
			continue;
		}

		task_lock_start = sched_clock();
		pmem[record_tasks].pid = tsk->pid;
		pmem[record_tasks].ppid = pid_alive(tsk) ?
			task_tgid_nr(rcu_dereference(tsk->real_parent)) : 0;
		pmem[record_tasks].uid = task_uid(tsk).val;
		memcpy(pmem[record_tasks].comm, tsk->comm, TASK_COMM_LEN);
		pmem[record_tasks].oom_score_adj = tsk->signal->oom_score_adj;
		pmem[record_tasks].rss = get_mm_rss(tsk->mm)<<2;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
		ptes = PTRS_PER_PTE * sizeof(pte_t) * atomic_long_read(&tsk->mm->nr_ptes);
		pmds = PTRS_PER_PMD * sizeof(pmd_t) * mm_nr_pmds(tsk->mm);
		pmem[record_tasks].rss += (ptes + pmds) >> 10;
#else
#ifdef CONFIG_MMU
		pmem[record_tasks].rss += atomic_long_read(&tsk->mm->pgtables_bytes) >> 10;
#endif
#endif
		pmem[record_tasks].rssfile = get_mm_counter(tsk->mm, MM_FILEPAGES) << 2;
		pmem[record_tasks].swapents_ori = get_mm_counter(tsk->mm, MM_SWAPENTS) << 2;
		task_unlock(tsk);
		pmem[record_tasks].ions = ((unsigned long)atomic64_read(&tsk->ions)) >> 10;
		gpu_read_start = sched_clock();
		task_lock_end = sched_clock();
		if (task_lock_end - task_lock_start > task_lock_time)
			task_lock_time = task_lock_end - task_lock_start;
		gpu_read_time += task_lock_end - gpu_read_start;

		record_tasks++;
		if (record_tasks >= 1024)
			break;
	}

	rcu_read_unlock();
	rcu_lock_end = sched_clock();
	for (i = 0; i < record_tasks; i++) {
		pmem[i].gl_dev = get_gpumem_by_pid(pmem[i].pid, MEM_TYPE_GL_AND_DEV);
		seq_printf(m, "%-24s\t%-24d\t%-24u\t%-24hd\t%-24lu\t%-24lu\t%-24lu\t%-24lu\t%-24lu\t%-18d\n",
				(char *)(pmem[i].comm), pmem[i].pid, pmem[i].uid,
				pmem[i].oom_score_adj, pmem[i].rss, pmem[i].swapents_ori,
				pmem[i].ions, pmem[i].gl_dev, pmem[i].rssfile, pmem[i].ppid);
	}
	end = sched_clock();
	seq_printf(m, "read time: %lu ns, rcu_lock: %lu, task_lock_max : %lu, gpu_max_read_time = %lu, record_tasks = %d\n",
			end - start, rcu_lock_end - start, task_lock_time, gpu_read_time, record_tasks);

	seq_printf(m, "-------------------------------------------------------\n");

	return 0;
}

static int memory_monitor_open(struct inode *inode, struct file *file)
{
	return single_open(file, memory_monitor_show, NULL);
}

static ssize_t memory_monitor_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
	char write_data[16] = {0};
	char *data_first_num = NULL;

	if (!buff || (len <= 0))
		return -EINVAL;

	if (len > 15)
		len = 15;
	if (copy_from_user(&write_data, buff, len)) {
		pr_err("memory_monitor_fops write error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	/* handle: just show one uid, write_data = u[uid_num] */
	data_first_num = strstr(write_data, "u");
	if (data_first_num) {
		data_first_num += 1;
		if (kstrtoint(data_first_num, 10, &just_show_one_uid)) {
			pr_err("memory_monitor_fops kstrtoul error.\n");
			return -EFAULT;
		}
		just_show_one_pid = JUST_IGNORE;
		return len;
	}

	/* handle: just show one pid, write_data = p[pid_num] */
	data_first_num = strstr(write_data, "p");
	if (data_first_num) {
		data_first_num += 1;
		if (kstrtoint(data_first_num, 10, &just_show_one_pid)) {
			pr_err("memory_monitor_fops kstrtoul error.\n");
			return -EFAULT;
		}
		just_show_one_uid = JUST_IGNORE;
		return len;
	}
	if (kstrtoint(write_data, 10, &show_uid_limit)) {
		pr_err("memory_monitor_fops kstrtoul error.\n");
		return -EFAULT;
	}

	just_show_one_pid = JUST_IGNORE;
	just_show_one_uid = JUST_IGNORE;
	return len;
}

static const struct file_operations memory_monitor_fops = {
	.open       = memory_monitor_open,
	.read       = seq_read,
	.llseek     = seq_lseek,
	.release    = single_release,
	.write      = memory_monitor_write,
};

static int __init memory_monitor_init(void)
{
	struct proc_dir_entry *pentry;

	pentry = proc_create("memory_monitor", S_IRWXUGO, NULL, &memory_monitor_fops);
	if(!pentry) {
		pr_err("create  proc memory_monitor failed.\n");
		return -1;
	}
	return 0;
}
device_initcall(memory_monitor_init);
