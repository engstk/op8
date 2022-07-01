// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/cpufreq_times.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#include <linux/midas_proc.h>

extern int __init dispcap_dev_init(void);
extern void __exit dispcap_dev_exit(void);
extern int __init vpu_pw_off_latency_proc_init(void);
extern void __exit vpu_pw_off_latency_proc_exit(void);

#define BUF_LEN		1024

static struct proc_dir_entry *g_midas_pentry;
static struct midas_id_state *g_midas_data;
static DEFINE_SPINLOCK(midas_data_lock);

#define MAX_RASMEVENT_PARAM	4
static char *rasm_env[MAX_RASMEVENT_PARAM];


struct rasm_data {
	long last_suspend_time;
	long last_resume_time;
};

static int rasm_resume(struct device *dev) {
	struct timeval resume_time;
	struct rasm_data *data;

	data = dev->driver_data;
	do_gettimeofday(&resume_time);
	data->last_resume_time = resume_time.tv_sec;
	dev_dbg(dev, "Now it is %ld, system will resume.", data->last_resume_time);

	rasm_env[0] = kasprintf(GFP_KERNEL, "NAME=rasm");
	rasm_env[1] = kasprintf(GFP_KERNEL, "suspend=%ld", data->last_suspend_time);
	rasm_env[2] = kasprintf(GFP_KERNEL, "resume=%ld", data->last_resume_time);
	rasm_env[3] = NULL;

	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, rasm_env);
	return 0;
}

static int rasm_suspend(struct device *dev) {
	struct timeval suspend_time;
	struct rasm_data *data;

	data = dev->driver_data;
	do_gettimeofday(&suspend_time);
	data->last_suspend_time = suspend_time.tv_sec;
	dev_dbg(dev, "Now it is %ld, system will suspend.", data->last_suspend_time);
	return 0;
}

static int rasm_probe(struct platform_device *pdev)
{
	struct rasm_data *data;
	data = devm_kzalloc(&pdev->dev, sizeof(struct rasm_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(&pdev->dev, "devm_kzalloc failed!");
		return -1;
	}
	dev_set_drvdata(&pdev->dev, data);
	return 0;
}

static int rasm_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct dev_pm_ops rasm_pm_ops = {
	.suspend = rasm_suspend,
	.resume = rasm_resume,
};
static struct platform_device_id tbl[] = {
	{"rasm"},
	{},
};
MODULE_DEVICE_TABLE(platform, tbl);

struct of_device_id of_tbl[] = {
	{ .compatible = "oplus,rasm", },
	{},
};

struct platform_device dev = {
.id = -1,
.name = "rasm",
};

static struct platform_driver drv = {
	.probe    = rasm_probe,
	.remove    = rasm_remove,

	.driver = {
		.name = "rasm",
		.of_match_table = of_tbl,
		.pm   = &rasm_pm_ops,
	},

	.id_table = tbl,
};

static int midas_proc_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (remap_vmalloc_range(vma, g_midas_data,
		  vma->vm_pgoff)) {
		pr_err("remap failed\n");
		return -EAGAIN;
	}

	return 0;
}

static ssize_t midas_proc_write(struct file *file, const char __user *buf,
		size_t cnt, loff_t *offset)
{
	int ret, len, type;
	char tmp[BUF_LEN + 1];
	unsigned long flags;

	if (cnt == 0)
		return 0;

	len = cnt > BUF_LEN ? BUF_LEN : cnt;

	ret = copy_from_user(tmp, buf, len);
	if (ret) {
		pr_err("copy_from_user failed, ret=%d\n", ret);
		return -EFAULT;
	}

	if (tmp[len - 1] == '\n')
		tmp[len - 1] = '\0';
	else
		tmp[len] = '\0';

	ret = sscanf(tmp, "%d", &type);
	if (ret < 1) {
		pr_err("write failed, ret=%d\n", ret);
		return -EINVAL;
	}

	if (type >= TYPE_TOTAL || type < TYPE_UID) {
		pr_err("write invalid para\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&midas_data_lock, flags);
	if (type == TYPE_UID)
		midas_get_uid_state(g_midas_data);
	else
		midas_get_pid_state(g_midas_data, type);
	spin_unlock_irqrestore(&midas_data_lock, flags);

	return cnt;
}

static const struct file_operations proc_midas_fops = {
	.write = midas_proc_write,
	.mmap = midas_proc_mmap,
};

static int __init midas_proc_init(void)
{
	int ret = 0;

	g_midas_pentry = proc_create("midas_time_in_state",
				0666, NULL, &proc_midas_fops);

	g_midas_data = vmalloc_user(sizeof(struct midas_id_state));
	if (IS_ERR_OR_NULL(g_midas_data)) {
		pr_err("malloc failed!\n");
		ret = -ENOMEM;
		goto err_malloc;
	}

	ret = platform_device_register(&dev);
	if (ret < 0) {
		dev_err(&dev.dev, "platform_device_register failed!");
		return ret;
	}
	ret = platform_driver_register(&drv);
	if (ret < 0) {
		dev_err(&dev.dev, "platform_driver_register!");
		goto err_register_driver;
	}

	// create dispcap dev
	if(dispcap_dev_init() < 0) {
		ret = -ENOMEM;
		goto err_malloc;
	}

	// create vpu pw_off_latency_proc
	if(vpu_pw_off_latency_proc_init() < 0) {
		ret = -ENOMEM;
		goto err_malloc;
	}

	return 0;

err_malloc:
	proc_remove(g_midas_pentry);
	return ret;
err_register_driver:
	platform_device_unregister(&dev);
	return ret;
}


static void __exit midas_proc_exit(void)
{
	vpu_pw_off_latency_proc_exit();
	dispcap_dev_exit();

	vfree(g_midas_data);
	proc_remove(g_midas_pentry);
	platform_driver_unregister(&drv);
	platform_device_unregister(&dev);
}

module_init(midas_proc_init);
module_exit(midas_proc_exit);
