// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019 The Linux Foundation. All rights reserved.
 */

#ifndef __OPLUS_CHG_CFG_H__
#define __OPLUS_CHG_CFG_H__

#include <linux/types.h>
#include <linux/kernel.h>

#define OPLUS_CHG_CFG_MAGIC 0x02000300

enum oplus_chg_param_type {
	OPLUS_CHG_WIRED_PARAM,
	OPLUS_CHG_WLS_PARAM,
	OPLUS_CHG_COMM_PARAM,
	OPLUS_CHG_VOOC_PARAM,
	OPLUS_CHG_PARAM_MAX,
};

struct oplus_chg_cfg_head {
	u32 magic;
	u32 head_size;
	u32 size;
	u32 param_index[OPLUS_CHG_PARAM_MAX];
	u8 signature[512];
} __attribute__ ((packed));

struct oplus_chg_param_head {
	u32 magic;
	u32 size;
	u32 type;
	u8 data[0];
} __attribute__ ((packed));

struct oplus_chg_cfg_data_head {
	u32 magic;
	u32 index;
	u32 size;
	u8 data[0];
} __attribute__ ((packed));

int oplus_chg_check_cfg_data(u8 *buf, int buf_len);
int oplus_chg_cfg_update_config(u8 *buf, int buf_len);

#endif /* __OPLUS_CHG_CFG_H__ */
