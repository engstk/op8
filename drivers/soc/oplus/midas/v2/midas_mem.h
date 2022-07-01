/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#ifndef __MIDAS_MEM_H__
#define __MIDAS_MEM_H__

#include <linux/kernel.h>
#include "midas_dev.h"

#define MEMINFO_NUM	        10

enum {
    MEM_TGID = 0,
    MEM_UID,
    MEM_ID_TOTAL,
};

struct procmem_entry {
        int id[MEM_ID_TOTAL];
        char comm[TASK_COMM_LEN];
        int oom_score_adj;
        unsigned long rss;
        unsigned long gl_dev;
};

struct meminfo_entry {
        unsigned long value;
};

struct mem_mmap_data {
        unsigned int cnt;
        struct procmem_entry entrys[ENTRY_MAX];
        struct meminfo_entry meminfo_entrys[MEMINFO_NUM];
};

int midas_ioctl_get_meminfo(void *kdata, void *priv_info);
int midas_ioctl_get_totalmem(void *kdata, void *priv_info);
#endif
