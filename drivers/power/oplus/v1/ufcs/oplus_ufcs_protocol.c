// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2022 Oplus. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>
#include <trace/events/sched.h>
#include <linux/ktime.h>
#include <uapi/linux/sched/types.h>
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "../oplus_ufcs.h"
#include "../oplus_chg_module.h"
#include "../voocphy/oplus_voocphy.h"
#include "oplus_ufcs_protocol.h"

static struct oplus_ufcs_protocol *g_protocol = NULL;
typedef int (*callback)(void *buf);

int __attribute__((weak)) oplus_sm8350_pps_mos_ctrl(int on)
{
	return 1;
}
int __attribute__((weak)) oplus_sm8350_get_btb_temp_status(void)
{
	return 1;
}
int __attribute__((weak)) oplus_chg_get_battery_btb_temp_cal(void)
{
	return 25;
}
int __attribute__((weak)) oplus_chg_get_usb_btb_temp_cal(void)
{
	return 25;
}
void __attribute__((weak)) oplus_chg_adc_switch_ctrl(void)
{
	return;
}
int __attribute__((weak)) oplus_chg_get_mos_btb_temp_cal(void)
{
	return 25;
}
int __attribute__((weak)) oplus_chg_get_battery_btb_tdiff(void)
{
	return 0;
}
bool __attribute__((weak)) oplus_ufcs_set_mos0_switch(bool enable)
{
	return true;
}
bool __attribute__((weak)) oplus_ufcs_get_mos0_switch(void)
{
	return true;
}
bool __attribute__((weak)) oplus_ufcs_set_mos1_switch(bool enable)
{
	return true;
}
bool __attribute__((weak)) oplus_ufcs_get_mos1_switch(void)
{
	return true;
}

extern int oplus_chg_get_battery_btb_temp_cal(void);
extern int oplus_chg_get_usb_btb_temp_cal(void);
extern void oplus_chg_adc_switch_ctrl(void);
extern int oplus_chg_get_mos_btb_temp_cal(void);
extern bool oplus_ufcs_set_mos0_switch(bool enable);
extern bool oplus_ufcs_get_mos0_switch(void);
extern bool oplus_ufcs_set_mos1_switch(bool enable);
extern bool oplus_ufcs_get_mos1_switch(void);

static int oplus_ufcs_protocol_send_soft_reset(void);
static int oplus_ufcs_protocol_source_hard_reset(void);
static int oplus_ufcs_protocol_cable_hard_reset(void);

const char *adapter_error_info[16] = {
	"adapter output OVP!",
	"adapter outout UVP!",
	"adapter output OCP!",
	"adapter output SCP!",
	"adapter USB OTP!",
	"adapter inside OTP!",
	"adapter CCOVP!",
	"adapter D-OVP!",
	"adapter D+OVP!",
	"adapter input OVP!",
	"adapter input UVP!",
	"adapter drain over current!",
	"adapter input current loss!",
	"adapter CRC error!",
	"adapter watchdog timeout!",
	"invalid msg!",
};

static int oplus_ufcs_protocol_mos_set(int on)
{
	int ret = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY) {
		ret = oplus_voocphy_set_ufcs_enable(on);
	} else if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_BIDIRECT_VOOCPHY) {
		ret = oplus_voocphy_set_ufcs_enable(on);
		oplus_voocphy_set_chg_auto_mode(on);
	} else if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_ADSP_VOOCPHY) {
		ret = oplus_sm8350_pps_mos_ctrl(on);
	} else {
		/*do nothing*/
		ret = 0;
	}
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_wdt_enable)
		return -ENODEV;
	else
		ret = chip->ops->ufcs_ic_wdt_enable(true);
	return ret;
}

static int oplus_ufcs_protocol_mos_get(void)
{
	int ret = 0;
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY ||
	    oplus_ufcs_get_support_type() == UFCS_SUPPORT_BIDIRECT_VOOCPHY) {
		ret = oplus_voocphy_get_cp_enable();
	}
	return ret;
}

static bool oplus_ufcs_protocol_get_btb_temp_status(void)
{
	bool btb_status = false;
	int btb_temp = 0, usb_temp = 0;
	static unsigned char temp_over_count = 0;

	oplus_chg_adc_switch_ctrl();

	btb_temp = oplus_chg_get_battery_btb_temp_cal();
	usb_temp = oplus_chg_get_usb_btb_temp_cal();

	if (btb_temp >= UFCS_BTB_TEMP_MAX || usb_temp >= UFCS_BTB_TEMP_MAX) {
		temp_over_count++;
		if (temp_over_count > UFCS_BTB_USB_OVER_CNTS) {
			btb_status = true;
			ufcs_err("btb_and_usb temp over");
		}
	} else {
		temp_over_count = 0;
	}

	return btb_status;
}

static bool oplus_ufcs_protocol_get_mos_temp_status(void)
{
	bool btb_status = false;
	int mos_temp = 0;
	static unsigned char temp_over_count = 0;

	mos_temp = oplus_chg_get_mos_btb_temp_cal();

	if (mos_temp >= UFCS_BTB_TEMP_MAX) {
		temp_over_count++;
		if (temp_over_count > UFCS_BTB_USB_OVER_CNTS) {
			btb_status = true;
			ufcs_err("mos temp over");
		}
	} else {
		temp_over_count = 0;
	}

	return btb_status;
}

static int oplus_ufcs_protocol_get_battery_btb_tdiff(void)
{
	int btb_temp = 0;

	btb_temp = oplus_chg_get_battery_btb_tdiff();

	return btb_temp;
}

static bool oplus_ufcs_protocol_get_mos0_switch(void)
{
	bool rc = true;

	rc = oplus_ufcs_get_mos0_switch();

	return rc;
}

static bool oplus_ufcs_protocol_set_mos0_switch(bool enable)
{
	bool rc = true;

	rc = oplus_ufcs_set_mos0_switch(enable);

	return rc;
}

static bool oplus_ufcs_protocol_get_mos1_switch(void)
{
	bool rc = true;

	rc = oplus_ufcs_get_mos1_switch();

	return rc;
}

static bool oplus_ufcs_protocol_set_mos1_switch(bool enable)
{
	bool rc = true;
	rc = oplus_ufcs_set_mos1_switch(enable);

	return rc;
}

static void oplus_ufcs_protocol_cp_hardware_init(void)
{
	ufcs_err(" end\n");
}

static int oplus_ufcs_protocol_cp_cfg_mode_init(int mode)
{
	int ret = 0;
	if (oplus_ufcs_get_support_type() != UFCS_SUPPORT_AP_VOOCPHY &&
	    oplus_ufcs_get_support_type() != UFCS_SUPPORT_BIDIRECT_VOOCPHY) {
		return ret;
	}
	if (mode == UFCS_SC_MODE)
		ret = oplus_voocphy_cp_svooc_init();
	else
		ret = oplus_voocphy_reset_voocphy();

	return 0;
}

struct oplus_ufcs_protocol *oplus_ufcs_get_protocol_struct(void)
{
	return g_protocol;
}

void oplus_ufcs_protocol_i2c_err_inc(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	if (atomic_inc_return(&chip->i2c_err_count) > UFCS_I2C_ERR_MAX) {
		atomic_set(&chip->i2c_err_count, 0);
		chip->error.i2c_error = 1;
	}
}

void oplus_ufcs_protocol_i2c_err_clr(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	atomic_set(&chip->i2c_err_count, 0);
	chip->error.i2c_error = 0;
}

static inline void oplus_ufcs_protocol_id_inc(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;
	chip->msg_send_id = 0;
}

static inline void oplus_ufcs_protocol_reset_state_machine(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	atomic_set(&chip->i2c_err_count, 0);

	if (mutex_is_locked(&chip->chip_lock))
		mutex_unlock(&chip->chip_lock);

	chip->state = STATE_NULL;
	memset(&chip->dev_msg, 0, sizeof(struct comm_msg));
	memset(chip->dev_buffer, 0, sizeof(chip->dev_buffer));
	memset(&chip->rcv_msg, 0, sizeof(struct comm_msg));
	memset(chip->rcv_buffer, 0, sizeof(chip->rcv_buffer));

	memset(&chip->src_cap, 0, sizeof(struct oplus_ufcs_src_cap));
	memset(&chip->info, 0, sizeof(struct oplus_ufcs_sink_info));
	memset(&chip->flag, 0, sizeof(struct ufcs_error_flag));
	memset(&chip->error, 0, sizeof(struct oplus_ufcs_error));
	memset(chip->reg_dump, 0, UFCS_REG_CNT);

	chip->msg_send_id = 0;
	chip->msg_recv_id = 0;

	chip->get_flag_failed = 0;
	chip->handshake_success = 0;
	chip->ack_received = 0;
	chip->msg_received = 0;
	chip->soft_reset = 0;
	chip->uct_mode = 0;
	chip->oplus_id = false;
	chip->abnormal_id = false;
}

static inline void oplus_ufcs_protocol_clr_rx_buffer(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	memset(&chip->rcv_msg, 0, sizeof(struct comm_msg));
	memset(chip->rcv_buffer, 0, sizeof(chip->rcv_buffer));
}

static inline void oplus_ufcs_protocol_clr_tx_buffer(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	memset(&chip->dev_msg, 0, sizeof(struct comm_msg));
	memset(chip->dev_buffer, 0, sizeof(chip->dev_buffer));
}

static inline void oplus_ufcs_protocol_clr_flag_reg(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	memset(&chip->flag, 0, sizeof(struct ufcs_error_flag));
}

static inline void oplus_ufcs_protocol_set_state(enum UFCS_MACHINE_STATE state)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	chip->state = state;
}

static inline void oplus_ufcs_protocol_call_end(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	chip->state = STATE_IDLE;
	chip->ack_received = 0;
	chip->msg_received = 0;
	chip->soft_reset = 0;
}

static inline void oplus_ufcs_protocol_get_chip_lock(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	mutex_lock(&chip->chip_lock);
}

static inline void oplus_ufcs_protocol_release_chip_lock(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	mutex_unlock(&chip->chip_lock);
}

static inline void oplus_ufcs_protocol_reset_msg_counter(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return;

	chip->msg_send_id = 0;
	chip->msg_recv_id = 0;
	chip->soft_reset = 0;
}

static void oplus_ufcs_protocol_awake_init(struct oplus_ufcs_protocol *chip)
{
	if (!chip)
		return;
	chip->chip_ws = wakeup_source_register(chip->dev, "ufcs_ws");
}

static void oplus_ufcs_protocol_set_awake(struct oplus_ufcs_protocol *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->chip_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->chip_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->chip_ws);
		pm_flag = false;
	}
}

static struct comm_msg *oplus_ufcs_protocol_construct_ctrl_msg(enum UFCS_CTRL_MSG_TYPE ctrl_msg)
{
	u8 addr = UFCS_ADDR_SRC;

	if (ctrl_msg == UFCS_CTRL_GET_CABLE_INFO)
		addr = UFCS_ADDR_CABLE;

	memset(&g_protocol->dev_msg, 0, sizeof(struct comm_msg));
	g_protocol->dev_msg.header = UFCS_HEADER(addr, g_protocol->msg_send_id, HEAD_TYPE_CTRL);
	g_protocol->dev_msg.command = ctrl_msg;
	g_protocol->dev_msg.len = DATA_LENGTH_BLANK;
	msleep(10);
	return &g_protocol->dev_msg;
}

static struct comm_msg *oplus_ufcs_protocol_construct_data_msg(enum UFCS_DATA_MSG_TYPE data_msg, void *buffer)
{
	u8 addr = UFCS_ADDR_SRC;
	u16 buf_16 = 0;
	u32 buf_32 = 0;
	u64 buf_64 = 0;

	oplus_ufcs_protocol_clr_tx_buffer();
	g_protocol->dev_msg.header = UFCS_HEADER(addr, g_protocol->msg_send_id, HEAD_TYPE_DATA);

	switch (data_msg) {
	case UFCS_DATA_REQUEST:
		g_protocol->dev_msg.command = UFCS_DATA_REQUEST;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + REQUEST_PDO_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(g_protocol->dev_buffer, &buf_64, sizeof(struct request_pdo));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_SINK_INFO:
		g_protocol->dev_msg.command = UFCS_DATA_SINK_INFO;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + sizeof(struct sink_info);
		buf_64 = cpu_to_be64p((u64 *)buffer);
		memcpy(g_protocol->dev_buffer, &buf_64, sizeof(struct sink_info));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_VERIFY_REQUEST:
		g_protocol->dev_msg.command = UFCS_DATA_VERIFY_REQUEST;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + VERIFY_REQUEST_LENGTH;
		memcpy(g_protocol->dev_buffer, buffer, sizeof(struct verify_request));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_DEVICE_INFO:
		g_protocol->dev_msg.command = UFCS_DATA_DEVICE_INFO;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + DEVICE_INFO_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(g_protocol->dev_buffer, &buf_64, sizeof(struct device_info));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_ERROR_INFO:
		g_protocol->dev_msg.command = UFCS_DATA_ERROR_INFO;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + sizeof(struct error_info);
		buf_32 = cpu_to_be32p((u32 *)buffer);
		memcpy(g_protocol->dev_buffer, &buf_32, sizeof(struct error_info));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_CONFIG_WATCHDOG:
		g_protocol->dev_msg.command = UFCS_DATA_CONFIG_WATCHDOG;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + CONFIG_WTD_LENGTH;
		buf_16 = *(u16 *)buffer;
		buf_16 = SWAP16(buf_16);
		memcpy(g_protocol->dev_buffer, &buf_16, sizeof(struct config_watchdog));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_DATA_REFUSE:
		g_protocol->dev_msg.command = UFCS_DATA_REFUSE;
		g_protocol->dev_msg.len = DATA_LENGTH_BLANK + REFUSE_MSG_LENGTH;
		buf_32 = *(u32 *)buffer;
		buf_32 = SWAP32(buf_32);
		memcpy(g_protocol->dev_buffer, &buf_32, sizeof(struct refuse_msg));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	default:
		ufcs_err("data msg can't be handled! msg = %d\n", data_msg);
		break;
	}
	msleep(10);

	return &g_protocol->dev_msg;
}

static struct comm_msg *oplus_ufcs_protocol_construct_vnd_msg(enum UFCS_VND_MSG_TYPE vnd_msg, void *buffer)
{
	u8 addr = UFCS_ADDR_SRC;
	u64 buf_64 = 0;

	oplus_ufcs_protocol_clr_tx_buffer();
	g_protocol->dev_msg.header = UFCS_HEADER(addr, g_protocol->msg_send_id, HEAD_TYPE_VENDER);

	switch (vnd_msg) {
	case UFCS_VND_GET_EMARK_INFO:
		g_protocol->dev_msg.command = UFCS_VND_GET_EMARK_INFO;
		g_protocol->dev_msg.vender = OPLUS_DEV_VENDOR;
		g_protocol->dev_msg.len = REQUEST_EMARK_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(g_protocol->dev_buffer, &buf_64, sizeof(struct emark_info));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	case UFCS_VND_GET_POWER_INFO:
		g_protocol->dev_msg.command = UFCS_VND_GET_POWER_INFO;
		g_protocol->dev_msg.vender = OPLUS_DEV_VENDOR;
		g_protocol->dev_msg.len = REQUEST_POWER_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(g_protocol->dev_buffer, &buf_64, sizeof(struct charger_power));
		g_protocol->dev_msg.buf = g_protocol->dev_buffer;
		break;
	default:
		ufcs_err("data msg can't be handled! msg = %d\n", vnd_msg);
		break;
	}
	msleep(10);
	return &g_protocol->dev_msg;
}

static int oplus_ufcs_protocol_trans_msg(struct oplus_ufcs_protocol *chip, struct comm_msg *msg)
{
	u8 *tx_buf;
	u16 k = 0;
	u16 send_len = 0;
	u16 i = 0;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_write_msg)
		return -ENODEV;

	if (msg->len > MAX_TX_MSG_SIZE) {
		ufcs_err("message length is too long. length = %d \n", msg->len);
		return -ENOMEM;
	}
	ufcs_info(" [0x%x, 0x%x, 0x%x]\n", msg->header, msg->command, chip->msg_send_id);

	tx_buf = kzalloc(MAX_TX_BUFFER_SIZE, GFP_KERNEL);
	if (!tx_buf) {
		ufcs_err("failed to allocate memory for tx_buf\n");
		return -ENOMEM;
	}

	tx_buf[i++] = msg->header >> 8;
	tx_buf[i++] = msg->header & 0xff;
	tx_buf[i++] = msg->command;
	tx_buf[i++] = msg->len;

	for (k = 0; k < msg->len; k++)
		tx_buf[i++] = msg->buf[k];

	if (msg->len)
		send_len = msg->len + WRT_DATA_BGN_POS;
	else
		send_len = 3;

	chip->ops->ufcs_ic_write_msg(tx_buf, send_len);
	oplus_ufcs_protocol_id_inc();
	kfree(tx_buf);

	return 0;
}

static int oplus_ufcs_protocol_trans_vnd(struct oplus_ufcs_protocol *chip, struct comm_msg *msg)
{
	u8 *tx_buf;
	u16 k = 0;
	u16 send_len = 0;
	u16 i = 0;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_write_msg)
		return -ENODEV;

	if (msg->len > MAX_TX_MSG_SIZE) {
		ufcs_err("message length is too long. length = %d \n", msg->len);
		return -ENOMEM;
	}
	ufcs_info(" [0x%x, 0x%x, 0x%x]\n", msg->header, msg->command, chip->msg_send_id);

	tx_buf = kzalloc(MAX_TX_BUFFER_SIZE, GFP_KERNEL);
	if (!tx_buf) {
		ufcs_err("failed to allocate memory for tx_buf\n");
		return -ENOMEM;
	}

	tx_buf[i++] = msg->header >> 8;
	tx_buf[i++] = msg->header & 0xff;
	tx_buf[i++] = msg->vender >> 8;
	tx_buf[i++] = msg->vender & 0xff;
	tx_buf[i++] = msg->len + VND_LENGTH_BLANK;
	tx_buf[i++] = msg->command;

	for (k = 0; k < msg->len; k++)
		tx_buf[i++] = msg->buf[k];

	send_len = msg->len + WRT_VENDER_LEN_POS;
	chip->ops->ufcs_ic_write_msg(tx_buf, send_len);
	oplus_ufcs_protocol_id_inc();
	kfree(tx_buf);

	return 0;
}

static void oplus_ufcs_protocol_retrieve_flag_err(struct oplus_ufcs_protocol *chip)
{
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_retrieve_flags)
		return;

	if ((chip->state != STATE_IDLE && chip->state != STATE_WAIT_MSG))
		return;
	chip->ops->ufcs_ic_retrieve_flags(chip);
}

static int oplus_ufcs_protocol_rcv_msg(struct oplus_ufcs_protocol *chip)
{
	int i = 0;
	struct comm_msg *rcv;
	struct receive_error *rf;
	struct communication_error *ce;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_rcv_msg)
		return -ENODEV;

	rcv = &chip->rcv_msg;
	rf = &chip->flag.rcv_error;
	ce = &chip->flag.commu_error;
	/*Decide whether to receive rx buffer*/
	if (ce->bus_conflict) {
		chip->soft_reset = 1;
		ufcs_err("bus conflict! try to softreset!\n");
		goto error;
	}

	if (rf->sent_cmp) {
		if (rf->msg_trans_fail) {
			ufcs_err("sent packet complete = 1 && msg trans fail!\n");
			goto error;
		}
		if (rf->ack_rcv_timeout)
			ufcs_err("sent packet complete = 1 && ack receive timeout!\n");
		if (rf->data_rdy)
			ufcs_err("sent packet complete = 1 && data ready = 1!\n");
	} else {
		if (!rf->data_rdy) {
			ufcs_err("sent packet complete = 0 && data ready = 0!\n");
			goto error;
		}
	}

	if ((chip->state != STATE_IDLE) && (chip->state != STATE_WAIT_MSG)) {
		chip->ack_received = 1;
		if (rf->data_rdy)
			ufcs_err("ack interrupt read delayed! need to read msg!\n");
		else
			return 0;
	}
	chip->ops->ufcs_ic_rcv_msg(chip);

	oplus_ufcs_protocol_retrieve_flag_err(chip);

	rcv->header = (chip->rcv_buffer[i++]) << 8;
	rcv->header += (chip->rcv_buffer[i++]);
	if ((rcv->header == 0x00) || (rcv->header == 0xFF)) {
		ufcs_err("receive header is wrong! header = 0x%02x", rcv->header);
		return 0;
	}

	chip->msg_recv_id = UFCS_HEADER_ID(rcv->header);

	if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_CTRL) {
		rcv->command = chip->rcv_buffer[i++];
		rcv->len = 0;
		rcv->buf = NULL;
	} else if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_DATA) {
		rcv->command = chip->rcv_buffer[i++];
		rcv->len = chip->rcv_buffer[i++];
		rcv->buf = &chip->rcv_buffer[READ_DATA_BGN_POS];
	} else if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_VENDER) {
		rcv->vender = chip->rcv_buffer[i++] << 8;
		rcv->vender += chip->rcv_buffer[i++];
		rcv->len = chip->rcv_buffer[i++];
		rcv->command = chip->rcv_buffer[i++];
		rcv->buf = &chip->rcv_buffer[READ_VENDER_BGN_POS];
	} else {
		ufcs_err("head err[0x%x, 0x%x]\n", rcv->header, chip->rcv_buffer[i++]);
		goto error;
	}
	ufcs_info("head ok[0x%x, 0x%x, 0x%x]\n", rcv->header, rcv->command, rcv->len);
	return 0;

error:
	if ((chip->state != STATE_IDLE) && (chip->state != STATE_WAIT_MSG))
		chip->ack_received = 0;
	return -EINVAL;
}

static int oplus_ufcs_protocol_chip_enable(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_enable)
		return -ENODEV;
	rc = chip->ops->ufcs_ic_enable();

	oplus_ufcs_protocol_reset_state_machine();
	oplus_ufcs_protocol_set_state(STATE_HANDSHAKE);
	return rc;
}

static int oplus_ufcs_protocol_chip_disable(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_disable)
		return -ENODEV;
	rc = chip->ops->ufcs_ic_disable();

	oplus_ufcs_protocol_reset_state_machine();
	return 0;
}

static int oplus_ufcs_protocol_report_error(struct oplus_ufcs_error *error)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	if (IS_ERR_OR_NULL(error)) {
		ufcs_err("ufcs_error_flag buffer is null!\n");
		return -EINVAL;
	}

	if (chip->get_flag_failed) {
		ufcs_err("failed to read protocol buffer \n");
		return -EINVAL;
	}

	memcpy(error, &chip->error, sizeof(struct oplus_ufcs_error));
	return 0;
}

static int oplus_ufcs_protocol_if_legal_state(enum UFCS_MACHINE_STATE expected_state)
{
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	if (chip->state != expected_state) {
		ufcs_err("state machine expected = %d, now in = %d\n", expected_state, chip->state);
		return -EINVAL;
	}

	if (chip->uct_mode == true) {
		ufcs_err("chip is in uct_mode! ap is not allowed to call function!\n");
		return -EBUSY;
	}

	if (chip->error.i2c_error) {
		ufcs_err("i2c error happened over 2 times!\n");
		return -EINVAL;
	}

	if (chip->error.rcv_hardreset) {
		ufcs_err("received hard reset!\n");
		return -EINVAL;
	}

	if (chip->error.hardware_error) {
		ufcs_err("hardware error happened!\n");
		return -EINVAL;
	}

	if (chip->error.power_rdy_timeout) {
		ufcs_err("power ready timeout!\n");
		return -EINVAL;
	}

	return 0;
}

static int oplus_ufcs_protocol_wait_for_ack(int ack_time, int *expected_flag, int expected_val)
{
	int rc = 0;
	struct receive_error *rcv_err;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rcv_err = &chip->flag.rcv_error;
	if (chip->state != STATE_HANDSHAKE)
		oplus_ufcs_protocol_set_state(STATE_WAIT_ACK);

	reinit_completion(&chip->rcv_cmp);
	rc = wait_for_completion_timeout(&chip->rcv_cmp, msecs_to_jiffies(ack_time));
	if (!rc) {
		ufcs_err("ACK TIMEOUT!\n");
		goto error;
	}

	if (*expected_flag == expected_val)
		return 0;
	else {
		ufcs_err("expected flag value = %d, real value = %d\n", expected_val, *expected_flag);
		goto error;
	}

	return 0;

error:
	if ((rcv_err->sent_cmp == 1) && (rcv_err->msg_trans_fail == 1))
		chip->soft_reset = 1;
	return -EBUSY;
}

static int oplus_ufcs_protocol_check_refuse_msg(void)
{
	struct refuse_msg refuse = { 0 };
	u32 data = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	data = be32_to_cpup((u32 *)chip->rcv_msg.buf);
	memcpy(&refuse, &data, sizeof(struct refuse_msg));
	ufcs_err(" reason = 0x%x\n", refuse.reason);

	if (refuse.reason == REASON_DEVICE_BUSY)
		chip->soft_reset = 1;
	return -EPERM;
}

static int oplus_ufcs_protocol_wait_for_msg(callback func, void *buffer, int msg_time, int expected_type,
					    int expected_msg)
{
	int rc = 0;
	int msg_type = 0;
	int msg_command = 0;
	struct receive_error *rf;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rf = &chip->flag.rcv_error;
	oplus_ufcs_protocol_set_state(STATE_WAIT_MSG);

	if ((rf->sent_cmp) && (rf->data_rdy)) {
		oplus_ufcs_protocol_clr_flag_reg();
		goto msg_parse;
	}

	reinit_completion(&chip->rcv_cmp);
	rc = wait_for_completion_timeout(&chip->rcv_cmp, msecs_to_jiffies(msg_time));
	if (!rc) {
		ufcs_err("wait for msg = 0x%02x, timeout!\n", expected_msg);
		return -EBUSY;
	}

	if (chip->msg_received == 0) {
		ufcs_err("no message is received!\n");
		return -EINVAL;
	}

msg_parse:
	msg_type = UFCS_HEADER_TYPE(chip->rcv_msg.header);
	msg_command = chip->rcv_msg.command;

	if ((msg_type != expected_type) || (msg_command != expected_msg)) {
		if ((msg_type != HEAD_TYPE_DATA) || (msg_command != UFCS_DATA_REFUSE)) {
			ufcs_err("unexpected message received! type = 0x%x, msg = 0x%02x\n", msg_type, msg_command);
			return -EINVAL;
		} else {
			return oplus_ufcs_protocol_check_refuse_msg();
		}
	}

	if (func != NULL)
		rc = (*func)(buffer);

	if (rc < 0)
		return -EBUSY;
	else
		return 0;
}

static int oplus_ufcs_protocol_handshake(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_handshake)
		return -ENODEV;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_HANDSHAKE);
	if (rc < 0)
		return rc;

	rc = chip->ops->ufcs_ic_handshake();
	if (rc < 0) {
		ufcs_err("I2c send handshake error\n");
		return rc;
	}

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_HANDSHAKE_TIMEOUT, &g_protocol->handshake_success, 1);
	if (rc < 0) {
		ufcs_err("handshake ack error\n");
		return rc;
	} else {
		ufcs_info("handshake success!\n");
		oplus_ufcs_protocol_set_state(STATE_PING);
		chip->ack_received = 0;
		chip->msg_received = 0;
		return 0;
	}
}

static int oplus_ufcs_protocol_ping(void)
{
	int rc = 0;
	enum UFCS_BAUD_RATE baud_num = UFCS_BAUD_115200;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_ping)
		return -ENODEV;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_PING);
	if (rc < 0)
		return rc;

retry:
	if (baud_num > UFCS_BAUD_19200) {
		oplus_ufcs_protocol_set_state(STATE_PING);
		ufcs_err("baud number is lower than 19200, failed!\n");
		return -EBUSY;
	}
	rc = chip->ops->ufcs_ic_ping(baud_num);

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_PING));
	if (rc < 0) {
		ufcs_err("failed to send ping to protocol !\n");
		baud_num++;
		goto retry;
	}

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_PING_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		baud_num++;
		ufcs_err("change baud rate to %d\n", baud_num);
		goto retry;
	} else {
		oplus_ufcs_protocol_call_end();
		ufcs_info("ping success!\n");
		return baud_num;
	}
}

static int oplus_ufcs_protocol_send_refuse(enum UFCS_REFUSE_REASON e_refuse_reason)
{
	int rc = 0;
	struct comm_msg *rcv;
	struct refuse_msg msg = { 0 };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rcv = &chip->rcv_msg;
	msg.reason = e_refuse_reason;
	msg.cmd = rcv->command;
	msg.msg_type = UFCS_HEADER_TYPE(rcv->header);
	msg.msg_id = chip->msg_recv_id;

	rc = oplus_ufcs_protocol_trans_msg(chip,
					   oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_REFUSE, (void *)(&msg)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to send refuse to adapter\n");
	return rc;
}

static int oplus_ufcs_protocol_parse_src_cap(void *payload)
{
	struct oplus_ufcs_src_cap *cap = (struct oplus_ufcs_src_cap *)payload;
	u64 *buffer = (u64 *)(g_protocol->rcv_msg.buf);
	struct source_cap *output_cap = NULL;
	u64 data = 0;
	int i = 0;
	int len = g_protocol->rcv_msg.len / PDO_LENGTH;

	if ((len > MAX_PDO_NUM) || (len < MIN_PDO_NUM)) {
		ufcs_err("error PDO number = %d", len);
		oplus_ufcs_protocol_send_refuse(REASON_CMD_NOT_DETECT);
		return -EINVAL;
	}

	memset(cap, 0, sizeof(struct oplus_ufcs_src_cap));
	cap->cap_num = len;

	for (i = 0; i < len; i++, buffer++) {
		data = SWAP64(*buffer);
		output_cap = (struct source_cap *)(&data);
		cap->min_curr[i] = (output_cap->min_cur) * 10;
		cap->max_curr[i] = (output_cap->max_cur) * 10;
		cap->min_volt[i] = (output_cap->min_vol) * 10;
		cap->max_volt[i] = (output_cap->max_vol) * 10;
	}

	memset(&g_protocol->src_cap, 0, sizeof(struct oplus_ufcs_src_cap));
	memcpy(&g_protocol->src_cap, cap, sizeof(struct oplus_ufcs_src_cap));
	return 0;
}

static int __oplus_ufcs_protocol_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_GET_OUTPUT_CAP));
	if (rc < 0) {
		ufcs_err("failed to send get output cap to protocol\n");
		goto end;
	}

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0)
		goto end;

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_src_cap, (void *)buffer, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_DATA, UFCS_DATA_OUTPUT_CAP);
	if (rc < 0)
		goto end;

	return 0;

end:
	ufcs_err("failed to get source cap from adapter!!!\n");
	return rc;
}

static int _oplus_ufcs_protocol_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_retrieve_output_capabilities(buffer);

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_retrieve_output_capabilities(buffer);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send get_output_cap to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_retrieve_output_capabilities(buffer);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_select_legal_pdo(struct request_pdo *req, int volt, int curr)
{
	struct oplus_ufcs_src_cap *pdo = &g_protocol->src_cap;
	int i = 0;
	if (g_protocol->uct_mode == true) {
		ufcs_err("uct_mode\n");
		req->cur = curr / 10;
		req->vol = volt / 10;
		req->reserve = 0;
		req->index = 1;
		return 0;
	}

	if (pdo->cap_num == 0) {
		ufcs_err("empty pdo list! \n");
		return -EBUSY;
	}

	for (i = 0; i < pdo->cap_num; i++) {
		if ((volt >= pdo->min_volt[i]) && (volt <= pdo->max_volt[i])) {
			if ((curr >= pdo->min_curr[i]) && (curr <= pdo->max_curr[i])) {
				req->cur = curr / 10;
				req->vol = volt / 10;
				req->index = i + 1;
				return 0;
			}
		}
	}

	ufcs_err("there's no legal pdo to select!!!\n");
	return -EBUSY;
}

static int oplus_ufcs_protocol_wait_power_request(void *buf)
{
	int rc = 0;

	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	oplus_ufcs_protocol_set_state(STATE_WAIT_MSG);

	reinit_completion(&chip->rcv_cmp);
	rc = wait_for_completion_timeout(&chip->rcv_cmp, msecs_to_jiffies(UFCS_POWER_RDY_TIMEOUT));
	if (!rc) {
		ufcs_err("Power Ready TIMEOUT!\n");
		goto error;
	}

	if (chip->msg_received == 0) {
		ufcs_err("invalid message received!\n");
		goto error;
	}

	if ((UFCS_HEADER_TYPE(chip->rcv_msg.header) != HEAD_TYPE_CTRL) ||
	    (chip->rcv_msg.command != UFCS_CTRL_POWER_READY)) {
		ufcs_err("Power ready isn't received! Received %02x\n", chip->rcv_msg.command);
		goto error;
	}

	return 0;

error:
	chip->error.power_rdy_timeout = 1;
	oplus_ufcs_protocol_source_hard_reset();
	return -EBUSY;
}

static int __oplus_ufcs_protocol_power_negotiation(int volt, int curr)
{
	int rc = 0;
	struct request_pdo pdo = { 0 };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_select_legal_pdo(&pdo, volt, curr);
	if (rc < 0)
		return rc;

	rc = oplus_ufcs_protocol_trans_msg(chip,
					   oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_REQUEST, (void *)(&pdo)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_wait_power_request, NULL, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_CTRL, UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("failed to receive POWER READY\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to request pdo to adapter!!!\n");
	return rc;
}

static int _oplus_ufcs_protocol_power_negotiation(int volt, int curr)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_power_negotiation(volt, curr);

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_power_negotiation(volt, curr);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_power_negotiation(int volt, int curr)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send request to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_power_negotiation(volt, curr);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_get_scap_imax(int volt)
{
	struct oplus_ufcs_src_cap *pdo = &g_protocol->src_cap;
	int i = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	oplus_ufcs_protocol_get_chip_lock();
	if (pdo->cap_num == 0) {
		ufcs_err("empty pdo list! \n");
		goto error;
	}

	for (i = 0; i <= pdo->cap_num - 1; i++) {
		if ((volt >= pdo->min_volt[i]) && (volt <= pdo->max_volt[i])) {
			oplus_ufcs_protocol_release_chip_lock();
			return pdo->max_curr[i];
		}
	}
	ufcs_err("there's no legal pdo to select!!!\n");
	for (i = 0; i <= pdo->cap_num - 1; i++) {
		ufcs_err("available pdo list %d: volt = %d ~ %d, current = %d ~ %d", i, pdo->min_volt[i],
			 pdo->max_volt[i], pdo->min_curr[i], pdo->max_curr[i]);
	}

error:
	oplus_ufcs_protocol_release_chip_lock();
	return -EBUSY;
}

static void oplus_ufcs_protocol_reconstruct_msg(u8 *array, int length)
{
	int i = 0;
	int j = length - 1;
	u8 tmp = 0;

	for (; i <= j; i++, j--) {
		tmp = array[j];
		array[j] = array[i];
		array[i] = tmp;
	}
}

static int oplus_ufcs_protocol_send_device_info(void *buffer)
{
	struct device_info dev_info = { 0 };
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	dev_info.dev_vender = OPLUS_DEV_VENDOR;
	dev_info.ic_vender = DEV_VENDOR_ID;
	dev_info.hardware_ver = DEV_HARDWARE_VER;
	dev_info.software_ver = DEV_SOFTWARE_VER;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_DEVICE_INFO,
											(void *)(&dev_info)));
	if (rc < 0) {
		ufcs_err("failed to send dev_info to adapter");
		return rc;
	}

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		return rc;
	}

	return 0;
}

static int oplus_ufcs_protocol_parse_verify_request(void *buffer)
{
	struct verify_response *resp = (struct verify_response *)buffer;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	if (chip->rcv_msg.len != VERIFY_RESPONSE_LENGTH) {
		ufcs_err("verify response length = %d, not 48!\n", chip->rcv_msg.len);
		return -EINVAL;
	}
	memcpy(resp->encrypt_result, chip->rcv_msg.buf, ADAPTER_ENCRYPT_LENGTH);
	memcpy(resp->adapter_random, &chip->rcv_msg.buf[ADAPTER_ENCRYPT_LENGTH], ADAPTER_RANDOM_LENGTH);
	oplus_ufcs_protocol_reconstruct_msg(resp->encrypt_result, ADAPTER_ENCRYPT_LENGTH);
	oplus_ufcs_protocol_reconstruct_msg(resp->adapter_random, ADAPTER_RANDOM_LENGTH);
	return 0;
}

static int __oplus_ufcs_protocol_adapter_encryption(struct oplus_ufcs_encry_array *array)
{
	int rc = 0;
	struct verify_request req = { 0 };
	struct verify_response resp = { 0 };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	memcpy(req.phone_random, array, PHONE_RANDOM_LENGTH);
	oplus_ufcs_protocol_reconstruct_msg(req.phone_random, PHONE_RANDOM_LENGTH);
	req.id = UFCS_KEY_VER;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_VERIFY_REQUEST,
											(void *)(&req)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(NULL, NULL, UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL, UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("failed to receive accept\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_send_device_info, NULL, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_CTRL, UFCS_CTRL_GET_DEVICE_INFO);
	if (rc < 0) {
		ufcs_err("get_device_info failed!\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_verify_request, &resp, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_DATA, UFCS_DATA_VERIFY_RESPONSE);
	if (rc < 0) {
		ufcs_err("verify_response failed!\n");
		goto error;
	}

	memcpy(array->adapter_random, resp.adapter_random, ADAPTER_RANDOM_LENGTH);
	memcpy(array->adapter_encry_result, resp.encrypt_result, ADAPTER_ENCRYPT_LENGTH);
	return 0;

error:
	ufcs_err("failed to encrypt with adapter!\n");
	return rc;
}

static int oplus_ufcs_protocol_adapter_encryption(struct oplus_ufcs_encry_array *array)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send verify_request to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = __oplus_ufcs_protocol_adapter_encryption(array);
	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_adapter_encryption(array);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_parse_src_info(void *buffer)
{
	int **p = (int **)buffer;
	u64 *raw_data = (u64 *)g_protocol->rcv_msg.buf;
	u64 data = SWAP64(*raw_data);
	struct src_info *res = (struct src_info *)(&data);

	*p[0] = (res->vol) * 10;
	*p[1] = (res->cur) * 10;

	return 0;
}

static int __oplus_ufcs_protocol_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;
	int *p[2] = { p_volt, p_curr };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_GET_SOURCE_INFO));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_src_info, p, UFCS_MSG_TIMEOUT, HEAD_TYPE_DATA,
					      UFCS_DATA_SOURCE_INFO);
	if (rc < 0) {
		ufcs_err("failed to receive source info from adapter\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to get source info\n");
	return rc;
}

static int _oplus_ufcs_protocol_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_retrieve_source_info(p_volt, p_curr);

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_retrieve_source_info(p_volt, p_curr);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send get src_info to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_retrieve_source_info(p_volt, p_curr);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_parse_cable_info(void *buffer)
{
	struct oplus_cable_info *cap = (struct oplus_cable_info *)buffer;
	u64 *raw_data = (u64 *)g_protocol->rcv_msg.buf;
	u64 data = SWAP64(*raw_data);
	struct cable_info *res = (struct cable_info *)(&data);

	memset(cap, 0, sizeof(struct oplus_cable_info));
	cap->imax = (res->imax) * 10;
	cap->vmax = (res->vmax) * 10;
	cap->resistence = (res->rcable);
	ufcs_info("[0x%x, 0x%x, 0x%x]\n", cap->imax, cap->vmax, cap->resistence);
	return 0;
}

static int __oplus_ufcs_protocol_cable_detect_start(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_START_CABLE_DETECT));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed start\n");
	return rc;
}

static int _oplus_ufcs_protocol_cable_detect_start(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_cable_detect_start();

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_cable_detect_start();
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_cable_detect_start(void)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send cable encryption\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_cable_detect_start();
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int __oplus_ufcs_protocol_cable_detect_end(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_END_CABLE_DETECT));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed end\n");
	return rc;
}

static int _oplus_ufcs_protocol_cable_detect_end(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_cable_detect_end();

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_cable_detect_end();
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_cable_detect_end(void)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send cable encryption\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_cable_detect_end();
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int __oplus_ufcs_protocol_cable_encryption(struct oplus_cable_info *buffer)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_GET_CABLE_INFO));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_cable_info, (void *)buffer, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_DATA, UFCS_DATA_CABLE_INFO);
	if (rc < 0) {
		ufcs_err("failed to receive cable info from adapter\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to get cable info\n");
	return rc;
}

static int _oplus_ufcs_protocol_cable_encryption(struct oplus_cable_info *buffer)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_cable_encryption(buffer);

	if ((rc < 0) && (chip->soft_reset)) {
		oplus_ufcs_protocol_cable_hard_reset();
	}

	return rc;
}

static int oplus_ufcs_protocol_cable_encryption(struct oplus_cable_info *buffer)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send cable encryption\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_cable_encryption(buffer);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_get_cable_info(struct oplus_cable_info *buffer)
{
	int rc = 0;

	oplus_ufcs_protocol_cable_detect_start();
	oplus_ufcs_protocol_cable_encryption(buffer);
	oplus_ufcs_protocol_cable_detect_end();
	return rc;
}

static int oplus_ufcs_protocol_parse_emark_info(void *buffer)
{
	struct oplus_emark_info *cap = (struct oplus_emark_info *)buffer;
	u64 *raw_data = (u64 *)g_protocol->rcv_msg.buf;
	u64 data = SWAP64(*raw_data);
	struct emark_info *emark_power = (struct emark_info *)(&data);

	memset(cap, 0, sizeof(struct oplus_emark_info));
	cap->imax = (emark_power->imax) * 10;
	cap->vmax = (emark_power->vmax) * 10;
	cap->status = (emark_power->status);
	cap->hw_id = (emark_power->hw_id);
	return 0;
}

static int __oplus_ufcs_protocol_emark_encryption(struct oplus_emark_info *buffer)
{
	int rc = 0;
	struct emark_info emark_ability = { 0 };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_vnd(chip, oplus_ufcs_protocol_construct_vnd_msg(UFCS_VND_GET_EMARK_INFO,
										       (void *)(&emark_ability)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_emark_info, (void *)buffer, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_VENDER, UFCS_VND_RCV_EMARK_INFO);
	if (rc < 0) {
		ufcs_err(" error\n");
		goto error;
	}

	return 0;
error:
	ufcs_err("fail\n");
	return rc;
}

static int _oplus_ufcs_protocol_emark_encryption(struct oplus_emark_info *buffer)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = __oplus_ufcs_protocol_emark_encryption(buffer);

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_emark_encryption(buffer);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}
	return rc;
}

static int oplus_ufcs_protocol_emark_encryption(struct oplus_emark_info *buffer)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send cable encryption\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_emark_encryption(buffer);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_parse_adapter_id(void *payload)
{
	u64 data = be64_to_cpup((u64 *)g_protocol->rcv_msg.buf);
	memcpy(payload, &data, sizeof(struct device_info));
	return 0;
}

static u16 __oplus_ufcs_protocol_retrieve_adapter_id(void)
{
	int rc = 0;
	struct device_info dev = { 0 };
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_trans_msg(chip, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_GET_DEVICE_INFO));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &chip->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_adapter_id, &dev, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_DATA, UFCS_DATA_DEVICE_INFO);
	if (rc < 0) {
		ufcs_err("failed to receive device info from adapter\n");
		goto error;
	}
	if (((dev.software_ver >= ABNORMAL_HARDWARE_VER_A_MIN) && (dev.software_ver <= ABNORMAL_HARDWARE_VER_A_MAX)) &&
	    ((dev.ic_vender == ABNORMAL_IC_VEND) && (dev.dev_vender == ABNORMAL_DEV_VEND)))
		chip->abnormal_id = true;

	if ((dev.ic_vender == UCT_MODE_IC_VEND) && (dev.dev_vender == UCT_MODE_DEV_VEND))
		chip->uct_mode = true;

	if ((dev.ic_vender == OPLUS_IC_VENDOR) && (dev.dev_vender == OPLUS_DEV_VENDOR))
		chip->oplus_id = true;
	else
		chip->oplus_id = false;
	return dev.hardware_ver;

error:
	ufcs_err("failed to get adapter id\n");
	return rc;
}

static u16 oplus_ufcs_protocol_retrieve_adapter_id(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip)
		return -ENODEV;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0)
		return rc;

	oplus_ufcs_protocol_get_chip_lock();
	rc = __oplus_ufcs_protocol_retrieve_adapter_id();

	if ((rc < 0) && (chip->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_retrieve_adapter_id();
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static bool oplus_ufcs_protocol_check_oplus_id(void)
{
	return g_protocol->oplus_id;
}

static bool oplus_ufcs_protocol_check_abnormal_id(void)
{
	return g_protocol->abnormal_id;
}

static int oplus_ufcs_protocol_flags_handler(struct oplus_ufcs_protocol *chip)
{
	struct hardware_error *fault;
	int rc = 0;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_read_flags)
		return -ENODEV;

	fault = &chip->flag.hd_error;
	rc = chip->ops->ufcs_ic_read_flags(chip);

	if ((fault->dp_ovp) || (fault->dm_ovp) || (fault->temp_shutdown) || (fault->wtd_timeout)) {
		chip->error.hardware_error = 1;
		ufcs_err("read protocol: find hardware error!\n");
		goto error;
	}

	if (fault->hardreset) {
		chip->error.rcv_hardreset = 1;
		ufcs_err("read protocol: adapter request hardreset!\n");
		goto error;
	}

	return 0;
error:
	complete(&chip->rcv_cmp);
	return -1;
}

static int oplus_ufcs_protocol_source_hard_reset(void)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_source_hard_reset)
		return -ENODEV;

	oplus_ufcs_protocol_mos_set(false);
	rc = chip->ops->ufcs_ic_source_hard_reset();
	if (rc < 0) {
		ufcs_err("I2c send hardrst error\n");
	}

	msleep(100);
	return 0;
}

static int oplus_ufcs_protocol_send_soft_reset(void)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_trans_msg(g_protocol, oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_SOFT_RESET));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0)
		goto error;

	oplus_ufcs_protocol_reset_msg_counter();
	ufcs_err("sending soft reset complete");
	return 0;

error:
	oplus_ufcs_protocol_reset_msg_counter();
	oplus_ufcs_protocol_source_hard_reset();
	ufcs_err("failed to send soft reset! sending hard reset!\n");
	return -EBUSY;
}

static int oplus_ufcs_protocol_cable_hard_reset(void)
{
	int rc = 0;
	int retry_count = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_cable_hard_reset)
		return -ENODEV;

retry:
	retry_count++;
	if (retry_count > UFCS_HARDRESET_RETRY_CNTS) {
		ufcs_err("send hard reset, retry count over!\n");
		return -EBUSY;
	}
	rc = chip->ops->ufcs_ic_cable_hard_reset();
	if (rc < 0) {
		ufcs_err("I2c send cable reset error\n");
		goto retry;
	}

	msleep(100);
	return 0;
}

static int oplus_ufcs_protocol_restore_sink_info(struct oplus_ufcs_sink_info *info)
{
	memcpy(&g_protocol->info, info, sizeof(struct oplus_ufcs_sink_info));
	return 0;
}

static int oplus_ufcs_protocol_exit_ufcs_mode(void)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send exit_ufcs to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();

	rc = oplus_ufcs_protocol_trans_msg(g_protocol,
					   oplus_ufcs_protocol_construct_ctrl_msg(UFCS_CTRL_EXIT_UFCS_MODE));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return 0;

error:
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	ufcs_err("failed to send exit_ufcs_mode!\n");
	return rc;
}

static int __oplus_ufcs_protocol_config_watchdog(int time_ms)
{
	int rc = 0;
	struct config_watchdog wdt = { 0 };

	wdt.timer = time_ms;

	rc = oplus_ufcs_protocol_trans_msg(g_protocol, oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_CONFIG_WATCHDOG,
											      (void *)(&wdt)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(NULL, NULL, UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL, UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("config_watchdog is not accepted by adapter\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to send config_watchdog to adapter\n");
	return rc;
}

static int _oplus_ufcs_protocol_config_watchdog(int time_ms)
{
	int rc = 0;

	rc = __oplus_ufcs_protocol_config_watchdog(time_ms);

	if ((rc < 0) && (g_protocol->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_config_watchdog(time_ms);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_config_watchdog(int time_ms)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send config_watchdog to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_config_watchdog(time_ms);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static int oplus_ufcs_protocol_parse_power_info(void *power_info)
{
	struct oplus_charger_power *cap = (struct oplus_charger_power *)power_info;
	u64 *buffer = (u64 *)(g_protocol->rcv_msg.buf);
	struct charger_power *adapter_power = NULL;
	int len = g_protocol->rcv_msg.len / 8;
	int i = 0;
	u64 data = 0;

	memset(&g_protocol->charger_power, 0, sizeof(struct oplus_charger_power));
	memset(cap, 0, sizeof(struct oplus_charger_power));

	if ((len > 7) || (len < 1)) {
		ufcs_err("error PDO number = %d", len);
		oplus_ufcs_protocol_send_refuse(REASON_CMD_NOT_DETECT);
		return -EINVAL;
	}
	cap->num = len;

	for (i = 0; i < len; i++, buffer++) {
		data = SWAP64(*buffer);
		adapter_power = (struct charger_power *)(&data);
		cap->imax[i] = (adapter_power->vnd_imax) * 10;
		cap->vmax[i] = (adapter_power->vnd_vmax) * 10;
		cap->type[i] = (adapter_power->vnd_type);
	}
	memcpy(&g_protocol->charger_power, cap, sizeof(struct oplus_charger_power));

	return 0;
}

static int __oplus_ufcs_protocol_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;
	struct oplus_charger_power power_ability = { 0 };

	rc = oplus_ufcs_protocol_trans_vnd(g_protocol, oplus_ufcs_protocol_construct_vnd_msg(UFCS_VND_GET_POWER_INFO,
											     (void *)(&power_ability)));
	if (rc < 0)
		goto error;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = oplus_ufcs_protocol_wait_for_msg(oplus_ufcs_protocol_parse_power_info, (void *)buffer, UFCS_MSG_TIMEOUT,
					      HEAD_TYPE_VENDER, UFCS_VND_RCV_POWER_INFO);
	if (rc < 0) {
		ufcs_err(" error\n");
		goto error;
	}

	return 0;
error:
	ufcs_err("fail\n");
	return rc;
}

static int _oplus_ufcs_protocol_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;

	rc = __oplus_ufcs_protocol_get_power_ability(buffer);

	if ((rc < 0) && (g_protocol->soft_reset)) {
		if (!oplus_ufcs_protocol_send_soft_reset()) {
			rc = __oplus_ufcs_protocol_get_power_ability(buffer);
			if (rc < 0)
				oplus_ufcs_protocol_source_hard_reset();
		}
	}

	return rc;
}

static int oplus_ufcs_protocol_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;

	rc = oplus_ufcs_protocol_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send config_watchdog to adapter\n");
		return rc;
	}

	oplus_ufcs_protocol_get_chip_lock();
	rc = _oplus_ufcs_protocol_get_power_ability(buffer);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();
	return rc;
}

static void oplus_ufcs_protocol_notify_baudrate_change(void)
{
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_baudrate_change)
		return;

	if (!chip->flag.commu_error.baud_change) {
		ufcs_err("baud_rate change flag = 0, PING msg is received!\n");
		return;
	}
	chip->ops->ufcs_ic_baudrate_change();
}

static int oplus_ufcs_protocol_reply_sink_info(void)
{
	int rc = 0;
	struct sink_info info = { 0 };

	info.cur = abs(g_protocol->info.batt_curr / 10);
	info.vol = g_protocol->info.batt_volt / 10;
	info.usb_temp = ATTRIBUTE_NOT_SUPPORTED;
	info.dev_temp = ATTRIBUTE_NOT_SUPPORTED;

	rc = oplus_ufcs_protocol_trans_msg(g_protocol, oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_SINK_INFO,
											      (void *)(&info)));
	if (rc < 0)
		return rc;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int oplus_ufcs_protocol_reply_error_info(void)
{
	int rc = 0;
	struct error_info err = { 0 };

	rc = oplus_ufcs_protocol_trans_msg(g_protocol, oplus_ufcs_protocol_construct_data_msg(UFCS_DATA_ERROR_INFO,
											      (void *)(&err)));
	if (rc < 0)
		return rc;

	rc = oplus_ufcs_protocol_wait_for_ack(UFCS_ACK_TIMEOUT, &g_protocol->ack_received, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int oplus_ufcs_protocol_parse_power_change(void)
{
	int i = 0;
	u8 buffer[POWER_CHANGE_LENGTH] = { 0 };
	struct oplus_ufcs_src_cap *cap = &g_protocol->src_cap;
	u8 *msg = g_protocol->rcv_msg.buf;
	u8 len = g_protocol->rcv_msg.len / POWER_CHANGE_LENGTH;

	if (len != cap->cap_num) {
		ufcs_err("invalid power change capability number = %d, expected number = %d\n", len, cap->cap_num);
		goto error;
	}

	for (i = 0; i < len; i++) {
		memset(buffer, 0, POWER_CHANGE_LENGTH);
		memcpy(buffer, &msg[i * POWER_CHANGE_LENGTH], POWER_CHANGE_LENGTH);
		cap->max_curr[i] = (buffer[1] << 8 | buffer[2]) * 10;
		ufcs_err("power change! src_cap[%d]: voltage = %dmV, current = %dmA\n", i, cap->max_volt[i],
			 cap->max_curr[i]);
	}

	return 0;

error:
	ufcs_err("adapter send invalid power change message!\n");
	return -EINVAL;
}

static int oplus_ufcs_protocol_check_adapter_error(void)
{
	int rc = 0;
	unsigned long error_msg = be32_to_cpup((u32 *)g_protocol->rcv_msg.buf) & VALID_ERROR_INFO_BITS;
	unsigned long error_type = find_first_bit(&error_msg, ERROR_INFO_NUM);

	int len = g_protocol->rcv_msg.len;
	ufcs_info(" error_msg = %04x, len = %d to phone.\n", error_msg, len);

	if (len != ERROR_INFO_LEN) {
		ufcs_err("len = %d", len);
		oplus_ufcs_protocol_send_refuse(REASON_CMD_NOT_DETECT);
		return rc;
	}

	if (error_msg == ATTRIBUTE_NOT_SUPPORTED) {
		ufcs_err("adapter sent error info = 0!\n");
		return rc;
	}

	for (; error_type < ERROR_INFO_NUM; error_type = find_first_bit(&error_msg, ERROR_INFO_NUM)) {
		ufcs_err("receive adapter error! msg = %s\n", adapter_error_info[error_type]);
		clear_bit(error_type, &error_msg);
		return -EINVAL;
	}
	return rc;
}

static bool oplus_ufcs_protocol_check_uct_mode(void)
{
	return g_protocol->uct_mode;
}

static bool oplus_ufcs_protocol_quit_uct_mode(void)
{
	if (g_protocol->error.rcv_hardreset)
		return true;
	else
		return false;
}

static int oplus_ufcs_protocol_ctrl_uct_mode(int msg_num)
{
	int rc = 0;
	int volt = 0;
	int curr = 0;
	struct oplus_ufcs_src_cap cap = { 0 };

	switch (msg_num) {
	case UFCS_CTRL_GET_OUTPUT_CAP:
		rc = _oplus_ufcs_protocol_retrieve_output_capabilities(&cap);
		break;

	case UFCS_CTRL_GET_SOURCE_INFO:
		rc = _oplus_ufcs_protocol_retrieve_source_info(&volt, &curr);
		break;

	default:
		ufcs_err("uct mode message is not supported! msg_type = ctrl msg; msg_num = %d\n", msg_num);
		break;
	}

	return rc;
}

static int oplus_ufcs_protocol_data_uct_mode(int msg_num)
{
	int rc = 0;
	int volt = UFCS_UCT_POWER_VOLT;
	int curr = UFCS_UCT_POWER_CURR;

	switch (msg_num) {
	case UFCS_DATA_REQUEST:
		rc = _oplus_ufcs_protocol_power_negotiation(volt, curr);
		break;

	case UFCS_DATA_CONFIG_WATCHDOG:
		rc = _oplus_ufcs_protocol_config_watchdog(UCT_MODE_WATCHDOG);
		break;

	default:
		ufcs_err("uct mode message is not supported! msg_type = data msg; msg_num = %d\n", msg_num);
		break;
	}

	return rc;
}

static int oplus_ufcs_protocol_reply_uct_mode(void)
{
	struct uct_mode mode = { 0 };
	u16 uct_data = be16_to_cpup((u16 *)g_protocol->rcv_msg.buf);
	int rc = 0;
	memcpy(&mode, &uct_data, sizeof(struct uct_mode));

	if (mode.msg_type == HEAD_TYPE_CTRL)
		rc = oplus_ufcs_protocol_ctrl_uct_mode(mode.msg_num);
	else if (mode.msg_type == HEAD_TYPE_DATA)
		rc = oplus_ufcs_protocol_data_uct_mode(mode.msg_num);
	else
		ufcs_err("uct mode message type is not supported! msg_type = %d\n", mode.msg_type);

	return rc;
}

static int oplus_ufcs_protocol_reply_ctrl_msg(enum UFCS_CTRL_MSG_TYPE cmd)
{
	int rc = 0;

	switch (cmd) {
	case UFCS_CTRL_PING:
		oplus_ufcs_protocol_notify_baudrate_change();
		break;
	case UFCS_CTRL_SOFT_RESET:
		ufcs_err("receive soft reset from adapter!\n");
		oplus_ufcs_protocol_reset_msg_counter();
		break;
	case UFCS_CTRL_GET_SINK_INFO:
		rc = oplus_ufcs_protocol_reply_sink_info();
		break;
	case UFCS_CTRL_GET_DEVICE_INFO:
		rc = oplus_ufcs_protocol_send_device_info(NULL);
		break;
	case UFCS_CTRL_GET_ERROR_INFO:
		rc = oplus_ufcs_protocol_reply_error_info();
		break;
	case UFCS_CTRL_GET_CABLE_INFO:
	case UFCS_CTRL_DETECT_CABLE_INFO:
	case UFCS_CTRL_START_CABLE_DETECT:
	case UFCS_CTRL_END_CABLE_DETECT:
		ufcs_err("reply cable related topic to adapter. send refuse to "
			 "adapter\n");
		rc = oplus_ufcs_protocol_send_refuse(REASON_ID_NOT_SUPPORT);
		break;

	case UFCS_CTRL_EXIT_UFCS_MODE:
		ufcs_err("receive EXIT_UFCS_MODE from adapter!\n");
		g_protocol->error.rcv_hardreset = 1;
		break;

	default:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = oplus_ufcs_protocol_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	}

	return rc;
}

static int oplus_ufcs_protocol_reply_data_msg(enum UFCS_DATA_MSG_TYPE cmd)
{
	int rc = 0;

	switch (cmd) {
	case UFCS_DATA_CABLE_INFO:
	case UFCS_DATA_VERIFY_REQUEST:
	case UFCS_DATA_VERIFY_RESPONSE:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = oplus_ufcs_protocol_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	case UFCS_DATA_POWER_CHANGE:
		rc = oplus_ufcs_protocol_parse_power_change();
		break;
	case UFCS_DATA_ERROR_INFO:
		rc = oplus_ufcs_protocol_check_adapter_error();
		g_protocol->error.rcv_hardreset = 1;
		break;
	case UFCS_DATA_UCT_REQUEST:
		rc = oplus_ufcs_protocol_reply_uct_mode();
		break;
	default:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = oplus_ufcs_protocol_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	}

	return rc;
}

static void oplus_ufcs_protocol_reply_adapter(struct work_struct *work)
{
	struct oplus_ufcs_protocol *chip = container_of(work, struct oplus_ufcs_protocol, reply_work);
	int rc = 0;
	int msg_type = UFCS_HEADER_TYPE(chip->rcv_msg.header);
	int msg_command = chip->rcv_msg.command;

	oplus_ufcs_protocol_get_chip_lock();
	oplus_ufcs_protocol_set_awake(chip, true);
	ufcs_err(" [0x%x, 0x%02x]\n", msg_type, msg_command);

	if (msg_type == HEAD_TYPE_CTRL)
		rc = oplus_ufcs_protocol_reply_ctrl_msg(msg_command);
	else if (msg_type == HEAD_TYPE_DATA)
		rc = oplus_ufcs_protocol_reply_data_msg(msg_command);
	else {
		rc = oplus_ufcs_protocol_send_refuse(REASON_CMD_NOT_DETECT);
		ufcs_err("adapter sending wrong msg\n", msg_type, msg_command);
	}
	if (rc < 0)
		ufcs_err("reply to adapter failed!\n");

	if (chip->soft_reset)
		oplus_ufcs_protocol_send_soft_reset();

	oplus_ufcs_protocol_set_awake(chip, false);
	oplus_ufcs_protocol_release_chip_lock();
	oplus_ufcs_protocol_call_end();

	if (chip->error.rcv_hardreset)
		oplus_ufcs_source_exit();
}

static void oplus_ufcs_protocol_rcv_work(struct kthread_work *work)
{
	struct oplus_ufcs_protocol *chip = container_of(work, struct oplus_ufcs_protocol, rcv_work);
	int rc = 0;
	if (!chip || !chip->ops || !chip->ops->ufcs_ic_rcv_msg)
		return;

	oplus_ufcs_protocol_set_awake(chip, true);
	rc = oplus_ufcs_protocol_flags_handler(chip);
	if (rc < 0) {
		oplus_ufcs_protocol_set_awake(chip, false);
		return;
	}
	ufcs_err(" chip->state = %d\n", chip->state);

	if ((chip->state != STATE_HANDSHAKE) && (chip->state != STATE_NULL)) {
		oplus_ufcs_protocol_clr_rx_buffer();
		rc = oplus_ufcs_protocol_rcv_msg(chip);
		if (rc < 0) {
			oplus_ufcs_protocol_set_awake(chip, false);
			ufcs_err("error when retreive msg from ic!\n");
			return;
		}
	}

	switch (chip->state) {
	case STATE_HANDSHAKE:
	case STATE_PING:
	case STATE_WAIT_ACK:
		complete_all(&chip->rcv_cmp);
		break;
	case STATE_WAIT_MSG:
		if (!rc)
			chip->msg_received = 1;
		else
			chip->msg_received = 0;
		complete_all(&chip->rcv_cmp);
		break;
	case STATE_IDLE:
		queue_work(system_highpri_wq, &chip->reply_work);
		break;
	default:
		ufcs_err("state machine error! state = %d\n", chip->state);
		break;
	}
	oplus_ufcs_protocol_set_awake(chip, false);
	/*schedule_delayed_work(&chip->ufcs_service_work, 0);*/
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int oplus_ufcs_protocol_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

int oplus_ufcs_protocol_track_upload_i2c_err_info(int err_type, u16 reg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = oplus_ufcs_protocol_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}
	ufcs_debug("1 err_type = %d reg = %d\n", err_type, reg);

	if (chip->debug_force_i2c_err)
		err_type = -chip->debug_force_i2c_err;

	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_uploading) {
		pr_info("i2c_err_uploading, should return\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->i2c_err_load_trigger)
		kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_UFCS_IC_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = oplus_ufcs_protocol_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", chip->device_name);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_UFCS_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$access_reg@@0x%02x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	ufcs_debug("success\n");

	return 0;
}

static void oplus_ufcs_protocol_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_ufcs_protocol *chip = container_of(dwork, struct oplus_ufcs_protocol, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));

	kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = NULL;

	chip->i2c_err_uploading = false;
}
static int oplus_ufcs_protocol_dump_reg_info(struct oplus_ufcs_protocol *chip, char *dump_info, int len)
{
	int i = 0;
	int index = 0;

	if (!chip || !dump_info)
		return 0;

	for (i = 0; i < UFCS_REG_CNT; i++) {
		index += snprintf(&(dump_info[index]), len - index, "0x%02x=0x%02x,", i, chip->reg_dump[i]);
	}

	return 0;
}

int oplus_ufcs_protoco_track_upload_cp_err_info(int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	memset(chip->dump_info, 0, sizeof(chip->dump_info));
	curr_time = oplus_ufcs_protocol_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (err_type == TRACK_UFCS_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_cp_err_lock);
	if (chip->cp_err_uploading) {
		pr_info("cp_err_uploading, should return\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->cp_err_load_trigger)
		kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->cp_err_load_trigger) {
		pr_err("cp_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->cp_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->cp_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_UFCS_IC_ABNORMAL;
	chip->cp_err_uploading = true;
	upload_count++;
	pre_upload_time = oplus_ufcs_protocol_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", chip->device_name);
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_UFCS_PROTOCOL_ERR);

	oplus_chg_track_get_ufcs_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	oplus_ufcs_protocol_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void oplus_ufcs_protocol_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_ufcs_protocol *chip = container_of(dwork, struct oplus_ufcs_protocol, cp_err_load_trigger_work);

	if (!chip->cp_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));

	kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = NULL;

	chip->cp_err_uploading = false;
}

static int oplus_ufcs_protocol_track_debugfs_init(struct oplus_ufcs_protocol *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_ufcs_ic;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_ufcs_ic = debugfs_create_dir("ufcs_ic", debugfs_root);
	if (!debugfs_ufcs_ic) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_UFCS_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_ufcs_ic, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_ufcs_ic, &(chip->debug_force_cp_err));

	return ret;
}

static int oplus_ufcs_protocol_track_init(struct oplus_ufcs_protocol *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_cp_err_lock);
	mutex_init(&chip->track_upload_lock);

	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
	chip->cp_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work, oplus_ufcs_protocol_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work, oplus_ufcs_protocol_track_cp_err_load_trigger_work);

	rc = oplus_ufcs_protocol_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("ufcs ic debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static void ufcs_service(struct work_struct *work)
{
	/*do nothing now*/
	return;
}

static int oplus_ufcs_protocol_parse_dt(struct oplus_ufcs_protocol *chip)
{
	struct device_node *node = NULL;

	if (!chip) {
		return -1;
	}
	node = chip->dev->of_node;
	return 0;
}

static int oplus_ufcs_protocol_master_get_vbus(void)
{
	int cp_vbus = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_get_master_vbus) {
		return cp_vbus;
	}
	cp_vbus = chip->ops->ufcs_ic_get_master_vbus();
	return cp_vbus;
}

static int oplus_ufcs_protocol_master_get_ibus(void)
{
	int cp_ibus = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_get_master_ibus) {
		return cp_ibus;
	}
	cp_ibus = chip->ops->ufcs_ic_get_master_ibus();
	return cp_ibus;
}

static int oplus_ufcs_protocol_master_get_vac(void)
{
	int vac = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_get_master_vout) {
		return vac;
	}
	vac = chip->ops->ufcs_ic_get_master_vac();
	return vac;
}

static int oplus_ufcs_protocol_master_get_vout(void)
{
	int vout = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || !chip->ops || !chip->ops->ufcs_ic_get_master_vac) {
		return vout;
	}
	vout = chip->ops->ufcs_ic_get_master_vout();

	return vout;
}

struct oplus_ufcs_operations oplus_ufcs_protocol_ops = {
	.ufcs_get_btb_temp_status = oplus_ufcs_protocol_get_btb_temp_status,
	.ufcs_get_mos_temp_status = oplus_ufcs_protocol_get_mos_temp_status,
	.ufcs_get_batt_btb_tdiff = oplus_ufcs_protocol_get_battery_btb_tdiff,
	.ufcs_mos_set = oplus_ufcs_protocol_mos_set,
	.ufcs_mos_get = oplus_ufcs_protocol_mos_get,
	.ufcs_get_scap_imax = oplus_ufcs_protocol_get_scap_imax,
	.ufcs_cp_hardware_init = oplus_ufcs_protocol_cp_hardware_init,
	.ufcs_cp_mode_init = oplus_ufcs_protocol_cp_cfg_mode_init,
	.ufcs_chip_enable = oplus_ufcs_protocol_chip_enable,
	.ufcs_chip_disable = oplus_ufcs_protocol_chip_disable,
	.ufcs_get_error = oplus_ufcs_protocol_report_error,
	.ufcs_transfer_sink_info = oplus_ufcs_protocol_restore_sink_info,
	.ufcs_handshake = oplus_ufcs_protocol_handshake,
	.ufcs_ping = oplus_ufcs_protocol_ping,
	.ufcs_get_power_ability = oplus_ufcs_protocol_get_power_ability,
	.ufcs_get_src_cap = oplus_ufcs_protocol_retrieve_output_capabilities,
	.ufcs_request_pdo = oplus_ufcs_protocol_power_negotiation,
	.ufcs_adapter_encryption = oplus_ufcs_protocol_adapter_encryption,
	.ufcs_get_src_info = oplus_ufcs_protocol_retrieve_source_info,
	.ufcs_get_cable_info = oplus_ufcs_protocol_get_cable_info,
	.ufcs_get_emark_info = oplus_ufcs_protocol_emark_encryption,
	.ufcs_get_adapter_id = oplus_ufcs_protocol_retrieve_adapter_id,
	.ufcs_config_watchdog = oplus_ufcs_protocol_config_watchdog,
	.ufcs_exit = oplus_ufcs_protocol_exit_ufcs_mode,
	.ufcs_hard_reset = oplus_ufcs_protocol_source_hard_reset,
	.ufcs_check_uct_mode = oplus_ufcs_protocol_check_uct_mode,
	.ufcs_check_oplus_id = oplus_ufcs_protocol_check_oplus_id,
	.ufcs_check_abnormal_id = oplus_ufcs_protocol_check_abnormal_id,
	.ufcs_quit_uct_mode = oplus_ufcs_protocol_quit_uct_mode,
	.ufcs_get_cp_master_vbus = oplus_ufcs_protocol_master_get_vbus,
	.ufcs_get_cp_master_ibus = oplus_ufcs_protocol_master_get_ibus,
	.ufcs_get_cp_master_vac = oplus_ufcs_protocol_master_get_vac,
	.ufcs_get_cp_master_vout = oplus_ufcs_protocol_master_get_vout,
	.ufcs_get_mos0_switch = oplus_ufcs_protocol_get_mos0_switch,
	.ufcs_set_mos0_switch = oplus_ufcs_protocol_set_mos0_switch,
	.ufcs_get_mos1_switch = oplus_ufcs_protocol_get_mos1_switch,
	.ufcs_set_mos1_switch = oplus_ufcs_protocol_set_mos1_switch,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static void oplus_ufcs_sched_set_fifo(struct oplus_ufcs_protocol *chip)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO / 2 };

	if (!chip) {
		ufcs_err("chip null\n");
		return;
	}

	sched_setscheduler(chip->wq->task, SCHED_FIFO, &sp);
}
#else
static void oplus_ufcs_sched_set_fifo(struct oplus_ufcs_protocol *chip)
{
	if (!chip) {
		ufcs_err("chip null\n");
		return;
	}
	sched_set_fifo(chip->wq->task);
}

#endif

int oplus_ufcs_ops_register(struct oplus_ufcs_protocol_operations *ucs_protocol_ops, char *name)
{
	int rc = 0;
	struct oplus_ufcs_protocol *chip = g_protocol;

	if (!chip || chip->ops || !name) {
		ufcs_err("chip->ops not null!\n");
		return -ENODEV;
	}

	chip->ops = ucs_protocol_ops;
	strncpy(chip->device_name, name, UFCS_NAME_LEN - 1);

	return rc;
}

static int oplus_ufcs_protocol_driver_probe(struct platform_device *pdev)
{
	struct oplus_ufcs_protocol *chip_ic;
	struct oplus_ufcs_chip *chip_ufcs;
	int rc = 0;

	chip_ic = devm_kzalloc(&pdev->dev, sizeof(struct oplus_ufcs_protocol), GFP_KERNEL);
	if (chip_ic == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip_ic->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip_ic);

	g_protocol = chip_ic;

	chip_ufcs = devm_kzalloc(&pdev->dev, sizeof(struct oplus_ufcs_chip), GFP_KERNEL);
	if (!chip_ufcs) {
		ufcs_err("kzalloc() chip_ufcs failed.\n");
		rc = -ENOMEM;
		goto chip_ufcs_err;
	}
	chip_ufcs->dev = &pdev->dev;

	chip_ic->wq = kthread_create_worker(0, dev_name(&pdev->dev));
	if (IS_ERR(chip_ic->wq)) {
		ufcs_err("create kthread worker failed!\n");
		rc = -ENOMEM;
		goto kthread_worker_err;
	}
	oplus_ufcs_sched_set_fifo(chip_ic);
	mutex_init(&chip_ic->chip_lock);
	init_completion(&chip_ic->rcv_cmp);
	oplus_ufcs_protocol_awake_init(chip_ic);
	kthread_init_work(&chip_ic->rcv_work, oplus_ufcs_protocol_rcv_work);
	INIT_WORK(&chip_ic->reply_work, oplus_ufcs_protocol_reply_adapter);
	INIT_DELAYED_WORK(&(chip_ic->ufcs_service_work), ufcs_service);
	oplus_ufcs_protocol_parse_dt(chip_ic);

	chip_ufcs->ops = &oplus_ufcs_protocol_ops;
	oplus_ufcs_init(chip_ufcs);
	oplus_ufcs_protocol_track_init(chip_ic);

	ufcs_debug("call end!\n");

	return 0;

kthread_worker_err:
	devm_kfree(&pdev->dev, chip_ufcs);
chip_ufcs_err:
	devm_kfree(&pdev->dev, chip_ic);

	return rc;
}

static int oplus_ufcs_protocol_driver_remove(struct platform_device *pdev)
{
	struct oplus_ufcs_protocol *chip = platform_get_drvdata(pdev);

	if (chip == NULL)
		return -ENODEV;

	kthread_destroy_worker(chip->wq);

	return 0;
}

static const struct of_device_id oplus_ufcs_protocol_match[] = {
	{ .compatible = "oplus,ufcs-protocol" },
	{},
};

static struct platform_driver oplus_ufcs_protocol_driver = {
	.driver =
		{
			.name = "ufcs-protocol",
			.owner = THIS_MODULE,
			.of_match_table = oplus_ufcs_protocol_match,
		},
	.probe = oplus_ufcs_protocol_driver_probe,
	.remove = oplus_ufcs_protocol_driver_remove,
};
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init oplus_ufcs_protocol_driver_init(void)
{
	int ret = 0;
	ufcs_err(" init start\n");

	if (platform_driver_register(&oplus_ufcs_protocol_driver) != 0) {
		ufcs_err(" failed to register sc8547a i2c driver.\n");
	} else {
		ufcs_err(" Success to register sc8547a i2c driver.\n");
	}

	return ret;
}

subsys_initcall(oplus_ufcs_protocol_driver_init);
#else
static __init int oplus_ufcs_protocol_driver_init(void)
{
	return platform_driver_register(&oplus_ufcs_protocol_driver);
}

static __exit void oplus_ufcs_protocol_driver_exit(void)
{
	platform_driver_unregister(&oplus_ufcs_protocol_driver);
}
oplus_chg_module_register(oplus_ufcs_protocol_driver);

#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
