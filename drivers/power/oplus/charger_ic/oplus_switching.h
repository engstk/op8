// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_SWITCHING_H_
#define _OPLUS_SWITCHING_H_

#include <linux/i2c.h>
#include <linux/power_supply.h>

#define SWITCH_ERROR_OVER_VOLTAGE      1
#define SWITCH_ERROR_I2C_ERROR         1

struct oplus_switch_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_switch_operations *switch_ops;
	int error_status;
};

struct oplus_switch_operations {
	int (*switching_hw_enable)(int en);
	int (*switching_set_fastcharge_current)(int curr_ma);
	int (*switching_set_discharge_current)(int curr_ma);
	int (*switching_enable_charge)(int en);
	int (*switching_get_fastcharge_current)(void);
	int (*switching_get_hw_enable)(void);
	int (*switching_get_discharge_current)(void);
	int (*switching_get_charge_enable)(void);
};

enum {
	PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE,
	PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE,           /*VBAT0 - VBAT1 >= 800mv*/
	PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE,           /*800mv > VBAT0 - VBAT1 >= 600mv*/
	PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE,           /*600mv > VBAT0 - VBAT1 >= 50mv*/
	PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE,           /*400mv > VBAT1 - VBAT0 > 50mv*/
	PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE,           /*VBAT0 > 3400mv && VBAT1 < 3400 && VBAT0 - VBAT1 <= 1000mv*/
	PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE,          /*VBAT0 > 3400mv && VBAT1 < 3100 && VBAT0 - VBAT1 > 1000mv*/
	PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE,          /*VBAT1 - VBAT0 > 400mv */
	PARALLEL_BAT_BALANCE_ERROR_STATUS8,                       /*bat error*/
};


void oplus_switching_init(struct oplus_switch_chip *chip);

int oplus_switching_hw_enable(int en);
int oplus_switching_set_fastcharge_current(int curr_ma);
int oplus_switching_get_if_need_balance_bat(int vbat0_mv, int vbat1_mv);
int oplus_switching_set_balance_bat_status(int status);
int oplus_switching_enable_charge(int en);
int oplus_switching_support_parallel_chg(void);
int oplus_switching_set_current(int current_ma);
#endif /* _OPLUS_GAUGE_H */
