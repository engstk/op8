// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <asm/current.h>
#include <linux/version.h>
#include <linux/slab.h>

#include "oplus_chg_exception.h"
#include "oplus_charger.h"

enum olc_notify_type {
	OLC_NOTIFY_TYPE_SOC_JUMP,
	OLC_NOTIFY_TYPE_GENERAL_RECORD,
	OLC_NOTIFY_TYPE_NO_CHARGING,
	OLC_NOTIFY_TYPE_CHARGING_SLOW,
	OLC_NOTIFY_TYPE_CHARGING_BREAK,
	OLC_NOTIFY_TYPE_DEVICE_ABNORMAL,
	OLC_NOTIFY_TYPE_SOFTWARE_ABNORMAL,
};

struct chg_exception_table {
	int type_reason;
	int olc_type;
};

static inline void *chg_kzalloc(size_t size, gfp_t flags)
{
	void *p;

	p = kzalloc(size, flags);

	if (!p)
		chg_err("%s: Failed to allocate memory\n", __func__);

	return p;
}

static int chg_olc_raise_exception(unsigned int excep_chgye, void *summary, unsigned int summary_size)
{
	return 0;
}

static int chg_olc_battery_log_raise_exception(void)
{
	return 0;
}

static struct chg_exception_table olc_table[] = {
	{ TRACK_NOTIFY_TYPE_SOC_JUMP, OLC_NOTIFY_TYPE_SOC_JUMP },
	{ TRACK_NOTIFY_TYPE_GENERAL_RECORD, OLC_NOTIFY_TYPE_GENERAL_RECORD },
	{ TRACK_NOTIFY_TYPE_NO_CHARGING, OLC_NOTIFY_TYPE_NO_CHARGING },
	{ TRACK_NOTIFY_TYPE_CHARGING_SLOW, OLC_NOTIFY_TYPE_CHARGING_SLOW },
	{ TRACK_NOTIFY_TYPE_CHARGING_BREAK, OLC_NOTIFY_TYPE_CHARGING_BREAK },
	{ TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL, OLC_NOTIFY_TYPE_DEVICE_ABNORMAL },
	{ TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL, OLC_NOTIFY_TYPE_SOFTWARE_ABNORMAL },
};

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (1 * 3600)
#define MAX_UPLOAD_COUNT 3
static int olc_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	chg_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static bool oplus_chg_olc_check_type_reason(struct exception_data *excep_data, int type_reason, int excep_flag)
{
	int i;
	int olc_type;
	int olc_config = 1 << excep_flag;
	bool ret = false;

	for (i = 0; i < ARRAY_SIZE(olc_table); i++) {
		if (olc_table[i].type_reason == type_reason)
			break;
	}
	if (i >= ARRAY_SIZE(olc_table)) {
		chg_err("%s: not support olc track type\n", __func__);
		return ret;
	}
	olc_type = olc_table[i].olc_type;
	if (olc_config & excep_data->olc_config[olc_type])
		ret = true;
	else
		ret = false;
	return ret;
}

static int oplus_chg_olc_get_excep_reason(int type_reason, int flag_reason, int *diff)
{
	int excep_value = -1;
	int flag_base_value = 0;
	int olc_base_value = 0;

	if (!diff)
		return excep_value;

	switch (type_reason) {
	case TRACK_NOTIFY_TYPE_SOC_JUMP:
		flag_base_value = TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP;
		olc_base_value = EXCEP_SOC_ERROR_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_GENERAL_RECORD:
		flag_base_value = TRACK_NOTIFY_FLAG_CHARGER_INFO;
		olc_base_value = EXCEP_GENERAL_RECORD_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_NO_CHARGING:
		flag_base_value = TRACK_NOTIFY_FLAG_NO_CHARGING;
		olc_base_value = EXCEP_NO_CHARGING_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_SLOW:
		flag_base_value = TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM;
		olc_base_value = EXCEP_CHARGING_SLOW_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_BREAK:
		flag_base_value = TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK;
		olc_base_value = EXCEP_CHARGING_BREAK_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL:
		flag_base_value = TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
		olc_base_value = EXCEP_DEVICE_ABNORMAL_DEFAULT;
		break;
	case TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL:
		flag_base_value = TRACK_NOTIFY_FLAG_UFCS_ABNORMAL;
		olc_base_value = EXCEP_SOFTWARE_ABNORMAL_DEFAULT;
		break;
	default:
		break;
	}
	*diff = flag_reason - flag_base_value;
	if ((*diff < 0) || (*diff > OLC_FLAG_NUM_MAX)) {
		chg_err("error olc flag reason\n");
		return excep_value;
	}
	excep_value = olc_base_value + *diff;
	chg_info("excep_value = %d, diff = %d\n", excep_value, *diff);
	return excep_value;
}

int chg_exception_report(void *chg_exception_data, int type_reason, int flag_reason, void *summary,
			 unsigned int summary_size)
{
	int ret = -1;
	int type_diff = 0;
	int excep_chgye = 0;
	bool should_upload = false;
	struct exception_data *exception_data = (struct exception_data *)chg_exception_data;

	if ((!exception_data) || (!summary))
		return ret;

	chg_info("reason: %s, receive type:%d, flag:%d,for olc type\n", (char *)summary, type_reason, flag_reason);
	excep_chgye = oplus_chg_olc_get_excep_reason(type_reason, flag_reason, &type_diff);
	if (excep_chgye < 0) {
		chg_err("error olc exception tyep\n");
		return ret;
	}

	should_upload = oplus_chg_olc_check_type_reason(exception_data, type_reason, type_diff);
	if (should_upload == false) {
		chg_err("not allow upload olc type\n");
		return ret;
	}

	exception_data->excep[flag_reason].num++;
	exception_data->excep[flag_reason].curr_time = olc_track_get_local_time_s();
	if (exception_data->excep[flag_reason].num == 1) {
		exception_data->excep[flag_reason].pre_upload_time = olc_track_get_local_time_s();
	}
	if (exception_data->excep[flag_reason].curr_time - exception_data->excep[flag_reason].pre_upload_time >
	    TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		exception_data->excep[flag_reason].num = 1;
	if (exception_data->excep[flag_reason].num > MAX_UPLOAD_COUNT) {
		chg_err("not allow upload olc max times\n");
		return ret;
	}

	ret = chg_olc_raise_exception(excep_chgye, summary, summary_size);

	return ret;
}

#define COUNT_TIMELIMIT 60
#define SUCCESS_COUNT 100
int oplus_chg_batterylog_exception_push(void)
{
	int ret = -1;
	static int count = 0;
	static unsigned long count_time;

	if (time_after(jiffies, count_time)) {
		count_time = COUNT_TIMELIMIT * HZ;
		count_time += jiffies;
		ret = chg_olc_battery_log_raise_exception();
		if (ret > 0) {
			count++;
			chg_info("push battery log %d times\n", count);
		} else {
			chg_err("push battery log fail");
		}
	}

	if (count > SUCCESS_COUNT) {
		count = 0;
		chg_info("push battery log more than 100 times, count to 0\n");
	}

	return ret;
}
