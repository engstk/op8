// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 *
 * oplus_battery_log.h
 */

#ifndef __OPLUS_BATTERY_LOG_H__
#define __OPLUS_BATTERY_LOG_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/of_platform.h>

#define BATTERY_LOG_INVAID_OP (-22)
#define BATTERY_LOG_MAX_SIZE 1024
#define BATTERY_LOG_RESERVED_SIZE 16
#define BATTERY_LOG_RD_BUF_SIZE 32
#define BATTERY_LOG_WR_BUF_SIZE 32

enum battery_log_device_id {
	BATTERY_LOG_DEVICE_ID_BEGIN = 0,
	BATTERY_LOG_DEVICE_ID_COMM_INFO = BATTERY_LOG_DEVICE_ID_BEGIN,
	BATTERY_LOG_DEVICE_ID_VOOC,
	BATTERY_LOG_DEVICE_ID_BUCK_IC,
	/*BATTERY_LOG_DEVICE_ID_BQ27541,*/
	BATTERY_LOG_DEVICE_ID_END,
};

enum battery_log_type {
	BATTERY_LOG_TYPE_BEGIN = 0,
	BATTERY_LOG_DUMP_LOG_HEAD = BATTERY_LOG_TYPE_BEGIN,
	BATTERY_LOG_DUMP_LOG_CONTENT,
	BATTERY_LOG_TYPE_END,
};

struct battery_log_ops {
	const char *dev_name;
	void *dev_data;
	int (*dump_log_head)(char *buf, int size, void *dev_data);
	int (*dump_log_content)(char *buf, int size, void *dev_data);
};

struct battery_log_dev {
	struct device *dev;
	int dev_id;
	bool battery_log_support;
	struct mutex log_lock;
	struct mutex devid_lock;
	char log_buf[BATTERY_LOG_MAX_SIZE];
	unsigned int total_ops;
	struct battery_log_ops *ops[BATTERY_LOG_DEVICE_ID_END];
};

int battery_log_ops_register(struct battery_log_ops *ops);
int battery_log_common_operate(int type, char *buf, int size);
int oplus_battery_log_support(void);
#endif /*__OPLUS_BATTERY_LOG_H__*/
