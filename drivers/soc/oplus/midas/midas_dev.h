/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#ifndef __MIDAS_DEV_H__
#define __MIDAS_DEV_H__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/oplus_midas.h>

#define STATE_MAX    60
#define CNT_MAX      1024
#define CPU_MAX      8
#define EM_CNT_MAX   100

enum {
        ID_PID = 0,
        ID_TGID,
        ID_UID,
        ID_TOTAL,
};

struct id_entry {
        unsigned int id[ID_TOTAL];
        unsigned int type;
        char pid_name[TASK_COMM_LEN];
        char tgid_name[TASK_COMM_LEN];
        unsigned long long time_in_state[STATE_MAX];
};

struct cpu_entry {
        unsigned long long time_in_state[STATE_MAX];
};

struct midas_mmap_data {
        unsigned int cnt;
        struct id_entry entrys[CNT_MAX];
        struct cpu_entry cpu_entrys[CPU_MAX];
};

struct midas_dev_info {
        unsigned int version;
        dev_t devno;
        struct cdev cdev;
        struct class *class;
};

struct midas_priv_info {
        struct midas_mmap_data *mmap_addr;
};

typedef int midas_ioctl_t(void *kdata, void *priv_info);

struct midas_ioctl_desc {
        unsigned int cmd;
        midas_ioctl_t *func;
};

struct em_data {
        unsigned int cnt;
        unsigned int power[EM_CNT_MAX];
};

long midas_dev_ioctl(struct file *filp,
                unsigned int cmd, unsigned long arg);

#endif
