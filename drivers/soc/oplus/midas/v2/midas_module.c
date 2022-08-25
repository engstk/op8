/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include "midas_dev.h"

extern int __init binder_stats_dev_init(void);
extern void __exit binder_stats_dev_exit(void);

static int __init midas_module_init(void)
{
	midas_dev_init();
	binder_stats_dev_init();

	return 0;
}

static void __exit midas_module_exit(void)
{
	midas_dev_exit();
	binder_stats_dev_exit();
}

module_init(midas_module_init);
module_exit(midas_module_exit);
MODULE_LICENSE("GPL v2");
