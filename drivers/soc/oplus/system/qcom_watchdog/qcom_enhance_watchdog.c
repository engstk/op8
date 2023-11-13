// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/rtc.h>
#include <linux/sched/debug.h>
#if IS_MODULE(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
#include "../../../kernel/irq/internals.h"
#endif

#define MASK_SIZE	32
#define MAX_IRQ_NO	1200

unsigned int smp_call_any_cpu;
unsigned long smp_call_many_cpumask;

static int oplus_print_utc_cnt = 0;

struct oplus_irq_counter {
	unsigned int all_irqs_last;
	unsigned int all_irqs_delta;
	unsigned int *irqs_last;
	unsigned int *irqs_delta;
};

static struct oplus_irq_counter o_irq_counter;
static int irqno_sort[10];

#if IS_MODULE(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
static bool irq_is_nmi(struct irq_desc *desc)
{
	return desc->istate & IRQS_NMI;
}

static unsigned int oplus_kstat_irqs(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned int sum = 0;
	int cpu;

	if (!desc || !desc->kstat_irqs)
		return 0;
	if (!irq_settings_is_per_cpu_devid(desc) &&
	    !irq_settings_is_per_cpu(desc) &&
	    !irq_is_nmi(desc))
	    return desc->tot_count;

	for_each_possible_cpu(cpu)
		sum += *per_cpu_ptr(desc->kstat_irqs, cpu);
	return sum;
}

static int oplus_task_curr(const struct task_struct *p)
{
	return p->state & TASK_RUNNING;
}
#endif

static int init_oplus_watchdog(void)
{
	if (!o_irq_counter.irqs_last) {
		o_irq_counter.irqs_last = (unsigned int *)kzalloc(
						sizeof(unsigned int)*MAX_IRQ_NO,
						GFP_KERNEL);
		if (!o_irq_counter.irqs_last) {
			return -ENOMEM;
		}
	}

	if (!o_irq_counter.irqs_delta) {
		o_irq_counter.irqs_delta = (unsigned int *)kzalloc(
						sizeof(unsigned int)*MAX_IRQ_NO,
						GFP_KERNEL);
		if (!o_irq_counter.irqs_delta) {
			kfree(o_irq_counter.irqs_last);
			return -ENOMEM;
		}
	}

	return 0;
}

static void update_irq_counter(void)
{
	int n;
	struct irq_desc *desc;
	unsigned int all_count = 0;
	unsigned int irq_count = 0;

	BUG_ON(nr_irqs > MAX_IRQ_NO);

	if (!o_irq_counter.irqs_delta || !o_irq_counter.irqs_last)
		return;

	for_each_irq_desc(n, desc) {
#if IS_MODULE(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
		irq_count = oplus_kstat_irqs(n);
#else
		irq_count = kstat_irqs(n);
#endif
		if (!desc->action && !irq_count)
			continue;

		if (irq_count <= o_irq_counter.irqs_last[n])
			o_irq_counter.irqs_delta[n] = 0;
		else
			o_irq_counter.irqs_delta[n] = irq_count - o_irq_counter.irqs_last[n];

		o_irq_counter.irqs_last[n] = irq_count;
		all_count += irq_count;
	}
	o_irq_counter.all_irqs_delta = all_count - o_irq_counter.all_irqs_last;
	o_irq_counter.all_irqs_last = all_count;
}

static void insert_irqno(int no, int i, int size)
{
	int n;
	for (n = size-1; n > i; n--) {
		irqno_sort[n] = irqno_sort[n-1];
	}
	irqno_sort[i] = no;
}

static void sort_irqs_delta(void)
{
	int irq, i;

	for (i = 0; i < 10; i++) {
		irqno_sort[i] = -1;
	}

	for_each_irq_nr(irq) {
		for (i = 0; i < 10; i++) {
			if (irqno_sort[i] == -1) {
				irqno_sort[i] = irq;
				break;
			}

			if (o_irq_counter.irqs_delta[irq] > o_irq_counter.irqs_delta[irqno_sort[i]]) {
				insert_irqno(irq, i, 10);
				break;
			}
		}
	}
}

static void print_top10_irqs(void)
{
	sort_irqs_delta();
	printk(KERN_INFO "Top10 irqs since last: %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; %d:%u; Total: %u\n",
		irqno_sort[0], o_irq_counter.irqs_delta[irqno_sort[0]], irqno_sort[1], o_irq_counter.irqs_delta[irqno_sort[1]],
		irqno_sort[2], o_irq_counter.irqs_delta[irqno_sort[2]], irqno_sort[3], o_irq_counter.irqs_delta[irqno_sort[3]],
		irqno_sort[4], o_irq_counter.irqs_delta[irqno_sort[4]], irqno_sort[5], o_irq_counter.irqs_delta[irqno_sort[5]],
		irqno_sort[6], o_irq_counter.irqs_delta[irqno_sort[6]], irqno_sort[7], o_irq_counter.irqs_delta[irqno_sort[7]],
		irqno_sort[8], o_irq_counter.irqs_delta[irqno_sort[8]], irqno_sort[9], o_irq_counter.irqs_delta[irqno_sort[9]], o_irq_counter.all_irqs_delta);
}

void oplus_dump_cpu_online_smp_call(void)
{
	static char alive_mask_buf[MASK_SIZE];
	struct cpumask avail_mask;
#ifdef CONFIG_SCHED_WALT
	cpumask_andnot(&avail_mask, cpu_online_mask, cpu_isolated_mask);
#else
	cpumask_copy(&avail_mask, cpu_online_mask);
#endif
	scnprintf(alive_mask_buf, MASK_SIZE, "%*pb1", cpumask_pr_args(&avail_mask));
	printk(KERN_INFO "cpu avail mask %s\n", alive_mask_buf);
	/* print_smp_call_cpu */
	printk(KERN_INFO "cpu of last smp_call_function_any: %d\n",
		smp_call_any_cpu);
	printk(KERN_INFO "cpumask of last smp_call_function_many: 0x%lx\n",
		smp_call_many_cpumask);
}
EXPORT_SYMBOL(oplus_dump_cpu_online_smp_call);

void oplus_get_cpu_ping_mask(cpumask_t *pmask, int *cpu_idle_pc_state)
{
	int cpu;
	struct cpumask avail_mask;
	update_irq_counter();
	cpumask_copy(pmask, cpu_online_mask);
#ifdef CONFIG_SCHED_WALT
	cpumask_andnot(&avail_mask, cpu_online_mask, cpu_isolated_mask);
#else
	cpumask_copy(&avail_mask, cpu_online_mask);
#endif
	for_each_cpu(cpu, cpu_online_mask) {
		if (cpu_idle_pc_state[cpu] || cpu_isolated(cpu))
			cpumask_clear_cpu(cpu, pmask);
	}
	printk(KERN_INFO "[wdog_util]cpu avail mask: 0x%lx; ping mask: 0x%lx; irqs since last: %u\n",
		*cpumask_bits(&avail_mask), *cpumask_bits(pmask), o_irq_counter.all_irqs_delta);
}
EXPORT_SYMBOL(oplus_get_cpu_ping_mask);

void oplus_dump_wdog_cpu(struct task_struct *w_task)
{
	int work_cpu = 0;
	int wdog_busy = 0;
#if IS_BUILTIN(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
	struct pt_regs *regs = get_irq_regs();
#endif
	update_irq_counter();
	print_top10_irqs();
	work_cpu = task_cpu(w_task);
#if IS_MODULE(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
	wdog_busy = oplus_task_curr(w_task);
#else
	wdog_busy = task_curr(w_task);
#endif
	if (wdog_busy)
		printk(KERN_EMERG "Watchdog work is running at CPU(%d)\n", work_cpu);
	else
		printk(KERN_EMERG "Watchdog work is pending at CPU(%d)\n", work_cpu);

#if IS_BUILTIN(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG)
	if (regs)
		show_regs(regs);
#endif
}
EXPORT_SYMBOL(oplus_dump_wdog_cpu);

/* replace android trigger utc time show, using watchdog print
 * petwatchdog 9s, 4 times print once
 */
void oplus_show_utc_time(void)
{
	struct timespec ts;
	struct rtc_time tm;
	if(oplus_print_utc_cnt > 2)
		oplus_print_utc_cnt = 0;
	else {
		oplus_print_utc_cnt ++;
		return;
	}
	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_warn("!@WatchDog: %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
}
EXPORT_SYMBOL(oplus_show_utc_time);

static int __init oplus_wd_init(void)
{
	int ret = 0;

	ret = init_oplus_watchdog();
	if (ret != 0) {
		pr_err("Failed to init oplus watchlog");
	}

	return ret;
}
module_init(oplus_wd_init);

MODULE_LICENSE("GPL v2");
