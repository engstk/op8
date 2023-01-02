/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __ILITEK_ILI9881H_H
#define __ILITEK_ILI9881H_H

#include "ilitek_common.h"

struct ilitek_chip_data_9881h {

	struct i2c_client *client;

	struct input_dev *input_device;

	const struct i2c_device_id *i2c_id;

#ifdef REGULATOR_POWER_ON
	struct regulator *vdd;
	struct regulator *vdd_i2c;
#endif

	struct mutex plat_mutex;
	spinlock_t plat_spinlock;

	uint32_t chip_id;

	int int_gpio;
	int reset_gpio;
	int isr_gpio;

	int delay_time_high;
	int delay_time_low;
	int edge_delay;

	bool isEnableIRQ;
	bool isEnablePollCheckPower;

#ifdef USE_KTHREAD
	struct task_struct *irq_thread;
	bool Mp_test_data_ready;
	bool free_irq_thread;
#else
	struct work_struct report_work_queue;
#endif

#ifdef CONFIG_FB
	struct notifier_block notifier_fb;
#else
	struct early_suspend early_suspend;
#endif

#ifdef BOOT_FW_UPGRADE
	struct task_struct *update_thread;
#endif

	/* obtain msg when battery status has changed */
	struct delayed_work check_power_status_work;
	struct workqueue_struct *check_power_status_queue;
	unsigned long work_delay;
	bool vpower_reg_nb;

	/* Sending report data to users for the debug */
	bool debug_node_open;
	int debug_data_frame;
	wait_queue_head_t inq;
	unsigned char ** debug_buf;
	struct mutex ilitek_debug_mutex;
	struct mutex ilitek_debug_read_mutex;

	struct spi_device *spi;
	/*support oplus struce*/
	struct hw_resource *hw_res;
	tp_dev tp_type;
	struct touchpanel_data *ts;
	char *fw_name;
	char *test_limit_name;
	char *fw_version;
	int apk_upgrade;
	int common_reset;
	bool edge_limit_status;
	bool headset_status;
	bool plug_status;
	bool lock_point_status;
	bool oplus_read_debug_data;
	int * oplus_debug_buf;

	unsigned long irq_timer;
    bool esd_check_enabled;
	int esd_retry;

	int resolution_x;
	int resolution_y;
	int touch_direction;
	struct firmware tp_firmware;
	bool fw_edge_limit_support;
};

extern struct ilitek_chip_data_9881h *ipd;

/* exported from platform.c */
extern void ilitek_platform_disable_irq(void);
extern void ilitek_platform_enable_irq(void);
extern int ilitek_platform_tp_hw_reset(bool isEnable);
#ifdef ENABLE_REGULATOR_POWER_ON
extern void ilitek_regulator_power_on(bool status);
#endif

/* exported from userspsace.c */
extern void netlink_reply_msg(void *raw, int size);
extern int ilitek_proc_init(void);
extern int ilitek_create_proc_for_oplus(struct touchpanel_data *ts);
extern void ilitek_proc_remove(void);
extern int ilitek_reset(void *chip_data);
extern int ilitek_reset_for_esd(void *chip_data);

#endif /* __PLATFORM_H */
