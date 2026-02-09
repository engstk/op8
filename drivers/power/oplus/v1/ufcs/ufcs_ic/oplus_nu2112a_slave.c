// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Opus. All rights reserved.
 */

#define pr_fmt(fmt) "[nu2112a] %s: " fmt, __func__

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
#include "../../oplus_vooc.h"
#include "../../oplus_gauge.h"
#include "../../oplus_charger.h"
#include "../../oplus_ufcs.h"
#include "../../oplus_chg_module.h"
#include "oplus_nu2112a.h"
#include "../../voocphy/oplus_voocphy.h"

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;
static bool error_reported = false;

extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);
static int nu2112a_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);

#define I2C_ERR_NUM 10
#define SLAVE_I2C_ERROR (1 << 1)

static void nu2112a_slave_i2c_error(bool happen)
{
	int report_flag = 0;
	if (!oplus_voocphy_mg || error_reported)
		return;

	if (happen) {
		oplus_voocphy_mg->slave_voocphy_iic_err = 1;
		oplus_voocphy_mg->slave_voocphy_iic_err_num++;
		if (oplus_voocphy_mg->slave_voocphy_iic_err_num >= I2C_ERR_NUM) {
			report_flag |= SLAVE_I2C_ERROR;
			oplus_chg_sc8547_error(report_flag, NULL, 0);
			error_reported = true;
		}
	} else {
		oplus_voocphy_mg->slave_voocphy_iic_err_num = 0;
		oplus_chg_sc8547_error(0, NULL, 0);
	}
}

/************************************************************************/
static int __nu2112a_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		nu2112a_slave_i2c_error(true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	nu2112a_slave_i2c_error(false);
	*data = (u8)ret;

	return 0;
}

static int __nu2112a_slave_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		nu2112a_slave_i2c_error(true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		return ret;
	}
	nu2112a_slave_i2c_error(false);

	return 0;
}

static int nu2112a_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_slave_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2112a_slave_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_slave_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2112a_slave_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2112a_slave_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __nu2112a_slave_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static void nu2112a_slave_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	int i = 0;
	u8 int_flag = 0;
	s32 ret = 0;

	nu2112a_slave_read_byte(chip->slave_client, NU2112A_REG_11, &int_flag);

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->slave_client, NU2112A_REG_1A, 2, data_block);
	if (ret < 0) {
		nu2112a_slave_i2c_error(true);
		pr_err("nu2112a_update_data slave read error \n");
	} else {
		nu2112a_slave_i2c_error(false);
	}
	for (i = 0; i < 2; i++) {
		pr_info("data_block[%d] = %u\n", i, data_block[i]);
	}
	chip->slave_cp_ichg = ((data_block[0] << 8) | data_block[1]) * NU2112A_IBUS_ADC_LSB;
	pr_info("slave cp_ichg = %d int_flag = %d", chip->slave_cp_ichg, int_flag);
}
/*********************************************************************/
int nu2112a_slave_get_ichg(struct oplus_voocphy_manager *chip)
{
	u8 slave_cp_enable;
	nu2112a_slave_update_data(chip);

	nu2112a_slave_get_chg_enable(chip, &slave_cp_enable);
	if (chip->slave_ops) {
		if (slave_cp_enable == 1)
			return chip->slave_cp_ichg;
		else
			return 0;
	} else {
		return 0;
	}
}

static int nu2112a_slave_get_cp_status(struct oplus_voocphy_manager *chip)
{
	u8 data_reg07, data_reg10;
	int ret_reg07, ret_reg10;

	if (!chip) {
		pr_err("Failed\n");
		return 0;
	}

	ret_reg07 = nu2112a_slave_read_byte(chip->slave_client, NU2112A_REG_07, &data_reg07);
	ret_reg10 = nu2112a_slave_read_byte(chip->slave_client, NU2112A_REG_10, &data_reg10);

	if (ret_reg07 < 0 || ret_reg10 < 0) {
		pr_err("NU2112A_REG_07 or NU2112A_REG_10 err\n");
		return 0;
	}
	data_reg07 = data_reg07 >> 7;
	data_reg10 = data_reg10 & NU2112A_CP_SWITCHING_STAT_MASK;

	data_reg10 = data_reg10 >> NU2112A_CP_SWITCHING_STAT_SHIFT;

	pr_err("11 reg07 = %d reg10 = %d\n", data_reg07, data_reg10);

	if (data_reg07 == 1 && data_reg10 == 1) {
		return 1;
	} else {
		return 0;
	}
}

static int nu2112a_slave_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = NU2112A_RESET_REG;
	else
		val = NU2112A_NO_REG_RESET;

	val <<= NU2112A_REG_RESET_SHIFT;

	ret = nu2112a_slave_update_bits(chip->slave_client, NU2112A_REG_07, NU2112A_REG_RESET_MASK, val);

	return ret;
}

static int nu2112a_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = nu2112a_slave_read_byte(chip->slave_client, NU2112A_REG_07, data);
	if (ret < 0) {
		pr_err("NU2112A_REG_1A\n");
		return -1;
	}
	*data = *data >> NU2112A_CHG_EN_SHIFT;

	return ret;
}

static int nu2112a_slave_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	u8 value = 0x8A;
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		value = 0x8A; /*Enable CP,550KHz*/
	else
		value = 0x0A; /*Disable CP,550KHz*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_07, value);
	pr_err(" enable  = %d, value = 0x%x!\n", enable, value);
	return 0;
}

static int nu2112a_slave_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = nu2112a_slave_read_byte(chip->slave_client, NU2112A_REG_2B, data);
	if (ret < 0) {
		pr_err("NU2112A_REG_2B\n");
		return -1;
	}

	return ret;
}

static int nu2112a_slave_set_chg_pmid2out(bool enable)
{
	if (!oplus_voocphy_mg)
		return 0;

	pr_err("-----------nu2112a_set_chg_pmid2out\n");

	if (enable)
		return nu2112a_slave_write_byte(oplus_voocphy_mg->slave_client, NU2112A_REG_05,
						0x31); /*PMID/2-VOUT < 10%VOUT*/
	else
		return nu2112a_slave_write_byte(oplus_voocphy_mg->slave_client, NU2112A_REG_05,
						0xB1); /*PMID/2-VOUT < 10%VOUT*/
}

static bool nu2112a_slave_get_chg_pmid2out(void)
{
	int ret = 0;
	u8 data = 0;

	if (!oplus_voocphy_mg) {
		pr_err("Failed\n");
		return false;
	}

	ret = nu2112a_slave_read_byte(oplus_voocphy_mg->slave_client, NU2112A_REG_05, &data);
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

static void nu2112a_slave_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;
	//u8 value[DUMP_REG_CNT] = {0};
	if (!chip) {
		pr_err("!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for (i = 0; i < 40; i++) {
		p = p + 1;
		nu2112a_slave_read_byte(chip->slave_client, i, &chip->slave_reg_dump[p]);
	}
	pr_err("p[%d], ", p);
	return;
}

static int nu2112a_slave_init_device(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_18, 0x10); /* ADC_CTRL:disable */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_02, 0x7); /*VAC OVP=6.5V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_03, 0x48); /* VBUS_OVP:9.7V */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	//nu2112a_write_byte(chip->client, SC8547_REG_00, reg_data); /* VBAT_OVP:4.65V */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_00, 0x5C); /* VBAT_OVP:4.65V */
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	//nu2112a_write_byte(chip->client, SC8547_REG_05, reg_data); /* IBUS_OCP_UCP:3.6A */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_04, 0x1f); /* IBUS_OCP_UCP:3.6A */
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x81); /* IBUS UCP Falling =5ms */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x03); /* IBUS UCP Falling =150ms */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0C, 0x41); /* IBUS UCP 250ma Falling,500ma Rising */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_01, 0xa8); /*IBAT OCP Disable*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_2B, 0x00); /* VOOC_CTRL:disable */
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_35, 0x20); /*VOOC Option2*/
	//REG_08=0x00, WD Disable,Charge mode 2:1
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_08, 0x0); /*VOOC Option2*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_17, 0x28); /*IBUS_UCP_RISE_MASK_MASK*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_15, 0x02); /* mask insert irq */

	pr_err("nu2112a_slave_init_device done");

	return 0;
}

static int nu2112a_slave_init_vooc(struct oplus_voocphy_manager *chip)
{
	pr_err("nu2112a_slave_init_vooc\n");

	nu2112a_slave_reg_reset(chip, true);
	nu2112a_slave_init_device(chip);

	return 0;
}

static int nu2112a_slave_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	//u8 reg_data;
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_02, 0x04); /*VAC_OVP:12V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	//REG_04=0x1f, REG_0D=0x01, IBUS_OCP_UCP:3.6A, UCP Falling =5ms
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_04, 0x1F); /*IBUS_OCP_UCP:3.6A*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x01); /*IBUS UCP Falling =5ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_08, 0x03); /*WD:1000ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0x31);/*PMID/2-VOUT < 10%VOUT*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0xB1); /*PMID/2-VOUT < 10%VOUT*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/
	return 0;
}

static int nu2112a_slave_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_02, 0x06); /*VAC_OVP:7V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_04, 0x2B); /*IBUS_OCP_UCP:4.8A*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x01); /*IBUS UCP Falling =5ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_08, 0x84); /*WD:1000ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0x21);/*PMID/2-VOUT < 7.5%VOUT*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0xB1); /*PMID/2-VOUT < 10%VOUT*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/

	return 0;
}

static int nu2112a_slave_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_02, 0x06); /*VAC_OVP:7V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_04, 0x1F); /*IBUS_OCP_UCP:3.6A*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x01); /*IBUS UCP Falling =5ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_08, 0x84); /*WD:1000ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_18, 0x90); /*ADC_CTRL:ADC_EN*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0x31);/*PMID/2-VOUT < 10%VOUT*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/

	return 0;
}

static int nu2112a_slave_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_02, 0x04); /*VAC_OVP:12V*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_03, 0x48); /*VBUS_OVP:9.7V*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_04, 0x1F); /*IBUS_OCP_UCP:3.6A*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_0D, 0x01); /*IBUS UCP Falling =5ms*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_17, 0x28); /*Mask IBUS UCP rising*/

	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_08, 0x00); /*WD:1000ms*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_18, 0x10); /*ADC_CTRL:ADC_EN*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_05, 0x21);/*PMID/2-VOUT < 7.5%VOUT*/

	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_33, 0xd1); /*Loose_det=1*/
	//nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_35, 0x20); /*VOOCPHY Option2*/
	nu2112a_slave_write_byte(chip->slave_client, NU2112A_REG_2B, 0x00); /*DISABLE VOOCPHY*/

	pr_err("nu2112a_pdqc_hw_setting done");
	return 0;
}

static int nu2112a_slave_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		nu2112a_slave_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		nu2112a_slave_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		nu2112a_slave_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		nu2112a_slave_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		nu2112a_slave_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static int nu2112a_slave_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	nu2112a_slave_set_chg_enable(chip, false);
	nu2112a_slave_hw_setting(chip, SETTING_REASON_RESET);

	return VOOCPHY_SUCCESS;
}

static ssize_t nu2112a_slave_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
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
		ret = nu2112a_slave_read_byte(chip->slave_client, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}
	return idx;
}

static ssize_t nu2112a_slave_store_register(struct device *dev, struct device_attribute *attr, const char *buf,
					    size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x38)
		nu2112a_slave_write_byte(chip->slave_client, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, nu2112a_slave_show_registers, nu2112a_slave_store_register);

static void nu2112a_slave_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static struct of_device_id nu2112a_slave_charger_match_table[] = {
	{
		.compatible = "nu,nu2112a-slave",
	},
	{},
};

static struct oplus_voocphy_operations oplus_nu2112a_slave_ops = {
	.hw_setting = nu2112a_slave_hw_setting,
	.init_vooc = nu2112a_slave_init_vooc,
	.update_data = nu2112a_slave_update_data,
	.get_chg_enable = nu2112a_slave_get_chg_enable,
	.set_chg_enable = nu2112a_slave_set_chg_enable,
	.get_ichg = nu2112a_slave_get_ichg,
	.reset_voocphy = nu2112a_slave_reset_voocphy,
	.get_cp_status = nu2112a_slave_get_cp_status,
	.get_voocphy_enable = nu2112a_slave_get_voocphy_enable,
	.dump_voocphy_reg = nu2112a_slave_dump_reg_in_err_issue,
	.set_chg_pmid2out = nu2112a_slave_set_chg_pmid2out,
	.get_chg_pmid2out = nu2112a_slave_get_chg_pmid2out,
};

static int nu2112a_slave_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = NULL;

	if (!chip) {
		chg_err("chip null\n");
		return -1;
	}

	node = chip->slave_dev->of_node;

	rc = of_property_read_u32(node, "ocp_reg", &chip->ocp_reg);
	if (rc) {
		chip->ocp_reg = 0x8;
	} else {
		chg_err("ocp_reg is %d\n", chip->ocp_reg);
	}

	return 0;
}

static int nu2112a_slave_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip null, will do after master cp init!");
		return -EPROBE_DEFER;
	} else {
		ret = i2c_smbus_read_byte_data(chip->slave_client, 0x07);
		pr_err("0x07 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		} else
			return 1;
	}
}

static int nu2112a_slave_charger_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	pr_err("nu2112a_slave_slave_charger_probe enter!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->slave_client = client;
	chip->slave_dev = &client->dev;
	mutex_init(&i2c_rw_lock);
	i2c_set_clientdata(client, chip);

	ret = nu2112a_slave_charger_choose(chip);
	if (ret <= 0)
		return ret;

	nu2112a_slave_create_device_node(&(client->dev));

	nu2112a_slave_parse_dt(chip);

	nu2112a_slave_reg_reset(chip, true);

	nu2112a_slave_init_device(chip);

	chip->slave_ops = &oplus_nu2112a_slave_ops;

	oplus_voocphy_slave_init(chip);

	oplus_voocphy_get_chip(&oplus_voocphy_mg);

	pr_err("nu2112a_slave_parse_dt successfully!\n");

	return 0;
}

static void nu2112a_slave_charger_shutdown(struct i2c_client *client)
{
	nu2112a_slave_write_byte(client, NU2112A_REG_18, 0x10);

	return;
}

static const struct i2c_device_id nu2112a_slave_charger_id[] = {
	{ "nu2112a-slave", 0 },
	{},
};

static struct i2c_driver nu2112a_slave_charger_driver = {
	.driver =
		{
			.name = "nu2112a-charger-slave",
			.owner = THIS_MODULE,
			.of_match_table = nu2112a_slave_charger_match_table,
		},
	.id_table = nu2112a_slave_charger_id,

	.probe = nu2112a_slave_charger_probe,
	.shutdown = nu2112a_slave_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init nu2112a_slave_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&nu2112a_slave_charger_driver) != 0) {
		chg_err(" failed to register nu2112a i2c driver.\n");
	} else {
		chg_debug(" Success to register nu2112a i2c driver.\n");
	}

	return ret;
}

subsys_initcall(nu2112a_slave_subsys_init);
#else
int nu2112a_slave_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&nu2112a_slave_charger_driver) != 0) {
		chg_err(" failed to register nu2112a i2c driver.\n");
	} else {
		chg_debug(" Success to register nu2112a i2c driver.\n");
	}

	return ret;
}

void nu2112a_slave_subsys_exit(void)
{
	i2c_del_driver(&nu2112a_slave_charger_driver);
}
oplus_chg_module_register(nu2112a_slave_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC NU2112A SLAVE VOOCPHY&UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
