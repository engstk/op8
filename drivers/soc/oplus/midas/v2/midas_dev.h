/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#ifndef __MIDAS_DEV_H__
#define __MIDAS_DEV_H__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <trace/hooks/midas_dev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/cred.h>

#define ENTRY_MAX           1024
#define BUF_NUM             2

enum {
        MMAP_CPU = 0,
        MMAP_MEM,
        MMAP_OCH,
        MMAP_TYPE_MAX,
};

struct midas_dev_info {
        unsigned int version;
        dev_t devno;
        struct cdev cdev;
        struct class *class;
};

struct midas_priv_info {
        int type;
        struct list_head list;
        spinlock_t lock;
        unsigned int avail;
        void *mmap_addr;
        bool removing;
};

typedef int midas_ioctl_t(void *kdata, void *priv_info);

struct midas_ioctl_desc {
        unsigned int cmd;
        midas_ioctl_t *func;
};

long midas_dev_ioctl(struct file *filp,
                unsigned int cmd, unsigned long arg);
struct list_head *get_user_list(void);
struct spinlock *get_user_lock(void);

int __init midas_dev_init(void);
void __exit midas_dev_exit(void);

#endif
