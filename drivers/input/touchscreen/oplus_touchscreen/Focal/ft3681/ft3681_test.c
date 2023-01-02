// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "ft3681_core.h"

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "ft3681-test"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (LEVEL_DEBUG == tp_debug) {\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DETAIL(a, arg...)\
        do {\
                if (LEVEL_BASIC != tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
        do {\
                if (tp_debug) {\
                        printk(a, ##arg);\
                }\
        }while(0)


#define FTS_TEST_FUNC_ENTER() do { \
    TPD_INFO("[FTS_TS][TEST]%s: Enter\n", __func__); \
} while (0)

#define FTS_TEST_FUNC_EXIT()  do { \
    TPD_INFO("[FTS_TS][TEST]%s: Exit(%d)\n", __func__, __LINE__); \
} while (0)


#define FTS_TEST_SAVE_INFO(fmt, args...) do { \
    if (g_ft3681_data->s) { \
        seq_printf(g_ft3681_data->s, fmt, ##args); \
    } \
    TPD_INFO(fmt, ##args); \
} while (0)

#define FTS_TEST_SAVE_ERR(fmt, args...)  do { \
    if (g_ft3681_data->s) { \
        seq_printf(g_ft3681_data->s, fmt, ##args); \
    } \
    TPD_INFO(fmt, ##args); \
} while (0)

#define SHORT_MIN_CA                    600

#define  SCAP_CB_ON_GCB_MIN             0
#define  SCAP_CB_ON_GCB_MAX             125
#define  SCAP_CB_OFF_GCB_MIN            0
#define  SCAP_CB_OFF_GCB_MAX            125

enum wp_type {
	WATER_PROOF_OFF = 0,
	WATER_PROOF_ON = 1,
	HIGH_SENSITIVITY = 2,
	HOV = 3,
	WATER_PROOF_ON_TX = 100,
	WATER_PROOF_ON_RX,
	WATER_PROOF_OFF_TX,
	WATER_PROOF_OFF_RX,
};

enum byte_mode {
	DATA_ONE_BYTE,
	DATA_TWO_BYTE,
};

enum normalize_type {
	NORMALIZE_OVERALL,
	NORMALIZE_AUTO,
};


#define MAX_LENGTH_TEST_NAME            64


static void sys_delay(int ms)
{
	msleep(ms);
}

int ft3681_abs(int value)
{
	if (value < 0) {
		value = 0 - value;
	}

	return value;
}

void ft3681_print_buffer(int *buffer, int length, int line_num)
{
	int i = 0;
	int j = 0;
	int tmpline = 0;
	char *tmpbuf = NULL;
	int tmplen = 0;
	int cnt = 0;

	if ((NULL == buffer) || (length <= 0)) {
		TPD_INFO("buffer/length(%d) fail", length);
		return;
	}

	tmpline = line_num ? line_num : length;
	tmplen = tmpline * 6 + 128;
	tmpbuf = kzalloc(tmplen, GFP_KERNEL);

	if (!tmpbuf) {
		TPD_INFO("%s, alloc failed \n", __func__);
		return;
	}

	for (i = 0; i < length; i = i + tmpline) {
		cnt = 0;

		for (j = 0; j < tmpline; j++) {
			cnt += snprintf(tmpbuf + cnt, tmplen - cnt, "%5d ", buffer[i + j]);

			if ((cnt >= tmplen) || ((i + j + 1) >= length)) {
				break;
			}
		}

		TPD_DEBUG("%s", tmpbuf);
	}

	if (tmpbuf) {
		kfree(tmpbuf);
		tmpbuf = NULL;
	}
}

/********************************************************************
 * test read/write interface
 *******************************************************************/
static int ft3681_test_bus_read(u8 *cmd, int cmdlen, u8 *data, int datalen)
{
	int ret = 0;

	ret = ft3681_read(cmd, cmdlen, data, datalen);
	if (ret < 0) {
		return ret;
	} else {
		return 0;
	}
}

static int ft3681_test_bus_write(u8 *writebuf, int writelen)
{
	int ret = 0;

	ret = ft3681_write(writebuf, writelen);
	if (ret < 0) {
		return ret;
	} else {
		return 0;
	}
}

static int ft3681_test_read_reg(u8 addr, u8 *val)
{
	return ft3681_test_bus_read(&addr, 1, val, 1);
}

static int ft3681_test_write_reg(u8 addr, u8 val)
{
	int ret;
	u8 cmd[2] = {0};

	cmd[0] = addr;
	cmd[1] = val;
	ret = ft3681_test_bus_write(cmd, 2);

	return ret;
}

static int ft3681_test_read(u8 addr, u8 *readbuf, int readlen)
{
	int ret = 0;
	int i = 0;
	int packet_length = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int byte_num = readlen;

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;

	if (packet_remainder) {
		packet_num++;
	}

	if (byte_num < BYTES_PER_TIME) {
		packet_length = byte_num;

	} else {
		packet_length = BYTES_PER_TIME;
	}

	/* FTS_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

	ret = ft3681_test_bus_read(&addr, 1, &readbuf[offset], packet_length);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read buffer fail\n");
		return ret;
	}

	for (i = 1; i < packet_num; i++) {
		offset += packet_length;

		if ((i == (packet_num - 1)) && packet_remainder) {
			packet_length = packet_remainder;
		}


		ret = ft3681_test_bus_read(&addr, 1, &readbuf[offset], packet_length);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("read buffer fail\n");
			return ret;
		}
	}

	return 0;
}

static int ft3681_test_write(u8 addr, u8 *writebuf, int writelen)
{
	int ret = 0;
	int i = 0;
	u8 *data = NULL;
	int packet_length = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int byte_num = writelen;

	data = kzalloc(BYTES_PER_TIME + 1, GFP_KERNEL);
	if (!data) {
		FTS_TEST_SAVE_ERR("malloc memory for bus write data fail\n");
		return -ENOMEM;
	}

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;
	if (packet_remainder) {
		packet_num++;
	}

	if (byte_num < BYTES_PER_TIME) {
		packet_length = byte_num;
	} else {
		packet_length = BYTES_PER_TIME;
	}
	/* FTS_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

	data[0] = addr;
	for (i = 0; i < packet_num; i++) {
		if (i != 0) {
			data[0] = addr + 1;
		}
		if ((i == (packet_num - 1)) && packet_remainder) {
			packet_length = packet_remainder;
		}
		memcpy(&data[1], &writebuf[offset], packet_length);

		ret = ft3681_test_bus_write(data, packet_length + 1);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("write buffer fail\n");
			kfree(data);
			return ret;
		}

		offset += packet_length;
	}

	kfree(data);
	return 0;
}
/*
 * wait_state_update - wait fw status update
 */
static int wait_state_update(u8 retval)
{
	int ret = 0;
	int times = 0;
	u8 addr = 0;
	u8 state = 0xFF;

	addr = FACTORY_REG_PARAM_UPDATE_STATE_TOUCH;
	while (times++ < FACTORY_TEST_RETRY) {
		sys_delay(FACTORY_TEST_DELAY);
		/* Wait register status update */
		state = 0xFF;
		ret = ft3681_test_read_reg(addr, &state);
		if ((ret >= 0) && (retval == state)) {
			break;
		} else {
			TPD_INFO("reg%x=%x,retry:%d", addr, state, times);
		}
	}

	if (times >= FACTORY_TEST_RETRY) {
		FTS_TEST_SAVE_ERR("Wait State Update fail,reg%x=%x\n", addr, state);
		return -EIO;
	}

	return 0;
}


/*
 * read_mass_data - read rawdata/short test data
 * addr - register addr which read data from
 * byte_num - read data length, unit:byte
 * buf - save data
 *
 * return 0 if read data succuss, otherwise return error code
 */
static int read_mass_data(u8 addr, int byte_num, int *buf)
{
	int ret = 0;
	int i = 0;
	u8 *data = NULL;

	data = (u8 *)kzalloc(byte_num * sizeof(u8), GFP_KERNEL);

	if (NULL == data) {
		FTS_TEST_SAVE_ERR("mass data buffer malloc fail\n");
		return -ENOMEM;
	}

	/* read rawdata buffer */
	TPD_INFO("mass data len:%d", byte_num);
	ret = ft3681_test_read(addr, data, byte_num);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read mass data fail\n");
		goto read_massdata_err;
	}

	for (i = 0; i < byte_num; i = i + 2) {
		buf[i >> 1] = (int)(short)((data[i] << 8) + data[i + 1]);
	}

	ret = 0;
read_massdata_err:
	kfree(data);
	return ret;
}

static void ft3681_test_save_data(char *name, int *data, int datacnt, int line, int fd)
{
	char *data_buf = NULL;
	u32 cnt = 0;
	u32 max_size = (datacnt * 8 + 128);
	int i = 0;

	if ((fd < 0) || !name || !data || !datacnt || !line) {
		FTS_TEST_SAVE_ERR("fd/name/data/datacnt/line is invalid\n");
		return;
	}

	data_buf = kzalloc(max_size, GFP_KERNEL);
	if (!data_buf) {
		FTS_TEST_SAVE_ERR("kzalloc for data_buf fail\n");
		return;
	}

	for (i = 0; i < datacnt; i++) {
		cnt += snprintf(data_buf + cnt, max_size - cnt, "%d,", data[i]);
		if ((i + 1) % line == 0) {
			cnt += snprintf(data_buf + cnt, max_size - cnt, "\n");
		}
	}

	if (i % line != 0) {
		cnt += snprintf(data_buf + cnt, max_size - cnt, "\n");
	}

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
	ksys_write(fd, data_buf, cnt);
#else
	sys_write(fd, data_buf, cnt);
#endif
	kfree(data_buf);
}


/********************************************************************
 * test global function enter work/factory mode
 *******************************************************************/
static int enter_work_mode(void)
{
	int ret = 0;
	u8 mode = 0;
	int i = 0;
	int j = 0;

	TPD_INFO("%s +\n", __func__);
	ret = ft3681_test_read_reg(DEVIDE_MODE_ADDR, &mode);

	if ((ret >= 0) && (0x00 == mode)) {
		return 0;
	}

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = ft3681_test_write_reg(DEVIDE_MODE_ADDR, 0x00);

		if (ret >= 0) {
			sys_delay(FACTORY_TEST_DELAY);

			for (j = 0; j < 20; j++) {
				ret = ft3681_test_read_reg(DEVIDE_MODE_ADDR, &mode);

				if ((ret >= 0) && (0x00 == mode)) {
					TPD_INFO("enter work mode success");
					return 0;

				} else {
					sys_delay(FACTORY_TEST_DELAY);
				}
			}
		}

		sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		FTS_TEST_SAVE_ERR("Enter work mode fail\n");
		return -EIO;
	}

	TPD_INFO("%s -\n", __func__);
	return 0;
}


static int ft3681_special_operation_for_samsung(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;

	if (true ==
	    ts_data->use_panelfactory_limit) {                      /*only for firmware released to samsung factory*/
		ret = ft3681_test_write_reg(FT3681_REG_SAMSUNG_SPECIFAL, 0x01);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("write FT3681_REG_SAMSUNG_SPECIFAL fail at %s,ret=%d\n", __func__, ret);
			return -EIO;
		}
	}

	return ret;
}


#define FT3681_FACTORY_MODE 0x40
static int enter_factory_mode(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	u8 mode = 0;
	int i = 0;
	int j = 0;

	ret = ft3681_test_read_reg(DEVIDE_MODE_ADDR, &mode);

	if ((ret >= 0) && (FT3681_FACTORY_MODE == mode)) {
		ft3681_special_operation_for_samsung(ts_data);
		return 0;
	}

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = ft3681_test_write_reg(DEVIDE_MODE_ADDR, 0x40);

		if (ret >= 0) {
			sys_delay(FACTORY_TEST_DELAY);

			for (j = 0; j < 20; j++) {
				ret = ft3681_test_read_reg(DEVIDE_MODE_ADDR, &mode);

				if ((ret >= 0) && (FT3681_FACTORY_MODE == mode)) {
					TPD_INFO("enter factory mode success");
					sys_delay(200);
					ft3681_special_operation_for_samsung(ts_data);
					return 0;

				} else {
					sys_delay(FACTORY_TEST_DELAY);
				}
			}
		}

		sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		FTS_TEST_SAVE_ERR("Enter factory mode fail\n");
		return -EIO;
	}

	ft3681_special_operation_for_samsung(ts_data);
	return 0;
}

static int get_channel_num(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	u8 tx_num = 0;
	u8 rx_num = 0;

	ret = ft3681_test_read_reg(FACTORY_REG_CHX_NUM, &tx_num);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read tx_num register fail\n");
		return ret;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_CHY_NUM, &rx_num);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read rx_num register fail\n");
		return ret;
	}

	if ((tx_num != ts_data->hw_res->TX_NUM) || (rx_num != ts_data->hw_res->RX_NUM)) {
		FTS_TEST_SAVE_ERR("channel num check fail, tx_num:%d-%d, rx_num:%d-%d\n",
				  tx_num, ts_data->hw_res->TX_NUM,
				  rx_num, ts_data->hw_res->RX_NUM);
		return -EIO;
	}

	return 0;
}

static int read_rawdata(u8 off_addr, u8 off_val, u8 rawdata_addr, int byte_num,
			int *data)
{
	int ret = 0;

	/* set line addr or rawdata start addr */
	ret = ft3681_test_write_reg(off_addr, off_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wirte line/start addr fail\n");
		return ret;
	}

	ret = read_mass_data(rawdata_addr, byte_num, data);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read rawdata fail\n");
		return ret;
	}

	return 0;
}

static int start_scan(void)
{
	int ret = 0;
	u8 addr = 0;
	u8 val = 0;
	u8 finish_val = 0;
	int times = 0;

	addr = DEVIDE_MODE_ADDR;
	val = 0xC0;
	finish_val = 0x40;

	/* write register to start scan */
	ret = ft3681_test_write_reg(addr, val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write start scan mode fail\n");
		return ret;
	}

	/* Wait for the scan to complete */
	while (times++ < FACTORY_TEST_RETRY) {
		sys_delay(FACTORY_TEST_DELAY);

		ret = ft3681_test_read_reg(addr, &val);

		if ((ret >= 0) && (val == finish_val)) {
			break;

		} else {
			TPD_INFO("reg%x=%x,retry:%d", addr, val, times);
		}
	}

	if (times >= FACTORY_TEST_RETRY) {
		FTS_TEST_SAVE_ERR("scan timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * start_scan - start to scan a frame
 */
int ft3681_start_scan(int frame_num)
{
	int ret = 0;
	u8 addr = 0;
	u8 val = 0;
	u8 finish_val = 0;
	int times = 0;

	addr = DEVIDE_MODE_ADDR;
	val = 0xC0;
	finish_val = 0x40;

	/* write register to start scan */
	ret = ft3681_test_write_reg(addr, val);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write start scan mode fail\n");
		return ret;
	}

	sys_delay(frame_num * FACTORY_TEST_DELAY / 2);
	/* Wait for the scan to complete */
	while (times++ < 100) {
		sys_delay(FACTORY_TEST_DELAY);

		ret = ft3681_test_read_reg(addr, &val);
		if ((ret >= 0) && (val == finish_val)) {
			break;
		} else {
			TPD_INFO("reg%x=%x,retry:%d", addr, val, times);
		}
	}

	if (times >= 100) {
		FTS_TEST_SAVE_ERR("scan timeout\n");
		return -EIO;
	}

	return 0;
}

static bool get_fw_wp(u8 wp_ch_sel, enum wp_type water_proof_type)
{
	bool fw_wp_state = false;

	switch (water_proof_type) {
	case WATER_PROOF_ON:
		/* bit5: 0-check in wp on, 1-not check */
		fw_wp_state = !(wp_ch_sel & 0x20);
		break;

	case WATER_PROOF_ON_TX:
		/* Bit6:  0-check Rx+Tx in wp mode  1-check one channel
		   Bit2:  0-check Tx in wp mode;  1-check Rx in wp mode
		*/
		fw_wp_state = (!(wp_ch_sel & 0x40) || !(wp_ch_sel & 0x04));
		break;

	case WATER_PROOF_ON_RX:
		fw_wp_state = (!(wp_ch_sel & 0x40) || (wp_ch_sel & 0x04));
		break;

	case WATER_PROOF_OFF:
		/* bit7: 0-check in wp off, 1-not check */
		fw_wp_state = !(wp_ch_sel & 0x80);
		break;

	case WATER_PROOF_OFF_TX:
		/* Bit1-0:  00-check Tx in non-wp mode
		            01-check Rx in non-wp mode
		            10:check Rx+Tx in non-wp mode
		*/
		fw_wp_state = ((0x0 == (wp_ch_sel & 0x03)) || (0x02 == (wp_ch_sel & 0x03)));
		break;

	case WATER_PROOF_OFF_RX:
		fw_wp_state = ((0x01 == (wp_ch_sel & 0x03)) || (0x02 == (wp_ch_sel & 0x03)));
		break;

	default:
		break;
	}

	return fw_wp_state;
}

static int get_cb_ft5662(int *cb_buf, int byte_num, bool is_cf)
{
	int ret = 0;
	int i = 0;
	u8 cb[SC_NUM_MAX] = { 0 };

	if (byte_num > SC_NUM_MAX) {
		FTS_TEST_SAVE_ERR("CB byte(%d)>max(%d)", byte_num, SC_NUM_MAX);
		return -EINVAL;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_MC_SC_CB_H_ADDR_OFF, 0);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write cb_h addr offset fail\n");
		return ret;
	}


	ret = ft3681_test_write_reg(FACTORY_REG_MC_SC_CB_ADDR_OFF, 0);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write cb addr offset fail\n");
		return ret;
	}

	ret = ft3681_test_read(FACTORY_REG_MC_SC_CB_ADDR, cb, byte_num);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read cb fail\n");
		return ret;
	}

	for (i = 0; i < byte_num; i++) {
		if (is_cf) {
			cb_buf[i] = (cb[i] & 0x80) ? -((cb[i] & 0x3F)) : (cb[i] & 0x3F);
		} else {
			cb_buf[i] = (cb[i] & 0x80) ? -((cb[i] & 0x7F)) : (cb[i] & 0x7F);
		}
	}

	return 0;
}

static int get_rawdata_mc_sc(enum wp_type wp, int byte_num, int *data)
{
	int ret = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;

	addr = FACTORY_REG_LINE_ADDR;
	rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;

	if (WATER_PROOF_ON == wp) {
		val = 0xAC;

	} else if (WATER_PROOF_OFF == wp) {
		val = 0xAB;
	} else if (HIGH_SENSITIVITY == wp) {
		val = 0xA0;
	} else if (HOV == wp) {
		val = 0xA1;
		byte_num = 4 * 2;
	}

	ret = read_rawdata(addr, val, rawdata_addr, byte_num, data);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read rawdata fail\n");
		return ret;
	}

	return 0;
}

static bool compare_mc_sc(struct chip_data_ft3681 *ts_data, bool tx_check,
			  bool rx_check, int *data, int *min, int *max)
{
	int i = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int channel_num = tx_num + rx_num;
	bool result = true;

	if (rx_check) {
		for (i = 0; i < rx_num; i++) {
			if (0 == ts_data->mpt.thr.node_valid_sc[i]) {
				continue;
			}

			if ((data[i] < min[i]) || (data[i] > max[i])) {
				TPD_INFO("rx check ERR [%d]: [%d] > [%d] > [%d] \n", i, max[i], data[i],
					 min[i]);
				FTS_TEST_SAVE_ERR("test fail,rx%d=%5d,range=(%5d,%5d)\n",
						  i + 1, data[i], min[i], max[i]);
				result = false;
			}
		}
	}

	if (tx_check) {
		for (i = rx_num; i < channel_num; i++) {
			if (0 == ts_data->mpt.thr.node_valid_sc[i]) {
				continue;
			}

			if ((data[i] < min[i]) || (data[i] > max[i])) {
				TPD_INFO("tx check ERR [%d]: [%d] > [%d] > [%d] \n", i, max[i], data[i],
					 min[i]);
				FTS_TEST_SAVE_INFO("test fail,tx%d=%5d,range=(%5d,%5d)\n",
						   i - rx_num + 1, data[i], min[i], max[i]);
				result = false;
			}
		}
	}

	return result;
}

static int short_get_adc_data_mc(u8 retval, int byte_num, int *adc_buf, u8 mode)
{
	int ret = 0;
	int i = 0;
	u8 short_state = 0;

	FTS_TEST_FUNC_ENTER();
	/* select short test mode & start test */
	ret = ft3681_test_write_reg(FACTROY_REG_SHORT2_TEST_EN, mode);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write short test mode fail\n");
		goto test_err;
	}

	for (i = 0; i < FACTORY_TEST_RETRY; i++) {
		sys_delay(FACTORY_TEST_RETRY_DELAY);

		ret = ft3681_test_read_reg(FACTROY_REG_SHORT2_TEST_STATE, &short_state);

		if ((ret >= 0) && (retval == short_state)) {
			break;
		} else
			TPD_DEBUG("reg%x=%x,retry:%d",
				  FACTROY_REG_SHORT2_TEST_STATE, short_state, i);
	}

	if (i >= FACTORY_TEST_RETRY) {
		FTS_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
		ret = -EIO;
		goto test_err;
	}

	ret = read_mass_data(FACTORY_REG_SHORT2_ADDR_MC, byte_num, adc_buf);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get short(adc) data fail\n");
	}

	/*    TPD_DEBUG("adc data:\n");*/
	/*    ft3681_print_buffer(adc_buf, byte_num / 2, 0);*/
test_err:
	FTS_TEST_FUNC_EXIT();
	return ret;
}

static int short_test_ch_to_all(struct chip_data_ft3681 *ts_data,
				int *res, u8 *ab_ch, bool *result)
{
	int ret = 0;
	int i = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int min_ca = SHORT_MIN_CA;
	int ch_num = tx_num + rx_num;
	int byte_num = 0;
	u8 ab_ch_num = 0;

	TPD_INFO("short test:channel to all other\n");

	ret = ft3681_test_write_reg(FACTROY_REG_SHORT2_RES_LEVEL, 1);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write short resistor level fail\n");
		return ret;
	}
	/*get resistance data*/
	byte_num = (ch_num + 1) * 2;
	ret = short_get_adc_data_mc(TEST_RETVAL_AA, byte_num, &res[0], \
				    FACTROY_REG_SHORT2_CA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get weak short data fail,ret:%d\n", ret);
		return ret;
	}

	*result = true;
	for (i = 0; i < ch_num; i++) {
		if (res[i] < min_ca) {
			ab_ch_num++;
			ab_ch[ab_ch_num] = i + 1;
			*result = false;
		}
	}

	if (ab_ch_num) {
		ft3681_print_buffer(res, ch_num, ch_num);
		ab_ch[0] = ab_ch_num;
		TPD_INFO("[FTS_TS]ab_ch:");
		for (i = 0; i < ab_ch_num + 1; i++) {
			TPD_INFO("%2d ", ab_ch[i]);
		}
		TPD_INFO("\n");
	}

	return 0;
}

static void ft3681_show_null_noise(int *null_noise, int rx_num)
{
	int i = 0;
	int cnt = 0;
	char tmpbuf[512] = { 0 };

	/*show noise*/
	TPD_INFO("null noise:%d", null_noise[0]);
	for (i = 0; i < (rx_num * 3); i = i + 1) {
		cnt += snprintf(tmpbuf + cnt, 512 - cnt, "%5d,", null_noise[i + 1]);
		if (((i + 1) % rx_num) == 0) {
			cnt = 0;
			TPD_INFO("%s", tmpbuf);
		}
	}
}

static void ft3681_get_null_noise(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	uint8_t data_buf[64];
	int *null_noise;
	int null_byte_num = 0;

	null_byte_num = ts_data->hw_res->RX_NUM * 3 + 1;//ts_data->fre_num + 1;
	TPD_INFO("null noise num:%d", null_byte_num);
	null_noise = kzalloc(null_byte_num * sizeof(int), GFP_KERNEL);
	if (!null_noise) {
		TPD_INFO("null_noise malloc fail");
		return;
	}

	null_byte_num = null_byte_num * 2;
	ret = read_rawdata(FACTORY_REG_LINE_ADDR, 0xB0, 0xCE, null_byte_num,
			   null_noise);
	if (ret < 0) {
		TPD_INFO("read null noise fail\n");
	} else {
		ts_data->null_noise_value = null_noise[0];
		snprintf(data_buf, 64, "%d,", null_noise[0]);
		//tp_test_write(focal_testdata->fp, focal_testdata->length, data_buf,
		//              strlen(data_buf), focal_testdata->pos);
		ft3681_show_null_noise(&null_noise[1], ts_data->hw_res->RX_NUM);
	}

	kfree(null_noise);
	null_noise = NULL;
}



#define NUM_MODE 2
#define TEST_RESULT_NORMAL     0
#define TEST_RESULT_ABNORMAL  -1
static int ft3681_auto_preoperation(struct chip_data_ft3681 *ts_data)
{
	int node_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM;
	int channel_num = ts_data->hw_res->TX_NUM + ts_data->hw_res->RX_NUM;
	//int ret;

	ts_data->noise_rawdata = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!ts_data->noise_rawdata) {
		FTS_TEST_SAVE_ERR("kzalloc for noise_rawdata fail\n");
		goto alloc_err;
	}

	ts_data->rawdata = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!ts_data->rawdata) {
		FTS_TEST_SAVE_ERR("kzalloc for rawdata fail\n");
		goto alloc_err;
	}

	ts_data->panel_differ = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!ts_data->panel_differ) {
		FTS_TEST_SAVE_ERR("kzalloc for panel_differ fail\n");
		goto alloc_err;
	}

	ts_data->scap_cb = (int *)kzalloc(channel_num * NUM_MODE * sizeof(int), GFP_KERNEL);
	if (!ts_data->scap_cb) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_cb fail\n");
		goto alloc_err;
	}

	ts_data->scap_rawdata = (int *)kzalloc(channel_num * NUM_MODE * sizeof(int), GFP_KERNEL);
	if (!ts_data->scap_rawdata) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_rawdata fail\n");
		goto alloc_err;
	}

	ts_data->rawdata_linearity = kzalloc(node_num * 2 * sizeof(int), GFP_KERNEL);
	if (!ts_data->rawdata_linearity) {
		FTS_TEST_SAVE_ERR("ts_data->rawdata_linearity buffer malloc fail\n");
		goto alloc_err;
	}

	return 0;

alloc_err:
	if (ts_data->rawdata_linearity) {
		kfree(ts_data->rawdata_linearity);
		ts_data->rawdata_linearity = NULL;
	}

	if (ts_data->scap_rawdata) {
		kfree(ts_data->scap_rawdata);
		ts_data->scap_rawdata = NULL;
	}

	if (ts_data->scap_cb) {
		kfree(ts_data->scap_cb);
		ts_data->scap_cb = NULL;
	}

	if (ts_data->panel_differ) {
		kfree(ts_data->panel_differ);
		ts_data->panel_differ = NULL;
	}

	if (ts_data->rawdata) {
		kfree(ts_data->rawdata);
		ts_data->rawdata = NULL;
	}

	if (ts_data->noise_rawdata) {
		kfree(ts_data->noise_rawdata);
		ts_data->noise_rawdata = NULL;
	}

	return TEST_RESULT_ABNORMAL;
}



static int ft3681_noise_autotest(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	int i = 0;
	u8 fre = 0;
	u8 reg06_val = 0;
	u8 reg0d_val = 0;
	u8 reg1a_val = 0;
	u8 reg1b_val = 0;
	u8 rawdata_addr = 0;
	bool result = false;
	int frame_num = 20;
	int byte_num = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Noise Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_noise_data_P || !ts_data->ft3681_autotest_offset->ft3681_noise_data_N) {
		TPD_INFO("ft3681_noise_data_P || ft3681_noise_data_N is NULL");
		return 0;
	}

	if (!thr || !thr->node_valid || !ts_data->noise_rawdata) {
		FTS_TEST_SAVE_ERR("thr/node_valid/rawdata is null\n");
		ret = -EINVAL;
		goto test_err;
	}

	ret = enter_factory_mode(ts_data);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_TOUCH_THR, &reg0d_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read reg0d fail,ret=%d\n", ret);
		goto test_err;
	}

	TPD_INFO("reg0d_val = [%d]\n", reg0d_val);

	/* save origin value */
	ret = ft3681_test_read_reg(FACTORY_REG_DATA_SELECT, &reg06_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read reg06 fail,ret=%d\n", ret);
		goto test_err;
	}

	TPD_INFO("reg06_val = [%d]\n", reg06_val);

	ret = ft3681_test_read_reg(FACTORY_REG_FRE_LIST, &fre);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read fre error,ret=%d\n", ret);
		goto test_err;
	}

	TPD_INFO("fre = [%d]\n", fre);

	ret = ft3681_test_read_reg(0x1A, &reg1a_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x1A error,ret=%d\n", ret);
		goto test_err;
	}

	TPD_INFO("reg1a_val = [%d]\n", reg1a_val);

	ret = ft3681_test_read_reg(0x1B, &reg1b_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x1B error,ret=%d\n", ret);
		goto test_err;
	}

	TPD_INFO("reg1B_val = [%d]\n", reg1b_val);

	ret = ft3681_test_write_reg(FACTORY_REG_DATA_SELECT, 0x01);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set reg06 fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, 0x00);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set fre fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(0x1A, 0x01);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write 0x1A fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_FRAME_NUM, (frame_num >> 8) & 0xFF);
	ret = ft3681_test_write_reg(FACTORY_REG_FRAME_NUM + 1, frame_num & 0xFF);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set frame fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_MAX_DIFF, 0x01);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write 0x1B fail,ret=%d\n", ret);
		goto restore_reg;
	}

	msleep(20);

	ft3681_start_scan(frame_num);

	/* read rawdata */
	rawdata_addr = 0xCE;
	byte_num = node_num * 2;
	ret = read_rawdata(FACTORY_REG_LINE_ADDR, 0xAA, rawdata_addr, byte_num,
			   ts_data->noise_rawdata);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read rawdata fail\n");
		result = false;
		goto restore_reg;
	}

	/* compare */
	result = true;
	if (ts_data->ft3681_autotest_offset->ft3681_noise_data_P && ts_data->ft3681_autotest_offset->ft3681_noise_data_N) {
		for (i = 0; i < node_num; i++) {
			//if ((rawdata[i] > ts_data->ft3681_autotest_offset->ft3681_noise_data_P[i]) || (rawdata[i] < ts_data->ft3681_autotest_offset->ft3681_noise_data_N[i])) {
			if (ts_data->noise_rawdata[i] > ts_data->ft3681_autotest_offset->ft3681_noise_data_P[i]) {
				TPD_INFO("noise data ERR [%d]: [%d] > [%d] > [%d] \n", i, ts_data->ft3681_autotest_offset->ft3681_noise_data_P[i], ts_data->noise_rawdata[i], ts_data->ft3681_autotest_offset->ft3681_noise_data_N[i]);
				FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
						  i / rx_num + 1, i % rx_num + 1, ts_data->noise_rawdata[i], ts_data->ft3681_autotest_offset->ft3681_noise_data_N[i], ts_data->ft3681_autotest_offset->ft3681_noise_data_P[i]);
				result = false;
			}
		}
	} else {
		TPD_INFO("ft3681_raw_data_P || ft3681_raw_data_N is null \n");
		result = false;
	}

	ft3681_get_null_noise(ts_data);
	//ft3681_test_save_data("Noise Test", rawdata, node_num, rx_num, ts_data->csv_fd);

restore_reg:
	ret = ft3681_test_write_reg(0x1B, reg1b_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x1B fail,ret=%d\n", ret);
	}

	/* set the origin value */
	ret = ft3681_test_write_reg(FACTORY_REG_DATA_SELECT, reg06_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore normalize fail,ret=%d\n", ret);
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, fre);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

	ret = ft3681_test_write_reg(0x1A, reg1a_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x1A fail,ret=%d\n", ret);
	}

test_err:
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------Noise Test PASS\n");
	} else {
		* test_result = false;
		FTS_TEST_SAVE_INFO("------Noise Test NG\n");
	}

	FTS_TEST_FUNC_EXIT();
	return ret;
}


static int ft3681_rawdata_autotest(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	int i = 0;
	u8 fre = 0;
	u8 reg06_val = 0;
	u8 reg5b_val = 0;
	u8 rawdata_addr = 0;
	bool result = false;
	int byte_num = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Rawdata Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_raw_data_P || !ts_data->ft3681_autotest_offset->ft3681_raw_data_N) {
		TPD_INFO("ft3681_raw_data_P || ft3681_raw_data_N is NULL");
		return 0;
	}

	if (!thr || !thr->rawdata_h_min || !thr->rawdata_h_max || !thr->node_valid || !ts_data->rawdata) {
		FTS_TEST_SAVE_ERR("thr/rawdata_h_min/max/node/rawdata is null\n");
		ret = -EINVAL;
		goto test_err;
	}

	ret = enter_factory_mode(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	/* save origin value */
	ret = ft3681_test_read_reg(FACTORY_REG_FRE_LIST, &fre);

	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x0A fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(0x5B, &reg5b_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read reg5b fail,ret=%d\n", reg5b_val);
		goto test_err;
	}


	ret = ft3681_test_read_reg(FACTORY_REG_DATA_SELECT, &reg06_val);

	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x06 error,ret=%d\n", ret);
		goto test_err;
	}

	/* set frequecy high */
	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, 0x81);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
		goto restore_reg;
	}

	/* wait fw state update */
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(0x5B, 1);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(0x06, 0);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set 0x06 fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	/*********************GET RAWDATA*********************/
	for (i = 0; i < 3; i++) {
		/* lost 3 frames, in order to obtain stable data */
		/* start scanning */
		ret = start_scan();

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan fail\n");
			continue;
		}

		/* read rawdata */
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
		byte_num = node_num * 2;
		ret = read_rawdata(FACTORY_REG_LINE_ADDR, 0xAA, rawdata_addr, byte_num, ts_data->rawdata);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("read rawdata fail\n");
		}
	}
	if (ret < 0) {
		result = false;
		goto restore_reg;
	}

	/* compare */
	result = true;
	for (i = 0; i < node_num; i++) {
		if (0 == thr->node_valid[i]) {
			continue;
		}

		if ((ts_data->rawdata[i] < ts_data->ft3681_autotest_offset->ft3681_raw_data_N[i]) || (ts_data->rawdata[i] > ts_data->ft3681_autotest_offset->ft3681_raw_data_P[i])) {
			TPD_INFO("raw data ERR [%d]: [%d] > [%d] > [%d] \n", i, ts_data->ft3681_autotest_offset->ft3681_raw_data_P[i], ts_data->rawdata[i], ts_data->ft3681_autotest_offset->ft3681_raw_data_N[i]);
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
					  i / rx_num + 1, i % rx_num + 1, ts_data->rawdata[i],
					  ts_data->ft3681_autotest_offset->ft3681_raw_data_N[i], ts_data->ft3681_autotest_offset->ft3681_raw_data_P[i]);
			result = false;
		}
	}

	//ft3681_test_save_data("Rawdata Test", rawdata, node_num, rx_num, ts_data->csv_fd);

restore_reg:
	/* set the origin value */
	ret = ft3681_test_write_reg(0x5B, reg5b_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x5B fail,ret=%d\n", ret);
	}

	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, fre);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);
	}

	/* wait fw state update */
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

	ret = ft3681_test_write_reg(FACTORY_REG_DATA_SELECT, reg06_val);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x06 fail,ret=%d\n", ret);
	}

test_err:
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------Rawdata Test PASS\n");
	} else {
		*test_result = false;
		FTS_TEST_SAVE_INFO("------Rawdata Test NG\n");
	}

	FTS_TEST_FUNC_EXIT();
	return ret;
}


static int ft3681_uniformity_autotest(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	int row = 0;
	int col = 1;
	int i = 0;
	int deviation = 0;
	int max = 0;
	int *rl_tmp = NULL;
	int offset = 0;
	int offset2 = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;
	bool result = false;
	bool result2 = false;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Rawdata Unfiormity Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P || !ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N) {
		TPD_INFO("ft3681_uniformity_data_P || ft3681_uniformity_data_N is NULL");
		return 0;
	}

	if (!thr || !thr->tx_linearity_max || !thr->rx_linearity_max || !thr->node_valid || !ts_data->rawdata) {
		FTS_TEST_SAVE_ERR("thr/tx/rx_lmax/node_valid/rawdata is null\n");
		ret = -EINVAL;
		goto test_err;
	}

	ret = enter_factory_mode(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}
	result = true;
	//    FTS_TEST_SAVE_INFO("Check Tx Linearity\n");
	ts_data->rl_cnt = 0;
	rl_tmp = ts_data->rawdata_linearity + ts_data->rl_cnt;
	for (row = 0; row < tx_num; row++) {
		for (col = 0; col < rx_num - 1; col++) {
			offset = row * rx_num + col;
			offset2 = row * rx_num + col + 1;
			deviation = abs(ts_data->rawdata[offset] - ts_data->rawdata[offset2]);
			max = max(ts_data->rawdata[offset], ts_data->rawdata[offset2]);
			max = max ? max : 1;
			rl_tmp[offset] = 100 * deviation / max;
		}
	}

	/* compare */
	for (i = 0; i < node_num; i++) {
		if (0 == thr->node_valid[i]) {
			continue;
		}

		if ((rl_tmp[i] < ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i]) || (rl_tmp[i] > ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i])) {
			TPD_INFO("uniformity data ERR [%d]: [%d] > [%d] > [%d] \n", i, ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i], rl_tmp[i], ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i]);
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
					  i / rx_num + 1, i % rx_num + 1, rl_tmp[i],
					  ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i], ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i]);
			result = false;
		}
	}
	ts_data->rl_cnt += node_num;

	result2 = true;
	//    FTS_TEST_SAVE_INFO("Check Rx Linearity\n");
	rl_tmp = ts_data->rawdata_linearity + ts_data->rl_cnt;
	for (row = 0; row < tx_num - 1; row++) {
		for (col = 0; col < rx_num; col++) {
			offset = row * rx_num + col;
			offset2 = (row + 1) * rx_num + col;
			deviation = abs(ts_data->rawdata[offset] - ts_data->rawdata[offset2]);
			max = max(ts_data->rawdata[offset], ts_data->rawdata[offset2]);
			max = max ? max : 1;
			rl_tmp[offset] = 100 * deviation / max;
		}
	}

	/* compare */
	for (i = 0; i < node_num; i++) {
		if (0 == thr->node_valid[i]) {
			continue;
		}

		if ((rl_tmp[i] < ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i]) || (rl_tmp[i] > ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i])) {
			TPD_INFO("uniformity data ERR [%d]: [%d] > [%d] > [%d] \n", i, ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i], rl_tmp[i], ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i]);
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
					  i / rx_num + 1, i % rx_num + 1, rl_tmp[i],
					  ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N[i], ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P[i]);
			result2 = false;
		}
	}
	ts_data->rl_cnt += node_num;

test_err:
	if (result && result2) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------Rawdata Uniformity Test PASS\n");
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("------Rawdata Uniformity Test NG\n");
	}

	FTS_TEST_FUNC_EXIT();
	return ret;
}



static int ft3681_scap_cb_autotest(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	bool tmp_result = false;
	bool tmp2_result = false;
	u8 wc_sel = 0;
	u8 sc_mode = 0;
	bool fw_wp_check = false;
	bool tx_check = false;
	bool rx_check = false;
	int *scb_tmp = NULL;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int channel_num = tx_num + rx_num;
	int byte_num = channel_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Scap CB Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P || !ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N || !ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N || !ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P) {
		TPD_INFO("ft3681_scap_cb_data_P || ft3681_scap_cb_data_N || ft3681_scap_cb_data_waterproof_N || ft3681_scap_cb_data_waterproof_P is NULL");
		return 0;
	}

	if (!thr || !thr->node_valid_sc || !thr->scap_cb_on_min || !thr->scap_cb_on_max
	    || !thr->scap_cb_off_min || !thr->scap_cb_off_max) {
		FTS_TEST_SAVE_ERR("thr/node_valid_sc/scap_cb_on/off_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	ret = enter_factory_mode(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
		goto test_err;
	}

	/* get waterproof channel select */
	ret = ft3681_test_read_reg(FACTORY_REG_WC_SEL, &wc_sel);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read water_channel_sel fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_MC_SC_MODE, &sc_mode);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read sc_mode fail,ret=%d\n", ret);
		goto test_err;
	}

	/* water proof on check */
	ts_data->scb_cnt = 0;
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_ON);
	if (fw_wp_check) {
		scb_tmp = ts_data->scap_cb + ts_data->scb_cnt;
		/* 1:waterproof 0:non-waterproof */
		ret = ft3681_test_write_reg(FACTORY_REG_MC_SC_MODE, WATER_PROOF_ON);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("set mc_sc mode fail\n");
			goto restore_reg;
		}

		ret = wait_state_update(TEST_RETVAL_AA);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("wait state update fail\n");
			goto restore_reg;
		}

		ret = get_cb_ft5662(scb_tmp, byte_num, false);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("read sc_cb fail,ret=%d\n", ret);
			goto restore_reg;
		}

		ret = ft3681_test_read_reg(FACTROY_REG_SCAP_GCB_RX, (u8 *)&ts_data->scap_gcb_rx);
		if (ret) {
			FTS_TEST_SAVE_ERR("read GCB_RX fail,ret=%d\n", ret);
			goto restore_reg;
		}

		ret = ft3681_test_read_reg(FACTROY_REG_SCAP_GCB_TX, (u8 *)&ts_data->scap_gcb_tx);
		if (ret) {
			FTS_TEST_SAVE_ERR("read GCB_TX fail,ret=%d\n", ret);
			goto restore_reg;
		}
		TPD_INFO("GCB:RX=%d,TX=%d", ts_data->scap_gcb_rx, ts_data->scap_gcb_tx);

		/* compare */
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_RX);
		tmp_result = compare_mc_sc(ts_data, tx_check, rx_check, scb_tmp,
					   ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N,
					   ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P);

		if (rx_check && ((ts_data->scap_gcb_rx < SCAP_CB_ON_GCB_MIN) ||
		                 (ts_data->scap_gcb_rx > SCAP_CB_ON_GCB_MAX))) {
			FTS_TEST_SAVE_ERR("test fail,gcb_rx:%5d,range=(%5d,%5d)\n",
			                  ts_data->scap_gcb_rx, SCAP_CB_ON_GCB_MIN,
			                  SCAP_CB_ON_GCB_MAX);
			tmp_result = false;
		}

		if (tx_check && ((ts_data->scap_gcb_tx < SCAP_CB_ON_GCB_MIN) ||
		                 (ts_data->scap_gcb_tx > SCAP_CB_ON_GCB_MAX))) {
			FTS_TEST_SAVE_ERR("test fail,gcb_tx:%5d,range=(%5d,%5d)\n",
			                  ts_data->scap_gcb_tx, SCAP_CB_ON_GCB_MIN,
			                  SCAP_CB_ON_GCB_MAX);
			tmp_result = false;
		}

		ts_data->scb_cnt += channel_num;
	} else {
		tmp_result = true;
	}

	/* water proof off check */
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_OFF);
	if (fw_wp_check) {
		scb_tmp = ts_data->scap_cb + ts_data->scb_cnt;
		/* 1:waterproof 0:non-waterproof */
		ret = ft3681_test_write_reg(FACTORY_REG_MC_SC_MODE, WATER_PROOF_OFF);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("set mc_sc mode fail\n");
			goto restore_reg;
		}

		ret = wait_state_update(TEST_RETVAL_AA);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("wait state update fail\n");
			goto restore_reg;
		}

		ret = get_cb_ft5662(scb_tmp, byte_num, false);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("read sc_cb fail,ret=%d\n", ret);
			goto restore_reg;
		}

		ret = ft3681_test_read_reg(FACTROY_REG_SCAP_GCB_RX, (u8 *)&ts_data->scap_gcb_rx);
		if (ret) {
			FTS_TEST_SAVE_ERR("read GCB_RX fail,ret=%d\n", ret);
			goto restore_reg;
		}

		ret = ft3681_test_read_reg(FACTROY_REG_SCAP_GCB_TX, (u8 *)&ts_data->scap_gcb_tx);
		if (ret) {
			FTS_TEST_SAVE_ERR("read GCB_TX fail,ret=%d\n", ret);
			goto restore_reg;
		}
		TPD_INFO("GCB:RX=%d,TX=%d", ts_data->scap_gcb_rx, ts_data->scap_gcb_tx);

		/* compare */
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_RX);

		tmp2_result = compare_mc_sc(ts_data, tx_check, rx_check, scb_tmp,
					    ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N,
					    ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P);
		if (rx_check && ((ts_data->scap_gcb_rx < SCAP_CB_ON_GCB_MIN) ||
		                 (ts_data->scap_gcb_rx > SCAP_CB_ON_GCB_MAX))) {
			FTS_TEST_SAVE_ERR("test fail,gcb_rx:%5d,range=(%5d,%5d)\n",
			                  ts_data->scap_gcb_rx, SCAP_CB_ON_GCB_MIN,
			                  SCAP_CB_ON_GCB_MAX);
			tmp_result = false;
		}

		if (tx_check && ((ts_data->scap_gcb_tx < SCAP_CB_ON_GCB_MIN) ||
		                 (ts_data->scap_gcb_tx > SCAP_CB_ON_GCB_MAX))) {
			FTS_TEST_SAVE_ERR("test fail,gcb_tx:%5d,range=(%5d,%5d)\n",
			                  ts_data->scap_gcb_tx, SCAP_CB_ON_GCB_MIN,
			                  SCAP_CB_ON_GCB_MAX);
			tmp_result = false;
		}
		ts_data->scb_cnt += channel_num;
	} else {
		tmp2_result = true;
	}


restore_reg:
	ret = ft3681_test_write_reg(FACTORY_REG_MC_SC_MODE, sc_mode);

	if (ret) {
		FTS_TEST_SAVE_ERR("write sc mode fail,ret=%d\n", ret);
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

test_err:
	if (tmp_result && tmp2_result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------Scap CB (normal && waterproof) Test PASS\n");
	} else {
		*test_result = false;
		if (tmp_result) {
			FTS_TEST_SAVE_ERR("------Scap CB Test (waterproof) NG\n");
		}
		if (tmp2_result) {
			FTS_TEST_SAVE_ERR("------Scap CB Test (normal) NG\n");
		}
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}


static int ft3681_scap_rawdata_autotest(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	bool tmp_result = false;
	bool tmp2_result = false;
	u8 wc_sel = 0;
	u8 data_type = 0;
	bool fw_wp_check = false;
	bool tx_check = false;
	bool rx_check = false;
	int *srawdata_tmp = NULL;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int channel_num = tx_num + rx_num;
	int byte_num = channel_num * 2;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Scap Rawdata Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P || !ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N ||
	    !ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N || !ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P) {
		TPD_INFO("ft3681_scap_raw_data_P || ft3681_scap_raw_data_N || ft3681_scap_raw_waterproof_data_N || ft3681_scap_raw_waterproof_data_P is NULL");
		return 0;
	}

	if (!thr || !thr->node_valid_sc || !thr->scap_rawdata_on_min || !thr->scap_rawdata_on_max
	    || !thr->scap_rawdata_off_min || !thr->scap_rawdata_off_max) {
		FTS_TEST_SAVE_ERR("thr/node_valid_sc/scap_rawdata_on/off_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	ret = enter_factory_mode(ts_data);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
		goto test_err;
	}

	/* get waterproof channel select */
	ret = ft3681_test_read_reg(FACTORY_REG_WC_SEL, &wc_sel);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read water_channel_sel fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(0x5B, &data_type);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x5B fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_write_reg(0x5B, 0x01);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
		goto restore_reg;
	}

	/* scan rawdata 2 times */
	ret = start_scan();

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("scan scap rawdata fail\n");
		goto restore_reg;
	}

	ret = start_scan();

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("scan scap rawdata fail\n");
		goto restore_reg;
	}

	/* water proof on check */
	ts_data->srawdata_cnt = 0;
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_ON);

	if (fw_wp_check) {
		srawdata_tmp = ts_data->scap_rawdata + ts_data->srawdata_cnt;
		ret = get_rawdata_mc_sc(WATER_PROOF_ON, byte_num, srawdata_tmp);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("get scap(WP_ON) rawdata fail\n");
			goto test_err;
		}

		//        FTS_TEST_SAVE_INFO("scap_rawdata in waterproof on mode:\n");

		/* compare */
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_RX);

		tmp_result = compare_mc_sc(ts_data, tx_check, rx_check, srawdata_tmp,
					   ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N,
					   ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P);
		ts_data->srawdata_cnt += channel_num;

	} else {
		tmp_result = true;
	}

	/* water proof off check */
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_OFF);

	if (fw_wp_check) {
		srawdata_tmp = ts_data->scap_rawdata + ts_data->srawdata_cnt;
		ret = get_rawdata_mc_sc(WATER_PROOF_OFF, byte_num, srawdata_tmp);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("get scap(WP_OFF) rawdata fail\n");
			goto test_err;
		}

		/* compare */
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_RX);
		tmp2_result = compare_mc_sc(ts_data, tx_check, rx_check, srawdata_tmp,
					    ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N,
					    ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P);
		ts_data->srawdata_cnt += channel_num;

	} else {
		tmp2_result = true;
	}

restore_reg:
	ret = ft3681_test_write_reg(0x5B, data_type);

	if (ret) {
		FTS_TEST_SAVE_ERR("restore data_type fail,ret=%d\n", ret);
	}

test_err:
	if (tmp_result && tmp2_result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------SCAP Rawdata Test PASS\n");
	} else {
		*test_result = false;
		FTS_TEST_SAVE_INFO("------SCAP Rawdata Test NG\n");
	}

	FTS_TEST_FUNC_EXIT();
	return ret;
}


static int ft3681_short_test(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	int adc[256] = { 0 };
	u8 ab_ch[256] = { 0 };
	bool ca_result = false;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Short Test\n");
	ret = enter_factory_mode(ts_data);

	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
		goto test_err;
	}

	/* get short resistance and exceptional channel */
	ret = short_test_ch_to_all(ts_data, adc, ab_ch, &ca_result);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("short test of channel to all fails\n");
		goto test_err;
	}

test_err:
	if (ca_result) {
		FTS_TEST_SAVE_INFO("------Short test PASS\n");
		* test_result = true;
	} else {
		FTS_TEST_SAVE_ERR("------Short Test NG\n");
		* test_result = false;
	}
	return ret;
}

static int ft3681_panel_differ_test(struct chip_data_ft3681 *ts_data, bool *test_result)
{
	int ret = 0;
	int i = 0;
	u8 fre = 0;
	u8 g_cb = 0;
	u8 normalize = 0;
	u8 data_type = 0;
	u8 rawdata_addr = 0;
	bool result = false;
	int byte_num = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: Panel Differ Test\n");

	if (!ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P
	    || !ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N) {
		TPD_INFO("ft3681_panel_differ_data_P || ft3681_panel_differ_data_N is NULL");
		return 0;
	}

	if (!ts_data->panel_differ || !thr->node_valid ) {
		FTS_TEST_SAVE_ERR("rawdata is null\n");
		ret = -EINVAL;
		goto test_err;
	}

	ret = enter_factory_mode(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	/* save origin value */
	ret = ft3681_test_read_reg(FACTORY_REG_FRE_LIST, &fre);
	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x0A fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_DATA_TYPE, &data_type);
	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x5B fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_GCB, &g_cb);
	if (ret) {
		FTS_TEST_SAVE_ERR("read regBD fail,ret=%d\n", ret);
		goto test_err;
	}

	ret = ft3681_test_read_reg(FACTORY_REG_NORMALIZE, &normalize);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read normalize fail,ret=%d\n", ret);
		goto test_err;
	}

	/* set frequecy high */
	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, 0x81);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
		goto restore_reg;
	}

	/* wait fw state update */
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_DATA_TYPE, 0x01);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = ft3681_test_write_reg(FACTORY_REG_GCB, 0x00);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set fir fail,ret=%d\n", ret);
		goto restore_reg;
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}

	/* set to overall normalize */
	ret = ft3681_test_write_reg(FACTORY_REG_NORMALIZE, 0x00);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write normalize fail,ret=%d\n", ret);
		goto restore_reg;
	}

	/* get rawdata */
	for (i = 0; i < 3; i++) {
		/* lost 3 frames, in order to obtain stable data */
		/* start scanning */
		ret = start_scan();

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan fail\n");
			continue;
		}

		/* read rawdata */
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
		byte_num = node_num * 2;
		ret = read_rawdata(FACTORY_REG_LINE_ADDR, 0xAA, rawdata_addr, byte_num,
		                   ts_data->panel_differ);

		if (ret < 0) {
			FTS_TEST_SAVE_ERR("read rawdata fail\n");
		}
	}

	for (i = 0; i < node_num; i++) {
		ts_data->panel_differ[i] = ts_data->panel_differ[i] / 10;
	}


	/* compare */
	result = true;

	for (i = 0; i < node_num; i++) {
		if (0 == thr->node_valid[i]) {
			continue;
		}

		if ((ts_data->panel_differ[i] < ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N[i])
		    || (ts_data->panel_differ[i] > ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P[i])) {
			TPD_INFO("raw data ERR [%d]: [%d] > [%d] > [%d] \n", i,
			         ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P[i], ts_data->panel_differ[i],
			         ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N[i]);
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
			                  i / rx_num + 1, i % rx_num + 1, ts_data->panel_differ[i],
			                  ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N[i],
			                  ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P[i]);
			result = false;
		}
	}

restore_reg:
	/* set the origin value */
	ret = ft3681_test_write_reg(FACTORY_REG_FRE_LIST, fre);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);
	}

	/* wait fw state update */
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

	ret = ft3681_test_write_reg(FACTORY_REG_DATA_TYPE, data_type);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
	}

	ret = ft3681_test_write_reg(FACTORY_REG_GCB, g_cb);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore 0xBD fail,ret=%d\n", ret);
	}

	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	}

	ret = ft3681_test_write_reg(FACTORY_REG_NORMALIZE, normalize);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("restore normalize fail,ret=%d\n", ret);
	}

test_err:

	if (result) {
		FTS_TEST_SAVE_INFO("------ Panel differ test PASS\n");
		ret = TEST_RESULT_NORMAL;
	} else {
		FTS_TEST_SAVE_INFO("------ Panel differ test NG\n");
		ret = TEST_RESULT_ABNORMAL;
	}

	FTS_TEST_FUNC_EXIT();
	return ret;

}

static void ft3681_auto_write_result(struct chip_data_ft3681 *ts_data, int failed_count)
{
	//uint8_t  data_buf[64];
	uint8_t file_data_buf[128];
	uint8_t  data_buf[256];
	uint32_t buflen = 0;
	//int i;
	int line_num = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM;
	int channel_num = ts_data->hw_res->TX_NUM + ts_data->hw_res->RX_NUM;
	mm_segment_t old_fs;
	struct timespec now_time;
	struct rtc_time rtc_now_time;

	TPD_INFO("%s +\n", __func__);

	//step2: create a file to store test data in /sdcard/Tp_Test
	getnstimeofday(&now_time);
	rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
	//if test fail,save result to path:/sdcard/TpTestReport/screenOn/NG/
	if (failed_count) {
		snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/NG/tp_testlimit_%02d%02d%02d-%02d%02d%02d-fail-utc.csv",
			 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
			 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
	} else {
		snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/OK/tp_testlimit_%02d%02d%02d-%02d%02d%02d-pass-utc.csv",
			 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
			 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
	ksys_mkdir("/sdcard/TpTestReport", 0666);
	ksys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
	ksys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
	ksys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
	ts_data->csv_fd = ksys_open(file_data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
	sys_mkdir("/sdcard/TpTestReport", 0666);
	sys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
	sys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
	sys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
	ts_data->csv_fd = sys_open(file_data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
	if (ts_data->csv_fd < 0) {
		TPD_INFO("Open log file '%s' failed, %d.\n", file_data_buf, ts_data->csv_fd);
		set_fs(old_fs);
		return;
	}

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER

	/*header*/
	buflen = snprintf(data_buf, 256, "ECC, 85, 170, IC Name, %s, IC Code, %x\n",
			  "FT3681", 0);
	ksys_write(ts_data->csv_fd, data_buf, buflen);


	buflen = snprintf(data_buf, 256, "TestItem Num, %d, ", 12);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num = 11;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Noise Test", 14,
			  tx_num, rx_num, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Null Noise", 41,
			  1, 1, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 1;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Rawdata Test", 7,
			  tx_num, rx_num, line_num, 2);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "Rawdata Uniformity Test", 16, tx_num, rx_num, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "Rawdata Uniformity Test", 16, tx_num, rx_num, line_num, 2);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, rx_num, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, 1, line_num, 2);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, rx_num, line_num, 3);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, 1, line_num, 4);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "SCAP Rawdata Test", 10, 2, rx_num, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "SCAP Rawdata Test", 10, 2, rx_num, line_num, 2);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Panel Differ Test", 20,
			  tx_num, rx_num, line_num, 1);
	ksys_write(ts_data->csv_fd, data_buf, buflen);

	buflen = snprintf(data_buf, 256, "\n\n\n\n\n\n\n\n\n");
	ksys_write(ts_data->csv_fd, data_buf, buflen);

#else

	/*header*/
	buflen = snprintf(data_buf, 256, "ECC, 85, 170, IC Name, %s, IC Code, %x\n",
			  "FT3681", 0);
	sys_write(ts_data->csv_fd, data_buf, buflen);


	buflen = snprintf(data_buf, 256, "TestItem Num, %d, ", 12);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num = 11;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Noise", 14,
			  tx_num, rx_num, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Null Noise", 41,
	                  1, 1, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 1;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Rawdata Test", 7,
			  tx_num, rx_num, line_num, 2);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "Rawdata Uniformity Test", 16, tx_num, rx_num, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "Rawdata Uniformity Test", 16, tx_num, rx_num, line_num, 2);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += tx_num;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, rx_num, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, 1, line_num, 2);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, rx_num, line_num, 3);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "SCAP CB Test", 9,
			  2, 1, line_num, 4);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "SCAP Rawdata Test", 10, 2, rx_num, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ",
			  "SCAP Rawdata Test", 10, 2, rx_num, line_num, 2);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	line_num += 2;
	buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Panel Differ Test", 20,
			  tx_num, rx_num, line_num, 1);
	sys_write(ts_data->csv_fd, data_buf, buflen);

	buflen = snprintf(data_buf, 256, "\n\n\n\n\n\n\n\n\n");
	sys_write(ts_data->csv_fd, data_buf, buflen);
#endif

	ft3681_test_save_data("Noise Test", ts_data->noise_rawdata, node_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("Null Noise Test", &ts_data->null_noise_value, 1, 1, ts_data->csv_fd);
	ft3681_test_save_data("Rawdata Test", ts_data->rawdata, node_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("Rawdata Uniformity Test", ts_data->rawdata_linearity, ts_data->rl_cnt, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("SCAP CB Test", ts_data->scap_cb, channel_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("SCAP CB Test", &ts_data->scap_gcb_rx, 1, 1, ts_data->csv_fd);
	ft3681_test_save_data("SCAP CB Test", ts_data->scap_cb + channel_num, channel_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("SCAP CB Test", &ts_data->scap_gcb_tx, 1, 1, ts_data->csv_fd);
	ft3681_test_save_data("SCAP Rawdata Test", ts_data->scap_rawdata, channel_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("SCAP Rawdata Test", ts_data->scap_rawdata + channel_num, channel_num, rx_num, ts_data->csv_fd);
	ft3681_test_save_data("Panel Differ Test", ts_data->panel_differ, node_num, rx_num, ts_data->csv_fd);

	if (ts_data->csv_fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
		ksys_close(ts_data->csv_fd);
#else
		sys_close(ts_data->csv_fd);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
		set_fs(old_fs);
	}
	TPD_INFO("%s -\n", __func__);
	return;
}


static int ft3681_auto_endoperation(struct chip_data_ft3681 *ts_data)
{
	TPD_INFO("%s +\n", __func__);
	if (ts_data->rawdata_linearity) {
		kfree(ts_data->rawdata_linearity);
		ts_data->rawdata_linearity = NULL;
	}
	if (ts_data->panel_differ) {
		kfree(ts_data->panel_differ);
		ts_data->panel_differ = NULL;
	}
	if (ts_data->scap_rawdata) {
		kfree(ts_data->scap_rawdata);
		ts_data->scap_rawdata = NULL;
	}

	if (ts_data->scap_cb) {
		kfree(ts_data->scap_cb);
		ts_data->scap_cb = NULL;
	}

	if (ts_data->rawdata) {
		kfree(ts_data->rawdata);
		ts_data->rawdata = NULL;
	}

	if (ts_data->noise_rawdata) {
		kfree(ts_data->noise_rawdata);
		ts_data->noise_rawdata = NULL;
	}
	TPD_INFO("%s -\n", __func__);

	return 0;
}

static int ft3681_rst_test(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	u8 val = 0;
	u8 val2 = 0;
	u8 val3 = 0;

	enter_work_mode();
	ft3681_test_read_reg(FT3681_REG_REPORT_RATE, &val);
	val2 = val - 1;
	ft3681_test_write_reg(FT3681_REG_REPORT_RATE, val2);
	ft3681_rstpin_reset((void *)ts_data);
	ft3681_test_read_reg(FT3681_REG_REPORT_RATE, &val3);
	TPD_INFO("one: reset test: val = %d, val3 = %d", val, val3);

	ft3681_test_read_reg(FT3681_REG_REPORT_RATE, &val);
	val2 = val - 1;
	ft3681_test_write_reg(FT3681_REG_REPORT_RATE, val2);
	ft3681_rstpin_reset((void *)ts_data);
	ft3681_test_read_reg(FT3681_REG_REPORT_RATE, &val3);
	TPD_INFO("two: reset test: val = %d, val3 = %d", val, val3);

	if (val3 != val) {
		FTS_TEST_SAVE_ERR("check reg to test rst failed.\n");
		ret = -1;
	}

	return ret;
}

static int ft3681_start_test(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	bool temp_result = false;
	int test_result = 0;
	int failed_count = 0;

	FTS_TEST_FUNC_ENTER();
	TPD_INFO("%s +\n", __func__);
	ft3681_auto_preoperation(ts_data);

	/*noise test*/
	ret = ft3681_noise_autotest(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 1);
		failed_count += 1;
	}

	/* rawdata test */
	ret = ft3681_rawdata_autotest(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 2);
		failed_count += 1;
	}

	/* uniformity test */
	ret = ft3681_uniformity_autotest(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 3);
		failed_count += 1;
	}


	/* scap_cb test */
	ret = ft3681_scap_cb_autotest(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 4);
		failed_count += 1;
	}


	/* scap_rawdata test */
	ret = ft3681_scap_rawdata_autotest(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 5);
		failed_count += 1;
	}



	/* short test */
	ret = ft3681_short_test(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 6);
		failed_count += 1;
	}

	/* panel differ test */
	ret = ft3681_panel_differ_test(ts_data, &temp_result);
	if ((ret < 0) || (false == temp_result)) {
		test_result = false;
		test_result |= (1 << 7);
		failed_count += 1;
	}

	/* rst pin test */
	ret = ft3681_rst_test(ts_data);
	if (ret < 0) {
		test_result = false;
		test_result |= (1 << 8);
		failed_count += 1;
	}

	ft3681_auto_write_result(ts_data, failed_count);
	ft3681_auto_endoperation(ts_data);

	TPD_INFO("%s: test_result = [0x%x] \n ", __func__, test_result);
	FTS_TEST_FUNC_EXIT();
	TPD_INFO("%s -\n", __func__);

	return failed_count;
}


static void ft3681_threshold_free(struct chip_data_ft3681 *ts_data)
{
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	TPD_INFO("%s +\n", __func__);
	kfree(thr->node_valid);
	kfree(thr->node_valid_sc);
	kfree(thr->rawdata_h_max);
	kfree(thr->rawdata_h_min);
	kfree(thr->tx_linearity_max);
	kfree(thr->tx_linearity_min);
	kfree(thr->rx_linearity_max);
	kfree(thr->rx_linearity_min);
	kfree(thr->scap_cb_off_max);
	kfree(thr->scap_cb_off_min);
	kfree(thr->scap_cb_on_max);
	kfree(thr->scap_cb_on_min);
	kfree(thr->scap_rawdata_off_max);
	kfree(thr->scap_rawdata_off_min);
	kfree(thr->scap_rawdata_on_max);
	kfree(thr->scap_rawdata_on_min);
	kfree(thr->panel_differ_max);
	kfree(thr->panel_differ_min);
	TPD_INFO("%s -\n", __func__);
	return;
}

static void ft3681_autotest_endoperation(struct chip_data_ft3681 *ts_data, const struct firmware *limit_fw)
{
	TPD_INFO("%s +\n", __func__);
	if (ts_data->ft3681_autotest_offset) {
		kfree(ts_data->ft3681_autotest_offset);
		ts_data->ft3681_autotest_offset = NULL;
	}

	if (limit_fw) {
		release_firmware(limit_fw);
		limit_fw = NULL;
	}
	TPD_INFO("%s -\n", __func__);
}

static int ft3681_threshold_malloc(struct chip_data_ft3681 *ts_data)
{
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	int channel_num = tx_num + rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	thr->node_valid = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->node_valid) {
		FTS_TEST_SAVE_ERR("kzalloc for node_valid fail\n");
		goto thr_free;
	}

	thr->node_valid_sc = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->node_valid_sc) {
		FTS_TEST_SAVE_ERR("kzalloc for node_valid_sc fail\n");
		goto thr_free;
	}

	thr->rawdata_h_max = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->rawdata_h_max) {
		FTS_TEST_SAVE_ERR("kzalloc for rawdata_h_max fail\n");
		goto thr_free;
	}

	thr->rawdata_h_min = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->rawdata_h_min) {
		FTS_TEST_SAVE_ERR("kzalloc for rawdata_h_min fail\n");
		goto thr_free;
	}

	thr->tx_linearity_max = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->tx_linearity_max) {
		FTS_TEST_SAVE_ERR("kzalloc for tx_linearity_max fail\n");
		goto thr_free;
	}

	thr->tx_linearity_min = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->tx_linearity_min) {
		FTS_TEST_SAVE_ERR("kzalloc for tx_linearity_min fail\n");
		goto thr_free;
	}

	thr->rx_linearity_max = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->rx_linearity_max) {
		FTS_TEST_SAVE_ERR("kzalloc for rx_linearity_max fail\n");
		goto thr_free;
	}

	thr->rx_linearity_min = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->rx_linearity_min) {
		FTS_TEST_SAVE_ERR("kzalloc for rx_linearity_min fail\n");
		goto thr_free;
	}

	thr->scap_cb_off_max = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_cb_off_max) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_cb_off_max fail\n");
		goto thr_free;
	}

	thr->scap_cb_off_min = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_cb_off_min) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_cb_off_min fail\n");
		goto thr_free;
	}

	thr->scap_cb_on_max = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_cb_on_max) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_cb_on_max fail\n");
		goto thr_free;
	}

	thr->scap_cb_on_min = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_cb_on_min) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_cb_on_min fail\n");
		goto thr_free;
	}

	thr->scap_rawdata_off_max = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_rawdata_off_max) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_rawdata_off_max fail\n");
		goto thr_free;
	}

	thr->scap_rawdata_off_min = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_rawdata_off_min) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_rawdata_off_min fail\n");
		goto thr_free;
	}

	thr->scap_rawdata_on_max = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_rawdata_on_max) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_rawdata_on_max fail\n");
		goto thr_free;
	}


	thr->scap_rawdata_on_min = (int *)kzalloc(channel_num * sizeof(int), GFP_KERNEL);
	if (!thr->scap_rawdata_on_min) {
		FTS_TEST_SAVE_ERR("kzalloc for scap_rawdata_on_min fail\n");
		goto thr_free;
	}

	thr->panel_differ_max = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->panel_differ_max) {
		FTS_TEST_SAVE_ERR("kzalloc for panel_differ_max fail\n");
		goto thr_free;
	}

	thr->panel_differ_min = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
	if (!thr->panel_differ_min) {
		FTS_TEST_SAVE_ERR("kzalloc for panel_differ_min fail\n");
		goto thr_free;
	}

	return 0;

thr_free:
	ft3681_threshold_free(ts_data);
	return -ENOMEM;
}

static int ft3681_get_threshold(struct chip_data_ft3681 *ts_data, char *data)
{
	int i = 0;
	int ret = 0;
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	int channel_num = tx_num + rx_num;
	struct mc_sc_threshold *thr = &ts_data->mpt.thr;

	ret = ft3681_threshold_malloc(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("malloc for threshold fail\n");
		return ret;
	}

	if (!data) {
		thr->noise_coefficient = 200;
		thr->short_cc = 500;
		thr->short_cg = 500;

		for (i = 0; i < node_num; i++) {
			thr->node_valid[i] = 1;
			thr->rawdata_h_max[i] = 12000;
			thr->rawdata_h_min[i] = 2690;
			thr->tx_linearity_max[i] = 40;
			thr->tx_linearity_min[i] = 0;
			thr->rx_linearity_max[i] = 40;
			thr->rx_linearity_min[i] = 0;
			thr->panel_differ_max[i] = 1400;
			thr->panel_differ_min[i] = 200;
		}
		TPD_INFO("raw_max = [%d] raw_min = [%d] \n", 12000, 2690);
		TPD_INFO("tx_linearity_max = [%d] tx_linearity_min = [%d] \n", 40, 0);
		TPD_INFO("rx_linearity_max = [%d] rx_linearity_min = [%d] \n", 40, 0);
		TPD_INFO("panel_differ_max = [%d] panel_differ_min = [%d] \n", 1400, 200);

#if 0
		thr->rawdata_h_max[0] = 7090;
		thr->rawdata_h_min[0] = 2690;
		thr->rawdata_h_max[rx_num - 1] = 8430;
		thr->rawdata_h_min[rx_num - 1] = 3198;
		thr->rawdata_h_max[(tx_num - 1) * rx_num] = 9278;
		thr->rawdata_h_min[(tx_num - 1) * rx_num] = 3520;
		thr->rawdata_h_max[tx_num * rx_num - 1] = 9572;
		thr->rawdata_h_min[tx_num * rx_num - 1] = 3632;
#endif

		for (i = 0; i < channel_num; i++) {
			thr->node_valid_sc[i] = 1;
			thr->scap_cb_off_max[i] = 490;
			thr->scap_cb_off_min[i] = 0;
			thr->scap_cb_on_max[i] = 490;
			thr->scap_cb_on_min[i] = 0;
			thr->scap_rawdata_off_max[i] = 15000;
			thr->scap_rawdata_off_min[i] = 3000;
			thr->scap_rawdata_on_max[i] = 15000;
			thr->scap_rawdata_on_min[i] = 3000;
		}
		TPD_INFO("node_valid_sc = [%d] \n", 1);
		TPD_INFO("scap_cb_off_max = [%d] scap_cb_off_min = [%d] \n", 490, 0);
		TPD_INFO("scap_cb_on_max = [%d] scap_cb_on_min = [%d] \n", 490, 0);
		TPD_INFO("scap_rawdata_off_max = [%d] scap_rawdata_off_min = [%d] \n", 15000, 3000);
		TPD_INFO("scap_rawdata_on_max = [%d] scap_rawdata_on_min = [%d] \n", 15000, 3000);
	}

	return 0;
}

static int ft3681_get_threshold_from_img(struct chip_data_ft3681 *ts_data, char *data, const struct firmware *limit_fw)
{

	int ret = 0;
	int i = 0;
	int item_cnt = 0;
	//uint8_t * p_print = NULL;
	uint32_t *p_item_offset = NULL;
	struct auto_test_header *ph = NULL;
	struct auto_test_item_header *item_head = NULL;
	u8 fwver = 0xFF;
	struct touchpanel_data *ts = ts_data->ts;

	ret = ft3681_read_reg(FT3681_REG_FW_VER, &fwver);
	if (fwver >= 0x20) {
		ts_data->use_panelfactory_limit = false;
	} else {
		ts_data->use_panelfactory_limit = true;
	}
	TPD_INFO("%s, use_panelfactory_limit = %d \n", __func__, ts_data->use_panelfactory_limit);

	ts_data->ft3681_autotest_offset = kzalloc(sizeof(struct ft3681_autotest_offset), GFP_KERNEL);

	ret = request_real_test_limit(ts,&limit_fw, ts->panel_data.test_limit_name, &ts_data->ft_spi->dev);
	TPD_INFO("limit_img path is [%s] \n", ts->panel_data.test_limit_name);
	if (ret < 0) {
		TPD_INFO("Request limit_img failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
		goto RELEASE_DATA;
	}

	ph = (struct auto_test_header *)(limit_fw->data);
#if 0
	TPD_INFO("start to dump img \n");
	p_print = (uint8_t *)ph;
	for (i = 0; i < 16 * 8; i++) {
		if (i % 16 == 0) {
			TPD_INFO("current line [%d]: \n", i / 16);
		}
		TPD_INFO("0x%x \n", *(p_print + i * sizeof(uint8_t)));
	}
	TPD_INFO("end of dump img \n");
#endif
	p_item_offset = (uint32_t *)(limit_fw->data + 16);
	for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
		if ((ph->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}
	TPD_INFO("%s: total test item = %d \n", __func__, item_cnt);

	TPD_INFO("%s: populating ft3681_test_offset \n", __func__);

	for (i = 0; i < item_cnt; i++) {
		TPD_INFO("%s: i[%d] \n", __func__, i);
		item_head = (struct auto_test_item_header *)(limit_fw->data + p_item_offset[i]);

		if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
			TPD_INFO("[%d] incorrect item type: LIMIT_TYPE_NO_DATA\n", item_head->item_bit);

		} else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
			if (false == ts_data->use_panelfactory_limit) {
				TPD_INFO("test item bit [%d] \n", item_head->item_bit);

				if (item_head->item_bit == TYPE_NOISE_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_noise_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_noise_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("item_head->top_limit_offset = %d, ft3681_noise_data_N = %d \n",
						 item_head->top_limit_offset,
						 ts_data->ft3681_autotest_offset->ft3681_noise_data_N);

				} else if (item_head->item_bit == TYPE_RAW_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_raw_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_raw_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_raw_data_P = %p, ft3681_raw_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_raw_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_raw_data_N);

				} else if (item_head->item_bit == TYPE_UNIFORMITY_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_uniformity_data_P = %p, ft3681_uniformity_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N);

				} else if (item_head->item_bit == TYPE_PANEL_DIFFER_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_panel_differ_data_P = %p, ft3681_panel_differ_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N);
				}

			} else if (true == ts_data->use_panelfactory_limit) {
				TPD_INFO("test item bit [%d] \n", item_head->item_bit);

				if (item_head->item_bit == TYPE_FACTORY_NOISE_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_noise_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_noise_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_noise_data_P = %p, ft3681_noise_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_noise_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_noise_data_P);

				} else if (item_head->item_bit == TYPE_FACTORY_RAW_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_raw_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_raw_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_raw_data_P = %p, ft3681_raw_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_raw_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_raw_data_N);

				} else if (item_head->item_bit == TYPE_FACTORY_UNIFORMITY_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_uniformity_data_P = %p, ft3681_uniformity_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N);

				} else if (item_head->item_bit == TYPE_FACTORY_PANEL_DIFFER_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_panel_differ_data_P = %p, ft3681_panel_differ_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N);
				}
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_RX_TX_DATA) {
			if (false == ts_data->use_panelfactory_limit) {
				TPD_INFO("test item bit [%d] \n", item_head->item_bit);

				if (item_head->item_bit == TYPE_SCAP_CB_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_scap_cb_data_P = %p, ft3681_scap_cb_data_P = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N);

				} else if (item_head->item_bit == TYPE_SCAP_RAW_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_scap_raw_data_P = %p, ft3681_scap_raw_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N);

				} else if (item_head->item_bit == TYPE_SCAP_CB_WATERPROOF_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_scap_cb_data_waterproof_P = %p, ft3681_scap_cb_data_waterproof_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P,
						 ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N);

				} else if (item_head->item_bit == TYPE_SCAP_RAW_WATERPROOF_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
					TPD_INFO("ft3681_scap_raw_waterproof_data_P = %p, ft3681_scap_raw_waterproof_data_N = %p \n",
						 ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P,
						 ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N);
				}

			} else if (true == ts_data->use_panelfactory_limit) {
				TPD_INFO("test item bit [%d] \n", item_head->item_bit);

				if (item_head->item_bit == TYPE_FACTORY_SCAP_CB_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);

				} else if (item_head->item_bit == TYPE_FACTORY_SCAP_RAW_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);

				} else if (item_head->item_bit == TYPE_FACTORY_SCAP_CB_WATERPROOF_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);

				} else if (item_head->item_bit == TYPE_FACTORY_SCAP_RAW_WATERPROOF_DATA) {
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P = (int32_t *)(
								limit_fw->data + item_head->top_limit_offset);
					ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N = (int32_t *)(
								limit_fw->data + item_head->floor_limit_offset);
				}
			}

		} else {
			TPD_INFO("[%d] unknown item type \n", item_head->item_bit);
		}
	}

	ret = 0;
RELEASE_DATA:
	if (limit_fw) {
		release_firmware(limit_fw);
	}
	return ret;
}


static void ft3681_print_threshold(struct chip_data_ft3681 *ts_data)
{
	int tx_num = ts_data->hw_res->TX_NUM;
	int rx_num = ts_data->hw_res->RX_NUM;
	int node_num = tx_num * rx_num;
	int channel_num = tx_num + rx_num;

	TPD_INFO("noise threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_noise_data_P, node_num, rx_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_noise_data_N, node_num, rx_num);

	TPD_INFO("rawdata threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_raw_data_P, node_num, rx_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_raw_data_N, node_num, rx_num);

	TPD_INFO("uniformity threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_uniformity_data_P, node_num, rx_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_uniformity_data_N, node_num, rx_num);

	TPD_INFO("scap cb normal threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_P, channel_num, channel_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_N, channel_num, channel_num);

	TPD_INFO("scap cb waterproof threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_P, channel_num, channel_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_cb_data_waterproof_N, channel_num, channel_num);

	TPD_INFO("scap rawdata threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_P, channel_num, channel_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_raw_data_N, channel_num, channel_num);

	TPD_INFO("panel differ threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_P, node_num, rx_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_panel_differ_data_N, node_num, rx_num);

	TPD_INFO("scap rawdata waterproof threshold max/min:");
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_P, channel_num, channel_num);
	ft3681_print_buffer(ts_data->ft3681_autotest_offset->ft3681_scap_raw_waterproof_data_N, channel_num, channel_num);
}

int ft3681_test_entry(struct chip_data_ft3681 *ts_data)
{
	int ret = 0;
	const struct firmware *limit_fw = NULL;

	TPD_INFO("%s +\n", __func__);
	FTS_TEST_SAVE_ERR("FW_VER:0x%02x, TX_NUM:%d, RX_NUM:%d\n", ts_data->fwver, ts_data->hw_res->TX_NUM, ts_data->hw_res->RX_NUM);
	ret = ft3681_get_threshold(ts_data, NULL);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get threshold fail,ret=%d\n", ret);
		return 0xFF;
	}

	ret = ft3681_get_threshold_from_img(ts_data, NULL, limit_fw);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get threshold from img fail,ret=%d\n", ret);
		return 0xFF;
	}

	ft3681_print_threshold(ts_data);

	ret = enter_factory_mode(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		ret = 0xFF;
		goto test_err;
	}

	ret = ft3681_test_read_reg(0x14, &ts_data->fre_num);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read fre_num fails");
		goto test_err;
	}
	TPD_INFO("fre_num:%d", ts_data->fre_num);

	ret = get_channel_num(ts_data);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("check channel num fail,ret=%d\n", ret);
		ret = 0xFF;
		goto test_err;
	}

	ret = ft3681_start_test(ts_data);
	//seq_printf(s, "%d error(s). %s\n", gts_test->error_count, gts_test->error_count ? "" : "All test passed.");
	//FTS_TEST_SAVE_INFO("\n\n %d Error(s). Factory Test Result \n", ret);
	//FTS_TEST_SAVE_INFO("\n\n%d error(s). %s\n", ret, ret ? "" : "All test passed.\n");
	seq_printf(ts_data->s, "%d error(s). %s\n", ret, ret ? "" : "All test passed.");

test_err:
	enter_work_mode();
	ft3681_threshold_free(ts_data);
	ft3681_autotest_endoperation(ts_data, limit_fw);
	TPD_INFO("%s -\n", __func__);
	return ret;
}
