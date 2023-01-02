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

#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "sec_drivers_s6d7at0.h"

/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "sec-s6d7at0"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (LEVEL_DEBUG == tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DETAIL(a, arg...)\
    do{\
        if (LEVEL_BASIC != tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
    do{\
        if (tp_debug)\
            printk(a, ##arg);\
    }while(0)
/******************** End of Log Tag Declear and level define*********************************/

/*************************** start of function delcare****************************************/
void sec_mdelay(unsigned int ms);
static int sec_reset(void *chip_data);
static int sec_power_control(void *chip_data, bool enable);
static int sec_get_verify_result(struct chip_data_s6d7at0 *chip_info);
static void sec_reset_esd(struct chip_data_s6d7at0 *chip_info);
static void sec_calibrate(struct seq_file *s, void *chip_data);

//extern int set_lsi_tp_watchdog_state(int state);
__attribute__((weak)) int set_lsi_tp_watchdog_state(int state) {return 1;}
/**************************** end of function delcare*****************************************/


/****** Start of other functions that work for oplus_touchpanel_operations callbacks***********/

static int last_state =MODE_NORMAL; //add define "last_state"

static int sec_enable_black_gesture(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;
    //int i = 0;

    TPD_INFO("%s, enable = %d\n", __func__, enable);

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);

    if (enable) {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0x00);
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x01);
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_AFE_SENSING, 0x0D);
        sec_mdelay(50);
        TPD_INFO("%s, last_state = %d,ret = %d\n", __func__, last_state, ret);
    } else {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0x01);
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_AFE_SENSING, 0x0C);
        TPD_INFO("%s, ret = %d\n", __func__, ret);
        return 0;
    }

    return 0;
}

static int sec_enable_edge_limit(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;
    u8 data[4] = {0,};

    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, data);

    if (chip_info->edge_limit_support) {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_EDGE_DEADZONE, 0x00);
    }
    else if (chip_info->fw_edge_limit_support) {
        if (enable == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {
            ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_EDGE_DEADZONE, 0x01);
        } else {
            if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
                ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_EDGE_DEADZONE, 0x00);
            } else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
                ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_EDGE_DEADZONE, 0x02);
            }
        }
    }
    else {
        TPD_INFO("%s: invalid fw version: %02X %02X %02X %02X\n", __func__, data[0], data[1], data[2], data[3]);
    }

    //ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_EDGE_DEADZONE, 0x00);
    TPD_INFO("%s: state: %d %d %s!\n", __func__, enable, chip_info->touch_direction, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_charge_mode(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;
    int retry = 0;
    unsigned char touchfunction, readfunction;

    touchfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
    sec_mdelay(20);

    if (enable) {
        touchfunction = touchfunction | SEC_BIT_SETFUNC_CHARGER | SEC_BIT_SETFUNC_TOUCH;
    } else {
        touchfunction = ((touchfunction & (~SEC_BIT_SETFUNC_CHARGER)) | SEC_BIT_SETFUNC_TOUCH);
    }
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION, touchfunction);
    sec_mdelay(150);

    while (retry++ < 50) {
        readfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
        TPD_DEBUG("%s: retry %d (%02X)\n", __func__, retry, readfunction);
        if ((touchfunction & SEC_BIT_SETFUNC_CHARGER) == (readfunction & SEC_BIT_SETFUNC_CHARGER))
            break;
        sec_mdelay(20);

    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_earsense_mode(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;

    if (enable) {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_HOVER_DETECT, 1);
        ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_DELTA);
    } else {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_HOVER_DETECT, 0);
        ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_SIGNAL_DATA);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_palm_reject(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;
    int retry = 0;
    unsigned char touchfunction, readfunction;

    touchfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
    sec_mdelay(20);

    if (enable) {
        touchfunction = touchfunction | SEC_BIT_SETFUNC_PALM| SEC_BIT_SETFUNC_TOUCH;
    } else {
        touchfunction = ((touchfunction & (~SEC_BIT_SETFUNC_PALM)) | SEC_BIT_SETFUNC_TOUCH);
    }
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION, touchfunction);

    while (retry++ < 50) {
        readfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
        TPD_DEBUG("%s: retry %d (%02X)\n", __func__, retry, readfunction);
        if ((touchfunction & SEC_BIT_SETFUNC_PALM) == (readfunction & SEC_BIT_SETFUNC_PALM))
            break;
        sec_mdelay(20);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_game_mode(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_STOP_FILTER, enable ? 0x01 : 0x00);
    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_headset_mode(struct chip_data_s6d7at0 *chip_info, bool enable)
{
    int ret = -1;
    int retry = 0;
    unsigned char touchfunction, readfunction;

    touchfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);

    sec_mdelay(20);

    if (enable) {
        touchfunction = touchfunction | SEC_BIT_SETFUNC_HEADSET| SEC_BIT_SETFUNC_TOUCH;
    } else {
        touchfunction = ((touchfunction & (~SEC_BIT_SETFUNC_HEADSET)) | SEC_BIT_SETFUNC_TOUCH);
    }
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION, touchfunction);
    sec_mdelay(150);

    while (retry++ < 50) {
        readfunction = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
        TPD_DEBUG("%s: retry %d (%02X)\n", __func__, retry, readfunction);
        if ((touchfunction & SEC_BIT_SETFUNC_HEADSET) == (readfunction & SEC_BIT_SETFUNC_HEADSET))
            break;
        sec_mdelay(20);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;

}

void sec_mdelay(unsigned int ms)
{
    if (ms < 20)
        usleep_range(ms * 1000, ms * 1000);
    else
        msleep(ms);
}

int sec_wait_for_ready(struct chip_data_s6d7at0 *chip_info, unsigned int ack)
{
    int rc = -1;
    int retry = 0, retry_cnt = 100;
    int8_t status = -1;
    u8 tBuff[SEC_EVENT_BUFF_SIZE] = {0,};

    while (touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE, tBuff) > 0) {
        status = (tBuff[0] >> 2) & 0xF;
        if ((status == TYPE_STATUS_EVENT_INFO) || (status == TYPE_STATUS_EVENT_VENDOR_INFO)) {
            if (tBuff[1] == ack) {
                rc = 0;
                break;
            }
        }

        if (retry++ > retry_cnt) {
            TPD_INFO("%s: Time Over, event_buf: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X \n", \
                        __func__, tBuff[0], tBuff[1], tBuff[2], tBuff[3], tBuff[4], tBuff[5], tBuff[6], tBuff[7]);
            break;
        }
        sec_mdelay(20);
    }
    TPD_INFO("%s: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X \n", \
                __func__, tBuff[0], tBuff[1], tBuff[2], tBuff[3], tBuff[4], tBuff[5], tBuff[6], tBuff[7]);

    return rc;
}

static u8 sec_checksum(u8 *data, int offset, int size)
{
    int i;
    u8 checksum = 0;

    for (i = 0; i < size; i++)
        checksum += data[i + offset];

    return checksum;
}

static int sec_ts_flash_set_datanum(struct chip_data_s6d7at0 *chip_info, u16 num)
{
    u8 tData[2];
    int ret;

    tData[0] = (num >> 8) & 0xFF;
    tData[1] = num & 0xFF;

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_FLASH_READ_SIZE, sizeof(tData), tData);
    if (ret < 0)
        TPD_INFO("%s: Set datanum Fail %d\n", __func__, num);

    return ret;
}

static int sec_ts_flash_cs_control(struct chip_data_s6d7at0 *chip_info, bool cs_level)
{
    u8 tData;
    int ret;

    tData = cs_level ? 1 : 0;

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CS_CONTROL, 1, &tData);
    if (ret < 0)
        TPD_INFO("%s: %s control Fail!\n", __func__, cs_level ? "CS High" : "CS Low");

    return ret;
}

static int  sec_ts_wren(struct chip_data_s6d7at0 *chip_info)
{
    u8 tData[2];
    int ret;

    sec_ts_flash_cs_control(chip_info, CS_LOW);

    tData[0] = FLASH_CMD_WREN;
    ret =  touch_i2c_write_block(chip_info->client, SEC_CMD_FLASH_SEND_DATA, 1, tData);
    if (ret < 0)
        TPD_INFO( "%s: Send WREN fail!\n", __func__);

    sec_ts_flash_cs_control(chip_info, CS_HIGH);

    return ret;
}

static u8 sec_ts_rdsr(struct chip_data_s6d7at0 *chip_info)
{
    u8 tData[2];
    int ret;

    sec_ts_flash_cs_control(chip_info, CS_LOW);

    tData[0] = FLASH_CMD_RDSR;
    ret =  touch_i2c_write_block(chip_info->client, SEC_CMD_FLASH_SEND_DATA, 1, tData);

    ret =  touch_i2c_read_block(chip_info->client, SEC_CMD_FLASH_READ_DATA, 1, tData);

    //chip_info->touch_i2c_read_block(chip_info, SEC_TS_CMD_FLASH_READ_MEM, tData, 1);

    sec_ts_flash_cs_control(chip_info, CS_HIGH);

    //TPD_INFO("%s: %d\n",__func__, tData[0]);
    return tData[0];
}

static bool IsFlashBusy(struct chip_data_s6d7at0 *chip_info)
{
    u8 tBuf;
    sec_ts_wren(chip_info);
    tBuf = sec_ts_rdsr(chip_info);
    if ((tBuf & SEC_TS_FLASH_WIP_MASK) == SEC_TS_FLASH_WIP_MASK)
        return true;
    else
        return false;
}

static int sec_ts_wait_for_flash_busy(struct chip_data_s6d7at0 *chip_info)
{
    int retry_cnt = 0;
    int ret = 0;

    while (IsFlashBusy(chip_info)) {
        sec_mdelay(10);

        if (retry_cnt++ > SEC_TS_WAIT_RETRY_CNT) {
            TPD_INFO("%s: Retry Cnt over!\n", __func__);
            ret = -1;
        }
    }

    return ret;
}

static int sec_ts_cmd_flash_se(struct chip_data_s6d7at0 *chip_info, u32 flash_addr)
{
    int ret;
    u8 tBuf[5];

    if( IsFlashBusy(chip_info) )
    {
        TPD_INFO("%s: flash busy, flash_addr = %X\n", __func__, flash_addr);
        return false;
    }

    sec_ts_wren(chip_info);

    sec_ts_flash_cs_control(chip_info, CS_LOW);

    tBuf[0] = SEC_CMD_FLASH_SEND_DATA;
    tBuf[1] = FLASH_CMD_SE;
    tBuf[2] = (flash_addr >> 16) & 0xFF;
    tBuf[3] = (flash_addr >>  8) & 0xFF;
    tBuf[4] = (flash_addr >>  0) & 0xFF;
    ret = touch_i2c_write(chip_info->client, tBuf, 5);

    sec_mdelay(10);

    sec_ts_flash_cs_control(chip_info, CS_HIGH);
    if( ret < 0)
    {
        TPD_INFO("%s: Send sector erase cmd fail!\n", __func__);
        return ret;
    }

    ret = sec_ts_wait_for_flash_busy(chip_info);
    if( ret < 0 )
        TPD_INFO("%s: Time out! - flash busy wait\n", __func__);

    return ret;
}

static int sec_flash_sector_erase(struct chip_data_s6d7at0 *chip_info, u32 sector_idx)
{
    u32 addr;
    int ret;

    addr = sector_idx * BYTE_PER_PAGE;

    ret = sec_ts_cmd_flash_se(chip_info, addr);
    if (ret < 0)
        TPD_INFO("%s : ret = %d, sector_idx = %d\n", __func__, ret, sector_idx);
    sec_mdelay(10);

    return ret;
}

static int sec_flash_page_write(struct chip_data_s6d7at0 *chip_info, u32 page_idx, u8 *page_data)
{
    int ret;
    int i, j;
    u8 *tCmd;
    u8 copy_data[3 + 256];
    int copy_left = 256 + 3;
    int copy_size = 0;
    int copy_max = 200;

    copy_data[0] = (u8)((page_idx >> 8) & 0xFF);
    copy_data[1] = (u8)((page_idx >> 0) & 0xFF);
    for (i = 0; i < 256; i++)
        copy_data[2 + i] = page_data[i];
    copy_data[2 + 256] = sec_checksum(copy_data, 0, 2 + 256);

    sec_ts_flash_cs_control(chip_info, CS_LOW);
    while (copy_left > 0) {
        int copy_cur = (copy_left > copy_max) ? copy_max : copy_left;
        tCmd = (u8 *)kzalloc(copy_cur + 1, GFP_KERNEL);
        if (copy_size == 0)
            tCmd[0] = 0xD9;
        else
            tCmd[0] = 0xDA;

        for (j = 0; j < copy_cur; j++)
            tCmd[j+1] = copy_data[copy_size + j];
        ret = touch_i2c_write(chip_info->client, tCmd, 1+copy_cur);
        if (ret < 0) {
            TPD_INFO("flash page write,iic write burst fail");
            break;
        }
        copy_size += copy_cur;
        copy_left -= copy_cur;
        kfree(tCmd);
    }

    //sec_mdelay(10);
    sec_mdelay(5);
    sec_ts_flash_cs_control(chip_info, CS_HIGH);

    return ret;
}

static int sec_block_read(struct chip_data_s6d7at0 *chip_info, u32 mem_addr, int mem_size, u8 *buf)
{
    int ret;
    u8 cmd[5];
    u8 *data;

    if (mem_size >= 64 * 1024) {
        TPD_INFO("%s: mem size over 64K\n", __func__);
        return -EIO;
    }

    sec_ts_flash_cs_control(chip_info, CS_LOW);

    cmd[0] = (u8)SEC_CMD_FLASH_SEND_DATA;
    cmd[1] = 0x03;
    cmd[2] = (u8)((mem_addr >> 16) & 0xff);
    cmd[3] = (u8)((mem_addr >> 8) & 0xff);
    cmd[4] = (u8)((mem_addr >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 5);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }

/*  //udelay(10);
    sec_mdelay(10);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_SIZE;
    cmd[1] = (u8)((mem_size >> 8) & 0xff);
    cmd[2] = (u8)((mem_size >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 3);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }
*/
    //sec_mdelay(10);
    sec_mdelay(1);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_DATA;
    data = buf;

    ret = touch_i2c_read(chip_info->client, cmd, 1, data, mem_size);
    if (ret < 0) {
        TPD_INFO("%s: memory read failed\n", __func__);
        return -EIO;
    }
    //sec_mdelay(5);
    sec_mdelay(3);
    sec_ts_flash_cs_control(chip_info, CS_HIGH);

    return 0;
}

static int sec_memory_read(struct chip_data_s6d7at0 *chip_info, u32 mem_addr, u8 *mem_data, u32 mem_size)
{
    int ret;
    int retry = 3;
    int read_size = 0;
    int unit_size;
    int max_size = I2C_BURSTMAX;
    int read_left = (int)mem_size;
    u8 *tmp_data;

    tmp_data = kmalloc(max_size, GFP_KERNEL);
    if (!tmp_data) {
        TPD_INFO("%s: failed to kmalloc\n", __func__);
        return -ENOMEM;
    }

    while (read_left > 0) {
        unit_size = (read_left > max_size) ? max_size : read_left;
        retry = 3;
        do {
            ret = sec_block_read(chip_info, mem_addr, unit_size, tmp_data);
            if (read_size == 0 && tmp_data[0] == 0)
                ret = -1;
            if (retry-- == 0) {
                TPD_INFO("%s: fw read fail mem_addr=%08X, unit_size=%d\n", __func__, mem_addr, unit_size);
                kfree(tmp_data);
                return -1;
            }

            memcpy(mem_data + read_size, tmp_data, unit_size);
        } while (ret < 0);

        mem_addr += unit_size;
        read_size += unit_size;
        read_left -= unit_size;
    }

    kfree(tmp_data);

    return read_size;
}

static int sec_flash_write(struct chip_data_s6d7at0 *chip_info, u32 mem_addr, u8 *mem_data, u32 mem_size)
{
    int ret = -1;
    u32 size_left = 0, size_copy = 0, flash_page_size = 0;
    int page_idx = 0;
    u32 page_idx_start = 0, page_idx_end = 0, page_num = 0;
    u8 page_buf[SEC_TS_FLASH_SIZE_256] = {0};

    if (mem_size == 0)
        return 0;

    flash_page_size = SEC_TS_FLASH_SIZE_256;
    page_idx_start = mem_addr / flash_page_size;
    page_idx_end = (mem_addr + mem_size - 1) / flash_page_size;
    page_num = page_idx_end - page_idx_start + 1;

    TPD_INFO("%s: page_idx_start=%X, page_idx_end=%X\n", __func__, page_idx_start, page_idx_end);

  sec_ts_flash_set_datanum(chip_info, 0xffff);

    for (page_idx = (int)((page_num - 1)/16); page_idx >= 0; page_idx--) {
        ret = sec_flash_sector_erase(chip_info, (u32)(page_idx_start + page_idx * 16));
        if (ret < 0) {
            TPD_INFO("%s: Sector erase failed, sector_idx = %d\n", __func__, page_idx_start + page_idx * 16);
            return -EIO;
        }
    }

    TPD_INFO("%s flash sector erase done\n", __func__);
    sec_mdelay(page_num + 10);

    size_left = (int)mem_size;
    size_copy = (int)(mem_size % flash_page_size);
    if (size_copy == 0)
        size_copy = (int)flash_page_size;

    memset(page_buf, 0, flash_page_size);

    for (page_idx = page_num - 1;; page_idx--) {
        memcpy(page_buf, mem_data + (page_idx * flash_page_size), size_copy);
        ret = sec_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
        if (ret < 0) {
            sec_mdelay(50);
            ret = sec_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
            if (ret < 0) {
                TPD_INFO("%s: fw write failed, page_idx = %u\n", __func__, page_idx);
                goto err;
            }
        }

        size_copy = flash_page_size;
        sec_mdelay(5);

        if (page_idx == 0)
            break;
    }

    return mem_size;
err:
    return -EIO;
}

static int sec_chunk_update(struct chip_data_s6d7at0 *chip_info, u32 addr, u32 size, u8 *data)
{
    int ii = 0;
    int ret = 0;
    u8 *mem_rb = NULL;
    u32 write_size = 0;
    u32 fw_size = size;

    write_size = sec_flash_write(chip_info, addr, data, fw_size);
    if (write_size != fw_size) {
        TPD_INFO("%s: fw write failed\n", __func__);
        ret = -1;
        goto err_write_fail;
    }

    TPD_INFO("%s: flash write success\n", __func__);

    mem_rb = vzalloc(fw_size);
    if (!mem_rb) {
        TPD_INFO("%s: vzalloc failed\n", __func__);
        ret = -1;
        goto err_write_fail;
    }
#if 1
    if (sec_memory_read(chip_info, addr, mem_rb, fw_size) >= 0) {
        for (ii = 0; ii < fw_size; ii++) {
            if (data[ii] != mem_rb[ii])
                break;
        }

        if (fw_size != ii) {
            TPD_INFO("%s: fw verify fail at data[%d](%d, %d)\n", __func__, ii, data[ii], mem_rb[ii]);
            ret = -1;
            goto out;
        }
    } else {
        ret = -1;
        goto out;
    }
#endif
    TPD_INFO("%s: verify done(%d)\n", __func__, ret);

out:
    vfree(mem_rb);
err_write_fail:
    sec_mdelay(10);

    return ret;
}

int sec_read_calibration_report(struct chip_data_s6d7at0 *chip_info)
{
    int ret;
    u8 buf[5] = { 0 };

    buf[0] = SEC_CMD_READ_CALIBRATION_REPORT;

    ret = touch_i2c_read(chip_info->client, &buf[0], 1, &buf[1], 4);
    if (ret < 0) {
        TPD_INFO("%s: failed to read, ret = %d\n", __func__, ret);
        return ret;
    }

    TPD_INFO("%s: count:%d, pass count:%d, fail count:%d, status:0x%X\n",
                __func__, buf[1], buf[2], buf[3], buf[4]);

    return buf[4];
}

int sec_execute_force_calibration(struct chip_data_s6d7at0 *chip_info)
{
    int rc = -1;

    if (touch_i2c_write_block(chip_info->client, SEC_CMD_FACTORY_PANELCALIBRATION, 0, NULL) < 0) {
        TPD_INFO("%s: Write Cal command failed!\n", __func__);
        return rc;
    }

    sec_mdelay(1000);
    rc = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_OFFSET_CAL_DONE);
    if (rc < 0) {
        TPD_INFO("%s: Read Cal done Ack Failed!\n", __func__);
        return rc;
    }

    TPD_INFO("%s: Mis Cal Check\n", __func__);

    rc = touch_i2c_write_block(chip_info->client, SEC_CMD_MIS_CAL_CHECK, 0, NULL);    //set to verify calibration
    if (rc < 0) {
        TPD_INFO("%s: Write Mis Cal command Failed!\n", __func__);
        return rc;
    }
    sec_mdelay(200);

    return rc;
}

static void handleFourCornerPoint(struct Coordinate *point, int n)
{
    int i = 0;
    struct Coordinate left_most = point[0], right_most = point[0], top_most = point[0], down_most = point[0];

    if (n < 4)
        return;

    for (i = 0; i < n; i++) {
        if (right_most.x < point[i].x) {   //xmax
            right_most = point[i];
        }
        if (left_most.x > point[i].x) {   //xmin
            left_most = point[i];
        }
        if (down_most.y < point[i].y) {   //ymax
            down_most = point[i];
        }
        if (top_most.y > point[i].y) {   //ymin
            top_most = point[i];
        }
    }
    point[0] = top_most;
    point[1] = left_most;
    point[2] = down_most;
    point[3] = right_most;
}
/****** End of other functions that work for oplus_touchpanel_operations callbacks*************/

/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
static int sec_reset(void *chip_data)
{
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    TPD_INFO("%s is called\n", __func__);
    if (chip_info->is_power_down) { //power off state, no need reset
        return 0;
    }

    disable_irq_nosync(chip_info->client->irq);

    touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET, 0, NULL);
    sec_mdelay(200);

    sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
    sec_mdelay(50);

    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
    TPD_INFO("%s: clear event buffer\n", __func__);

    enable_irq(chip_info->client->irq);

    return 0;
}

static int sec_vrom_reset(void *chip_data)
{
//    int ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    TPD_INFO("%s is called\n", __func__);
    if (chip_info->is_power_down) { //power off state, no need reset
        return 0;
    }

    disable_irq_nosync(chip_info->client->irq);

    touch_i2c_write_block(chip_info->client, SEC_CMD_VROM_RESET, 0, NULL);
    sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);

    enable_irq(chip_info->client->irq);

    return 0;
}

static int sec_ftm_process(void *chip_data)
{
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    TPD_INFO("%s is called!\n", __func__);
    tp_powercontrol_2v8(chip_info->hw_res, false);
    tp_powercontrol_1v8(chip_info->hw_res, false);
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, false);
    }

    return 0;
}

static int sec_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    char manu_temp[MAX_DEVICE_MANU_LENGTH] = "SEC_";
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len-3] == 'i') && \
        (panel_data->fw_name[len-2] == 'm') && (panel_data->fw_name[len-1] == 'g')) {
        panel_data->fw_name[len-3] = 'b';
        panel_data->fw_name[len-2] = 'i';
        panel_data->fw_name[len-1] = 'n';
    }
    chip_info->tp_type = panel_data->tp_type;
    strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
    strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", chip_info->tp_type, panel_data->fw_name);

    return 0;
}

static int sec_get_chip_info(void *chip_data)
{
    return 0;
}

static int sec_power_control(void *chip_data, bool enable)
{
   /* int ret = 0;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    TPD_INFO("%s enable :%d\n", __func__, enable);
    if (true == enable) {
        tp_powercontrol_1v8(chip_info->hw_res, true);
        tp_powercontrol_2v8(chip_info->hw_res, true);

        if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
            TPD_INFO("Set the reset_gpio \n");
            gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
        }
        msleep(RESET_TO_NORMAL_TIME);
        sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
        TPD_INFO("%s: write sense on %s\n", __func__, (ret < 0) ? "failed" : "success");
        chip_info->is_power_down = false;

        enable_irq(chip_info->client->irq);
    } else {
        disable_irq_nosync(chip_info->client->irq);

        tp_powercontrol_2v8(chip_info->hw_res, false);
        tp_powercontrol_1v8(chip_info->hw_res, false);

        if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
            TPD_INFO("Set the reset_gpio \n");
            gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
        }
        chip_info->is_power_down = true;
    }*/

    return 0;
}

static fw_check_state sec_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    int ret = 0;
    unsigned char data[5] = { 0 };
    bool valid_fw_integrity = false;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_FIRMWARE_INTEGRITY);  //judge whether fw is right
    if (ret < 0) {
        TPD_INFO("%s: failed to do integrity check (%d)\n", __func__, ret);
    } else {
        if (ret & 0x80) {
            valid_fw_integrity = true;
        } else {
            valid_fw_integrity = false;
            TPD_INFO("invalid firmware integrity (%d)\n", ret);
        }
    }

    data[0] = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    if (data[0] < 0) {
        TPD_INFO("%s: failed to read boot status\n", __func__);
    } else {
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_TS_STATUS, 4, &data[1]);
        if (ret < 0) {
            TPD_INFO("%s: failed to read touch status\n", __func__);
        }
    }
    if ((((data[0] == SEC_STATUS_APP_MODE) && (data[2] == TOUCH_SYSTEM_MODE_FLASH)) || (ret < 0)) && (valid_fw_integrity == false)) {
        TPD_INFO("%s: fw id abnormal, need update\n", __func__);
        return FW_ABNORMAL;
    }

    memset(data, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, data);
    panel_data->TP_FW = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (panel_data->manufacture_info.version)
        sprintf(panel_data->manufacture_info.version, "0x%x", panel_data->TP_FW);

    // ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
    // TPD_INFO("%s: write sense on %s\n", (ret < 0) ? "failed" : "success");
    return FW_NORMAL;
}

static fw_update_state sec_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int i = 0, ret = 0;
    u8 num_chunk;
    u8 buf[4] = {0};
    u8 config_in_bin = 0;
    u8 config_in_ic = 0;
    u8 version_in_ic = 0;
    //u8 version_in_bin = 0;
    u8 vers_in_ic = 0;
    u8 *fd = NULL;
    uint8_t cal_status = 0;
    sec_fw_header *fw_hd = NULL;
    u32 *para_ver;
    u8 fw_status = 0;
    uint32_t fw_version_in_bin = 0, fw_version_in_ic = 0;
    uint32_t config_version_in_bin = 0, config_version_in_ic = 0;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    if (!chip_info) {
        TPD_INFO("Chip info is NULL\n");
        return 0;
    }

    TPD_INFO("%s is called, force update:%d\n", __func__, force);

    fd = (u8 *)(fw->data);
    fw_hd = (sec_fw_header *)(fw->data);
    buf[3] = (fw_hd->img_ver >> 24) & 0xff;
    buf[2] = (fw_hd->img_ver >> 16) & 0xff;
    buf[1] = (fw_hd->img_ver >> 8) & 0xff;
    buf[0] = (fw_hd->img_ver >> 0) & 0xff;
    fw_version_in_bin = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    //version_in_bin = buf[2];
    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, buf);
    fw_version_in_ic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    version_in_ic = buf[2];
    vers_in_ic = buf[3];
    TPD_INFO("img version in bin is 0x%04x, img version in ic is 0x%04x\n", fw_version_in_bin, fw_version_in_ic);

    para_ver = (u32 *)fw_hd + 0x701A;

    buf[3] = (*para_ver >> 24) & 0xff;
    buf[2] = (*para_ver >> 16) & 0xff;
    buf[1] = (*para_ver >> 8) & 0xff;
    buf[0] = (*para_ver >> 0) & 0xff;
    config_version_in_bin = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    config_in_bin = buf[3];
    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_CONFIG_VERSION, 4, buf);
    config_version_in_ic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    config_in_ic = buf[3];
    TPD_INFO("config version in bin is 0x%04x, config version in ic is 0x%04x\n", config_version_in_bin, config_version_in_ic);

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    if (ret == SEC_STATUS_BOOT_MODE) {
        force = 1;
        TPD_INFO("%s: still in bootloader mode, will do force update\n", __func__);
    }

    if (!force) {
        if (fw_version_in_bin == fw_version_in_ic) {
            return FW_NO_NEED_UPDATE;
        }
    }
    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(0);

    num_chunk = fw_hd->chunk_num[0] & 0xff;
    for (i = 0; i < num_chunk; i++) {
        ret = sec_chunk_update(chip_info, 0x00, fw->size, fd);
        if (ret < 0) {
            TPD_INFO("update chunk failed\n");
            goto UPDATE_ERR;
        }
    }

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    if (ret < 0) {
        TPD_INFO("%s: read fail, read boot status = 0x%x\n", __func__, ret);
        goto UPDATE_ERR;
    }

    fw_status = ret;
    if (fw_status != SEC_STATUS_APP_MODE) {
        TPD_INFO("%s: fw update sequence done, BUT read_boot_status = 0x%x\n", __func__, fw_status);
        sec_vrom_reset(chip_info);
    }
    else
        sec_reset(chip_info);

    cal_status = sec_read_calibration_report(chip_info);    //read out calibration result
    /*if (cal_status == 0xFF || ((version_in_bin == 0xD1) && (config_in_bin == 0x51)) || ((vers_in_ic == 0x05) && (version_in_ic == 0xE1))
        || ((version_in_bin == 0xB1) && (config_in_bin == 0x51)) || ((version_in_bin == 0xC1) && (config_in_bin == 0x51))) {*/
    if (cal_status == 0xFF || (config_in_bin !=  config_in_ic) || ((vers_in_ic == 0x05) && (version_in_ic == 0xE1))) {
        TPD_INFO("start calibration.\n");
        while (i < 2) {
            ret = sec_execute_force_calibration(chip_info);
            if (ret < 0) {
                TPD_INFO("calibration failed(%d), try again.\n", i++);
                continue;
            }

            cal_status = sec_read_calibration_report(chip_info);
            if (cal_status != 0x00) {
                TPD_INFO("calibration result(%02X) failed(%d), try again.\n", cal_status, i++);
                continue;
            }

            break;
        }

        TPD_INFO("calibration %s(%02X)\n", (ret < 0) ? "failed" : "success", cal_status);
    }
    sec_reset(chip_info);

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    if (ret < 0) {
        TPD_INFO("%s: read fail, read_boot_status = 0x%x\n", __func__, ret);
        goto UPDATE_ERR;
    }

    fw_status = ret;
    if (fw_status != SEC_STATUS_APP_MODE) {
        TPD_INFO("%s: fw update sequence done, BUT read_boot_status = 0x%x\n", __func__, fw_status);
        goto UPDATE_ERR;
    }

    TPD_INFO("%s: fw update sequence success! read_boot_status = 0x%x\n", __func__, fw_status);
    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(1);

    TPD_INFO("%s: update success\n", __func__);
    return FW_UPDATE_SUCCESS;

UPDATE_ERR:
    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(1);
    return FW_UPDATE_ERROR;
}

static u8 sec_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    int ret = 0;
    int event_id = 0;
    u8 left_event_cnt = 0;
    struct sec_event_status *p_event_status = NULL;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    memset(chip_info->first_event, 0, SEC_EVENT_BUFF_SIZE);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE, chip_info->first_event);
    if (ret < 0) {
        TPD_DETAIL("%s: read one event failed\n", __func__);
        return IRQ_IGNORE;
    }

    TPD_DEBUG("first event: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", \
        chip_info->first_event[0], chip_info->first_event[1], chip_info->first_event[2], chip_info->first_event[3],\
        chip_info->first_event[4], chip_info->first_event[5], chip_info->first_event[6], chip_info->first_event[7]);

    if (chip_info->first_event[0] == 0) {
        TPD_DETAIL("%s: event buffer is empty\n", __func__);
        return IRQ_IGNORE;
    }

    left_event_cnt = chip_info->first_event[7] & 0x3F;
    if ((left_event_cnt > MAX_EVENT_COUNT - 1) || (left_event_cnt == 0xFF)) {
        TPD_INFO("%s: event buffer overflow, do clear the buffer\n", __func__);
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
        if (ret < 0) {
            TPD_INFO("%s: clear event buffer failed\n", __func__);
        }
        return IRQ_IGNORE;
    }

    event_id = chip_info->first_event[0] & 0x3;
    if (event_id == SEC_STATUS_EVENT) {
        /* watchdog reset -> send SENSEON command */
        p_event_status = (struct sec_event_status *)chip_info->first_event;
        if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO) &&
            (p_event_status->status_id == SEC_ACK_BOOT_COMPLETE) && (p_event_status->status_data_1 == 0x20)) {

            ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
            if (ret < 0) {
                TPD_INFO("%s: write sense on failed\n", __func__);
            }
            return IRQ_FW_AUTO_RESET;
        }
        /* palm reject Event -> clear event */
        if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO) && (p_event_status->status_id == SEC_ACK_PALM_MODE)){
            ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
            if (ret < 0) {
                TPD_INFO("%s: clear parm event stack failed\n", __func__);
            }
        }
        /* event queue full-> all finger release */
        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_ERR_EVENT_QUEUE_FULL)) {
            TPD_INFO("%s: IC Event Queue is full! Send Clear Event Queue\n", __func__);
            ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
            if (ret < 0) {
                TPD_INFO("%s: clear parm event stack failed\n", __func__);
            }
            tp_touch_btnkey_release();
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_ERR) && (p_event_status->status_id == SEC_ERR_EVENT_ESD)) {
            TPD_INFO("%s: ESD detected. run reset\n", __func__);
            return IRQ_EXCEPTION;
        }
        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_STATUS_EARDETECTED)) {
            TPD_INFO("%s: EAR detected. \n", __func__);
            return IRQ_IGNORE;
        }
    } else if (event_id == SEC_COORDINATE_EVENT) {
        return IRQ_TOUCH;
    } else if (event_id == SEC_GESTURE_EVENT) {
        return IRQ_GESTURE;
    }

    return IRQ_IGNORE;
}

static int sec_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    int i = 0;
    int t_id = 0;
    int ret = -1;
    int left_event = 0;
    struct sec_event_coordinate *p_event_coord = NULL;
    uint32_t obj_attention = 0;
    u8 event_buff[MAX_EVENT_COUNT][SEC_EVENT_BUFF_SIZE] = { { 0 } };
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    sec_reset_esd(chip_info); //reset irq low state
    p_event_coord = (struct sec_event_coordinate *)chip_info->first_event;
    t_id = (p_event_coord->tid - 1);
    if ((t_id < max_num) && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS) || (p_event_coord->tchsta == SEC_COORDINATE_ACTION_MOVE))) {
        points[t_id].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
        points[t_id].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
        points[t_id].z = p_event_coord->z & 0x3F;
        points[t_id].width_major = p_event_coord->major;
        points[t_id].touch_major = p_event_coord->major;
        points[t_id].status = 1;

        if (points[t_id].z <= 0) {
            points[t_id].z = 1;
        }
        obj_attention = obj_attention | (1 << t_id);    //set touch bit
    }

    left_event = chip_info->first_event[7] & 0x3F;
    if (left_event == 0) {
        return obj_attention;
    } else if (left_event > max_num - 1) {
        TPD_INFO("%s: read left event beyond max touch points\n", __func__);
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
        if (ret < 0) {
            TPD_INFO("%s: clear event buffer failed\n", __func__);
        }
        left_event = max_num - 1;
    }
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ALL_EVENT, sizeof(u8) * (SEC_EVENT_BUFF_SIZE) * (left_event), (u8 *)event_buff[0]);
    if (ret < 0) {
        TPD_INFO("%s: i2c read all event failed\n", __func__);
        return obj_attention;
    }

    for (i = 0; i < left_event; i++) {
        p_event_coord = (struct sec_event_coordinate *)event_buff[i];
        t_id = (p_event_coord->tid - 1);
        if ((t_id < max_num) && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS) || (p_event_coord->tchsta == SEC_COORDINATE_ACTION_MOVE))) {
            points[t_id].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
            points[t_id].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
            points[t_id].z = p_event_coord->z & 0x3F;
            points[t_id].width_major = p_event_coord->major;
            points[t_id].touch_major = p_event_coord->major;
            points[t_id].status = 1;

            if (points[t_id].z <= 0) {
                points[t_id].z = 1;
            }
            obj_attention = obj_attention | (1 << t_id);    //set touch bit

            TPD_DEBUG("event[%d]: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", i, \
                event_buff[i][0], event_buff[i][1], event_buff[i][2], event_buff[i][3], \
                event_buff[i][4], event_buff[i][5], event_buff[i][6], event_buff[i][7]);
        }
    }

    return obj_attention;
}

static int sec_get_gesture_info(void *chip_data, struct gesture_info * gesture)
{
    int i = 0, ret = -1;
    uint8_t coord[18] = {0};
    struct Coordinate limitPoint[4];
    struct sec_gesture_status *p_event_gesture = NULL;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    p_event_gesture = (struct sec_gesture_status *)chip_info->first_event;
    if (p_event_gesture->coordLen > 18) {
        p_event_gesture->coordLen = 18;
    }

    if (p_event_gesture->gestureId == GESTURE_EARSENSE) {
        TPD_DETAIL("earsense gesture: away from panel\n");
        return 0;
    }
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_GESTURE_EVENT, p_event_gesture->coordLen, coord);
    if (ret < 0) {
        TPD_INFO("%s: read gesture data failed\n", __func__);
    }

    if (LEVEL_BASIC != tp_debug) {
        TPD_INFO("gesture points:");
        for (i = 0; i < p_event_gesture->coordLen/3; i++) {
            printk("(%d, %d) ",(coord[3*i] << 4) | ((coord[3*i+2] >> 0) & 0x0F), (coord[3*i+1] << 4) | ((coord[3*i+2] >> 4) & 0x0F));
        }
    }

    switch (p_event_gesture->gestureId)     //judge gesture type
    {
        case GESTURE_RIGHT:
            gesture->gesture_type  = Left2RightSwip;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_LEFT:
            gesture->gesture_type  = Right2LeftSwip;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_DOWN:
            gesture->gesture_type  = Up2DownSwip;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_UP:
            gesture->gesture_type  = Down2UpSwip;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_DOUBLECLICK:
            gesture->gesture_type  = DouTap;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end     = gesture->Point_start;
            break;

        case GESTURE_UP_V:
            gesture->gesture_type  = UpVee;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_DOWN_V:
            gesture->gesture_type  = DownVee;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_LEFT_V:
            gesture->gesture_type = LeftVee;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_RIGHT_V:
            gesture->gesture_type  = RightVee;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            break;

        case GESTURE_O:
            gesture->gesture_type = Circle;
            gesture->clockwise = (p_event_gesture->data == 0) ? 1 : 0;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            limitPoint[0].x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);    //ymin
            limitPoint[0].y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            limitPoint[1].x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);    //xmin
            limitPoint[1].y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            limitPoint[2].x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);   //ymax
            limitPoint[2].y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
            limitPoint[3].x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);  //xmax
            limitPoint[3].y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[15] << 4) | ((coord[17] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[16] << 4) | ((coord[17] >> 0) & 0x0F);
            handleFourCornerPoint(&limitPoint[0], 4);
            gesture->Point_1st = limitPoint[0]; //ymin
            gesture->Point_2nd = limitPoint[1]; //xmin
            gesture->Point_3rd = limitPoint[2]; //ymax
            gesture->Point_4th = limitPoint[3]; //xmax
            break;

        case GESTURE_DOUBLE_LINE:
            gesture->gesture_type  = DouSwip;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_2nd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
            gesture->Point_2nd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
            break;

        case GESTURE_M:
            gesture->gesture_type  = Mgestrue;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            gesture->Point_2nd.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_2nd.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_3rd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
            gesture->Point_3rd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
            break;

        case GESTURE_W:
            gesture->gesture_type  = Wgestrue;
            gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
            gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
            gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
            gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
            gesture->Point_2nd.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
            gesture->Point_2nd.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
            gesture->Point_3rd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
            gesture->Point_3rd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
            gesture->Point_end.x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
            gesture->Point_end.y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
            break;

        default:
            gesture->gesture_type = UnkownGesture;
            break;
    }

    TPD_INFO("%s, gesture_id: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
                __func__, p_event_gesture->gestureId, gesture->gesture_type, gesture->clockwise, \
                gesture->Point_start.x, gesture->Point_start.y, \
                gesture->Point_end.x, gesture->Point_end.y, \
                gesture->Point_1st.x, gesture->Point_1st.y, \
                gesture->Point_2nd.x, gesture->Point_2nd.y, \
                gesture->Point_3rd.x, gesture->Point_3rd.y, \
                gesture->Point_4th.x, gesture->Point_4th.y);

    return 0;
}

static int sec_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    if (chip_info->is_power_down) {
        sec_power_control(chip_info, true);
    }

    switch(mode) {
        case MODE_NORMAL:
            //ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x00);
            ret = 0;
            sec_mdelay(50);
            if (ret < 0) {
                TPD_INFO("%s: power down failed\n", __func__);
            }
            break;

        case MODE_SLEEP:
            ret = sec_power_control(chip_info, false);
            if (ret < 0) {
                TPD_INFO("%s: power down failed\n", __func__);
            }
            break;

        case MODE_GESTURE:
            ret = sec_enable_black_gesture(chip_info, flag);
            TPD_INFO("%s, ret = %d\n", __func__, ret);
            if (ret < 0) {
                TPD_INFO("%s: sec enable gesture failed.\n", __func__);
                return ret;
            }
            break;

        case MODE_EDGE:
            ret = sec_enable_edge_limit(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: sec enable edg limit failed.\n", __func__);
                return ret;
            }
            break;

        case MODE_CHARGE:
            ret = sec_enable_charge_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_EARSENSE:
            ret = sec_enable_earsense_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable earsense mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_PALM_REJECTION:
            ret = sec_enable_palm_reject(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable palm rejection: %d failed\n", __func__, flag);
            }
            break;

        case MODE_GAME:
            ret = sec_enable_game_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable game mode: %d failed\n", __func__, flag);
            }
            break;
        case MODE_HEADSET:
            ret = sec_enable_headset_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable headset mode: %d failed\n", __func__, flag);
            }
            break;

        default:
            TPD_INFO("%s: Wrong mode.\n", __func__);
    }

    last_state = mode;  //update the "last_state" value when the mode change sequence finish

    return ret;
}

//#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
//extern unsigned int upmu_get_rgs_chrdet(void);
//static int sec_get_usb_state(void)
//{
//    return upmu_get_rgs_chrdet();
//}
//#else
//static int sec_get_usb_state(void)
//{
//    return 0;
//}
//#endif

static void sec_black_screen_test(void *chip_data, char *message);

static void sec_reset_esd(struct chip_data_s6d7at0 *chip_info)
{
        chip_info->irq_low_cnt = 0;
        chip_info->esd_stay_cnt = 0;
}

static int sec_esd_handle(void *chip_data)
{
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    if (0 == gpio_get_value(chip_info->hw_res->irq_gpio)) {
        chip_info->irq_low_cnt++;
    } else {
        sec_reset_esd(chip_info);
    }
    if (chip_info->irq_low_cnt >= SEC_TOUCH_IRQ_LOW_CNT) {
        TPD_INFO("do ESD recovery, just clear event buffer\n");
        touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
        tp_touch_btnkey_release();
        /* update low count */
        chip_info->irq_low_cnt = 0;
        chip_info->esd_stay_cnt++;
    }
    if (chip_info->esd_stay_cnt >= SEC_TOUCH_ESD_CNT) {
        TPD_INFO("do ESD recovery, do soft reset\n");
        //touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET, 0, NULL);
        sec_reset(chip_info);
        tp_touch_btnkey_release();
        /* update low count */
        sec_reset_esd(chip_info);
    }

    return 0;
}

static void sec_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

        chip_info->touch_direction = dir;
}

static uint8_t sec_get_touch_direction(void *chip_data)
{
        struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

        return chip_info->touch_direction;
}


static struct oplus_touchpanel_operations sec_ops = {
    .ftm_process                = sec_ftm_process,
    .get_vendor                 = sec_get_vendor,
    .get_chip_info              = sec_get_chip_info,
    .reset                      = sec_reset,
    .power_control              = sec_power_control,
    .fw_check                   = sec_fw_check,
    .fw_update                  = sec_fw_update,
    .trigger_reason             = sec_trigger_reason,
    .get_touch_points           = sec_get_touch_points,
    .get_gesture_info           = sec_get_gesture_info,
    .mode_switch                = sec_mode_switch,
//    .get_usb_state              = sec_get_usb_state,
    .black_screen_test          = sec_black_screen_test,
    .esd_handle                 = sec_esd_handle,
    .set_touch_direction        = sec_set_touch_direction,
    .get_touch_direction        = sec_get_touch_direction,
    .calibrate			= sec_calibrate,
};
/********* End of implementation of oplus_touchpanel_operations callbacks**********************/


/**************** Start of implementation of debug_info proc callbacks************************/
/*int sec_fix_tmode(struct chip_data_s6d7at0 *chip_info, u8 mode, u8 state)
{
    int ret = -1;
    u8 tBuff[2] = { mode, state };

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_STATEMANAGE_ON, STATE_MANAGE_OFF);
    sec_mdelay(20);
    ret |= touch_i2c_write_block(chip_info->client, SEC_CMD_CHG_SYSMODE, sizeof(tBuff), tBuff);
    sec_mdelay(20);

    return ret;
}

int sec_release_tmode(struct chip_data_s6d7at0 *chip_info)
{
    int ret = -1;

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_STATEMANAGE_ON, STATE_MANAGE_ON);
    sec_mdelay(20);

    return ret;
}
*/
static int sec_read_self(struct chip_data_s6d7at0 *chip_info, u8 type, char *data, int len)
{
    int ret = 0;
    unsigned int data_len = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    if (len != data_len) {
        return -1;
    }
/*
    ret = sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    if (ret < 0) {
        TPD_INFO("%s: fix touch mode failed\n", __func__);
        goto err_out;
    }
*/
/*
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SELF_RAW_TYPE, type);
    if (ret < 0) {
        TPD_INFO("%s: Set self type failed\n", __func__);
        goto err_out;
    }

    sec_mdelay(50);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_SELF_RAWDATA, data_len, data);
    if (ret < 0) {
        TPD_INFO("%s: read self failed!\n", __func__);
    }
*/

    /* release data monitory (unprepare AFE data memory) */
/*    ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_SELF_RAW_TYPE, TYPE_INVALID_DATA);
    if (ret < 0) {
        TPD_INFO("%s: Set self type failed\n", __func__);
    }

err_out:
//    ret |= sec_release_tmode(chip_info);
*/
    return ret;
}

static int sec_read_mutual(struct chip_data_s6d7at0 *chip_info, u8 type, char *data, int len)
{
    int ret = 0;
    u8 buf[2] = {0};
    unsigned int data_len = (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) * 2;

    if ((len > data_len) || (len % chip_info->hw_res->TX_NUM != 0)) {
        return -1;
    }
/*
    ret = sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    if (ret < 0) {
        TPD_INFO("%s: fix touch mode failed\n", __func__);
        goto err_out;
    }
*/
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, type);
    if (ret < 0) {
        TPD_INFO("%s: Set mutual type failed\n", __func__);
        goto err_out;
    }

    sec_mdelay(20);
    buf[0] = (u8)((len >> 8) & 0xFF);
    buf[1] = (u8)(len & 0xFF);
//    touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_RAWDATA, len, data);
    if (ret < 0) {
        TPD_INFO("%s: read mutual failed!\n", __func__);
    }

    /* release data monitory (unprepare AFE data memory) */
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_INVALID_DATA);
    if (ret < 0) {
        TPD_INFO("%s: Set mutual type failed\n", __func__);
    }

err_out:
//    ret |= sec_release_tmode(chip_info);
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);

    return ret;
}

static void sec_delta_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, y = 0, z = 0, temp_delta = 0, ret = 0;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    int readbytes = chip_info->hw_res->TX_NUM * (chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_SIGNAL_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read delta failed\n");
        goto kfree_out;
    }

    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
      seq_printf(s, "\n[%2d]", y);
      for (x = chip_info->hw_res->TX_NUM-1; x >= 0; x--) {
          z = chip_info->hw_res->RX_NUM * x + y;
          temp_delta = ((data[z * 2] << 8) | data[z * 2 + 1]);
          seq_printf(s, "%4d, ", temp_delta);
      }
    }

    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_baseline_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, y = 0, z = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    //read decoded data
    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_DECODED_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "DECODED DATA:");
    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
      seq_printf(s, "\n[%2d]", y);
      for (x = chip_info->hw_res->TX_NUM-1; x >= 0; x--) {
          z = chip_info->hw_res->RX_NUM * x + y;
          temp_delta = ((data[z * 2] << 8) | data[z * 2 + 1]);
          seq_printf(s, "%4d, ", temp_delta);
      }
    }


    seq_printf(s, "\n");

    //read ambient data
    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_AMBIENT_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "AMBIENT DATA:");
    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
      seq_printf(s, "\n[%2d]", y);
      for (x = chip_info->hw_res->TX_NUM-1; x >= 0; x--) {
          z = chip_info->hw_res->RX_NUM * x + y;
          temp_delta = ((data[z * 2] << 8) | data[z * 2 + 1]);
          seq_printf(s, "%4d, ", temp_delta);
      }
    }

    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_self_delta_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, rx_offset = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_self(chip_info, TYPE_SIGNAL_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read self delta failed\n");
        goto kfree_out;
    }

    seq_printf(s, "TX:\n");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        temp_delta = (data[x * 2] << 8) | data[x * 2 + 1];
        seq_printf(s, "%4d, ", temp_delta);
    }

    seq_printf(s, "\nRX:\n");
    rx_offset = chip_info->hw_res->TX_NUM * 2;
    for (x = 0; x < chip_info->hw_res->RX_NUM; x++) {
        temp_delta = (data[x * 2 + rx_offset] << 8) | data[x * 2 + 1 + rx_offset];
        seq_printf(s, "%4d, ", temp_delta);
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_self_raw_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, rx_offset = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_self(chip_info, TYPE_RAW_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read self rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "TX:\n");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        temp_delta = (data[x * 2] << 8) | data[x * 2 + 1];
        seq_printf(s, "%4d, ", temp_delta);
    }

    seq_printf(s, "\nRX:\n");
    rx_offset = chip_info->hw_res->TX_NUM * 2;
    for (x = 0; x < chip_info->hw_res->RX_NUM; x++) {
        temp_delta = (data[x * 2 + rx_offset] << 8) | data[x * 2 + 1 + rx_offset];
        seq_printf(s, "%4d, ", temp_delta);
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_main_register_read(struct seq_file *s, void *chip_data)
{
    u8 buf[4] = {0};
    int state = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    memset(buf, 0, 3);
    touch_i2c_read_block(chip_info->client, SEC_READ_ID, 3, buf);
    seq_printf(s, "ID: %02X %02X %02X\n", buf[0], buf[1], buf[2]);

    state = sec_read_calibration_report(chip_info);
    seq_printf(s, "calibration status: 0x%02x\n", state);

    state = sec_get_verify_result(chip_info);
    seq_printf(s, "calibration result: 0x%02x\n", state);

    state = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_TOUCHFUNCTION);
    seq_printf(s, "Charger State: 0x%02X\n", (state & SEC_BIT_SETFUNC_CHARGER));
    seq_printf(s, "Palm State: 0x%02X\n", (state & SEC_BIT_SETFUNC_PALM));
    seq_printf(s, "Headset State: 0x%02X\n", (state & SEC_BIT_SETFUNC_HEADSET));

    state = touch_i2c_read_byte(chip_info->client, SEC_CMD_STOP_FILTER);
    seq_printf(s, "Game mode State: 0x%02X\n", state);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, buf);
    seq_printf(s, "fw img version: 0x%08X\n", (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_CONFIG_VERSION, 4, buf);
    seq_printf(s, "config version: 0x%08X\n", (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_TS_STATUS, 4, buf);
    seq_printf(s, "power mode: 0x%02X[normal-0x02/lpwg-0x05], 0x%02X[idle-0x00/active-0x02]\n", buf[1], buf[3]);
    return;
}

static void sec_reserve_read(struct seq_file *s, void *chip_data)
{
    static int int_state = 1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    if (int_state) {
        int_state = 0;
        touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 1);   //disable interrupt
    } else {
        int_state = 1;
        touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);   //clear event buffer
        touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 0);   //enable interrupt
    }
}

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read         = sec_limit_read,
    .delta_read         = sec_delta_read,
    .self_delta_read    = sec_self_delta_read,
    .baseline_read      = sec_baseline_read,
    .self_raw_read      = sec_self_raw_read,
    .main_register_read = sec_main_register_read,
    .reserve_read       = sec_reserve_read,
};

static void sec_earsese_rawdata_read(void *chip_data, char *rawdata, int read_len)
{
    int ret = 0;
    u8 buf[2] = {0};
    int i = 0, j = 0;
    int8_t tmp_byte[2] = {0};
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    uint8_t len_x = chip_info->hw_res->EARSENSE_TX_NUM;
    uint8_t len_y = chip_info->hw_res->EARSENSE_RX_NUM;

    if ((!chip_info) || (!rawdata))
        return ;

    buf[0] = (u8)((read_len >> 8) & 0xFF);
    buf[1] = (u8)(read_len & 0xFF);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_RAWDATA);
    touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_READ, read_len, rawdata);
    if (ret < 0) {
        TPD_INFO("read rawdata failed\n");
        return;
    }

    for (i = 0; i < len_y; i++) {
        for (j = 0; j < len_x/2; j++) {
            tmp_byte[0] = rawdata[2*(len_x*i+j)];
            tmp_byte[1] = rawdata[2*(len_x*i+j)+1];
            rawdata[2*(len_x*i+j)] = rawdata[2*(len_x*i+len_x-1-j)+1];
            rawdata[2*(len_x*i+j)+1] = rawdata[2*(len_x*i+len_x-1-j)];
            rawdata[2*(len_x*i+len_x-1-j)] = tmp_byte[1];
            rawdata[2*(len_x*i+len_x-1-j)+1] = tmp_byte[0];
        }
    }
    if (len_x%2) {
        j = len_x/2;
        for (i = 0; i < len_y; i++) {
            tmp_byte[0] = rawdata[2*(len_x*i+j)];
            rawdata[2*(len_x*i+j)] = rawdata[2*(len_x*i+j)+1];
            rawdata[2*(len_x*i+j)+1] = tmp_byte[0];
        }
    }
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
    return;
}

static void sec_earsese_delta_read(void *chip_data, char *earsense_delta, int read_len)
{
    int ret = 0, hover_status = 0, data_type = 0;
    u8 buf[2] = {0};
    int i = 0, j = 0;
    int8_t tmp_byte[2] = {0};
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;
    uint8_t len_x = chip_info->hw_res->EARSENSE_TX_NUM;
    uint8_t len_y = chip_info->hw_res->EARSENSE_RX_NUM;

    if (!chip_info)
        return ;

    if (!earsense_delta) {
        TPD_INFO("earsense_delta is NULL\n");
        return;
    }

    buf[0] = (u8)((read_len >> 8) & 0xFF);
    buf[1] = (u8)(read_len & 0xFF);
    hover_status = touch_i2c_read_byte(chip_info->client, SEC_CMD_HOVER_DETECT); //read hover state
    data_type = touch_i2c_read_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE); //read current data type
    if (hover_status && (data_type != TYPE_DATA_DELTA)) {
        touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_DELTA);
        sec_mdelay(20);
    } else if (!hover_status && (data_type != TYPE_SIGNAL_DATA)){
        touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_SIGNAL_DATA);
        sec_mdelay(20);
    }

    touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_DELTA_READ, read_len, earsense_delta);
    if (ret < 0) {
        TPD_INFO("read delta failed\n");
        return;
    }

    for (i = 0; i < len_y; i++) {
        for (j = 0; j < len_x/2; j++) {
            tmp_byte[0] = earsense_delta[2*(len_x*i+j)];
            tmp_byte[1] = earsense_delta[2*(len_x*i+j)+1];
            earsense_delta[2*(len_x*i+j)] = earsense_delta[2*(len_x*i+len_x-1-j)+1];
            earsense_delta[2*(len_x*i+j)+1] = earsense_delta[2*(len_x*i+len_x-1-j)];
            earsense_delta[2*(len_x*i+len_x-1-j)] = tmp_byte[1];
            earsense_delta[2*(len_x*i+len_x-1-j)+1] = tmp_byte[0];
        }
    }
    if (len_x%2) {
        j = len_x/2;
        for (i = 0; i < len_y; i++) {
            tmp_byte[0] = earsense_delta[2*(len_x*i+j)];
            earsense_delta[2*(len_x*i+j)] = earsense_delta[2*(len_x*i+j)+1];
            earsense_delta[2*(len_x*i+j)+1] = tmp_byte[0];
        }
    }
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
    return;
}

static void sec_earsese_selfdata_read( void *chip_data, char *self_data, int read_len)
{
    int i = 0, ret = 0;
    int8_t tmp = 0;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    if ((!chip_info) || (!self_data))
        return ;

    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_SELFDATA_READ, read_len, self_data);
    if (ret < 0) {
        TPD_INFO("read selfdata failed\n");
        return;
    }

    for (i = 0; i < chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM; i++) {
        tmp = self_data[2*i];
        self_data[2*i] = self_data[2*i + 1];
        self_data[2*i + 1] = tmp;
    }
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
    return;
}


static struct earsense_proc_operations earsense_proc_ops = {
    .rawdata_read = sec_earsese_rawdata_read,
    .delta_read   = sec_earsese_delta_read,
    .self_data_read = sec_earsese_selfdata_read,
};
/***************** End of implementation of debug_info proc callbacks*************************/
/*static void sec_swap(u8 *a, u8 *b)
{
    u8 temp = *a;
    *a = *b;
    *b = temp;
}

static void rearrange_sft_result(u8 *data, int length)
{
    int i = 0;

    for(i = 0; i < length; i += 4) {
        sec_swap(&data[i], &data[i + 3]);
        sec_swap(&data[i + 1], &data[i + 2]);
    }
}*/

#define SELFTEST_RESULT_CNT( result, n ) (((result) & (1 << (n))) == 0 ? 0 : 1)
#define SELFTEST_RESULT( result, n ) (((result) & (1 << (n))) == 0 ? "PASS" : "FAIL")
#define I2C_MAX 256
static int sec_execute_selftest(struct seq_file *s, struct chip_data_s6d7at0 *chip_info, struct sec_testdata *sec_testdata)
{
    int ret = -1;
    u8 tData[2];

    sec_reset(chip_info);

    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 1);   //disable interrupt
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_LEVEL, 1);   //disable interrupt
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);   //clear event buffer

    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(0);

    TPD_INFO("%s: Selftest START!\n", __func__);
    tData[0] = 0x1E;
    tData[1] = 0x7f;
    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SELFTEST_CHOICE, sizeof(tData), tData);
    /* execute selftest */
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SELFTEST, 0x03);
    if (ret < 0) {
        TPD_INFO("%s: Execute selftest failed\en", __func__);
        seq_printf(s, "Send selftest cmd failed!\n");
        goto err_out;
    }
    sec_mdelay(4000);

    /* wait for selftest done ack */
    ret = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_SELF_TEST_DONE);
    if (ret < 0) {
        TPD_INFO("%s: Wait for selftest done ack failed\n", __func__);
        //seq_printf(s, "Wait for selftest done ack failed\n");
        goto err_out;
    }
    TPD_INFO("%s: Selftest Done!\n", __func__);

    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(1);

err_out:
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_LEVEL, 0);   //enable interrupt
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 0);   //enable interrupt

    if (chip_info->sec_fw_watchdog_surport)
        ret = set_lsi_tp_watchdog_state(1);

    sec_reset(chip_info);
    return ret;
}

static int sec_execute_lp_selftest(struct chip_data_s6d7at0 *chip_info, struct sec_testdata *sec_testdata)
{
    int ret = -1;
    u8 tBuff[] = { TOUCH_SYSTEM_MODE_LOWPOWER, TOUCH_MODE_STATE_TOUCH};

    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 1);   //disable interrupt
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_LEVEL, 1);   //disable interrupt
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);   //clear event buffer
    touch_i2c_write_block(chip_info->client, SEC_CMD_CHG_SYSMODE, sizeof(tBuff), tBuff);

    TPD_INFO("%s: LP Selftest START!\n", __func__);
    /* execute selftest */
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_LP_SELFTEST, 0x03);
    if (ret < 0) {
        TPD_INFO("%s: Execute lp selftest failed\en", __func__);
        goto err_out;
    }
    sec_mdelay(3000);

    /* wait for lp selftest done ack */
    ret = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_SELF_TEST_DONE);
    if (ret < 0) {
        TPD_INFO("%s: Wait for lp selftest done ack failed\n", __func__);
        //seq_printf(s, "Wait for selftest done ack failed\n");
        goto err_out;
    }
    TPD_INFO("%s: LP Selftest Done!\n", __func__);

err_out:
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_LEVEL, 0);   //enable interrupt
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 0);   //enable interrupt
    return ret;
}
static void store_to_file(int fd, char* format, ...)
{
    va_list args;
    char buf[120] = {0};

    va_start(args, format);
    vsnprintf(buf, 120, format, args);
    va_end(args);

    if(fd >= 0) {
        sys_write(fd, buf, strlen(buf));
    }
}

static void sec_print_selftest_frame(struct chip_data_s6d7at0 *chip_info, int test_type, struct sec_testdata *sec_testdata, short *frame)
{
    int i = 0, j = 0;
    int gap_para_x;
    int gap_para_y;
    unsigned char *pStr = NULL;
    unsigned char pTmp[16] = { 0 };

    if (test_type == TOUCH_SELFTEST_ITEM_RAW_VAR_X) {
        gap_para_x = sec_testdata->RX_NUM - 1;
        gap_para_y = sec_testdata->TX_NUM;
    } else if (test_type == TOUCH_SELFTEST_ITEM_RAW_VAR_Y) {
        gap_para_x = sec_testdata->RX_NUM;
        gap_para_y = sec_testdata->TX_NUM - 1;
    } else {
        gap_para_x = sec_testdata->RX_NUM;
        gap_para_y = sec_testdata->TX_NUM;
    }

    pStr = kzalloc(6 * (gap_para_y + 1), GFP_KERNEL);
    if (pStr == NULL)
        return;

    memset(pStr, 0x0, 6 * (gap_para_y + 1));
    for (i = 0; i < gap_para_y; i++) {
      snprintf(pTmp, sizeof(pTmp), ",Tx%02d", (gap_para_y - 1 - i));
      strlcat(pStr, pTmp, 6 * (gap_para_y + 1));
    }
    TPD_INFO("%s\n", pStr);
    store_to_file(sec_testdata->fd, "%s\n", pStr);

    for (i = 0; i < gap_para_x; i++) {
        memset(pStr, 0x0, 6 * (gap_para_y + 1));
        snprintf(pTmp, sizeof(pTmp), "Rx%02d", i);
        strlcat(pStr, pTmp, 6 * (gap_para_x + 1));

        for (j = 0; j < gap_para_y; j++) {
            snprintf(pTmp, sizeof(pTmp), ",%3d", frame[((gap_para_y - 1 - j) * gap_para_x) + i]);
            strlcat(pStr, pTmp, 6 * (gap_para_y + 1));
        }
        TPD_INFO("%s\n", pStr);
        store_to_file(sec_testdata->fd, "%s\n", pStr);
    }
    store_to_file(sec_testdata->fd, "\n");
    kfree(pStr);
}

static int sec_read_flash(struct chip_data_s6d7at0 * chip_info, u32 mem_addr, u8 *buff, int mem_size)
{
    int ret;
    u8 cmd[5];
    u8 *data;

    if (mem_size >= 64 * 1024) {
        TPD_INFO("%s: mem size over 64K\n", __func__);
        return -EIO;
    }

    sec_ts_flash_cs_control(chip_info, CS_LOW);

    cmd[0] = (u8)SEC_CMD_FLASH_SEND_DATA;
    cmd[1] = 0x03;
    cmd[2] = (u8)((mem_addr >> 16) & 0xff);
    cmd[3] = (u8)((mem_addr >> 8) & 0xff);
    cmd[4] = (u8)((mem_addr >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 5);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }

    //udelay(10);
    sec_mdelay(10);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_SIZE;
    cmd[1] = (u8)((mem_size >> 8) & 0xff);
    cmd[2] = (u8)((mem_size >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 3);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }

    //udelay(10);
    sec_mdelay(10);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_DATA;
    data = buff;

    ret = touch_i2c_read(chip_info->client, cmd, 1, data, mem_size);
    if (ret < 0) {
        TPD_INFO("%s: memory read failed\n", __func__);
        return -EIO;
    }
    sec_mdelay(10);

    sec_ts_flash_cs_control(chip_info, CS_HIGH);

    return 0;
}

static int sec_print_selftest_report(struct chip_data_s6d7at0 *chip_info, struct sec_testdata *sec_testdata, int test_type, u8 test_result)
{
    int ret = -1; int i = 0;
    u32 addr = SEC_FLASH_SELFTEST_REPORT_ADDR;
    u8 *frame_buff = NULL;
    short *print_buff = NULL;
    int read_bytes;

    read_bytes = sec_testdata->TX_NUM *sec_testdata->RX_NUM * 2;

    frame_buff = kzalloc(read_bytes, GFP_KERNEL);
    if (!frame_buff)
        goto err_out;

    print_buff = kzalloc(read_bytes, GFP_KERNEL);
    if (!print_buff)
        goto err_out;

    switch(test_type) {
    case TOUCH_SELFTEST_ITEM_SENSOR_UNI:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR;
        TPD_INFO("%s: Sensor Uniformity: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: Sensor Uniformity: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_RAW_VAR_X:
        read_bytes /= 2;
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 10);
        TPD_INFO("%s: Raw Variance X: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: Raw Variance X: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_RAW_VAR_Y:
        read_bytes /= 2;
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 11);
        TPD_INFO("%s: Raw Variance Y: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: Raw Variance Y: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_P2P_MIN:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 6);
        TPD_INFO("%s: P2P min: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: P2P min: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_P2P_MAX:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 8);
        TPD_INFO("%s: P2P max: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: P2P max: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_OPEN:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 4);
        TPD_INFO("%s: Open: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: Open: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_SHORT:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 2);
        TPD_INFO("%s: Short: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        store_to_file(sec_testdata->fd, "%s: Short: %s\n", __func__, SELFTEST_RESULT(test_result, test_type));
        break;
    case TOUCH_SELFTEST_ITEM_OFFSET:
        read_bytes /= 2;
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 14);
        TPD_INFO("%s: Offset\n", __func__);
        store_to_file(sec_testdata->fd, "%s: Offset\n", __func__);
        break;
    case TOUCH_SELFTEST_ITEM_RAWDATA:
        addr = SEC_FLASH_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_MAX_SIZE * 12);
        TPD_INFO("%s: RawData\n", __func__);
        store_to_file(sec_testdata->fd, "%s: RawData\n", __func__);
        break;
    case TOUCH_SELFTEST_ITEM_LP_RAWDATA:
        addr = SEC_FLASH_LP_SELFTEST_REPORT_ADDR + (SELFTEST_FRAME_SIZE * 2);
        TPD_INFO("%s: LP RawData: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        store_to_file(sec_testdata->fd, "%s: LP RawData: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        break;
    case TOUCH_SELFTEST_ITEM_LP_P2P_MIN:
        addr = SEC_FLASH_LP_SELFTEST_REPORT_ADDR;
        TPD_INFO("%s: LP P2P min: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        store_to_file(sec_testdata->fd, "%s: LP P2P min: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        break;
    case TOUCH_SELFTEST_ITEM_LP_P2P_MAX:
        addr = SEC_FLASH_LP_SELFTEST_REPORT_ADDR + SELFTEST_FRAME_SIZE;
        TPD_INFO("%s: LP P2P max: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        store_to_file(sec_testdata->fd, "%s: LP P2P max: %s\n", __func__, SELFTEST_RESULT(test_result, test_type - 10));
        break;
    }

    ret = sec_read_flash(chip_info, addr, frame_buff, read_bytes);
    if (ret < 0) {
        TPD_INFO("%s: Read frame data failed. test type:%d\n", __func__, test_type);
        goto err_out;
    }

    if (read_bytes == sec_testdata->TX_NUM *sec_testdata->RX_NUM) {
        for (i = 0; i < read_bytes; i++)
            print_buff[i] = frame_buff[i];
    } else {
        for (i = 0; i < read_bytes; i += 2)
            print_buff[i / 2] = frame_buff[i] + (frame_buff[i + 1] << 8);
    }

    sec_print_selftest_frame(chip_info, test_type, sec_testdata, print_buff);
    ret = SELFTEST_RESULT_CNT(test_result, test_type >= 10 ? test_type - 10 : test_type);

err_out:
    if (frame_buff) {
        kfree(frame_buff);
        frame_buff = NULL;
    }

    if (print_buff) {
        kfree(print_buff);
        print_buff = NULL;
    }
    return ret;
}

static int sec_read_selftest_report(struct chip_data_s6d7at0 *chip_info, struct sec_testdata *sec_testdata)
{
    int ret = 0;
    int i = 0;
    int err_cnt = 0;
    u32 addr = SEC_FLASH_SELFTEST_REPORT_ADDR;
    u8 report_buff[4] = {1, 2, 3, 4};

    /* read fw version */
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, report_buff);
    if (ret < 0) {
        TPD_INFO("%s: read FW version failed\n", __func__);
        return ret;
    }
    TPD_INFO("%s: FW version: %02X%02X%02X%02X\n", __func__, report_buff[0], report_buff[1], report_buff[2], report_buff[3]);
    store_to_file(sec_testdata->fd, "FW version: %02X%02X%02X%02X\n", report_buff[0], report_buff[1], report_buff[2], report_buff[3]);

    /* read selftest result */
    addr = SEC_FLASH_SELFTEST_REPORT_ADDR - 4;
    for (i = 0; i < 2; i++) {
        ret = sec_read_flash(chip_info, addr, report_buff, 4);
        if (ret < 0) {
            TPD_INFO("%s: read selftest report failed\n", __func__);
            return ret;
        }
    }
    TPD_INFO("%s: Test Report: %02X%02X%02X%02X\n\n", __func__, report_buff[0], report_buff[1], report_buff[2], report_buff[3]);
    store_to_file(sec_testdata->fd, "%s: Test Report: %02X%02X%02X%02X\n\n", __func__, report_buff[0], report_buff[1], report_buff[2], report_buff[3]);

    /* print each result */
    for (i = 0; i< 10; i++) {
        ret = sec_print_selftest_report(chip_info, sec_testdata, i, report_buff[0]);
        if (ret < 0) {
            TPD_INFO("%s: Print selftest report %d failed\n", __func__, i);
            return ret;
        }
        err_cnt += ret;
    }

    if ((report_buff[0] & 0xff) == 0) {
        TPD_INFO("%s: TEST ALL PASSED!\n", __func__);
        store_to_file(sec_testdata->fd, "%s: TEST ALL PASSED!\n", __func__);
    } else {
    TPD_INFO("%s: Test Result FAIL... Sensor_Uniformity: %s, Raw_Variance_X: %s, Raw_Variance_Y: %s, P2P_min: %s, P2P_max: %s, Open: %s, Short: %s, High-Z: %s\n",
              __func__, SELFTEST_RESULT(report_buff[0], 0), SELFTEST_RESULT(report_buff[0], 1), SELFTEST_RESULT(report_buff[0], 2), SELFTEST_RESULT(report_buff[0], 3),
            SELFTEST_RESULT(report_buff[0], 4), SELFTEST_RESULT(report_buff[0], 5), SELFTEST_RESULT(report_buff[0], 6), SELFTEST_RESULT(report_buff[0], 7));
    store_to_file(sec_testdata->fd, "%s: Test Result FAIL... Sensor_Uniformity: %s Raw_Variance_X: %s Raw_Variance_Y: %s P2P_min: %s P2P_max: %s Open: %s Short: %s High-Z: %s\n",
              __func__, SELFTEST_RESULT(report_buff[0], 0), SELFTEST_RESULT(report_buff[0], 1), SELFTEST_RESULT(report_buff[0], 2), SELFTEST_RESULT(report_buff[0], 3),
            SELFTEST_RESULT(report_buff[0], 4), SELFTEST_RESULT(report_buff[0], 5), SELFTEST_RESULT(report_buff[0], 6), SELFTEST_RESULT(report_buff[0], 7));
    }

    return err_cnt;
}

static int sec_read_lp_selftest_report(struct chip_data_s6d7at0 *chip_info, struct sec_testdata *sec_testdata)
{
    int ret = 0;
    int i = 0;
    int err_cnt = 0;
    u32 addr = SEC_FLASH_LP_SELFTEST_REPORT_ADDR;
    u8 report_buff[4] = {1, 2, 3, 4};

    /* read fw version */
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, report_buff);
    if (ret < 0) {
        TPD_INFO("%s: read FW version failed\n", __func__);
        return ret;
    }
    TPD_INFO("%s: FW version: %02X%02X%02X%02X\n", __func__, report_buff[0], report_buff[1], report_buff[2], report_buff[3]);
    store_to_file(sec_testdata->fd, "FW version: %02X%02X%02X%02X\n", report_buff[0], report_buff[1], report_buff[2], report_buff[3]);

    sec_ts_flash_set_datanum(chip_info, 0xffff);
    sec_mdelay(200);

    /* read selftest result */
    addr = SEC_FLASH_LP_SELFTEST_REPORT_ADDR - 2;
    for (i = 0; i < 2; i++) {
        ret = sec_block_read(chip_info, addr, 2, report_buff);
        if (ret < 0) {
            TPD_INFO("%s: read \n", __func__);
            return ret;
        }
    }
    TPD_INFO("%s: LP Test Report: %02X%02X\n\n", __func__, report_buff[0], report_buff[1]);
    store_to_file(sec_testdata->fd, "%s: LP Test Report: %02X%02X\n\n", __func__, report_buff[0], report_buff[1]);

    /* print each result */
    for (i = 10; i< 13; i++) {
        ret = sec_print_selftest_report(chip_info, sec_testdata, i, report_buff[0]);
        if (ret < 0) {
            TPD_INFO("%s: Print selftest report %d failed\n", __func__, i);
            return ret;
        }
        err_cnt += ret;
    }

    if ((report_buff[0] & 0xff) == 0) {
        TPD_INFO("%s: TEST ALL PASSED!\n", __func__);
        store_to_file(sec_testdata->fd, "%s: TEST ALL PASSED!\n", __func__);
    } else {
        TPD_INFO("%s: LP Test Result FAIL... RawData: %s, P2P_min: %s, P2P_max: %s\n",
            __func__, SELFTEST_RESULT(report_buff[0], 0), SELFTEST_RESULT(report_buff[0], 1), SELFTEST_RESULT(report_buff[0], 2));
        store_to_file(sec_testdata->fd, "%s: LP Test Result FAIL... RawData: %s, P2P_min: %s, P2P_max: %s\n",
            __func__, SELFTEST_RESULT(report_buff[0], 0), SELFTEST_RESULT(report_buff[0], 1), SELFTEST_RESULT(report_buff[0], 2));
    }

    return err_cnt;
}

/*static uint32_t search_for_item(const struct firmware *fw, int item_cnt, uint8_t item_index)
{
    int i = 0;
    uint32_t item_offset = 0;
    struct sec_test_item_header *item_header = NULL;
    uint32_t *p_item_offset = (uint32_t *)(fw->data + 16);

    for (i = 0; i < item_cnt; i++) {
        item_header = (struct sec_test_item_header *)(fw->data + p_item_offset[i]);
        if (item_header->item_bit == item_index) {      //check the matched item offset
            item_offset = p_item_offset[i];
        }
    }

    return item_offset;
}*/

static int sec_run_mis_cal(struct chip_data_s6d7at0 *chip_info)
{
    int ret = -1;

    TPD_INFO("%s: Mis cal Check\n", __func__);
    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_MIS_CAL_CHECK, 0, NULL);    //set to verify calibration
    sec_mdelay(200);
    return ret;
}

static void sec_auto_test(struct seq_file *s, void *chip_data, struct sec_testdata *sec_testdata)
{
    int ret = -1;
    int eint_status = 0, eint_count = 0, read_gpio_num = 0;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0*)chip_data;
    int err_cnt = 0;
    u8 tBuff[SEC_EVENT_BUFF_SIZE] = {0,};
    u8 MASK_2_BITS = 3;
    /*  check if screen has any finger or palm touch */
    do {
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE, tBuff);
        TPD_INFO("%s: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
            __func__, tBuff[0], tBuff[1], tBuff[2], tBuff[3], tBuff[4], tBuff[5], tBuff[6], tBuff[7]);
        if (tBuff[0] == 0x00) {
            break;
        } else if ((tBuff[0] & MASK_2_BITS) == SEC_COORDINATE_EVENT || ((tBuff[0] & MASK_2_BITS) == SEC_STATUS_EVENT
            && ((tBuff[0] >> 2) & 0xF) == TYPE_STATUS_EVENT_INFO && tBuff[1] == SEC_ACK_PALM_MODE)) {
            TPD_INFO("%s: FAIL: DO NOT TOUCH SCREEN!\n", __func__);
            seq_printf(s, "FAIL: DO NOT TOUCH SCREEN!\n");
            return;
        }
    } while(ret > 0);

  /* interrupt pin short test */
    read_gpio_num = 10;
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 1);   //disable interrupt
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);   //clear event buffer
    while (read_gpio_num--) {
        sec_mdelay(5);
        eint_status = gpio_get_value(sec_testdata->irq_gpio);
        if (eint_status == 1) {
            eint_count--;
        } else {
            eint_count++;
        }
    }
    TPD_INFO("%s: TP EINT PIN direct short test! eint_count = %d\n", __func__, eint_count);
    if (eint_count == 10) {
        TPD_INFO("%s: error :  TP EINT PIN direct short!\n", __func__);
        seq_printf(s, "eint_status is low, TP EINT direct short\n");
    }
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 0);   //enable interrupt

    ret = sec_execute_force_calibration(chip_info);
    if (ret < 0) {
        TPD_INFO("%s: Calibration failed\n", __func__);
        seq_printf(s, "Calibration failed\n");
        return;
    }

    /* get mis cal result */
    ret = sec_get_verify_result(chip_info);
    if (ret != 0) {
        TPD_INFO("%s: Mis-Calibration result failed(0x%02x)\n", __func__, ret);
        seq_printf(s, "Mis-Calibration result failed(0x%02x)\n", ret);
        return;
    }

    /* execute selftest */
    ret = sec_execute_selftest(s, chip_info, sec_testdata);
    if (ret < 0) {
        TPD_INFO("%s: execute selftest failed\n", __func__);
        return;
    }
    sec_mdelay(500);

    /* read and print selftest report */
    ret = sec_read_selftest_report(chip_info, sec_testdata);
    if (ret < 0) {
        TPD_INFO("%s: Read selftest report failed\n", __func__);
        return;
    }

    err_cnt += ret;
    TPD_INFO("%d error(s). %s\n", err_cnt, err_cnt?"":"All Test Passed.");
    seq_printf(s, "%d error(s). %s\n", err_cnt, err_cnt?"":"All Test Passed.");
}

static void sec_black_screen_test(void *chip_data, char *message)
{
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    int err_cnt = 0;
    mm_segment_t old_fs;
    uint8_t data_buf[128];
    int fd = -1, ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0*) chip_data;

    struct sec_testdata sec_testdata =
    {
        .TX_NUM = 0,
        .RX_NUM = 0,
        .fd = -1,
        .irq_gpio = -1,
        .TP_FW = 0,
        .fw = NULL,
        .test_item = 0,
    };

    // step 1: get mutex locked
    disable_irq_nosync(chip_info->client->irq);

    //step2: create a file to store test data in /sdcard/Tp_Test
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOff/tp_testlimit_%02d%02d%02d-%02d%02d%02d-utc.csv",
        (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
        rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    sys_mkdir("/sdcard/TpTestReport", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", data_buf);
        err_cnt++;
        goto out;
    }

    //init sec_testdata
    sec_testdata.fd = fd;
    sec_testdata.TX_NUM = chip_info->hw_res->TX_NUM;
    sec_testdata.RX_NUM = chip_info->hw_res->RX_NUM;
    sec_testdata.irq_gpio = chip_info->hw_res->irq_gpio;

    //step3: execute lp selftest
    ret = sec_execute_lp_selftest(chip_info, &sec_testdata);
    if (ret < 0) {
        TPD_INFO("%s: execute lp selftest failed\n", __func__);
        err_cnt++;
        goto out;
    }
    sec_mdelay(500);

    //step4: read and print lp selftest report
    ret = sec_read_lp_selftest_report(chip_info, &sec_testdata);
    if (ret < 0) {
        TPD_INFO("%s: Read selftest report failed\n", __func__);
        err_cnt++;
        goto out;
    }

    err_cnt += ret;

out:
    /* run mis cal */
    ret = sec_run_mis_cal(chip_info);
    if (ret < 0) {
        TPD_INFO("%s: Write Mis Cal command Failed!\n", __func__);
        sprintf(message, "Write Mis Cal command Failed!\n");
        goto out_fd;
    }

    /* get mis cal result */
    ret = sec_get_verify_result(chip_info);
    if (ret != 0) {
        TPD_INFO("%s: Mis-Calibration result failed(0x%02x)\n", __func__, ret);
        sprintf(message, "Mis-Calibration result failed(0x%02x)\n", ret);
        goto out_fd;
    }

out_fd:
    if (fd >= 0) {
        sys_close(fd);
        set_fs(old_fs);
    }
    enable_irq(chip_info->client->irq);
    TPD_INFO("%d error(s). %s\n", err_cnt, err_cnt?"":"All Test Passed.");
    sprintf(message, "%d error(s). %s\n", err_cnt, err_cnt?"":"All Test Passed.");
    return;
}

static int sec_get_verify_result(struct chip_data_s6d7at0 *chip_info)
{
    int ret = -1;

    ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_MIS_CAL_READ);
    //sec_release_tmode(chip_info);
    TPD_INFO("%s: Mis cal Read: %02X", __func__, ret);

    return ret;
}

static void sec_calibrate(struct seq_file *s, void *chip_data)
{
    int ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    //sec_reset(chip_info);
    ret = sec_execute_force_calibration(chip_info);
    if (ret < 0) {
        seq_printf(s, "1 error, calibration failed\n");
    } else {
        seq_printf(s, "0 error, calibration successed\n");
    }

    return;
}

static void sec_verify_calibration(struct seq_file *s, void *chip_data)
{
    int ret = -1;
    struct chip_data_s6d7at0 *chip_info = (struct chip_data_s6d7at0 *)chip_data;

    ret = sec_get_verify_result(chip_info);
    if (ret != 0) {
        seq_printf(s, "1 error, verify calibration result failed(0x%02x)\n", ret);
    } else {
        seq_printf(s, "0 error, verify calibration result successed\n");
    }

    return;
}

static struct sec_proc_operations sec_proc_ops = {
    .auto_test          = sec_auto_test,
    .verify_calibration = sec_verify_calibration,
};

/*********** Start of I2C Driver and Implementation of it's callbacks*************************/
static int sec_tp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct chip_data_s6d7at0 *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    unsigned char data[8] = { 0 };
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    /* 1. alloc chip info */
    chip_info = kzalloc(sizeof(struct chip_data_s6d7at0), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(chip_info, 0, sizeof(*chip_info));

    /* 2. Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }
    memset(ts, 0, sizeof(*ts));

    /* 3. bind client and dev for easy operate */
    chip_info->client = client;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->client = client;
    ts->irq = client->irq;
    i2c_set_clientdata(client, ts);
    ts->dev = &client->dev;
    ts->chip_data = chip_info;
    chip_info->hw_res = &ts->hw_res;
    chip_info->touch_direction = VERTICAL_SCREEN;
    //ts->ic_name = TPD_DEVICE;

    /* 4. file_operations callbacks binding */
    ts->ts_ops = &sec_ops;
    ts->earsense_ops = &earsense_proc_ops;

    /* 5. register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    //ts->tp_resume_order = LCD_TP_RESUME;
    ts->tp_suspend_order = TP_LCD_SUSPEND;
    //ts->mode_switch_type = SINGLE;
    chip_info->edge_limit_support = ts->edge_limit_support;
    chip_info->fw_edge_limit_support = ts->fw_edge_limit_support;
    chip_info->sec_fw_watchdog_surport = of_property_read_bool(ts->dev->of_node, "sec_fw_watchdog_surport");

    //reset esd handle time interval
    if (ts->esd_handle_support) {
        sec_reset_esd(chip_info);
        ts->esd_info.esd_work_time = msecs_to_jiffies(SEC_TOUCH_ESD_CHECK_PERIOD); // change esd check interval to 0.2s
        TPD_INFO("%s:change esd handle time to %d ms\n", __func__, ts->esd_info.esd_work_time/HZ);
    }

    /* check touch firmware status*/
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ID, 3, &data[0]);
    if (ret < 0)
        TPD_INFO("%s: failed to read device ID(%d)\n", __func__, ret);
    else
        TPD_INFO("%s: TOUCH DEVICE ID : %02X, %02X, %02X\n", __func__, data[0], data[1], data[2]);

    ret = touch_i2c_read_block(chip_info->client, SEC_READ_BOOT_STATUS, 1, &data[0]);
    if (ret < 0)
        TPD_INFO("%s: failed to read touch status(%d)\n", __func__, ret);
    else
        TPD_INFO("%s: TOUCH BOOT STATUS: %02X\n", __func__,data[0]);

    ret = touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 8, &data[0]);
    if (ret < 0)
        TPD_INFO("%s: failed to read FW version(%d)\n", __func__, ret);
    else
        TPD_INFO("%s: TOUCH FW VERSION: %02X.%02X.%02X.%02X\n", __func__,data[0], data[1], data[2], data[3]);
    sec_reset(chip_info);

    /* 6. create debug interface*/
    sec_raw_device_init(ts);
    sec_create_proc(ts, &sec_proc_ops);
    TPD_INFO("%s, probe normal end\n", __func__);
    return 0;

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

ts_malloc_failed:
    kfree(chip_info);
    chip_info = NULL;
    ret = -1;

    TPD_INFO("%s, probe error\n", __func__);
    return ret;
}

static int sec_tp_remove(struct i2c_client *client)
{
    struct touchpanel_data *ts = i2c_get_clientdata(client);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int sec_i2c_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called\n", __func__);
    tp_i2c_suspend(ts);

    return 0;
}

static int sec_i2c_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);
    tp_i2c_resume(ts);

    return 0;
}

static const struct i2c_device_id tp_id[] =
{
    {TPD_DEVICE, 0},
    {},
};

static struct of_device_id tp_match_table[] =
{
    { .compatible = TPD_DEVICE, },
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = sec_i2c_suspend,
    .resume = sec_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver =
{
    .probe = sec_tp_probe,
    .remove = sec_tp_remove,
    .id_table = tp_id,
    .driver = {
        .name = TPD_DEVICE,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
};
/******************* End of I2C Driver and It's dev_pm_ops***********************/

/***********************Start of module init and exit****************************/
static int __init tp_driver_init(void)
{
    TPD_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE))
       return -1;

    if (i2c_add_driver(&tp_i2c_driver)!= 0) {
        TPD_INFO("unable to add i2c driver.\n");
        return -1;
    }
    return 0;
}

static void __exit tp_driver_exit(void)
{
    i2c_del_driver(&tp_i2c_driver);
}

module_init(tp_driver_init);
module_exit(tp_driver_exit);
/***********************End of module init and exit*******************************/

MODULE_AUTHOR("Samsung Driver");
MODULE_DESCRIPTION("Samsung Electronics TouchScreen driver");
MODULE_LICENSE("GPL v2");
