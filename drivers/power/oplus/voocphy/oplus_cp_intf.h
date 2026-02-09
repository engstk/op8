// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_PPS_CP_H_
#define _OPLUS_PPS_CP_H_

#include <linux/i2c.h>

struct oplus_pps_cp_device_operations {
	int (*oplus_cp_hardware_init)(struct i2c_client *client);
	int (*oplus_cp_reset)(struct i2c_client *client);
	int (*oplus_cp_pmid2vout_enable)(struct i2c_client *client, bool enable);
	int (*oplus_cp_cfg_sc)(struct i2c_client *client);
	int (*oplus_cp_cfg_bypass)(struct i2c_client *client);
	int (*oplus_get_ucp_flag)(struct i2c_client *client);
	int (*oplus_set_cp_enable)(struct i2c_client *client, int enable);
	int (*oplus_get_cp_vbus)(struct i2c_client *client);
	int (*oplus_get_cp_ibus)(struct i2c_client *client);
	int (*oplus_get_cp_vac)(struct i2c_client *client);
	int (*oplus_get_cp_vout)(struct i2c_client *client);
	int (*oplus_get_cp_vbat)(struct i2c_client *client);
	int (*oplus_get_cp_tdie)(struct i2c_client *client);
};

typedef enum {
	CP_MASTER,
	CP_SLAVE_A,
	CP_SLAVE_B,
	CP_MAX,
} PPS_CP_DEVICE_NUM;

typedef union protect_flag {
	u8 value;
	struct {
		u8 ibus_ucp : 1;
		u8 ibus_ocp : 1;
		u8 vbus_ovp : 1;
		u8 ibat_ocp : 1;
		u8 vbat_ovp : 1;
		u8 vout_ovp : 1;
	} value_bit;
} DEV_PROTECT_FLAG;

struct chargepump_device {
	const char *dev_name;
	struct i2c_client *client;
	struct oplus_pps_cp_device_operations *dev_ops;
};

int oplus_cp_device_register(PPS_CP_DEVICE_NUM index, struct chargepump_device *cp_dev);
void oplus_pps_device_protect_irq_callback(DEV_PROTECT_FLAG flag);
#endif /*_OPLUS_PPS_CP_H_*/
