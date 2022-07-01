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

#ifdef CONFIG_OPLUS_FEATURE_SET_ALL_VPU_LATENCY

// import from MTK vpu driver
extern int set_all_vpu_power_off_latency(uint64_t pw_off_latency);

#define BUF_LEN		1024

static struct proc_dir_entry *g_vpu_pw_off_latency_pentry = NULL;

static ssize_t vpu_pw_off_latency_proc_write(struct file *file, const char __user *buf,
		size_t cnt, loff_t *offset)
{
	int ret, len;
	uint64_t latency_ms;
	char tmp[BUF_LEN + 1];

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

	ret = sscanf(tmp, "%llu", &latency_ms);
	if (ret < 1) {
		pr_err("write failed, ret=%d\n", ret);
		return -EINVAL;
	}
	pr_info("latency_ms cmd:%llu\n", latency_ms);

	set_all_vpu_power_off_latency(latency_ms);

	return cnt;
}

static const struct file_operations vpu_pw_off_latency_proc_fops = {
	.write = vpu_pw_off_latency_proc_write,
};

int __init vpu_pw_off_latency_proc_init(void)
{
	g_vpu_pw_off_latency_pentry = proc_create("all_vpu_pw_off_latency",
				0666, NULL, &vpu_pw_off_latency_proc_fops);
	return 0;
}


void __exit vpu_pw_off_latency_proc_exit(void)
{
	if (NULL != g_vpu_pw_off_latency_pentry) {
		proc_remove(g_vpu_pw_off_latency_pentry);
		g_vpu_pw_off_latency_pentry = NULL;
	}
}

module_init(vpu_pw_off_latency_proc_init);
module_exit(vpu_pw_off_latency_proc_exit);

#endif // #ifdef CONFIG_OPLUS_FEATURE_SET_ALL_VPU_LATENCY
