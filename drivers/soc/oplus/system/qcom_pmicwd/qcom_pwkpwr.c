// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/kthread.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <uapi/linux/sched/types.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "soc/oplus/system/oplus_project.h"
#include <linux/input/qpnp-power-on.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <soc/oplus/system/qcom_pmicwd.h>
#else
#include <linux/regmap.h>
#include <linux/input/qpnp-power-on.h>
#endif

#define QPNP_PON_KPDPWR_S1_TIMER(pon)		((pon)->base + 0x40)
#define QPNP_PON_KPDPWR_S2_TIMER(pon)		((pon)->base + 0x41)
#define QPNP_PON_KPDPWR_S2_CNTL(pon)		((pon)->base + 0x42)
#define QPNP_PON_KPDPWR_S2_CNTL2(pon)		((pon)->base + 0x43)

#define QPNP_PON_RESET_S1_TIMER(pon)		((pon)->base + 0x44)
#define QPNP_PON_RESET_S2_TIMER(pon)		((pon)->base + 0x45)
#define QPNP_PON_RESET_S2_CNTL(pon)		    ((pon)->base + 0x46)
#define QPNP_PON_RESET_S2_CNTL2(pon)		((pon)->base + 0x47)

#define QPNP_PON_S2_CNTL2_EN			BIT(7)
#define QPNP_PON_S2_CNTL2_DIS			0x0

#define QPNP_PON_S2_CNTL_TYPE_MASK		(0xF)
#define QPNP_PON_S2_CNTL_EN			0x1
#define QPNP_PON_S2_CNTL_DIS			0x7

#define QPNP_PON_S1_TIMER_MASK			(0xF)
#define QPNP_PON_KPDPWR_S1_TIMER_TIME    0xF

#define QPNP_PON_S2_TIMER_MASK			(0x7)
#define QPNP_PON_KPDPWR_S2_TIMER_TIME    0x7

#define SEQ_printf(m, x...)     \
    do {                        \
        if (m)                  \
            seq_printf(m, x);   \
        else                    \
            pr_debug(x);        \
    } while (0)

/**
 * add qpnp-power-on  kpdpwr off/on
 *
**/
static bool pwkpwr_enable;
static bool volup_enable;

static ssize_t kpdpwr_proc_write(struct file *filep, const char __user *ubuf,
		size_t cnt,loff_t *data)
{
	struct qpnp_pon *pon = sys_reset_dev;
	char buf[64];
	long val = 0;
	int rc = 0;

	if (cnt >= sizeof(buf)) {
		return -EINVAL;
	}
	if (copy_from_user(&buf, ubuf, cnt)) {
		return -EFAULT;
	}
	buf[cnt] = 0;
	rc = kstrtoul(buf, 0, (unsigned long *)&val);
	if (rc < 0) {
		return rc;
	}
	if(!pon){
		return -EFAULT;
	}
	if (val == 1)
	{
		pr_err("QPNP_PON_KPDPWR_S2_CNTL2(pon) addr:%x", QPNP_PON_KPDPWR_S2_CNTL2(pon));
		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S2_CNTL2(pon),
				QPNP_PON_S2_CNTL2_EN, QPNP_PON_S2_CNTL2_EN);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S2_CNTL2(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S2_CNTL(pon),
				QPNP_PON_S2_CNTL_TYPE_MASK, QPNP_PON_S2_CNTL_EN);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S2_CNTL(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S1_TIMER(pon),
				QPNP_PON_S1_TIMER_MASK, QPNP_PON_KPDPWR_S1_TIMER_TIME);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S1_TIMER(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S2_TIMER(pon),
				QPNP_PON_S2_TIMER_MASK, QPNP_PON_KPDPWR_S2_TIMER_TIME);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S2_TIMER(pon), rc);
		pwkpwr_enable = true;
	}
	if (val == 0)
	{
		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S2_CNTL2(pon),
				QPNP_PON_S2_CNTL2_EN, QPNP_PON_S2_CNTL2_DIS);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S2_CNTL2(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_KPDPWR_S2_CNTL(pon),
				QPNP_PON_S2_CNTL_TYPE_MASK, QPNP_PON_S2_CNTL_DIS);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_KPDPWR_S2_CNTL(pon), rc);
		pwkpwr_enable = false;
	}
        if (val == 101) {
		pr_err("QPNP_PON_RESET_S2_CNTL2(pon) addr:%x", QPNP_PON_RESET_S2_CNTL2(pon));
		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S2_CNTL2(pon),
				QPNP_PON_S2_CNTL2_EN, QPNP_PON_S2_CNTL2_EN);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S2_CNTL2(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S2_CNTL(pon),
				QPNP_PON_S2_CNTL_TYPE_MASK, QPNP_PON_S2_CNTL_EN);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S2_CNTL(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S1_TIMER(pon),
				QPNP_PON_S1_TIMER_MASK, QPNP_PON_KPDPWR_S1_TIMER_TIME);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S1_TIMER(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S2_TIMER(pon),
				QPNP_PON_S2_TIMER_MASK, QPNP_PON_KPDPWR_S2_TIMER_TIME);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S2_TIMER(pon), rc);
		volup_enable = true;
	}
        if (val == 100) {
		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S2_CNTL2(pon),
				QPNP_PON_S2_CNTL2_EN, QPNP_PON_S2_CNTL2_DIS);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S2_CNTL2(pon), rc);

		rc = qpnp_pon_masked_write(pon, QPNP_PON_RESET_S2_CNTL(pon),
				QPNP_PON_S2_CNTL_TYPE_MASK, QPNP_PON_S2_CNTL_DIS);
		if (rc)
			dev_err(pon->dev,
					"Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_RESET_S2_CNTL(pon), rc);
		volup_enable = false;
	}
	return cnt;
}

static int kpdpwr_proc_show(struct seq_file *m, void *v)
{
	SEQ_printf(m, "%s\n%s\n", pwkpwr_enable ? "Long pwk dump enable\n" : "Long pwk dump disable\n",
	                       volup_enable ? "Long volup dump enable\n" : "Long volup dump disable\n");
	return 0;
}

static int kpdpwr_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, kpdpwr_proc_show, inode->i_private);
}

static struct file_operations kpdpwr_proc_fops = {
	.open 			= kpdpwr_proc_open,
	.read 			= seq_read,
	.write 			= kpdpwr_proc_write,
	.release		= single_release,
};


void kpdpwr_init(struct qpnp_pon *pon, bool sys_reset)
{
	struct proc_dir_entry *pe;
	if (!sys_reset)
		return;
	if (!pon){
		pr_err("%s: pon invalid!\n", __func__);
		return;
	}

	pe = proc_create("kpdpwr", 0664, NULL, &kpdpwr_proc_fops);
	if (!pe) {
		pr_err("kpdpwr:Failed to register kpdpwr interface\n");
		return;
	}
}
EXPORT_SYMBOL(kpdpwr_init);
