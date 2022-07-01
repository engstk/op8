// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <../fs/proc/internal.h>
#include <../kernel/sched/sched.h>

#include <soc/oplus/healthinfo.h>

void update_jank_trace_info(struct task_struct *tsk, int trace_type, unsigned int cpu, u64 delta)
{
	struct root_domain *rd = NULL;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	static unsigned int ltt_cpu_nr = 0;
#endif /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/

	if (!tsk->jank_trace) {
		return;
	}

	rd = cpu_rq(smp_processor_id())->rd;

	if (!rd)
		return;

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if (!ltt_cpu_nr) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#ifdef CONFIG_SCHED_WALT
		ltt_cpu_nr = cpumask_weight(topology_core_cpumask(rd->wrd.min_cap_orig_cpu));
#else
		ltt_cpu_nr = 4;
#endif
#else /* KERNEL_VERSION */
		ltt_cpu_nr = cpumask_weight(topology_core_cpumask(rd->min_cap_orig_cpu));
#endif /* KERNEL_VERSION */
	}
#endif /* CONFIG_OPLUS_SYSTEM_KERNEL_QCOM */

	if (trace_type == JANK_TRACE_RUNNABLE) { // runnable
		tsk->jank_info.runnable_state           += delta;
	} else if (trace_type == JANK_TRACE_DSTATE) { // D state
		tsk->jank_info.d_state.cnt++;
		if (tsk->in_iowait) {
			tsk->jank_info.d_state.iowait_ns    += delta;
		} else if (tsk->in_mutex) {
			tsk->jank_info.d_state.mutex_ns     += delta;
		} else if (tsk->in_downread) {
			tsk->jank_info.d_state.downread_ns  += delta;
		} else if (tsk->in_downwrite) {
			tsk->jank_info.d_state.downwrite_ns += delta;
		} else {
			tsk->jank_info.d_state.other_ns     += delta;
		}
	} else if (trace_type == JANK_TRACE_SSTATE) { // S state
		tsk->jank_info.s_state.cnt++;
		if (tsk->in_binder) {
			tsk->jank_info.s_state.binder_ns    += delta;
		} else if (tsk->in_futex) {
			tsk->jank_info.s_state.futex_ns     += delta;
		} else if (tsk->in_epoll) {
			tsk->jank_info.s_state.epoll_ns     += delta;
		} else {
			tsk->jank_info.s_state.other_ns     += delta;
		}
	} else if (trace_type == JANK_TRACE_RUNNING) { // running
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && !defined(CONFIG_SCHED_WALT)
		bool ltt_cpu = capacity_orig_of(cpu) < rd->max_cpu_capacity.val;
#else
		bool ltt_cpu = cpu < ltt_cpu_nr;
#endif
#else /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/
		bool ltt_cpu = capacity_orig_of(cpu) < rd->max_cpu_capacity.val;
#endif /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/
		if (ltt_cpu) {
			tsk->jank_info.ltt_running_state += delta;
		} else {
			tsk->jank_info.big_running_state += delta;
		}
	}
}

static int proc_jank_trace_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p;
	u64 d_time, s_time, ltt_time, big_time, rn_time, iow_time, binder_time, futex_time;
	p = get_proc_task(inode);
	if (!p) {
		return -ESRCH;
	}
	task_lock(p);
	iow_time = p->jank_info.d_state.iowait_ns;
	binder_time = p->jank_info.s_state.binder_ns;
	futex_time = p->jank_info.s_state.futex_ns;

	d_time = p->jank_info.d_state.iowait_ns + p->jank_info.d_state.mutex_ns +
		p->jank_info.d_state.downread_ns + p->jank_info.d_state.downwrite_ns +
		p->jank_info.d_state.other_ns;

	s_time = p->jank_info.s_state.binder_ns + p->jank_info.s_state.futex_ns +
		p->jank_info.s_state.epoll_ns + p->jank_info.s_state.other_ns;

	ltt_time = p->jank_info.ltt_running_state;

	big_time = p->jank_info.big_running_state;

	rn_time = p->jank_info.runnable_state;

	task_unlock(p);

	seq_printf(m, "BR:%llu LR:%llu RN:%llu D:%llu IOW:%llu S:%llu BD:%llu FT:%llu\n",
		big_time / NSEC_PER_MSEC, ltt_time / NSEC_PER_MSEC, rn_time / NSEC_PER_MSEC,
		d_time / NSEC_PER_MSEC, iow_time / NSEC_PER_MSEC,
		s_time / NSEC_PER_MSEC, binder_time / NSEC_PER_MSEC, futex_time / NSEC_PER_MSEC);
	put_task_struct(p);
	return 0;
}

static int proc_jank_trace_open(struct inode* inode, struct file *filp)
{
	return single_open(filp, proc_jank_trace_show, inode);
}

static ssize_t proc_jank_trace_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int err, jank_trace;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	err = kstrtoint(strstrip(buffer), 0, &jank_trace);
	if(err) {
		return err;
	}
	task = get_proc_task(file_inode(file));
	if (!task) {
		return -ESRCH;
	}

	if (jank_trace == 1) {
		task->jank_trace = 1;
	} else if (jank_trace == 0) {
		task->jank_trace = 0;
		memset(&task->jank_info, 0, sizeof(struct jank_monitor_info));
	}

	put_task_struct(task);
	return count;
}

const struct file_operations proc_jank_trace_operations = {
	.open		= proc_jank_trace_open,
	.write		= proc_jank_trace_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
