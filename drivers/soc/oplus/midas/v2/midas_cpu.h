/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#ifndef __MIDAS_CPU_H__
#define __MIDAS_CPU_H__

#include <linux/kernel.h>
#include "midas_dev.h"
#include "../../cpufreq_health/cpufreq_health.h"

#define STATE_MAX    60
#define CPU_MAX      8

enum {
        ID_PID = 0,
        ID_TGID,
        ID_PROC_TOTAL,
        ID_UID = 2,
        ID_TOTAL,
};

struct id_entry {
        unsigned int id[ID_TOTAL];
        unsigned int type;
        char name[ID_PROC_TOTAL][TASK_COMM_LEN];
        unsigned long long time_in_state[STATE_MAX];
};

struct cpu_entry {
        unsigned long long time_in_state[STATE_MAX];
};

struct cpu_mmap_data {
        unsigned int cnt;
        struct id_entry entrys[ENTRY_MAX];
        struct cpu_entry cpu_entrys[CPU_MAX];
};

struct cpu_mmap_pool {
        struct cpu_mmap_data buf[BUF_NUM];
};

struct cpu_och_data {
        unsigned int cnt;
        struct cpufreq_health_info info[STATE_MAX];
};

int midas_ioctl_get_time_in_state(void *kdata, void *priv_info);
int midas_ioctl_get_och(void *kdata, void *priv_info);
void midas_record_task_times(void *data, u64 cputime, struct task_struct *p, unsigned int state);
#endif /* __MIDAS_CPU_H__ */
