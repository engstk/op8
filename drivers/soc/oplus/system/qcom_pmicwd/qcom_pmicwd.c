// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** OPLUS_SYSTEM_QCOM_PMICWD
** File : qcom_pmicwd.c
** Description : qcom pmic watchdog driver
** Version : 1.0
******************************************************************/

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
#include <linux/module.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/qcom_scm.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <soc/oplus/system/qcom_pmicwd.h>
#else
#include <linux/regmap.h>
#include <linux/input/qpnp-power-on.h>
#endif

#define QPNP_PON_WD_RST_S1_TIMER(pon)		((pon)->base + 0x54)
#define QPNP_PON_WD_RST_S2_TIMER(pon)		((pon)->base + 0x55)
#define QPNP_PON_WD_RST_S2_CTL(pon)			((pon)->base + 0x56)
#define QPNP_PON_WD_RST_S2_CTL2(pon)		((pon)->base + 0x57)
#define QPNP_PON_WD_RESET_PET(pon)  		((pon)->base + 0x58)
#define QPNP_PON_RT_STS(pon)				((pon)->base + 0x10)

#define QPNP_PON_S2_CNTL_TYPE_MASK		(0xF)
#define QPNP_PON_WD_S2_TIMER_MASK		(0x7F)
#define QPNP_PON_WD_S1_TIMER_MASK		(0x7F)
#define QPNP_PON_WD_RESET_PET_MASK		BIT(0)

#define PMIC_WD_DEFAULT_TIMEOUT 254
#define PMIC_WD_DEFAULT_ENABLE 0

#define  OPLUS_KE_PROC_ENTRY(name, entry, mode)\
	({if (!proc_create(#name, S_IFREG | mode, oplus_ke_proc_dir, \
		&proc_##entry##_fops)){ \
		pr_info("proc_create %s failed\n", #name);}})

#define OPLUS_KE_FILE_OPS(entry) \
	static const struct file_operations proc_##entry##_fops = { \
		.read = proc_##entry##_read, \
		.write = proc_##entry##_write, \
	}

static struct proc_dir_entry *oplus_ke_proc_dir;

static ssize_t proc_force_shutdown_read(struct file *file,
				char __user *buf, size_t size, loff_t *ppos)
{
	return 0;
}

static ssize_t proc_force_shutdown_write(struct file *file,
			const char __user *buf, size_t size, loff_t *ppos)
{
	qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	qcom_scm_deassert_ps_hold();
	#else
	if (pm_power_off)
		pm_power_off();
	#endif

	return 0;
}

OPLUS_KE_FILE_OPS(force_shutdown);

const struct dev_pm_ops qpnp_pm_ops;
struct qpnp_pon *sys_reset_dev;
EXPORT_SYMBOL(sys_reset_dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#define PON_GEN3_PBS                            0x08
#define PON_GEN3_HLOS                           0x09
#define QPNP_PON_WD_EN                          BIT(7)

static bool is_pon_gen3(struct qpnp_pon *pon)
{
        return pon->subtype == PON_GEN3_PBS ||
                pon->subtype == PON_GEN3_HLOS;
}

static int oplus_qpnp_pon_wd_config(bool enable)
{
        if (!sys_reset_dev)
                return -EPROBE_DEFER;

        if (is_pon_gen3(sys_reset_dev))
                return -EPERM;

        return qpnp_pon_masked_write(sys_reset_dev,
                                QPNP_PON_WD_RST_S2_CTL2(sys_reset_dev),
                                QPNP_PON_WD_EN, enable ? QPNP_PON_WD_EN : 0);
}
#else
static int oplus_qpnp_pon_wd_config(bool enable)
{
        return qpnp_pon_wd_config(enable);
}
#endif

int qpnp_pon_wd_timer(unsigned char timer, enum pon_power_off_type reset_type)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc = 0;
	u8 s1_timer,s2_timer;

	if (!pon)
		return -EPROBE_DEFER;

	if(timer > 127)
	{
		s2_timer = 127;
		if(timer - 127 > 127)
			s1_timer = 127;
		else
			s1_timer = timer - 127;
	}else{
		s2_timer = timer&0xff;
		s1_timer = 0;
	}
	rc = qpnp_pon_masked_write(pon, QPNP_PON_WD_RST_S2_TIMER(pon),
			QPNP_PON_WD_S2_TIMER_MASK, s2_timer);
	if (rc)
		dev_err(pon->dev,
				"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_WD_RST_S2_TIMER(pon), rc);

	rc = qpnp_pon_masked_write(pon, QPNP_PON_WD_RST_S1_TIMER(pon),
			QPNP_PON_WD_S1_TIMER_MASK, s1_timer);
	if (rc)
		dev_err(pon->dev,
				"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_WD_RST_S1_TIMER(pon), rc);

	rc = qpnp_pon_masked_write(pon, QPNP_PON_WD_RST_S2_CTL(pon),
			QPNP_PON_S2_CNTL_TYPE_MASK, reset_type);
	if (rc)
		dev_err(pon->dev,
				"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_WD_RST_S2_CTL(pon), rc);

	return rc;
}

int qpnp_pon_wd_pet(void)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc = 0;

	if (!pon)
		return -EPROBE_DEFER;

	rc = qpnp_pon_masked_write(pon, QPNP_PON_WD_RESET_PET(pon),
			QPNP_PON_WD_RESET_PET_MASK, 1);
	if (rc)
		dev_err(pon->dev,
				"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_WD_RESET_PET(pon), rc);

	return rc;
}

static int pmicwd_kthread(void *arg)
{
	struct qpnp_pon *pon = (struct qpnp_pon *)arg;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO-1};
	sched_setscheduler(current, SCHED_FIFO, &param);

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(msecs_to_jiffies((((pon->pmicwd_state >> 8)&0xff)*1000)/2));
		dev_info(pon->dev, "pmicwd_kthread PET wd suspend state %d\n", pon->suspend_state);
		qpnp_pon_wd_pet();

		if((pon->suspend_state & 0x0F) >= 1)
			panic("suspend resume state %d\n", pon->suspend_state);
		else if(pon->suspend_state & 0xF0)
			pon->suspend_state ++;

	}
	oplus_qpnp_pon_wd_config(0);
	return 0;
}
static ssize_t pmicwd_proc_read(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	struct qpnp_pon *pon = sys_reset_dev;
	unsigned int val;
	char page[128] = {0};
	int len = 0;

	if(!pon){
		return -EFAULT;
	}
	mutex_lock(&pon->wd_task_mutex);
	regmap_read(pon->regmap, QPNP_PON_WD_RST_S2_CTL2(pon), &val);
	dev_info(pon->dev, "pmicwd_proc_read:%x wd=%x\n",pon->pmicwd_state,val);
	//|reserver|rst type|timeout|enable|
	len = snprintf(&page[len],128 - len,"enable = %d timeout = %d rstype = %d\n",
		pon->pmicwd_state & 0xff,(pon->pmicwd_state >> 8) & 0xff,(pon->pmicwd_state >> 16) & 0xff);
	mutex_unlock(&pon->wd_task_mutex);

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static ssize_t pmicwd_proc_write(struct file *file, const char __user *buf,
		size_t count,loff_t *off)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int tmp_rstypt = 0;
	int tmp_timeout = 0;
	int tmp_enable = 0;
	int ret = 0;
    char buffer[64] = {0};
	unsigned int new_state;

	if(!pon){
		return -EFAULT;
	}

    if (count > 64) {
       count = 64;
    }

    if (copy_from_user(buffer, buf, count)) {
		dev_err(pon->dev, "%s: read proc input error.\n", __func__);
		return count;
    }
	ret = sscanf(buffer, "%d %d %d", &tmp_enable, &tmp_timeout, &tmp_rstypt);
	if(ret <= 0){
		dev_err(pon->dev, "%s: input error\n", __func__);
		return count;
	}
	if(tmp_timeout < 60 || tmp_timeout > 255){
		tmp_timeout = PMIC_WD_DEFAULT_TIMEOUT;
	}
	if(tmp_rstypt >= PON_POWER_OFF_MAX_TYPE || tmp_rstypt <= PON_POWER_OFF_RESERVED){
		if (get_eng_version() == AGING) {
			tmp_rstypt = PON_POWER_OFF_WARM_RESET;
		} else {
			tmp_rstypt = PON_POWER_OFF_HARD_RESET;
		}
	}
	new_state = (tmp_enable & 0xff)|((tmp_timeout & 0xff) << 8)|((tmp_rstypt & 0xff)<< 16);
	dev_info(pon->dev, "pmicwd_proc_write:old:%x new:%x\n",pon->pmicwd_state,new_state);

	if(new_state == pon->pmicwd_state)
		return count;

	mutex_lock(&pon->wd_task_mutex);
	if(pon->wd_task){
		oplus_qpnp_pon_wd_config(0);
		pon->pmicwd_state &= ~0xff;
		kthread_stop(pon->wd_task);
		pon->wd_task = NULL;
	}
	qpnp_pon_wd_timer(tmp_timeout,tmp_rstypt);
	pon->pmicwd_state = new_state;
	if(tmp_enable){
		pon->wd_task = kthread_create(pmicwd_kthread, pon,"pmicwd");
		if(pon->wd_task){
			oplus_qpnp_pon_wd_config(1);
			wake_up_process(pon->wd_task);
		}else{
			oplus_qpnp_pon_wd_config(0);
			pon->pmicwd_state &= ~0xff;
		}
	}
	qpnp_pon_wd_pet();
	mutex_unlock(&pon->wd_task_mutex);
	return count;
}

void set_pmicWd_state(int enable)
{
	struct qpnp_pon *pon = sys_reset_dev;
	unsigned int new_state;

	new_state = (pon->pmicwd_state & ~0x1) | (enable & 0x1);

	dev_info(pon->dev, "set_pmicWd_state:old:%x new:%x\n", pon->pmicwd_state, new_state);
	if(new_state == pon->pmicwd_state)
		return ;

	mutex_lock(&pon->wd_task_mutex);
	if(pon->wd_task){
		oplus_qpnp_pon_wd_config(0);
		pon->pmicwd_state &= ~0xff;
		kthread_stop(pon->wd_task);
		pon->wd_task = NULL;
	}
	pon->pmicwd_state = new_state;
	if(enable){
		pon->wd_task = kthread_create(pmicwd_kthread, pon,"pmicwd");
		if(pon->wd_task){
			oplus_qpnp_pon_wd_config(1);
			wake_up_process(pon->wd_task);
		}else{
			oplus_qpnp_pon_wd_config(0);
			pon->pmicwd_state &= ~0xff;
		}
	}
	qpnp_pon_wd_pet();
	mutex_unlock(&pon->wd_task_mutex);
}
EXPORT_SYMBOL(set_pmicWd_state);

/*
 * This function is register as callback function to get notifications
 * from the PM module on the system suspend state.
 */
static int pmicWd_pm_notifier(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	struct qpnp_pon *pon = sys_reset_dev;
	switch (event) {
	case PM_SUSPEND_PREPARE:
		pon->suspend_state = 0x80 ;
		pr_info("pmicwd start suspend\n");
		break;

	case PM_POST_SUSPEND:
		pon->suspend_state = 0;
		pr_info("pmicwd finish resume\n");
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block pmicWd_pm_nb = {
	.notifier_call = pmicWd_pm_notifier,
	.priority = INT_MAX,
};

static struct file_operations pmicwd_proc_fops = {
	.read = pmicwd_proc_read,
	.write = pmicwd_proc_write,
};

#if defined(CONFIG_DEBUG_FS)
static const struct file_operations pmicwd_config_fops = {
	.open = simple_open,
	.read = pmicwd_proc_read,
	.write = pmicwd_proc_write,
};

static bool pmicwd_test_flag =false;
static int pmicwd_test_get(void *data, u64 *val)
{
	struct qpnp_pon *pon = data;
	*val = (u64)pmicwd_test_flag;
	dev_info(pon->dev, "%s: pmicwd test flag: %d\n", __func__, pmicwd_test_flag);
	return 0;
}

static int pmicwd_test_set(void *data, u64 val)
{
	struct qpnp_pon *pon = data;

	mutex_lock(&pon->wd_task_mutex);
	if(val){
		if(pmicwd_test_flag){
			dev_info(pon->dev, "%s: pmicwd test already started!\n", __func__);
			mutex_unlock(&pon->wd_task_mutex);
			return 0;
		}
		/*stop wd_task, and keep wd enbled, cause wd timeout!*/
		if(pon->wd_task){
			kthread_stop(pon->wd_task);
			oplus_qpnp_pon_wd_config(1);  // wd will be disabled in wd_task stop flow, so re-enabled 
			pon->wd_task = NULL;
			dev_info(pon->dev, "%s: pmicwd test start: after %d seconds reboot or enter RAMDUMP! \n", __func__, (pon->pmicwd_state >> 8)&0xFF);
		}else{
			dev_info(pon->dev, "%s: pmicwd test start: wd task not run, ignore! \n", __func__);
		}
		pmicwd_test_flag = true;
	}else{
		dev_info(pon->dev, "%s: pmicwd test exit!\n", __func__);
		if(!pon->wd_task){
			pon->wd_task = kthread_create(pmicwd_kthread, pon,"pmicwd");
			if(pon->wd_task){
				oplus_qpnp_pon_wd_config(1);
				wake_up_process(pon->wd_task);
			}
			qpnp_pon_wd_pet();
		}
		pmicwd_test_flag = false;
	}
	mutex_unlock(&pon->wd_task_mutex);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmicwd_test_fops,
			pmicwd_test_get,
			pmicwd_test_set,
			"%llu\n");

static void pmicwd_debugfs_init(struct qpnp_pon *pon)
{
	 struct dentry *file;

	if (!pon->debugfs) {
		dev_err(pon->dev, "%s: debugfs directory invalid!\n", __func__);
		return;
	}

	file = debugfs_create_file("pmicwd_config", 0666,
			    pon->debugfs, NULL, &pmicwd_config_fops);
	if (IS_ERR_OR_NULL(file))
		dev_err(pon->dev, "%s: Couldn't create pmicwd_config debugfs file\n", __func__);

	file = debugfs_create_file("pmicwd_test", 0666,
			    pon->debugfs, pon, &pmicwd_test_fops);
	if (IS_ERR_OR_NULL(file))
		dev_err(pon->dev, "%s: Couldn't create pmicwd_test debugfs file\n", __func__);

	return;
}

#else

static void pmicwd_debugfs_init(struct qpnp_pon *pon)
{}
#endif

void pmicwd_init(struct platform_device *pdev, struct qpnp_pon *pon, bool sys_reset)
{
	u32 pon_rt_sts = 0;
	int rc;

	oplus_ke_proc_dir = proc_mkdir("oplus_ke", NULL);
	if (oplus_ke_proc_dir == NULL) {
		pr_info("oplus_ke proc_mkdir failed\n");
	}

	OPLUS_KE_PROC_ENTRY(force_shutdown, force_shutdown, 0600);

	if (!pon){
		return;
	}
	if (!sys_reset)
		return;
	pon->pmicwd_state = of_property_read_bool(pdev->dev.of_node,"qcom,pmicwd");
	pon->wd_task = NULL;
	pon->suspend_state = 0;

	dev_info(pon->dev, "%s: sys_reset = 0x%x, pon pmicwd_state = 0x%x\n", __func__, sys_reset, pon->pmicwd_state);
	if(sys_reset && pon->pmicwd_state){
		if (get_eng_version() == AGING) {
			pon->pmicwd_state = PMIC_WD_DEFAULT_ENABLE | (PMIC_WD_DEFAULT_TIMEOUT << 8) |
				(PON_POWER_OFF_WARM_RESET << 16);
		} else {
			pon->pmicwd_state = PMIC_WD_DEFAULT_ENABLE | (PMIC_WD_DEFAULT_TIMEOUT << 8) |
				(PON_POWER_OFF_HARD_RESET << 16);
		}
		proc_create("pmicWd", 0666, NULL, &pmicwd_proc_fops);
		mutex_init(&pon->wd_task_mutex);
		#if PMIC_WD_DEFAULT_ENABLE
		pon->wd_task = kthread_create(pmicwd_kthread, pon,"pmicwd");
		if(pon->wd_task){
			if (get_eng_version() == AGING) {
				qpnp_pon_wd_timer(PMIC_WD_DEFAULT_TIMEOUT, PON_POWER_OFF_WARM_RESET);
			} else {
				qpnp_pon_wd_timer(PMIC_WD_DEFAULT_TIMEOUT, PON_POWER_OFF_HARD_RESET);
			}
			oplus_qpnp_pon_wd_config(1);
			wake_up_process(pon->wd_task);
		}else{
			pon->pmicwd_state &= ~0xff;
		}
		#endif

		rc = register_pm_notifier(&pmicWd_pm_nb);
		if (rc) {
			dev_err(pon->dev, "%s: pmicWd power state notif error %d\n", __func__, rc);
		}
	}

	regmap_read(pon->regmap, QPNP_PON_RT_STS(pon), &pon_rt_sts);
	dev_info(pon->dev, "probe keycode = 116, key_st = 0x%x\n", pon_rt_sts);

	pmicwd_debugfs_init(pon);

	return;
}
EXPORT_SYMBOL(pmicwd_init);

static int  setalarm(unsigned long time,bool enable)
{
	struct qpnp_pon *pon = sys_reset_dev;
	static struct rtc_device *rtc;
	static struct rtc_wkalrm alm;
	static struct rtc_wkalrm org_alm;
	unsigned long now;
	int rc = -1;
	static bool store_alm_success = false;

	if(!rtc){
		rtc = rtc_class_open("rtc0");
	}

	if(!rtc){
		dev_err(pon->dev, "open rtc fail %d\n",rc);
		return rc;
	}

	if(enable){
		rc = rtc_read_alarm(rtc, &org_alm);
		if (rc < 0) {
			dev_err(pon->dev, "setalarm read alarm fail %d\n",rc);
			store_alm_success = false;
			return rc;
		}
		store_alm_success = true;
		rc = rtc_read_time(rtc, &alm.time);
		if (rc < 0) {
			dev_err(pon->dev, "setalarm read time fail %d\n",rc);
			return rc;
		}

		rtc_tm_to_time(&alm.time, &now);
		memset(&alm, 0, sizeof alm);
		rtc_time_to_tm(now + time, &alm.time);
		alm.enabled = true;
		rc = rtc_set_alarm(rtc, &alm);
		if (rc < 0) {
			dev_err(pon->dev, "setalarm  set alarm fail %d\n",rc);
			return rc;
		}
	} else if (store_alm_success) {
		alm.enabled = false;
		rc = rtc_set_alarm(rtc, &alm);
		if (rc < 0) {
			dev_err(pon->dev, "setalarm  set alarm fail %d\n",rc);
			return rc;
		}
	    /* consider setting timer and orginal timer. we store orginal timer at pon suspend,
           and reset rtc from store at pon resume, no matter which one is greater. bottom
           driver would judge write to RTC or not. */
		rc = rtc_set_alarm(rtc, &org_alm);
		if (rc < 0) {
			dev_err(pon->dev, "setalarm  set org alarm fail %d\n",rc);
			return rc;
		}
	} else {
		dev_info(pon->dev, "%s store_alm_success:%d\n", __func__, store_alm_success);
	}
	return 0;
}
static int qpnp_suspend(struct device *dev)
{
	struct qpnp_pon *pon =
			(struct qpnp_pon *)dev_get_drvdata(dev);
	unsigned long time = 0;

	if(sys_reset_dev == NULL || sys_reset_dev != pon){
		return 0;
	}
	if(!(pon->pmicwd_state & 0xff))
	{
		dev_err(pon->dev, "%s:qpnp_suspend disable wd\n",dev_name(dev));
		return 0;
	}
	pon->suspend_state = 0;
	time = (pon->pmicwd_state >> 8)&0xff;
	dev_info(pon->dev, "%s:qpnp_suspend wd has enable\n",dev_name(dev));
	qpnp_pon_wd_pet();
	setalarm(time - 30,true);
	return 0;
}

static int qpnp_resume(struct device *dev)
{
	struct qpnp_pon *pon =
			(struct qpnp_pon *)dev_get_drvdata(dev);


	if(sys_reset_dev == NULL || sys_reset_dev != pon
		|| !(pon->pmicwd_state & 0xff)){
		return 0;
	}
	pon->suspend_state = 0x70;
	dev_info(pon->dev, "%s:qpnp_resume wd has enable\n",dev_name(dev));
	//disable alarm
	setalarm(0,false);
	qpnp_pon_wd_pet();
	return 0;
}

static int qpnp_poweroff(struct device *dev)
{
	struct qpnp_pon *pon =
			(struct qpnp_pon *)dev_get_drvdata(dev);
	dev_info(pon->dev, "qpnp_poweroff is call\n");
	if(sys_reset_dev == NULL || sys_reset_dev != pon)
		return 0;
	qpnp_pon_wd_pet();
	oplus_qpnp_pon_wd_config(0);
	return 0;
}

const struct dev_pm_ops qpnp_pm_ops = {
	.suspend = qpnp_suspend,
	.resume = qpnp_resume,
	.poweroff = qpnp_poweroff,
};
EXPORT_SYMBOL(qpnp_pm_ops);

MODULE_LICENSE("GPL v2");
