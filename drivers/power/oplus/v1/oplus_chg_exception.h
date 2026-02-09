/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_EXCEPTION_H__
#define __OPLUS_CHG_EXCEPTION_H__

#define EXCEP_SOC_ERROR_DEFAULT (0x000)
#define EXCEP_GENERAL_RECORD_DEFAULT (0x100)
#define EXCEP_NO_CHARGING_DEFAULT (0x200)
#define EXCEP_CHARGING_SLOW_DEFAULT (0x300)
#define EXCEP_CHARGING_BREAK_DEFAULT (0x400)
#define EXCEP_DEVICE_ABNORMAL_DEFAULT (0x500)
#define EXCEP_SOFTWARE_ABNORMAL_DEFAULT (0x600)
#define EXCEP_TYPE_MAX_DEFAULT (0xFFF)

#define OLC_CONFIG_NUM_MAX 16
#define OLC_FLAG_NUM_MAX 64
#define OLC_CONFIG_SIZE (16 * 16 + 16)
#define OLC_CONFIG_BIT_NUM 16
#define TRACK_EXCEP_NUM_MAX 1024

struct excet_dat {
	int curr_time;
	int pre_upload_time;
	int num;
};

struct exception_data {
	u64 olc_config[OLC_CONFIG_NUM_MAX];
	struct excet_dat excep[TRACK_EXCEP_NUM_MAX];
};

int chg_exception_report(void *chg_exception_data, int type_reason, int flag_reason, void *summary,
			 unsigned int summary_size);
int oplus_chg_batterylog_exception_push(void);
#endif /*__OPLUS_CHG_EXCEPTION_H__*/
