// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>

#define MAX_CMDLINE_PARAM_LEN 1024
char saupwk_enable[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(saupwk_enable);

module_param_string(en, saupwk_enable, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(en,
"en=<en>");

MODULE_LICENSE("GPL v2");
