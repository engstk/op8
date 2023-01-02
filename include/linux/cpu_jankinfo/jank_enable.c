// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <uapi/linux/sched/types.h>

#include "../../../fs/proc/internal.h"
#include "jank_enable.h"

unsigned int cpu_jank_info_enable;

static ssize_t proc_jank_info_enable_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", cpu_jank_info_enable);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_jank_info_enable_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, enable;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &enable);
	if (err)
		return err;

	cpu_jank_info_enable = enable;

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_jank_info_enable_operations = {
	.read = proc_jank_info_enable_read,
	.write = proc_jank_info_enable_write,
};
#else
static const struct proc_ops proc_jank_info_enable_operations = {
	.proc_read = proc_jank_info_enable_read,
	.proc_write = proc_jank_info_enable_write,
};
#endif

struct proc_dir_entry *jank_enable_proc_init(
			struct proc_dir_entry *pde)
{
	return proc_create("enable", S_IRUGO | S_IWUGO,
				pde, &proc_jank_info_enable_operations);
}

void jank_enable_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("enable", pde);
}
