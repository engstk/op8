// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** File : uboot_log.c
** Description : BSP uboot_log back up xbl uefi kernel boot log , cat /proc/boot_dmesg
** Version : 1.0
******************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cred.h>
#include <linux/nmi.h>
#include <linux/utsname.h>
#include <trace/events/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/signal.h>

#include <soc/oplus/system/hung_task_enhance.h>
#include <soc/oplus/system/oplus_signal.h>

#include <linux/version.h>

#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
/* 
 * format: task_name,reason. e.g. system_server,uninterruptible for 60 secs
 */
#define HUNG_TASK_KILL_LEN	128
char __read_mostly sysctl_hung_task_kill[HUNG_TASK_KILL_LEN];
#define TWICE_DEATH_PERIOD	300000000000ULL	 /* 300s */
#define MAX_DEATH_COUNT	3
#define DISP_TASK_COMM_LEN_MASK 10

/* Foreground background optimization,change max io count */
#define MAX_IO_WAIT_HUNG 5
int __read_mostly sysctl_hung_task_maxiowait_count = MAX_IO_WAIT_HUNG;
#endif

/* key process:zygote system_server surfaceflinger*/
static bool is_usersapce_key_process(struct task_struct *t)
{
	const struct cred *tcred = __task_cred(t);
	if(!strcmp(t->comm, "main") && (tcred->uid.val == 0) && (t->parent != 0 && !strcmp(t->parent->comm,"init")))
		return true;
	if(!strncmp(t->comm,"system_server", TASK_COMM_LEN)
			|| !strncmp(t->comm,"surfaceflinger", TASK_COMM_LEN) )
		return true;
	if (!strncmp(t->comm, "Binder:", 7) && (t->group_leader->pid == t->pid)
			&& (tcred->uid.val == 1000) && (t->parent != 0 && !strcmp(t->parent->comm, "main")))
		return true;

	return false;
}


static bool is_ignore_process(struct task_struct *t)
{
	if(!strncmp(t->comm,"mdss_dsi_event", TASK_COMM_LEN)||
		!strncmp(t->comm,"msm-core:sampli", TASK_COMM_LEN)||
		!strncmp(t->comm,"mdss_fb0", TASK_COMM_LEN)||
		!strncmp(t->comm,"mdss_fb_ffl0", TASK_COMM_LEN)||
		!strncmp(t->comm,"hdcp_2x", TASK_COMM_LEN)||
		!strncmp(t->comm,"dp_hdcp2p2", TASK_COMM_LEN)||
		!strncmp(t->comm,"opmonitor_boot", TASK_COMM_LEN)||
		!strncmp(t->comm,"panic_flush", TASK_COMM_LEN)||
		!strncmp(t->comm,"fb_flush", TASK_COMM_LEN)||
		!strncmp(t->comm,"crtc_commit", DISP_TASK_COMM_LEN_MASK)||
		!strncmp(t->comm,"crtc_event", DISP_TASK_COMM_LEN_MASK)){
		return true;
	}
	return false;
}

static void oplus_check_hung_task(struct task_struct *t, unsigned long timeout, unsigned int *iowait_count, bool *show_lock, bool *call_panic)
{
	unsigned long switch_count = t->nvcsw + t->nivcsw;

#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
	static unsigned long long last_death_time = 0;
	unsigned long long cur_death_time = 0;
	static int death_count = 0;
	unsigned int local_iowait = 0;
#endif

	if(is_ignore_process(t))
		return;

	/*
	 * Ensure the task is not frozen.
	 * Also, skip vfork and any other user process that freezer should skip.
	 */
	if (unlikely(t->flags & (PF_FROZEN | PF_FREEZER_SKIP)))
	{
#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
/* DeathHealer, kill D/T/t state tasks */
		if (is_usersapce_key_process(t)) 
		{
			if (t->flags & PF_FROZEN)
				return;
		}
		else
#endif
		return;
	}

	/*
	 * When a freshly created task is scheduled once, changes its state to
	 * TASK_UNINTERRUPTIBLE without having ever been switched out once, it
	 * musn't be checked.
	 */
	if (unlikely(!switch_count))
		return;

	if (switch_count != t->last_switch_count) {
		t->last_switch_count = switch_count;
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)) || defined(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM)
		t->last_switch_time = jiffies;
		#endif
		return;
	}
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)) || defined(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM)
	if (time_is_after_jiffies(t->last_switch_time + timeout * HZ))
		return;
	#endif

	trace_sched_process_hang(t);

#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
	/* kill D/T/t state tasks ,if this task blocked at iowait. so maybe we should reboot system first */
	if(t->in_iowait){
		printk(KERN_ERR "DeathHealer io wait too long time\n");
                if(t->mm != NULL && t == t->group_leader)// only work on user main thread
                {
                        *iowait_count = *iowait_count + 1;
                        local_iowait = 1;
                }
	}
	if (is_usersapce_key_process(t))
	{
		if (t->state == TASK_UNINTERRUPTIBLE)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN, "%s,uninterruptible for %lu seconds", t->comm, timeout);
		else if (t->state == TASK_STOPPED)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN, "%s,stopped for %lu seconds", t->comm, timeout);
		else if (t->state == TASK_TRACED)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN, "%s,traced for %lu seconds", t->comm, timeout);
		else
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN, "%s,unknown hung for %lu seconds", t->comm, timeout);

		printk(KERN_ERR "DeathHealer: task %s:%d blocked for more than %lu seconds in state 0x%lx. Count:%d\n",
			t->comm, t->pid, timeout, t->state, death_count+1);

                sched_show_task(t);
                debug_show_held_locks(t);
                trigger_all_cpu_backtrace();

		death_count++;
		cur_death_time = local_clock();
		if (death_count >= MAX_DEATH_COUNT) {
			if (cur_death_time - last_death_time < TWICE_DEATH_PERIOD) {
				printk(KERN_ERR "DeathHealer has been triggered %d times, \
					last time at: %llu\n", death_count, last_death_time);
				BUG();
			}
		}
		last_death_time = cur_death_time;

                if (get_eng_version() == AGING)
                        BUG();

		t->flags |= PF_KILLING;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		do_send_sig_info(SIGKILL, SEND_SIG_PRIV, t, true);
#else
		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, t, true);
#endif
		wake_up_process(t);
	}
#endif

	if (sysctl_hung_task_panic) {
		console_verbose();
		*show_lock = true;
		*call_panic = true;

		/* Panic on critical process D-state */
		if (is_usersapce_key_process(t))
		{
			trigger_all_cpu_backtrace();
			panic("hung_task: blocked tasks");
		}

	}

	/*
	 * Ok, the task did not get scheduled for more than 2 minutes,
	 * complain:
	 */
#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
    /* Modify for make sure we could print the stack of iowait thread before panic */
	if (sysctl_hung_task_warnings || local_iowait)
#else
	if (sysctl_hung_task_warnings)
#endif
	{
		if (sysctl_hung_task_warnings > 0)
			sysctl_hung_task_warnings--;
		pr_err("INFO: task %s:%d blocked for more than %ld seconds.\n",
			t->comm, t->pid, timeout);
		pr_err("      %s %s %.*s\n",
			print_tainted(), init_utsname()->release,
			(int)strcspn(init_utsname()->version, " "),
			init_utsname()->version);
		pr_err("\"echo 0 > /proc/sys/kernel/hung_task_timeout_secs\""
			" disables this message.\n");
		sched_show_task(t);
		*show_lock = true;
	}
	touch_nmi_watchdog();
}



void io_check_hung_detection(struct task_struct *t, unsigned long timeout, unsigned int *iowait_count, bool *show_lock, bool *call_panic)
{
#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
	/* add io wait monitor */
	if (t->state == TASK_UNINTERRUPTIBLE || t->state == TASK_STOPPED || t->state == TASK_TRACED)
		/* Check for selective monitoring */
		if (!sysctl_hung_task_selective_monitoring ||
			t->hang_detection_enabled)
			oplus_check_hung_task(t, timeout, iowait_count, show_lock, call_panic);
#endif
	return;
}
EXPORT_SYMBOL(io_check_hung_detection);

void io_block_panic(unsigned int *iowait_count, unsigned int sys_mamxiowait_count)
{
#ifdef CONFIG_OPLUS_FEATURE_DEATH_HEALER
/* Foreground background optimization,change max io count */
	if(*iowait_count >= sysctl_hung_task_maxiowait_count){
		panic("hung_task:[%u]IO blocked too long time",*iowait_count);
       }
#endif
	return;
}
EXPORT_SYMBOL(io_block_panic);
