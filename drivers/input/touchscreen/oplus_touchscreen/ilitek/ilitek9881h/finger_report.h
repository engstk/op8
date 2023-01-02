/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FINGER_REPORT_H
#define __FINGER_REPORT_H

struct core_fr_data {
	struct input_dev *input_device;

	/* the default of finger report is enabled */
	bool isEnableFR;
	bool handleint;
	/* used to send finger report packet to user psace */
	bool isEnableNetlink;
	/* allow input dev to report the value of physical touch */
	bool isEnablePressure;

	/* get screen resloution from fw if it's true */
	bool isSetResolution;
	/* used to change I2C Uart Mode when fw mode is in this mode */
	uint8_t i2cuart_mode;

	/* current firmware mode in driver */
	uint16_t actual_fw_mode;

	/* mutual firmware info */
	uint16_t log_packet_length;
	uint8_t log_packet_header;
	uint8_t type;
	uint8_t Mx;
	uint8_t My;
	uint8_t Sd;
	uint8_t Ss;
};

/* An id with position in each fingers */
struct mutual_touch_point {
	uint16_t id;
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
};

/* Keys and code with each fingers */
struct mutual_touch_info {
	uint8_t touch_num;
	uint8_t key_code;
	//for oplus
	int pointid_info;
	struct point_info *points;
	uint8_t gesture_data[170];
	//end for oplus
	struct mutual_touch_point mtp[10];
};

/* Store the packet of finger report */
struct fr_data_node {
	uint8_t *data;
	uint16_t len;
};

extern struct core_fr_data *core_fr;
extern struct mutual_touch_info g_mutual_data;

extern void core_fr_touch_press(int32_t x, int32_t y, uint32_t pressure, int32_t id);
extern void core_fr_touch_release(int32_t x, int32_t y, int32_t id);
extern uint8_t cal_fr_checksum(uint8_t *pMsg, uint32_t nLength);
extern void core_fr_handler(void);
extern int core_fr_get_gesture_data(uint8_t * data);
extern void core_fr_input_set_param(struct input_dev *input_device);
extern int core_fr_init(void);
extern void core_fr_remove(void);

#endif /* __FINGER_REPORT_H */
