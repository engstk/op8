// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/limits.h>
#include <linux/printk.h>      /* for pr_err, pr_info etc */
#include <linux/mutex.h>
#include <linux/fcntl.h>
#include <linux/atomic.h>
#define FDLEAK_CHECK_LOG_TAG "[fdleak_check]"
#define FD_MAX 32768
#define DEFAULT_THRESHOLD (FD_MAX/2)
#define DEFAULT_DUMP_THRESHOLD (DEFAULT_THRESHOLD + 500)
#define FDLEAK_ALREADY_TRIGGER_FLAG 0x55
#define FDLEAK_ALREADY_DUMP_FLAG 0x56
#define SIG_FDLEAK_CHECK_TRIGGER (SIGRTMIN + 10)
#define BIONIC_SIGNAL_FDTRACK (SIGRTMIN + 7)

#define MAX_SYMBOL_LEN 64
static char symbol[MAX_SYMBOL_LEN] = "__alloc_fd";
module_param_string(symbol, symbol, sizeof(symbol), 0644);
int load_threshold = DEFAULT_THRESHOLD;
int dump_threshold = DEFAULT_DUMP_THRESHOLD;

struct fdleak_white_list_struct {
	char *comm;
	int load_threshold;
	int dump_threshold;
};

static struct fdleak_white_list_struct white_list[] = {
	{"fdleak_example", 2048, 2560},
	{"vendor.qti.hardware.display.composer-service", 2048, 2560},
	{"android.hardware.graphics.composer", 2048, 2560},
};

/* used for handle_fdleak_error serialize to avoid race condition */
static atomic_t error_is_handling;

static inline void handle_fdleak_error(struct task_struct *task)
{
	get_task_struct(task);
	send_sig(SIG_FDLEAK_CHECK_TRIGGER, task, 0);

	put_task_struct(task);
	atomic_set(&error_is_handling, 0);
}

int white_list_check(struct task_struct *p) {
        int i;

        for (i = 0; i < ARRAY_SIZE(white_list); i++) {
		if (!strncmp(p->comm, white_list[i].comm, strlen(white_list[i].comm))) {
			load_threshold = white_list[i].load_threshold;
			dump_threshold = white_list[i].dump_threshold;
			return 0;
		}
        }

        load_threshold = DEFAULT_THRESHOLD;
        dump_threshold = DEFAULT_DUMP_THRESHOLD;
        return 0;
}

static void fdleak_check(struct task_struct *p, int fd)
{
	struct task_struct *ots = p;

        if (p->sighand == NULL) {
		return;
        }

        if (p->sighand->action[SIG_FDLEAK_CHECK_TRIGGER - 1].sa.sa_handler == SIG_DFL) {
		return;
        }

	/* already fdleak, return, not check */
	if (ots->fdleak_flag == FDLEAK_ALREADY_DUMP_FLAG || p->pid != p->tgid) {
		return;
        } else if (ots->fdleak_flag == FDLEAK_ALREADY_TRIGGER_FLAG && !white_list_check(p) && fd >= dump_threshold) {
		send_sig(BIONIC_SIGNAL_FDTRACK, p, 0);
		ots->fdleak_flag = FDLEAK_ALREADY_DUMP_FLAG;
	} else if (ots->fdleak_flag != FDLEAK_ALREADY_TRIGGER_FLAG && !white_list_check(p) && fd >= load_threshold) {
		if (atomic_cmpxchg(&error_is_handling, 0, 1) != 0)
			return;

		ots->fdleak_flag = FDLEAK_ALREADY_TRIGGER_FLAG;
		handle_fdleak_error(p);
	} else {
		return;
        }
}

static int ret_handler(struct kretprobe_instance *kri, struct pt_regs *regs)
{
	int fd;

	fd = regs_return_value(regs);
	if (fd < 0) {
		return -1;
	}

	fdleak_check(kri->task, fd);
	return 0;
}

/* For each probe you need to allocate a kprobe structure */
static struct kretprobe g_krp = {
	.handler = ret_handler,
	.maxactive = 10,
};

static int __init fdleak_check_init(void)
{
	int ret;

	g_krp.kp.symbol_name = symbol;
	ret = register_kretprobe(&g_krp);
	if (ret < 0) {
		pr_err(FDLEAK_CHECK_LOG_TAG "oplus_fdleak_check, register_kretprobe failed, return %d\n", ret);
		return ret;
	}
	pr_info(FDLEAK_CHECK_LOG_TAG "oplus_fdleak_check, planted kretprobe at %p\n", g_krp.kp.addr);


	return 0;
}

static void __exit fdleak_chekc_exit(void)
{
	unregister_kretprobe(&g_krp);
	pr_info("oplus_fdleak_check, kretprobe at %p unregistered\n", g_krp.kp.addr);
}

MODULE_DESCRIPTION("oplus fdleak check");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wei.Li");

module_init(fdleak_check_init);
module_exit(fdleak_chekc_exit);

