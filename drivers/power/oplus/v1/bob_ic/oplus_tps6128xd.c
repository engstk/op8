// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <linux/xlog.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/mtk_gpio.h>
#endif
#include <mt-plat/mtk_rtc.h>
#endif
#include <uapi/linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <mt-plat/charging.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <soc/oplus/device_info.h>

extern void mt_power_off(void);
#else
#include <linux/proc_fs.h>

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <soc/oplus/device_info.h>

#include <soc/oplus/system/boot_mode.h>

#endif
#include "../oplus_charger.h" /*add for log print */
#include "oplus_tps6128xd.h"

#define CONFIG_REGISTER 0x01
#define VOUTFLOORSET_REG 0x02
#define VOUTROOFSET_REG 0x03
#define ILIMSET_REG 0x04
#define STATUS_REG 0x05
#define E2PROMCTRL_REG 0xFF

#define BOB_STATUS_EXPECT 0x11

struct tps6128xd_dev_info {
	struct i2c_client *client;
	struct device *dev;
};

struct tps6128xd_dev_info *g_tps_dev = NULL;

static int tps6128xd_read(struct tps6128xd_dev_info *tps_dev, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(tps_dev->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int tps6128xd_write(struct tps6128xd_dev_info *tps_dev, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(tps_dev->client, reg, data);
}

int tps6128xd_get_status_reg(void)
{
	u8 reg_value = 0;
	int ret = 0;

	ret = tps6128xd_read(g_tps_dev, STATUS_REG, &reg_value);
	if (ret < 0) {
		chg_err("tps6128xd cannot get status reg ret = %d\n", ret);
		return ret;
	}

	reg_value = (int)reg_value;

	chg_debug("tps6128xd get status reg = 0x%x\n", reg_value);

	return reg_value;
}

int tps6128xd_get_status(void)
{
	int reg_value = 0;
	int ret = 0;

	reg_value = tps6128xd_get_status_reg();

	reg_value = reg_value & BOB_STATUS_EXPECT;
	if (reg_value == BOB_STATUS_EXPECT)
		ret = 1;
	else
		ret = 0;

	return ret;
}

#define HIGH_BATTERY_VOL_THRE 0x1F
#define DEFAULT_BATTERY_VOL_THRE 0x0B
int tps6128xd_set_high_batt_vol(int val)
{
	int ret = 0;
	u8 reg_value = 0;

	if (val != 0) {
		ret = tps6128xd_write(g_tps_dev, VOUTROOFSET_REG, HIGH_BATTERY_VOL_THRE);
		if (ret < 0)
			chg_err("tps6128xd cannot set high vol ret = %d\n", ret);
	} else {
		ret = tps6128xd_write(g_tps_dev, VOUTROOFSET_REG, DEFAULT_BATTERY_VOL_THRE);
		if (ret < 0)
			chg_err("tps6128xd cannot set default vol ret = %d\n", ret);
	}

	tps6128xd_read(g_tps_dev, VOUTROOFSET_REG, &reg_value);
	chg_debug("set 0x03 reg is 0x%x\n", reg_value);

	return ret;
}

static ssize_t tps6128xd_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "tps6128xd");
	for (addr = 0x1; addr <= 0x5; addr++) {
		ret = tps6128xd_read(g_tps_dev, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t tps6128xd_store_register(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && (reg >= 0x1 && reg <= 0x5))
		tps6128xd_write(g_tps_dev, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, tps6128xd_show_registers, tps6128xd_store_register);

static int tps6128xd_create_device_node(struct device *dev)
{
	int error = 0;

	error = device_create_file(dev, &dev_attr_registers);
	if (error)
		return error;

	return 0;
}

static void tps6128xd_hw_init(struct tps6128xd_dev_info *tps_dev)
{
	int ret = 0;
	u8 buf[5] = { 0x0 };

	ret = tps6128xd_write(tps_dev, VOUTFLOORSET_REG, 0x03);
	if (ret < 0) {
		chg_err("write voutfloor fail, ret:%d\n", ret);
	}
	tps6128xd_read(tps_dev, VOUTFLOORSET_REG, &buf[0]);

	tps6128xd_write(tps_dev, VOUTROOFSET_REG, 0x0B);
	tps6128xd_read(tps_dev, VOUTROOFSET_REG, &buf[1]);

	tps6128xd_write(tps_dev, ILIMSET_REG, 0x1F);
	tps6128xd_read(tps_dev, ILIMSET_REG, &buf[2]);

	/* read status reg */
	tps6128xd_read(tps_dev, STATUS_REG, &buf[3]);

	chg_debug("voutfloor:0x%x, voutroof:0x%x, ilim:0x%x, status:0x%x\n", buf[0], buf[1], buf[2], buf[3]);
}

static int tps6128xd_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tps6128xd_dev_info *tps_dev = NULL;

	chg_debug("tps6128xd probe start!\n");
	tps_dev = kzalloc(sizeof(*tps_dev), GFP_KERNEL);
	if (!tps_dev) {
		chg_err("can't alloc tps6128xd tps_dev struct\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, tps_dev);
	tps_dev->client = client;
	tps_dev->dev = dev;

	g_tps_dev = tps_dev;

	tps6128xd_create_device_node(tps_dev->dev);

	tps6128xd_hw_init(tps_dev);
	chg_debug("tps6128xd probe finished!\n");
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/

static const struct of_device_id tps6128xd_match[] = {
	{ .compatible = "oplus,bob-tps6128xd" },
	{},
};

static const struct i2c_device_id tps6128xd_id[] = {
	{ "bob-tps6128xd", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps6128xd_id);

static struct i2c_driver tps6128xd_i2c_driver = {
	.driver		= {
		.name = "bob-tps6128xd",
		.owner	= THIS_MODULE,
		.of_match_table = tps6128xd_match,
	},
	.probe		= tps6128xd_driver_probe,
	.id_table	= tps6128xd_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(tps6128xd_i2c_driver);
#else
void tps6128xd_subsys_exit(void)
{
	i2c_del_driver(&tps6128xd_i2c_driver);
}

int tps6128xd_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&tps6128xd_i2c_driver) != 0) {
		chg_err(" failed to register tps6128xd i2c driver.\n");
	} else {
		chg_debug(" Success to register tps6128xd i2c driver.\n");
	}
	return ret;
}
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/
MODULE_DESCRIPTION("Driver for bob tps6128xd chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:bob-tps6128xd");
