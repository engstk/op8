/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

#include "midas_dev.h"
#include "midas_cpu.h"
#include "midas_mem.h"

/*
 * Version:
 * 3. Add OCA ioctl.
 * 2. Add MIDAS memory module.
 */
#define MIDAS_DEV_VERSION 			3

#define MIDAS_IOCTL_DEF(ioctl, _func) \
	[MIDAS_IOCTL_NR(ioctl)] = { \
		.cmd = ioctl, \
		.func = _func, \
	}

#define MIDAS_IOCTL_BASE 			'm'
#define MIDAS_IO(nr) 				_IO(MIDAS_IOCTL_BASE, nr)
#define MIDAS_IOR(nr, type) 			_IOR(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOW(nr, type) 			_IOW(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOWR(nr, type) 			_IOWR(MIDAS_IOCTL_BASE, nr, type)
#define MIDAS_IOCTL_NR(n) 			_IOC_NR(n)
#define MIDAS_CORE_IOCTL_CNT 			ARRAY_SIZE(midas_ioctls)

#define MIDAS_IOCTL_GET_TIME_IN_STATE 		MIDAS_IOR(0x1, unsigned int)
#define MIDAS_IOCTL_GET_VERSION 		MIDAS_IOR(0x5, int)
#define MIDAS_IOCTL_SET_MMAP_TYPE 		MIDAS_IOW(0x6, int)
#define MIDAS_IOCTL_GET_MEMINFO 		MIDAS_IOR(0x7, int)
#define MIDAS_IOCTL_GET_TOTALMEM 		MIDAS_IOR(0x8, unsigned long)
#define MIDAS_IOCTL_GET_OCH 		    MIDAS_IOW(0x9, int)

static int midas_ioctl_get_version(void *kdata, void *priv_info)
{
	int *tmp = (int *)kdata;

	*tmp = MIDAS_DEV_VERSION;
	return 0;
}

static int midas_ioctl_set_mmap_type(void *kdata, void *priv_info)
{
	int *type = kdata;
	struct midas_priv_info *info = priv_info;

	info->type = *type;
	return 0;
}

/* Ioctl table */
static const struct midas_ioctl_desc midas_ioctls[] = {
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_TIME_IN_STATE, midas_ioctl_get_time_in_state),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_VERSION, midas_ioctl_get_version),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_SET_MMAP_TYPE, midas_ioctl_set_mmap_type),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_MEMINFO, midas_ioctl_get_meminfo),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_TOTALMEM, midas_ioctl_get_totalmem),
	MIDAS_IOCTL_DEF(MIDAS_IOCTL_GET_OCH, midas_ioctl_get_och),
};

#define KDATA_SIZE	512
long midas_dev_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	struct midas_priv_info *info = filp->private_data;
	const struct midas_ioctl_desc *ioctl = NULL;
	midas_ioctl_t *func;
	unsigned int nr = MIDAS_IOCTL_NR(cmd);
	int ret = -EINVAL;
	char kdata[KDATA_SIZE] = { };
	unsigned int in_size, out_size;

	if (nr >=  MIDAS_CORE_IOCTL_CNT) {
		pr_err("out of array\n");
		return -EINVAL;
	}

	ioctl = &midas_ioctls[nr];

	out_size = in_size = _IOC_SIZE(cmd);
	if ((cmd & IOC_IN) == 0)
		in_size = 0;
	if ((cmd & IOC_OUT) == 0)
		out_size = 0;

	if (out_size > KDATA_SIZE || in_size > KDATA_SIZE) {
		pr_err("out of memory\n");
		ret = -EINVAL;
		goto err_out_of_mem;
	}

	func = ioctl->func;
	if (unlikely(!func)) {
		pr_err("no func\n");
		ret = -EINVAL;
		goto err_no_func;
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size)) {
		pr_err("copy_from_user failed\n");
		ret = -EFAULT;
		goto err_fail_cp;
	}

	ret = func(kdata, info);

	if (copy_to_user((void __user *)arg, kdata, out_size)) {
		pr_err("copy_to_user failed\n");
		ret = -EFAULT;
	}

err_fail_cp:
err_no_func:
err_out_of_mem:
	return ret;
}
