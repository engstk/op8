// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[nu2112a]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/ktime.h>
#include <trace/events/sched.h>
#include <uapi/linux/sched/types.h>

#include "../../oplus_charger.h"
#include "../../oplus_gauge.h"
#include "../../oplus_vooc.h"

#include "../../charger_ic/oplus_switching.h"
#include "../../oplus_chg_module.h"
#include "../../voocphy/oplus_voocphy.h"
#include "oplus_nu2112a.h"

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;

static bool error_reported = false;
extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define I2C_ERR_NUM 10
#define MAIN_I2C_ERROR (1 << 0)

static int nu2112a_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);
static int nu2112a_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg);
static int nu2112a_track_upload_cp_err_info(struct oplus_voocphy_manager *chip, int err_type);

const char *nu2112a_adapter_error_info[16] = {
	"adapter output OVP!",
	"adapter outout UVP!",
	"adapter output OCP!",
	"adapter output SCP!",
	"adapter USB OTP!",
	"adapter inside OTP!",
	"adapter CCOVP!",
	"adapter D-OVP!",
	"adapter D+OVP!",
	"adapter input OVP!",
	"adapter input UVP!",
	"adapter drain over current!",
	"adapter input current loss!",
	"adapter CRC error!",
	"adapter watchdog timeout!",
	"invalid msg!",
};

static void nu2112a_i2c_error(bool happen)
{
	int report_flag = 0;
	if (!oplus_voocphy_mg || error_reported)
		return;

	if (happen) {
		oplus_voocphy_mg->voocphy_iic_err = true;
		oplus_voocphy_mg->voocphy_iic_err_num++;
		if (oplus_voocphy_mg->voocphy_iic_err_num >= I2C_ERR_NUM) {
			report_flag |= MAIN_I2C_ERROR;
			oplus_chg_sc8547_error(report_flag, NULL, 0);
			error_reported = true;
		}
	} else {
		oplus_voocphy_mg->voocphy_iic_err_num = 0;
		oplus_chg_sc8547_error(0, NULL, 0);
	}
}

/************************************************************************/
static int __nu2112a_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		nu2112a_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	nu2112a_i2c_error(false);
	*data = (u8)ret;

	return 0;
}

static int __nu2112a_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		nu2112a_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	nu2112a_i2c_error(false);
	return 0;
}

static int nu2112a_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2112a_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2112a_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __nu2112a_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 nu2112a_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		nu2112a_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	nu2112a_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 nu2112a_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		nu2112a_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	nu2112a_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int nu2112a_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int nu2112a_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg)
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
	curr_time = nu2112a_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->debug_force_i2c_err)
		err_type = -chip->debug_force_i2c_err;

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
	pre_upload_time = nu2112a_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "nu2112a");
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

static void nu2112a_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, i2c_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
}

static int nu2112a_dump_reg_info(struct oplus_voocphy_manager *chip, char *dump_info, int len)
{
	int ret;
	u8 data_block[4] = { 0 };
	int index = 0;

	if (!chip || !dump_info)
		return 0;
	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_11, 4, data_block);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a_update_data read vsys vbat error \n");
	} else {
		nu2112a_i2c_error(false);
	}

	index += snprintf(&(dump_info[index]), len - index, "reg[0x%02x ~ 0x%02x] = 0x%02x, 0x%02x, 0x%02x, 0x%02x",
			  NU2112A_REG_11, NU2112A_REG_14, data_block[0], data_block[1], data_block[2], data_block[3]);

	return 0;
}

static int nu2112a_track_upload_cp_err_info(struct oplus_voocphy_manager *chip, int err_type)
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
	curr_time = nu2112a_track_get_local_time_s();
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
	pre_upload_time = nu2112a_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "nu2112a");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_CP_ERR);

	oplus_chg_track_get_cp_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	nu2112a_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void nu2112a_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, cp_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	if (chip->cp_err_load_trigger) {
		kfree(chip->cp_err_load_trigger);
		chip->cp_err_load_trigger = NULL;
	}
	chip->cp_err_uploading = false;
}

static int nu2112a_track_debugfs_init(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_nu2112a;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_nu2112a = debugfs_create_dir("nu2112a", debugfs_root);
	if (!debugfs_nu2112a) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_WLS_TRX_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_nu2112a, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_nu2112a, &(chip->debug_force_cp_err));

	return ret;
}

static int nu2112a_track_init(struct oplus_voocphy_manager *chip)
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

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work, nu2112a_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work, nu2112a_track_cp_err_load_trigger_work);
	rc = nu2112a_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("nu2112a debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int nu2112a_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = nu2112a_write_word(chip->client, NU2112A_REG_31, val);
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}
	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static int nu2112a_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = nu2112a_write_word(chip->client, NU2112A_REG_2C, val);
	if (ret < 0) {
		pr_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int nu2112a_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;
	if (!chip) {
		pr_err("chip is null\n");
		return -1;
	}

	data = nu2112a_read_word(chip->client, NU2112A_REG_2E);

	if (data < 0) {
		pr_err("nu2112a_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	pr_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void nu2112a_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[4] = { 0 };
	int i = 0;
	u8 data = 0;
	s32 ret = 0;

	/*int_flag*/
	nu2112a_read_byte(chip->client, NU2112A_REG_11, &data);
	chip->int_flag = data;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_20, 4,
					    data_block); /*REG20-21 vsys, REG22-23 vbat*/
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a_update_data read vsys vbat error \n");
	} else {
		nu2112a_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read vsys vbat data_block[%d] = %u\n", i, data_block[i]);
	}
	chip->cp_vsys = (((data_block[0] & NU2112A_VOUT_POL_H_MASK) << 8) | data_block[1]) * NU2112A_VOUT_ADC_LSB;
	chip->cp_vbat = (((data_block[2] & NU2112A_VOUT_POL_H_MASK) << 8) | data_block[3]) * NU2112A_VOUT_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_1A, 4,
					    data_block); /*REG1A-1B ibus, REG1C-1D vbus*/
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a_update_data read vsys vbat error \n");
	} else {
		nu2112a_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read ichg vbus data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_ichg = (((data_block[0] & NU2112A_IBUS_POL_H_MASK) << 8) | data_block[1]) * NU2112A_IBUS_ADC_LSB;
	chip->cp_vbus = (((data_block[2] & NU2112A_VBUS_POL_H_MASK) << 8) | data_block[3]) * NU2112A_VBUS_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_1E, 2, data_block); /*REG1E-1F vac*/
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a_update_data read vac error\n");
	} else {
		nu2112a_i2c_error(false);
	}
	for (i = 0; i < 2; i++) {
		pr_info("read vac data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vac = (((data_block[0] & NU2112A_VAC_POL_H_MASK) << 8) | data_block[1]) * NU2112A_VAC_ADC_LSB;

	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d cp_vac = "
		"%d int_flag = %d",
		chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->cp_vac, chip->int_flag);
}

static int nu2112a_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	int cp_ichg = 0;
	u8 cp_enable = 0;
	s32 ret = 0;

	nu2112a_get_chg_enable(chip, &cp_enable);

	if (cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_1A, 2, data_block);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a read ichg error \n");
	} else {
		nu2112a_i2c_error(false);
	}

	cp_ichg = (((data_block[0] & NU2112A_IBUS_POL_H_MASK) << 8) | data_block[1]) * NU2112A_IBUS_ADC_LSB;

	return cp_ichg;
}

static int nu2112a_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_22, 2, data_block);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a read vbat error \n");
	} else {
		nu2112a_i2c_error(false);
	}

	chip->cp_vbat = (((data_block[0] & NU2112A_VBAT_POL_H_MASK) << 8) | data_block[1]) * NU2112A_VBAT_ADC_LSB;

	return chip->cp_vbat;
}

static int nu2112a_get_cp_vbus(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/* parse data_block for improving time of interrupt */
	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_1C, 2, data_block);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a read vbat error \n");
	} else {
		nu2112a_i2c_error(false);
	}

	chip->cp_vbus = (((data_block[0] & NU2112A_VBUS_POL_H_MASK) << 8) | data_block[1]) * NU2112A_VBUS_ADC_LSB;

	return chip->cp_vbus;
}

/*********************************************************************/
static int nu2112a_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = NU2112A_RESET_REG;
	else
		val = NU2112A_NO_REG_RESET;

	val <<= NU2112A_REG_RESET_SHIFT;

	ret = nu2112a_update_bits(chip->client, NU2112A_REG_07, NU2112A_REG_RESET_MASK, val);

	return ret;
}

static int nu2112a_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = nu2112a_read_byte(chip->client, NU2112A_REG_07, data);
	if (ret < 0) {
		pr_err("NU2112A_REG_07\n");
		return -1;
	}

	*data = *data >> NU2112A_CHG_EN_SHIFT;

	return ret;
}

static int nu2112a_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = nu2112a_read_byte(chip->client, NU2112A_REG_2B, data);
	if (ret < 0) {
		pr_err("NU2112A_REG_2B\n");
		return -1;
	}

	return ret;
}

static void nu2112a_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;
	if (!chip) {
		pr_err("!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for (i = 0; i < 40; i++) {
		p = p + 1;
		nu2112a_read_byte(chip->client, i, &chip->reg_dump[p]);
	}
	pr_err("p[%d], ", p);

	return;
}

static int nu2112a_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = nu2112a_read_byte(chip->client, NU2112A_REG_18, data);
	if (ret < 0) {
		pr_err("NU2112A_REG_18\n");
		return -1;
	}

	*data = *data >> NU2112A_ADC_ENABLE_SHIFT;

	return ret;
}

static u8 nu2112a_match_err_value(struct oplus_voocphy_manager *chip, u8 data_block[4])
{
	int err_type = TRACK_CP_ERR_DEFAULT;
	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -EINVAL;
	}
	if (data_block[0] & NU2112A_VBAT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBAT_OVP;
	else if (data_block[0] & NU2112A_IBAT_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBAT_OCP;
	else if (data_block[0] & NU2112A_VBUS_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS_OVP;
	else if (data_block[0] & NU2112A_IBUS_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_OCP;
	else if (data_block[1] & NU2112A_TSD_FLAG_MASK)
		err_type = TRACK_CP_ERR_TSD;
	else if (data_block[1] & NU2112A_PMID2VOUT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_PMID2OUT_OVP;
	else if (data_block[1] & NU2112A_PMID2VOUT_UVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_PMID2OUT_UVP;
	else if (data_block[2] & NU2112A_SS_TIMEOUT_FLAG_MASK)
		err_type = TRACK_CP_ERR_SS_TIMEOUT;
	else if (data_block[2] & NU2112A_PIN_DIAG_FALL_FLAG_MASK)
		err_type = TRACK_CP_ERR_DIAG_FAIL;

	if (chip->debug_force_cp_err)
		err_type = chip->debug_force_cp_err;
	if (err_type != TRACK_CP_ERR_DEFAULT)
		nu2112a_track_upload_cp_err_info(chip, err_type);

	return 0;
}

static u8 nu2112a_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;
	u8 data_block[4] = { 0 };
	int i = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}
	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, NU2112A_REG_11, 4, data_block);
	if (ret < 0) {
		nu2112a_i2c_error(true);
		pr_err("nu2112a_get_int_value read vac error\n");
	} else {
		nu2112a_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read int data_block[%d] = %u\n", i, data_block[i]);
	}

	nu2112a_match_err_value(chip, data_block);

	return data;
}

static int nu2112a_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}
	if (enable)
		return nu2112a_write_byte(chip->client, NU2112A_REG_07, 0x82); /*Enable CP, 500KHz*/
	else
		return nu2112a_write_byte(chip->client, NU2112A_REG_07, 0x2); /*Disable CP*/
}

static void nu2112a_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 reg_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}

	if (enable) {
		reg_data = 0x81;
		nu2112a_write_byte(chip->client, NU2112A_REG_0C, 0x81); /*Enable IBUS_UCP*/
		nu2112a_write_byte(chip->client, NU2112A_REG_17, 0x28); /*IBUS_UCP_RISE_MASK*/
		nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x03); /*WD=1000ms*/
	} else {
		reg_data = 0x00;
		nu2112a_write_byte(chip->client, NU2112A_REG_0C, 0x00);
	}

	ret = nu2112a_read_byte(chip->client, NU2112A_REG_0C, &reg_data);
	if (ret < 0) {
		pr_err("NU2112A_REG_0C\n");
		return;
	}
	pr_err("pd_svooc config NU2112A_REG_0C = %d\n", reg_data);
}

static bool nu2112a_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = nu2112a_read_byte(chip->client, NU2112A_REG_0C, &data);
	if (ret < 0) {
		pr_err("NU2112A_REG_0C\n");
		return false;
	}

	pr_err("NU2112A_REG_0C = 0x%0x\n", data);

	data = data >> NU2112A_IBUS_UCP_DIS_SHIFT;
	if (data == NU2112A_IBUS_UCP_DISABLE)
		return true;
	else
		return false;
}

static int nu2112a_set_chg_pmid2out(bool enable)
{
	if (!oplus_voocphy_mg)
		return 0;

	if (enable)
		return nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_05, 0x31); /*PMID/2-VOUT < 10%VOUT*/
	else
		return nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_05, 0xB1); /*PMID/2-VOUT < 10%VOUT*/
}

static bool nu2112a_get_chg_pmid2out(void)
{
	int ret = 0;
	u8 data = 0;

	if (!oplus_voocphy_mg) {
		pr_err("Failed\n");
		return false;
	}

	ret = nu2112a_read_byte(oplus_voocphy_mg->client, NU2112A_REG_05, &data);
	if (ret < 0) {
		pr_err("NU2112A_REG_05\n");
		return false;
	}

	pr_err("NU2112A_REG_05 = 0x%0x\n", data);

	data = data >> NU2112A_PMID2OUT_OVP_DIS_SHIFT;
	if (data == NU2112A_PMID2OUT_OVP_ENABLE)
		return true;
	else
		return false;
}

static int nu2112a_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x90); /*Enable ADC*/
	else
		return nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x10); /*Disable ADC*/
}

static void nu2112a_send_handshake(struct oplus_voocphy_manager *chip)
{
	nu2112a_write_byte(chip->client, NU2112A_REG_2B, 0x81);
}

static int nu2112a_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;

	/* turn off mos */
	nu2112a_write_byte(chip->client, NU2112A_REG_07, 0x02);
	/* hwic config with plugout */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	nu2112a_write_byte(chip->client, NU2112A_REG_00, 0x5c); /*vbat ovp=4.65V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x07); /*vac ovp=6.5V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /*VBUS OVP=9.7V*/
	reg_data = 0x10 | (chip->ocp_reg & 0xf);

	nu2112a_write_byte(chip->client, NU2112A_REG_04, 0x1f); /*IBUS OCP=3.6A*/
	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x00); /*WD disable, cp 2:1*/
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x10); /*ADC Disable*/

	/* clear tx data */
	nu2112a_write_byte(chip->client, NU2112A_REG_2C, 0x00); /*WDATA write 0*/
	nu2112a_write_byte(chip->client, NU2112A_REG_2D, 0x00);

	/* disable vooc phy irq */
	nu2112a_write_byte(chip->client, NU2112A_REG_30, 0x7f); /*VOOC MASK*/

	/* set D+ HiZ
  nu2112a_write_byte(chip->client, SC8547_REG_21, 0xc0); //delete this sentence
  */

	/* select big bang mode */

	/* disable vooc */
	nu2112a_write_byte(chip->client, NU2112A_REG_2B, 0x00);

	/* set predata */
	nu2112a_write_word(chip->client, NU2112A_REG_31, 0x0);

	/* mask insert irq */
	nu2112a_write_byte(chip->client, NU2112A_REG_15, 0x02);
	pr_err("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int nu2112a_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 value;

	/*set predata to avoid cmd of adjust current(0x01)return error, add voocphy
   * bit0 hold time to 800us*/
	nu2112a_write_word(chip->client, NU2112A_REG_31, 0x0);
	nu2112a_read_byte(chip->client, NU2112A_REG_3A, &value);
	value = value | (3 << 5);
	nu2112a_write_byte(chip->client, NU2112A_REG_3A, value);

	/*dpdm*/
	nu2112a_write_byte(chip->client, NU2112A_REG_21, 0x21);
	nu2112a_write_byte(chip->client, NU2112A_REG_22, 0x00);
	nu2112a_write_byte(chip->client, NU2112A_REG_33, 0xD1);

	/*clear tx data*/
	nu2112a_write_byte(chip->client, NU2112A_REG_2C, 0x00);
	nu2112a_write_byte(chip->client, NU2112A_REG_2D, 0x00);

	/*vooc*/
	nu2112a_write_byte(chip->client, NU2112A_REG_30, 0x05);
	nu2112a_send_handshake(chip);

	pr_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static irqreturn_t nu2112a_charger_interrupt(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return oplus_voocphy_interrupt_handler(chip);
}

static int nu2112a_init_device(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x10); /* ADC_CTRL:disable */
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x7); /*VAC OVP=6.5V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /* VBUS_OVP:9.7V */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	nu2112a_write_byte(chip->client, NU2112A_REG_00, 0x5C); /* VBAT_OVP:4.65V */
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	nu2112a_write_byte(chip->client, NU2112A_REG_04, 0x1f); /* IBUS_OCP_UCP:3.6A */
	nu2112a_write_byte(chip->client, NU2112A_REG_0D, 0x03); /* IBUS UCP Falling =150ms */
	nu2112a_write_byte(chip->client, NU2112A_REG_0C, 0x41); /* IBUS UCP 250ma Falling,500ma Rising */

	nu2112a_write_byte(chip->client, NU2112A_REG_01, 0xa8); /*IBAT OCP Disable*/
	nu2112a_write_byte(chip->client, NU2112A_REG_2B, 0x00); /* VOOC_CTRL:disable */
	nu2112a_write_byte(chip->client, NU2112A_REG_35, 0x20); /*VOOC Option2*/
	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x0); /*VOOC Option2*/
	nu2112a_write_byte(chip->client, NU2112A_REG_17, 0x28); /*REG_17=0x28, IBUS_UCP_RISE_MASK_MASK*/
	nu2112a_write_byte(chip->client, NU2112A_REG_15, 0x02); /* mask insert irq */
	pr_err("nu2112a_init_device done");

	return 0;
}

static int nu2112a_init_vooc(struct oplus_voocphy_manager *chip)
{
	pr_err(" >>>>start init vooc\n");

	nu2112a_reg_reset(chip, true);
	nu2112a_init_device(chip);

	/*to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time
   * to 800us */
	/*SET PREDATA */
	nu2112a_write_word(chip->client, NU2112A_REG_31, 0x0);
	/*nu2112a_set_predata(0x0);*/
	nu2112a_write_byte(chip->client, NU2112A_REG_35, 0x20);

	/*dpdm */
	nu2112a_write_byte(chip->client, NU2112A_REG_33, 0xD1);

	/*vooc */
	nu2112a_write_byte(chip->client, NU2112A_REG_30, 0x05);

	return 0;
}

static int nu2112a_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node, "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		pr_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio, "irq_gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->irq_gpio);
			}
		}
		pr_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	chip->irq = gpio_to_irq(chip->irq_gpio);
	pr_err("irq chip->irq = %d\n", chip->irq);

	/* set voocphy pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active = pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep = pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	pr_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int nu2112a_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	nu2112a_irq_gpio_init(chip);
	pr_err(" nu2112a chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL, nu2112a_charger_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "nu2112a_charger_irq", chip);
		if (ret < 0) {
			pr_debug("request irq for irq=%d failed, ret =%d\n", chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	pr_debug("request irq ok\n");

	return ret;
}

int nu2112a_clk_err_clean(void)
{
	if (!oplus_voocphy_mg)
		return 0;

	nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_2B, 0x02); /*reset vooc*/
	nu2112a_write_word(oplus_voocphy_mg->client, NU2112A_REG_31, 0x0);
	nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_35, 0x20); /*dpdm */
	nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_33, 0xD1);
	/*vooc */
	nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_30, 0x05);
	msleep(15);
	nu2112a_write_byte(oplus_voocphy_mg->client, NU2112A_REG_2B, 0x80); /*enable phy*/

	pr_err("nu2112a_clk_err_clean done");
	return 0;
}

static int nu2112a_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x04); /*VAC_OVP:12v*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7v*/
	nu2112a_write_byte(chip->client, NU2112A_REG_04, 0x1F); /*IBUS_OCP_UCP:3.6A*/
	nu2112a_write_byte(chip->client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x03); /*WD:1000ms*/
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	nu2112a_write_byte(chip->client, NU2112A_REG_05, 0xB1); /*PMID/2-VOUT < 10%VOUT*/

	nu2112a_write_byte(chip->client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_write_byte(chip->client, NU2112A_REG_35, 0x20);
	return 0;
}

static int nu2112a_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x06); /*VAC_OVP:7V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_04, 0x2B); /*IBUS_OCP_UCP:4.8A*/
	nu2112a_write_byte(chip->client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x84); /*WD:1000ms*/
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	nu2112a_write_byte(chip->client, NU2112A_REG_05, 0xB1); /*PMID/2-VOUT < 10%VOUT*/
	nu2112a_write_byte(chip->client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_write_byte(chip->client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/

	return 0;
}

static int nu2112a_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x06); /*VAC_OVP:7V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_04, 0x1F); /*IBUS_OCP_UCP:3.6A*/
	nu2112a_write_byte(chip->client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x84); /*WD:1000ms*/
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	nu2112a_write_byte(chip->client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_write_byte(chip->client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/

	return 0;
}

static int nu2112a_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_write_byte(chip->client, NU2112A_REG_02, 0x04); /*VAC_OVP:12V*/
	nu2112a_write_byte(chip->client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/

	nu2112a_write_byte(chip->client, NU2112A_REG_08, 0x00); /*WD:1000ms*/
	nu2112a_write_byte(chip->client, NU2112A_REG_18, 0x10); /*ADC_CTRL:ADC_EN*/
	nu2112a_write_byte(chip->client, NU2112A_REG_2B, 0x00); /*DISABLE VOOCPHY*/

	pr_err("nu2112a_pdqc_hw_setting done");
	return 0;
}

static int nu2112a_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		nu2112a_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		nu2112a_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		nu2112a_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		nu2112a_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		nu2112a_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static ssize_t nu2112a_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "nu2112a");
	for (addr = 0x0; addr <= 0x38; addr++) {
		ret = nu2112a_read_byte(chip->client, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t nu2112a_store_register(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x38)
		nu2112a_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, nu2112a_show_registers, nu2112a_store_register);

static void nu2112a_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static int nu2112a_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_high = pinctrl_lookup_state(chip->pinctrl, "switch1_act_switch2_act");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("get switch1_act_switch2_act fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_low = pinctrl_lookup_state(chip->pinctrl, "switch1_sleep_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("get switch1_sleep_switch2_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);

	chip->slave_charging_inter_default = pinctrl_lookup_state(chip->pinctrl, "slave_charging_inter_default");
	if (IS_ERR_OR_NULL(chip->slave_charging_inter_default)) {
		chg_err("get slave_charging_inter_default fail\n");
	} else {
		pinctrl_select_state(chip->pinctrl, chip->slave_charging_inter_default);
	}

	printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip is ready!\n", __func__);
	return 0;
}

static int nu2112a_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = NULL;

	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio = of_get_named_gpio(node, "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		pr_debug("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio, "charging-switch1-gpio");
			if (rc) {
				pr_debug("unable to request gpio [%d]\n", chip->switch1_gpio);
			} else {
				rc = nu2112a_gpio_init(chip);
				if (rc)
					chg_err("unable to init "
						"charging_sw_ctrl2-gpio:%d\n",
						chip->switch1_gpio);
			}
		}
		pr_debug("chip->switch1_gpio =%d\n", chip->switch1_gpio);
	}

	rc = of_property_read_u32(node, "ovp_reg", &chip->ovp_reg);
	if (rc) {
		chip->ovp_reg = 0xE;
	} else {
		chg_err("ovp_reg is %d\n", chip->ovp_reg);
	}

	rc = of_property_read_u32(node, "ocp_reg", &chip->ocp_reg);
	if (rc) {
		chip->ocp_reg = 0x8;
	} else {
		chg_err("ocp_reg is %d\n", chip->ocp_reg);
	}

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_low", &chip->voocphy_vbus_low);
	if (rc) {
		chip->voocphy_vbus_low = DEFUALT_VBUS_LOW;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_low);

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_high", &chip->voocphy_vbus_high);
	if (rc) {
		chip->voocphy_vbus_high = DEFUALT_VBUS_HIGH;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_high);

	return 0;
}

static void nu2112a_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("nu2112a_set_switch_fast_charger chip null\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1); /* out 1*/
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	pr_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void nu2112a_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("nu2112a_set_switch_normal_charger chip null\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0); /* in 0*/
	}
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	pr_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void nu2112a_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		nu2112a_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		nu2112a_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static int nu2112a_get_chip_id(void)
{
	return CHIP_ID_NU2112A;
}

static struct oplus_voocphy_operations oplus_nu2112a_ops = {
	.hw_setting = nu2112a_hw_setting,
	.init_vooc = nu2112a_init_vooc,
	.set_predata = nu2112a_set_predata,
	.set_txbuff = nu2112a_set_txbuff,
	.get_adapter_info = nu2112a_get_adapter_info,
	.update_data = nu2112a_update_data,
	.get_chg_enable = nu2112a_get_chg_enable,
	.set_chg_enable = nu2112a_set_chg_enable,
	.reset_voocphy = nu2112a_reset_voocphy,
	.reactive_voocphy = nu2112a_reactive_voocphy,
	.set_switch_mode = nu2112a_set_switch_mode,
	.send_handshake = nu2112a_send_handshake,
	.get_cp_vbat = nu2112a_get_cp_vbat,
	.get_cp_vbus = nu2112a_get_cp_vbus,
	.get_int_value = nu2112a_get_int_value,
	.get_adc_enable = nu2112a_get_adc_enable,
	.set_adc_enable = nu2112a_set_adc_enable,
	.get_ichg = nu2112a_get_cp_ichg,
	.set_pd_svooc_config = nu2112a_set_pd_svooc_config,
	.get_pd_svooc_config = nu2112a_get_pd_svooc_config,
	.get_voocphy_enable = nu2112a_get_voocphy_enable,
	.dump_voocphy_reg = nu2112a_dump_reg_in_err_issue,
	.get_chip_id = nu2112a_get_chip_id,
	.set_chg_pmid2out = nu2112a_set_chg_pmid2out,
	.get_chg_pmid2out = nu2112a_get_chg_pmid2out,
	.clk_err_clean = nu2112a_clk_err_clean,
};

static int nu2112a_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x07);
		pr_err("0x07 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		} else
			return 1;
	}
}

static void register_ufcs_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int rc = 0;
	char *version;
	char *manufacture;

	version = "nu2112a";
	manufacture = "SouthChip";

	rc = register_device_proc("nu2112a", version, manufacture);
	if (rc)
		chg_err("register_ufcs_devinfo fail\n");
#endif
}

static int nu2112a_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);
	mutex_init(&chip->voocphy_pinctrl_mutex);

	i2c_set_clientdata(client, chip);

	ret = nu2112a_charger_choose(chip);
	if (ret <= 0)
		return ret;

	nu2112a_create_device_node(&(client->dev));

	ret = nu2112a_parse_dt(chip);

	nu2112a_reg_reset(chip, true);

	nu2112a_init_device(chip);

	ret = nu2112a_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_nu2112a_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;
	nu2112a_track_init(chip);

	init_proc_voocphy_debug();

	register_ufcs_devinfo();
	chg_err("call end!\n");

	return 0;
err_1:
	pr_err("nu2112a probe err_1\n");
	return ret;
}

static int nu2112a_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	return 0;
}

static int nu2112a_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	return 0;
}

static const struct dev_pm_ops nu2112a_pm_ops = {
	.resume = nu2112a_pm_resume,
	.suspend = nu2112a_pm_suspend,
};

static int nu2112a_driver_remove(struct i2c_client *client)
{
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return -ENODEV;

	devm_kfree(&client->dev, chip);

	return 0;
}

static void nu2112a_shutdown(struct i2c_client *client)
{
	nu2112a_write_byte(client, NU2112A_REG_18, 0x10);
	return;
}

static const struct of_device_id nu2112a_match[] = {
	{ .compatible = "oplus,nu2112a-ufcs" },
	{},
};

static const struct i2c_device_id nu2112a_id[] = {
	{ "oplus,nu2112a-ufcs", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, nu2112a_id);

static struct i2c_driver nu2112a_i2c_driver = {
	.driver =
		{
			.name = "nu2112a-ufcs",
			.owner = THIS_MODULE,
			.of_match_table = nu2112a_match,
			.pm = &nu2112a_pm_ops,
		},
	.probe = nu2112a_driver_probe,
	.remove = nu2112a_driver_remove,
	.id_table = nu2112a_id,
	.shutdown = nu2112a_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init nu2112a_i2c_driver_init(void)
{
	int ret = 0;
	chg_err(" init start\n");

	if (i2c_add_driver(&nu2112a_i2c_driver) != 0) {
		chg_err(" failed to register nu2112a i2c driver.\n");
	} else {
		chg_err(" Success to register nu2112a i2c driver.\n");
	}

	return ret;
}

subsys_initcall(nu2112a_i2c_driver_init);
#else
static __init int nu2112a_i2c_driver_init(void)
{
	return i2c_add_driver(&nu2112a_i2c_driver);
}

static __exit void nu2112a_i2c_driver_exit(void)
{
	i2c_del_driver(&nu2112a_i2c_driver);
}

oplus_chg_module_register(nu2112a_i2c_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC NU2112A MASTER VOOCPHY&UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
