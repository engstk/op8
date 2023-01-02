// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>
#include <linux/list.h>

#include "ilitek_ili9881h.h"
#include "data_transfer.h"
#include "finger_report.h"
#include "protocol.h"


/* record the status of touch being pressed or released currently and previosuly */
uint8_t g_current_touch[MAX_TOUCH_NUM];
uint8_t g_previous_touch[MAX_TOUCH_NUM];

/* the total length of finger report packet */
int g_total_len = 0;

struct mutual_touch_info g_mutual_data;
struct fr_data_node *g_fr_node = NULL, *g_fr_uart = NULL;
struct core_fr_data *core_fr = NULL;

//static struct mutual_touch_info pre_touch_info;

/**
 * Calculate the check sum of each packet reported by firmware
 *
 * @pMsg: packet come from firmware
 * @nLength : the length of its packet
 */
uint8_t cal_fr_checksum(uint8_t *pMsg, uint32_t nLength)
{
	int i = 0;
	int32_t nCheckSum = 0;

	for (i = 0; i < nLength; i++) {
		nCheckSum += pMsg[i];
	}

	return (uint8_t) ((-nCheckSum) & 0xFF);
}
EXPORT_SYMBOL(cal_fr_checksum);

#if (INTERFACE == I2C_INTERFACE)
/**
 *  Receive data when fw mode stays at i2cuart mode.
 *
 *  the first is to receive N bytes depending on the mode that firmware stays
 *  before going in this function, and it would check with i2c buffer if it
 *  remains the rest of data.
 */
static void i2cuart_recv_packet(void)
{
	int res = 0;
	int need_read_len = 0;
	int one_data_bytes = 0;
	int type = g_fr_node->data[3] & 0x0F;
	int actual_len = g_fr_node->len - 5;

	TPD_DEBUG("pid = %x, data[3] = %x, type = %x, actual_len = %d\n",
	    g_fr_node->data[0], g_fr_node->data[3], type, actual_len);

	need_read_len = g_fr_node->data[1] * g_fr_node->data[2];

	if (type == 0 || type == 1 || type == 6) {
		one_data_bytes = 1;
	} else if (type == 2 || type == 3) {
		one_data_bytes = 2;
	} else if (type == 4 || type == 5) {
		one_data_bytes = 4;
	}

	TPD_DEBUG("need_read_len = %d  one_data_bytes = %d\n", need_read_len, one_data_bytes);

	need_read_len = need_read_len * one_data_bytes + 1;

	if (need_read_len > actual_len) {
		g_fr_uart = kmalloc(sizeof(*g_fr_uart), GFP_ATOMIC);
		if (ERR_ALLOC_MEM(g_fr_uart)) {
			TPD_INFO("Failed to allocate g_fr_uart memory %ld\n", PTR_ERR(g_fr_uart));
			return;
		}

		g_fr_uart->len = need_read_len - actual_len;
		g_fr_uart->data = kzalloc(g_fr_uart->len, GFP_ATOMIC);
		if (ERR_ALLOC_MEM(g_fr_uart->data)) {
			TPD_INFO("Failed to allocate g_fr_uart memory %ld\n", PTR_ERR(g_fr_uart->data));
			return;
		}

		g_total_len += g_fr_uart->len;
		res = core_read(core_config->slave_i2c_addr, g_fr_uart->data, g_fr_uart->len);
		if (res < 0)
			TPD_INFO("Failed to read finger report packet\n");
	}
}
#endif

/*
 * It'd be called when a finger's touching down a screen. It'll notify the event
 * to the uplayer from input device.
 *
 * @x: the axis of X
 * @y: the axis of Y
 * @pressure: the value of pressue on a screen
 * @id: an id represents a finger pressing on a screen
 */
void core_fr_touch_press(int32_t x, int32_t y, uint32_t pressure, int32_t id)
{
	TPD_DEBUG("DOWN: id = %d, x = %d, y = %d\n", id, x, y);

#ifdef MT_B_TYPE
	input_mt_slot(core_fr->input_device, id);
	input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, true);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_X, x);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_Y, y);

	if (core_fr->isEnablePressure)
		input_report_abs(core_fr->input_device, ABS_MT_PRESSURE, pressure);
#else
	input_report_key(core_fr->input_device, BTN_TOUCH, 1);

	input_report_abs(core_fr->input_device, ABS_MT_TRACKING_ID, id);
	input_report_abs(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 1);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_X, x);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_Y, y);

	if (core_fr->isEnablePressure)
		input_report_abs(core_fr->input_device, ABS_MT_PRESSURE, pressure);

	input_mt_sync(core_fr->input_device);
#endif /* MT_B_TYPE */
}
EXPORT_SYMBOL(core_fr_touch_press);

/*
 * It'd be called when a finger's touched up from a screen. It'll notify
 * the event to the uplayer from input device.
 *
 * @x: the axis of X
 * @y: the axis of Y
 * @id: an id represents a finger leaving from a screen.
 */
void core_fr_touch_release(int32_t x, int32_t y, int32_t id)
{
	TPD_DEBUG("UP: id = %d, x = %d, y = %d\n", id, x, y);

#ifdef MT_B_TYPE
	input_mt_slot(core_fr->input_device, id);
	input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, false);
#else
	input_report_key(core_fr->input_device, BTN_TOUCH, 0);
	input_mt_sync(core_fr->input_device);
#endif /* MT_B_TYPE */
}
EXPORT_SYMBOL(core_fr_touch_release);

static int parse_touch_package_v3_2(void)
{
	TPD_INFO("Not implemented yet\n");
	return 0;
}

static int finger_report_ver_3_2(void)
{
	TPD_INFO("Not implemented yet\n");
	parse_touch_package_v3_2();
	return 0;
}

/*
 * It mainly parses the packet assembled by protocol v5.0
 */
static int parse_touch_package_v5_0(uint8_t pid)
{
	int i = 0;
	int res = 0;
	int index = 0;
	uint8_t check_sum = 0;
	uint32_t nX = 0;
	uint32_t nY = 0;
	uint32_t count = (core_config->tp_info->nXChannelNum * core_config->tp_info->nYChannelNum) * 2;
	for (i = 0; i < 9; i++)
		TPD_DEBUG("data[%d] = %x\n", i, g_fr_node->data[i]);

	check_sum = cal_fr_checksum(&g_fr_node->data[0], (g_fr_node->len - 1));
	TPD_DEBUG("data = %x  ;  check_sum : %x\n", g_fr_node->data[g_fr_node->len - 1], check_sum);

	if (g_fr_node->data[g_fr_node->len - 1] != check_sum) {
		TPD_INFO("Wrong checksum set pointid info = -1\n");
		//memcpy(&g_mutual_data, &pre_touch_info, sizeof(struct mutual_touch_info));
		TPD_INFO("check sum error data is:");
		for (i = 0; i < g_fr_node->len; i++) {
			TPD_DEBUG_NTAG("0x%02X, ", g_fr_node->data[i]);
		}
		TPD_DEBUG_NTAG("\n");
		g_mutual_data.pointid_info = -1;
		res = -1;
		goto out;
	}

	/* start to parsing the packet of finger report */
	if (pid == protocol->demo_pid) {
		TPD_DEBUG(" **** Parsing DEMO packets : 0x%x ****\n", pid);

		for (i = 0; i < MAX_TOUCH_NUM; i++) {
			if ((g_fr_node->data[(4 * i) + 1] == 0xFF) && (g_fr_node->data[(4 * i) + 2] == 0xFF)
			    && (g_fr_node->data[(4 * i) + 3] == 0xFF)) {
#ifdef MT_B_TYPE
				g_current_touch[i] = 0;
#endif
				continue;
			}

			nX = (((g_fr_node->data[(4 * i) + 1] & 0xF0) << 4) | (g_fr_node->data[(4 * i) + 2]));
			nY = (((g_fr_node->data[(4 * i) + 1] & 0x0F) << 8) | (g_fr_node->data[(4 * i) + 3]));

			if (!core_fr->isSetResolution) {
				g_mutual_data.mtp[g_mutual_data.touch_num].x = nX * (ipd->resolution_x) / TPD_WIDTH;
				g_mutual_data.mtp[g_mutual_data.touch_num].y = nY * (ipd->resolution_y) / TPD_HEIGHT;
				g_mutual_data.mtp[g_mutual_data.touch_num].id = i;
			} else {
				g_mutual_data.mtp[g_mutual_data.touch_num].x = nX;
				g_mutual_data.mtp[g_mutual_data.touch_num].y = nY;
				g_mutual_data.mtp[g_mutual_data.touch_num].id = i;
			}

			g_mutual_data.mtp[g_mutual_data.touch_num].pressure = g_fr_node->data[(4 * i) + 4];
			if (g_mutual_data.mtp[g_mutual_data.touch_num].pressure == 0) {
				TPD_INFO("pressure = 0 force set 1\n");
				g_mutual_data.mtp[g_mutual_data.touch_num].pressure = 1;
			}
            g_mutual_data.pointid_info = g_mutual_data.pointid_info | (1 << i);
			if (!(ERR_ALLOC_MEM(g_mutual_data.points))) {
	            g_mutual_data.points[i].x = g_mutual_data.mtp[g_mutual_data.touch_num].x;
	            g_mutual_data.points[i].y = g_mutual_data.mtp[g_mutual_data.touch_num].y;
	            g_mutual_data.points[i].z = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].width_major = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].touch_major = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].status = 1;
			}
			TPD_DEBUG("[x,y]=[%d,%d]\n", nX, nY);
			TPD_DEBUG("point[%d] : (%d,%d) = %d\n",
		    g_mutual_data.mtp[g_mutual_data.touch_num].id,
		    g_mutual_data.mtp[g_mutual_data.touch_num].x,
		    g_mutual_data.mtp[g_mutual_data.touch_num].y, g_mutual_data.mtp[g_mutual_data.touch_num].pressure);

			g_mutual_data.touch_num++;

#ifdef MT_B_TYPE
			g_current_touch[i] = 1;
#endif
		}
		//memcpy(&pre_touch_info, &g_mutual_data, sizeof(struct mutual_touch_info));
	} else if (pid == protocol->debug_pid) {
		TPD_DEBUG(" **** Parsing DEBUG packets : 0x%x ****\n", pid);
		TPD_DEBUG("Length = %d\n", (g_fr_node->data[1] << 8 | g_fr_node->data[2]));

		for (i = 0; i < MAX_TOUCH_NUM; i++) {
			if ((g_fr_node->data[(3 * i) + 5] == 0xFF) && (g_fr_node->data[(3 * i) + 6] == 0xFF)
			    && (g_fr_node->data[(3 * i) + 7] == 0xFF)) {
#ifdef MT_B_TYPE
				g_current_touch[i] = 0;
#endif
				continue;
			}

			nX = (((g_fr_node->data[(3 * i) + 5] & 0xF0) << 4) | (g_fr_node->data[(3 * i) + 6]));
			nY = (((g_fr_node->data[(3 * i) + 5] & 0x0F) << 8) | (g_fr_node->data[(3 * i) + 7]));

			if (!core_fr->isSetResolution) {
				g_mutual_data.mtp[g_mutual_data.touch_num].x = nX * (ipd->resolution_x) / TPD_WIDTH;
				g_mutual_data.mtp[g_mutual_data.touch_num].y = nY * (ipd->resolution_y) / TPD_HEIGHT;
				g_mutual_data.mtp[g_mutual_data.touch_num].id = i;
			} else {
				g_mutual_data.mtp[g_mutual_data.touch_num].x = nX;
				g_mutual_data.mtp[g_mutual_data.touch_num].y = nY;
				g_mutual_data.mtp[g_mutual_data.touch_num].id = i;
			}

			g_mutual_data.mtp[g_mutual_data.touch_num].pressure = g_fr_node->data[(4 * i) + 4];
			if (g_mutual_data.mtp[g_mutual_data.touch_num].pressure == 0) {
				TPD_INFO("pressure = 0 force set 1\n");
				g_mutual_data.mtp[g_mutual_data.touch_num].pressure = 1;
			}
            g_mutual_data.pointid_info = g_mutual_data.pointid_info | (1 << i);
			if (!(ERR_ALLOC_MEM(g_mutual_data.points))) {
	            g_mutual_data.points[i].x = g_mutual_data.mtp[g_mutual_data.touch_num].x;
	            g_mutual_data.points[i].y = g_mutual_data.mtp[g_mutual_data.touch_num].y;
	            g_mutual_data.points[i].z = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].width_major = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].touch_major = g_mutual_data.mtp[g_mutual_data.touch_num].pressure;
	            g_mutual_data.points[i].status = 1;
			}
			TPD_DEBUG("[x,y]=[%d,%d]\n", nX, nY);
			TPD_DEBUG("point[%d] : (%d,%d) = %d\n",
			    g_mutual_data.mtp[g_mutual_data.touch_num].id,
			    g_mutual_data.mtp[g_mutual_data.touch_num].x,
			    g_mutual_data.mtp[g_mutual_data.touch_num].y, g_mutual_data.mtp[g_mutual_data.touch_num].pressure);

			g_mutual_data.touch_num++;

#ifdef MT_B_TYPE
			g_current_touch[i] = 1;
#endif
		}
		//memcpy(&pre_touch_info, &g_mutual_data, sizeof(struct mutual_touch_info));
		if (ipd->oplus_read_debug_data && (!ERR_ALLOC_MEM(ipd->oplus_debug_buf))) {
			for (index = 0, i = 35; i < count + 35; i+=2, index++) {
				if((uint8_t)(g_fr_node->data[i] & 0x80) == (uint8_t)0x80)
				{
					TPD_DEBUG("%d, ", (((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]) - 0x10000));
					ipd->oplus_debug_buf[index] = (((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]) - 0x10000);
				}
				else
				{
					TPD_DEBUG("%d, ", (g_fr_node->data[i] << 8) + g_fr_node->data[i+1]);
					ipd->oplus_debug_buf[index] = ((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]);
				}
			}
			ipd->oplus_read_debug_data = false;
		}
	} else {
		if (pid != 0) {
			/* ignore the pid with 0x0 after enable irq at once */
			TPD_INFO(" **** Unknown PID : 0x%x ****\n", pid);
			res = -1;
		}
	}

out:
	return res;
}

/*
 * The function is called by an interrupt and used to handle packet of finger
 * touch from firmware. A differnece in the process of the data is acorrding to the protocol
 */
static int finger_report_ver_5_0(void)
{
	int res = 0;
	uint8_t pid = 0x0;
#if (INTERFACE == SPI_INTERFACE)
	res = core_spi_read_data_after_checksize(g_fr_node->data, g_fr_node->len);
#else
#ifdef I2C_SEGMENT
	res = core_i2c_segmental_read(core_config->slave_i2c_addr, g_fr_node->data, g_fr_node->len);
#else
	res = core_read(core_config->slave_i2c_addr, g_fr_node->data, g_fr_node->len);
#endif
#endif
	if (res < 0) {
		TPD_INFO("Failed to read finger report packet\n");
		g_mutual_data.pointid_info = -1;
		goto out;
	}

	pid = g_fr_node->data[0];
	TPD_DEBUG("PID = 0x%x\n", pid);

	if (pid == protocol->i2cuart_pid) {
		TPD_DEBUG("I2CUART(0x%x): set pointid info = -1;\n", pid);
#if (INTERFACE == I2C_INTERFACE)
		TPD_DEBUG("I2CUART(0x%x): prepare to receive rest of data;\n", pid);
		i2cuart_recv_packet();
#endif
		//memcpy(&g_mutual_data, &pre_touch_info, sizeof(struct mutual_touch_info));
		g_mutual_data.pointid_info = -1;
		goto out;
	}

	if (pid == protocol->ges_pid && core_config->isEnableGesture) {
		TPD_DEBUG("pid = 0x%x, code = %x\n", pid, g_fr_node->data[1]);
		memcpy(g_mutual_data.gesture_data, g_fr_node->data, (g_total_len > 170 ? 170 : g_total_len));
		goto out;
	}

	res = parse_touch_package_v5_0(pid);
	if (res < 0) {
		TPD_INFO("Failed to parse packet of finger touch\n");
		goto out;
	}

	TPD_DEBUG("Touch Num = %d oplus id info 0x%X\n", g_mutual_data.touch_num, g_mutual_data.pointid_info);
out:
	return res;
}


/**
 * Calculate the length with different modes according to the format of protocol 5.0
 *
 * We compute the length before receiving its packet. If the length is differnet between
 * firmware and the number we calculated, in this case I just print an error to inform users
 * and still send up to users.
 */
static int calc_packet_length(void)
{
	int rlen = 0;
#if (INTERFACE == SPI_INTERFACE)
	rlen = core_spi_check_read_size();
#else
	uint16_t xch = 0;
	uint16_t ych = 0;
	uint16_t stx = 0;
	uint16_t srx = 0;
	/* FIXME: self_key not defined by firmware yet */
	uint16_t self_key = 2;
	if (protocol->major == 0x5) {
		if (!ERR_ALLOC_MEM(core_config->tp_info)) {
			xch = core_config->tp_info->nXChannelNum;
			ych = core_config->tp_info->nYChannelNum;
			stx = core_config->tp_info->self_tx_channel_num;
			srx = core_config->tp_info->self_rx_channel_num;
		}

		TPD_DEBUG("firmware mode : 0x%x\n", core_fr->actual_fw_mode);

		if (protocol->demo_mode == core_fr->actual_fw_mode) {
			rlen = protocol->demo_len;
		} else if (protocol->test_mode == core_fr->actual_fw_mode) {
			if (ERR_ALLOC_MEM(core_config->tp_info)) {
				rlen = protocol->test_len;
			} else {
				rlen = (2 * xch * ych) + (stx * 2) + (srx * 2) + 2 * self_key + 1;
				rlen += 1;
			}
		} else if (protocol->debug_mode == core_fr->actual_fw_mode) {
			if (ERR_ALLOC_MEM(core_config->tp_info)) {
				rlen = protocol->debug_len;
			} else {
				rlen = (2 * xch * ych) + (stx * 2) + (srx * 2) + 2 * self_key + (8 * 2) + 1;
				rlen += 35;
			}
		} else {
			TPD_INFO("Unknown firmware mode : %d\n", core_fr->actual_fw_mode);
			rlen = 0;
		}
	} else {
		TPD_INFO("Wrong the major version of protocol, 0x%x\n", protocol->major);
		return -1;
	}
#endif

	TPD_DEBUG("rlen = %d\n", rlen);
	return rlen;
}

/**
 * The table is used to handle calling functions that deal with packets of finger report.
 * The callback function might be different of what a protocol is used on a chip.
 *
 * It's possible to have the different protocol according to customer's requirement on the same
 * touch ic with customised firmware, so I don't have to identify which of the ic has been used; instead,
 * the version of protocol should match its parsing pattern.
 */
typedef struct {
	uint8_t protocol_marjor_ver;
	uint8_t protocol_minor_ver;
	int (*finger_report)(void);
} fr_hashtable;

fr_hashtable fr_t[] = {
	{0x3, 0x2, finger_report_ver_3_2},
	{0x5, 0x0, finger_report_ver_5_0},
	{0x5, 0x1, finger_report_ver_5_0},
};

int core_fr_get_gesture_data(uint8_t * data)
{
	int res = 0;
	res = core_read(core_config->slave_i2c_addr, data, GESTURE_INFO_LENGTH);
	//res = core_read(core_config->slave_i2c_addr, data, GESTURE_MORMAL_LENGTH);
	if (data[0] != protocol->ges_pid) {
		TPD_INFO("get gesture packet id error data[0] = 0x%X", data[0]);
		res = -1;
	}
	return res;
}
EXPORT_SYMBOL(core_fr_get_gesture_data);

/**
 * The function is an entry for the work queue registered by ISR activates.
 *
 * Here will allocate the size of packet depending on what the current protocol
 * is used on its firmware.
 */
void core_fr_handler(void)
{
	int i = 0;
    int res = 0;
	uint8_t *tdata = NULL;
	mutex_lock(&ipd->plat_mutex);
	if (core_fr->isEnableFR && core_fr->handleint) {
		g_total_len = calc_packet_length();
		if (g_total_len > 0) {
			g_fr_node = kmalloc(sizeof(*g_fr_node), GFP_ATOMIC);
			if (ERR_ALLOC_MEM(g_fr_node)) {
				TPD_INFO("Failed to allocate g_fr_node memory %ld\n", PTR_ERR(g_fr_node));
				goto out;
			}

			g_fr_node->data = kcalloc(g_total_len, sizeof(uint8_t), GFP_ATOMIC);
			if (ERR_ALLOC_MEM(g_fr_node->data)) {
				TPD_INFO("Failed to allocate g_fr_node memory %ld\n", PTR_ERR(g_fr_node->data));
				goto out;
			}

			g_fr_node->len = g_total_len;
			memset(g_fr_node->data, 0xFF, (uint8_t) sizeof(uint8_t) * g_total_len);

			while (i < ARRAY_SIZE(fr_t)) {
				if (protocol->major == fr_t[i].protocol_marjor_ver) {
					res = fr_t[i].finger_report();
                    if (res < 0) {
                        TPD_INFO("set pointid_info = -1\n");
                        g_mutual_data.pointid_info = -1;
                    }
					/* 2048 is referred to the defination by user */
					if (g_total_len < 2048) {
						//tdata = kmalloc(g_total_len, GFP_ATOMIC);
						tdata = kmalloc(2048, GFP_ATOMIC);
						if (ERR_ALLOC_MEM(tdata)) {
							TPD_INFO("Failed to allocate g_fr_node memory %ld\n",
								PTR_ERR(tdata));
							goto out;
						}

						memcpy(tdata, g_fr_node->data, g_fr_node->len);
						/* merge uart data if it's at i2cuart mode */
						if (g_fr_uart != NULL)
							memcpy(tdata + g_fr_node->len, g_fr_uart->data, g_fr_uart->len);
					} else {
						TPD_INFO("total length (%d) is too long than user can handle\n",
							g_total_len);
						goto out;
					}

					if (core_fr->isEnableNetlink) {
						//netlink_reply_msg(tdata, g_total_len);
						netlink_reply_msg(tdata, 2048);
					}

					if (ipd->debug_node_open) {
						mutex_lock(&ipd->ilitek_debug_mutex);
						if (!ERR_ALLOC_MEM(ipd->debug_buf) && !ERR_ALLOC_MEM(ipd->debug_buf[ipd->debug_data_frame])) {
							memset(ipd->debug_buf[ipd->debug_data_frame], 0x00,
							       (uint8_t) sizeof(uint8_t) * 2048);
							memcpy(ipd->debug_buf[ipd->debug_data_frame], tdata, g_total_len);
						}
						else {
							TPD_INFO("Failed to malloc debug_buf\n");
						}
						ipd->debug_data_frame++;
						if (ipd->debug_data_frame > 1) {
							TPD_INFO("ipd->debug_data_frame = %d\n", ipd->debug_data_frame);
						}
						if (ipd->debug_data_frame > 1023) {
							TPD_INFO("ipd->debug_data_frame = %d > 1023\n",
								ipd->debug_data_frame);
							ipd->debug_data_frame = 1023;
						}
						mutex_unlock(&ipd->ilitek_debug_mutex);
						wake_up(&(ipd->inq));
					}
					break;
				}
				i++;
			}

			if (i >= ARRAY_SIZE(fr_t))
				TPD_INFO("Can't find any callback functions to handle INT event\n");
		}
		else {
			TPD_INFO("Wrong the length of packet\n");
		}
	} else {
		TPD_INFO("The figner report was disabled\n");
		core_fr->handleint = true;
	}

out:
	mutex_unlock(&ipd->plat_mutex);
	if (CHECK_RECOVER == g_total_len) {
			TPD_INFO("==================Recover=================\n");
		#ifdef CHECK_REG
			res = core_config_ice_mode_enable();
			if (res < 0) {
				TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
			}
			core_get_tp_register();
		#ifdef CHECK_DDI_REG
			core_get_ddi_register();
		#endif
			core_config_ice_mode_disable();
			core_spi_rx_check_test();
		#endif
			ilitek_reset_for_esd((void *)ipd);
	}
	ipio_kfree((void **)&tdata);

	if(!ERR_ALLOC_MEM(g_fr_node)) {
		ipio_kfree((void **)&g_fr_node->data);
		ipio_kfree((void **)&g_fr_node);
	}

	if(!ERR_ALLOC_MEM(g_fr_uart)) {
		ipio_kfree((void **)&g_fr_uart->data);
		ipio_kfree((void **)&g_fr_uart);
	}

	g_total_len = 0;
	TPD_DEBUG("handle INT done\n");
}
EXPORT_SYMBOL(core_fr_handler);

void core_fr_input_set_param(struct input_dev *input_device)
{
	int max_x = 0;
	int max_y = 0;
	int min_x = 0;
	int min_y = 0;
	int max_tp = 0;

	core_fr->input_device = input_device;

	/* set the supported event type for input device */
	set_bit(EV_ABS, core_fr->input_device->evbit);
	set_bit(EV_SYN, core_fr->input_device->evbit);
	set_bit(EV_KEY, core_fr->input_device->evbit);
	set_bit(BTN_TOUCH, core_fr->input_device->keybit);
	set_bit(BTN_TOOL_FINGER, core_fr->input_device->keybit);
	set_bit(INPUT_PROP_DIRECT, core_fr->input_device->propbit);

	if (core_fr->isSetResolution) {
		max_x = core_config->tp_info->nMaxX;
		max_y = core_config->tp_info->nMaxY;
		min_x = core_config->tp_info->nMinX;
		min_y = core_config->tp_info->nMinY;
		max_tp = core_config->tp_info->nMaxTouchNum;
	} else {
		max_x = TOUCH_SCREEN_X_MAX;
		max_y = TOUCH_SCREEN_Y_MAX;
		min_x = TOUCH_SCREEN_X_MIN;
		min_y = TOUCH_SCREEN_Y_MIN;
		max_tp = MAX_TOUCH_NUM;
	}

	TPD_INFO("input resolution : max_x = %d, max_y = %d, min_x = %d, min_y = %d\n", max_x, max_y, min_x, min_y);
	TPD_INFO("input touch number: max_tp = %d\n", max_tp);

#if (TP_PLATFORM != PT_MTK)
	input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_X, min_x, max_x - 1, 0, 0);
	input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_Y, min_y, max_y - 1, 0, 0);

	input_set_abs_params(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
#endif /* PT_MTK */
	TPD_INFO("\n");
	if (core_fr->isEnablePressure)
		input_set_abs_params(core_fr->input_device, ABS_MT_PRESSURE, 0, 255, 0, 0);

#ifdef MT_B_TYPE
	#if KERNEL_VERSION(3, 7, 0) <= LINUX_VERSION_CODE
	input_mt_init_slots(core_fr->input_device, max_tp, INPUT_MT_DIRECT);
	#else
	input_mt_init_slots(core_fr->input_device, max_tp);
	#endif /* LINUX_VERSION_CODE */
#else
	input_set_abs_params(core_fr->input_device, ABS_MT_TRACKING_ID, 0, max_tp, 0, 0);
#endif /* MT_B_TYPE */
	TPD_INFO("\n");
	/* Set up virtual key with gesture code */
	//core_gesture_init(core_fr);
}
EXPORT_SYMBOL(core_fr_input_set_param);

int core_fr_init(void)
{
	int i = 0;

	core_fr = kzalloc(sizeof(*core_fr), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_fr)) {
		TPD_INFO("Failed to allocate core_fr mem, %ld\n", PTR_ERR(core_fr));
		core_fr_remove();
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(ipio_chip_list); i++) {
		if (ipio_chip_list[i] == TP_TOUCH_IC) {
			core_fr->isEnableFR = true;
			core_fr->handleint = true;
			core_fr->isEnableNetlink = false;
			core_fr->isEnablePressure = false;
			core_fr->isSetResolution = false;
			core_fr->actual_fw_mode = protocol->demo_mode;
			return 0;
		}
	}
    core_fr->input_device = ipd->ts->input_dev;

	TPD_INFO("Can't find this chip in support list\n");
	return 0;
}
EXPORT_SYMBOL(core_fr_init);

void core_fr_remove(void)
{
	TPD_INFO("Remove core-FingerReport members\n");
	ipio_kfree((void **)&core_fr);
}
EXPORT_SYMBOL(core_fr_remove);
