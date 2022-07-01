/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef VM_ANTI_FRAGMENT_H
#define VM_ANTI_FRAGMENT_H

#define CPU_OOM_TRIGGER_GAP (HZ * 120)
#define HUNDRED_M (100 * 1024 * 1024)

extern int vm_fra_op_enabled;
extern int cpu_oom_event_enable;
extern int vm_search_two_way;

extern void trigger_cpu_oom_event(unsigned long len);

extern const struct file_operations vm_fra_op_fops;
extern const struct file_operations brk_accounts_fops;
extern const struct file_operations vm_search_two_way_fops;

#endif
