// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include "jank_base.h"
#include "jank_tasktrack.h"
#include "jank_topology.h"
#include "jank_cpuload.h"
#include "jank_netlink.h"
#include "jank_cpuset.h"

int s_dstate_threshold = S_DSTATE_THRESHOLD;
int s_silver_usage = S_SILVER_USAGE;
int s_gold_usage = S_GOLD_USAGE;

int c_dstate_threshold = C_DSTATE_THRESHOLD;
int c_silver_usage = C_SILVER_USAGE;
int c_gold_usage = C_GOLD_USAGE;

#define BG_CPU_0_7		1
#define BG_CPU_0_3		0

static int send_netlink(int data)
{
	int dt[] = { data };

	return send_to_user(CPUSET_ADJUST_BG, 1, dt);
}

void jank_cpuset_adjust(struct task_track_info *trace_info)
{
	u64 dstate_time;
	u32 silver_load, gold_load;
	u32 idx, winidx;
	u32 task_num;
	static u32 bg_state = BG_CPU_0_3;

	struct state_time *time;
	struct jank_task_info *ti = NULL;
	pid_t pid;

	/* all threads feeling good */
	u32 fell_good = 0;

	if (!trace_info)
		return;

	task_num = trace_info->task_num;
	if (!task_num)
		return;

	silver_load = get_cpu_load(1, &silver_cpu);
	gold_load = get_cpu_load(1, &gold_cpu);

	for (idx = 0; idx < TASK_TRACK_NUM; idx++) {
		ti = &trace_info->task_info[idx];

		pid = ti->pid;
		if (!pid || pid == INVALID_PID)
			continue;

		winidx = ti->winidx;
		time = &ti->time[winidx];
		dstate_time = time->val[TRACE_DISKSLEEP];

		if (silver_load >= s_silver_usage &&
			gold_load <= s_gold_usage &&
			dstate_time >= s_dstate_threshold * 1000 * 1000) {
			fell_good = 0;
			break;
		} else if (silver_load <= c_silver_usage ||
			gold_load >= c_gold_usage ||
			dstate_time <= c_dstate_threshold * 1000 * 1000) {
			fell_good++;
		}
	}
#ifdef JANK_DEBUG
	jank_dbg("[CPUSET] DEBUG BYHP: [A] task_num=%d, fell_good=%d\n",
					task_num, fell_good);
#endif

	if (fell_good == 0 && bg_state == BG_CPU_0_3) {
		/* bg: cpu0~7 */
		send_netlink(BG_CPU_0_7);
		bg_state = BG_CPU_0_7;
#ifdef JANK_DEBUG
		jank_dbg("[CPUSET] DEBUG BYHP: [S] silver_load=%d, gold_load=%d, dstate_time=%llu\n",
				silver_load, gold_load, dstate_time);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset", BG_CPU_0_7);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_pid", pid);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_s1", silver_load);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_s2", gold_load);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_s3", dstate_time);
#endif
	} else if (fell_good == task_num && bg_state == BG_CPU_0_7) {
		/* bg: cpu0~3 */
		send_netlink(BG_CPU_0_3);
		bg_state = BG_CPU_0_3;
#ifdef JANK_DEBUG
		jank_dbg("[CPUSET] DEBUG BYHP: [C] silver_load=%d, gold_load=%d, dstate_time=%llu\n",
				silver_load, gold_load, dstate_time);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset", BG_CPU_0_3);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_pid", 0);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_c1", silver_load);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_c2", gold_load);
		jank_systrace_print(JANK_SYSTRACE_TASKTRACK, "cpuset_c3", dstate_time);
#endif
	}
}

static int proc_cpuset_threshold_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%d %d %d %d %d %d\n",
				s_dstate_threshold,
				s_silver_usage,
				s_gold_usage,
				c_dstate_threshold,
				c_silver_usage,
				c_gold_usage);

	return 0;
}

static ssize_t proc_cpuset_threshold_write(struct file *file,
					const char __user *buf, size_t count, loff_t *offset)
{
	char buffer[512];
	char *token, *p = buffer;
	int para_cnt = 0;
	int para[CPUSTE_PARA_CNT];

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	while ((token = strsep(&p, " ")) != NULL) {
		if (para_cnt >= CPUSTE_PARA_CNT)
			break;

		if (kstrtoint(strstrip(token), 10, &para[para_cnt]))
			return -EINVAL;

		para_cnt++;
	}

	if (para_cnt != CPUSTE_PARA_CNT) {
		pr_err("ERROR: Configure all parameters at once!!!\n");
		return -EINVAL;
	}

	s_dstate_threshold = para[0];
	s_silver_usage = para[1];
	s_gold_usage = para[2];
	c_dstate_threshold = para[3];
	c_silver_usage = para[4];
	c_gold_usage = para[5];

	return count;
}


static int proc_cpuset_threshold_open(struct inode *inode,
			struct file *file)
{
	return single_open(file, proc_cpuset_threshold_read, inode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_cpuset_threshold_operations = {
	.open = proc_cpuset_threshold_open,
	.read = seq_read,
	.write = proc_cpuset_threshold_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
const struct proc_ops proc_cpuset_threshold_operations = {
	.proc_open = proc_cpuset_threshold_open,
	.proc_read = seq_read,
	.proc_write = proc_cpuset_threshold_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

struct proc_dir_entry *jank_cpuset_threshold_proc_init(
			struct proc_dir_entry *pde)
{
	struct proc_dir_entry *entry = NULL;

	entry = proc_create("cpuset_threshold", S_IRUGO | S_IWUGO,
				pde, &proc_cpuset_threshold_operations);
	if (!entry) {
		pr_err("create cpuset_threshold fail\n");
		goto err_cpuset_threshold;
	}

	return entry;

err_cpuset_threshold:
	return NULL;
}

void jank_cpuset_threshold_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("cpuset_threshold", pde);
}

