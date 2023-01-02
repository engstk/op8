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

#include "ft3518_core.h"

struct fts_ts_data *fts_data = NULL;
extern int tp_register_times;

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "focaltech,fts"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
        do {\
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
        } while(0)


#define FTS_REG_UPGRADE                             0xFC
#define FTS_UPGRADE_AA                              0xAA
#define FTS_UPGRADE_55                              0x55
#define FTS_DELAY_UPGRADE_AA                        10
#define FTS_DELAY_UPGRADE_RESET                     80
#define FTS_UPGRADE_LOOP                            10

#define FTS_CMD_RESET                               0x07
#define FTS_CMD_START                               0x55
#define FTS_CMD_START_DELAY                         12
#define FTS_CMD_READ_ID                             0x90
#define FTS_CMD_DATA_LEN                            0xB0
#define FTS_CMD_ERASE_APP                           0x61
#define FTS_RETRIES_REASE                           50
#define FTS_RETRIES_DELAY_REASE                     400
#define FTS_REASE_APP_DELAY                         1350
#define FTS_CMD_ECC_INIT                            0x64
#define FTS_CMD_ECC_CAL                             0x65
#define FTS_RETRIES_ECC_CAL                         10
#define FTS_RETRIES_DELAY_ECC_CAL                   50
#define FTS_CMD_ECC_READ                            0x66
#define FTS_CMD_FLASH_STATUS                        0x6A
#define FTS_CMD_WRITE                               0xBF
#define FTS_RETRIES_WRITE                           100
#define FTS_RETRIES_DELAY_WRITE                     1

#define FTS_CMD_FLASH_STATUS_NOP                    0x0000
#define FTS_CMD_FLASH_STATUS_ECC_OK                 0xF055
#define FTS_CMD_FLASH_STATUS_ERASE_OK               0xF0AA
#define FTS_CMD_FLASH_STATUS_WRITE_OK               0x1000


enum GESTURE_ID {
    GESTURE_RIGHT2LEFT_SWIP = 0x20,
    GESTURE_LEFT2RIGHT_SWIP = 0x21,
    GESTURE_DOWN2UP_SWIP = 0x22,
    GESTURE_UP2DOWN_SWIP = 0x23,
    GESTURE_DOUBLE_TAP = 0x24,
    GESTURE_DOUBLE_SWIP = 0x25,
    GESTURE_RIGHT_VEE = 0x51,
    GESTURE_LEFT_VEE = 0x52,
    GESTURE_DOWN_VEE = 0x53,
    GESTURE_UP_VEE = 0x54,
    GESTURE_O_CLOCKWISE = 0x57,
    GESTURE_O_ANTICLOCK = 0x30,
    GESTURE_W = 0x31,
    GESTURE_M = 0x32,
    GESTURE_FINGER_PRINT = 0x26,
    GESTURE_SINGLE_TAP = 0x27,
    GESTURE_HEART_ANTICLOCK = 0x55,
    GESTURE_HEART_CLOCKWISE = 0x59,
};

/*******Pinctrl config for egpio active and suspend*******/

int fts_pinctrl_select_active(struct fts_ts_data *fts_data)
{
	int ret = 0;

	if (fts_data->hw_res->pinctrl && fts_data->hw_res->pin_set_reset_high) {
		ret = pinctrl_select_state(fts_data->hw_res->pinctrl, fts_data->hw_res->pin_set_reset_high);
		TPD_INFO("%s : ret = %d\n", __func__, ret);
		if (ret < 0) {
			TPD_INFO("Set active pin state error:%d", ret);
			devm_pinctrl_put(fts_data->hw_res->pinctrl);
			fts_data->hw_res->pinctrl = NULL;
		}
	}

	return ret;
}

int fts_pinctrl_select_suspend(struct fts_ts_data *fts_data)
{
	int ret = 0;

	if (fts_data->hw_res->pinctrl && fts_data->hw_res->pin_set_reset_low) {
		ret = pinctrl_select_state(fts_data->hw_res->pinctrl , fts_data->hw_res->pin_set_reset_low);
		TPD_INFO("%s : ret = %d\n", __func__, ret);
		if (ret < 0) {
			TPD_INFO("Set suspend pin state error:%d", ret);
			devm_pinctrl_put(fts_data->hw_res->pinctrl);
			fts_data->hw_res->pinctrl = NULL;
		}
	}

	return ret;
}

/*******Part1:Call Back Function implement*******/
static void fts_read_fod_info(struct fts_ts_data *ts_data);
static int fts_get_gesture_info(void *chip_data, struct gesture_info *gesture);



static int fts_rstgpio_set(struct hw_resource *hw_res, bool on)
{
	if (fts_data->need_pinctrl_pull_up_reset) {
		if (on == true) {
			fts_pinctrl_select_active(fts_data);
		} else {
			fts_pinctrl_select_suspend(fts_data);
		}
	} else {
		if (gpio_is_valid(hw_res->reset_gpio)) {
			TPD_INFO("Set the reset_gpio \n");
			gpio_direction_output(hw_res->reset_gpio, on);
		} else {
			TPD_INFO("reset is invalid!!\n");
		}
	}

    return 0;
}

/*
 * return success: 0; fail : negative
 */
static int fts_hw_reset(struct fts_ts_data *ts_data, u32 delayms)
{
    TPD_INFO("%s.\n", __func__);
    fts_rstgpio_set(ts_data->hw_res, false); /* reset gpio*/
    msleep(5);
    fts_rstgpio_set(ts_data->hw_res, true); /* reset gpio*/
    if (delayms) {
        msleep(delayms);
    }

    return 0;
}

static int focal_dump_reg_state(void *chip_data, char *buf)
{
    int count = 0;
    u8 regvalue = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    //power mode 0:active 1:monitor 3:sleep
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_POWER_MODE);
    count += sprintf(buf + count, "Power Mode:0x%02x\n", regvalue);

    //FW version
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_FW_VER);
    count += sprintf(buf + count, "FW Ver:0x%02x\n", regvalue);

    //Vendor ID
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_VENDOR_ID);
    count += sprintf(buf + count, "Vendor ID:0x%02x\n", regvalue);

    // 1 Gesture mode,0 Normal mode
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_GESTURE_EN);
    count += sprintf(buf + count, "Gesture Mode:0x%02x\n", regvalue);

    // 3 charge in
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_CHARGER_MODE_EN);
    count += sprintf(buf + count, "charge stat:0x%02x\n", regvalue);

    //Interrupt counter
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_INT_CNT);
    count += sprintf(buf + count, "INT count:0x%02x\n", regvalue);

    //Flow work counter
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_FLOW_WORK_CNT);
    count += sprintf(buf + count, "ESD count:0x%02x\n", regvalue);

    return count;
}

static int focal_get_fw_version(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return touch_i2c_read_byte(ts_data->client, FTS_REG_FW_VER);
}

static void focal_esd_check_enable(void *chip_data, bool enable)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    ts_data->esd_check_enabled = enable;
    //ts_data->esd_check_need_stop = !enable;
}

static bool focal_get_esd_check_flag(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return ts_data->esd_check_need_stop;
}

static int fts_esd_handle(void *chip_data)
{
    int ret = -1;
    int i = 0;
    static int flow_work_cnt_last = 0;
    static int err_cnt = 0;
    static int i2c_err = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    //if (ts_data->esd_check_need_stop) {
    if (!ts_data->esd_check_enabled) {
        goto NORMAL_END;
    }

    ret = touch_i2c_read_byte(ts_data->client, 0x00);
    if ((ret & 0x70) == 0x40) { //work in factory mode
        goto NORMAL_END;
    }

    for (i = 0; i < 3; i++) {
        ret = touch_i2c_read_byte(ts_data->client, FTS_REG_CHIP_ID);
        if (ret != 0x54) {
            TPD_INFO("%s: read chip_id failed!(ret:%d)\n", __func__, ret);
            msleep(10);
            i2c_err++;
        } else {
            i2c_err = 0;
            break;
        }
    }
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_FLOW_WORK_CNT);
    if (ret < 0) {
        TPD_INFO("%s: read FTS_REG_FLOW_WORK_CNT failed!\n", __func__);
        i2c_err++;
    }

    if (flow_work_cnt_last == ret) {
        err_cnt++;
    } else {
        err_cnt = 0;
    }
    flow_work_cnt_last = ret;

    if ((err_cnt >= 5) || (i2c_err >= 3)) {
        TPD_INFO("esd check failed, start reset!\n");
        disable_irq_nosync(ts_data->client->irq);
        tp_touch_btnkey_release();
        fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
        enable_irq(ts_data->client->irq);
        flow_work_cnt_last = 0;
        err_cnt = 0;
        i2c_err = 0;
    }

NORMAL_END:
    return 0;
}


static bool fts_fwupg_check_flash_status(struct fts_ts_data *ts_data, u16 flash_status, int retries, int retries_delay)
{
    int ret = 0;
    int i = 0;
    u8 cmd = 0;
    u8 val[2] = { 0 };
    u16 read_status = 0;

    for (i = 0; i < retries; i++) {
        cmd = FTS_CMD_FLASH_STATUS;
        ret = touch_i2c_read_block(ts_data->client, cmd, 2, val);
        read_status = (((u16)val[0]) << 8) + val[1];
        if (flash_status == read_status) {
            return true;
        }
        TPD_DEBUG("flash status fail,ok:%04x read:%04x, retries:%d", flash_status, read_status, i);
        msleep(retries_delay);
    }

    return false;
}

static int fts_fwupg_enter_into_boot(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int i = 0;
    u8 cmd = 0;
    u8 id[2] = { 0 };

    do {
        /*reset to boot*/
        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_UPGRADE, FTS_UPGRADE_AA);
        if (ret < 0) {
            TPD_INFO("write FC=0xAA fail");
            return ret;
        }
        msleep(FTS_DELAY_UPGRADE_AA);

        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_UPGRADE, FTS_UPGRADE_55);
        if (ret < 0) {
            TPD_INFO("write FC=0x55 fail");
            return ret;
        }
        msleep(FTS_DELAY_UPGRADE_RESET);

        /*read boot id*/
        cmd = FTS_CMD_START;
        ret = touch_i2c_write_block(ts_data->client, cmd, 0, NULL);
        if (ret < 0) {
            TPD_INFO("write 0x55 fail");
            return ret;
        }

        cmd = FTS_CMD_READ_ID;
        ret = touch_i2c_read_block(ts_data->client, cmd, 2, id);
        if (ret < 0) {
            TPD_INFO("read boot id fail");
            return ret;
        }

        TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);
        if ((id[0] == FTS_VAL_BL_ID) && (id[1] == FTS_VAL_BL_ID2)) {
            break;
        }
    } while (i++ < FTS_UPGRADE_LOOP);

    return 0;
}

static int fts_fwupg_erase(struct fts_ts_data *ts_data, u32 delay)
{
    int ret = 0;
    u8 cmd = 0;
    bool flag = false;

    TPD_INFO("**********erase now**********");

    /*send to erase flash*/
    cmd = FTS_CMD_ERASE_APP;
    ret = touch_i2c_write_block(ts_data->client, cmd, 0, NULL);
    if (ret < 0) {
        TPD_INFO("send erase cmd fail");
        return ret;
    }
    msleep(delay);

    /* read status 0xF0AA: success */
    flag = fts_fwupg_check_flash_status(ts_data, FTS_CMD_FLASH_STATUS_ERASE_OK,
                                        FTS_RETRIES_REASE, FTS_RETRIES_DELAY_REASE);
    if (!flag) {
        TPD_INFO("check ecc flash status fail");
        return -EIO;
    }

    return 0;
}

static int fts_flash_write_buf(struct fts_ts_data *ts_data, u32 saddr, u8 *buf, u32 len, u32 delay)
{
    int ret = 0;
    u32 i = 0;
    u32 j = 0;
    u32 packet_number = 0;
    u32 packet_len = 0;
    u32 addr = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u32 cmdlen = 0;
    u8 packet_buf[BYTES_PER_TIME + 6] = { 0 };
    u8 cmd = 0;
    u8 val[2] = { 0 };
    u16 read_status = 0;
    u16 wr_ok = 0;

    TPD_INFO( "**********write data to flash**********");
    TPD_INFO("data buf start addr=0x%x, len=0x%x", saddr, len);
    packet_number = len / BYTES_PER_TIME;
    remainder = len % BYTES_PER_TIME;
    if (remainder > 0)
        packet_number++;
    packet_len = BYTES_PER_TIME;
    TPD_INFO("write data, num:%d remainder:%d", packet_number, remainder);

    for (i = 0; i < packet_number; i++) {
        offset = i * BYTES_PER_TIME;
        addr = saddr + offset;
        cmdlen = 6;
        packet_buf[0] = FTS_CMD_WRITE;
        packet_buf[1] = (addr >> 16) & 0xFF;
        packet_buf[2] = (addr >> 8) & 0xFF;
        packet_buf[3] = (addr) & 0xFF;
        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;
        packet_buf[4] = (packet_len >> 8) & 0xFF;
        packet_buf[5] = (packet_len) & 0xFF;
        memcpy(&packet_buf[cmdlen], &buf[offset], packet_len);
        ret = touch_i2c_write_block(ts_data->client, packet_buf[0], packet_len + cmdlen - 1, &packet_buf[1]);
        if (ret < 0) {
            TPD_INFO("app write fail");
            return ret;
        }
        mdelay(delay);

        /* read status */
        wr_ok = FTS_CMD_FLASH_STATUS_WRITE_OK + addr / packet_len;
        for (j = 0; j < FTS_RETRIES_WRITE; j++) {
            cmd = FTS_CMD_FLASH_STATUS;
            ret = touch_i2c_read_block(ts_data->client, cmd, 2, val);
            read_status = (((u16)val[0]) << 8) + val[1];
            /* TPD_DEBUG("%x %x", wr_ok, read_status); */
            if (wr_ok == read_status) {
                break;
            }
            mdelay(FTS_RETRIES_DELAY_WRITE);
        }
    }

    return 0;
}

static int fts_fwupg_ecc_cal_host(u8 *buf, u32 len)
{
    int i = 0;
    u8 ecc = 0;

    for (i = 0; i < len; i++) {
        ecc ^= buf[i];
    }

    return (int)ecc;
}

int fts_fwupg_ecc_cal_tp(struct fts_ts_data *ts_data, u32 saddr, u32 len)
{
    int ret = 0;
    u8 wbuf[7] = { 0 };
    u8 ecc = 0;
    bool bflag = false;

    TPD_INFO( "**********read out checksum**********");
    /* check sum init */
    wbuf[0] = FTS_CMD_ECC_INIT;
    ret = touch_i2c_write_block(ts_data->client, wbuf[0] & 0xff, 0, NULL);
    if (ret < 0) {
        TPD_INFO("ecc init cmd write fail");
        return ret;
    }

    /* send commond to start checksum */
    wbuf[0] = FTS_CMD_ECC_CAL;
    wbuf[1] = (saddr >> 16) & 0xFF;
    wbuf[2] = (saddr >> 8) & 0xFF;
    wbuf[3] = (saddr);
    wbuf[4] = (len >> 16) & 0xFF;
    wbuf[5] = (len >> 8) & 0xFF;
    wbuf[6] = (len);
    TPD_DEBUG("ecc calc startaddr:0x%04x, len:%d", saddr, len);
    ret = touch_i2c_write_block(ts_data->client, wbuf[0] & 0xff, 6, &wbuf[1]);
    if (ret < 0) {
        TPD_INFO("ecc calc cmd write fail");
        return ret;
    }
    msleep(len / 256);

    /* read status if check sum is finished */
    bflag = fts_fwupg_check_flash_status(ts_data, FTS_CMD_FLASH_STATUS_ECC_OK,
                                         FTS_RETRIES_ECC_CAL,
                                         FTS_RETRIES_DELAY_ECC_CAL);
    if (!bflag) {
        TPD_INFO("ecc flash status read fail");
        return -EIO;
    }

    /* read out check sum */
    wbuf[0] = FTS_CMD_ECC_READ;
    ret = touch_i2c_read_block(ts_data->client, wbuf[0], 1, &ecc);
    if (ret < 0) {
        TPD_INFO( "ecc read cmd write fail");
        return ret;
    }

    return (int)ecc;
}

static int fts_upgrade(struct fts_ts_data *ts_data, u8 *buf, u32 len)
{
    struct monitor_data_v2 *monitor_data = ts_data->monitor_data_v2;
    int ret = 0;
    u32 start_addr = 0;
    u8 cmd[4] = { 0 };
    int ecc_in_host = 0;
    int ecc_in_tp = 0;

    if (!buf) {
        TPD_INFO("fw_buf is invalid");
        return -EINVAL;
    }

    /* enter into upgrade environment */
    ret = fts_fwupg_enter_into_boot(ts_data);
    if (ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "Enter pramboot/bootloader failed");
        TPD_INFO("enter into pramboot/bootloader fail,ret=%d", ret);
        goto fw_reset;
    }

    cmd[0] = FTS_CMD_DATA_LEN;
    cmd[1] = (len >> 16) & 0xFF;
    cmd[2] = (len >> 8) & 0xFF;
    cmd[3] = (len) & 0xFF;
    ret = touch_i2c_write_block(ts_data->client, cmd[0], 3, &cmd[1]);
    if (ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FTS_CMD_DATA_LEN failed");
        TPD_INFO("data len cmd write fail");
        goto fw_reset;
    }

    /*erase*/
    ret = fts_fwupg_erase(ts_data, FTS_REASE_APP_DELAY);
    if (ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FTS_REASE_APP_DELAY failed");
        TPD_INFO("erase cmd write fail");
        goto fw_reset;
    }

    /* write app */
    start_addr = 0;
    ret = fts_flash_write_buf(ts_data, start_addr, buf, len, 1);
    if (ret < 0 ) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "Flash Write failed");
        TPD_INFO("flash write fail");
        goto fw_reset;
    }

    ecc_in_host = fts_fwupg_ecc_cal_host(buf, len);
    ecc_in_tp = fts_fwupg_ecc_cal_tp(ts_data, start_addr, len);
    if (ecc_in_tp < 0 ) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "ECC Read failed");
        TPD_INFO("ecc read fail");
        goto fw_reset;
    }

    TPD_INFO("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
    if (ecc_in_tp != ecc_in_host) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "ECC Check failed");
        TPD_INFO("ecc check fail");
        goto fw_reset;
    }

    TPD_INFO("upgrade success, reset to normal boot");
    cmd[0] = FTS_CMD_RESET;
    ret = touch_i2c_write_block(ts_data->client, cmd[0], 0, NULL);
    if (ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FTS_CMD_RESET failed");
        TPD_INFO("reset to normal boot fail");
    }

    msleep(200);
    return 0;

fw_reset:
    TPD_INFO("upgrade fail, reset to normal boot");
    cmd[0] = FTS_CMD_RESET;
    ret = touch_i2c_write_block(ts_data->client, cmd[0], 0, NULL);
    if (ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FTS_CMD_RESET failed");
        TPD_INFO("reset to normal boot fail");
    }
    return -EIO;
}

void fts_auto_test(struct seq_file *s, void *chip_data, struct focal_testdata *focal_testdata)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;

    ts_data->s = s;
    ts_data->csv_fd = focal_testdata->fd;

    focal_esd_check_enable(ts_data, false);
    ret = fts_test_entry(ts_data);
    tp_healthinfo_report(ts_data->monitor_data_v2, HEALTH_TEST_AUTO, &ret);
    focal_esd_check_enable(ts_data, true);
}

static int fts_enter_factory_work_mode(struct fts_ts_data *ts_data, u8 mode_val)
{
    int ret = 0;
    int retry = 20;
    u8 regval = 0;

    TPD_INFO("%s:enter %s mode", __func__, (mode_val == 0x40) ? "factory" : "work");
    ret = touch_i2c_write_byte(ts_data->client, DEVIDE_MODE_ADDR, mode_val);
    if (ret < 0) {
        TPD_INFO("%s:write mode(val:0x%x) fail", __func__, mode_val);
        return ret;
    }

    while (--retry) {
        regval = touch_i2c_read_byte(ts_data->client, DEVIDE_MODE_ADDR);
        if (regval == mode_val)
            break;
        msleep(20);
    }

    if (!retry) {
        TPD_INFO("%s:enter mode(val:0x%x) timeout", __func__, mode_val);
        return -EIO;
    }

    msleep(FACTORY_TEST_DELAY);
    return 0;
}

static int fts_start_scan(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int retry = 50;
    u8 regval = 0;
    u8 scanval = FTS_FACTORY_MODE_VALUE | (1 << 7);

    TPD_INFO("%s: start to scan a frame", __func__);
    ret = touch_i2c_write_byte(ts_data->client, DEVIDE_MODE_ADDR, scanval);
    if (ret < 0) {
        TPD_INFO("%s:start to scan a frame fail", __func__);
        return ret;
    }

    while (--retry) {
        regval = touch_i2c_read_byte(ts_data->client, DEVIDE_MODE_ADDR);
        if (regval == FTS_FACTORY_MODE_VALUE)
            break;
        msleep(20);
    }

    if (!retry) {
        TPD_INFO("%s:scan a frame timeout", __func__);
        return -EIO;
    }

    return 0;
}

#define FTS_MUTUAL_CAP_DATA_DELTA  0xAA  /*mutual delta*/
#define FTS_SELF_CAP_DATA_DELTA    0xAB  /*self delta normal*/
#define FTS_SELF_CAP_DATA_DELTA_WP 0xAC  /*self delta waterproof*/
#define FTS_CAP_DATA_TYPE_RAW   0x00
#define FTS_CAP_DATA_TYPE_DELTA 0x01
static int fts_get_rawdata(struct fts_ts_data *ts_data, int *raw, bool is_diff, int data_type)
{
    int ret = 0;
    int i = 0;
    int byte_num = 0;
    int size = 0;
    int packet_len = 0;
    int offset = 0;
    u8 raw_addr = 0;
    u8 regval = 0;
    u8 *buf = NULL;

    TPD_INFO("%s:call", __func__);
    if (data_type == FTS_MUTUAL_CAP_DATA_DELTA) {
        byte_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM * 2;
    } else {
        byte_num = (ts_data->hw_res->TX_NUM + ts_data->hw_res->RX_NUM) * 2;
	if (byte_num < MAX_PACKET_SIZE) {
		byte_num = MAX_PACKET_SIZE;
	}
    }

    /*kzalloc buffer*/
    buf = kzalloc(byte_num, GFP_KERNEL);
    if (!buf) {
        TPD_INFO("%s:kzalloc for raw byte buf fail", __func__);
        return -ENOMEM;
    }

    ret = fts_enter_factory_work_mode(ts_data, FTS_FACTORY_MODE_VALUE);
    if (ret < 0) {
        TPD_INFO("%s:enter factory mode fail", __func__);
        goto raw_err;
    }

    if (is_diff) {
        regval = touch_i2c_read_byte(ts_data->client, FACTORY_REG_DATA_SELECT);
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_DATA_SELECT, FTS_CAP_DATA_TYPE_DELTA);
        if (ret < 0) {
            TPD_INFO("%s:write 0x01 to reg0x06 fail", __func__);
            goto reg_restore;
        }
    } else if (!is_diff) {
        regval = touch_i2c_read_byte(ts_data->client, FACTORY_REG_DATA_SELECT);
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_DATA_SELECT, FTS_CAP_DATA_TYPE_RAW);
        if (ret < 0) {
            TPD_INFO("%s:write 0x01 to reg0x06 fail", __func__);
            goto reg_restore;
        }

        ret = fts_start_scan(ts_data);
        if (ret < 0) {
            TPD_INFO("%s:scan a frame fail", __func__);
            goto reg_restore;
        }
    }

    if (data_type == FTS_MUTUAL_CAP_DATA_DELTA) {
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_LINE_ADDR, FTS_MUTUAL_CAP_DATA_DELTA);
    } else if (data_type == FTS_SELF_CAP_DATA_DELTA) {
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_LINE_ADDR, FTS_SELF_CAP_DATA_DELTA);
    } else if (data_type == FTS_SELF_CAP_DATA_DELTA_WP) {
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_LINE_ADDR, FTS_SELF_CAP_DATA_DELTA_WP);
    }
    if (ret < 0) {
        TPD_INFO("%s:write [data_type] to reg0x01 fail", __func__);
        goto reg_restore;
    }

    raw_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
    ret = touch_i2c_read_block(ts_data->client, raw_addr, MAX_PACKET_SIZE, buf);
    size = byte_num - MAX_PACKET_SIZE;
    offset = MAX_PACKET_SIZE;
    while (size > 0) {
        if (size >= MAX_PACKET_SIZE)
            packet_len = MAX_PACKET_SIZE;
        else
            packet_len = size;

        ret = touch_i2c_read(ts_data->client, NULL, 0, buf + offset, packet_len);
        if (ret < 0) {
            TPD_INFO("%s:read raw data(packet:%d) fail", __func__, offset / MAX_PACKET_SIZE);
            goto reg_restore;
        }

        size -= packet_len;
        offset += packet_len;
    }

    for (i = 0; i < byte_num; i = i + 2) {
        raw[i >> 1] = (int)(short)((buf[i] << 8) + buf[i + 1]);
    }

reg_restore:
    if (is_diff) {
        ret = touch_i2c_write_byte(ts_data->client, FACTORY_REG_DATA_SELECT, regval);
        if (ret < 0) {
            TPD_INFO("%s:restore reg0x06 fail", __func__);
        }
    }

raw_err:
    kfree(buf);
    ret = fts_enter_factory_work_mode(ts_data, FTS_WORK_MODE_VALUE);
    if (ret < 0) {
        TPD_INFO("%s:enter work mode fail", __func__);
    }

    return ret;
}

static void fts_delta_read(struct seq_file *s, void *chip_data)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int *raw = NULL;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
	unsigned char pTmp[40] = {0};
	unsigned char *Pstr = NULL;
	int lsize = tx_num * rx_num;

    TPD_INFO("%s:start to read diff data", __func__);
    focal_esd_check_enable(ts_data, false);   //no allowed esd check

    raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);
    if (!raw) {
		if (s) {
			seq_printf(s, "kzalloc for raw fail\n");
		}
		else {
			TPD_INFO("kzalloc for raw fail\n");
		}
        goto raw_fail;
    }
	if(s == NULL) {
		TPD_INFO("start to read JCQ data: \n");
		TPD_INFO("0xD0 = %d: \n", touch_i2c_read_byte(ts_data->client, 0xD0));
		TPD_INFO("0x86 = %d: \n", touch_i2c_read_byte(ts_data->client, 0x86));
		TPD_INFO("0xCF = %d: \n", touch_i2c_read_byte(ts_data->client, 0xCF));
		fts_read_fod_info(ts_data);
		fts_get_gesture_info(ts_data, NULL);
		TPD_INFO("0x8F = %d: \n", touch_i2c_read_byte(ts_data->client, 0x8F));
		TPD_INFO("0x91 = %d: \n", touch_i2c_read_byte(ts_data->client, 0x91));
	}


    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_AUTOCLB_ADDR, 0x01);
    if (ret < 0) {
        TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
    }

    ret = fts_get_rawdata(ts_data, raw, true, FTS_MUTUAL_CAP_DATA_DELTA);
    if (ret < 0) {
		if (s) {
			seq_printf(s, "get diff data fail\n");
		}
		else {
			TPD_INFO("get diff data fail\n");
		}
        goto raw_fail;
    }
    
	TPD_INFO("now is reading delta\n");
	if(s == NULL) {
		Pstr = kzalloc(lsize * (sizeof(int)), GFP_KERNEL);
	}
    for (i = 0; i < tx_num; i++) {
		if (s) {
			seq_printf(s, "\n[%2d]", i + 1);
		}
		else {
			memset(Pstr, 0x0, lsize);
			snprintf(pTmp, sizeof(pTmp), "[%2d]", i+1);
			strncat(Pstr, pTmp, lsize);
		}
        for (j = 0; j < rx_num; j++) {
			if (s) {
				seq_printf(s, " %5d,", raw[i * rx_num + j]);
			}
			else {
				snprintf(pTmp, sizeof(pTmp), " %5d", raw[i * rx_num + j]);
				strncat(Pstr, pTmp, lsize);
			}
		}
		if(s == NULL) {
			TPD_INFO("%s\n", Pstr);
        }
    }
	if (s) {
		seq_printf(s, "\n");
	}
	else {
		TPD_INFO("0x91 = %d: \n", touch_i2c_read_byte(ts_data->client, 0x91));
		kfree(Pstr);
	}

raw_fail:
    focal_esd_check_enable(ts_data, true);
    kfree(raw);
}

#define DATA_LEN_EACH_RAW 17
static void fts_self_delta_read(struct seq_file *s, void *chip_data)
{
    int ret = 0;
    int i = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int *raw = NULL;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;

    TPD_INFO("%s:start to read self-cap diff data", __func__);
    focal_esd_check_enable(ts_data, false);   //no allowed esd check

    raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);
    if (!raw) {
        seq_printf(s, "kzalloc for raw fail\n");
        goto raw_fail;
    }

    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_AUTOCLB_ADDR, 0x01);
    if (ret < 0) {
        TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
    }

    ret = fts_get_rawdata(ts_data, raw, true, FTS_SELF_CAP_DATA_DELTA_WP);
    if (ret < 0) {
        seq_printf(s, "get self delta data fail\n");
        goto raw_fail;
    }

    seq_printf(s, "self data delta \n");
    for (i = 0; i < tx_num + rx_num; i++) {
        if (i % DATA_LEN_EACH_RAW == 0)
            seq_printf(s, "\n");
        seq_printf(s, " %5d",  raw[i]);
    }
    seq_printf(s, "\n");

    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_AUTOCLB_ADDR, 0x01);
    if (ret < 0) {
        TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
    }

    ret = fts_get_rawdata(ts_data, raw, true, FTS_SELF_CAP_DATA_DELTA_WP);
    if (ret < 0) {
        seq_printf(s, "get self delta data fail\n");
        goto raw_fail;
    }

    seq_printf(s, "self data delta waterproof\n");
    for (i = 0; i < tx_num + rx_num; i++) {
        if (i % DATA_LEN_EACH_RAW == 0)
            seq_printf(s, "\n");
        seq_printf(s, " %5d",  raw[i]);
    }
    seq_printf(s, "\n");

raw_fail:
    focal_esd_check_enable(ts_data, true);
    kfree(raw);
}

static void fts_baseline_read(struct seq_file *s, void *chip_data)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int *raw = NULL;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;

    TPD_INFO("%s:start to read raw data", __func__);
    focal_esd_check_enable(ts_data, false);

    raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);
    if (!raw) {
        seq_printf(s, "kzalloc for raw fail\n");
        goto raw_fail;
    }

    ret = fts_get_rawdata(ts_data, raw, false, FTS_MUTUAL_CAP_DATA_DELTA);
    if (ret < 0) {
        seq_printf(s, "get raw data fail\n");
        goto raw_fail;
    }

    for (i = 0; i < tx_num; i++) {
        seq_printf(s, "\n[%2d]", i + 1);
        for (j = 0; j < rx_num; j++) {
            seq_printf(s, " %5d,", raw[i * rx_num + j]);
        }
    }
    seq_printf(s, "\n");

raw_fail:
    focal_esd_check_enable(ts_data, true);
    kfree(raw);
}

static void fts_main_register_read(struct seq_file *s, void *chip_data)
{
    u8 regvalue = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    /*TP FW version*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_FW_VER);
    seq_printf(s, "TP FW Ver:0x%02x\n", regvalue);

    /*Vendor ID*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_VENDOR_ID);
    seq_printf(s, "Vendor ID:0x%02x\n", regvalue);

    /*Gesture enable*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_GESTURE_EN);
    seq_printf(s, "Gesture Mode:0x%02x\n", regvalue);

    /*charge in*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_CHARGER_MODE_EN);
    seq_printf(s, "charge state:0x%02x\n", regvalue);

    /*edge limit*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_EDGE_LIMIT);
    seq_printf(s, "edge Mode:0x%02x\n", regvalue);

    /*game mode*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_GAME_MODE_EN);
    seq_printf(s, "Game Mode:0x%02x\n", regvalue);

    /*FOD mode*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_FOD_EN);
    seq_printf(s, "FOD Mode:0x%02x\n", regvalue);

    /*Interrupt counter*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_INT_CNT);
    seq_printf(s, "INT count:0x%02x\n", regvalue);

    /*Flow work counter*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_FLOW_WORK_CNT);
    seq_printf(s, "ESD count:0x%02x\n", regvalue);

    /*Panel ID*/
    regvalue = touch_i2c_read_byte(ts_data->client, FTS_REG_MODULE_ID);
    seq_printf(s, "PANEL ID:0x%02x\n", regvalue);

    return;
}

#define SET_FTS_GESTURE(state, state_flag, config, config_flag)\
    if (CHK_BIT(state, (1 << state_flag))) {\
        SET_BIT(config, (1 << config_flag));\
    } else {\
        CLR_BIT(config, (1 << config_flag));\
    }

static int fts_enable_black_gesture(struct fts_ts_data *ts_data, bool enable)
{
    int i = 0;
    int ret = 0;
    int state = ts_data->gesture_state;

    int config1 = 0xff;
    int config2 = 0xff;
    int config4 = 0xff;

    if (ts_data->black_gesture_indep) {
        if (enable) {
            SET_FTS_GESTURE(state, Right2LeftSwip, config1, 0)
            SET_FTS_GESTURE(state, Left2RightSwip, config1, 1)
            SET_FTS_GESTURE(state, Down2UpSwip, config1, 2)
            SET_FTS_GESTURE(state, Up2DownSwip, config1, 3)
            SET_FTS_GESTURE(state, DouTap, config1, 4)
            SET_FTS_GESTURE(state, DouSwip, config1, 5)
            SET_FTS_GESTURE(state, Circle, config2, 0)
            SET_FTS_GESTURE(state, Wgestrue, config2, 1)
            SET_FTS_GESTURE(state, Mgestrue, config2, 2)
            SET_FTS_GESTURE(state, RightVee, config4, 1)
            SET_FTS_GESTURE(state, LeftVee, config4, 2)
            SET_FTS_GESTURE(state, DownVee, config4, 3)
            SET_FTS_GESTURE(state, UpVee, config4, 4)
        } else {
            config1 = 0;
            config2 = 0;
            config4 = 0;
        }
    }
    TPD_INFO("MODE_GESTURE, write 0xD0=%d", enable);
    TPD_INFO("MODE_GESTURE, config1=%x", config1);
    TPD_INFO("MODE_GESTURE, config2=%x", config2);
    TPD_INFO("MODE_GESTURE, config4=%x", config4);
    if (enable) {
        for (i = 0; i < 5 ; i++) {
            ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG1, config1);
            ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG2, config2);
            ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG4, config4);
            ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_EN, enable);
            msleep(1);
            ret = touch_i2c_read_byte(ts_data->client, FTS_REG_GESTURE_EN);
            if (1 == ret)
                break;
        }
    } else {
        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_EN, enable);
    }
    if (i >= 5)
        TPD_INFO("MODE_GESTURE, write 0x%x[%d] failed \n", FTS_REG_GESTURE_EN, enable);
    return ret;
}

static int fts_enable_edge_limit(struct fts_ts_data *ts_data, bool enable)
{
    u8 edge_mode = 0;

    /*0:Horizontal, 1:Vertical*/
    if ((enable == 1) || (VERTICAL_SCREEN == ts_data->touch_direction)) {
        edge_mode = 0;
    } else if (enable == 0) {
        if (LANDSCAPE_SCREEN_90 == ts_data->touch_direction)
            edge_mode = 1;
        else if (LANDSCAPE_SCREEN_270 == ts_data->touch_direction)
            edge_mode = 2;
    }
    TPD_INFO("MODE_EDGE, write 0x8C=%d", edge_mode);
    return touch_i2c_write_byte(ts_data->client, FTS_REG_EDGE_LIMIT, edge_mode);
}

static int fts_enable_charge_mode(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_CHARGE, write 0x8B=%d", enable);
    return touch_i2c_write_byte(ts_data->client, FTS_REG_CHARGER_MODE_EN, enable);
}

static int fts_enable_game_mode(struct fts_ts_data *ts_data, bool enable)
{
    int ret = 0;

    /*TODO, based on test result*/
    TPD_INFO("MODE_GAME, write 0x86=%d", enable);
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GAME_MODE_EN, !enable);
    if (ret < 0) {
        TPD_INFO("%s: enable MODE_GAME failed.\n", __func__);
    }
    if (enable) {
        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_REPORT_RATE, FTS_180HZ_REPORT_RATE);
        if (ret < 0) {
            TPD_INFO("%s: enable 180hz failed.\n", __func__);
        }
    } else {
        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_REPORT_RATE, FTS_120HZ_REPORT_RATE);
        if (ret < 0) {
            TPD_INFO("%s: enable 120hz failed.\n", __func__);
        }
    }
    return ret;
}

static int fts_enable_headset_mode(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_HEADSET, write 0xC3=%d \n", enable);
    return touch_i2c_write_byte(ts_data->client, FTS_REG_HEADSET_MODE_EN, enable);
}

static int fts_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;

    switch (mode) {
    case MODE_NORMAL:
        TPD_INFO("MODE_NORMAL");
        break;

    case MODE_SLEEP:
        TPD_INFO("MODE_SLEEP, write 0xA5=3");
        ret = touch_i2c_write_byte(ts_data->client, FTS_REG_POWER_MODE, 0x03);
        if (ret < 0) {
            TPD_INFO("%s: enter into sleep failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GESTURE:
        TPD_INFO("MODE_GESTURE, ts->is_suspended = %d \n", ts_data->ts->is_suspended);
        if (ts_data->ts->is_suspended) {                             // do not pull up reset when doing resume
            if (ts_data->last_mode == MODE_SLEEP) {
                fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
            }
        }
        ret = fts_enable_black_gesture(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable gesture failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GLOVE:
        break;

    case MODE_EDGE:
        ret = fts_enable_edge_limit(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable edg limit failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_FACE_DETECT:
        break;

    case MODE_CHARGE:
        ret = fts_enable_charge_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable charge mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GAME:
        ret = fts_enable_game_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable game mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_HEADSET:
        ret = fts_enable_headset_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable headset mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    default:
        TPD_INFO("%s: Wrong mode.\n", __func__);
        goto mode_err;
    }

    ts_data->last_mode = mode;
    return 0;
mode_err:
    return ret;
}

static int fts_power_control(void *chip_data, bool enable)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    if (true == enable) {
        fts_rstgpio_set(ts_data->hw_res, false);
        msleep(1);
        ret = tp_powercontrol_2v8(ts_data->hw_res, true);
        if (ret) {
            return -1;
        }
        ret = tp_powercontrol_1v8(ts_data->hw_res, true);
        if (ret) {
            return -1;
        }
        msleep(POWEWRUP_TO_RESET_TIME);
        fts_rstgpio_set(ts_data->hw_res, true);
        msleep(RESET_TO_NORMAL_TIME);
    } else {
        fts_rstgpio_set(ts_data->hw_res, false);
        ret = tp_powercontrol_1v8(ts_data->hw_res, false);
        if (ret) {
            return -1;
        }
        ret = tp_powercontrol_2v8(ts_data->hw_res, false);
        if (ret) {
            return -1;
        }
    }

    return ret;
}

/*
 * return success: 0; fail : negative
 */
static int fts_reset(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s:call\n", __func__);
    fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);

    return 0;
}

int fts_rstpin_reset(void *chip_data)
{
	return fts_reset(chip_data);
}

static int  fts_reset_gpio_control(void *chip_data, bool enable)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return fts_rstgpio_set(ts_data->hw_res, enable);
}

static int fts_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    //char manu_temp[MAX_DEVICE_MANU_LENGTH] = FOCAL_PREFIX;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') && \
        (panel_data->fw_name[len - 2] == 'm') && (panel_data->fw_name[len - 1] == 'g')) {
        //panel_data->fw_name[len-3] = 'b';
        //panel_data->fw_name[len-2] = 'i';
        //panel_data->fw_name[len-1] = 'n';
        TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type, panel_data->fw_name);
    }
    //strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
    //strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
    TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type, panel_data->fw_name);

    return 0;
}

static int fts_get_chip_info(void *chip_data)
{
    u8 cmd = 0x90;
    u8 id[2] = { 0 };
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    id[0] = touch_i2c_read_byte(ts_data->client, FTS_REG_CHIP_ID);
    id[1] = touch_i2c_read_byte(ts_data->client, FTS_REG_CHIP_ID2);
    TPD_INFO("read chip id:0x%02x%02x", id[0], id[1]);
    if ((id[0] == FTS_VAL_CHIP_ID) && (id[1] == FTS_VAL_CHIP_ID2)) {
        return 0;
    }

    TPD_INFO("fw is invalid, need read boot id");
    touch_i2c_read_block(ts_data->client, cmd, 2, id);
    TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);
    if ((id[0] == FTS_VAL_BL_ID) && (id[1] == FTS_VAL_BL_ID2)) {
        return 0;
    }

    //    return -EIO;

    return 0;
}

static int fts_ftm_process(void *chip_data)
{
    int ret = 0;

    ret = fts_mode_switch(chip_data, MODE_SLEEP, true);
    if (ret < 0) {
        TPD_INFO("%s:switch mode to MODE_SLEEP fail", __func__);
        return ret;
    }

    return 0;
}

static fw_check_state fts_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    u8 cmd = 0x90;
    u8 id[2] = { 0 };
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    id[0] = touch_i2c_read_byte(ts_data->client, FTS_REG_CHIP_ID);
    id[1] = touch_i2c_read_byte(ts_data->client, FTS_REG_CHIP_ID2);
    if ((id[0] != FTS_VAL_CHIP_ID) || (id[1] != FTS_VAL_CHIP_ID2)) {
        touch_i2c_read_block(ts_data->client, cmd, 2, id);
        TPD_INFO("boot id:0x%02x%02x, fw abnormal", id[0], id[1]);
        return FW_ABNORMAL;
    }

    /*fw check normal need update TP_FW  && device info*/
    panel_data->TP_FW = touch_i2c_read_byte(ts_data->client, FTS_REG_FW_VER);
    ts_data->fwver = panel_data->TP_FW;
    TPD_INFO("FW VER:%d", panel_data->TP_FW);
    if (panel_data->manufacture_info.version) {
        sprintf(dev_version, "%04x", panel_data->TP_FW);
        strlcpy(&(panel_data->manufacture_info.version[7]), dev_version, 5);
    }
    return FW_NORMAL;
}

#define OFFSET_FW_DATA_FW_VER 0x010E
static fw_update_state fts_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 *buf;
    u32 len = 0;

    if (!fw) {
        TPD_INFO("fw is null");
        return FW_UPDATE_ERROR;
    }

    buf = (u8 *)fw->data;
    len = (int)fw->size;
    if ((len < 0x120) || (len > (116 * 1024))) {
        TPD_INFO("fw_len(%d) is invalid", len);
        return FW_UPDATE_ERROR;
    }

    if (force || (buf[OFFSET_FW_DATA_FW_VER] != ts_data->fwver)) {
        TPD_INFO("Need update, force(%d)/fwver:Host(0x%02x),TP(0x%02x)", force, buf[OFFSET_FW_DATA_FW_VER], ts_data->fwver);
        focal_esd_check_enable(ts_data, false);
        ret = fts_upgrade(ts_data, buf, len);
        focal_esd_check_enable(ts_data, true);
        if (ret < 0) {
            TPD_INFO("fw update fail");
            return FW_UPDATE_ERROR;
        }

        return FW_UPDATE_SUCCESS;
    }

    return FW_NO_NEED_UPDATE;
}

static void fts_read_fod_info(struct fts_ts_data *ts_data)
{
    int ret = 0;
    u8 cmd = FTS_REG_FOD_INFO;
    u8 val[FTS_REG_FOD_INFO_LEN] = { 0 };

    ret = touch_i2c_read_block(ts_data->client, cmd, FTS_REG_FOD_INFO_LEN, val);
    if (ret < 0) {
        TPD_INFO("%s:read FOD info fail", __func__);
        return ;
    }

    TPD_DEBUG("%s:FOD info buffer:%x %x %x %x %x %x %x %x %x", __func__, val[0],
              val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8]);
    ts_data->fod_info.fp_id = val[0];
    ts_data->fod_info.event_type = val[1];
    if (val[8] == 0) {
        ts_data->fod_info.fp_down = 1;
    } else if (val[8] == 1) {
        ts_data->fod_info.fp_down = 0;
    }

    ts_data->fod_info.fp_area_rate = val[2];
    ts_data->fod_info.fp_x = (val[4] << 8) + val[5];
    ts_data->fod_info.fp_y = (val[6] << 8) + val[7];
}

static u32 fts_u32_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;
    u8 cmd = FTS_REG_POINTS;
    u32 result_event = 0;
    u8 *buf = ts_data->rbuf;


    memset(buf, 0xFF, FTS_MAX_POINTS_LENGTH);
    if (gesture_enable && is_suspended) {
        ret = touch_i2c_read_byte(ts_data->client, FTS_REG_GESTURE_EN);
        if (ret == 0x01) {
            ret = touch_i2c_read_byte(ts_data->client, FTS_REG_POINTS_LB);
            return IRQ_GESTURE;
        }
    }
    ret = touch_i2c_read_block(ts_data->client, cmd, FTS_POINTS_ONE, &buf[0]);
    if (ret < 0) {
        TPD_INFO("read touch point one fail");
        return IRQ_IGNORE;
    }

    if ((buf[0] == 0xFF) && (buf[1] == 0x00) && (buf[2] == 0xFF)) {
        if (sizeof(ts_data->rbuf) >= FTS_POINTS_ONE) {
            TPD_DEBUG("idle to active :%*ph\n", FTS_POINTS_ONE, ts_data->rbuf);
        }
        return IRQ_IGNORE;
    }

    if ((buf[0] == 0xFF) && (buf[1] == 0xFF) && (buf[2] == 0xFF) && (!is_suspended)) {
        TPD_INFO("Need recovery TP state");
        ret = touch_i2c_read_byte(ts_data->client, FTS_REG_POINTS_LB);
        return IRQ_FW_AUTO_RESET;
    }

    /*confirm need print debug info*/
    if (ts_data->rbuf[0] != ts_data->irq_type) {
        SET_BIT(result_event, IRQ_FW_HEALTH);
    }
    ts_data->irq_type = ts_data->rbuf[0];

    /*normal touch*/
    SET_BIT(result_event, IRQ_TOUCH);
    TPD_DEBUG("%s, fgerprint, is_suspended = %d, fp_en = %d, ", __func__, is_suspended, ts_data->fp_en);
    TPD_DEBUG("%s, fgerprint, touched = %d, event_type = %d, fp_down = %d, fp_down_report = %d, ", __func__, ts_data->ts->view_area_touched, ts_data->fod_info.event_type, ts_data->fod_info.fp_down, ts_data->fod_info.fp_down_report);

    if (!is_suspended && ts_data->fp_en) {
        fts_read_fod_info(ts_data);
        if ((ts_data->fod_info.event_type == FTS_EVENT_FOD) && (ts_data->fod_info.fp_down)) {
            if (!ts_data->fod_info.fp_down_report) {    // 38, 1, 0
                ts_data->fod_info.fp_down_report = 1;
                SET_BIT(result_event, IRQ_FINGERPRINT);
                TPD_DEBUG("%s, fgerprint, set IRQ_FINGERPRINT when fger down but not reported! \n", __func__);
            }
            //            if (ts_data->fod_info.fp_down_report) {     // 38, 1, 1
            //            }
        } else if ((ts_data->fod_info.event_type == FTS_EVENT_FOD) && (!ts_data->fod_info.fp_down)) {
            if (ts_data->fod_info.fp_down_report) {     // 38, 0, 1
                ts_data->fod_info.fp_down_report = 0;
                SET_BIT(result_event, IRQ_FINGERPRINT);
                TPD_DEBUG("%s, fgerprint, set IRQ_FINGERPRINT when fger up but still reported! \n", __func__);
            }
            //                if (!ts_data->fod_info.fp_down_report) {    // 38, 0, 0
            //                }
        }
    }

    return result_event;
}

//static void fts_show_touch_buffer(u8 *data, int datalen)
//{
//    int i = 0;
//    int count = 0;
//    char *tmpbuf = NULL;
//
//    tmpbuf = kzalloc(1024, GFP_KERNEL);
//    if (!tmpbuf) {
//        TPD_DEBUG("tmpbuf zalloc fail");
//        return;
//    }
//
//    for (i = 0; i < datalen; i++) {
//        count += snprintf(tmpbuf + count, 1024 - count, "%02X,", data[i]);
//        if (count >= 1024)
//            break;
//    }
//    TPD_DEBUG("point buffer:%s", tmpbuf);
//
//    if (tmpbuf) {
//        kfree(tmpbuf);
//        tmpbuf = NULL;
//    }
//}

static int fts_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;
    int i = 0;
    int obj_attention = 0;
    int base = 0;
    int touch_point = 0;
    u8 point_num = 0;
    u8 pointid = 0;
    u8 event_flag = 0;
    u8 cmd = FTS_REG_POINTS_N;
    u8 *buf = ts_data->rbuf;

    if (buf[FTS_POINTS_ONE - 1] == 0xFF)
        ret = touch_i2c_read_byte(ts_data->client, FTS_REG_POINTS_LB);
    else
        ret = touch_i2c_read_block(ts_data->client, cmd, FTS_POINTS_TWO, &buf[FTS_POINTS_ONE]);
    if (ret < 0) {
        TPD_INFO("read touch point two fail");
        return ret;
    }

    //    fts_show_touch_buffer(buf, FTS_MAX_POINTS_LENGTH);

    point_num = buf[1] & 0xFF;
    if (point_num > max_num) {
        TPD_INFO("invalid point_num(%d),max_num(%d)", point_num, max_num);
        return -EIO;
    }

    for (i = 0; i < max_num; i++) {
        base = 6 * i;
        pointid = (buf[4 + base]) >> 4;
        if (pointid >= FTS_MAX_ID)
            break;
        else if (pointid >= max_num) {
            TPD_INFO("ID(%d) beyond max_num(%d)", pointid, max_num);
            return -EINVAL;
        }

        touch_point++;
        if (!ts_data->high_resolution_support && !ts_data->high_resolution_support_x8) {
            points[pointid].x = ((buf[2 + base] & 0x0F) << 8) + (buf[3 + base] & 0xFF);
            points[pointid].y = ((buf[4 + base] & 0x0F) << 8) + (buf[5 + base] & 0xFF);
            points[pointid].touch_major = buf[7 + base];
            points[pointid].width_major = buf[7 + base];
            points[pointid].z =  buf[6 + base];
            event_flag = (buf[2 + base] >> 6);
        } else if (ts_data->high_resolution_support_x8) {
	    points[pointid].x = ((buf[2 + base] & 0x20) >> 5) +
	                        ((buf[2 + base] & 0x0F) << 11) +
	                        ((buf[3 + base] & 0xFF) << 3) +
	                        ((buf[6 + base] & 0xC0) >> 5);
	    points[pointid].y = ((buf[2 + base] & 0x10) >> 4) +
		                ((buf[4 + base] & 0x0F) << 11) +
	                        ((buf[5 + base] & 0xFF) << 3) +
	                        ((buf[6 + base] & 0x30) >> 3);
	   points[pointid].touch_major = buf[7 + base];
	   points[pointid].width_major = buf[7 + base];
	   points[pointid].z =  buf[6 + base] & 0x0F;
	   event_flag = (buf[2 + base] >> 6);

        } else {
            points[pointid].x = ((buf[2 + base] & 0x0F) << 10) + ((buf[3 + base] & 0xFF) << 2)
                        + ((buf[6 + base] & 0xC0) >> 6);
            points[pointid].y = ((buf[4 + base] & 0x0F) << 10) + ((buf[5 + base] & 0xFF) << 2)
                        + ((buf[6 + base] & 0x30) >> 4);
            points[pointid].touch_major = buf[7 + base];
            points[pointid].width_major = buf[7 + base];
            points[pointid].z =  buf[6 + base] & 0x0F;
            event_flag = (buf[2 + base] >> 6);
        }
        points[pointid].status = 0;
        if ((event_flag == 0) || (event_flag == 2)) {
            points[pointid].status = 1;
            obj_attention |= (1 << pointid);
            if (point_num == 0) {
                TPD_INFO("abnormal touch data from fw");
                return -EIO;
            }
        }
    }

    if (touch_point == 0) {
        TPD_INFO("no touch point information");
        return -EIO;
    }

    return obj_attention;
}

static void fts_health_report(void *chip_data, struct monitor_data *mon_data)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    ret = touch_i2c_read_byte(ts_data->client, 0x01);
    TPD_INFO("Health register(0x01):0x%x", ret);
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_HEALTH_1);
    TPD_INFO("Health register(0xFD):0x%x", ret);
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_HEALTH_2);
    TPD_INFO("Health register(0xFE):0x%x", ret);
}

static void fts_health_report_v2(void *chip_data, struct monitor_data_v2 *mon_data_v2)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    char *freq_str = NULL;

    ret = touch_i2c_read_byte(ts_data->client, 0x01);
    TPD_INFO("Health register(0x01):0x%x", ret);
    if (ret & 0x01) {
        TPD_DETAIL("Health register(0x01):Water Shield");
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_SHIELD_WATER);
    }
    if (ret & 0x02) {
        TPD_DETAIL("Health register(0x01):Palm Shield");
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_SHIELD_PALM);
    }
    if (ret & 0x04) {
        TPD_DETAIL("Health register(0x01):Freq Hop");
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_NOISE);
    }
    if (ret & 0x08) {
        TPD_DETAIL("Health register(0x01):Base Refresh");
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_BASELINE_ERR);
    }
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_HEALTH_1);
    TPD_INFO("Health register(0xFD):0x%x(water-flag:%d / noise-flag:%d / no-suitable-freq:%d)",
            ret, (ret & 0x01), (ret & 0x02), ((ret & 0x10) >> 4));
    if (ret & 0x10 && !mon_data_v2->no_suitable_freq) {
        mon_data_v2->no_suitable_freq = true;
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_NO_SUITABLE_FREQ);
    }
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_HEALTH_2);
    TPD_INFO("Health register(0xFE):0x%x(work-freq:%d)", ret, ret);
    if (mon_data_v2->work_freq && mon_data_v2->work_freq != ret) {
        freq_str = kzalloc(10, GFP_KERNEL);
        if (!freq_str) {
            TPD_INFO("freq_str kzalloc failed.\n");
        } else {
            snprintf(freq_str, 10, "freq_%d", ret);
            tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, freq_str);
            kfree(freq_str);
        }
    }
    mon_data_v2->work_freq = ret;
}

static int fts_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;
    u8 cmd = FTS_REG_GESTURE_OUTPUT_ADDRESS;
    u8 buf[FTS_GESTURE_DATA_LEN] = { 0 };
    u8 gesture_id = 0;
    u8 point_num = 0;

    ret = touch_i2c_read_block(ts_data->client, cmd, FTS_GESTURE_DATA_LEN - 2, &buf[2]);
    if (ret < 0) {
        TPD_INFO("read gesture data fail");
        return ret;
    }

    gesture_id = buf[2];
    point_num = buf[3];
    TPD_INFO("gesture_id=%d, point_num=%d", gesture_id, point_num);
	if (gesture == NULL) {
		TPD_INFO("gesture == NULL, return\n\
 			gesture->Point_start.x = %d\n;\
			gesture->Point_start.y = %d\n;\
			gesture->Point_end.x = %d\n;\
			gesture->Point_end.y = %d\n;\
			gesture->Point_1st.x = %d\n;\
			gesture->Point_1st.y = %d\n;\
			gesture->Point_2nd.x = %d\n;\
			gesture->Point_2nd.y = %d\n;\
			gesture->Point_3rd.x = %d\n;\
			gesture->Point_3rd.y = %d\n;\
			gesture->Point_4th.x = %d\n;\
			gesture->Point_4th.y = %d\n;"
			,((buf[4] << 8) + buf[5]), ((buf[6] << 8) + buf[7]), ((buf[8] << 8) + buf[9])
			,((buf[10] << 8) + buf[11]), ((buf[12] << 8) + buf[13]), ((buf[14] << 8) + buf[15])
			,((buf[16] << 8) + buf[17]), ((buf[18] << 8) + buf[19]), ((buf[20] << 8) + buf[21])
			,((buf[22] << 8) + buf[23]), ((buf[24] << 8) + buf[25]), ((buf[26] << 8) + buf[27]));
		return ret;
	}
    switch (gesture_id) {
    case GESTURE_DOUBLE_TAP:
        gesture->gesture_type = DouTap;
        break;
    case GESTURE_UP_VEE:
        gesture->gesture_type = UpVee;
        break;
    case GESTURE_DOWN_VEE:
        gesture->gesture_type = DownVee;
        break;
    case GESTURE_LEFT_VEE:
        gesture->gesture_type = LeftVee;
        break;
    case GESTURE_RIGHT_VEE:
        gesture->gesture_type = RightVee;
        break;
    case GESTURE_O_CLOCKWISE:
        gesture->clockwise = 1;
        gesture->gesture_type = Circle;
        break;
    case GESTURE_O_ANTICLOCK:
        gesture->clockwise = 0;
        gesture->gesture_type = Circle;
        break;
    case GESTURE_DOUBLE_SWIP:
        gesture->gesture_type = DouSwip;
        break;
    case GESTURE_LEFT2RIGHT_SWIP:
        gesture->gesture_type = Left2RightSwip;
        break;
    case GESTURE_RIGHT2LEFT_SWIP:
        gesture->gesture_type = Right2LeftSwip;
        break;
    case GESTURE_UP2DOWN_SWIP:
        gesture->gesture_type = Up2DownSwip;
        break;
    case GESTURE_DOWN2UP_SWIP:
        gesture->gesture_type = Down2UpSwip;
        break;
    case GESTURE_M:
        gesture->gesture_type = Mgestrue;
        break;
    case GESTURE_W:
        gesture->gesture_type = Wgestrue;
        break;
    case GESTURE_HEART_CLOCKWISE:
        gesture->clockwise = 1;
        gesture->gesture_type = Heart;
        break;
    case GESTURE_HEART_ANTICLOCK:
        gesture->clockwise = 0;
        gesture->gesture_type = Heart;
        break;
    case GESTURE_FINGER_PRINT:
        fts_read_fod_info(ts_data);
        TPD_INFO("FOD event type:0x%x", ts_data->fod_info.event_type);
        TPD_DEBUG("%s, fgerprint, touched = %d, fp_down = %d, fp_down_report = %d, \n", __func__, ts_data->ts->view_area_touched, ts_data->fod_info.fp_down, ts_data->fod_info.fp_down_report);
        if (ts_data->fod_info.event_type == FTS_EVENT_FOD) {
            if (ts_data->fod_info.fp_down && !ts_data->fod_info.fp_down_report) {
                gesture->gesture_type = FingerprintDown;
                ts_data->fod_info.fp_down_report = 1;
            } else if (!ts_data->fod_info.fp_down && ts_data->fod_info.fp_down_report) {
                gesture->gesture_type = FingerprintUp;
                ts_data->fod_info.fp_down_report = 0;
            }
            gesture->Point_start.x = ts_data->fod_info.fp_x;
            gesture->Point_start.y = ts_data->fod_info.fp_y;
            gesture->Point_end.x = ts_data->fod_info.fp_area_rate;
            gesture->Point_end.y = 0;
        }

        break;
    case GESTURE_SINGLE_TAP:
        gesture->gesture_type = SingleTap;
        break;
    default:
        gesture->gesture_type = UnkownGesture;
    }

    if ((gesture->gesture_type != FingerprintDown)
        && (gesture->gesture_type != FingerprintUp)
        && (gesture->gesture_type != UnkownGesture)) {
        gesture->Point_start.x = (u16)((buf[4] << 8) + buf[5]);
        gesture->Point_start.y = (u16)((buf[6] << 8) + buf[7]);
        gesture->Point_end.x = (u16)((buf[8] << 8) + buf[9]);
        gesture->Point_end.y = (u16)((buf[10] << 8) + buf[11]);
        gesture->Point_1st.x = (u16)((buf[12] << 8) + buf[13]);
        gesture->Point_1st.y = (u16)((buf[14] << 8) + buf[15]);
        gesture->Point_2nd.x = (u16)((buf[16] << 8) + buf[17]);
        gesture->Point_2nd.y = (u16)((buf[18] << 8) + buf[19]);
        gesture->Point_3rd.x = (u16)((buf[20] << 8) + buf[21]);
        gesture->Point_3rd.y = (u16)((buf[22] << 8) + buf[23]);
        gesture->Point_4th.x = (u16)((buf[24] << 8) + buf[25]);
        gesture->Point_4th.y = (u16)((buf[26] << 8) + buf[27]);
    }

    return 0;
}

static void fts_enable_fingerprint_underscreen(void *chip_data, uint32_t enable)
{
    int ret = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s:enable=%d", __func__, enable);
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_FOD_EN);
    if (ret < 0) {
        TPD_INFO("%s: read FOD enable(%x) fail", __func__, FTS_REG_FOD_EN);
        return;
    }

    TPD_DEBUG("%s, fgerprint, touched = %d, event_type = %d, fp_down = %d. fp_down_report = %d \n", __func__, ts_data->ts->view_area_touched, ts_data->fod_info.event_type, ts_data->fod_info.fp_down, ts_data->fod_info.fp_down_report);
    val = ret;
    if (enable) {
        val |= 0x02;
        ts_data->fp_en = 1;
        if ((!ts_data->ts->view_area_touched) && (ts_data->fod_info.event_type != FTS_EVENT_FOD) && (!ts_data->fod_info.fp_down) && (ts_data->fod_info.fp_down_report)) {  // notouch, !38, 0, 1
            ts_data->fod_info.fp_down_report = 0;
            TPD_DEBUG("%s, fgerprint, fp_down_report status abnormal (notouch, 38!, 0, 1), needed to be reseted! \n", __func__);
        }
    } else {
        val &= 0xFD;
        ts_data->fp_en = 0;
        ts_data->fod_info.fp_down = 0;
        ts_data->fod_info.event_type = 0;
        //        ts_data->fod_info.fp_down_report = 0;
    }
    TPD_INFO("%s:write %x=%x.", __func__, FTS_REG_FOD_EN, val);
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_FOD_EN, val);
    if (ret < 0) {
        TPD_INFO("%s: write FOD enable(%x=%x) fail", __func__, FTS_REG_FOD_EN, val);
    }
}

static void fts_enable_gesture_mask(void *chip_data, uint32_t enable)
{
    int ret = 0;
    int config1 = 0;
    int config2 = 0;
    int config4 = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    if (enable) {
        config1 = 0xff;
        config2 = 0xff;
        config4 = 0xff;
    } else if (!enable) {

    }
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG1, config1);
    if (ret < 0) {
        TPD_INFO("%s: write FTS_REG_GESTURE_CONFIG1 enable(%x=%x) fail", __func__, FTS_REG_GESTURE_CONFIG1, config1);
    }
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG2, config2);
    if (ret < 0) {
        TPD_INFO("%s: write FTS_REG_GESTURE_CONFIG2 enable(%x=%x) fail", __func__, FTS_REG_GESTURE_CONFIG2, config2);
    }
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GESTURE_CONFIG4, config4);
    if (ret < 0) {
        TPD_INFO("%s: write FTS_REG_GESTURE_CONFIG4 enable(%x=%x) fail", __func__, FTS_REG_GESTURE_CONFIG4, config4);
    }

    msleep(1);
    TPD_INFO("%s, enable[%d] register[FTS_REG_GESTURE_CONFIG1. FTS_REG_GESTURE_CONFIG2. FTS_REG_GESTURE_CONFIG4]", __func__, enable);
}

static void fts_screenon_fingerprint_info(void *chip_data, struct fp_underscreen_info *fp_tpinfo)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    memset(fp_tpinfo, 0, sizeof(struct fp_underscreen_info));
    TPD_INFO("FOD event type:0x%x", ts_data->fod_info.event_type);
    if (ts_data->fod_info.fp_down)
        fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
    else
        fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;

    fp_tpinfo->area_rate = ts_data->fod_info.fp_area_rate;
    fp_tpinfo->x = ts_data->fod_info.fp_x;
    fp_tpinfo->y = ts_data->fod_info.fp_y;

    TPD_INFO("FOD Info:touch_state:%d,area_rate:%d,x:%d,y:%d[fp_down:%d]",
             fp_tpinfo->touch_state, fp_tpinfo->area_rate, fp_tpinfo->x,
             fp_tpinfo->y, ts_data->fod_info.fp_down);
}

static void fts_register_info_read(void *chip_data, uint16_t register_addr, uint8_t *result, uint8_t length)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 addr = (u8)register_addr;

    touch_i2c_read_block(ts_data->client, addr, length, result);
}

static void fts_set_touch_direction(void *chip_data, uint8_t dir)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    ts_data->touch_direction = dir;
}

static uint8_t fts_get_touch_direction(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return ts_data->touch_direction;
}

static int fts_refresh_switch(void *chip_data, int fps)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("lcd fps =%d", fps);

    return touch_i2c_write_byte(ts_data->client, FTS_REG_REPORT_RATE,
                                fps == 60 ? FTS_120HZ_REPORT_RATE : FTS_180HZ_REPORT_RATE);
}

static void fts_set_gesture_state(void *chip_data, int state)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    ts_data->gesture_state = state;
}

static int fts_smooth_lv_set(void *chip_data, int level)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("set smooth lv to %d", level);

    return touch_i2c_write_byte(ts_data->client, FTS_REG_SMOOTH_LEVEL, level);
}

static int fts_sensitive_lv_set(void *chip_data, int level)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("set sensitive lv to %d", level);

    return touch_i2c_write_byte(ts_data->client, FTS_REG_SENSITIVE_LEVEL, level);
}

static int fts_set_high_frame_rate(void *chip_data, int level, int time)
{
	int ret = 0;
	struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
	struct touchpanel_data *ts = i2c_get_clientdata(ts_data->client);

	TPD_INFO("set high_frame_rate to %d, keep %ds", level, time);
	if (level != 0) {
		level = 4;
	}
	level = level | (!ts->noise_level);

	ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GAME_MODE_EN, level);
	if (ret < 0) {
		return ret;
	}
	if (level) {
		ret = touch_i2c_write_byte(ts_data->client, FTS_REG_HIGH_FRAME_TIME, time);
	}
	return ret;
}

static struct oplus_touchpanel_operations fts_ops = {
    .power_control              = fts_power_control,
    .get_vendor                 = fts_get_vendor,
    .get_chip_info              = fts_get_chip_info,
    .fw_check                   = fts_fw_check,
    .mode_switch                = fts_mode_switch,
    .reset                      = fts_reset,
    .reset_gpio_control         = fts_reset_gpio_control,
    .fw_update                  = fts_fw_update,
    .u32_trigger_reason         = fts_u32_trigger_reason,
    .get_touch_points           = fts_get_touch_points,
    .health_report              = fts_health_report,
    .health_report_v2           = fts_health_report_v2,
    .get_gesture_info           = fts_get_gesture_info,
    .ftm_process                = fts_ftm_process,
    .enable_fingerprint         = fts_enable_fingerprint_underscreen,
    .screenon_fingerprint_info  = fts_screenon_fingerprint_info,
    .register_info_read         = fts_register_info_read,
    .set_touch_direction        = fts_set_touch_direction,
    .get_touch_direction        = fts_get_touch_direction,
    .esd_handle                 = fts_esd_handle,
    .enable_gesture_mask        = fts_enable_gesture_mask,
    .tp_refresh_switch          = fts_refresh_switch,
    .smooth_lv_set              = fts_smooth_lv_set,
    .sensitive_lv_set           = fts_sensitive_lv_set,
    .set_gesture_state          = fts_set_gesture_state,
    .set_high_frame_rate        = fts_set_high_frame_rate,
};

static struct fts_proc_operations fts_proc_ops = {
    .auto_test              = fts_auto_test,
};

static struct debug_info_proc_operations fts_debug_info_proc_ops = {
    .limit_read        = ft_limit_read_std,
    .delta_read        = fts_delta_read,
    .self_delta_read   = fts_self_delta_read,
    .baseline_read	   = fts_baseline_read,
    .main_register_read = fts_main_register_read,
	.baseline_blackscreen_read = fts_baseline_read,
};

struct focal_debug_func focal_debug_ops = {
    .esd_check_enable       = focal_esd_check_enable,
    .get_esd_check_flag     = focal_get_esd_check_flag,
    .get_fw_version         = focal_get_fw_version,
    .dump_reg_sate          = focal_dump_reg_state,
};

static void ft3518_start_aging_test(void *chip_data)
{
    int ret = -1;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s: start aging test \n", __func__);
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GAME_MODE_EN, 2);
    if (ret < 0) {
        TPD_INFO("%s: enable(%x=%x) fail", __func__, FTS_REG_GAME_MODE_EN, 2);
    }
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_POWER_MODE, 0);
    if (ret < 0) {
        TPD_INFO("%s: enable(%x=%x) fail", __func__, FTS_REG_POWER_MODE, 0);
    }
}

static void ft3518_finish_aging_test(void *chip_data)
{
    int ret = -1;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s: finish aging test \n", __func__);
    ret = touch_i2c_write_byte(ts_data->client, FTS_REG_GAME_MODE_EN, 1);
    if (ret < 0) {
        TPD_INFO("%s: enable(%x=%x) fail", __func__, FTS_REG_GAME_MODE_EN, 1);
    }
}

static struct aging_test_proc_operations ft3518_aging_test_ops = {
    .start_aging_test   = ft3518_start_aging_test,
    .finish_aging_test  = ft3518_finish_aging_test,
};

static int ft3518_parse_dts(struct fts_ts_data *ts_data, struct i2c_client *client)
{
    struct device *dev;
    struct device_node *np;
	int ret = 0;

    dev = &client->dev;
    np = dev->of_node;

	ts_data->high_resolution_support = of_property_read_bool(np, "high_resolution_support");
	ts_data->high_resolution_support_x8 = of_property_read_bool(np, "high_resolution_support_x8");
	TPD_INFO("%s:high_resolution_support is:%d %d\n", __func__, ts_data->high_resolution_support, ts_data->high_resolution_support_x8);
	ts_data->need_pinctrl_pull_up_reset = of_property_read_bool(np, "need_pinctrl_pull_up_reset");
	TPD_INFO("%s:need_pinctrl_pull_up_reset is: %d\n", __func__, ts_data->need_pinctrl_pull_up_reset);

	if (ts_data->need_pinctrl_pull_up_reset) {
		ts_data->hw_res->pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR_OR_NULL(ts_data->hw_res->pinctrl)) {
			TPD_INFO("Getting pinctrl handle failed");
			ret = PTR_ERR(ts_data->hw_res->pinctrl);
			goto err_pinctrl_get;
		} else {
			ts_data->hw_res->pin_set_reset_high = pinctrl_lookup_state(ts_data->hw_res->pinctrl, "pin_set_reset_high");
			if (IS_ERR_OR_NULL(ts_data->hw_res->pin_set_reset_high)) {
				TPD_INFO("Failed to get reset high state pinctrl handle\n");
				ret = PTR_ERR(ts_data->hw_res->pin_set_reset_high);
				goto err_pinctrl_lookup;
			}

			ts_data->hw_res->pin_set_reset_low = pinctrl_lookup_state(ts_data->hw_res->pinctrl, "pin_set_reset_low");
			if (IS_ERR_OR_NULL(ts_data->hw_res->pin_set_reset_low)) {
				TPD_INFO(" Failed to get reset low state pinctrl handle\n");
				ret = PTR_ERR(ts_data->hw_res->pin_set_reset_low);
				goto err_pinctrl_lookup;
			}
		}
	}
	return 0;

err_pinctrl_lookup:
	if (ts_data->hw_res->pinctrl) {
		devm_pinctrl_put(ts_data->hw_res->pinctrl);
	}
err_pinctrl_get:
	ts_data->hw_res->pinctrl = NULL;
	ts_data->hw_res->pin_set_reset_low = NULL;
	ts_data->hw_res->pin_set_reset_high = NULL;

	return ret;
}

static int fts_tp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct fts_ts_data *ts_data;
    struct touchpanel_data *ts = NULL;
    u64 time_counter = 0;
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    if (tp_register_times > 0) {
        TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
        return -1;
    }
    reset_healthinfo_time_counter(&time_counter);

    /*step1:Alloc chip_info*/
    ts_data = kzalloc(sizeof(struct fts_ts_data), GFP_KERNEL);
    if (ts_data == NULL) {
        TPD_INFO("ts_data kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(ts_data, 0, sizeof(*ts_data));
    fts_data = ts_data;

    /*step2:Alloc common ts*/
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }
    memset(ts, 0, sizeof(*ts));

    /*step3:binding client && dev for easy operate*/
    //ts_data->dev = &client->dev;
    ts_data->dev = ts->dev;
    ts_data->client = client;
    ts_data->hw_res = &ts->hw_res;
    ts_data->irq_num = ts->irq;
    ts_data->ts = ts;
    ts_data->syna_ops = &fts_proc_ops;
    ts->debug_info_ops = &fts_debug_info_proc_ops;
    ts->client = client;
    ts->irq = client->irq;
    i2c_set_clientdata(client, ts);
    ts->dev = &client->dev;
    ts->chip_data = ts_data;

    /*step4:file_operations callback binding*/
    ts->ts_ops = &fts_ops;
    ts->private_data = &focal_debug_ops;
    ts->aging_test_ops = &ft3518_aging_test_ops;
    ft3518_parse_dts(ts_data, client);

    /*step5:register common touch*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    ts_data->black_gesture_indep = ts->black_gesture_indep_support;
    ts_data->monitor_data_v2 = &ts->monitor_data_v2;
	ts_data->exception_data = &ts->exception_data;

    /*step6:create synaptics related proc files*/
    fts_create_proc(ts, ts_data->syna_ops);

    /*step7:Chip Related function*/
    focal_create_sysfs(client);

    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_PROBE, &time_counter);
    }
    ts_data->probe_done = 1;
    TPD_INFO("%s, probe normal end\n", __func__);

    return 0;

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

ts_malloc_failed:
    kfree(ts_data);
    ts_data = NULL;
    ret = -1;

    TPD_INFO("%s, probe error\n", __func__);

    return ret;
}

static int fts_tp_remove(struct i2c_client *client)
{
    struct touchpanel_data *ts = i2c_get_clientdata(client);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int fts_i2c_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called\n", __func__);
    tp_i2c_suspend(ts);

    return 0;
}

static int fts_i2c_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);
    tp_i2c_resume(ts);

    return 0;
}

static const struct i2c_device_id tp_id[] = {
    { TPD_DEVICE, 0 },
    { }
};

static struct of_device_id tp_match_table[] = {
    { .compatible = TPD_DEVICE, },
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = fts_i2c_suspend,
    .resume = fts_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver = {
    .probe          = fts_tp_probe,
    .remove         = fts_tp_remove,
    .id_table   = tp_id,
    .driver         = {
        .name   = TPD_DEVICE,
        .of_match_table =  tp_match_table,
        .pm = &tp_pm_ops,
    },
};

static int __init tp_driver_init(void)
{
    TPD_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE))
        return -1;

    if (i2c_add_driver(&tp_i2c_driver) != 0) {
        TPD_INFO("unable to add i2c driver.\n");
        return -1;
    }
    return 0;
}

/* should never be called */
static void __exit tp_driver_exit(void)
{
    i2c_del_driver(&tp_i2c_driver);
    return;
}
#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init);
#else
module_init(tp_driver_init);
#endif
module_exit(tp_driver_exit);

MODULE_DESCRIPTION("Touchscreen Driver");
MODULE_LICENSE("GPL");
