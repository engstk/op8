// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */
#include "oplus_charger.h"
#include "oplus_pps.h"
#include "oplus_pps_ops_manager.h"
#include "oplus_cp_intf.h"

#define BTB_OVER_TEMP_THRLD 80
enum BTB_STATUS {
	BTB_TMP_STATUS_BAD = 0,
	BTB_TMP_STATUS_GOOD,
};

static struct chargepump_device cp_device[CP_MAX] = {
	{ "master_cp", NULL, NULL },
	{ "slave_a_cp", NULL, NULL },
	{ "slave_b_cp", NULL, NULL },
};

int __attribute__((weak)) oplus_chg_pps_get_authentiate(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_read_vbat0_voltage(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_get_battery_btb_temp_cal(void)
{
	return 25;
}

int __attribute__((weak)) oplus_chg_get_usb_btb_temp_cal(void)
{
	return 25;
}

int __attribute__((weak)) oplus_chg_pps_get_max_volt(void)
{
	return -EINVAL;
}

extern int oplus_chg_get_battery_btb_temp_cal(void);
extern int oplus_chg_get_usb_btb_temp_cal(void);

int oplus_chg_check_btb_temp(void)
{
	int btb_bat = 0;
	int btb_usb = 0;

	btb_bat = oplus_chg_get_battery_btb_temp_cal();
	btb_usb = oplus_chg_get_usb_btb_temp_cal();
	pps_err("btb_bat=%d, btb_usb=%d", btb_bat, btb_usb);

	if (btb_bat >= BTB_OVER_TEMP_THRLD || btb_usb >= BTB_OVER_TEMP_THRLD)
		return BTB_TMP_STATUS_BAD;
	else
		return BTB_TMP_STATUS_GOOD;
}

static int oplus_cp_cfg_mode_init(int mode)
{
	int i;
	int ret = 0;

	if (mode == PPS_SC_MODE) {
		for (i = CP_MASTER; i < CP_MAX; i++) {
			if (cp_device[i].dev_ops != NULL) {
				if (cp_device[i].dev_ops->oplus_cp_cfg_sc != NULL) {
					ret = cp_device[i].dev_ops->oplus_cp_cfg_sc(cp_device[i].client);
				} else {
					pps_err("%s Not support SC mode", cp_device[i].dev_name);
					ret = -EINVAL;
				}
			}
		}
	} else if (mode == PPS_BYPASS_MODE) {
		for (i = CP_MASTER; i < CP_MAX; i++) {
			if (cp_device[i].dev_ops != NULL) {
				if (cp_device[i].dev_ops->oplus_cp_cfg_bypass != NULL) {
					ret = cp_device[i].dev_ops->oplus_cp_cfg_bypass(cp_device[i].client);
				} else {
					pps_err("%s Not support bypass mode", cp_device[i].dev_name);
					ret = -EINVAL;
				}
			}
		}
	}
	return ret;
}

static void oplus_cp_hardware_init(void)
{
	int i;
	for (i = CP_MASTER; i < CP_MAX; i++) {
		if (cp_device[i].dev_ops != NULL && cp_device[i].dev_ops->oplus_cp_hardware_init != NULL) {
			cp_device[i].dev_ops->oplus_cp_hardware_init(cp_device[i].client);
		}
	}
	pps_err(" end\n");
}

static void oplus_cp_reset(void)
{
	int i;
	for (i = CP_MASTER; i < CP_MAX; i++) {
		if (cp_device[i].dev_ops != NULL && cp_device[i].dev_ops->oplus_cp_reset != NULL) {
			cp_device[i].dev_ops->oplus_cp_reset(cp_device[i].client);
		}
	}
	pps_err(" end\n");
}

static void oplus_cp_pmid2vout_enable(bool enable)
{
	int i;
	for (i = CP_MASTER; i < CP_MAX; i++) {
		if (cp_device[i].dev_ops != NULL) {
			if (cp_device[i].dev_ops->oplus_cp_pmid2vout_enable != NULL)
				cp_device[i].dev_ops->oplus_cp_pmid2vout_enable(cp_device[i].client, enable);
			else
				pps_err("%s Not support pmid2vout DIS", cp_device[i].dev_name);
		}
	}
	pps_err(" end\n");
}

static int oplus_cp_get_ucp_flag(void)
{
	int ucp_fail = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_ucp_flag != NULL) {
		ucp_fail = cp_device[CP_MASTER].dev_ops->oplus_get_ucp_flag(cp_device[CP_MASTER].client);
	}
	pps_err("ucp_fail = 0x%x\n", ucp_fail);
	return ucp_fail;
}

static int oplus_cp_get_vbat(void)
{
	int vbat = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_vbat != NULL) {
		vbat = cp_device[CP_MASTER].dev_ops->oplus_get_cp_vbat(cp_device[CP_MASTER].client);
	}
	pps_err("vbat = %d\n", vbat);
	return vbat;
}

static int oplus_cp_master_cp_enable(int enable)
{
	int status = 0;

	pps_err("CP_MASTER enable = %d\n", enable);
	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_set_cp_enable != NULL) {
		status = cp_device[CP_MASTER].dev_ops->oplus_set_cp_enable(cp_device[CP_MASTER].client, enable);
	}

	return status;
}

static int oplus_cp_slave_cp_enable(int enable)
{
	int status = 0;

	pps_err("CP_SLAVE_A enable = %d\n", enable);
	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_set_cp_enable != NULL) {
		status = cp_device[CP_SLAVE_A].dev_ops->oplus_set_cp_enable(cp_device[CP_SLAVE_A].client, enable);
	}

	return status;
}

static int oplus_cp_slave_b_cp_enable(int enable)
{
	int status = 0;

	pps_err("CP_SLAVE_B enable = %d\n", enable);
	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_set_cp_enable != NULL) {
		status = cp_device[CP_SLAVE_B].dev_ops->oplus_set_cp_enable(cp_device[CP_SLAVE_B].client, enable);
	}

	return status;
}

static int oplus_cp_master_get_vbus(void)
{
	int vbus = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_vbus != NULL) {
		vbus = cp_device[CP_MASTER].dev_ops->oplus_get_cp_vbus(cp_device[CP_MASTER].client);
		if (vbus < 0)
			vbus = 0;
	}

	return vbus;
}

static int oplus_cp_slave_get_vbus(void)
{
	int vbus = 0;

	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vbus != NULL) {
		vbus = cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vbus(cp_device[CP_SLAVE_A].client);
		if (vbus < 0)
			vbus = 0;
	}

	return vbus;
}

static int oplus_cp_slave_b_get_vbus(void)
{
	int vbus = 0;

	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vbus != NULL) {
		vbus = cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vbus(cp_device[CP_SLAVE_B].client);
		if (vbus < 0)
			vbus = 0;
	}

	return vbus;
}

static int oplus_cp_master_get_ibus(void)
{
	int ibus = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_ibus != NULL) {
		ibus = cp_device[CP_MASTER].dev_ops->oplus_get_cp_ibus(cp_device[CP_MASTER].client);
		if (ibus < 0)
			ibus = 0;
	}

	return ibus;
}

static int oplus_cp_slave_get_ibus(void)
{
	int ibus = 0;

	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_ibus != NULL) {
		ibus = cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_ibus(cp_device[CP_SLAVE_A].client);
		if (ibus < 0)
			ibus = 0;
	}

	return ibus;
}

static int oplus_cp_slave_b_get_ibus(void)
{
	int ibus = 0;

	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_ibus != NULL) {
		ibus = cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_ibus(cp_device[CP_SLAVE_B].client);
		if (ibus < 0)
			ibus = 0;
	}

	return ibus;
}

static int oplus_cp_master_get_vac(void)
{
	int vac = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_vac != NULL) {
		vac = cp_device[CP_MASTER].dev_ops->oplus_get_cp_vac(cp_device[CP_MASTER].client);
		if (vac < 0)
			vac = 0;
	}

	return vac;
}

static int oplus_cp_slave_get_vac(void)
{
	int vac = 0;

	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vac != NULL) {
		vac = cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vac(cp_device[CP_SLAVE_A].client);
		if (vac < 0)
			vac = 0;
	}

	return vac;
}

static int oplus_cp_slave_b_get_vac(void)
{
	int vac = 0;

	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vac != NULL) {
		vac = cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vac(cp_device[CP_SLAVE_B].client);
		if (vac < 0)
			vac = 0;
	}

	return vac;
}

static int oplus_cp_master_get_vout(void)
{
	int vout = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_vout != NULL) {
		vout = cp_device[CP_MASTER].dev_ops->oplus_get_cp_vout(cp_device[CP_MASTER].client);
		if (vout < 0)
			vout = 0;
	}

	return vout;
}

static int oplus_cp_slave_get_vout(void)
{
	int vout = 0;

	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vout != NULL) {
		vout = cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_vout(cp_device[CP_SLAVE_A].client);
		if (vout < 0)
			vout = 0;
	}

	return vout;
}

static int oplus_cp_slave_b_get_vout(void)
{
	int vout = 0;

	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vout != NULL) {
		vout = cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_vout(cp_device[CP_SLAVE_B].client);
		if (vout < 0)
			vout = 0;
	}

	return vout;
}

static int oplus_cp_master_get_tdie(void)
{
	int tdie = 0;

	if (cp_device[CP_MASTER].dev_ops != NULL && cp_device[CP_MASTER].dev_ops->oplus_get_cp_tdie != NULL) {
		tdie = cp_device[CP_MASTER].dev_ops->oplus_get_cp_tdie(cp_device[CP_MASTER].client);
	}

	return tdie;
}

static int oplus_cp_slave_get_tdie(void)
{
	int tdie = 0;

	if (cp_device[CP_SLAVE_A].dev_ops != NULL && cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_tdie != NULL) {
		tdie = cp_device[CP_SLAVE_A].dev_ops->oplus_get_cp_tdie(cp_device[CP_SLAVE_A].client);
	}

	return tdie;
}

static int oplus_cp_slave_b_get_tdie(void)
{
	int tdie = 0;

	if (cp_device[CP_SLAVE_B].dev_ops != NULL && cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_tdie != NULL) {
		tdie = cp_device[CP_SLAVE_B].dev_ops->oplus_get_cp_tdie(cp_device[CP_SLAVE_B].client);
	}

	return tdie;
}

/* there no check null in oplus_pps.c call, all pfunc not be null */
static struct oplus_pps_operations oplus_cp_pps_ops = {
	.get_vbat0_volt = oplus_chg_read_vbat0_voltage,
	.check_btb_temp = oplus_chg_check_btb_temp,
	.pps_get_authentiate = oplus_chg_pps_get_authentiate,
	.pps_pdo_select = oplus_chg_set_pps_config,
	.get_pps_status = oplus_chg_get_pps_status,
	.get_pps_max_cur = oplus_chg_pps_get_max_cur,
	.get_pps_max_volt = oplus_chg_pps_get_max_volt,

	.pps_cp_hardware_init = oplus_cp_hardware_init,
	.pps_cp_reset = oplus_cp_reset,
	.pps_cp_mode_init = oplus_cp_cfg_mode_init,
	.pps_cp_pmid2vout_enable = oplus_cp_pmid2vout_enable,
	.pps_get_ucp_flag = oplus_cp_get_ucp_flag,
	.pps_get_cp_vbat = oplus_cp_get_vbat,

	.pps_mos_ctrl = oplus_cp_master_cp_enable,
	.pps_get_cp_master_vbus = oplus_cp_master_get_vbus,
	.pps_get_cp_master_ibus = oplus_cp_master_get_ibus,
	.pps_get_cp_master_vac = oplus_cp_master_get_vac,
	.pps_get_cp_master_vout = oplus_cp_master_get_vout,
	.pps_get_cp_master_tdie = oplus_cp_master_get_tdie,

	.pps_get_cp_slave_vbus = oplus_cp_slave_get_vbus,
	.pps_get_cp_slave_ibus = oplus_cp_slave_get_ibus,
	.pps_mos_slave_ctrl = oplus_cp_slave_cp_enable,
	.pps_get_cp_slave_vac = oplus_cp_slave_get_vac,
	.pps_get_cp_slave_vout = oplus_cp_slave_get_vout,
	.pps_get_cp_slave_tdie = oplus_cp_slave_get_tdie,

	.pps_get_cp_slave_b_vbus = oplus_cp_slave_b_get_vbus,
	.pps_get_cp_slave_b_ibus = oplus_cp_slave_b_get_ibus,
	.pps_mos_slave_b_ctrl = oplus_cp_slave_b_cp_enable,
	.pps_get_cp_slave_b_vac = oplus_cp_slave_b_get_vac,
	.pps_get_cp_slave_b_vout = oplus_cp_slave_b_get_vout,
	.pps_get_cp_slave_b_tdie = oplus_cp_slave_b_get_tdie,
};

void oplus_pps_device_protect_irq_callback(DEV_PROTECT_FLAG flag)
{
	if (!oplus_pps_get_pps_mos_started()) {
		pps_err(",oplus_pps_get_pps_mos_started false\r\n");
		return;
	}

	if (flag.value_bit.ibus_ucp || flag.value_bit.ibus_ocp) {
		oplus_pps_stop_disconnect();
	}
}

int oplus_cp_device_register(PPS_CP_DEVICE_NUM index, struct chargepump_device *cp_dev)
{
	if (index < CP_MASTER || index >= CP_MAX) {
		pps_err("the device index out of bound, not support!");
		return -EINVAL;
	}
	if (cp_dev == NULL || cp_dev->dev_name == NULL || cp_dev->client == NULL || cp_dev->dev_ops == NULL) {
		pps_err("need a non-null device");
		return -EINVAL;
	}
	if (cp_device[index].dev_ops != NULL) {
		pps_err("Don't repeat register %s, %s failed", cp_device[index].dev_name, cp_dev->dev_name);
		return -EINVAL;
	}

	cp_device[index].dev_ops = cp_dev->dev_ops;
	cp_device[index].client = cp_dev->client;

	if (index == CP_MASTER)
		oplus_pps_ops_register(cp_dev->dev_name, &oplus_cp_pps_ops);

	pps_err("Register %s as the %s CP success.", cp_dev->dev_name, cp_device[index].dev_name);
	return 0;
}
