// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Opus. All rights reserved.
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
#include <linux/pm_qos.h>
#include "oplus_voocphy.h"
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_hl7138.h"
#include "../charger_ic/oplus_switching.h"
#include "../oplus_chg_module.h"

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;
extern bool oplus_chg_check_pd_svooc_adapater(void);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define HL7138_DEVICE_ID 0x03

static int __hl7138_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*data = (u8)ret;

	return 0;
}

static int __hl7138_slave_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int hl7138_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_slave_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int hl7138_slave_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_slave_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}
#define HL7138_SVOOC_IBUS_FACTOR 110 / 100
#define HL7138_VOOC_IBUS_FACTOR 215 / 100
#define HL7138_REG_NUM_2 2
#define HL7138_REG_ADC_BIT_OFFSET_4 4
#define HL7138_REG_ADC_H_BIT_ICHG 0
#define HL7138_REG_ADC_L_BIT_ICHG 1
#define HL7138_SVOOC_STATUS_OK 2
#define HL7138_VOOC_STATUS_OK 3
#define HL7138_CHG_STATUS_EN 1

static void hl7138_slave_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = { 0 };
	u8 int_flag = 0;
	int data;
	int ret = 0;

	data = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_01, &int_flag);
	if (data < 0) {
		chg_err("hl7138_slave_read_byte faile\n");
		return;
	}

	/* parse data_block for improving time of interrupt
	 * VIN,IIN,VBAT,IBAT,VTS,VOUT,VDIE,;
	 */
	ret = i2c_smbus_read_i2c_block_data(chip->slave_client, HL7138_REG_44, HL7138_REG_NUM_2, data_block);
	if (chip->adapter_type == ADAPTER_SVOOC) {
		chip->slave_cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4) +
				       data_block[HL7138_REG_ADC_L_BIT_ICHG]) *
				      HL7138_SVOOC_IBUS_FACTOR; /* Iin_lbs=1.10mA@CP; */
	} else {
		chip->slave_cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4) +
				       data_block[HL7138_REG_ADC_L_BIT_ICHG]) *
				      HL7138_VOOC_IBUS_FACTOR; /* Iin_lbs=2.15mA@BP; */
	}

	chg_debug("slave_cp_ichg = %d int_flag = %d", chip->slave_cp_ichg, int_flag);
}

/*********************************************************************/
static int hl7138_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_12, data);
	if (ret < 0) {
		chg_err("HL7138_REG_12\n");
		return -1;
	}
	*data = *data >> HL7138_CHG_EN_SHIFT;

	return ret;
}

int hl7138_slave_get_ichg(struct oplus_voocphy_manager *chip)
{
	u8 slave_cp_enable;
	hl7138_slave_update_data(chip);

	hl7138_slave_get_chg_enable(chip, &slave_cp_enable);
	if (slave_cp_enable == 1) {
		return chip->slave_cp_ichg;
	} else {
		return 0;
	}
}

static int hl7138_slave_get_cp_status(struct oplus_voocphy_manager *chip)
{
	u8 data_reg03, data_reg12;
	int ret_reg03, ret_reg12;

	if (!chip) {
		chg_err("Failed\n");
		return 0;
	}

	ret_reg03 = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_03, &data_reg03);
	ret_reg12 = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_12, &data_reg12);
	data_reg03 = data_reg03 >> 6;
	data_reg12 = data_reg12 >> 7;

	if ((data_reg03 == HL7138_SVOOC_STATUS_OK || data_reg03 == HL7138_VOOC_STATUS_OK) &&
	    data_reg12 == HL7138_CHG_STATUS_EN) {
		return 1;
	} else {
		chg_err("%s reg03 = %d data_reg12 = %d\n", __func__, data_reg03, data_reg12);
		return 0;
	}
}

#define HL7138_CHG_EN (HL7138_CHG_ENABLE << HL7138_CHG_EN_SHIFT) /* 1 << 7   0x80 */
#define HL7138_CHG_DIS (HL7138_CHG_DISABLE << HL7138_CHG_EN_SHIFT) /* 0 << 7   0x00 */
#define HL7138_IBUS_UCP_EN (HL7138_IBUS_UCP_ENABLE << HL7138_IBUS_UCP_DIS_SHIFT) /* 1 << 2   0x04 */
#define HL7138_IBUS_UCP_DIS (HL7138_IBUS_UCP_DISABLE << HL7138_IBUS_UCP_DIS_SHIFT) /* 0 << 2   0x00 */
#define HL7138_IBUS_UCP_DEFAULT 0x01

static int hl7138_slave_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	chg_debug("%s enable %d \n", __func__, enable);
	if (enable) {
		return hl7138_slave_write_byte(
			chip->slave_client, HL7138_REG_12,
			HL7138_CHG_EN | HL7138_IBUS_UCP_EN |
				HL7138_IBUS_UCP_DEFAULT); /* is not pdsvooc adapter: enable ucp */
	} else {
		return hl7138_slave_write_byte(chip->slave_client, HL7138_REG_12,
					       HL7138_CHG_DIS | HL7138_IBUS_UCP_EN |
						       HL7138_IBUS_UCP_DEFAULT); /* chg disable */
	}
}

static int hl7138_slave_init_device(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05); /* ADC_CTRL:disable */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88); /* VBUS_OVP=7V,JL:02->0B; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C,
				0x0F); //VBUS_OVP:10.2 2:1 or 1:1V,JL:04-0C; Modify by JL-2023;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_11, 0xEC); /* ovp:90mV */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3C); /* VBAT_OVP:4.56	4.56+0.09*/
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0x32); /* IBUS_OCP:3.5A      ocp:100mA */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0F, 0x60); /* IBUS_OCP:3.5A      ocp:250mA */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_02, 0xE1); /* mask all INT_FLAG */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_10, 0xFC); /* Dis IIN_REG; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_12, 0x05); /* Fsw=500KHz;07->12; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* dis WDG; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C); /* OV=500, UV=250 */

	return 0;
}

static bool hl7138_check_slave_hw_ba_version(struct oplus_voocphy_manager *chip)
{
	int ret;
	u8 val;

	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_00, &val);
	if (ret < 0) {
		chg_err("read hl7138 slave reg0 error\n");
		return false;
	}

	if (val == HL7138_BA_VERSION)
		return true;
	else
		return false;
}

/* turn off system clk for BA version only */
static int hl7138_slave_turnoff_sys_clk(struct oplus_voocphy_manager *chip)
{
	u8 reg_data[2] = { 0 };
	int retry = 0;

	if (!chip) {
		chg_err("slave turn off sys clk failed\n");
		return -1;
	}

	do {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0xF9);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x9F); /* Unlock register */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A3, 0x01);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x00); /* Lock register */

		hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A3, &reg_data[0]);
		hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A0, &reg_data[1]);
		chg_debug("slave 0xA3 = 0x%02x, 0xA0 = 0x%02x\n", reg_data[0], reg_data[1]);

		if ((reg_data[0] == 0x01) && (reg_data[1] == 0x00)) /* Lock register success */
			break;
		mdelay(5);
		retry++;
	} while (retry <= 3);
	chg_debug("hl7138_slave_turnoff_sys_clk done\n");

	return 0;
}

/* turn on system clk for BA version only */
static int hl7138_slave_turnon_sys_clk(struct oplus_voocphy_manager *chip)
{
	u8 reg_data[2] = { 0 };
	int retry = 0;
	int ret = 0;

	if (!chip) {
		chg_err("slave turn on sys clk failed\n");
		return -1;
	}

	do {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0xF9);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x9F); /* Unlock register */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A3, 0x00);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x00); /* Lock register */

		ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A3, &reg_data[0]);
		ret |= hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A0, &reg_data[1]);
		chg_debug("slave 0xA3 = 0x%02x, 0xA0 = 0x%02x\n", reg_data[0], reg_data[1]);

		/* Lock register success */
		if ((reg_data[0] == 0x00) && (reg_data[1] == 0x00) && ret == 0)
			break;
		mdelay(5);
		retry++;
	} while (retry <= 3);

	/* combined operation, let sys_clk return auto mode, current restore to uA level */
	/* force enable adc read average with 4 samples data */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x0D);
	/* soft reset register and disable watchdog */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0xC8);
	mdelay(2);
	chg_debug("hl7138_slave_turnon_sys_clk done\n");

	return 0;
}

int hl7138_slave_init_vooc(struct oplus_voocphy_manager *chip)
{
	chg_debug(">>>> slave init vooc\n");

	hl7138_slave_init_device(chip);

	return 0;
}

static int hl7138_slave_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3C); /* VBAT_OVP:4.65V,JL:00-08; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05); /* ADC_CTRL:ADC_EN,JL:11-40; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_11, 0xEC); /* Disable IIN Regulation*/
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_10, 0xFC); /* Disable VBAT Regulation*/

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88); /* VBUS_OVP:12V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F); /* VIN_OVP:10.2V */

	if (chip->high_curr_setting) {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xB2); /* disable OCP */
	} else {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0x32); /* IBUS_OCP:3.6A,UCP_DEB=5ms */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0F, 0x60); /* IBUS_OCP:3.5A      ocp:250mA */
	}

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* WD:1000ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x00); /* enter cp mode */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C); /* OV=500, UV=250 */

	return 0;
}

static int hl7138_slave_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3C); /* VBAT_OVP:4.65V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05); /* ADC_CTRL:ADC_EN */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88); /* VBUS_OVP=7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F); /* VIN_OVP:5.85v */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF); /* US_OCP:4.6A,(16+9)*0.1+0.1+2=4.6A; */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* WD:1000ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x80); /* bp mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C); /* OV=500, UV=250 */

	return 0;
}

static int hl7138_slave_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3c); /* VBAT_OVP:4.65V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88); /* VBUS_OVP=7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F); /* VIN_OVP:11.7v */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF); /* IBUS_OCP:3.6A,UCP_DEB=5ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* WD:DIS */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x80); /* bp mode; */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x00); /* DC_CTRL:disable */

	return 0;
}

static int hl7138_slave_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3c); /* VBAT_OVP:4.45V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88); /* VBUS_OVP:12V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F); /* VIN_OVP:11.7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF); /* IBUS_OCP:3.6A */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* WD:DIS */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x00); /* enter cp mode */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x00); /* ADC_CTRL:disable */
	return 0;
}

static int hl7138_slave_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		hl7138_slave_init_device(chip);
		chg_debug("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		hl7138_slave_svooc_hw_setting(chip);
		chg_debug("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		hl7138_slave_vooc_hw_setting(chip);
		chg_debug("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		hl7138_slave_5v2a_hw_setting(chip);
		chg_debug("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		hl7138_slave_pdqc_hw_setting(chip);
		chg_debug("SETTING_REASON_PDQC\n");
		break;
	default:
		chg_err("do nothing\n");
		break;
	}
	return 0;
}

static int hl7138_slave_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_set_chg_enable(chip, false);
	hl7138_slave_hw_setting(chip, SETTING_REASON_RESET);

	return VOOCPHY_SUCCESS;
}

static ssize_t hl7138_slave_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "chip");
	for (addr = 0x0; addr <= 0x4F; addr++) {
		if ((addr < 0x18) || (addr > 0x35 && addr < 0x4F)) {
			ret = hl7138_slave_read_byte(chip->slave_client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t hl7138_slave_store_register(struct device *dev, struct device_attribute *attr, const char *buf,
					   size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x4F)
		hl7138_slave_write_byte(chip->slave_client, (unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, hl7138_slave_show_registers, hl7138_slave_store_register);

static void hl7138_slave_create_device_node(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		chg_err("hl7138 create device err!\n");
}

static struct of_device_id hl7138_slave_charger_match_table[] = {
	{
		.compatible = "chip,hl7138-slave",
	},
	{},
};

static struct oplus_voocphy_operations oplus_hl7138_slave_ops = {
	.hw_setting = hl7138_slave_hw_setting,
	.init_vooc = hl7138_slave_init_vooc,
	.update_data = hl7138_slave_update_data,
	.get_chg_enable = hl7138_slave_get_chg_enable,
	.set_chg_enable = hl7138_slave_set_chg_enable,
	.get_ichg = hl7138_slave_get_ichg,
	.reset_voocphy = hl7138_slave_reset_voocphy,
	.get_cp_status = hl7138_slave_get_cp_status,
};

static int hl7138_slave_parse_dt(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_debug("chip null\n");
		return -1;
	}

	return 0;
}

static int hl7138_slave_charger_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;

	chg_debug("hl7138_slave_charger_probe enter!\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->slave_client = client;
	chip->slave_dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	if (oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip null, will do after master cp init.\n");
		return -EPROBE_DEFER;
	}
	hl7138_slave_create_device_node(&(client->dev));

	hl7138_slave_parse_dt(chip);
	hl7138_slave_init_device(chip);

	chip->slave_ops = &oplus_hl7138_slave_ops;

	oplus_voocphy_slave_init(chip);

	oplus_voocphy_get_chip(&oplus_voocphy_mg);

	/* turn on system clk for BA version only */
	if (hl7138_check_slave_hw_ba_version(chip))
		hl7138_slave_turnon_sys_clk(chip);

	chg_debug("hl7138_slave_charger_probe succesfull\n");
	return 0;
}

static void hl7138_slave_charger_shutdown(struct i2c_client *client)
{
	hl7138_slave_write_byte(client, HL7138_REG_40, 0x00); /* disable */
	hl7138_slave_write_byte(client, HL7138_REG_12, 0x25);

	/* turn off system clk for BA version only */
	if (hl7138_check_slave_hw_ba_version(oplus_voocphy_mg))
		hl7138_slave_turnoff_sys_clk(oplus_voocphy_mg);

	chg_err("hl7138_slave_charger_shutdown end\n");

	return;
}

static const struct i2c_device_id hl7138_slave_charger_id[] = {
	{ "hl7138-slave", 0 },
	{},
};

static struct i2c_driver hl7138_slave_charger_driver = {
	.driver		= {
		.name	= "hl7138-charger-slave",
		.owner	= THIS_MODULE,
		.of_match_table = hl7138_slave_charger_match_table,
	},
	.id_table	= hl7138_slave_charger_id,

	.probe		= hl7138_slave_charger_probe,
	.shutdown	= hl7138_slave_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init hl7138_slave_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&hl7138_slave_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_debug(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(hl7138_slave_subsys_init);
#else
int hl7138_slave_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&hl7138_slave_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_debug(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

void hl7138_slave_subsys_exit(void)
{
	i2c_del_driver(&hl7138_slave_charger_driver);
}
oplus_chg_module_register(hl7138_slave_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("Hl7138 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
