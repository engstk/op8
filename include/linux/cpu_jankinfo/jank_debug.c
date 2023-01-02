// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/cgroup.h>
#include <linux/completion.h>
#include <uapi/linux/sched/types.h>

#include "jank_debug.h"
#include "jank_cpuload.h"
#include "jank_topology.h"

#define CPU_NUMS	8

static DECLARE_COMPLETION(sched_test);
struct kthread_param {
	int policy;
	int nice;
	int rt_priority;
	struct cpumask mask;
	int busy_us;
	int idle_us;
	u32 second;
	u32 flag;
};

static void busy_wait(int timeout)
{
	ktime_t start, end, delta;

	start = ktime_get();
	do {
		udelay(100);
		end = ktime_get();
		delta = ktime_sub(end, start);
	} while ((ktime_to_us(delta)) < timeout);
}

static int sa_test(void *data)
{
	struct kthread_param *param = data;
	int busy_us = param->busy_us;
	int idle_us = param->idle_us;
	u32 second = param->second;
	u32 flag = param->flag;
	u32 i;

	if (!cpumask_empty(&param->mask))
		set_cpus_allowed_ptr(current, &param->mask);

	if (param->policy == SCHED_NORMAL) {
		set_user_nice(current, param->nice);
	} else if (param->policy == SCHED_FIFO) {
		struct sched_param sp;

		sp.sched_priority = param->rt_priority;
		sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
	}

	complete(&sched_test);

	/* will run continuously for N seconds */
	for (i = 0; i < second*10; i++) {
		if (flag && (!(i & 0x7)))
			busy_wait(busy_us*10);
		else
			busy_wait(busy_us);

		usleep_range(idle_us, idle_us+100);
	}

	return 0;
}

void bounded_target_latency_test(u32 cpu, u32 load, u32 second)
{
	struct kthread_param param;
	static u32 id;
	u32 busy, idle;

	if (cpu >= CPU_NUMS || load > 100)
		return;

	param.policy = SCHED_NORMAL;
	param.nice = -20;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(cpu, &param.mask);

	busy = load;
	idle = 100 - busy;

	param.busy_us = busy * USEC_PER_MSEC;
	param.idle_us = idle * USEC_PER_MSEC;
	/*
	 * param.busy_us = busy;
	 * param.idle_us = idle;
	 */

	param.second = second;
	param.flag = 0;

	kthread_run(sa_test, (void *)&param, "jank_test%d", id++);

	wait_for_completion(&sched_test);
}

void pelt_util_est(u32 runtime, u32 sleeptime,
			u32 cpu, u32 second, u32 flag)
{
	struct kthread_param param;
	static u32 id;

	if (cpu >= CPU_NUMS)
		return;

	param.policy = SCHED_NORMAL;
	param.nice = -20;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(cpu, &param.mask);

	param.busy_us = runtime * USEC_PER_MSEC;
	param.idle_us = sleeptime * USEC_PER_MSEC;
	param.second = second;
	param.flag = flag;

	kthread_run(sa_test, (void *)&param, "pelt_test%d", id++);

	wait_for_completion(&sched_test);
}

void __maybe_unused bounded_target_latency_test_bak(void)
{
	struct kthread_param param;

	param.policy = SCHED_NORMAL;
	param.nice = -20;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(3, &param.mask);
	param.busy_us = 200 * USEC_PER_MSEC;
	param.idle_us = 20 * USEC_PER_MSEC;

	kthread_run(sa_test, (void *)&param, "sa_test_high");

	wait_for_completion(&sched_test);

	param.policy = SCHED_NORMAL;
	param.nice = 0;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(3, &param.mask);
	param.busy_us = 200 * USEC_PER_MSEC;
	param.idle_us = 20 * USEC_PER_MSEC;

	kthread_run(sa_test, (void *)&param, "sa_test_low1");

	wait_for_completion(&sched_test);

	param.policy = SCHED_NORMAL;
	param.nice = 0;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(3, &param.mask);
	param.busy_us = 200 * USEC_PER_MSEC;
	param.idle_us = 20 * USEC_PER_MSEC;

	kthread_run(sa_test, (void *)&param, "sa_test_low2");

	wait_for_completion(&sched_test);
#ifdef JANKINFO_DEBUG
	param.policy = SCHED_FIFO;
	param.rt_priority = 1;
	cpumask_clear(&param.mask);
	cpumask_set_cpu(3, &param.mask);
	param.busy_us = 200;
	param.idle_us = 1000;

	kthread_run(sa_test, (void *)&param, "sa_test_rt");

	wait_for_completion(&sched_test);
#endif
}

#define PARA_NUM		10
static int jank_unit_test(char *buf)
{
	u32 i, j;
	struct cpumask mask;
	u32 testcase, para_count = 0;
	u32 cpu, load, second;
	u32 para[PARA_NUM] = {0};
	char *token;
	u32 runtime, sleeptime, flag;
	struct perf_domain *pd;
	struct root_domain *rd;
	struct cpumask *pd_mask;

	while ((token = strsep(&buf, " ")) != NULL) {
		if (para_count >= PARA_NUM)
			break;

		if (kstrtoint(strstrip(token), 10, &para[para_count]))
			return -EINVAL;

		para_count++;
	}
	testcase = para[0];

	switch (testcase) {
	case 1:
		cpu = smp_processor_id();
		rd = cpu_rq(cpu)->rd;
		pd = rcu_dereference(rd->pd);
		if (!pd)
			break;

		pd_mask = perf_domain_span(pd);
		pr_info("DEBUG BYHP0: cpu=%d, 0x=%x, pd_mask=[%*pbl]\n",
						cpu, pd, cpumask_pr_args(pd_mask));

		for (; pd; pd = pd->next) {
			pd_mask = perf_domain_span(pd);
			pr_info("DEBUG BYHP1: cpu=%d, 0x=%x, pd_mask=[%*pbl]\n",
				cpu, pd, cpumask_pr_args(pd_mask));
		}

		pr_info("DEBUG BYHP2: cpu=%d, 0x=%x, pd_mask=[%*pbl]\n",
				cpu, pd, cpumask_pr_args(pd_mask));
		break;
	case 2:
		for (i = 0; i < 16; i++) {
			cpumask_clear(&mask);
			for (j = 0; j < 8; j++) {
				cpumask_set_cpu(j, &mask);
				pr_info("DEBUG BYHP case2:i=%d, cpu[%*pbl], "
						"load=%d, load32=%d\n",
						i, cpumask_pr_args(&mask),
						get_cpu_load(i, &mask),
						get_cpu_load32(i, &mask));
			}
		}
		break;
	case 3:
		cpu = para[1];
		load = para[2];
		second = para[3];
		bounded_target_latency_test(cpu, load, second);
		break;
	case 4:
		load = para[1];
		second = para[2];
		for (i = 0; i < 8; i++)
			bounded_target_latency_test(i, load, second);

		break;
	case 5:
		runtime = para[1];
		sleeptime = para[2];
		cpu = para[3];
		second = para[4];
		flag = para[5];
		pelt_util_est(runtime, sleeptime, cpu, second, flag);
		break;
	case 6:
		pr_info("DEBUG BYHP: all_cpu[%*pbl], "
						"silver_cpu[%*pbl], "
						"gold_cpu[%*pbl]\n",
						cpumask_pr_args(&all_cpu),
						cpumask_pr_args(&silver_cpu),
						cpumask_pr_args(&gold_cpu));
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t proc_debug_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[256];

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	(void) jank_unit_test(buffer);

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_debug_ops = {
	.write = proc_debug_write,
};
#else
static const struct proc_ops proc_debug_ops = {
	.proc_write = proc_debug_write,
};
#endif

struct proc_dir_entry *jank_debug_proc_init(
			struct proc_dir_entry *pde)
{
	return proc_create("debug", S_IWUGO, pde, &proc_debug_ops);
}

void jank_debug_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("debug", pde);
}
