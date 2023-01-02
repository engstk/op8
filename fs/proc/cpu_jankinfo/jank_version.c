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

#include "jank_version.h"

static int proc_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", JANK_VERSION);
	return 0;
}

static int proc_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_version_show, inode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_version_operations = {
	.open = proc_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
static const struct proc_ops proc_version_operations = {
	.proc_open = proc_version_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

struct proc_dir_entry *jank_version_proc_init(
			struct proc_dir_entry *pde)
{
	return proc_create("version", S_IRUGO,
				pde, &proc_version_operations);
}

void jank_version_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("version", pde);
}

