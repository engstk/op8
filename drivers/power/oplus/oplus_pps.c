// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/list.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/usb/typec.h>
#include <linux/usb/usbpd.h>
#endif
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/rtc.h>

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/gpio.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#include <uapi/linux/sched/types.h>
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#else
#include <uapi/linux/sched/types.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/msm_bcl.h>
#endif
//#include <linux/ktime.h>
//#include <linux/kernel.h>
#endif

#include "oplus_charger.h"
#include "oplus_pps.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "charger_ic/oplus_short_ic.h"

#ifdef CONFIG_OPLUS_EMMC_LOG
#include <soc/oplus/oplus_emmclog.h>
#endif /*CONFIG_OPLUS_EMMC_LOG*/

#ifndef CONFIG_OPLUS_CHARGER_MTK
extern int oplus_usbpd_send_svdm(u16 svid, u8 cmd,
                                 enum usbpd_svdm_cmd_type cmd_type, int obj_pos,
                                 const u32 *vdos, int num_vdos);
#endif
extern int oplus_pps_get_src_cap(void);
extern void oplus_oplus_pps_start_reset(void);

static void oplus_pps_stop_work(struct work_struct *work);
static void oplus_pps_update_work(struct work_struct *work);
static int oplus_pps_psy_changed(struct notifier_block *nb, unsigned long evt,
                           void *ptr);
static int oplus_pps_get_vcp(struct oplus_pps_chip *chip);
int __attribute__((weak)) oplus_chg_set_pps_config(int vbus_mv, int ibus_ma)
{
	return 0;
}
u32 __attribute__((weak))  oplus_chg_get_pps_status(void)
{
	return 0;
}
int __attribute__((weak)) oplus_chg_pps_get_max_cur(int vbus_mv)
{
	return 0;
}
int __attribute__((weak)) oplus_chg_set_pd_5v(void)
{
	return 0;
}


static struct oplus_pps_chip g_pps_chip;

static const char * const strategy_soc[] = {
	[BATT_CURVE_SOC_0_TO_50]	= "strategy_soc_0_to_50",
	[BATT_CURVE_SOC_50_TO_75]	= "strategy_soc_50_to_75",
	[BATT_CURVE_SOC_75_TO_85]	= "strategy_soc_75_to_85",
	[BATT_CURVE_SOC_85_TO_90]	= "strategy_soc_85_to_90",
};

static const char * const strategy_temp[] = {
	[BATT_CURVE_TEMP_0_TO_50]	= "strategy_temp_0_to_50",
	[BATT_CURVE_TEMP_50_TO_120]	= "strategy_temp_50_to_120",
	[BATT_CURVE_TEMP_120_TO_160]	= "strategy_temp_120_to_160",
	[BATT_CURVE_TEMP_160_TO_430]	= "strategy_temp_160_to_430",
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct timespec current_kernel_time(void)
{
	struct timespec ts;
	getnstimeofday(&ts);
    return ts;
}
#endif

static int oplus_pps_parse_batt_curves(struct oplus_pps_chip *chip)
{
	struct device_node *node, *pps_node, *soc_node;
	int rc = 0, i, j, k, length;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	node = chip->dev->of_node;

	pps_node = of_get_child_by_name(node, "pps_charge_strategy");
	if (!pps_node) {
		pr_err("Can not find pps_charge_strategy node\n");
		return -EINVAL;
	}

	for (i=0; i<BATT_CURVE_SOC_MAX; i++) {
		soc_node = of_get_child_by_name(pps_node, strategy_soc[i]);
		if (!soc_node) {
			pr_err("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j=0; j<BATT_CURVE_TEMP_MAX; j++) {
			rc = of_property_count_elems_of_size(soc_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count %s failed, rc=%d\n", strategy_temp[j], rc);
				return rc;
			}

			length = rc;

			switch(i) {
			case BATT_CURVE_SOC_0_TO_50:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
						(u32 *)chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_50_TO_75:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
						(u32 *)chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_75_TO_85:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
						(u32 *)chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_85_TO_90:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
						(u32 *)chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			default:
				break;
			}
		}
	}

	for (i=0; i< BATT_CURVE_SOC_MAX; i++) {
		for (j=0; j<BATT_CURVE_TEMP_MAX; j++) {
			for (k=0; k<chip->batt_curves_soc[i].batt_curves_temp[j].batt_curve_num; k++) {
				pr_err("%s: %d %d %d %d %d\n", __func__,
					chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves[k].target_vbus,
					chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves[k].target_vbat,
					chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves[k].target_ibus,
					chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves[k].exit,
					chip->batt_curves_soc[i].batt_curves_temp[j].batt_curves[k].target_time);
			}
		}
	}

	return rc;
}

int oplus_pps_parse_dt(struct oplus_pps_chip *chip)
{
	int rc;
	struct device_node *node;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	node = chip->dev->of_node;
	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(node, "qcom,pps_support_type",
	                          &chip->pps_support_type);
	if (rc || chip->pps_support_type == 0) {
		chip->pps_support_type = 0;
		return -ENODEV;
	} else {
		chg_debug("qcom,pps_support_type is %d\n", chip->pps_support_type);
	}

	rc = of_property_read_u32(node, "qcom,pps-low-temp",
	                          &chip->pps_low_temp);
	if (rc) {
		chip->pps_low_temp = 120;
	} else {
		chg_debug("qcom,pps-low-temp is %d\n", chip->pps_low_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-little-low-temp",
	                          &chip->pps_little_low_temp);
	if (rc) {
		chip->pps_little_low_temp = 160;
	} else {
		chg_debug("qcom,pps-low-temp is %d\n", chip->pps_low_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-little-cool-temp",
	                          &chip->pps_little_cool_temp);
	if (rc) {
		chip->pps_little_cool_temp = 160;
	} else {
		chg_debug("qcom,pps-little-cool-temp is %d\n",
		          chip->pps_little_cool_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-cool-temp",
	                          &chip->pps_cool_temp);
	if (rc) {
		chip->pps_cool_temp = 120;
	} else {
		chg_debug("qcom,pps-cool-temp is %d\n", chip->pps_cool_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-little-cool-to-normal-temp",
	                          &chip->pps_little_cool_to_normal_temp);
	if (rc) {
		chip->pps_little_cool_to_normal_temp = 160;
	} else {
		chg_debug("qcom,pps-little-cool-to-normal-temp is %d\n",
		          chip->pps_little_cool_to_normal_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-normal-to-little-cool-power",
	                          &chip->pps_normal_to_little_cool_power);
	if (rc) {
		chip->pps_normal_to_little_cool_power = 100;
	} else {
		chg_debug("qcom,pps-normal-to-little-cool-power is %d\n",
		          chip->pps_normal_to_little_cool_power);
	}

	rc = of_property_read_u32(node, "qcom,pps-high-temp",
	                          &chip->pps_high_temp);
	if (rc) {
		chip->pps_high_temp = 430;
	} else {
		chg_debug("qcom,pps-high-temp is %d\n", chip->pps_high_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-little-high-temp ",
	                          &chip->pps_high_temp);
	if (rc) {
		chip->pps_little_high_temp = 350;
	} else {
		chg_debug("qcom,pps-high-temp is %d\n",
		          chip->pps_little_high_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps-low-soc",
	                          &chip->pps_low_soc);
	if (rc) {
		chip->pps_low_soc = 1;
	} else {
		chg_debug("qcom,pps-low-soc is %d\n", chip->pps_low_soc);
	}

	rc = of_property_read_u32(node, "qcom,pps-high-soc",
	                          &chip->pps_high_soc);
	if (rc) {
		chip->pps_high_soc = 85;
	} else {
		chg_debug("qcom,pps-high-soc is %d\n", chip->pps_high_soc);
	}

	rc = of_property_read_u32(node, "qcom,pps_multistep_initial_batt_temp",
	                          &chip->pps_multistep_initial_batt_temp);
	if (rc) {
		chip->pps_multistep_initial_batt_temp = 305;
	} else {
		chg_debug("qcom,pps_multistep_initial_batt_temp is %d\n",
		          chip->pps_multistep_initial_batt_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy_normal_power",
	                          &chip->pps_strategy_normal_power);
	if (rc) {
		chip->pps_strategy_normal_power = 0x03;
	} else {
		chg_debug("qcom,pps_strategy_normal_power is %d\n",
		          chip->pps_strategy_normal_power);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_low_temp1",
	                          &chip->pps_strategy1_batt_low_temp1);
	if (rc) {
		chip->pps_strategy1_batt_low_temp1 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_low_temp1 is %d\n",
		          chip->pps_strategy1_batt_low_temp1);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_low_temp2",
	                          &chip->pps_strategy1_batt_low_temp2);
	if (rc) {
		chip->pps_strategy1_batt_low_temp2 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_low_temp2 is %d\n",
		          chip->pps_strategy1_batt_low_temp2);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_low_temp0",
	                          &chip->pps_strategy1_batt_low_temp0);
	if (rc) {
		chip->pps_strategy1_batt_low_temp0 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_low_temp0 is %d\n",
		          chip->pps_strategy1_batt_low_temp0);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_high_temp0",
	                          &chip->pps_strategy1_batt_high_temp0);
	if (rc) {
		chip->pps_strategy1_batt_high_temp0 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_high_temp0 is %d\n",
		          chip->pps_strategy1_batt_high_temp0);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_high_temp1",
	                          &chip->pps_strategy1_batt_high_temp1);
	if (rc) {
		chip->pps_strategy1_batt_high_temp1 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_high_temp1 is %d\n",
		          chip->pps_strategy1_batt_high_temp1);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_batt_high_temp2",
	                          &chip->pps_strategy1_batt_high_temp2);
	if (rc) {
		chip->pps_strategy1_batt_high_temp2 =
		    chip->pps_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,pps_strategy1_batt_high_temp2 is %d\n",
		          chip->pps_strategy1_batt_high_temp2);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_high_power0",
	                          &chip->pps_strategy1_high_power0);
	if (rc) {
		chip->pps_strategy1_high_power0 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_high_power0 is %d\n",
		          chip->pps_strategy1_high_power0);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_high_power1",
	                          &chip->pps_strategy1_high_power1);
	if (rc) {
		chip->pps_strategy1_high_power1 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_high_power1 is %d\n",
		          chip->pps_strategy1_high_power1);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_high_power2",
	                          &chip->pps_strategy1_high_power2);
	if (rc) {
		chip->pps_strategy1_high_power2 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_high_power2 is %d\n",
		          chip->pps_strategy1_high_power2);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_low_power2",
	                          &chip->pps_strategy1_low_power2);
	if (rc) {
		chip->pps_strategy1_low_power2 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_low_power2 is %d\n",
		          chip->pps_strategy1_low_power2);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_low_power1",
	                          &chip->pps_strategy1_low_power1);
	if (rc) {
		chip->pps_strategy1_low_power1 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_low_power1 is %d\n",
		          chip->pps_strategy1_low_power1);
	}

	rc = of_property_read_u32(node, "qcom,pps_strategy1_low_power0",
	                          &chip->pps_strategy1_low_power0);
	if (rc) {
		chip->pps_strategy1_low_power0 =
		    chip->pps_strategy_normal_power;
	} else {
		chg_debug("qcom,pps_strategy1_low_power0 is %d\n",
		          chip->pps_strategy1_low_power0);
	}

	rc = of_property_read_u32(node, "qcom,pps_batt_over_high_temp",
	                          &chip->pps_batt_over_high_temp);
	if (rc) {
		chip->pps_batt_over_high_temp = 440;
	} else {
		chg_debug("qcom,pps_batt_over_high_temp is %d\n",
		          chip->pps_batt_over_high_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps_batt_over_low_temp",
	                          &chip->pps_batt_over_low_temp);
	if (rc) {
		chip->pps_batt_over_low_temp = 50;
	} else {
		chg_debug("qcom,pps_batt_over_low_temp is %d\n",
		          chip->pps_batt_over_low_temp);
	}

	rc = of_property_read_u32(node, "qcom,pps_over_high_or_low_power",
	                          &chip->pps_over_high_or_low_power);
	if (rc) {
		chip->pps_over_high_or_low_power = -EINVAL;
	} else {
		chg_debug("qcom,pps_over_high_or_low_power is %d\n",
		          chip->pps_over_high_or_low_power);
	}

	oplus_pps_parse_batt_curves(chip);

	return 0;
}

int oplus_pps_get_curve_vbus(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbus;
}

int oplus_pps_get_curve_ibus(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
}

int oplus_pps_get_curve_time(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_time;
}

int oplus_pps_get_curve_vbat(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbat;
}

int oplus_pps_get_next_curve_vbus(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index+1].target_vbus;
}

int oplus_pps_get_next_curve_ibus(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index+1].target_ibus;
}

static int oplus_pps_get_adapter_status(struct oplus_pps_chip *chip)
{
	u32 pps_status;
	int volt, cur;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	pps_status = chip->get_pps_status();
	if (pps_status < 0) {
		chg_err("pps get pdo status fail\n");
		return -EINVAL;
	}

	if (PD_PPS_STATUS_VOLT(pps_status) == 0xFFFF
			|| PD_PPS_STATUS_CUR(pps_status) == 0xFF) {
		chg_err("get adapter pps status fail\n");
		return -EINVAL;
	}

	volt = PD_PPS_STATUS_VOLT(pps_status) * 20;
	cur = PD_PPS_STATUS_CUR(pps_status) * 50;
	chg_debug("pps get volt:%d,cur:%d,pps_status:0x%x\n", volt, cur, pps_status);

	chip->charger_output_volt = volt;
	chip->charger_output_current = cur;

	return 0;
}

static int oplus_pps_get_status(struct oplus_pps_chip *chip)
{
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	oplus_chg_get_props_from_adsp_by_buffer();

	chip->ap_batt_soc = oplus_gauge_get_batt_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	chip->ap_batt_current = oplus_gauge_get_batt_current();
	chip->current_adapter_max = chip->get_pps_max_cur(oplus_pps_get_curve_vbus(chip));

	ret = chip->ops->get_input_volt();
	chip->ap_input_volt = ret & 0xffff;
	//chip->ap_system_current = chip->chg_ops->get_charger_current() / 1000;
	chip->ap_input_current = - chip->ap_batt_current;
	chg_err("batt volt= %d, batt current= %d, input volt= %d, input current= %d\n",
			chip->ap_batt_volt, chip->ap_batt_current,
			chip->ap_input_volt, chip->ap_input_current);

	if (chip->oplus_pps_status == OPLUS_10V_30W_STATUS) {
		chip->charger_output_volt = chip->ap_input_volt;
		chip->charger_output_current = chip->ap_input_current;
	} else {
		ret = oplus_pps_get_adapter_status(chip);
		if (ret < 0) {
			chip->charger_output_volt = chip->ap_input_volt;
			chip->charger_output_current = chip->ap_input_current;
		}
	}

	ret = oplus_pps_get_vcp(chip);
	if (ret < 0) {
		chg_err("pps get vbat0 failed\n");
		return -EINVAL;
	}

	return 0;
}

static int oplus_pps_choose_curves(struct oplus_pps_chip *chip)
{
	int i;
	int batt_soc_plugin, batt_temp_plugin;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->ap_batt_soc <= 50) {
		batt_soc_plugin = BATT_CURVE_SOC_0_TO_50;
	} else if (chip->ap_batt_soc <= 75) {
		batt_soc_plugin = BATT_CURVE_SOC_50_TO_75;
	} else if (chip->ap_batt_soc <= 85) {
		batt_soc_plugin = BATT_CURVE_SOC_75_TO_85;
	} else if (chip->ap_batt_soc <= 90) {
		batt_soc_plugin = BATT_CURVE_SOC_85_TO_90;
	} else {
		batt_soc_plugin = BATT_CURVE_SOC_MAX;
		chg_err("batt soc high, stop pps\n");
		return -EINVAL;
	}

	if (chip->ap_batt_temperature < 0) {
		chg_err("batt temp low, stop pps\n");
                return -EINVAL;
	} else if (chip->ap_batt_temperature < 50) {
		batt_temp_plugin = BATT_CURVE_TEMP_0_TO_50;
	} else if (chip->ap_batt_temperature < 120) {
		batt_temp_plugin = BATT_CURVE_TEMP_50_TO_120;
	} else if (chip->ap_batt_temperature < 160) {
		batt_temp_plugin = BATT_CURVE_TEMP_120_TO_160;
	} else if (chip->ap_batt_temperature < 430) {
		batt_temp_plugin = BATT_CURVE_TEMP_160_TO_430;
	} else {
		chg_err("batt temp high, stop pps\n");
		batt_temp_plugin = BATT_CURVE_TEMP_MAX;
		return -EINVAL;
	}

	memcpy(&chip->batt_curves,
	       chip->batt_curves_soc[batt_soc_plugin].batt_curves_temp[batt_temp_plugin].batt_curves, sizeof(struct batt_curves));

	pr_err("temperature: %d, soc: %d, batt_temp_plugin: %d, batt_temp_plugin: %d",
	       chip->ap_batt_temperature, chip->ap_batt_soc, batt_soc_plugin, batt_temp_plugin);

	for (i=0; i < chip->batt_curves.batt_curve_num; i++) {
		pr_err("%s: %d %d %d %d %d\n", __func__,
		       chip->batt_curves.batt_curves[i].target_vbus,
		       chip->batt_curves.batt_curves[i].target_vbat,
		       chip->batt_curves.batt_curves[i].target_ibus,
		       chip->batt_curves.batt_curves[i].exit,
		       chip->batt_curves.batt_curves[i].target_time);
	}

	chip->batt_curve_index = 0;
	while (chip->get_pps_max_cur(oplus_pps_get_curve_vbus(chip)) < oplus_pps_get_curve_ibus(chip)) {
		chip->batt_curve_index++;
	}

	chip->current_batt_curve
		= chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;

	return 0;
}

static int oplus_pps_variables_init(struct oplus_pps_chip *chip, int status)
{
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chg_err("status:%d\n", status);
	chip->oplus_pps_status = status;

	oplus_pps_get_status(chip);

	ret = oplus_pps_choose_curves(chip);
	if (ret < 0) {
		return ret;
	}

	chip->pps_status = OPLUS_PPS_STATUS_START;
	chip->curve_ov_count = 0;
	chip->curve_uc_count = 0;
	chip->fast3c_timeout_time = 0;
	chip->fg_batt_full = 0;
	chip->curr_over_count = 0;
	chip->fg_current_over = 0;
	chip->chg_curr_over_count = 0;
	chip->fg_chg_current_over = 0;
	chip->cable_over_count = 0;
	chip->fg_cable_over = 0;
	chip->temp_over_count = 0;
	chip->temp_over = 0;
	chip->pps_disconnect_count = 0;
	chip->pps_disconnect = 0;
	chip->pps_disconnect_volt = 0;
	chip->set_pdo_flag = 0;
	chip->set_vcp_flag = 0;
	chip->set_fastchg_flag = 0;
	chip->set_temp_flag = 0;
	chip->target_charger_volt = 0;
	chip->target_charger_current = 0;
	chip->target_charger_volt_pre = 0;
	chip->target_charger_current_pre = 0;
	chip->ask_charger_volt_last = 0;
	chip->ask_charger_current_last = 0;
	chip->ask_charger_volt = 5500;
	chip->ask_charger_current = 0;
	chip->fastchg_timer = current_kernel_time();
	chip->temp_timer = current_kernel_time();
	chip->vcp_timer = current_kernel_time();
	chip->pdo_timer = current_kernel_time();
	chip->fastchg_timeout_time = FASTCHG_TIMEOUT;
	chip->vcpout_volt = chip->ops->get_vbat0_volt();
	chip->current_batt_curve
		= chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;

	return 0;
}

static int oplus_pps_charging_enable(struct oplus_pps_chip *chip, bool on)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (on) {
		chip->ops->pps_mos_ctrl(1);
	} else {
		chip->ops->pps_mos_ctrl(0);
	}
	return 0;
}

#define COUNTS	2
static int oplus_pps_get_batt_temp_curr(
    struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	static int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	switch (chip->pps_fastchg_batt_temp_status) {
	case PPS_BAT_TEMP_NATURAL:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp0) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
			ret = chip->pps_strategy1_high_power0;
		} else if (vbat_temp_cur > chip->pps_little_cool_temp) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NATURAL;
			ret = chip->pps_strategy_normal_power;
		} else if (vbat_temp_cur > chip->pps_cool_temp) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
			ret = chip->pps_normal_to_little_cool_power;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
			ret = chip->pps_strategy_normal_power;
		}
		break;
	case PPS_BAT_TEMP_COOL:
		if (chip->pps_batt_over_low_temp != -EINVAL
		    && vbat_temp_cur < chip->pps_batt_over_low_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_OVER_LOW_EXIT;
				ret = chip->pps_over_high_or_low_power;
			}
		} else if (chip->pps_batt_over_low_temp != -EINVAL
		           && vbat_temp_cur > chip->pps_batt_over_high_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status =
				    PPS_BAT_TEMP_OVER_HIGH_EXIT;
				ret = chip->pps_over_high_or_low_power;
			}
		} else if (chip->pps_batt_over_low_temp != -EINVAL
		           && vbat_temp_cur > chip->pps_cool_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				oplus_pps_choose_curves(chip);
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
				ret = chip->pps_strategy_normal_power;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
			ret = chip->pps_strategy_normal_power;
		}
		break;
	case PPS_BAT_TEMP_LITTLE_COOL:
		if (vbat_temp_cur < chip->pps_cool_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				oplus_pps_choose_curves(chip);
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
				ret = chip->pps_strategy_normal_power;
			}
		} else if (vbat_temp_cur > chip->pps_little_cool_to_normal_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NATURAL;
				ret = chip->pps_strategy_normal_power;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
			ret = chip->pps_normal_to_little_cool_power;
		}
		break;
	case PPS_BAT_TEMP_HIGH0:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp1) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
				ret = chip->pps_strategy1_high_power1;
			}
		} else if (vbat_temp_cur < chip->pps_strategy1_batt_low_temp0) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
				ret = chip->pps_strategy1_low_power0;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
			ret = chip->pps_strategy1_high_power0;
		}
		break;
	case PPS_BAT_TEMP_HIGH1:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp2) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
				ret = chip->pps_strategy1_high_power2;
			}
		} else if (vbat_temp_cur < chip->pps_strategy1_batt_low_temp1) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
				ret = chip->pps_strategy1_low_power1;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
			ret = chip->pps_strategy1_high_power1;
		}
		break;
	case PPS_BAT_TEMP_HIGH2:
		if (chip->pps_batt_over_high_temp != -EINVAL
		    && vbat_temp_cur > chip->pps_batt_over_high_temp) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status =
				    PPS_BAT_TEMP_OVER_HIGH_EXIT;
				ret = chip->pps_over_high_or_low_power;
			}
		} else if (vbat_temp_cur < chip->pps_strategy1_batt_low_temp2) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW2;
				ret = chip->pps_strategy1_low_power2;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
			ret = chip->pps_strategy1_high_power2;
			;
		}
		break;
	case PPS_BAT_TEMP_LOW0:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp0) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
				ret = chip->pps_strategy1_high_power0;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
			ret = chip->pps_strategy1_low_power0;
		}
		break;
	case PPS_BAT_TEMP_LOW1:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp1) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
				ret = chip->pps_strategy1_high_power1;
			}
		} else if (vbat_temp_cur < chip->pps_strategy1_batt_low_temp0) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
				ret = chip->pps_strategy1_low_power0;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
			ret = chip->pps_strategy1_low_power1;
		}
		break;
	case PPS_BAT_TEMP_LOW2:
		if (vbat_temp_cur > chip->pps_strategy1_batt_high_temp2) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
				ret = chip->pps_strategy1_high_power2;
			}
		} else if (vbat_temp_cur < chip->pps_strategy1_batt_low_temp1) {
			chip->pps_strategy_change_count++;
			if (chip->pps_strategy_change_count >= COUNTS) {
				chip->pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
				ret = chip->pps_strategy1_low_power1;
			}
		} else {
			chip->pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW2;
			ret = chip->pps_strategy1_low_power2;
		}
		break;
	case PPS_BAT_TEMP_OVER_HIGH_EXIT:
		chip->pps_strategy_change_count = 0;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_OVER_HIGH_EXIT;
		ret = chip->pps_over_high_or_low_power;
		break;
	case PPS_BAT_TEMP_OVER_LOW_EXIT:
		chip->pps_strategy_change_count = 0;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_OVER_LOW_EXIT;
		ret = chip->pps_over_high_or_low_power;
		break;
	default:
		break;
	}
	
	chg_err("the ret: %d, the temp =%d, status = %d\r\n", ret, vbat_temp_cur,
	        chip->pps_fastchg_batt_temp_status);

	chip->current_batt_temp = ret;

	return ret;
}

static int oplus_pps_get_batt_curve_curr(struct oplus_pps_chip *chip)
{
	int curve_change = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->ap_batt_volt > oplus_pps_get_curve_vbat(chip)) {
		if (chip->curve_ov_count > 3) {
			curve_change = 1;
			chip->curve_ov_count = 0;
		} else {
			chip->curve_ov_count++;
		}
	} else {
		chip->curve_ov_count = 0;
	}

	if (chip->charger_output_current
			< (chip->ask_charger_current - 1000)) {
		if (chip->curve_uc_count > 3) {
			curve_change = 1;
			chip->curve_uc_count = 0;
		} else {
			chip->curve_uc_count++;
		}
	} else {
		chip->curve_uc_count = 0;
	}

	if (curve_change) {
		chip->batt_curve_index++;
		chip->current_batt_curve
				= chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
		chg_err("batt_curve_index = %d\n", chip->batt_curve_index);
		if (chip->batt_curve_index >= chip->batt_curves.batt_curve_num) {
			chip->fg_batt_full = 1;
		}
	}

	return 0;
}

static const int Cool_Down_Curve[] = {3000, 1000, 1500, 2000, 2500, 3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000};
static int oplus_pps_get_cool_down_curr(struct oplus_pps_chip *chip)
{
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->current_cool_down = Cool_Down_Curve[oplus_chg_get_cool_down_status()];

	return 0;
}

static int oplus_pps_get_target_current(struct oplus_pps_chip *chip)
{
	int target_current_temp = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chg_err("current_batt_curve= %d, current_batt_temp= %d, current_cool_down= %d\n",
			chip->current_batt_curve,
			chip->current_batt_temp,
			chip->current_cool_down);

	target_current_temp = chip->current_batt_curve < chip->current_batt_temp ? chip->current_batt_curve :chip->current_batt_temp;
	target_current_temp = target_current_temp < chip->current_cool_down ? target_current_temp :chip->current_cool_down;

	return target_current_temp;
}

static void oplus_pps_tick_timer(struct oplus_pps_chip *chip)
{
	struct timespec ts_current;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	ts_current = current_kernel_time();

	if ((ts_current.tv_sec - chip->fastchg_timer.tv_sec) >= update_fastchg_time) {
		chip->fastchg_timer = ts_current;
		//chip->set_fastchg_flag = 1;
		if (oplus_pps_get_curve_time(chip) > 0) {
			chip->fast3c_timeout_time++;
		}
		if (chip->fastchg_timeout_time > 0) {
			chip->fastchg_timeout_time--;
		}
	}

	if ((ts_current.tv_sec - chip->pdo_timer.tv_sec) >= update_pdo_time) {
		chip->pdo_timer = ts_current;
		chip->set_pdo_flag = 1;
	}

	if ((ts_current.tv_sec - chip->vcp_timer.tv_sec) >= update_vcp_time) {
		chip->vcp_timer = ts_current;
		chip->set_vcp_flag = 1;
	}

	if ((ts_current.tv_sec - chip->temp_timer.tv_sec) >= update_temp_time) {
		chip->temp_timer = ts_current;
		chip->set_temp_flag = 1;
	}
}

static int oplus_pps_check_temp(struct oplus_pps_chip *chip)
{
	struct oplus_temp_chip *oplus_temp;
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->set_temp_flag) {

		list_for_each_entry (oplus_temp, &chip->temp_list, temp_list) {
			ret = oplus_temp->get_temp(oplus_temp->dev);

			if (ret >= 85)
				return -EINVAL;
		}

		ret = chip->ops->check_btb_temp();
		if (ret < 0) {
			return -EINVAL;
		}

		if (chip->ap_batt_temperature > chip->pps_batt_over_high_temp ||
		    chip->ap_batt_temperature < chip->pps_batt_over_low_temp) {
			chg_err("pps battery temp out of range!!!\n");
			return -EINVAL;
		}

		chip->set_temp_flag = 0;
	}

	return 0;
}

static void oplus_pps_status_check(struct oplus_pps_chip *chip)
{
	uint16_t i_curr;
	int r1, r2;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	oplus_pps_tick_timer(chip);

	if (chip->fastchg_timeout_time == 0) {
		chip->fg_batt_full = 1;
	}

	if ((chip->ap_batt_volt > BAT_FULL_1TIME_THD_PPS)
			|| (chip->ap_batt_volt > BAT_FULL_2TIME_THD_PPS
			&& chip->ap_batt_soc > 80)) {
		chip->fg_batt_full = 1;
	}

	if (chip->ap_batt_current > 2000) {
		chip->curr_over_count++;
		if (chip->curr_over_count > 3) {
			chip->fg_current_over = 1;
		}
	} else {
		chip->curr_over_count = 0;
	}

	if (chip->ap_batt_current < -OVER_CURRENT_VALUE) {
		chip->chg_curr_over_count++;
		if (chip->chg_curr_over_count > 7) {
			chip->fg_chg_current_over = 1;
		}
	} else {
		chip->chg_curr_over_count = 0;
	}

	r1 = ((chip->charger_output_volt * 100 - 204 * chip->vcpout_volt)
	      / chip->charger_output_current) / 100;

	//vol_limit1 = (R1_LIMIT * chip->charger_output_current) + (206 * chip->vcpout_volt / 100);

	if (r1 > R1_LIMIT) {
		chip->r1_over_count++;
		if (chip->r1_over_count > 10) {
			chip->fg_r1_over = 1;
		}
	} else {
		chip->r1_over_count = 0;
	}

	//vol_limit2 = (R2_LIMIT * chip->ap_batt_current) + chip->ap_batt_volt;
	r2 = (chip->vcpout_volt - chip->ap_batt_volt * 2)
	     / (-chip->ap_batt_current);
	if (r2 > R2_LIMIT) {
		chip->r2_over_count++;
		if (chip->r2_over_count > 10) {
			chip->fg_r2_over = 1;
		}
	} else {
		chip->r2_over_count = 0;
	}
	chg_err("r1:%d,r2:%d\n", r1, r2);

	i_curr = chip->charger_output_current
	         - chip->ap_input_current;
	if (i_curr > CABLE_CURRENT_LIMIT) {
		chip->cable_over_count++;
		if (chip->cable_over_count > 10) {
			//chip->fg_cable_over = 1;
		}
	} else {
		chip->cable_over_count = 0;
	}

	if (oplus_pps_check_temp(chip) < 0) {
		chip->temp_over_count++;
		if (chip->temp_over_count > 3) {
			chip->temp_over = 1;
		}
	} else {
		chip->temp_over_count = 0;
	}

	if (chip->pps_status == OPLUS_PPS_STATUS_VOLT_CHANGE
		|| chip->pps_status == OPLUS_PPS_STATUS_CUR_INCREASE) {
		if (chip->ap_input_current < 0) {
			chg_err("pps_disconnect_count:%d\n", chip->pps_disconnect_count);
			if (chip->pps_disconnect_count >= 2) {
				chip->pps_disconnect = 1;
			}
			chip->pps_disconnect_count++;
		} else {
			chip->pps_disconnect_count = 0;
		}
	}

	if (chip->pps_status == OPLUS_PPS_STATUS_CUR_DECREASE
		|| chip->pps_status == OPLUS_PPS_STATUS_CHECK) {
		if (chip->ap_input_current > 0 && chip->ap_input_current < 200) {
			chg_err("pps_disconnect_count:%d\n", chip->pps_disconnect_count);
			if (chip->pps_disconnect_count >= 2) {
				chip->pps_disconnect = 1;
			}
			chip->pps_disconnect_count++;
		} else if (chip->ap_input_current < 0) {
			chg_err("pps_disconnect_current error\n");
			oplus_pps_stop();
		}
	}

}

static int oplus_pps_get_vcp(struct oplus_pps_chip *chip)
{
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->set_vcp_flag) {

		ret = chip->ops->get_vbat0_volt();
		if (ret < 0)
			return -EINVAL;
		chip->vcpout_volt = ret;
		chip->set_vcp_flag = 0;
	}
	return 0;
}

static int oplus_pps_action(struct oplus_pps_chip *chip)
{
	int update_size = 0;
	static int curr_increase_cnt = 0;
	//static int curr_decrease_cnt = 0;
	static int curr_over_target_cnt = 0;
	int ret;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->target_charger_volt_pre = chip->target_charger_volt;
	chip->target_charger_current_pre = chip->target_charger_current;

	switch (chip->pps_status) {
	case OPLUS_PPS_STATUS_START:
		oplus_chg_disable_charge();
		oplus_chg_suspend_charger();
		if (oplus_pps_get_curve_vbus(chip) == 20000) {
			chip->target_charger_volt = ((chip->ap_batt_volt * 4) / 100) * 100 + 200;
			chip->target_charger_current = 500;
		} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
			chip->target_charger_volt = ((chip->ap_batt_volt * 2) / 100) * 100 + 100;
			chip->target_charger_current = 1000;
		} else {
			chg_err("Invalid argument!\n");
			return -EINVAL;
		}

		if ((chip->ask_charger_volt - chip->target_charger_volt) > 1000
				|| (chip->target_charger_volt - chip->ask_charger_volt) > 1000) {
			update_size = 500;
		} else {
			update_size = 100;
		}

		chip->ask_charger_current = chip->target_charger_current;
		if (chip->ap_input_volt < chip->target_charger_volt) {
			if (chip->ask_charger_volt > chip->target_charger_volt + 500) {
				chg_err("adapter abnormal, not start pps\n");
				return -EINVAL;
			}
			chip->ask_charger_volt += update_size;
		} else if (chip->ap_input_volt > (chip->target_charger_volt + 300)) {
			if (chip->ask_charger_volt < chip->target_charger_volt - 500) {
				chg_err("adapter abnormal, not start pps\n");
				return -EINVAL;
			}
			chip->ask_charger_volt -= update_size;
		} else {
			chg_debug("pps chargering volt okay\n");
			chip->pps_status = OPLUS_PPS_STATUS_OPEN_OVP_CP;
		}

		 chip->work_delay = 300;
		 break;
	case OPLUS_PPS_STATUS_OPEN_OVP_CP:
		ret = oplus_pps_charging_enable(chip, true);
		if (ret < 0) {
			return ret;
		}

		if (oplus_pps_get_curve_vbus(chip) == 20000) {
			chip->target_charger_volt = ((chip->ap_batt_volt * 4) / 100) * 100 + 200;
			chip->target_charger_current = 500;
		} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
			chip->target_charger_volt = ((chip->ap_batt_volt * 2) / 100) * 100 + 100;
			chip->target_charger_current = 1000;
		} else {
			chg_err("Invalid argument!\n");
			return -EINVAL;
		}

		chip->pps_status = OPLUS_PPS_STATUS_VOLT_CHANGE;
		chip->work_delay = 150;
		break;
	case OPLUS_PPS_STATUS_VOLT_CHANGE:
		ret = oplus_pps_charging_enable(chip, true);
		if (ret < 0) {
			return ret;
		}

		chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
		if (oplus_pps_get_curve_vbus(chip) == 20000) {
			chip->target_charger_current = 500;
		} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
			chip->target_charger_current = 1000;
		} else {
			chg_err("Invalid argument!\n");
			return -EINVAL;
		}

		if (chip->ask_charger_volt < chip->target_charger_volt) {
			chip->ask_charger_volt += 1000;
		}
		if (chip->ask_charger_volt >= chip->target_charger_volt) {
			chip->ask_charger_volt = chip->target_charger_volt;
			chip->pps_status = OPLUS_PPS_STATUS_CUR_INCREASE;
		}
		chip->work_delay = 500;
		break;
	case OPLUS_PPS_STATUS_CUR_INCREASE:
		ret = oplus_pps_charging_enable(chip, true);
		if (ret < 0) {
			return ret;
		}

		ret = oplus_pps_get_batt_curve_curr(chip);
		if (ret < 0) {
			return -EINVAL;
		}

		oplus_pps_get_cool_down_curr(chip);
		oplus_pps_get_batt_temp_curr(chip, chip->ap_batt_temperature);

		chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
		chip->target_charger_current = oplus_pps_get_target_current(chip);

		if ((chip->ap_input_current - chip->target_charger_current) > 1000
				|| (chip->target_charger_current - chip->ap_input_current) > 1000) {
			update_size = 250;
		} else {
			update_size = 50;
		}

		chip->ask_charger_volt = chip->target_charger_volt;
		if (chip->ap_input_current < (chip->target_charger_current - 500)) {
			chip->ask_charger_current += update_size;
			if (chip->ask_charger_current > chip->target_charger_current) {
				chip->ask_charger_current = chip->target_charger_current;
				chip->pps_status = OPLUS_PPS_STATUS_CHECK;
			}
			curr_increase_cnt = 0;
			curr_over_target_cnt = 0;
		} else {
			if (chip->ap_input_current < chip->target_charger_current - 50) {
				curr_increase_cnt++;
				if (curr_increase_cnt > 3) {
					chip->ask_charger_current += update_size;
					if (chip->ask_charger_current > chip->target_charger_current) {
						chip->ask_charger_current = chip->target_charger_current;
						chip->pps_status = OPLUS_PPS_STATUS_CHECK;
					}
					curr_increase_cnt = 0;
				}
				curr_over_target_cnt = 0;
			} else {
				if (chip->ap_input_current > chip->target_charger_current) {
					curr_over_target_cnt++;
					if (curr_over_target_cnt > 3) {
						chip->ask_charger_current -= update_size;
						chip->pps_status = OPLUS_PPS_STATUS_CHECK;
						curr_over_target_cnt = 0;
						chg_debug("pps chargering over target current\n");
					}
					curr_increase_cnt = 0;
				} else {
					chg_debug("pps chargering current okay\n");
					chip->pps_status = OPLUS_PPS_STATUS_CHECK;
					curr_increase_cnt = 0;
					curr_over_target_cnt = 0;
				}
			}
		}
		chip->work_delay = 500;
		break;
	case OPLUS_PPS_STATUS_CUR_DECREASE:
		ret = oplus_pps_charging_enable(chip, true);
		if (ret < 0) {
			return ret;
		}

		ret = oplus_pps_get_batt_curve_curr(chip);
		if (ret < 0) {
			return -EINVAL;
		}

		oplus_pps_get_cool_down_curr(chip);
		oplus_pps_get_batt_temp_curr(chip, chip->ap_batt_temperature);
		
		chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
		chip->target_charger_current = oplus_pps_get_target_current(chip);
		
		if ((chip->ap_input_current - chip->target_charger_current) > 1000
				|| (chip->target_charger_current - chip->ap_input_current) > 1000) {
			update_size = 250;
		} else {
			update_size = 50;
		}

		chip->ask_charger_volt = chip->target_charger_volt;
		if (chip->ap_input_current > chip->target_charger_current) {
			chip->ask_charger_current -= update_size;
			if (chip->ask_charger_current < 800) {
				chip->ask_charger_current = 800;
				chip->pps_status = OPLUS_PPS_STATUS_CHECK;
			}
		} else {
			chg_debug("pps chargering current okay\n");
			chip->pps_status = OPLUS_PPS_STATUS_CHECK;
		}
		chip->work_delay = 500;
		break;
	case OPLUS_PPS_STATUS_CHECK:
		ret = oplus_pps_charging_enable(chip, true);
		if (ret < 0) {
			return ret;
		}

		ret = oplus_pps_get_batt_curve_curr(chip);
		if (ret < 0) {
			return -EINVAL;
		}

		oplus_pps_get_cool_down_curr(chip);
		oplus_pps_get_batt_temp_curr(chip, chip->ap_batt_temperature);
		
		chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
		chip->target_charger_current = oplus_pps_get_target_current(chip);

		if (chip->ask_charger_volt != chip->target_charger_volt) {
			chip->ask_charger_volt = chip->target_charger_volt;
			if (oplus_pps_get_curve_vbus(chip) == 20000) {
				chip->ask_charger_current = 500;
			} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
				chip->ask_charger_current = 1000;
			} else {
				chg_err("Invalid argument!\n");
				return -EINVAL;
			}
			chip->pps_status = OPLUS_PPS_STATUS_VOLT_CHANGE;
		} else {
			if (chip->ap_input_current > chip->target_charger_current) {
				curr_over_target_cnt++;
			} else {
				curr_over_target_cnt = 0;
			}
			
			if (chip->target_charger_current < chip->target_charger_current_pre
					|| curr_over_target_cnt > 3) {
				chip->pps_status = OPLUS_PPS_STATUS_CUR_DECREASE;
				curr_over_target_cnt = 0;
			} else if (chip->target_charger_current > chip->target_charger_current_pre) {
				chip->pps_status = OPLUS_PPS_STATUS_CUR_INCREASE;
				curr_over_target_cnt = 0;
			} else {
				/*do nothing*/
			}
		}
		chip->work_delay = 500;
		break;
	default:
		chg_err("wrong status!\n");
		return -EINVAL;
	}
	chg_err("pps_status = %d, target_volt: %d, target_current = %d, ask_charger_volt: %d, ask_charger_current: %d, ap_input_volt = %d\n",
			chip->pps_status, chip->target_charger_volt, chip->target_charger_current,
			chip->ask_charger_volt, chip->ask_charger_current,chip->ap_input_volt);

	return 0;
}


static int oplus_pps_set_pdo(struct oplus_pps_chip *chip)
{
	int ret = 0;
	int ask_volt, ask_cur, max_cur = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if ((chip->ask_charger_volt != chip->ask_charger_volt_last)
	    || (chip->ask_charger_current
	        != chip->ask_charger_current_last)) {
		chip->ask_charger_volt_last = chip->ask_charger_volt;
		chip->ask_charger_current_last = chip->ask_charger_current;
		chip->set_pdo_flag = 1;
	}

	ask_volt = chip->ask_charger_volt;
	ask_cur = chip->ask_charger_current;

	max_cur = chip->get_pps_max_cur(ask_volt);
	if (ask_cur > max_cur) {
		chg_err("pdo cannot support %dmA,max current:%d\n", ask_cur, max_cur);
		ask_cur = max_cur;
	}
	chg_err("ask_volt, ask_cur : %d, %d\n", ask_volt, ask_cur);

	if (chip->set_pdo_flag == 1) {
		ret = chip->pps_pdo_select(ask_volt, ask_cur);
		if (ret) {
			chg_err("pps set pdo fail\n");
			return -EINVAL;
		}
		chip->set_pdo_flag = 0;
	}

	return ret;
}

void oplus_pps_stop(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	chg_err("oplus_pps_stop\n");
	if (oplus_pps_get_chg_status() == PPS_CHARGERING) {
		schedule_delayed_work(&chip->pps_stop_work, 0);
	}
}
EXPORT_SYMBOL(oplus_pps_stop);

static int oplus_pps_psy_changed(struct notifier_block *nb, unsigned long evt,
                           void *ptr)
{
#if 0
	struct oplus_pps_chip *chip = &g_pps_chip;
	union power_supply_propval val;

	int ret;

	ret = power_supply_get_property(chip->pps_usb_psy,
	                                POWER_SUPPLY_PROP_PD_ACTIVE, &val);
	if (ret) {
		chg_err("Unable to read PD MODE: %d\n", ret);
		return ret;
	}

	if (val.intval != POWER_SUPPLY_PD_PPS_ACTIVE) {
		if (oplus_pps_get_chg_status() == PPS_CHARGERING)
			schedule_delayed_work(&chip->pps_stop_work, 0);
	}
#endif

	return 0;
}

static void oplus_pps_update_work(struct work_struct *work)
{
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip,
						update_pps_work.work);
	int ret;
	int pd_type = oplus_chg_get_pps_type();

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	oplus_chg_disable_charge();
	oplus_chg_suspend_charger();

	if ((pd_type == PD_PPS_ACTIVE)
			&& (oplus_pps_get_chg_status() == PPS_CHARGERING)) {
		ret = oplus_pps_get_status(chip);
		if (ret < 0) {
			chg_err("oplus_pps_get_status ERROR\n");
			goto fail;
		}

		ret = oplus_pps_action(chip);
		if (ret < 0) {
			chg_err("oplus_pps_action ERROR\n");
			goto fail;
		}

		oplus_pps_status_check(chip);
		if (chip->fg_chg_current_over || chip->fg_current_over
		    || chip->fg_batt_full || chip->temp_over) {
			chg_err(
			    "stop pps charger,fg_chg_current_over:%d,fg_current_over:%d,fg_batt_full:%d,temp_over:%d\n",
			    chip->fg_chg_current_over, chip->fg_current_over,
			    chip->fg_batt_full, chip->temp_over);
			goto fail;
		} else if (chip->pps_disconnect) {
			oplus_chg_get_props_from_adsp_by_buffer();
			chip->pps_disconnect_volt = (chip->ops->get_input_volt() & 0xffff);
			chg_err("pps_disconnect_volt = %d\n", chip->pps_disconnect_volt);
			ret = oplus_pps_charging_enable(chip, false);
			if (ret < 0) {
				return;
			}

			msleep(100);

			oplus_chg_get_props_from_adsp_by_buffer();
			chg_err("close mos pps_disconnect_volt = %d\n", (chip->ops->get_input_volt() & 0xffff));
			if (chip->pps_disconnect_volt >= (chip->ops->get_input_volt() & 0xffff)) {
				goto fail;
			}
			chip->pps_disconnect_count = 0;
			chip->pps_disconnect = 0;
			ret = oplus_pps_charging_enable(chip, true);
			if (ret < 0) {
				return;
			}
		}

		ret = oplus_pps_set_pdo(chip);
		if (ret < 0) {
			chg_err("oplus_pps_action ERROR\n");
			goto fail;
		}

		schedule_delayed_work(&chip->update_pps_work,
		                      msecs_to_jiffies(chip->work_delay));
		return;
	}
fail:

	chg_err("oplus_pps_update_work fail\n");
	if(oplus_pps_get_chg_status() != PPS_NOT_SUPPORT) {
		oplus_start_svooc_reset();
		chg_err("-------------oplus_start_svooc_reset\n");
	}

	if (oplus_pps_get_chg_status() == PPS_CHARGERING)
		schedule_delayed_work(&chip->pps_stop_work, 0);

}

static void oplus_pps_stop_work(struct work_struct *work)
{
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip,
						pps_stop_work.work);

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	if (oplus_pps_get_chg_status() == PPS_CHARGE_END
	    || oplus_pps_get_chg_status() == PPS_NOT_SUPPORT) {
		return;
	} else {
		if (chip->temp_over) {
			oplus_pps_set_chg_status(PPS_CHECKING);
		} else {
			oplus_pps_set_chg_status(PPS_CHARGE_END);
		}
	}
	chg_err("oplus_pps_stop_work\n");
	oplus_pps_charging_enable(chip, false);

	msleep(300);
	oplus_chg_set_pd_5v();
	msleep(300);

	oplus_chg_unsuspend_charger();
	oplus_chg_enable_charge();

	oplus_chg_wake_update_work();
}

int oplus_pps_start(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	oplus_chg_set_pd_5v();
	msleep(300);

	oplus_pps_set_chg_status(PPS_CHARGERING);

	chg_err("oplus_pps_start\n");
	ret = oplus_pps_variables_init(chip, OPLUS_10V_30W_STATUS);
	if (ret)
		goto fail;
	schedule_delayed_work(&chip->update_pps_work, 0);

	return 0;

fail:
	oplus_pps_set_chg_status(PPS_CHARGE_END);
	return -1;
}

bool oplus_pps_check_by_thermal(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return false;
	}

	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();

	if (chip->ap_batt_temperature < chip->pps_batt_over_high_temp - TEMP_DELTAT
			&& chip->ap_batt_temperature > chip->pps_batt_over_low_temp + TEMP_DELTAT
			&& chip->ops->check_btb_temp() >= 0) {
		return true;
	}
	return false;
}

int oplus_pps_get_chg_status(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int ret;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->pps_support_type == 0) {
		return PPS_NOT_SUPPORT;
	}
	ret = chip->pps_chging;
	return ret;
}

int oplus_pps_set_chg_status(int status)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->pps_chging = status;
	return 0;
}

void oplus_pps_variables_reset(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	chip->pps_hard_reset_count--;
	if (chip->pps_hard_reset_count <= 0) {
		oplus_pps_set_chg_status(PPS_CHECKING);
		chip->pps_hard_reset_count = 0;
	} else {
		oplus_pps_set_chg_status(PPS_CHARGE_END);
		chg_err("pps_hard_reset_count:%d\n", chip->pps_hard_reset_count);
	}
}

int oplus_pps_register_ops(struct oplus_pps_mcu_operations *ops)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	chip->ops = ops;
	return 0;
}

int oplus_pps_init(struct oplus_chg_chip *g_chg_chip)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	struct power_supply *usb_psy;
	int ret = 0;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->dev = g_chg_chip->dev;

	oplus_pps_parse_dt(chip);

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_err(chip->dev, "USB psy not found; deferring probe\n");
		return -EPROBE_DEFER;
		//ret = -EPROBE_DEFER;
		//goto power_psy_reg_failed;
	}

	chip->pps_usb_psy = usb_psy;
	chip->pps_psy_nb.notifier_call = oplus_pps_psy_changed;
	ret = power_supply_reg_notifier(&chip->pps_psy_nb);
	if (ret)
		chg_err("register oplus_pps_psy_changed fail\n");

	chip->pps_pdo_select = oplus_chg_set_pps_config;
	chip->get_pps_status = oplus_chg_get_pps_status;
	chip->get_pps_max_cur = oplus_chg_pps_get_max_cur;
	chip->pps_hard_reset_count = 0;

	INIT_DELAYED_WORK(&chip->pps_stop_work, oplus_pps_stop_work);
	INIT_DELAYED_WORK(&chip->update_pps_work, oplus_pps_update_work);
	INIT_LIST_HEAD(&chip->temp_list);
	oplus_pps_set_chg_status(PPS_CHECKING);

	return 0;
}

bool oplus_is_pps_charging(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return false;
	}

	return (chip->pps_status >= OPLUS_PPS_STATUS_OPEN_OVP_CP) && (oplus_pps_get_chg_status() == PPS_CHARGERING);
}

void oplus_pps_hard_reset_notify(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	if (oplus_is_pps_charging()) {
		chip->pps_hard_reset_count = 3;
	} else {
		chip->pps_hard_reset_count = 0;
	}
}
