/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/sched/task.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include "midas_dev.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#include <linux/timekeeping32.h>
#else
#include <linux/timekeeping.h>
#endif

#define MIDAS_MAX_DEVS		1
#define MAX_RASMEVENT_PARAM	7

static char *rasm_env[MAX_RASMEVENT_PARAM];

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static inline void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}
#endif

static void do_getboottime(struct timeval *tv)
{
	struct timespec64 now;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
        ktime_get_boottime_ts64(&now);
#else
	get_monotonic_boottime64(&now);
#endif
        tv->tv_sec = now.tv_sec;
        tv->tv_usec = now.tv_nsec/1000;
}

struct rasm_data {
	long last_suspend_time;
	long last_resume_time;
	long last_resume_boot_time;
	long last_suspend_millsec_time;
	long last_resume_millsec_time;
};

static int rasm_resume(struct device *dev) {
	struct timeval resume_time;
	struct timeval resume_boot_time;
	struct rasm_data *data;
	int index = 0;
	int i;

	data = dev->driver_data;
	do_gettimeofday(&resume_time);
	data->last_resume_time = resume_time.tv_sec;
	data->last_resume_millsec_time = resume_time.tv_sec * 1000 + resume_time.tv_usec / 1000;
	do_getboottime(&resume_boot_time);
	data->last_resume_boot_time = resume_boot_time.tv_sec * 1000 + resume_boot_time.tv_usec / 1000;
	dev_dbg(dev, "Now it is %ld, system will resume.", data->last_resume_time);

	rasm_env[index++] = kasprintf(GFP_KERNEL, "NAME=rasm");
	rasm_env[index++] = kasprintf(GFP_KERNEL, "suspend=%ld", data->last_suspend_time);
	rasm_env[index++] = kasprintf(GFP_KERNEL, "resume=%ld", data->last_resume_time);
	rasm_env[index++] = kasprintf(GFP_KERNEL, "resume_boot=%ld", data->last_resume_boot_time);
	rasm_env[index++] = kasprintf(GFP_KERNEL, "resume_millsec=%ld", data->last_resume_millsec_time);
	rasm_env[index++] = kasprintf(GFP_KERNEL, "suspend_millsec=%ld", data->last_suspend_millsec_time);
	rasm_env[index++] = NULL;

        if (index > ARRAY_SIZE(rasm_env))
		BUG();

	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, rasm_env);
	for (i = 0; i < index - 1; ++i)
		kfree(rasm_env[i]);
	return 0;
}

static int rasm_suspend(struct device *dev) {
	struct timeval suspend_time;
	struct rasm_data *data;

	data = dev->driver_data;
	do_gettimeofday(&suspend_time);
	data->last_suspend_time = suspend_time.tv_sec;
	data->last_suspend_millsec_time = suspend_time.tv_sec * 1000 + suspend_time.tv_usec / 1000;
	dev_dbg(dev, "Now it is %ld, system will suspend.", data->last_suspend_time);
	return 0;
}
static const struct dev_pm_ops rasm_pm_ops = {
	.suspend = rasm_suspend,
	.resume = rasm_resume,
};
static int midas_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct midas_priv_info *info = filp->private_data;
	if (IS_ERR_OR_NULL(info))
		return -EINVAL;

	if (remap_vmalloc_range(vma, info->mmap_addr,
		  vma->vm_pgoff)) {
		pr_err("remap failed\n");
		return -EAGAIN;
	}

	return 0;
}

static int midas_dev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;

	struct midas_priv_info *info = kzalloc(sizeof(struct midas_priv_info), GFP_KERNEL);
	if (IS_ERR_OR_NULL(info))
		return -ENOMEM;

	info->mmap_addr = vmalloc_user(sizeof(struct midas_mmap_data));
	if (IS_ERR_OR_NULL(info->mmap_addr)) {
		pr_err("mmap_addr vmalloc failed!\n");
		ret = -ENOMEM;
		goto err_info_alloc;
	}

	filp->private_data = info;
	return 0;

err_info_alloc:
	kfree(info);
	return ret;
}

static int midas_dev_release(struct inode *inode, struct file *filp)
{
	struct midas_priv_info *info = filp->private_data;
	if (IS_ERR_OR_NULL(info))
		return 0;

	if (info->mmap_addr != NULL)
		vfree(info->mmap_addr);

	kfree(info);
	return 0;
}

static const struct file_operations midas_dev_fops = {
	.open = midas_dev_open,
	.release = midas_dev_release,
	.mmap = midas_dev_mmap,
	.unlocked_ioctl = midas_dev_ioctl,
};

static int midas_pdev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev;
	struct midas_dev_info *dev_info;

	dev_info = kzalloc(sizeof(struct midas_dev_info), GFP_KERNEL);
	if (IS_ERR_OR_NULL(dev_info)) {
		pr_err("Fail to alloc dev info\n");
		ret = -ENOMEM;
		goto err_info_alloc;
	}

	ret = alloc_chrdev_region(&dev_info->devno, 0, MIDAS_MAX_DEVS, "midas_dev");
	if (ret) {
		pr_err("Fail to alloc devno, ret=%d\n", ret);
		goto err_cdev_alloc;
	}

	cdev_init(&dev_info->cdev, &midas_dev_fops);
	ret = cdev_add(&dev_info->cdev, dev_info->devno, MIDAS_MAX_DEVS);
	if (ret) {
		pr_err("Fail to add cdev, ret=%d\n", ret);
		goto err_cdev_add;
	}

	dev_info->class = class_create(THIS_MODULE, "midas");
	if (IS_ERR_OR_NULL(dev_info->class)) {
		pr_err("Fail to create class, ret=%d\n", ret);
		goto err_class_create;
	}

	dev = device_create(dev_info->class, NULL, dev_info->devno,
				dev_info, "midas_dev");

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("Fail to create device, ret=%d\n", ret);
		goto err_device_create;
	}

	platform_set_drvdata(pdev, dev_info);

	return 0;

err_device_create:
	class_destroy(dev_info->class);
err_class_create:
	cdev_del(&dev_info->cdev);
err_cdev_add:
	unregister_chrdev_region(dev_info->devno, MIDAS_MAX_DEVS);
err_cdev_alloc:
	kfree(dev_info);
err_info_alloc:
	return ret;
}


static int midas_pdev_remove(struct platform_device *pdev)
{
	struct midas_dev_info *dev_info = platform_get_drvdata(pdev);
	if (IS_ERR_OR_NULL(dev_info))
		return -EINVAL;

	device_destroy(dev_info->class, dev_info->devno);
	class_destroy(dev_info->class);
	cdev_del(&dev_info->cdev);
	unregister_chrdev_region(dev_info->devno, MIDAS_MAX_DEVS);
	kfree(dev_info);

	return 0;
}

static const struct of_device_id midas_pdev_dt_ids[] = {
	{ .compatible = "oplus,midas-pdev", },
	{ }
};

static struct platform_driver midas_pdev_driver = {
	.driver = {
		.name = "midas-pdev",
		.of_match_table = midas_pdev_dt_ids,
		.pm   = &rasm_pm_ops,
	},
	.probe = midas_pdev_probe,
	.remove = midas_pdev_remove,
};

static int __init midas_dev_init(void)
{
	return platform_driver_register(&midas_pdev_driver);
}

static void __exit midas_dev_exit(void)
{
	return platform_driver_unregister(&midas_pdev_driver);
}

module_init(midas_dev_init);
module_exit(midas_dev_exit);
