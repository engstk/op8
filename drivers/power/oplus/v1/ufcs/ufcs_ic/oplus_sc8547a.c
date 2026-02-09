// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[8547a]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
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
#include <uapi/linux/sched/types.h>

#include "../../oplus_charger.h"
#include "../../oplus_gauge.h"
#include "../../oplus_vooc.h"

#include "../../charger_ic/oplus_switching.h"
#include "../../oplus_ufcs.h"
#include "../../oplus_chg_module.h"
#include "../../voocphy/oplus_sc8547.h"
#include "oplus_sc8547a.h"
#include "../oplus_ufcs_protocol.h"
#include "../../voocphy/oplus_voocphy.h"

static struct oplus_sc8547a_ufcs *sc8547a_ufcs = NULL;
static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;

static bool error_reported = false;
extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define I2C_ERR_NUM 10
#define MAIN_I2C_ERROR (1 << 0)

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);
static int sc8547_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg);
static int sc8547_track_upload_cp_err_info(struct oplus_voocphy_manager *chip, int err_type);

static void sc8547_i2c_error(bool happen)
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
static int __sc8547_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sc8547_i2c_error(false);
	*data = (u8)ret;

	return 0;
}

static int __sc8547_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sc8547_i2c_error(false);
	return 0;
}

static int sc8547_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8547_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8547_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8547_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int sc8547_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int sc8547_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg)
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
	curr_time = sc8547_track_get_local_time_s();
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
	pre_upload_time = sc8547_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8547a");
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

static void sc8547_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = NULL;
	chip->i2c_err_uploading = false;
}

static int sc8547_dump_reg_info(struct oplus_voocphy_manager *chip, char *dump_info, int len)
{
	int ret;
	u8 data[2] = { 0, 0 };
	int index = 0;

	if (!chip || !dump_info)
		return 0;

	ret = sc8547_read_byte(chip->client, SC8547_REG_06, &data[0]);
	if (ret < 0) {
		pr_err(" read SC8547_REG_06 failed\n");
		return -EINVAL;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_0F, &data[1]);
	if (ret < 0) {
		pr_err(" read SC8547_REG_0F failed\n");
		return -EINVAL;
	}

	index += snprintf(&(dump_info[index]), len - index, "0x%02x=0x%02x,0x%02x=0x%02x", SC8547_REG_06, data[0],
			  SC8547_REG_0F, data[1]);

	return 0;
}

static int sc8547_track_upload_cp_err_info(struct oplus_voocphy_manager *chip, int err_type)
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
	curr_time = sc8547_track_get_local_time_s();
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
	pre_upload_time = sc8547_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8547a");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_CP_ERR);

	oplus_chg_track_get_cp_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	sc8547_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sc8547_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, cp_err_load_trigger_work);

	if (!chip->cp_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
}

static int sc8547_track_debugfs_init(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sc8547;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_sc8547 = debugfs_create_dir("sc8547", debugfs_root);
	if (!debugfs_sc8547) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_WLS_TRX_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_sc8547, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_sc8547, &(chip->debug_force_cp_err));

	return ret;
}

static int sc8547_track_init(struct oplus_voocphy_manager *chip)
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

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work, sc8547_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work, sc8547_track_cp_err_load_trigger_work);
	rc = sc8547_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("sc8547 debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int sc8547_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sc8547_write_word(chip->client, SC8547_REG_31, val);
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}
	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static int sc8547_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sc8547_write_word(chip->client, SC8547_REG_2C, val);
	if (ret < 0) {
		pr_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int sc8547_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;
	if (!chip) {
		pr_err("chip is null\n");
		return -1;
	}

	data = sc8547_read_word(chip->client, SC8547_REG_2E);

	if (data < 0) {
		pr_err("sc8547_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	pr_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sc8547_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[4] = { 0 };
	int i = 0;
	u8 data = 0;
	s32 ret = 0;

	/*int_flag*/
	sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	chip->int_flag = data;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_19, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read vsys vbat data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vsys = ((data_block[0] << 8) | data_block[1]) * SC8547_VOUT_ADC_LSB;
	chip->cp_vbat = ((data_block[2] << 8) | data_block[3]) * SC8547_VOUT_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read ichg vbus data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_ichg = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	chip->cp_vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) | data_block[3]) * SC8547_VBUS_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_17, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vac error\n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 2; i++) {
		pr_info("read vac data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vac = (((data_block[0] & SC8547_VAC_POL_H_MASK) << 8) | data_block[1]) * SC8547_VAC_ADC_LSB;

	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d cp_vac = "
		"%d int_flag = %d",
		chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->cp_vac, chip->int_flag);
}

static int sc8547_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	int cp_ichg = 0;
	u8 cp_enable = 0;
	s32 ret = 0;

	sc8547_get_chg_enable(chip, &cp_enable);

	if (cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read ichg error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ichg = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;

	return cp_ichg;
}

static int sc8547_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_1B, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	chip->cp_vbat = ((data_block[0] << 8) | data_block[1]) * SC8547_VBAT_ADC_LSB;

	return chip->cp_vbat;
}

static int sc8547_get_cp_vbus(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/* parse data_block for improving time of interrupt */
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_15, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	chip->cp_vbus = (((data_block[0] & SC8547_VBUS_POL_H_MASK) << 8) | data_block[1]) * SC8547_VBUS_ADC_LSB;

	return chip->cp_vbus;
}

/*********************************************************************/
static int sc8547_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = SC8547_RESET_REG;
	else
		val = SC8547_NO_REG_RESET;

	val <<= SC8547_REG_RESET_SHIFT;

	ret = sc8547_update_bits(chip->client, SC8547_REG_07, SC8547_REG_RESET_MASK, val);

	return ret;
}

static bool sc8547a_hw_version_check(struct oplus_voocphy_manager *chip)
{
	int ret;
	u8 val;
	ret = sc8547_read_byte(chip->client, SC8547_REG_36, &val);
	if (val == SC8547A_DEVICE_ID)
		return true;
	else
		return false;
}

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_07, data);
	if (ret < 0) {
		pr_err("SC8547_REG_07\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static int sc8547_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_2B, data);
	if (ret < 0) {
		pr_err("SC8547_REG_2B\n");
		return -1;
	}

	return ret;
}

static void sc8547_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;
	if (!chip) {
		pr_err("!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for (i = 0; i < 37; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, i, &chip->reg_dump[p]);
	}
	for (i = 0; i < 9; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, 43 + i, &chip->reg_dump[p]);
	}
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_36, &chip->reg_dump[p]);
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_3A, &chip->reg_dump[p]);
	pr_err("p[%d], ", p);

	return;
}

static int sc8547_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_11, data);
	if (ret < 0) {
		pr_err("SC8547_REG_11\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static u8 sc8547_match_err_value(struct oplus_voocphy_manager *chip, u8 reg_state, u8 reg_flag)
{
	int err_type = TRACK_CP_ERR_DEFAULT;
	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -EINVAL;
	}

	if (reg_state & SC8547_PIN_DIAG_FALL_FLAG_MASK)
		err_type = TRACK_CP_ERR_CFLY_CDRV_FAULT;
	else if (reg_flag & SC8547_VBAT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBAT_OVP;
	else if (reg_flag & SC8547_IBAT_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBAT_OCP;
	else if (reg_flag & SC8547_VBUS_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS_OVP;
	else if (reg_flag & SC8547_IBUS_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_OCP;

	if (chip->debug_force_cp_err)
		err_type = chip->debug_force_cp_err;
	if (err_type != TRACK_CP_ERR_DEFAULT)
		sc8547_track_upload_cp_err_info(chip, err_type);

	return 0;
}

static u8 sc8547_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;
	u8 state = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	if (ret < 0) {
		pr_err(" read SC8547_REG_0F failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_06, &state);
	if (ret < 0) {
		pr_err(" read SC8547_REG_06 failed\n");
		return -1;
	}
	ufcs_err("REG06 = 0x%x REG0F = 0x%x\n", state, data);

	sc8547_match_err_value(chip, state, data);

	return data;
}

static int sc8547_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}
	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x80);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
}

static void sc8547_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 reg_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}

	if (enable) {
		reg_data = 0x90 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
		sc8547_write_byte(chip->client, SC8547_REG_09, 0x13);
	} else {
		reg_data = 0x10 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &reg_data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return;
	}
	pr_err("pd_svooc config SC8547_REG_05 = %d\n", reg_data);
}

static bool sc8547_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return false;
	}

	pr_err("SC8547_REG_05 = 0x%0x\n", data);

	data = data >> 7;
	if (data == 1)
		return true;
	else
		return false;
}

static int sc8547_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x80);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);
}

static void sc8547_send_handshake(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);
}

static int sc8547_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;

	/* turn off mos */
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	/* sc8547b need disable WDT when exit charging, to avoid after WDT time out.
	IF time out, sc8547b will trigger interrupt frequently.
	in addition, sc8547 and sc8547b WDT will disable when disable CP */
	sc8547_update_bits(chip->client, SC8547_REG_09, SC8547_WATCHDOG_MASK, SC8547_WATCHDOG_DIS);
	/* hwic config with plugout */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);
	reg_data = 0x10 | (chip->ocp_reg & 0xf);

	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);

	/* clear tx data */
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	/* disable vooc phy irq */
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x7f);

	/* set D+ HiZ */
	sc8547_write_byte(chip->client, SC8547_REG_21, 0xc0);

	/* select big bang mode */

	/* disable vooc */
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);

	/* set predata */
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);

	/* mask insert irq */
	sc8547_write_byte(chip->client, SC8547_REG_10, 0x02);
	sc8547_write_byte(chip->client, SC8547A_ADDR_OTG_EN, 0x04);
	pr_err("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int sc8547_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 value;

	/*set predata to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us*/
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (3 << 5);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);

	/*dpdm*/
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	/*clear tx data*/
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	/*vooc*/
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);
	sc8547_send_handshake(chip);

	pr_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int sc8547a_set_ufcs_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}
	if (enable) {
		ret = sc8547_write_byte(chip->client, SC8547_REG_07, 0x80);
		ret = sc8547_write_byte(chip->client, SC8547_REG_09, 0x14);
	} else {
		ret = sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
		ret = sc8547_write_byte(chip->client, SC8547_REG_09, 0x14);
	}
	chg_err("set enable = %d\n", enable);
	if (ret < 0) {
		chg_err("SC8547_REG_07 fail\n");
		return -1;
	}
	return ret;
}

static irqreturn_t sc8547a_charger_interrupt(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;
	struct oplus_ufcs_protocol *chip_protocol = oplus_ufcs_get_protocol_struct();

	if (!chip || !sc8547a_ufcs) {
		return IRQ_HANDLED;
	}
	if (atomic_read(&sc8547a_ufcs->suspended) == 1) {
		ufcs_err("sc8547a is suspend!\n");
		return IRQ_HANDLED;
	}

	if (sc8547a_ufcs->ufcs_enable && chip_protocol) {
		kthread_queue_work(chip_protocol->wq, &chip_protocol->rcv_work);
		return IRQ_HANDLED;
	} else {
		return oplus_voocphy_interrupt_handler(chip);
	}
}

static int sc8547_init_device(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x0); /* ADC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x1);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /* VBUS_OVP:10 2:1 or 1:1V */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00, reg_data); /* VBAT_OVP:4.65V */
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data); /* IBUS_OCP_UCP:3.6A */
	sc8547_write_byte(chip->client, SC8547_REG_01, 0xbf);
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00); /* OOC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	sc8547_update_bits(chip->client, SC8547_REG_09, SC8547_IBUS_UCP_RISE_MASK_MASK,
			   (1 << SC8547_IBUS_UCP_RISE_MASK_SHIFT));
	sc8547_write_byte(chip->client, SC8547_REG_10, 0x02); /* mask insert irq */
	sc8547_write_byte(chip->client, SC8547A_ADDR_OTG_EN, 0x04); /* OTG EN */
	pr_err("sc8547_init_device done");

	return 0;
}

static int sc8547_init_vooc(struct oplus_voocphy_manager *chip)
{
	u8 value;
	pr_err(" >>>>start init vooc\n");

	sc8547_reg_reset(chip, true);
	sc8547_init_device(chip);

	/*to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us */
	/*SET PREDATA */
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	/*sc8547_set_predata(0x0);*/
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (1 << 6);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);
	pr_err("read value %d\n", value);

	/*dpdm */
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	/*vooc */
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);

	return 0;
}

static int sc8547_irq_gpio_init(struct oplus_voocphy_manager *chip)
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

static int sc8547_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	sc8547_irq_gpio_init(chip);
	pr_err(" sc8547 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL, sc8547a_charger_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sc8547_charger_irq", chip);
		if (ret < 0) {
			pr_debug("request irq for irq=%d failed, ret =%d\n", chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	pr_debug("request irq ok\n");

	return ret;
}

static int sc8547_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:12v*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:10v*/
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data); /*IBUS_OCP_UCP:3.6A*/
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x13); /*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x80); /*ADC_CTRL:ADC_EN*/
	sc8547_write_byte(chip->client, SC8547_REG_0D, 0x70);
	/*sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);*/ /*VOOC_CTRL,send handshake*/

	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1); /*Loose_det=1*/
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_05, 0x1c); /*IBUS_OCP_UCP:*/
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x93); /*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x80); /*ADC_CTRL:*/
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1); /*Loose_det*/
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10); /*WD:DISABLE*/

	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00); /*ADC_CTRL:*/
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00); /*VOOC_CTRL*/
	return 0;
}

static int sc8547_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_00, 0x2E); /*VAC_OV:*/
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP*/
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10); /*WD:DISABLE*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00); /*ADC_CTRL*/
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00); /*VOOC_CTRL*/
	pr_err("sc8547_pdqc_hw_setting done");
	return 0;
}

static int sc8547_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sc8547_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		sc8547_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sc8547_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sc8547_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sc8547_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static ssize_t sc8547_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr, val, tmpbuf[300];
	int len = 0, idx = 0, ret = 0;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8547");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if ((addr < 0x24) || (addr > 0x2B && addr < 0x33) || addr == 0x36 || addr == 0x3C) {
			ret = sc8547_read_byte(chip->client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t sc8547_store_register(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		sc8547_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc8547_show_registers, sc8547_store_register);

static void sc8547_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static int sc8547_gpio_init(struct oplus_voocphy_manager *chip)
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

static int sc8547_parse_dt(struct oplus_voocphy_manager *chip)
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
				rc = sc8547_gpio_init(chip);
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

static void sc8547_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sc8547_set_switch_fast_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sc8547_set_switch_normal_charger gpio\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) || IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("%s pinctrl or active or sleep null!\n", __func__);
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1); /* out 1*/

	pr_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8547_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sc8547_set_switch_normal_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sc8547_set_switch_normal_charger gpio\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) || IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("%s pinctrl or active or sleep null!\n", __func__);
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0); /* in 0*/
	}

	pr_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8547_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		sc8547_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		sc8547_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static struct oplus_voocphy_operations oplus_sc8547_ops = {
	.hw_setting = sc8547_hw_setting,
	.init_vooc = sc8547_init_vooc,
	.set_predata = sc8547_set_predata,
	.set_txbuff = sc8547_set_txbuff,
	.get_adapter_info = sc8547_get_adapter_info,
	.update_data = sc8547_update_data,
	.get_chg_enable = sc8547_get_chg_enable,
	.set_chg_enable = sc8547_set_chg_enable,
	.reset_voocphy = sc8547_reset_voocphy,
	.reactive_voocphy = sc8547_reactive_voocphy,
	.set_switch_mode = sc8547_set_switch_mode,
	.send_handshake = sc8547_send_handshake,
	.get_cp_vbat = sc8547_get_cp_vbat,
	.get_cp_vbus = sc8547_get_cp_vbus,
	.get_int_value = sc8547_get_int_value,
	.get_adc_enable = sc8547_get_adc_enable,
	.set_adc_enable = sc8547_set_adc_enable,
	.get_ichg = sc8547_get_cp_ichg,
	.set_pd_svooc_config = sc8547_set_pd_svooc_config,
	.get_pd_svooc_config = sc8547_get_pd_svooc_config,
	.get_voocphy_enable = sc8547_get_voocphy_enable,
	.dump_voocphy_reg = sc8547_dump_reg_in_err_issue,
	.set_ufcs_enable = sc8547a_set_ufcs_enable,
};

static int sc8547_charger_choose(struct oplus_voocphy_manager *chip)
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

static bool sc8547a_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static int sc8547a_read_byte(struct oplus_sc8547a_ufcs *chip, u8 addr, u8 *data)
{
	u8 addr_buf = addr & 0xff;
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, &addr_buf, 1);
	if (rc < 1) {
		ufcs_err("write 0x%04x error, rc = %d \n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	rc = i2c_master_recv(chip->client, data, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc = %d \n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;
error:
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_inc();
	return rc;
}

static int sc8547a_read_data(struct oplus_sc8547a_ufcs *chip, u8 addr, u8 *buf, int len)
{
	u8 addr_buf = addr & 0xff;
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, &addr_buf, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	rc = i2c_master_recv(chip->client, buf, len);
	if (rc < len) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_inc();
	return rc;
}

static int sc8547a_write_byte(struct oplus_sc8547a_ufcs *chip, u8 addr, u8 data)
{
	u8 buf[2] = { addr & 0xff, data };
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, 2);
	if (rc < 2) {
		ufcs_err("write 0x%04x error, rc = %d \n", addr, rc);
		mutex_unlock(&i2c_rw_lock);
		oplus_ufcs_protocol_i2c_err_inc();
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;
}

static int sc8547a_write_data(struct oplus_sc8547a_ufcs *chip, u8 addr, u16 length, u8 *data)
{
	u8 *buf;
	int rc = 0;

	buf = kzalloc(length + 1, GFP_KERNEL);
	if (!buf) {
		ufcs_err("alloc memorry for i2c buffer error\n");
		return -ENOMEM;
	}

	buf[0] = addr & 0xff;
	memcpy(&buf[1], data, length);

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, length + 1);
	if (rc < length + 1) {
		ufcs_err("write 0x%04x error, ret = %d \n", addr, rc);
		mutex_unlock(&i2c_rw_lock);
		kfree(buf);
		oplus_ufcs_protocol_i2c_err_inc();
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	kfree(buf);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;
}

static int sc8547a_write_bit_mask(struct oplus_sc8547a_ufcs *chip, u8 reg, u8 mask, u8 data)
{
	u8 temp = 0;
	int rc = 0;

	rc = sc8547a_read_byte(chip, reg, &temp);
	if (rc < 0)
		return rc;

	temp = (data & mask) | (temp & (~mask));

	rc = sc8547a_write_byte(chip, reg, temp);
	if (rc < 0)
		return rc;

	return 0;
}
static int sc8547a_write_tx_buffer(u8 *buf, u16 len)
{
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return -ENODEV;
	sc8547a_write_byte(chip, SC8547A_ADDR_TX_LENGTH, len);
	sc8547a_write_data(chip, SC8547A_ADDR_TX_BUFFER0, len, buf);
	sc8547a_write_bit_mask(chip, SC8547A_ADDR_UFCS_CTRL0, SC8547A_MASK_SND_CMP, SC8547A_CMD_SND_CMP);
	return 0;
}

static int sc8547a_rcv_msg(struct oplus_ufcs_protocol *protocol_chip)
{
	u8 rx_length = 0;
	int rc = 0;
	struct comm_msg *rcv;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip || !protocol_chip)
		return -ENODEV;

	rcv = &protocol_chip->rcv_msg;

	rc = sc8547a_read_byte(chip, SC8547A_ADDR_RX_LENGTH, &rx_length);
	if (rx_length > SC8547A_LEN_MAX) {
		rx_length = SC8547A_LEN_MAX;
	}
	rcv->len = rx_length;

	sc8547a_read_data(chip, SC8547A_ADDR_RX_BUFFER0, protocol_chip->rcv_buffer, rcv->len);

	return rc;
}

static int sc8547a_retrieve_flags(struct oplus_ufcs_protocol *protocol_chip)
{
	int rc = 0;

	if (!protocol_chip)
		return -ENODEV;
	rc = sc8547_get_int_value(oplus_voocphy_mg);
	return rc;
}

static int sc8547a_read_flags(struct oplus_ufcs_protocol *protocol_chip)
{
	struct ufcs_error_flag *reg;
	int rc = 0;
	u8 flag_buf[SC8547A_FLAG_NUM] = { 0 };
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip || !protocol_chip)
		return -ENODEV;

	reg = &protocol_chip->flag;
	rc = sc8547a_read_data(chip, SC8547A_ADDR_GENERAL_INT_FLAG1, flag_buf, SC8547A_FLAG_NUM);
	if (rc < 0) {
		ufcs_err("failed to read flag register\n");
		protocol_chip->get_flag_failed = 1;
		return -EBUSY;
	}
	memcpy(protocol_chip->reg_dump, flag_buf, sizeof(flag_buf));

	reg->hd_error.dp_ovp = 0;
	reg->hd_error.dm_ovp = 0;
	reg->hd_error.temp_shutdown = 0;
	reg->hd_error.wtd_timeout = 0;
	reg->rcv_error.ack_rcv_timeout = flag_buf[1] & SC8547A_FLAG_ACK_RECEIVE_TIMEOUT;
	reg->hd_error.hardreset = flag_buf[1] & SC8547A_FLAG_HARD_RESET;
	reg->rcv_error.data_rdy = flag_buf[1] & SC8547A_FLAG_DATA_READY;
	reg->rcv_error.sent_cmp = flag_buf[1] & SC8547A_FLAG_SENT_PACKET_COMPLETE;
	reg->commu_error.crc_error = flag_buf[1] & SC8547A_FLAG_CRC_ERROR;
	reg->commu_error.baud_error = flag_buf[1] & SC8547A_FLAG_BAUD_RATE_ERROR;

	reg->commu_error.training_error = flag_buf[2] & SC8547A_FLAG_TRAINING_BYTE_ERROR;
	reg->rcv_error.msg_trans_fail = flag_buf[2] & SC8547A_FLAG_MSG_TRANS_FAIL;
	reg->commu_error.byte_timeout = flag_buf[2] & SC8547A_FLAG_DATA_BYTE_TIMEOUT;
	reg->commu_error.baud_change = flag_buf[2] & SC8547A_FLAG_BAUD_RATE_CHANGE;
	reg->commu_error.rx_len_error = flag_buf[2] & SC8547A_FLAG_LENGTH_ERROR;
	reg->commu_error.rx_overflow = flag_buf[2] & SC8547A_FLAG_RX_OVERFLOW;
	reg->commu_error.bus_conflict = flag_buf[2] & SC8547A_FLAG_BUS_CONFLICT;
	reg->commu_error.start_fail = 0;
	reg->commu_error.stop_error = 0;

	if (protocol_chip->state == STATE_HANDSHAKE) {
		protocol_chip->handshake_success =
			(flag_buf[1] & SC8547A_FLAG_HANDSHAKE_SUCCESS) && !(flag_buf[1] & SC8547A_FLAG_HANDSHAKE_FAIL);
	}
	ufcs_err("[0x%x, 0x%x, 0x%x]\n", flag_buf[0], flag_buf[1], flag_buf[2]);
	protocol_chip->get_flag_failed = 0;
	return 0;
}

static int sc8547a_dump_registers(void)
{
	int rc = 0;
	u8 addr;
	u8 val_buf[6] = { 0x0 };
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;
	if (atomic_read(&chip->suspended) == 1) {
		ufcs_err("sc8547a is suspend!\n");
		return -ENODEV;
	}

	for (addr = 0; addr <= 5; addr++) {
		rc = sc8547a_read_byte(chip, addr, &val_buf[addr]);
		if (rc < 0) {
			ufcs_err("sc8547a_dump_registers Couldn't read 0x%02x rc = %d\n", addr, rc);
			break;
		}
	}
	ufcs_err(":[0~5][0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n", val_buf[0], val_buf[1], val_buf[2], val_buf[3],
		 val_buf[4], val_buf[5]);

	return 0;
}

static int sc8547a_chip_enable(void)
{
	u8 addr_buf[SC8547A_ENABLE_REG_NUM] = { SC8547A_ADDR_DPDM_CTRL,	       SC8547A_ADDR_UFCS_CTRL0,
						SC8547A_ADDR_TXRX_BUFFER_CTRL, SC8547A_ADDR_GENERAL_INT_FLAG1,
						SC8547A_ADDR_UFCS_INT_MASK0,   SC8547A_ADDR_UFCS_INT_MASK1 };
	u8 cmd_buf[SC8547A_ENABLE_REG_NUM] = { SC8547A_CMD_DPDM_EN,	     SC8547A_CMD_EN_CHIP,
					       SC8547A_CMD_CLR_TX_RX,	     SC8547A_CMD_MASK_ACK_DISCARD,
					       SC8547A_CMD_MASK_ACK_TIMEOUT, SC8547A_MASK_TRANING_BYTE_ERROR };
	int i = 0, rc = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (IS_ERR_OR_NULL(chip)) {
		ufcs_err("global pointer sc8547a_ufcs error\n");
		return -ENODEV;
	}

	if (atomic_read(&chip->suspended) == 1) {
		ufcs_err("sc8547a is suspend!\n");
		return -ENODEV;
	}
	if (oplus_voocphy_mg) {
		sc8547_reg_reset(oplus_voocphy_mg, true);
		msleep(10);
		sc8547_init_device(oplus_voocphy_mg);
	}
	for (i = 0; i < SC8547A_ENABLE_REG_NUM; i++) {
		rc = sc8547a_write_byte(chip, addr_buf[i], cmd_buf[i]);
		if (rc < 0) {
			ufcs_err("write i2c failed!\n");
			return rc;
		}
	}
	chip->ufcs_enable = true;
	return 0;
}

static int sc8547a_chip_disable(void)
{
	int rc = 0;
	if (IS_ERR_OR_NULL(sc8547a_ufcs)) {
		ufcs_err("sc8547a_ufcs null \n");
		return -ENODEV;
	}

	rc = sc8547a_write_byte(sc8547a_ufcs, SC8547A_ADDR_UFCS_CTRL0, SC8547A_CMD_DIS_CHIP);
	if (rc < 0) {
		ufcs_err("write i2c failed\n");
		return rc;
	}
	sc8547a_ufcs->ufcs_enable = false;
	return 0;
}

static int sc8547a_handshake(void)
{
	int rc = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return -ENODEV;

	rc = sc8547a_write_bit_mask(chip, SC8547A_ADDR_UFCS_CTRL0, SC8547A_MASK_EN_HANDSHAKE, SC8547A_CMD_EN_HANDSHAKE);
	return rc;
}

static int sc8547a_ping(int baud)
{
	int rc = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return -ENODEV;

	rc = sc8547a_write_bit_mask(chip, SC8547A_ADDR_UFCS_CTRL0, SC8547A_FLAG_BAUD_RATE_VALUE,
				    (baud << SC8547A_FLAG_BAUD_NUM_SHIFT));
	return rc;
}

static int sc8547a_source_hard_reset(void)
{
	int rc = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return -ENODEV;

	rc = sc8547a_write_bit_mask(chip, SC8547A_ADDR_UFCS_CTRL0, SC8547A_SEND_SOURCE_HARDRESET,
				    SC8547A_SEND_SOURCE_HARDRESET);

	return rc;
}

static int sc8547a_cable_hard_reset(void)
{
	int rc = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return -ENODEV;

	return rc;
}

static void sc8547a_notify_baudrate_change(void)
{
	u8 baud_rate = 0;
	struct oplus_sc8547a_ufcs *chip = sc8547a_ufcs;

	if (!chip)
		return;

	sc8547a_read_byte(chip, SC8547A_ADDR_UFCS_CTRL0, &baud_rate);
	baud_rate = baud_rate & SC8547A_FLAG_BAUD_RATE_VALUE;
	ufcs_err("baud_rate change! new value = %d\n", baud_rate);
}

static int sc8547a_hardware_init(struct oplus_sc8547a_ufcs *chip)
{
	return 0;
}

static int sc8547_master_get_vbus(void)
{
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	int cp_ibus = 0, vbus = 0;
	if (!oplus_voocphy_mg)
		return 0;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client, SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ibus = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) | data_block[3]) * SC8547_VBUS_ADC_LSB;

	return vbus;
}

static int sc8547_master_get_ibus(void)
{
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	int cp_ibus = 0, vbus = 0;
	if (!oplus_voocphy_mg)
		return 0;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client, SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ibus = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) | data_block[3]) * SC8547_VBUS_ADC_LSB;

	return cp_ibus;
}

static int sc8547_master_get_vac(void)
{
	int vac = 0;
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	if (!oplus_voocphy_mg)
		return 0;

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client, SC8547_REG_17, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vac error\n");
	} else {
		sc8547_i2c_error(false);
	}

	vac = (((data_block[0] & SC8547_VAC_POL_H_MASK) << 8) | data_block[1]) * SC8547_VAC_ADC_LSB;

	return vac;
}

static int sc8547_master_get_vout(void)
{
	int vout = 0;
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	if (!oplus_voocphy_mg)
		return 0;

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client, SC8547_REG_19, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	vout = ((data_block[0] << 8) | data_block[1]) * 125 / 100;
	return vout;
}

static int sc8547a_parse_dt(struct oplus_sc8547a_ufcs *chip)
{
	struct device_node *node = NULL;

	if (!chip) {
		return -1;
	}
	node = chip->dev->of_node;
	return 0;
}

static void register_ufcs_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int rc = 0;
	char *version;
	char *manufacture;

	version = "sc8547a";
	manufacture = "SouthChip";

	rc = register_device_proc("sc8547a", version, manufacture);
	if (rc)
		ufcs_err("register_ufcs_devinfo fail\n");
#endif
}

static struct regmap_config sc8547a_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = SC8547A_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sc8547a_is_volatile_reg,
};

struct oplus_ufcs_protocol_operations oplus_ufcs_sc8547a_ops = {
	.ufcs_ic_enable = sc8547a_chip_enable,
	.ufcs_ic_disable = sc8547a_chip_disable,
	.ufcs_ic_handshake = sc8547a_handshake,
	.ufcs_ic_ping = sc8547a_ping,
	.ufcs_ic_source_hard_reset = sc8547a_source_hard_reset,
	.ufcs_ic_cable_hard_reset = sc8547a_cable_hard_reset,
	.ufcs_ic_baudrate_change = sc8547a_notify_baudrate_change,
	.ufcs_ic_write_msg = sc8547a_write_tx_buffer,
	.ufcs_ic_rcv_msg = sc8547a_rcv_msg,
	.ufcs_ic_read_flags = sc8547a_read_flags,
	.ufcs_ic_retrieve_flags = sc8547a_retrieve_flags,
	.ufcs_ic_dump_registers = sc8547a_dump_registers,
	.ufcs_ic_get_master_vbus = sc8547_master_get_vbus,
	.ufcs_ic_get_master_ibus = sc8547_master_get_ibus,
	.ufcs_ic_get_master_vac = sc8547_master_get_vac,
	.ufcs_ic_get_master_vout = sc8547_master_get_vout,
};

static int sc8547a_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	struct oplus_sc8547a_ufcs *chip_ic;
	struct oplus_ufcs_protocol *chip_protocol = NULL;
	int ret;

	client->addr = SC8547A_I2C_ADDR;
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	if (!sc8547a_hw_version_check(chip)) {
		pr_err("sc8547 dont use ufcs\n");
		return -ENOMEM;
	}

	ret = sc8547_charger_choose(chip);
	if (ret <= 0)
		return ret;

	sc8547_create_device_node(&(client->dev));

	ret = sc8547_parse_dt(chip);

	sc8547_reg_reset(chip, true);

	sc8547_init_device(chip);

	ret = sc8547_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_sc8547_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;
	sc8547_track_init(chip);

	init_proc_voocphy_debug();

	ufcs_err("sc8547a_parse_dt successfully!\n");
	chip_ic = devm_kzalloc(&client->dev, sizeof(struct oplus_sc8547a_ufcs), GFP_KERNEL);
	if (!chip_ic) {
		ufcs_err("failed to allocate oplus_sc8547a_ufcs\n");
		return -ENOMEM;
	}

	chip_ic->regmap = devm_regmap_init_i2c(client, &sc8547a_regmap_config);
	if (!chip_ic->regmap) {
		ret = -ENODEV;
		goto regmap_init_err;
	}

	chip_ic->dev = &client->dev;
	chip_ic->client = client;
	i2c_set_clientdata(client, chip_ic);
	sc8547a_ufcs = chip_ic;

	chip_protocol = devm_kzalloc(&client->dev, sizeof(struct oplus_ufcs_protocol), GFP_KERNEL);
	if (!chip_protocol) {
		ufcs_err("kzalloc() chip_protocol failed.\n");
		return -ENOMEM;
	}
	chip_protocol->dev = &client->dev;

	sc8547a_hardware_init(chip_ic);
	sc8547a_dump_registers();
	sc8547a_parse_dt(chip_ic);
	chip_ic->ufcs_enable = false;
	oplus_ufcs_ops_register(&oplus_ufcs_sc8547a_ops, UFCS_IC_NAME);
	register_ufcs_devinfo();
	ufcs_err("call end!\n");

	return 0;
regmap_init_err:
	devm_kfree(&client->dev, chip_ic);
	return ret;
err_1:
	pr_err("sc8547 probe err_1\n");
	return ret;
}

static int sc8547a_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc8547a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static int sc8547a_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc8547a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static const struct dev_pm_ops sc8547a_pm_ops = {
	.resume = sc8547a_pm_resume,
	.suspend = sc8547a_pm_suspend,
};

static int sc8547a_driver_remove(struct i2c_client *client)
{
	struct oplus_sc8547a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return -ENODEV;

	devm_kfree(&client->dev, chip);

	return 0;
}

static void sc8547a_shutdown(struct i2c_client *client)
{
	sc8547_write_byte(client, SC8547_REG_11, 0x00);
	sc8547_write_byte(client, SC8547_REG_21, 0x00);
	sc8547a_chip_disable();
	return;
}

static const struct of_device_id sc8547a_match[] = {
	{ .compatible = "oplus,sc8547a-ufcs" },
	{},
};

static const struct i2c_device_id sc8547a_id[] = {
	{ "oplus,sc8547a-ufcs", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc8547a_id);

static struct i2c_driver sc8547a_i2c_driver = {
	.driver =
		{
			.name = "sc8547a-ufcs",
			.owner = THIS_MODULE,
			.of_match_table = sc8547a_match,
			.pm = &sc8547a_pm_ops,
		},
	.probe = sc8547a_driver_probe,
	.remove = sc8547a_driver_remove,
	.id_table = sc8547a_id,
	.shutdown = sc8547a_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sc8547a_i2c_driver);
#else
static __init int sc8547a_i2c_driver_init(void)
{
	return i2c_add_driver(&sc8547a_i2c_driver);
}

static __exit void sc8547a_i2c_driver_exit(void)
{
	i2c_del_driver(&sc8547a_i2c_driver);
}

oplus_chg_module_register(sc8547a_i2c_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC SC8547A VOOCPHY&UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
