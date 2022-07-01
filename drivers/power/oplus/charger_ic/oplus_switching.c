// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include "oplus_switching.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "../oplus_charger.h"
#include "../oplus_vooc.h"
struct oplus_switch_chip * g_switching_chip;
extern struct oplus_chg_chip* oplus_chg_get_chg_struct(void);

int oplus_switching_get_error_status(void)
{
	if (g_switching_chip->error_status) {
		chg_err("error_status:%d\n", g_switching_chip->error_status);
		return g_switching_chip->error_status;
	} else {
		g_switching_chip->switch_ops->switching_get_fastcharge_current();
		chg_err("error_status:%d\n", g_switching_chip->error_status);
		return g_switching_chip->error_status;
	}

	return 0;
}

int oplus_switching_hw_enable(int en)
{
	if (!g_switching_chip) {
		chg_err("fail\n");
		return -1;
	} else {
		chg_err("success\n");
		return g_switching_chip->switch_ops->switching_hw_enable(en);
	}
}

int oplus_switching_set_fastcharge_current(int curr_ma)
{
	if (!g_switching_chip) {
		chg_err("fail\n");
		return -1;
	} else {
		chg_err("success\n");
		return g_switching_chip->switch_ops->switching_set_fastcharge_current(curr_ma);
	}
}

int oplus_switching_enable_charge(int en)
{
	if (!g_switching_chip) {
		chg_err("fail\n");
		return -1;
	} else {
		chg_err("success\n");
		return g_switching_chip->switch_ops->switching_enable_charge(en);
	}
}

bool oplus_switching_get_hw_enable(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_hw_enable) {
		chg_err("fail\n");
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_hw_enable();
	}
}

bool oplus_switching_get_charge_enable(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_charge_enable) {
		chg_err("fail\n");
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_charge_enable();
	}
}

int oplus_switching_get_fastcharge_current(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_fastcharge_current) {
		chg_err("fail\n");
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_fastcharge_current();
	}
}

int oplus_switching_get_discharge_current(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_discharge_current) {
		chg_err("fail\n");
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_discharge_current();
	}
}

int oplus_switching_get_if_need_balance_bat(int vbat0_mv, int vbat1_mv)
{
	int diff_volt = 0;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	static int error_count = 0;
	chg_err("vbat0_mv:%d, vbat1_mv:%d\n", vbat0_mv, vbat1_mv);
	if (!g_switching_chip) {
		chg_err("fail\n");
		return -1;
	} else {
		diff_volt = abs(vbat0_mv - vbat1_mv);
		if (diff_volt < 150) {
			return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
		}

		if (chip->sub_batt_temperature == -400 || chip->temperature == -400) {
			return PARALLEL_BAT_BALANCE_ERROR_STATUS8;
		}

		if (oplus_switching_get_error_status()) {
			return PARALLEL_BAT_BALANCE_ERROR_STATUS8;
		}

		if (oplus_vooc_get_fastchg_started() == true) {
			if ((abs(chip->sub_batt_icharging) < 100 || abs(chip->icharging) < 100)
					&& (abs(chip->icharging - chip->sub_batt_icharging) >= 2000)) {
				if (error_count < 2) {
					error_count++;
				} else {
					return PARALLEL_BAT_BALANCE_ERROR_STATUS8;
				}
			}
		}
		if (vbat0_mv >= 3400 && vbat1_mv < 3400) {
			if (vbat1_mv < 3100) {
				if (vbat0_mv - vbat1_mv <= 1000) {
					return PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE;
				} else {
					return PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE;
				}
			} else {
				return PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE;
			}
		} else if (vbat0_mv >= vbat1_mv) {
			if (diff_volt >= 800) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE;
			} else if (diff_volt >= 600) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE;
			} else if (diff_volt >= 150) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE;
			} else {
				return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
			}
		} else if (vbat0_mv < vbat1_mv) {
			if (diff_volt >= 400) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE;
			} else if (diff_volt >= 150) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE;
			} else {
				return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
			}
		}
	}
	return -1;
}

int oplus_switching_set_balance_bat_status(int status)
{
	chg_err("status:%d\n", status);
	switch (status) {
	case PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(2800);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(500);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(500);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(1000);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(1000);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(2800);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(2800);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(1);
		g_switching_chip->switch_ops->switching_set_discharge_current(200);
		g_switching_chip->switch_ops->switching_set_fastcharge_current(200);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(0);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE:
		g_switching_chip->switch_ops->switching_hw_enable(0);
		break;
	case PARALLEL_BAT_BALANCE_ERROR_STATUS8:
		g_switching_chip->switch_ops->switching_hw_enable(0);
		break;
	}
	return 0;
}

int oplus_switching_set_current(int current_ma)
{
	chg_err("current_ma:%d\n", current_ma);
	g_switching_chip->switch_ops->switching_set_fastcharge_current(current_ma);

	return 0;
}

void oplus_switching_init(struct oplus_switch_chip *chip)
{
	chg_err("");
	g_switching_chip = chip;
}

int oplus_switching_support_parallel_chg(void)
{
	if (g_switching_chip == NULL) {
		return 0;
	} else {
		return 1;
	}
}
