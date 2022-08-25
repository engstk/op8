// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/process_mm_reclaim.h>

int __weak create_process_reclaim_enable_proc(struct proc_dir_entry *parent)
{
	return 0;
}
