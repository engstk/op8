// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2022 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_sc8571.h"
#include "../oplus_pps.h"

static struct chip_sc8571 *chip_sc8571_master = NULL;

static struct mutex i2c_rw_lock;
int sc8571_master_dump_registers(void);

bool sc8571_master_get_enable(void);
static int sc8571_track_upload_i2c_err_info(struct chip_sc8571 *chip, int err_type, u8 reg);
static int sc8571_track_upload_cp_err_info(struct chip_sc8571 *chip, int err_type);

/************************************************************************/
static int __sc8571_read_byte(u8 reg, u8 *data)
{
	int ret = 0;
	int retry = 3;

	struct chip_sc8571 *chip = chip_sc8571_master;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}
	ret = i2c_smbus_read_byte_data(chip->master_client, reg);
	if (ret < 0) {
		pps_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		oplus_pps_notify_master_cp_error();
		while (retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_byte_data(chip->master_client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
		if (ret < 0)
			sc8571_track_upload_i2c_err_info(chip, ret, reg);
	}

	*data = (u8)ret;
	if (chip->debug_force_i2c_err)
		sc8571_track_upload_i2c_err_info(chip, ret, reg);

	return 0;
}

static int __sc8571_write_byte(u8 reg, u8 val)
{
	int ret = 0;
	int retry = 3;

	struct chip_sc8571 *chip = chip_sc8571_master;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(chip->master_client, reg, val);

	if (ret < 0) {
		pps_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		oplus_pps_notify_master_cp_error();
		while (retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(chip->master_client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
		if (ret < 0)
			sc8571_track_upload_i2c_err_info(chip, ret, reg);
	}
	if (chip->debug_force_i2c_err)
		sc8571_track_upload_i2c_err_info(chip, ret, reg);

	return ret;
}

static int sc8571_read_byte(u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_read_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8571_write_byte(u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_write_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8571_read_word(u8 reg, u8 *data_block)
{
	struct chip_sc8571 *chip = chip_sc8571_master;
	int ret = 0;
	int retry = 3;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_i2c_block_data(chip->master_client, reg, 2, data_block);
	if (ret < 0) {
		pps_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		oplus_pps_notify_master_cp_error();
		while (retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_i2c_block_data(chip->master_client, reg, 2, data_block);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
		if (ret < 0)
			sc8571_track_upload_i2c_err_info(chip, ret, reg);
	}
	if (chip->debug_force_i2c_err)
		sc8571_track_upload_i2c_err_info(chip, ret, reg);
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int sc8571_master_i2c_masked_write(u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_read_byte(reg, &tmp);
	if (ret) {
		pps_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sc8571_write_byte(reg, tmp);
	if (ret)
		pps_err("Faileds: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int sc8571_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int sc8571_track_upload_i2c_err_info(struct chip_sc8571 *chip, int err_type, u8 reg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = sc8571_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->debug_force_i2c_err) {
		err_type = -chip->debug_force_i2c_err;
		chip->debug_force_i2c_err = false;
	}

	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_uploading) {
		pr_info("i2c_err_uploading, should return\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->i2c_err_load_trigger)
		kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8571_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8571");
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$access_reg@@0x%02x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sc8571_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sc8571 *chip = container_of(dwork, struct chip_sc8571, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = NULL;
	chip->i2c_err_uploading = false;
}

static int sc8571_dump_reg_info(struct chip_sc8571 *chip, char *dump_info, int len)
{
	int index = 0;
	u8 addr;
	struct oplus_pps_chip *pps_chip = oplus_pps_get_pps_chip();
	if (!pps_chip)
		return -1;

	if (!chip || !dump_info)
		return -1;

	for (addr = SC8571_REG_18; addr <= SC8571_REG_1C; addr++) {
		index += snprintf(&(dump_info[index]), len - index, "0x%02x=0x%02x,", addr,
				  pps_chip->int_column[addr - SC8571_REG_18]);
	}
	addr = SC8571_REG_1C - SC8571_REG_18 + 1;
	index += snprintf(&(dump_info[index]), len - index, "0x%02x=0x%02x", SC8571_REG_42, pps_chip->int_column[addr]);

	return 0;
}

static int sc8571_track_upload_cp_err_info(struct chip_sc8571 *chip, int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	memset(chip->dump_info, 0, sizeof(chip->dump_info));
	curr_time = sc8571_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (err_type == TRACK_CP_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_cp_err_lock);
	if (chip->cp_err_uploading) {
		pr_info("cp_err_uploading, should return\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->cp_err_load_trigger)
		kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->cp_err_load_trigger) {
		pr_err("cp_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->cp_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->cp_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->cp_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8571_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8571");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_CP_ERR);

	oplus_chg_track_get_cp_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	sc8571_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("upload cp error success\n");

	return 0;
}

static void sc8571_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sc8571 *chip = container_of(dwork, struct chip_sc8571, cp_err_load_trigger_work);

	if (!chip->cp_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
}

static int sc8571_track_debugfs_init(struct chip_sc8571 *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sc8571;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_sc8571 = debugfs_create_dir("sc8571", debugfs_root);
	if (!debugfs_sc8571) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_CP_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_sc8571, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_sc8571, &(chip->debug_force_cp_err));

	return ret;
}

static int sc8571_track_init(struct chip_sc8571 *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_cp_err_lock);
	mutex_init(&chip->track_upload_lock);
	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
	chip->cp_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work, sc8571_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work, sc8571_track_cp_err_load_trigger_work);
	rc = sc8571_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("sc8571 debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

int sc8571_master_get_tdie(void)
{
	u8 data_block[2] = { 0 };
	int tdie = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return tdie;
	}

	sc8571_read_word(SC8571_REG_37, data_block);
	tdie = (((data_block[0] & SC8571_TDIE_POL_H_MASK) << 8) | (data_block[1] & SC8571_TDIE_POL_L_MASK)) *
	       SC8571_TDIE_ADC_LSB;
	if (tdie < SC8571_TDIE_MIN || tdie > SC8571_TDIE_MAX)
		tdie = SC8571_TDIE_MAX;
	return tdie;
}

int sc8571_master_get_ucp_flag(void)
{
	int ret = 0;
	u8 temp;
	int ucp_fail = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return 0;
	}

	ret = sc8571_read_byte(SC8571_REG_19, &temp);
	if (ret < 0) {
		pps_err("SC8571_REG_19\n");
		return 0;
	}

	ucp_fail = (temp & SC8571_BUS_UCP_FALL_FLAG_MASK) >> SC8571_BUS_UCP_FALL_FLAG_SHIFT;
	pps_err("0x19[0x%x] ucp_fail = %d\n", temp, ucp_fail);

	return ucp_fail;
}

int sc8571_match_err_value(void)
{
	struct oplus_pps_chip *chip = oplus_pps_get_pps_chip();
	int err_type = TRACK_CP_ERR_DEFAULT;

	if (!chip)
		return -1;
	if (!chip_sc8571_master)
		return -1;

	if (chip->int_column[0] & SC8571_BAT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBAT_OVP;
	else if (chip->int_column[0] & SC8571_OUT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VOUT_OVP;
	else if (chip->int_column[0] & SC8571_BUS_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS_OVP;

	if (chip->int_column[1] & SC8571_BUS_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_OCP;
	else if (chip->int_column[1] & SC8571_BUS_UCP_FALL_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_UCP;
	else if (chip->int_column[1] & SC8571_PIN_DIAG_FALL_FLAG_MASK)
		err_type = TRACK_CP_ERR_CFLY_CDRV_FAULT;

	if (chip->int_column[2] & SC8571_AC1_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VAC1_OVP;
	else if (chip->int_column[2] & SC8571_AC2_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VAC2_OVP;

	if (chip->int_column[3] & SC8571_SS_TIMEOUT_FLAG_MASK)
		err_type = TRACK_CP_ERR_SS_TIMEOUT;
	else if (chip->int_column[3] & SC8571_TSHUT_FLT_FLAG_MASK)
		err_type = TRACK_CP_ERR_TSHUT;
	else if (chip->int_column[3] & SC8571_WD_TIMEOUT_FLAG_MASK)
		err_type = TRACK_CP_ERR_I2C_WDT;

	if (chip->int_column[4] & SC8571_VBUS_ERR_HI_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS2OUT_ERRORHI;
	else if (chip->int_column[4] & SC8571_VBUS_ERR_LO_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS2OUT_ERRORLO;

	/* IBUS_UCP still trigger when normal plug out, so use for debug */
	if (chip_sc8571_master->debug_force_cp_err || err_type == TRACK_CP_ERR_IBUS_UCP)
		err_type = chip_sc8571_master->debug_force_cp_err;
	if (err_type != TRACK_CP_ERR_DEFAULT)
		sc8571_track_upload_cp_err_info(chip_sc8571_master, err_type);

	return 0;
}

int sc8571_master_get_int_value(void)
{
	int ret = 0;
	u8 addr, int_column[PPS_DUMP_REG_CNT];

	struct oplus_pps_chip *chip = oplus_pps_get_pps_chip();
	if (!chip)
		return -1;
	memset(int_column, 0, sizeof(u8) * PPS_DUMP_REG_CNT);
	for (addr = SC8571_REG_18; addr <= SC8571_REG_1C; addr++) {
		ret = sc8571_read_byte(addr, &int_column[addr - SC8571_REG_18]);
		if (ret < 0) {
			pps_err(" Couldn't read  0x%02x ret = %d\n", addr, ret);
			return -1;
		}
		pps_err("reg[0x%x] = 0x%x\n", addr, int_column[addr - SC8571_REG_18]);
	}
	addr = SC8571_REG_1C - SC8571_REG_18 + 1;
	ret = sc8571_read_byte(SC8571_REG_42, &int_column[addr]);
	memcpy(chip->int_column, int_column, sizeof(chip->int_column));
	oplus_pps_print_dbg_info(chip);

	sc8571_match_err_value();
	return 0;
}

int sc8571_master_get_vout(void)
{
	u8 data_block[2] = { 0 };
	int vout = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return vout;
	}

	sc8571_read_word(SC8571_REG_2D, data_block);
	vout = (((data_block[0] & SC8571_VOUT_POL_H_MASK) << 8) | (data_block[1] & SC8571_VOUT_POL_L_MASK)) *
	       SC8571_VOUT_ADC_LSB;
	if (vout < SC8571_VOUT_MIN || vout > SC8571_VOUT_MAX)
		vout = SC8571_VOUT_MAX;

	return vout;
}

int sc8571_master_get_vac(void)
{
	u8 data_block[2] = { 0 };
	int vac = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return vac;
	}

	sc8571_read_word(SC8571_REG_29, data_block);
	vac = (((data_block[0] & SC8571_VAC1_POL_H_MASK) << 8) | (data_block[1] & SC8571_VAC1_POL_L_MASK)) *
	      SC8571_VAC1_ADC_LSB;
	if (vac < SC8571_VAC1_MIN || vac > SC8571_VAC1_MAX)
		vac = SC8571_VAC1_MAX;

	return vac;
}

int sc8571_master_get_vbus(void)
{
	u8 data_block[2] = { 0 };
	int cp_vbus = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return cp_vbus;
	}

	sc8571_read_word(SC8571_REG_27, data_block);
	cp_vbus = (((data_block[0] & SC8571_VBUS_POL_H_MASK) << 8) | (data_block[1] & SC8571_VBUS_POL_L_MASK)) *
		  SC8571_VBUS_ADC_LSB;
	if (cp_vbus < SC8571_VBUS_MIN || cp_vbus > SC8571_VBUS_MAX)
		cp_vbus = SC8571_VBUS_MAX;

	return cp_vbus;
}

int sc8571_master_get_ibus(void)
{
	u8 data_block[2] = { 0 };
	int cp_ibus = 0;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return cp_ibus;
	}

	sc8571_read_word(SC8571_REG_25, data_block);
	cp_ibus = (((data_block[0] & SC8571_IBUS_POL_H_MASK) << 8) | (data_block[1] & SC8571_IBUS_POL_L_MASK)) *
		  SC8571_IBUS_ADC_LSB;
	if (cp_ibus < SC8571_IBUS_MIN || cp_ibus > SC8571_IBUS_MAX)
		cp_ibus = SC8571_IBUS_MAX;

	return cp_ibus;
}

int sc8571_master_cp_enable(int enable)
{
	struct chip_sc8571 *chip = chip_sc8571_master;
	int ret = 0;
	if (!chip) {
		pps_err("chip is NULL\n");
		return ret;
	}
	mutex_lock(&chip->cp_enable_mutex);
	if (enable && (sc8571_master_get_enable() == false)) {
		ret = sc8571_master_i2c_masked_write(SC8571_REG_0F, SC8571_CHG_EN_MASK,
						     SC8571_CHG_ENABLE << SC8571_CHG_EN_SHIFT);
	} else if (!enable) {
		if (sc8571_master_get_enable() == false)
			msleep(100);
		if (sc8571_master_get_enable() == true)
			ret = sc8571_master_i2c_masked_write(SC8571_REG_0F, SC8571_CHG_EN_MASK,
							     SC8571_CHG_DISABLE << SC8571_CHG_EN_SHIFT);
	}
	mutex_unlock(&chip->cp_enable_mutex);
	return ret;
}

bool sc8571_master_get_enable(void)
{
	int ret = 0;
	u8 temp;
	bool cp_enable = false;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return false;
	}

	ret = sc8571_read_byte(SC8571_REG_0F, &temp);
	if (ret < 0) {
		pr_err("SC8571_REG_0F\n");
		return false;
	}

	cp_enable = (temp & SC8571_CHG_EN_MASK) >> SC8571_CHG_EN_SHIFT;

	return cp_enable;
}

void sc8571_master_pmid2vout_enable(bool enable)
{
	/*do nothing now*/
}

void sc8571_master_cfg_sc(void)
{
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x00); /*0x0F Disable charge, SC_mode*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/

	sc8571_write_byte(SC8571_REG_05, 0x00); /*0X05 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_06, 0x4B); /*0X06 BUS_OVP=23V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_08, 0x14); /*0X08 DIS_IBUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A,
			  0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	sc8571_write_byte(SC8571_REG_0E, 0xD8); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	sc8571_write_byte(SC8571_REG_10, 0x30); /*0X30 enable watchdog*/
	sc8571_write_byte(SC8571_REG_11, 0x58); /*0X11  IBUS UCP*/
	sc8571_write_byte(SC8571_REG_12, 0x60); /*0X12 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_23, 0x80); /*0X23 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_41, 0x20); /*0X41 disable pmid2vout temp*/
	sc8571_write_byte(SC8571_REG_42, 0x5C); /*0X42 pmid2vout 400mv*/
	sc8571_master_pmid2vout_enable(false);
}

void sc8571_master_cfg_bypass(void)
{
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x08); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_05, 0x0); /*0X05 DIS_BATOCP_ALM*/

	sc8571_write_byte(SC8571_REG_06, 0x5A); /*0X06 BUS_OVP=10.5V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_08, 0x16); /*0X08 DIS_IBUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A,
			  0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	sc8571_write_byte(SC8571_REG_0E, 0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	sc8571_write_byte(SC8571_REG_10, 0x30); /*0X30 enable watchdog*/
	sc8571_write_byte(SC8571_REG_11, 0x58); /*0X11 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_12, 0x60); /*0X12 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_23, 0x80); /*0X23 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_41, 0x20); /*0X41 disable pmid2vout temp*/
	sc8571_write_byte(SC8571_REG_42, 0xFC); /*0xFC*/
}

void sc8571_master_hardware_init(void)
{
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x0); /*0x0F Disable charge, sc mode*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_06, 0x0); /*0X06 BUS_OVP=10.5V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A,
			  0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	sc8571_write_byte(SC8571_REG_0E, 0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	sc8571_write_byte(SC8571_REG_10, 0x84); /*0X10 disalbe watchdog*/
	sc8571_write_byte(SC8571_REG_23, 0x00); /*0X23 adc disable continous*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 disalbe TSBUT_ADC/TSBAT_ADC/IBAT_ADC*/
	pps_err(" end!\n");
}

void sc8571_master_reset(void)
{
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x80); /*0x0F reset cp*/
}

int sc8571_master_dump_registers(void)
{
	int ret = 0;
	char buf[1024];
	char *s;

	u8 addr;
	u8 val_buf[0x43] = { 0x0 };

	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return -1;
	}

	for (addr = 0; addr < 0x10; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s += sprintf(s, "sc8571_master_dump_registers:0~0x10");
	for (addr = 0; addr < 0x10; addr++) {
		s += sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s += sprintf(s, "\n");
	pr_err("%s \n", buf);

	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x10; addr < 0x20; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s += sprintf(s, "sc8571_master_dump_registers: 0x10~0x20");
	for (addr = 0x10; addr < 0x20; addr++) {
		s += sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s += sprintf(s, "\n");
	pr_err("%s \n", buf);

	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x20; addr < 0x30; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s += sprintf(s, "sc8571_master_dump_registers: 0x20~0x30");
	for (addr = 0x20; addr < 0x30; addr++) {
		s += sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s += sprintf(s, "\n");
	pr_err("%s \n", buf);

	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x30; addr < 0x42; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s += sprintf(s, "sc8571_master_dump_registers: 0x30~0x42");
	for (addr = 0x30; addr < 0x43; addr++) {
		s += sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s += sprintf(s, "\n");
	pr_err("%s \n", buf);

	return ret;
}

static ssize_t sc8571_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return idx;
	}

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8571");
	for (addr = SC8571_REG_00; addr <= SC8571_REG_43; addr++) {
		ret = sc8571_read_byte(addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc8571_store_register(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int reg;
	unsigned int val;
	if (!chip_sc8571_master) {
		pps_err("chip is NULL\n");
		return 0;
	}

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SC8571_REG_43)
		sc8571_write_byte((unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, sc8571_show_registers, sc8571_store_register);

static void sc8571_create_device_node(struct device *dev)
{
	int err = 0;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		pps_err("sc8571 create device err!\n");
}

static int sc8571_parse_dt(struct chip_sc8571 *chip)
{
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	return 0;
}

irqreturn_t sc8571_ucp_interrupt_handler(struct chip_sc8571 *chip)
{
	int ucp_value;
	if (!oplus_pps_get_pps_mos_started()) {
		pps_err(",oplus_pps_get_pps_mos_started false\r\n");
		return IRQ_HANDLED;
	}
	/*change to work*/
	sc8571_master_get_int_value();
	ucp_value = sc8571_master_get_ucp_flag();

	if (ucp_value) {
		oplus_pps_stop_disconnect();
	}
	pps_err(",ucp_value = %d", ucp_value);

	return IRQ_HANDLED;
}

static int sc8571_irq_gpio_init(struct chip_sc8571 *chip)
{
	int rc;
	struct device_node *node = chip->master_dev->of_node;

	if (!node) {
		pps_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->ucp_gpio = of_get_named_gpio(node, "qcom,ucp_gpio", 0);
	if (chip->ucp_gpio < 0) {
		pps_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->ucp_gpio)) {
			rc = gpio_request(chip->ucp_gpio, "ucp_gpio");
			if (rc) {
				pps_err("unable to request gpio [%d]\n", chip->ucp_gpio);
			}
		}
		pps_err("chip->ucp_gpio =%d\n", chip->ucp_gpio);
		chip->ucp_irq = gpio_to_irq(chip->ucp_gpio);
		pps_err("irq chip->ucp_irq =%d\n", chip->ucp_irq);
	}

	/* set voocphy pinctrl*/
	chip->ucp_pinctrl = devm_pinctrl_get(chip->master_dev);
	if (IS_ERR_OR_NULL(chip->ucp_pinctrl)) {
		pps_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->ucp_int_active = pinctrl_lookup_state(chip->ucp_pinctrl, "ucp_int_active");
	if (IS_ERR_OR_NULL(chip->ucp_int_active)) {
		pps_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	chip->ucp_int_sleep = pinctrl_lookup_state(chip->ucp_pinctrl, "ucp_int_sleep");
	if (IS_ERR_OR_NULL(chip->ucp_int_sleep)) {
		pps_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->ucp_gpio);
	pinctrl_select_state(chip->ucp_pinctrl, chip->ucp_int_active); /* no_PULL */

	rc = gpio_get_value(chip->ucp_gpio);
	pps_err("irq chip->ucp_gpio input =%d irq_gpio_stat = %d\n", chip->ucp_gpio, rc);

	return 0;
}

static irqreturn_t sc8571_ucp_interrupt(int irq, void *dev_id)
{
	struct chip_sc8571 *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return sc8571_ucp_interrupt_handler(chip);
}

static int sc8571_irq_register(struct chip_sc8571 *chip)
{
	int ret = 0;

	sc8571_irq_gpio_init(chip);
	pps_err("sc8571 chip->ucp_irq = %d\n", chip->ucp_irq);
	if (chip->ucp_irq) {
		ret = request_threaded_irq(chip->ucp_irq, NULL, sc8571_ucp_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sc8571_ucp_irq", chip);
		if (ret < 0) {
			pps_err("request irq for ucp_irq=%d failed, ret =%d\n", chip->ucp_irq, ret);
			return ret;
		}
		enable_irq_wake(chip->ucp_irq);
	}
	pps_err("request irq ok\n");

	return ret;
}

extern int pps_cp_id;
int sc8571_master_get_hw_id(void)
{
	u8 temp;
	int hw_id = PPS_CP_ID_SC8571;
	int ret;

	ret = sc8571_read_byte(SC8571_REG_22, &temp);
	if (ret < 0) {
		pps_err("failed sc8571_master_get_enable\n");
		return hw_id;
	}
	hw_id = (int)temp;
	pps_err(" hw_id = 0x%x\n", hw_id);
	return hw_id;
}

static void register_pps_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	version = "sc8571";
	manufacture = "MP";

	ret = register_device_proc("sc8571", version, manufacture);
	if (ret)
		pps_err("register_pps_devinfo fail\n");
#endif
}

static int sc8571_master_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct chip_sc8571 *chip;

	pps_err(" enter!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->master_client = client;
	chip->master_dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	chip_sc8571_master = chip;
	sc8571_track_init(chip);
	if (sc8571_master_get_hw_id() == PPS_CP_ID_BQ25980) {
		pps_cp_id = PPS_CP_ID_BQ25980;
		return 0;
	} else {
		pps_cp_id = PPS_CP_ID_SC8571;
	}

	sc8571_create_device_node(&(client->dev));

	sc8571_parse_dt(chip);
	sc8571_master_dump_registers();

	sc8571_master_hardware_init();
	/*oplus_pps_cp_register_ops(&oplus_sc8571_ops);*/
	sc8571_irq_register(chip);

	sc8571_master_get_enable();

	register_pps_devinfo();

	mutex_init(&chip->cp_enable_mutex);

	pps_err("sc8571_parse_dt successfully!\n");

	return 0;
}

static void sc8571_master_shutdown(struct i2c_client *client)
{
	return;
}

static struct of_device_id sc8571_master_match_table[] = {
	{
		.compatible = "oplus,sc8571-master",
	},
	{},
};

static const struct i2c_device_id sc8571_master_charger_id[] = {
	{ "sc8571-master", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc8571_master_charger_id);

static struct i2c_driver sc8571_master_driver = {
	.driver =
		{
			.name = "sc8571-master",
			.owner = THIS_MODULE,
			.of_match_table = sc8571_master_match_table,
		},
	.id_table = sc8571_master_charger_id,

	.probe = sc8571_master_probe,
	.shutdown = sc8571_master_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init sc8571_master_subsys_init(void)
{
	int ret = 0;
	pps_err("init start\n");

	if (i2c_add_driver(&sc8571_master_driver) != 0) {
		pps_err(" failed to register sc8571 i2c driver.\n");
	} else {
		pps_err(" Success to register sc8571 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sc8571_master_subsys_init);
#else
int sc8571_master_subsys_init(void)
{
	int ret = 0;
	pps_err("2 init start\n");

	if (i2c_add_driver(&sc8571_master_driver) != 0) {
		pps_err(" failed to register sc8571 i2c driver.\n");
	} else {
		pps_err(" Success to register sc8571 i2c driver.\n");
	}

	return ret;
}

void sc8571_master_subsys_exit(void)
{
	i2c_del_driver(&sc8571_master_driver);
}
#endif

MODULE_AUTHOR("JJ Kong");
MODULE_DESCRIPTION("SC SC8571 Master Charge Pump Driver");
MODULE_LICENSE("GPL v2");
