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

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include <linux/spi/spi.h>


#include "hx83112a_noflash.h"

#define OPLUS17001TRULY_TD4322_1080P_CMD_PANEL 29

/*******Part0:LOG TAG Declear********************/
#define TPD_DEVICE "himax,hx83112a_nf"
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

struct hx83112a_nf_report_data *hx83112a_nf_touch_data;
struct chip_data_hx83112a_nf *hx83112a_nf_chip_info;
int hx83112a_nf_touch_data_size = 128;
int HX83112A_NF_HW_RESET_ACTIVATE = 0;
static int HX83112A_NF_TOUCH_INFO_POINT_CNT   = 0;
int hx83112a_nf_lcd_vendor = 0;
int hx83112a_nf_irq_en_cnt = 0;

int hx83112a_nf_1kind_raw_size = 0;
uint32_t hx83112a_nf_rslt_data_len;
char *hx83112a_nf_rslt_data;
int **hx83112a_nf_inspection_criteria;
int *hx83112a_nf_inspt_crtra_flag;
int HX83112A_NF_CRITERIA_ITEM = 4;
int HX83112A_NF_CRITERIA_SIZE;
char *hx83112a_nf_file_path_OK;
char *hx83112a_nf_file_path_NG;
bool hx83112a_nf_isEA006proj = false;
bool hx83112a_nf_isBD12proj = false;
bool hx83112a_nf_isRead_csv = true;
int hx83112a_nf_fail_write_count;

#ifdef HX_ZERO_FLASH
extern int hx83112a_nf_f_0f_updat;
/* 128k+ */
int hx83112a_nf_cfg_crc = -1;
int hx83112a_nf_cfg_sz;
uint8_t hx83112a_nf_sram_min[4];
unsigned char *hx83112a_nf_FW_buf;
/* 128k- */
#endif

int hx83112a_nf_check_point_format;
unsigned char hx83112a_nf_switch_algo;
uint8_t HX83112A_NF_HX_PROC_SEND_FLAG;


/*******Part0: SPI Interface***************/
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
const struct mtk_chip_config hx83112a_nf_hx_spi_ctrdata = {
    .rx_mlsb = 1,
    .tx_mlsb = 1,
    .cs_pol = 0,
};
#else
const struct mt_chip_conf hx83112a_nf_hx_spi_ctrdata = {
    .setuptime = 25,
    .holdtime = 25,
    .high_time = 3, /* 16.6MHz */
    .low_time = 3,
    .cs_idletime = 2,
    .ulthgh_thrsh = 0,

    .cpol = 0,
    .cpha = 0,

    .rx_mlsb = 1,
    .tx_mlsb = 1,

    .tx_endian = 0,
    .rx_endian = 0,

    .com_mod = DMA_TRANSFER,

    .pause = 0,
    .finish_intr = 1,
    .deassert = 0,
    .ulthigh = 0,
    .tckdly = 0,
};
#endif

#if !defined(CONFIG_VMAP_STACK) || defined(CONFIG_TOUCHPANEL_MTK_PLATFORM)  // For Checksum Fail On MTK Platform
static ssize_t hx83112a_nf_spi_sync(struct touchpanel_data *ts, struct spi_message *message)
{
    int status;

    status = spi_sync(ts->s_client, message);

    if (status == 0) {
        status = message->status;
        if (status == 0)
            status = message->actual_length;
    }
    return status;
}

static int hx83112a_nf_spi_read(uint8_t *command, uint8_t command_len, uint8_t *data, uint32_t length, uint8_t toRetry)
{
    struct spi_message message;
    struct spi_transfer xfer[2];
    int retry = 0;
    int error = -1;

    spi_message_init(&message);
    memset(xfer, 0, sizeof(xfer));

    xfer[0].tx_buf = command;
    xfer[0].len = command_len;
    spi_message_add_tail(&xfer[0], &message);

    xfer[1].rx_buf = data;
    xfer[1].len = length;
    spi_message_add_tail(&xfer[1], &message);

    for (retry = 0; retry < toRetry; retry++) {
        error = spi_sync(hx83112a_nf_pri_ts->s_client, &message);
        if (error) {
            TPD_INFO("SPI read error: %d\n", error);
        } else {
            break;
        }
    }
    if (retry == toRetry) {
        TPD_INFO("%s: SPI read error retry over %d\n",
                 __func__, toRetry);
        return -EIO;
    }

    return 0;
}

static int hx83112a_nf_spi_write(uint8_t *buf, uint32_t length)
{

    struct spi_transfer t = {
        .tx_buf = buf,
        .len = length,
    };
    struct spi_message    m;
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    return hx83112a_nf_spi_sync(hx83112a_nf_pri_ts, &m);

}
#endif // end of !defined(CONFIG_VMAP_STACK）|| defined(CONFIG_TOUCHPANEL_MTK_PLATFORM)

static int hx83112a_nf_bus_read(uint8_t command, uint32_t length, uint8_t *data)
{
    int result = 0;
    uint8_t spi_format_buf[3];

    mutex_lock(&(hx83112a_nf_chip_info->spi_lock));
    spi_format_buf[0] = 0xF3;
    spi_format_buf[1] = command;
    spi_format_buf[2] = 0x00;
#ifdef CONFIG_VMAP_STACK
    #ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM   // For Checksum Fail On MTK Platform
       result = hx83112a_nf_spi_read(&spi_format_buf[0], 3, data, length, 10);
    #else
       result = spi_write_then_read(hx83112a_nf_pri_ts->s_client, &spi_format_buf[0], 3, data, length);
    #endif   //end of CONFIG_TOUCHPANEL_MTK_PLATFORM
#else
    result = hx83112a_nf_spi_read(&spi_format_buf[0], 3, data, length, 10);
#endif  // end of CONFIG_VMAP_STACK
    mutex_unlock(&(hx83112a_nf_chip_info->spi_lock));

    return result;
}

static int hx83112a_nf_bus_write(uint8_t command, uint32_t length, uint8_t *data)
{
    /* uint8_t spi_format_buf[length + 2]; */
    int result = 0;
    static uint8_t *spi_format_buf;
    int alloc_size = 0;
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    alloc_size = 256;
#else
    alloc_size = 49156;
#endif
    mutex_lock(&(hx83112a_nf_chip_info->spi_lock));
    if (spi_format_buf == NULL) {
        spi_format_buf = kzalloc((alloc_size + 2) * sizeof(uint8_t), GFP_KERNEL);
    }

    if (spi_format_buf == NULL) {
        TPD_INFO("%s: Can't allocate enough buf\n", __func__);
        return -ENOMEM;
    }
    spi_format_buf[0] = 0xF2;
    spi_format_buf[1] = command;

    memcpy((uint8_t *)(&spi_format_buf[2]), data, length);
#ifdef CONFIG_VMAP_STACK
    #ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM    // For Checksum Fail On MTK Platform
       result = hx83112a_nf_spi_write(spi_format_buf, length + 2);
    #else
       result = spi_write_then_read(hx83112a_nf_pri_ts->s_client, &spi_format_buf[0], length + 2, &spi_format_buf[0], 0);
    #endif   //end of CONFIG_TOUCHPANEL_MTK_PLATFORM
#else
    result = hx83112a_nf_spi_write(spi_format_buf, length + 2);
#endif  // end of CONFIG_VMAP_STACK
    mutex_unlock(&(hx83112a_nf_chip_info->spi_lock));

    return result;
}

/*******Part1: Function Declearation*******/
static int hx83112a_nf_power_control(void *chip_data, bool enable);
static int hx83112a_nf_get_chip_info(void *chip_data);
static int hx83112a_nf_mode_switch(void *chip_data, work_mode mode, bool flag);
static uint32_t hx83112a_nf_hw_check_CRC(uint8_t *start_addr, int reload_length);
static fw_check_state hx83112a_nf_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data);
static void hx83112a_nf_read_FW_ver(void);
static int hx83112a_nf_resetgpio_set(struct hw_resource *hw_res, bool on);

/*******Part2:Call Back Function implement*******/

/* add for himax */
void hx83112a_nf_flash_write_burst(uint8_t *reg_byte, uint8_t *write_data)
{
    uint8_t data_byte[8];
    int i = 0;
    int j = 0;

    for (i = 0; i < 4; i++) {
        data_byte[i] = reg_byte[i];
    }
    for (j = 4; j < 8; j++) {
        data_byte[j] = write_data[j - 4];
    }

    if (hx83112a_nf_bus_write(0x00, 8, data_byte) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return;
    }
}

void hx83112a_nf_flash_write_burst_length(uint8_t *reg_byte, uint8_t *write_data, int length)
{
    uint8_t *data_byte;
    data_byte = kzalloc(sizeof(uint8_t) * (length + 4), GFP_KERNEL);

    if (data_byte == NULL) {
        TPD_INFO("%s: Can't allocate enough buf\n", __func__);
        return;
    }
    memcpy(data_byte, reg_byte, 4); /* assign addr 4bytes */
    memcpy(data_byte + 4, write_data, length); /* assign data n bytes */

    if (hx83112a_nf_bus_write(0, length + 4, data_byte) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
    }
    kfree(data_byte);
}

void hx83112a_nf_burst_enable(uint8_t auto_add_4_byte)
{
    uint8_t tmp_data[4];
    tmp_data[0] = 0x31;

    if (hx83112a_nf_bus_write(0x13, 1, tmp_data) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return;
    }

    tmp_data[0] = (0x10 | auto_add_4_byte);
    if (hx83112a_nf_bus_write(0x0D, 1, tmp_data) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return;
    }
    /*isBusrtOn = true;*/
}

void hx83112a_nf_register_read(uint8_t *read_addr, int read_length, uint8_t *read_data, bool hx83112a_nf_cfg_flag)
{
    uint8_t tmp_data[4];
    int ret;
    if(hx83112a_nf_cfg_flag == false) {
        if(read_length > 256) {
            TPD_INFO("%s: read len over 256!\n", __func__);
            return;
        }
        if (read_length > 4) {
            hx83112a_nf_burst_enable(1);
        } else {
            hx83112a_nf_burst_enable(0);
        }

        tmp_data[0] = read_addr[0];
        tmp_data[1] = read_addr[1];
        tmp_data[2] = read_addr[2];
        tmp_data[3] = read_addr[3];
        ret = hx83112a_nf_bus_write(0x00, 4, tmp_data);
        if (ret < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
        tmp_data[0] = 0x00;
        ret = hx83112a_nf_bus_write(0x0C, 1, tmp_data);
        if (ret < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }

        if (hx83112a_nf_bus_read(0x08, read_length, read_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
        if (read_length > 4) {
            hx83112a_nf_burst_enable(0);
        }
    } else if(hx83112a_nf_cfg_flag == true) {
        if(hx83112a_nf_bus_read(read_addr[0], read_length, read_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
    } else {
        TPD_INFO("%s: hx83112a_nf_cfg_flag = %d, value is wrong!\n", __func__, hx83112a_nf_cfg_flag);
        return;
    }
}

void hx83112a_nf_register_write(uint8_t *write_addr, int write_length, uint8_t *write_data, bool hx83112a_nf_cfg_flag)
{
    int i = 0;
    int address = 0;
    if (hx83112a_nf_cfg_flag == false) {
        address = (write_addr[3] << 24) + (write_addr[2] << 16) + (write_addr[1] << 8) + write_addr[0];

        for (i = address; i < address + write_length; i++) {
            if (write_length > 4) {
                hx83112a_nf_burst_enable(1);
            } else {
                hx83112a_nf_burst_enable(0);
            }
            hx83112a_nf_flash_write_burst_length(write_addr, write_data, write_length);
        }
    } else if(hx83112a_nf_cfg_flag == true) {
        if(hx83112a_nf_bus_write(write_addr[0], write_length, write_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
    } else {
        TPD_INFO("%s: hx83112a_nf_cfg_flag = %d, value is wrong!\n", __func__, hx83112a_nf_cfg_flag);
        return;
    }
}

static int hx83112a_nf_mcu_register_write(uint8_t *write_addr, uint32_t write_length, uint8_t *write_data, uint8_t hx83112a_nf_cfg_flag)
{
    int total_read_times = 0;
    int max_bus_size = 128, test = 0;
    int total_size_temp = 0;
    int address = 0;
    int i = 0;

    uint8_t tmp_addr[4];
    uint8_t *tmp_data;

    total_size_temp = write_length;
    TPD_DETAIL("%s, Entering - total write size=%d\n", __func__, total_size_temp);

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (write_length > 240) {
        max_bus_size = 240;
    } else {
        max_bus_size = write_length;
    }

#else
    if (write_length > 49152) {
        max_bus_size = 49152;
    } else {
        max_bus_size = write_length;
    }
#endif

    hx83112a_nf_burst_enable(1);

    tmp_addr[3] = write_addr[3];
    tmp_addr[2] = write_addr[2];
    tmp_addr[1] = write_addr[1];
    tmp_addr[0] = write_addr[0];
    TPD_INFO("%s, write addr = 0x%02X%02X%02X%02X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);

    tmp_data = kzalloc (sizeof (uint8_t) * max_bus_size, GFP_KERNEL);
    if (tmp_data == NULL) {
        TPD_INFO("%s: Can't allocate enough buf \n", __func__);
        return -1;
    }

    if (total_size_temp % max_bus_size == 0) {
        total_read_times = total_size_temp / max_bus_size;
    } else {
        total_read_times = total_size_temp / max_bus_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        if (total_size_temp >= max_bus_size) {
            memcpy (tmp_data, write_data + (i * max_bus_size), max_bus_size);
            hx83112a_nf_flash_write_burst_length (tmp_addr, tmp_data, max_bus_size);

            total_size_temp = total_size_temp - max_bus_size;
        } else {
            test = total_size_temp % max_bus_size;
            memcpy (tmp_data, write_data + (i * max_bus_size), test);
            TPD_DEBUG("last total_size_temp=%d\n", total_size_temp % max_bus_size);

            hx83112a_nf_flash_write_burst_length (tmp_addr, tmp_data, max_bus_size);
        }

        address = ((i + 1) * max_bus_size);
        tmp_addr[0] = write_addr[0] + (uint8_t) ((address) & 0x00FF);

        if (tmp_addr[0] <  write_addr[0]) {
            tmp_addr[1] = write_addr[1] + (uint8_t) ((address >> 8) & 0x00FF) + 1;
        } else {
            tmp_addr[1] = write_addr[1] + (uint8_t) ((address >> 8) & 0x00FF);
        }

        udelay (100);
    }
    TPD_DETAIL("%s, End \n", __func__);
    kfree (tmp_data);
    return 0;
}


bool hx83112a_nf_sense_off(void)
{
    uint8_t cnt = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x5C;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0xA5;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    msleep(20);

    do {
        //===========================================
        //  0x31 ==> 0x27
        //===========================================
        tmp_data[0] = 0x27;
        if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }
        //===========================================
        //  0x32 ==> 0x95
        //===========================================
        tmp_data[0] = 0x95;
        if (hx83112a_nf_bus_write(0x32, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }

        // ======================
        // Check enter_save_mode
        // ======================
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xA8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        TPD_INFO("%s: Check enter_save_mode data[0]=%X \n", __func__, tmp_data[0]);

        if (tmp_data[0] == 0x0C) {
            //=====================================
            // Reset TCON
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x20;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            //=====================================
            // Reset ADC
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x94;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            return true;
        } else {
            msleep(10);
#ifdef HX_RST_PIN_FUNC
            hx83112a_nf_ic_reset(false, false);
#endif
        }
    } while (cnt++ < 15);

    return false;
}

bool hx83112a_nf_sense_off_mptest(void)
{
    uint8_t cnt = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    do {
        if (cnt == 0 || (tmp_data[0] != 0xA5 && tmp_data[0] != 0x00 && tmp_data[0] != 0x87)) {
            tmp_addr[3] = 0x90;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x5C;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0xA5;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
        }
        msleep(20);
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        cnt++;
        TPD_INFO("%s: save mode lock cnt = %d, data[0] = %2X!\n", __func__, cnt, tmp_data[0]);
    } while (tmp_data[0] != 0x87 && (cnt < 50));

    cnt = 0;

    do {
        //===========================================
        //  0x31 ==> 0x27
        //===========================================
        tmp_data[0] = 0x27;
        if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }
        //===========================================
        //  0x32 ==> 0x95
        //===========================================
        tmp_data[0] = 0x95;
        if (hx83112a_nf_bus_write(0x32, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }

        // ======================
        // Check enter_save_mode
        // ======================
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xA8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        TPD_INFO("%s: Check enter_save_mode data[0]=%X \n", __func__, tmp_data[0]);

        if (tmp_data[0] == 0x0C) {
            //=====================================
            // Reset TCON
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x20;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            //=====================================
            // Reset ADC
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x94;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            return true;
        } else {
            msleep(10);
#ifdef HX_RST_PIN_FUNC
            hx83112a_nf_ic_reset(false, false);
#endif
        }
    } while (cnt++ < 15);

    return false;
}

bool hx83112a_nf_enter_safe_mode(void)
{
    uint8_t cnt = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x5C;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0xA5;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    msleep(20);

    do {
        //===========================================
        //  0x31 ==> 0x27
        //===========================================
        tmp_data[0] = 0x27;
        if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }
        //===========================================
        //  0x32 ==> 0x95
        //===========================================
        tmp_data[0] = 0x95;
        if (hx83112a_nf_bus_write(0x32, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }

        //===========================================
        //  0x31 ==> 0x00
        //===========================================
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }
        //msleep(10);
        //===========================================
        //  0x31 ==> 0x27
        //===========================================
        tmp_data[0] = 0x27;
        if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }
        //===========================================
        //  0x32 ==> 0x95
        //===========================================
        tmp_data[0] = 0x95;
        if (hx83112a_nf_bus_write(0x32, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return false;
        }

        // ======================
        // Check enter_save_mode
        // ======================
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xA8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        TPD_DETAIL("%s: Check enter_save_mode data[0]=%X \n", __func__, tmp_data[0]);

        if (tmp_data[0] == 0x0C) {
            //=====================================
            // Reset TCON
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x20;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            //=====================================
            // Reset ADC
            //=====================================
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x02;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x94;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            msleep(1);
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x01;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            return true;
        } else {
            msleep(10);
#ifdef HX_RST_PIN_FUNC
            hx83112a_nf_ic_reset(false, false);
#endif
        }
    } while (cnt++ < 20);

    return false;
}

void hx83112a_nf_interface_on(void)
{
    uint8_t tmp_data[5];
    uint8_t tmp_data2[2];
    int cnt = 0;

    //Read a dummy register to wake up I2C.
    if ( hx83112a_nf_bus_read(0x08, 4, tmp_data) < 0) { // to knock I2C
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return;
    }

    do {
        //===========================================
        // Enable continuous burst mode : 0x13 ==> 0x31
        //===========================================
        tmp_data[0] = 0x31;
        if (hx83112a_nf_bus_write(0x13, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
        //===========================================
        // AHB address auto +4 : 0x0D ==> 0x11
        // Do not AHB address auto +4 : 0x0D ==> 0x10
        //===========================================
        tmp_data[0] = (0x10);
        if (hx83112a_nf_bus_write(0x0D, 1, tmp_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }

        // Check cmd
        hx83112a_nf_bus_read(0x13, 1, tmp_data);
        hx83112a_nf_bus_read(0x0D, 1, tmp_data2);

        if (tmp_data[0] == 0x31 && tmp_data2[0] == 0x10) {
            //isBusrtOn = true;
            break;
        }
        msleep(1);
    } while (++cnt < 10);

    if (cnt > 0) {
        TPD_INFO("%s:Polling burst mode: %d times", __func__, cnt);
    }
}

void hx83112a_nf_diag_register_set(uint8_t diag_command)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    TPD_INFO("diag_command = %d\n", diag_command );

    hx83112a_nf_interface_on();

    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x02;
    tmp_addr[1] = 0x04;
    tmp_addr[0] = 0xB4;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = diag_command;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    TPD_INFO("%s: tmp_data[3] = 0x%02X, tmp_data[2] = 0x%02X, tmp_data[1] = 0x%02X, tmp_data[0] = 0x%02X!\n",
             __func__, tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);

}


bool hx83112a_nf_wait_wip(int Timing)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t in_buffer[10];
    //uint8_t out_buffer[20];
    int retry_cnt = 0;

    //=====================================
    // SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
    //=====================================
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x10;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x02;
    tmp_data[1] = 0x07;
    tmp_data[0] = 0x80;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    in_buffer[0] = 0x01;

    do {
        //=====================================
        // SPI Transfer Control : 0x8000_0020 ==> 0x4200_0003
        //=====================================
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x20;
        tmp_data[3] = 0x42;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x03;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        //=====================================
        // SPI Command : 0x8000_0024 ==> 0x0000_0005
        // read 0x8000_002C for 0x01, means wait success
        //=====================================
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x24;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x05;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        in_buffer[0] = in_buffer[1] = in_buffer[2] = in_buffer[3] = 0xFF;
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x2C;
        hx83112a_nf_register_read(tmp_addr, 4, in_buffer, false);

        if ((in_buffer[0] & 0x01) == 0x00) {
            return true;
        }

        retry_cnt++;

        if (in_buffer[0] != 0x00 || in_buffer[1] != 0x00 || in_buffer[2] != 0x00 || in_buffer[3] != 0x00) {
            TPD_INFO("%s:Wait wip retry_cnt:%d, buffer[0]=%d, buffer[1]=%d, buffer[2]=%d, buffer[3]=%d \n", __func__,
                     retry_cnt, in_buffer[0], in_buffer[1], in_buffer[2], in_buffer[3]);
        }

        if (retry_cnt > 100) {
            TPD_INFO("%s: Wait wip error!\n", __func__);
            return false;
        }
        msleep(Timing);
    } while ((in_buffer[0] & 0x01) == 0x01);
    return true;
}


void hx83112a_nf_sense_on(uint8_t FlashMode)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int retry = 0;

    TPD_DETAIL("Enter %s  \n", __func__);

    hx83112a_nf_interface_on();
    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x5C;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
    //msleep(20);

    if (!FlashMode) {
        //===AHBI2C_SystemReset==========
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x18;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x55;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);
    } else {
        do {
            tmp_addr[3] = 0x90;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x98;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x53;
            hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

            tmp_addr[0] = 0xE4;
            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

            TPD_DETAIL("%s:Read status from IC = %X, %X\n", __func__, tmp_data[0], tmp_data[1]);
        } while ((tmp_data[1] != 0x01 || tmp_data[0] != 0x00) && retry++ < 5);

        if (retry >= 5) {
            TPD_INFO("%s: Fail:\n", __func__);

            //===AHBI2C_SystemReset==========
            tmp_addr[3] = 0x90;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x18;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x55;
            hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);
        } else {
            TPD_DETAIL("%s:OK and Read status from IC = %X, %X\n", __func__, tmp_data[0], tmp_data[1]);

            /* reset code*/
            tmp_data[0] = 0x00;
            if (hx83112a_nf_bus_write(0x31, 1, tmp_data) < 0) {
                TPD_INFO("%s: i2c access fail!\n", __func__);
            }
            if (hx83112a_nf_bus_write(0x32, 1, tmp_data) < 0) {
                TPD_INFO("%s: i2c access fail!\n", __func__);
            }

            tmp_addr[3] = 0x90;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x98;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);
        }
    }
}

/**
 * hx83112a_nf_enable_interrupt -   Device interrupt ability control.
 * @chip_info: struct include i2c resource.
 * @enable: disable or enable control purpose.
 * Return  0: succeed, -1: failed.
 */
static int hx83112a_nf_enable_interrupt(struct chip_data_hx83112a_nf *chip_info, bool enable)
{
    TPD_DETAIL("%s enter, enable = %d.\n", __func__, enable);

    if (enable == true && hx83112a_nf_irq_en_cnt == 0) {
        enable_irq(chip_info->hx_irq);
        hx83112a_nf_irq_en_cnt = 1;
    } else if (enable == false && hx83112a_nf_irq_en_cnt == 1) {
        disable_irq_nosync(chip_info->hx_irq);
        hx83112a_nf_irq_en_cnt = 0;
    } else {
        TPD_DETAIL("irq is not pairing! enable= %d, cnt = %d\n", enable, hx83112a_nf_irq_en_cnt);
    }

    return 0;
}

#ifdef HX_ZERO_FLASH
struct hx83112a_nf_core_fp g_hx83112a_nf_core_fp;
struct hx83112a_nf_operation *hx83112a_nf_pzf_op = NULL;
//char *i_CTPM_firmware_name = "Himax_firmware.bin";
bool hx83112a_nf_auto_update_flag = false;
int HX83112A_NF_POWERONOF = 1;

void hx83112a_nf_in_parse_assign_cmd(uint32_t addr, uint8_t *cmd, int len)
{
    /*TPD_INFO("%s: Entering!\n", __func__);*/
    switch (len) {
    case 1:
        cmd[0] = addr;
        /*TPD_INFO("%s: cmd[0] = 0x%02X\n", __func__, cmd[0]);*/
        break;

    case 2:
        cmd[0] = addr % 0x100;
        cmd[1] = (addr >> 8) % 0x100;
        /*TPD_INFO("%s: cmd[0] = 0x%02X,cmd[1] = 0x%02X\n", __func__, cmd[0], cmd[1]);*/
        break;

    case 4:
        cmd[0] = addr % 0x100;
        cmd[1] = (addr >> 8) % 0x100;
        cmd[2] = (addr >> 16) % 0x100;
        cmd[3] = addr / 0x1000000;
        /*  TPD_INFO("%s: cmd[0] = 0x%02X,cmd[1] = 0x%02X,cmd[2] = 0x%02X,cmd[3] = 0x%02X\n",
            __func__, cmd[0], cmd[1], cmd[2], cmd[3]);*/
        break;

    default:
        TPD_INFO("%s: input length fault, len = %d!\n", __func__, len);
    }
}

void hx83112a_nf_update_dirly_0f(void)
{
    TPD_INFO("It will update fw after esd event in zero flash mode!\n");
    g_hx83112a_nf_core_fp.fp_0f_operation_dirly();
}

void hx83112a_nf_mcu_sys_reset(void)
{
    hx83112a_nf_register_write (hx83112a_nf_pzf_op->addr_system_reset,  4,  hx83112a_nf_pzf_op->data_system_reset,  false);
}

int hx83112a_nf_dis_rload_0f(int disable)
{
    /*Diable Flash Reload*/
    int retry = 10;
    int check_val = 0;
    uint8_t tmp_data[4] = {0};

    TPD_DETAIL("%s: Entering !\n", __func__);

    do {
        hx83112a_nf_flash_write_burst (hx83112a_nf_pzf_op->addr_dis_flash_reload,  hx83112a_nf_pzf_op->data_dis_flash_reload);
        hx83112a_nf_register_read(hx83112a_nf_pzf_op->addr_dis_flash_reload, 4, tmp_data, false);
        TPD_DETAIL("Now data: tmp_data[3] = 0x%02X || tmp_data[2] = 0x%02X || tmp_data[1] = 0x%02X || tmp_data[0] = 0x%02X\n", tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
        if( tmp_data[3] != 0x00 || tmp_data[2] != 0x00 || tmp_data[1] != 0x9A || tmp_data[0] != 0xA9) {
            TPD_INFO("Now data: tmp_data[3] = 0x%02X || tmp_data[2] = 0x%02X || tmp_data[1] = 0x%02X || tmp_data[0] = 0x%02X\n", tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
            TPD_INFO("Not Same,Write Fail, there is %d retry times!\n", retry);
        } else {
            check_val = 1;
            TPD_DETAIL("It's same! Write success!\n");
        }
        msleep(5);
    } while(check_val == 0 && retry-- > 0);

    TPD_DETAIL("%s: END !\n", __func__);

    return check_val;
}

void hx83112a_nf_mcu_clean_sram_0f(uint8_t *addr, int write_len, int type)
{
    int total_read_times = 0;
    int max_bus_size = MAX_TRANS_SZ;
    int total_size_temp = 0;
    int total_size = 0;
    int address = 0;
    int i = 0;

    uint8_t fix_data = 0x00;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[MAX_TRANS_SZ] = {0};

    TPD_DETAIL("%s, Entering \n", __func__);

    total_size = write_len;
    total_size_temp = write_len;

    if (total_size > MAX_TRANS_SZ) {
        max_bus_size = MAX_TRANS_SZ;
    }

    total_size_temp = write_len;

    hx83112a_nf_burst_enable(1);

    tmp_addr[3] = addr[3];
    tmp_addr[2] = addr[2];
    tmp_addr[1] = addr[1];
    tmp_addr[0] = addr[0];
    TPD_DETAIL("%s, write addr tmp_addr[3] = 0x%2.2X, tmp_addr[2] = 0x%2.2X, tmp_addr[1] = 0x%2.2X, tmp_addr[0] = 0x%2.2X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);

    switch (type) {
    case 0:
        fix_data = 0x00;
        break;
    case 1:
        fix_data = 0xAA;
        break;
    case 2:
        fix_data = 0xBB;
        break;
    }

    for (i = 0; i < MAX_TRANS_SZ; i++) {
        tmp_data[i] = fix_data;
    }

    TPD_DETAIL("%s, total size=%d\n", __func__, total_size);

    if (total_size_temp % max_bus_size == 0) {
        total_read_times = total_size_temp / max_bus_size;
    } else {
        total_read_times = total_size_temp / max_bus_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        /*TPD_DETAIL("[log]write %d time start!\n", i);*/
        if (total_size_temp >= max_bus_size) {
            hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data,  max_bus_size);
            total_size_temp = total_size_temp - max_bus_size;
        } else {
            TPD_DETAIL("last total_size_temp=%d\n", total_size_temp);
            hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data,  total_size_temp % max_bus_size);
        }
        address = ((i + 1) * max_bus_size);
        tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF);
        tmp_addr[0] = addr[0] + (uint8_t) ((address) & 0x00FF);

        msleep (10);
    }

    TPD_DETAIL("%s, END \n", __func__);
}

void hx83112a_nf_mcu_write_sram_0f(const struct firmware *fw_entry, uint8_t *addr, int start_index, uint32_t write_len)
{
    int total_read_times = 0;
    int max_bus_size = MAX_TRANS_SZ;
    int total_size_temp = 0;
    int total_size = 0;
    int address = 0;
    int i = 0;

    uint8_t tmp_addr[4];
    //uint8_t *tmp_data;
    uint32_t now_addr;

    TPD_DETAIL("%s, ---Entering \n", __func__);

    total_size = fw_entry->size;

    total_size_temp = write_len;
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (write_len > MAX_TRANS_SZ) {
        max_bus_size = MAX_TRANS_SZ;
    } else {
        max_bus_size = write_len;
    }
#else
    if (write_len > 49152) {
        max_bus_size = 49152;
    } else {
        max_bus_size = write_len;
    }
#endif
    hx83112a_nf_burst_enable(1);

    tmp_addr[3] = addr[3];
    tmp_addr[2] = addr[2];
    tmp_addr[1] = addr[1];
    tmp_addr[0] = addr[0];
    TPD_DETAIL("%s, write addr tmp_addr[3] = 0x%2.2X, tmp_addr[2] = 0x%2.2X, tmp_addr[1] = 0x%2.2X, tmp_addr[0] = 0x%2.2X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);
    now_addr = (addr[3] << 24) + (addr[2] << 16) + (addr[1] << 8) + addr[0];
    TPD_DETAIL("now addr= 0x%08X\n", now_addr);

    TPD_DETAIL("%s,  total size=%d\n", __func__, total_size);

    if (hx83112a_nf_chip_info->tmp_data == NULL) {
        //TPD_INFO("%s,  enteralloc hx83112a_nf_chip_info->tmp_data\n", __func__);
        hx83112a_nf_chip_info->tmp_data = kzalloc (sizeof (uint8_t) * firmware_update_space, GFP_KERNEL);
        if (hx83112a_nf_chip_info->tmp_data == NULL) {
            TPD_INFO("%s, alloc hx83112a_nf_chip_info->tmp_data failed\n", __func__);
            return ;
        }
        //TPD_INFO("%s, end---------alloc hx83112a_nf_chip_info->tmp_data\n", __func__);
    }
    memcpy (hx83112a_nf_chip_info->tmp_data, fw_entry->data, total_size);
    /*
    for (i = 0;i < 10;i++) {
        TPD_INFO("[%d] 0x%2.2X", i, tmp_data[i]);
    }
    TPD_INFO("\n");
    */
    if (total_size_temp % max_bus_size == 0) {
        total_read_times = total_size_temp / max_bus_size;
    } else {
        total_read_times = total_size_temp / max_bus_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        /*
        TPD_INFO("[log]write %d time start!\n", i);
        TPD_INFO("[log]addr[3] = 0x%02X, addr[2] = 0x%02X, addr[1] = 0x%02X, addr[0] = 0x%02X!\n", tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);
        */
        if (total_size_temp >= max_bus_size) {
            hx83112a_nf_flash_write_burst_length(tmp_addr, &(hx83112a_nf_chip_info->tmp_data[start_index + i * max_bus_size]),  max_bus_size);
            total_size_temp = total_size_temp - max_bus_size;
        } else {
            TPD_DETAIL("last total_size_temp=%d\n", total_size_temp);
            hx83112a_nf_flash_write_burst_length(tmp_addr, &(hx83112a_nf_chip_info->tmp_data[start_index + i * max_bus_size]),  total_size_temp % max_bus_size);
        }

        /*TPD_INFO("[log]write %d time end!\n", i);*/
        address = ((i + 1) * max_bus_size);
        tmp_addr[0] = addr[0] + (uint8_t) ((address) & 0x00FF);

        if (tmp_addr[0] < addr[0]) {
            tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF) + 1;
        } else {
            tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF);
        }


        udelay (100);
    }
    TPD_DETAIL("%s, ----END \n", __func__);
    //kfree (tmp_data);
    memset(hx83112a_nf_chip_info->tmp_data, 0, total_size);
}
int hx83112a_nf_sram_write_crc_check(const struct firmware *fw_entry, uint8_t *addr, int strt_idx, uint32_t len)
{
    int retry = 0;
    int crc = -1;

    do {
        g_hx83112a_nf_core_fp.fp_write_sram_0f(fw_entry, addr, strt_idx, len);
        crc = hx83112a_nf_hw_check_CRC (hx83112a_nf_pzf_op->data_sram_start_addr, HX_48K_SZ);
        retry++;
        /*I("%s, HW CRC %s in %d time\n", __func__, (crc == 0)?"OK":"Fail", retry);*/
    } while (crc != 0 && retry < 10);

    return crc;
}
static int hx83112a_nf_mcu_Calculate_CRC_with_AP(unsigned char *FW_content, int CRC_from_FW, int len)
{
    int i, j, length = 0;
    int fw_data;
    int fw_data_2;
    int CRC = 0xFFFFFFFF;
    int PolyNomial = 0x82F63B78;

    length = len / 4;

    for (i = 0; i < length; i++) {
        fw_data = FW_content[i * 4];

        for (j = 1; j < 4; j++) {
            fw_data_2 = FW_content[i * 4 + j];
            fw_data += (fw_data_2) << (8 * j);
        }

        CRC = fw_data ^ CRC;

        for (j = 0; j < 32; j++) {
            if ((CRC % 2) != 0) {
                CRC = ((CRC >> 1) & 0x7FFFFFFF) ^ PolyNomial;
            } else {
                CRC = (((CRC >> 1) & 0x7FFFFFFF)/*& 0x7FFFFFFF*/);
            }
        }
        /*I("CRC = %x, i = %d \n", CRC, i);*/
    }

    return CRC;
}

bool hx83112a_nf_parse_bin_cfg_data(const struct firmware *fw_entry)
{
    int part_num = 0;
    int i = 0;
    uint8_t buf[16];
    int i_max = 0;
    int i_min = 0;
    uint32_t dsram_base = 0xFFFFFFFF;
    uint32_t dsram_max = 0;
    struct hx83112a_nf_info *hx83112a_nf_info_arr;

    /*1. get number of partition*/
    part_num = fw_entry->data[HX64K + 12];
    TPD_INFO("%s, Number of partition is %d\n", __func__, part_num);
    if (part_num <= 1) {
        TPD_INFO("%s, size of cfg part failed! part_num = %d\n", __func__, part_num);
        return false;
    }
    /*2. initial struct of array*/
    hx83112a_nf_info_arr = kzalloc(part_num * sizeof(struct hx83112a_nf_info), GFP_KERNEL);
    if (hx83112a_nf_info_arr == NULL) {
        TPD_INFO("%s, Allocate ZF info array failed!\n", __func__);
        return false;
    }

    for (i = 0; i < part_num; i++) {
        /*3. get all partition*/
        memcpy(buf, &fw_entry->data[i * 0x10 + HX64K], 16);
        memcpy(hx83112a_nf_info_arr[i].sram_addr, buf, 4);
        hx83112a_nf_info_arr[i].write_size = buf[5] << 8 | buf[4];
        hx83112a_nf_info_arr[i].fw_addr = buf[9] << 8 | buf[8];
        hx83112a_nf_info_arr[i].cfg_addr = hx83112a_nf_info_arr[i].sram_addr[0];
        hx83112a_nf_info_arr[i].cfg_addr += hx83112a_nf_info_arr[i].sram_addr[1] << 8;
        hx83112a_nf_info_arr[i].cfg_addr += hx83112a_nf_info_arr[i].sram_addr[2] << 16;
        hx83112a_nf_info_arr[i].cfg_addr += hx83112a_nf_info_arr[i].sram_addr[3] << 24;

        /*TPD_INFO("%s, [%d] SRAM addr = %08X\n", __func__, i, hx83112a_nf_info_arr[i].cfg_addr);
        TPD_INFO("%s, [%d] fw_addr = %04X!\n", __func__, i, hx83112a_nf_info_arr[i].fw_addr);
        TPD_INFO("%s, [%d] write_size = %d!\n", __func__, i, hx83112a_nf_info_arr[i].write_size); */
        if (i == 0)
            continue;
        if (dsram_base > hx83112a_nf_info_arr[i].cfg_addr) {
            dsram_base = hx83112a_nf_info_arr[i].cfg_addr;
            i_min = i;
        } else if (dsram_max < hx83112a_nf_info_arr[i].cfg_addr) {
            dsram_max = hx83112a_nf_info_arr[i].cfg_addr;
            i_max = i;
        }
    }
    for (i = 0; i < 4; i++)
        hx83112a_nf_sram_min[i] = hx83112a_nf_info_arr[i_min].sram_addr[i];
    hx83112a_nf_cfg_sz = (dsram_max - dsram_base) + hx83112a_nf_info_arr[i_max].write_size;
    hx83112a_nf_cfg_sz = hx83112a_nf_cfg_sz + (hx83112a_nf_cfg_sz % 16);

    TPD_INFO("%s, hx83112a_nf_cfg_sz = %d!, dsram_base = %X, dsram_max = %X\n", __func__, hx83112a_nf_cfg_sz, dsram_base, dsram_max);

    if (hx83112a_nf_FW_buf == NULL)
        hx83112a_nf_FW_buf = kzalloc(sizeof(unsigned char) * FW_BIN_16K_SZ, GFP_KERNEL);

    for (i = 1; i < part_num; i++)
        memcpy(hx83112a_nf_FW_buf + (hx83112a_nf_info_arr[i].cfg_addr - dsram_base), (unsigned char *)&fw_entry->data[hx83112a_nf_info_arr[i].fw_addr], hx83112a_nf_info_arr[i].write_size);

    hx83112a_nf_cfg_crc = hx83112a_nf_mcu_Calculate_CRC_with_AP(hx83112a_nf_FW_buf, 0, hx83112a_nf_cfg_sz);
    TPD_INFO("chenyunrui:hx83112a_nf_cfg_crc = %d\n", hx83112a_nf_cfg_crc);
    kfree(hx83112a_nf_info_arr);
    return true;
}

static int hx83112a_nf_nf_zf_part_info(const struct firmware *fw_entry)
{
    struct timespec timeStart, timeEnd, timeDelta;
    int retry = 0;
    int crc = -1;
    bool ret = false;

    if (!hx83112a_nf_parse_bin_cfg_data(fw_entry))
        TPD_INFO("%s, Parse cfg from bin failed\n", __func__);
    hx83112a_nf_register_write(hx83112a_nf_pzf_op->addr_system_reset, 4, hx83112a_nf_pzf_op->data_system_reset, false);
    hx83112a_nf_enter_safe_mode();
    getnstimeofday(&timeStart);
    /* first 48K */

    hx83112a_nf_sram_write_crc_check(fw_entry, hx83112a_nf_pzf_op->data_sram_start_addr, 0, HX_48K_SZ);
    crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_sram_start_addr, HX_48K_SZ);
    ret = (crc == 0) ? true : false;
    if (crc != 0)
        TPD_INFO("48k CRC Failed! CRC = %X", crc);
    do {
        hx83112a_nf_mcu_register_write(hx83112a_nf_sram_min, hx83112a_nf_cfg_sz, hx83112a_nf_FW_buf, 0);
        /*hx83112a_nf_register_write(hx83112a_nf_sram_min, hx83112a_nf_cfg_sz, hx83112a_nf_FW_buf, 0); */
        crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_sram_min, hx83112a_nf_cfg_sz);
        if (crc != hx83112a_nf_cfg_crc)
            TPD_INFO("Config CRC FAIL, HW CRC = %X, SW CRC = %X, retry time = %d", crc, hx83112a_nf_cfg_crc, retry);
        retry++;
    } while (!ret && retry < 10);
    if (HX83112A_NF_POWERONOF == 1)
        g_hx83112a_nf_core_fp.fp_write_sram_0f(fw_entry, hx83112a_nf_pzf_op->data_mode_switch, 0xC33C, 4);
    else
        g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_mode_switch, 4, 2);

    getnstimeofday(&timeEnd);
    timeDelta.tv_nsec = (timeEnd.tv_sec * 1000000000 + timeEnd.tv_nsec) - (timeStart.tv_sec * 1000000000 + timeStart.tv_nsec);
    TPD_INFO("update firmware time = %ld us\n", timeDelta.tv_nsec / 1000);
    return 0;
}

void hx83112a_nf_mcu_firmware_update_0f(const struct firmware *fw_entry)
{
    int retry = 0;
    int crc = -1;
    int ret = 0;
    uint8_t temp_addr[4];
    uint8_t temp_data[4];
    struct firmware *request_fw_headfile = NULL;
    const struct firmware *tmp_fw_entry = NULL;
    bool reload = false;

    TPD_DETAIL("%s, Entering \n", __func__);

fw_reload:
    if (fw_entry == NULL || reload) {
        TPD_INFO("Get FW from headfile\n");
        if (request_fw_headfile == NULL) {
            request_fw_headfile = kzalloc(sizeof(struct firmware), GFP_KERNEL);
        }
        if(request_fw_headfile == NULL) {
            TPD_INFO("%s kzalloc failed!\n", __func__);
            return;
        }
        if (hx83112a_nf_chip_info->g_fw_sta) {
            TPD_INFO("request firmware failed, get from g_fw_buf\n");
            request_fw_headfile->size = hx83112a_nf_chip_info->g_fw_len;
            request_fw_headfile->data = hx83112a_nf_chip_info->g_fw_buf;
            tmp_fw_entry = request_fw_headfile;

        } else {
            TPD_INFO("request firmware failed, get from headfile\n");
            if(hx83112a_nf_chip_info->p_firmware_headfile->firmware_data) {
                request_fw_headfile->size = hx83112a_nf_chip_info->p_firmware_headfile->firmware_size;
                request_fw_headfile->data = hx83112a_nf_chip_info->p_firmware_headfile->firmware_data;
                tmp_fw_entry = request_fw_headfile;
                hx83112a_nf_chip_info->using_headfile = true;
            } else {
                TPD_INFO("firmware_data is NULL! exit firmware update!\n");
                if(request_fw_headfile != NULL) {
                    kfree(request_fw_headfile);
                    request_fw_headfile = NULL;
                }
                return;
            }
        }
    } else {
        tmp_fw_entry = fw_entry;
    }

    if ((int)tmp_fw_entry->size > HX64K) {
        ret = hx83112a_nf_nf_zf_part_info(tmp_fw_entry);
    } else {
        hx83112a_nf_register_write(hx83112a_nf_pzf_op->addr_system_reset, 4, hx83112a_nf_pzf_op->data_system_reset, false);
        hx83112a_nf_enter_safe_mode();
        /* first 48K */
        do {
            g_hx83112a_nf_core_fp.fp_write_sram_0f (tmp_fw_entry, hx83112a_nf_pzf_op->data_sram_start_addr, 0, HX_48K_SZ);
            crc = hx83112a_nf_hw_check_CRC (hx83112a_nf_pzf_op->data_sram_start_addr,  HX_48K_SZ);

            temp_addr[3] = 0x08;
            temp_addr[2] = 0x00;
            temp_addr[1] = 0xBF;
            temp_addr[0] = 0xFC;
            hx83112a_nf_register_read(temp_addr, 4, temp_data, false);

            TPD_DETAIL("%s, 48K LAST 4 BYTES: data[3] = 0x%02X, data[2] = 0x%02X, data[1] = 0x%02X, data[0] = 0x%02X\n", __func__, temp_data[3], temp_data[2], temp_data[1], temp_data[0]);

            if (crc == 0) {
                TPD_DETAIL("%s, HW CRC OK in %d time \n", __func__, retry);
                break;
            } else {
                TPD_INFO("%s, HW CRC FAIL in %d time !\n", __func__, retry);
            }
            retry++;
        } while (((temp_data[3] == 0 && temp_data[2] == 0 && temp_data[1] == 0 && temp_data[0] == 0 && retry < 80) || (crc != 0 && retry < 30)) && !(hx83112a_nf_chip_info->using_headfile && retry < 3));

        if (crc != 0) {
            TPD_INFO("Last time CRC Fail!\n");
            if(reload) {
                return;
            } else {
                reload = true;
                goto fw_reload;
            }
        }

        /* if HX83112A_NF_POWERONOF!= 1, it will be clean mode! */
        /*config and setting */
        /*config info*/
        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_cfg_info, 0xC000, 128);//132
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_cfg_info, 128);
                if (crc == 0) {
                    TPD_DETAIL("%s, config info ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, config info fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("config info CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_cfg_info, 128, 2);
        }
        /*FW config*/
        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_fw_cfg_p1, 0xC100, 528);//482
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_fw_cfg_p1, 528);
                if (crc == 0) {
                    TPD_DETAIL("%s, 1 FW config ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, 1 FW config fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("1 FW config CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_fw_cfg_p1, 528, 1);
        }

        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_fw_cfg_p3, 0xCA00, 128);
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_fw_cfg_p3, 128);
                if (crc == 0) {
                    TPD_DETAIL("%s, 3 FW config ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, 3 FW config fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("3 FW config CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_fw_cfg_p3, 128, 1);
        }

        /*ADC config*/
        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_adc_cfg_1, 0xD640, 1200);
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_adc_cfg_1, 1200);
                if (crc == 0) {
                    TPD_DETAIL("%s, 1 ADC config ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, 1 ADC config fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("1 ADC config CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_adc_cfg_1, 1200, 2);
        }

        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_adc_cfg_2, 0xD320, 800);
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_adc_cfg_2, 800);
                if (crc == 0) {
                    TPD_DETAIL("%s, 2 ADC config ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, 2 ADC config fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("2 ADC config CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_adc_cfg_2, 800, 2);
        }

        /*mapping table*/
        if (HX83112A_NF_POWERONOF == 1) {
            retry = 0;
            do {
                g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_map_table, 0xE000, 1536);
                crc = hx83112a_nf_hw_check_CRC(hx83112a_nf_pzf_op->data_map_table, 1536);
                if (crc == 0) {
                    TPD_DETAIL("%s, mapping table ok in %d time \n", __func__, retry);
                    break;
                } else {
                    TPD_INFO("%s, mapping table fail in %d time !\n", __func__, retry);
                }
                retry++;
            } while ((crc != 0 && retry < 30) || (hx83112a_nf_chip_info->using_headfile && retry < 15));

            if (crc != 0) {
                TPD_INFO("mapping table CRC Fail!\n");
                if (!reload) {
                    reload = true;
                    goto fw_reload;
                }
            }
        } else {
            g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_map_table, 1536, 2);
        }
    }

    /* switch mode*/
    if (HX83112A_NF_POWERONOF == 1) {
        g_hx83112a_nf_core_fp.fp_write_sram_0f(tmp_fw_entry, hx83112a_nf_pzf_op->data_mode_switch, 0xC30C, 4);
    } else {
        g_hx83112a_nf_core_fp.fp_clean_sram_0f(hx83112a_nf_pzf_op->data_mode_switch, 4, 2);
    }

    hx83112a_nf_fw_check(hx83112a_nf_pri_ts->chip_data, &hx83112a_nf_pri_ts->resolution_info, &hx83112a_nf_pri_ts->panel_data);

    if (request_fw_headfile != NULL) {
        kfree(request_fw_headfile);
        request_fw_headfile = NULL;
    }
    TPD_DETAIL("%s, END \n", __func__);
}
int hx83112a_nf_0f_op_file_dirly(char *file_name)
{
    int err = NO_ERR;
    const struct firmware *fw_entry = NULL;


    TPD_INFO("%s, Entering \n", __func__);
    TPD_INFO("file name = %s\n", file_name);
    if (hx83112a_nf_pri_ts->fw_update_app_support) {
        err = request_firmware_select (&fw_entry, file_name, hx83112a_nf_pri_ts->dev);
    } else {
        err = request_firmware (&fw_entry, file_name, hx83112a_nf_pri_ts->dev);
    }
    if (err < 0) {
        TPD_INFO("%s, fail in line%d error code=%d, file maybe fail\n", __func__, __LINE__, err);
        return err;
    }

    //hx83112a_nf_int_enable (0);

    if(hx83112a_nf_f_0f_updat == 1) {
        TPD_INFO("%s:[Warning]Other thread is updating now!\n", __func__);
        release_firmware (fw_entry);
        err = -1;
        return err;
    } else {
        TPD_INFO("%s:Entering Update Flow!\n", __func__);
        hx83112a_nf_f_0f_updat = 1;
    }

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    /* trigger reset */
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, false); // reset gpio
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, true); // reset gpio

    g_hx83112a_nf_core_fp.fp_firmware_update_0f (fw_entry);
    release_firmware (fw_entry);

    hx83112a_nf_f_0f_updat = 0;
    TPD_INFO("%s, END \n", __func__);
    return err;
}
int hx83112a_nf_mcu_0f_operation_dirly(void)
{
    int err = NO_ERR;
    //const struct firmware *fw_entry = NULL;

    TPD_DETAIL("%s, Entering \n", __func__);

    if(hx83112a_nf_f_0f_updat == 1) {
        TPD_INFO("%s:[Warning]Other thread is updating now!\n", __func__);
        err = -1;
        return err;
    } else {
        TPD_INFO("%s:Entering Update Flow!\n", __func__);
        hx83112a_nf_f_0f_updat = 1;
    }

    //hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    g_hx83112a_nf_core_fp.fp_firmware_update_0f(NULL);
    //release_firmware (hx83112a_nf_chip_info->g_fw_entry);

    hx83112a_nf_f_0f_updat = 0;
    TPD_DETAIL("%s, END \n", __func__);
    return err;
}

int hx83112a_nf_mcu_0f_operation_test_dirly(char *fw_name)
{
    int err = NO_ERR;
    const struct firmware *fw_entry = NULL;

    TPD_DETAIL("%s, Entering \n", __func__);
    TPD_DETAIL("file name = %s\n", fw_name);
    TPD_INFO("Request TP firmware.\n");
    err = request_firmware (&fw_entry, fw_name, hx83112a_nf_pri_ts->dev);
    if (err < 0) {
        TPD_INFO("%s, fail in line%d error code=%d, file maybe fail\n", __func__, __LINE__, err);
        if (fw_entry != NULL) {
            release_firmware(fw_entry);
            fw_entry = NULL;
        }
        return err;
    }

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    g_hx83112a_nf_core_fp.fp_firmware_update_0f(fw_entry);
    release_firmware (fw_entry);

    TPD_DETAIL("%s, END \n", __func__);
    return err;
}

void hx83112a_nf_mcu_0f_operation(struct work_struct *work)
{
    TPD_INFO("%s, Entering \n", __func__);

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (hx83112a_nf_pri_ts->boot_mode != RECOVERY_BOOT  && !is_oem_unlocked())
#else
    if (hx83112a_nf_pri_ts->boot_mode != MSM_BOOT_MODE__RECOVERY  && !is_oem_unlocked())
#endif
    {
        TPD_INFO("file name = %s\n", hx83112a_nf_pri_ts->panel_data.fw_name);

    } else {
        TPD_INFO("TP firmware has been requested.\n");
    }

    if (hx83112a_nf_f_0f_updat == 1) {
        TPD_INFO("%s:[Warning]Other thread is updating now!\n", __func__);
        return ;
    } else {
        TPD_INFO("%s:Entering Update Flow!\n", __func__);
        hx83112a_nf_f_0f_updat = 1;
    }

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    /* trigger reset */
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, false); // reset gpio
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, true); // reset gpio

    g_hx83112a_nf_core_fp.fp_firmware_update_0f(NULL);
    //release_firmware (hx83112a_nf_chip_info->g_fw_entry);

    g_hx83112a_nf_core_fp.fp_reload_disable(0);
    msleep (10);
    hx83112a_nf_read_FW_ver();
    msleep (10);
    hx83112a_nf_sense_on(0x00);
    msleep (10);
    TPD_INFO("%s:End \n", __func__);

#ifdef CONFIG_OPLUS_TP_APK
    if(hx83112a_nf_chip_info->debug_mode_sta) {
        if(hx83112a_nf_pri_ts->apk_op && hx83112a_nf_pri_ts->apk_op->apk_debug_set) {
            hx83112a_nf_pri_ts->apk_op->apk_debug_set(hx83112a_nf_pri_ts->chip_data, true);
        }
    }
#endif // end of CONFIG_OPLUS_TP_APK

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, true);

    hx83112a_nf_f_0f_updat = 0;
    TPD_INFO("%s, END \n", __func__);
    return ;
}

#ifdef HX_0F_DEBUG
void hx83112a_nf_mcu_read_sram_0f(const struct firmware *fw_entry, uint8_t *addr, int start_index, int read_len)
{
    int total_read_times = 0;
    int max_bus_size = MAX_TRANS_SZ;
    int total_size_temp = 0;
    int total_size = 0;
    int address = 0;
    int i = 0, j = 0;
    int not_same = 0;

    uint8_t tmp_addr[4];
    uint8_t *temp_info_data;
    int *not_same_buff;

    TPD_INFO("%s, Entering \n", __func__);

    hx83112a_nf_burst_enable(1);

    total_size = read_len;

    total_size_temp = read_len;
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (read_len > MAX_RECVS_SZ) {
        max_bus_size = MAX_RECVS_SZ;
    } else {
        max_bus_size = read_len;
    }
#else
    if (read_len > 2048) {
        max_bus_size = 2048;
    } else {
        max_bus_size = read_len;
    }
#endif
    temp_info_data = kzalloc (sizeof (uint8_t) * total_size, GFP_KERNEL);
    not_same_buff = kzalloc (sizeof (int) * total_size, GFP_KERNEL);


    tmp_addr[3] = addr[3];
    tmp_addr[2] = addr[2];
    tmp_addr[1] = addr[1];
    tmp_addr[0] = addr[0];
    TPD_INFO("%s, read addr tmp_addr[3] = 0x%2.2X, tmp_addr[2] = 0x%2.2X, tmp_addr[1] = 0x%2.2X, tmp_addr[0] = 0x%2.2X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);

    TPD_INFO("%s, total size=%d\n", __func__, total_size);

    hx83112a_nf_burst_enable(1);

    if (total_size % max_bus_size == 0) {
        total_read_times = total_size / max_bus_size;
    } else {
        total_read_times = total_size / max_bus_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        if (total_size_temp >= max_bus_size) {
            hx83112a_nf_register_read(tmp_addr, max_bus_size, &temp_info_data[i * max_bus_size], false);
            total_size_temp = total_size_temp - max_bus_size;
        } else {
            hx83112a_nf_register_read(tmp_addr, total_size_temp % max_bus_size, &temp_info_data[i * max_bus_size], false);
        }

        address = ((i + 1) * max_bus_size);
        tmp_addr[0] = addr[0] + (uint8_t) ((address) & 0x00FF);
        if (tmp_addr[0] < addr[0]) {
            tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF) + 1;
        } else {
            tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF);
        }

        msleep (10);
    }
    TPD_INFO("%s, READ Start \n", __func__);
    TPD_INFO("%s, start_index = %d \n", __func__, start_index);
    j = start_index;
    for (i = 0; i < read_len; i++, j++) {
        if (hx83112a_nf_chip_info->g_fw_buf[j] != temp_info_data[i]) {
            not_same++;
            not_same_buff[i] = 1;
        }

        TPD_INFO("0x%2.2X, ", temp_info_data[i]);

        if (i > 0 && i % 16 == 15) {
            printk ("\n");
        }
    }
    TPD_INFO("%s, READ END \n", __func__);
    TPD_INFO("%s, Not Same count=%d\n", __func__, not_same);
    if (not_same != 0) {
        j = start_index;
        for (i = 0; i < read_len; i++, j++) {
            if (not_same_buff[i] == 1) {
                TPD_INFO("bin = [%d] 0x%2.2X\n", i, hx83112a_nf_chip_info->g_fw_buf[j]);
            }
        }
        for (i = 0; i < read_len; i++, j++) {
            if (not_same_buff[i] == 1) {
                TPD_INFO("sram = [%d] 0x%2.2X \n", i, temp_info_data[i]);
            }
        }
    }
    TPD_INFO("%s, READ END \n", __func__);
    TPD_INFO("%s, Not Same count=%d\n", __func__, not_same);
    TPD_INFO("%s, END \n", __func__);

    kfree (not_same_buff);
    kfree (temp_info_data);
}

void hx83112a_nf_mcu_read_all_sram(uint8_t *addr, int read_len)
{
    int total_read_times = 0;
    int max_bus_size = MAX_RECVS_SZ;
    int total_size_temp = 0;
    int total_size = 0;
    int address = 0;
    int i = 0;
    /*
    struct file *fn;
    struct filename *vts_name;
    */

    uint8_t tmp_addr[4];
    uint8_t *temp_info_data;

    TPD_INFO("%s, Entering \n", __func__);

    hx83112a_nf_burst_enable(1);

    total_size = read_len;

    total_size_temp = read_len;

    temp_info_data = kzalloc (sizeof (uint8_t) * total_size, GFP_KERNEL);


    tmp_addr[3] = addr[3];
    tmp_addr[2] = addr[2];
    tmp_addr[1] = addr[1];
    tmp_addr[0] = addr[0];
    TPD_INFO("%s, read addr tmp_addr[3] = 0x%2.2X, tmp_addr[2] = 0x%2.2X, tmp_addr[1] = 0x%2.2X, tmp_addr[0] = 0x%2.2X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);

    TPD_INFO("%s, total size=%d\n", __func__, total_size);

    if (total_size % max_bus_size == 0) {
        total_read_times = total_size / max_bus_size;
    } else {
        total_read_times = total_size / max_bus_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        if (total_size_temp >= max_bus_size) {
            hx83112a_nf_register_read(tmp_addr, max_bus_size, &temp_info_data[i * max_bus_size], false);
            total_size_temp = total_size_temp - max_bus_size;
        } else {
            hx83112a_nf_register_read(tmp_addr, total_size_temp % max_bus_size, &temp_info_data[i * max_bus_size], false);
        }

        address = ((i + 1) * max_bus_size);
        tmp_addr[1] = addr[1] + (uint8_t) ((address >> 8) & 0x00FF);
        tmp_addr[0] = addr[0] + (uint8_t) ((address) & 0x00FF);

        msleep (10);
    }
    TPD_INFO("%s, NOW addr tmp_addr[3] = 0x%2.2X, tmp_addr[2] = 0x%2.2X, tmp_addr[1] = 0x%2.2X, tmp_addr[0] = 0x%2.2X\n", __func__, tmp_addr[3], tmp_addr[2], tmp_addr[1], tmp_addr[0]);
    /*for (i = 0;i < read_len;i++) {
        TPD_INFO("0x%2.2X, ", temp_info_data[i]);

        if (i > 0 && i%16 == 15)
            printk("\n");
    }*/

    /* need modify
    TPD_INFO("Now Write File start!\n");
    vts_name = getname_kernel("/sdcard/dump_dsram.txt");
    fn = file_open_name(vts_name, O_CREAT | O_WRONLY, 0);
    if (!IS_ERR (fn)) {
        TPD_INFO("%s create file and ready to write\n", __func__);
        fn->f_op->write (fn, temp_info_data, read_len*sizeof (uint8_t), &fn->f_pos);
        filp_close (fn, NULL);
    }
    TPD_INFO("Now Write File End!\n");
    */

    TPD_INFO("%s, END \n", __func__);

    kfree (temp_info_data);
}

void hx83112a_nf_mcu_firmware_read_0f(const struct firmware *fw_entry, int type)
{
    uint8_t tmp_addr[4] = {0};

    TPD_INFO("%s, Entering \n", __func__);
    if (type == 0) { /* first 48K */
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_sram_start_addr, 0, HX_48K_SZ);
        g_hx83112a_nf_core_fp.fp_read_all_sram (tmp_addr, 0xC000);
    } else { /*last 16k*/
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_cfg_info, 0xC000, 132);
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_fw_cfg, 0xC0FE, 512);
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_adc_cfg_1, 0xD000, 376);
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_adc_cfg_2, 0xD178, 376);
        g_hx83112a_nf_core_fp.fp_read_sram_0f (fw_entry, hx83112a_nf_pzf_op->data_adc_cfg_3, 0xD000, 376);
        g_hx83112a_nf_core_fp.fp_read_all_sram (hx83112a_nf_pzf_op->data_sram_clean, HX_32K_SZ);
    }
    TPD_INFO("%s, END \n", __func__);
}

void hx83112a_nf_mcu_0f_operation_check(int type)
{


    TPD_INFO("%s, Entering \n", __func__);

    TPD_INFO("first 4 bytes 0x%2X, 0x%2X, 0x%2X, 0x%2X !\n", hx83112a_nf_chip_info->g_fw_buf[0], hx83112a_nf_chip_info->g_fw_buf[1], hx83112a_nf_chip_info->g_fw_buf[2], hx83112a_nf_chip_info->g_fw_buf[3]);
    TPD_INFO("next 4 bytes 0x%2X, 0x%2X, 0x%2X, 0x%2X !\n", hx83112a_nf_chip_info->g_fw_buf[4], hx83112a_nf_chip_info->g_fw_buf[5], hx83112a_nf_chip_info->g_fw_buf[6], hx83112a_nf_chip_info->g_fw_buf[7]);
    TPD_INFO("and next 4 bytes 0x%2X, 0x%2X, 0x%2X, 0x%2X !\n", hx83112a_nf_chip_info->g_fw_buf[8], hx83112a_nf_chip_info->g_fw_buf[9], hx83112a_nf_chip_info->g_fw_buf[10], hx83112a_nf_chip_info->g_fw_buf[11]);

    g_hx83112a_nf_core_fp.fp_firmware_read_0f(NULL, type);

    //release_firmware(hx83112a_nf_chip_info->g_fw_entry);
    TPD_INFO("%s, END \n", __func__);
    return ;
}
#endif


int hx83112a_nf_0f_init(void)
{
    hx83112a_nf_pzf_op = kzalloc(sizeof(struct hx83112a_nf_operation), GFP_KERNEL);

    g_hx83112a_nf_core_fp.fp_reload_disable = hx83112a_nf_dis_rload_0f;
    g_hx83112a_nf_core_fp.fp_sys_reset = hx83112a_nf_mcu_sys_reset;
    g_hx83112a_nf_core_fp.fp_clean_sram_0f = hx83112a_nf_mcu_clean_sram_0f;
    g_hx83112a_nf_core_fp.fp_write_sram_0f = hx83112a_nf_mcu_write_sram_0f;
    g_hx83112a_nf_core_fp.fp_firmware_update_0f = hx83112a_nf_mcu_firmware_update_0f;
    g_hx83112a_nf_core_fp.fp_0f_operation = hx83112a_nf_mcu_0f_operation;
    g_hx83112a_nf_core_fp.fp_0f_operation_dirly = hx83112a_nf_mcu_0f_operation_dirly;
    g_hx83112a_nf_core_fp.fp_0f_op_file_dirly = hx83112a_nf_0f_op_file_dirly;
#ifdef HX_0F_DEBUG
    g_hx83112a_nf_core_fp.fp_read_sram_0f = hx83112a_nf_mcu_read_sram_0f;
    g_hx83112a_nf_core_fp.fp_read_all_sram = hx83112a_nf_mcu_read_all_sram;
    g_hx83112a_nf_core_fp.fp_firmware_read_0f = hx83112a_nf_mcu_firmware_read_0f;
    g_hx83112a_nf_core_fp.fp_0f_operation_check = hx83112a_nf_mcu_0f_operation_check;
#endif

    hx83112a_nf_in_parse_assign_cmd(zf_addr_dis_flash_reload, hx83112a_nf_pzf_op->addr_dis_flash_reload, sizeof(hx83112a_nf_pzf_op->addr_dis_flash_reload));
    hx83112a_nf_in_parse_assign_cmd(zf_data_dis_flash_reload, hx83112a_nf_pzf_op->data_dis_flash_reload, sizeof(hx83112a_nf_pzf_op->data_dis_flash_reload));
    hx83112a_nf_in_parse_assign_cmd(zf_addr_system_reset, hx83112a_nf_pzf_op->addr_system_reset, sizeof(hx83112a_nf_pzf_op->addr_system_reset));
    hx83112a_nf_in_parse_assign_cmd(zf_data_system_reset, hx83112a_nf_pzf_op->data_system_reset, sizeof(hx83112a_nf_pzf_op->data_system_reset));
    hx83112a_nf_in_parse_assign_cmd(zf_data_sram_start_addr, hx83112a_nf_pzf_op->data_sram_start_addr, sizeof(hx83112a_nf_pzf_op->data_sram_start_addr));
    hx83112a_nf_in_parse_assign_cmd(zf_data_sram_clean, hx83112a_nf_pzf_op->data_sram_clean, sizeof(hx83112a_nf_pzf_op->data_sram_clean));
    hx83112a_nf_in_parse_assign_cmd(zf_data_cfg_info, hx83112a_nf_pzf_op->data_cfg_info, sizeof(hx83112a_nf_pzf_op->data_cfg_info));
    hx83112a_nf_in_parse_assign_cmd(zf_data_fw_cfg_p1, hx83112a_nf_pzf_op->data_fw_cfg_p1, sizeof(hx83112a_nf_pzf_op->data_fw_cfg_p1));
    hx83112a_nf_in_parse_assign_cmd(zf_data_fw_cfg_p2, hx83112a_nf_pzf_op->data_fw_cfg_p2, sizeof(hx83112a_nf_pzf_op->data_fw_cfg_p2));
    hx83112a_nf_in_parse_assign_cmd(zf_data_fw_cfg_p3, hx83112a_nf_pzf_op->data_fw_cfg_p3, sizeof(hx83112a_nf_pzf_op->data_fw_cfg_p3));
    hx83112a_nf_in_parse_assign_cmd(zf_data_adc_cfg_1, hx83112a_nf_pzf_op->data_adc_cfg_1, sizeof(hx83112a_nf_pzf_op->data_adc_cfg_1));
    hx83112a_nf_in_parse_assign_cmd(zf_data_adc_cfg_2, hx83112a_nf_pzf_op->data_adc_cfg_2, sizeof(hx83112a_nf_pzf_op->data_adc_cfg_2));
    hx83112a_nf_in_parse_assign_cmd(zf_data_adc_cfg_3, hx83112a_nf_pzf_op->data_adc_cfg_3, sizeof(hx83112a_nf_pzf_op->data_adc_cfg_3));
    hx83112a_nf_in_parse_assign_cmd(zf_data_map_table, hx83112a_nf_pzf_op->data_map_table, sizeof(hx83112a_nf_pzf_op->data_map_table));
    hx83112a_nf_in_parse_assign_cmd(zf_data_mode_switch, hx83112a_nf_pzf_op->data_mode_switch, sizeof(hx83112a_nf_pzf_op->data_mode_switch));


    return 0;
}

#endif

bool hx83112a_nf_ic_package_check(void)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t ret_data = 0x00;
    int i = 0;

    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, true); // reset gpio
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, false); // reset gpio
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, true); // reset gpio
    msleep(5);

    hx83112a_nf_enter_safe_mode();

    for (i = 0; i < 5; i++) {
        // Product ID
        // Touch
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xD0;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        TPD_INFO("%s:Read driver IC ID = %X, %X, %X\n", __func__, tmp_data[3], tmp_data[2], tmp_data[1]);
        /*if ((tmp_data[3] == 0x83) && (tmp_data[2] == 0x11) && ((tmp_data[1] == 0x2a) || (tmp_data[1] == 0x2b)))*/
        if (1) {
            HX83112A_NF_IC_TYPE = HX_83112A_SERIES_PWON;
            HX83112A_NF_IC_CHECKSUM = HX_TP_BIN_CHECKSUM_CRC;
            hx83112a_nf_0f_init();
            //Himax: Set FW and CFG Flash Address
            HX83112A_NF_FW_VER_MAJ_FLASH_ADDR   = 49157;  //0x00C005
            HX83112A_NF_FW_VER_MAJ_FLASH_LENG   = 1;
            HX83112A_NF_FW_VER_MIN_FLASH_ADDR   = 49158;  //0x00C006
            HX83112A_NF_FW_VER_MIN_FLASH_LENG   = 1;
            HX83112A_NF_CFG_VER_MAJ_FLASH_ADDR = 49408;  //0x00C100
            HX83112A_NF_CFG_VER_MAJ_FLASH_LENG = 1;
            HX83112A_NF_CFG_VER_MIN_FLASH_ADDR = 49409;  //0x00C101
            HX83112A_NF_CFG_VER_MIN_FLASH_LENG = 1;
            HX83112A_NF_CID_VER_MAJ_FLASH_ADDR = 49154;  //0x00C002
            HX83112A_NF_CID_VER_MAJ_FLASH_LENG = 1;
            HX83112A_NF_CID_VER_MIN_FLASH_ADDR = 49155;  //0x00C003
            HX83112A_NF_CID_VER_MIN_FLASH_LENG = 1;
            //PANEL_VERSION_ADDR = 49156;  //0x00C004
            //PANEL_VERSION_LENG = 1;
#ifdef HX_AUTO_UPDATE_FW
            g_i_FW_VER = i_CTPM_FW[HX83112A_NF_FW_VER_MAJ_FLASH_ADDR] << 8 | i_CTPM_FW[HX83112A_NF_FW_VER_MIN_FLASH_ADDR];
            g_i_CFG_VER = i_CTPM_FW[HX83112A_NF_CFG_VER_MAJ_FLASH_ADDR] << 8 | i_CTPM_FW[HX83112A_NF_CFG_VER_MIN_FLASH_ADDR];
            g_i_CID_MAJ = i_CTPM_FW[HX83112A_NF_CID_VER_MAJ_FLASH_ADDR];
            g_i_CID_MIN = i_CTPM_FW[HX83112A_NF_CID_VER_MIN_FLASH_ADDR];
#endif
            TPD_INFO("Himax IC package 83112_in\n");
            ret_data = true;
            break;
        } else {
            ret_data = false;
            TPD_INFO("%s:Read driver ID register Fail:\n", __func__);
        }
    }

    return ret_data;
}


void hx83112a_nf_power_on_init(void)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    TPD_INFO("%s\n", __func__);

    /*RawOut select initial*/
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x02;
    tmp_addr[1] = 0x04;
    tmp_addr[0] = 0xB4;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

    /*DSRAM func initial*/
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x07;
    tmp_addr[0] = 0xFC;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

    hx83112a_nf_sense_on(0x00);
}


static void hx83112a_nf_read_FW_ver(void)
{
    uint8_t cmd[4];
    uint8_t data[64];
    uint8_t data2[64];
    int retry = 200;
    int reload_status = 0;

    hx83112a_nf_sense_on(0);

    while(reload_status == 0) {
        cmd[3] = 0x10;  // oplus fw id bin address : 0xc014   , 49172    Tp ic address : 0x 10007014
        cmd[2] = 0x00;
        cmd[1] = 0x7f;
        cmd[0] = 0x00;
        hx83112a_nf_register_read(cmd, 4, data, false);

        cmd[3] = 0x10;
        cmd[2] = 0x00;
        cmd[1] = 0x72;
        cmd[0] = 0xc0;
        hx83112a_nf_register_read(cmd, 4, data2, false);

        if ((data[1] == 0x3A && data[0] == 0xA3) || (data2[1] == 0x72 && data2[0] == 0xc0)) {
            TPD_INFO("reload OK! \n");
            reload_status = 1;
            break;
        } else if (retry == 0) {
            TPD_INFO("reload 20 times! fail \n");
            return;
        } else {
            retry--;
            msleep(10);
            TPD_INFO("reload fail, delay 10ms retry=%d\n", retry);
        }
    }
    TPD_INFO("%s : data[0] = 0x%2.2X, data[1] = 0x%2.2X, data[2] = 0x%2.2X, data[3] = 0x%2.2X\n", __func__, data[0], data[1], data[2], data[3]);
    TPD_INFO("reload_status=%d\n", reload_status);

    hx83112a_nf_sense_off();

    //=====================================
    // Read FW version : 0x1000_7004  but 05,06 are the real addr for FW Version
    //=====================================

    cmd[3] = 0x10;
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x04;
    hx83112a_nf_register_read(cmd, 4, data, false);


    TPD_INFO("PANEL_VER : %X \n", data[0]);
    TPD_INFO("FW_VER : %X \n", data[1] << 8 | data[2]);

    cmd[3] = 0x10;
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x84;
    hx83112a_nf_register_read(cmd, 4, data, false);

    TPD_INFO("CFG_VER : %X \n", data[2] << 8 | data[3]);
    TPD_INFO("TOUCH_VER : %X \n", data[2]);
    TPD_INFO("DISPLAY_VER : %X \n", data[3]);

    cmd[3] = 0x10;
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x00;
    hx83112a_nf_register_read(cmd, 4, data, false);

    TPD_INFO("CID_VER : %X \n", ( data[2] << 8 | data[3]) );
    return;
}


void hx83112a_nf_read_OPLUS_FW_ver(struct chip_data_hx83112a_nf *chip_info)
{
    uint8_t cmd[4];
    uint8_t data[4];
    uint32_t touch_ver = 0;

    cmd[3] = 0x10;  // oplus fw id bin address : 0xc014    Tp ic address : 0x 10007014
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x14;
    hx83112a_nf_register_read(cmd, 4, data, false);
    if (data[0] == 0xBD && data[1] == 0x12)
        hx83112a_nf_isBD12proj = true;
    if (data[0] == 0xEA && data[1] == 0x00 && (data[2] & 0xF0) == 0x60)
        hx83112a_nf_isEA006proj = true;
    chip_info->fw_id = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
    TPD_INFO("%s : data[0] = 0x%2.2X, data[1] = 0x%2.2X, data[2] = 0x%2.2X, data[3] = 0x%2.2X\n", __func__, data[0], data[1], data[2], data[3]);

    cmd[3] = 0x10;  // oplus fw id bin address : 0xc014    Tp ic address : 0x 10007014
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x84;
    hx83112a_nf_register_read(cmd, 4, data, false);
    touch_ver = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
    TPD_INFO("%s :touch_ver = 0x%08X\n", __func__, touch_ver);

    cmd[3] = 0x10;
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x00;
    hx83112a_nf_register_read(cmd, 4, data, false);
    chip_info->fw_ver = data[2] << 8 | data[3];
    TPD_INFO("%s :fw_Ver = 0x%04X \n", __func__, chip_info->fw_ver);
    return;
}

uint32_t hx83112a_nf_hw_check_CRC(uint8_t *start_addr, int reload_length)
{
    uint32_t result = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int cnt = 0;
    int length = reload_length / 4;

    //CRC4 // 0x8005_0020 <= from, 0x8005_0028 <= 0x0099_length
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x05;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x20;
    //tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0xFB; tmp_data[0] = 0x00;
    hx83112a_nf_flash_write_burst(tmp_addr, start_addr);

    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x05;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x28;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x99;
    tmp_data[1] = (length >> 8);
    tmp_data[0] = length;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    cnt = 0;
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x05;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x00;
    do {
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        if ((tmp_data[0] & 0x01) != 0x01) {
            tmp_addr[3] = 0x80;
            tmp_addr[2] = 0x05;
            tmp_addr[1] = 0x00;
            tmp_addr[0] = 0x18;
            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
            TPD_DETAIL("%s: tmp_data[3]=%X, tmp_data[2]=%X, tmp_data[1]=%X, tmp_data[0]=%X  \n", __func__, tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
            result = ((tmp_data[3] << 24) + (tmp_data[2] << 16) + (tmp_data[1] << 8) + tmp_data[0]);
            break;
        }
    } while (cnt++ < 100);

    return result;
}


bool hx83112a_nf_calculateChecksum(bool change_iref)
{
    uint8_t CRC_result = 0;
    uint8_t tmp_data[4];

    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;

    CRC_result = hx83112a_nf_hw_check_CRC(tmp_data, FW_SIZE_64k);

    msleep(50);

    return !CRC_result;
}

int hx83112a_nf_cal_data_len(int raw_cnt_rmd, int HX_MAX_PT, int raw_cnt_max)
{
    int RawDataLen;
    if (raw_cnt_rmd != 0x00) {
        RawDataLen = 128 - ((HX_MAX_PT + raw_cnt_max + 3) * 4) - 1;
    } else {
        RawDataLen = 128 - ((HX_MAX_PT + raw_cnt_max + 2) * 4) - 1;
    }
    return RawDataLen;
}


int hx83112a_nf_report_data_init(int max_touch_point, int tx_num, int rx_num)
{
    if (hx83112a_nf_touch_data->hx_coord_buf != NULL) {
        kfree(hx83112a_nf_touch_data->hx_coord_buf);
    }

    if (hx83112a_nf_touch_data->diag_mutual != NULL) {
        kfree(hx83112a_nf_touch_data->diag_mutual);
    }

    //#if defined(HX_SMART_WAKEUP)
    hx83112a_nf_touch_data->event_size = 128;
    //#endif*/

    hx83112a_nf_touch_data->touch_all_size = 128; //hx83112a_nf_get_touch_data_size();

    HX83112A_NF_TOUCH_INFO_POINT_CNT = max_touch_point * 4 ;

    if ((max_touch_point % 4) == 0)
        HX83112A_NF_TOUCH_INFO_POINT_CNT += (max_touch_point / 4) * 4 ;
    else
        HX83112A_NF_TOUCH_INFO_POINT_CNT += ((max_touch_point / 4) + 1) * 4 ;

    hx83112a_nf_touch_data->raw_cnt_max = max_touch_point / 4;
    hx83112a_nf_touch_data->raw_cnt_rmd = max_touch_point % 4;

    if (hx83112a_nf_touch_data->raw_cnt_rmd != 0x00) {//more than 4 fingers
        hx83112a_nf_touch_data->rawdata_size = hx83112a_nf_cal_data_len(hx83112a_nf_touch_data->raw_cnt_rmd, max_touch_point, hx83112a_nf_touch_data->raw_cnt_max);
        hx83112a_nf_touch_data->touch_info_size = (max_touch_point + hx83112a_nf_touch_data->raw_cnt_max + 2) * 4;
    } else {//less than 4 fingers
        hx83112a_nf_touch_data->rawdata_size = hx83112a_nf_cal_data_len(hx83112a_nf_touch_data->raw_cnt_rmd, max_touch_point, hx83112a_nf_touch_data->raw_cnt_max);
        hx83112a_nf_touch_data->touch_info_size = (max_touch_point + hx83112a_nf_touch_data->raw_cnt_max + 1) * 4;
    }

    if ((tx_num * rx_num + tx_num + rx_num) % hx83112a_nf_touch_data->rawdata_size == 0) {
        hx83112a_nf_touch_data->rawdata_frame_size = (tx_num * rx_num + tx_num + rx_num) / hx83112a_nf_touch_data->rawdata_size;
    } else {
        hx83112a_nf_touch_data->rawdata_frame_size = (tx_num * rx_num + tx_num + rx_num) / hx83112a_nf_touch_data->rawdata_size + 1;
    }
    TPD_INFO("%s: rawdata_frame_size = %d ", __func__, hx83112a_nf_touch_data->rawdata_frame_size);
    TPD_INFO("%s: max_touch_point:%d, hx_raw_cnt_max:%d, hx_raw_cnt_rmd:%d, g_hx_rawdata_size:%d, hx83112a_nf_touch_data->touch_info_size:%d\n",
             __func__, max_touch_point, hx83112a_nf_touch_data->raw_cnt_max, hx83112a_nf_touch_data->raw_cnt_rmd, hx83112a_nf_touch_data->rawdata_size, hx83112a_nf_touch_data->touch_info_size);

    hx83112a_nf_touch_data->hx_coord_buf = kzalloc(sizeof(uint8_t) * (hx83112a_nf_touch_data->touch_info_size), GFP_KERNEL);
    if (hx83112a_nf_touch_data->hx_coord_buf == NULL) {
        goto mem_alloc_fail;
    }

    hx83112a_nf_touch_data->diag_mutual = kzalloc(tx_num * rx_num * sizeof(int32_t), GFP_KERNEL);
    if (hx83112a_nf_touch_data->diag_mutual == NULL) {
        goto mem_alloc_fail;
    }

    //#ifdef HX_TP_PROC_DIAG
    hx83112a_nf_touch_data->hx_rawdata_buf = kzalloc(sizeof(uint8_t) * (hx83112a_nf_touch_data->touch_all_size - hx83112a_nf_touch_data->touch_info_size), GFP_KERNEL);
    if (hx83112a_nf_touch_data->hx_rawdata_buf == NULL) {
        goto mem_alloc_fail;
    }
    //#endif

    //#if defined(HX_SMART_WAKEUP)
    hx83112a_nf_touch_data->hx_event_buf = kzalloc(sizeof(uint8_t) * (hx83112a_nf_touch_data->event_size), GFP_KERNEL);
    if (hx83112a_nf_touch_data->hx_event_buf == NULL) {
        goto mem_alloc_fail;
    }
    //#endif

    return NO_ERR;

mem_alloc_fail:
    kfree(hx83112a_nf_touch_data->hx_coord_buf);
    //#if defined(HX_TP_PROC_DIAG)
    kfree(hx83112a_nf_touch_data->hx_rawdata_buf);
    //#endif
    //#if defined(HX_SMART_WAKEUP)
    kfree(hx83112a_nf_touch_data->hx_event_buf);
    //#endif

    TPD_INFO("%s: Memory allocate fail!\n", __func__);
    return MEM_ALLOC_FAIL;

}

bool hx83112a_nf_read_event_stack(uint8_t *buf, uint8_t length)
{
    uint8_t cmd[4];

    //  AHB_I2C Burst Read Off
    cmd[0] = 0x00;
    if (hx83112a_nf_bus_write(0x11, 1, cmd) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return 0;
    }

    hx83112a_nf_bus_read(0x30, length, buf);

    //  AHB_I2C Burst Read On
    cmd[0] = 0x01;
    if (hx83112a_nf_bus_write(0x11, 1, cmd) < 0) {
        TPD_INFO("%s: i2c access fail!\n", __func__);
        return 0;
    }
    return 1;
}

int hx83112a_nf_zero_event_count = 0;
int hx83112a_nf_ic_esd_recovery(int hx_esd_event, int hx_zero_event, int length)
{
    if (hx_esd_event == length) {
        hx83112a_nf_zero_event_count = 0;
        goto checksum_fail;
    } else if (hx_zero_event == length) {
        hx83112a_nf_zero_event_count++;
        TPD_INFO("[HIMAX TP MSG]: ALL Zero event is %d times.\n", hx83112a_nf_zero_event_count);
        if (hx83112a_nf_zero_event_count > 5) {
            hx83112a_nf_zero_event_count = 0;
            TPD_INFO("[HIMAX TP MSG]: ESD event checked - ALL Zero.\n");
            goto checksum_fail;
        }
        goto err_workqueue_out;
    }

checksum_fail:
    return CHECKSUM_FAIL;
err_workqueue_out:
    return WORK_OUT;
}

static int hx83112a_nf_resetgpio_set(struct hw_resource *hw_res, bool on)
{
    int ret = 0;
    if (gpio_is_valid(hw_res->reset_gpio)) {
        TPD_DETAIL("Set the reset_gpio on=%d \n", on);
        ret = gpio_direction_output(hw_res->reset_gpio, on);
        if (ret) {
            TPD_INFO("Set the reset_gpio on=%d fail\n", on);
        } else {
            HX83112A_NF_RESET_STATE = on;
        }
        msleep(RESET_TO_NORMAL_TIME);
        TPD_DETAIL("%s hw_res->reset_gpio = %d\n", __func__, hw_res->reset_gpio);
    }

    return ret;
}

void hx83112a_nf_esd_hw_reset(struct chip_data_hx83112a_nf *chip_info)
{
    int ret = 0;
    int load_fw_times = 10;

    TPD_DETAIL("START_Himax TP: ESD - Reset\n");
    HX83112A_NF_ESD_RESET_ACTIVATE = 1;

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    do {
        hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio
        hx83112a_nf_resetgpio_set(chip_info->hw_res, false); // reset gpio
        hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio

        TPD_DETAIL("%s: ESD reset finished\n", __func__);

        TPD_DETAIL("It will update fw after esd event in zero flash mode!\n");

        load_fw_times--;
        g_hx83112a_nf_core_fp.fp_0f_operation_dirly();
        ret = g_hx83112a_nf_core_fp.fp_reload_disable(0);//success return 1, fail return 0
    } while (!ret && load_fw_times > 0);

    if (!load_fw_times) {
        TPD_INFO("%s: load_fw_times over 10 times\n", __func__);
    }

    hx83112a_nf_sense_on(0x00);
    /* need_modify*/
    /* report all leave event
    hx83112a_nf_report_all_leave_event(hx83112a_nf_pri_ts);*/

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, true);
}


int hx83112a_nf_checksum_cal(struct chip_data_hx83112a_nf *chip_info, uint8_t *buf, int ts_status)
{
    //#if defined(HX_ESD_RECOVERY)
    int hx_EB_event = 0;
    int hx_EC_event = 0;
    int hx_ED_event = 0;
    int hx_esd_event = 0;
    int hx_zero_event = 0;
    int shaking_ret = 0;
    //#endif
    uint16_t check_sum_cal = 0;
    int32_t loop_i = 0;
    int length = 0;

    /* Normal */
    if (ts_status == HX_REPORT_COORD) {
        length = hx83112a_nf_touch_data->touch_info_size;
    }

    /* SMWP */
    else if (ts_status == HX_REPORT_SMWP_EVENT) {
        length = (GEST_PTLG_ID_LEN + GEST_PTLG_HDR_LEN);
    } else {
        TPD_INFO("%s, Neither Normal Nor SMWP error!\n", __func__);
    }
    //TPD_INFO("Now status=%d,length=%d\n",ts_status,length);
    for (loop_i = 0; loop_i < length; loop_i++) {
        check_sum_cal += buf[loop_i];
        /* #ifdef HX_ESD_RECOVERY  */
        if (ts_status == HX_REPORT_COORD) {
            /* case 1 ESD recovery flow */
            if (buf[loop_i] == 0xEB) {
                hx_EB_event++;
            } else if (buf[loop_i] == 0xEC) {
                hx_EC_event++;
            } else if (buf[loop_i] == 0xED) {
                hx_ED_event++;
            } else if (buf[loop_i] == 0x00) {/* case 2 ESD recovery flow-Disable */
                hx_zero_event++;
            } else {
                hx_EB_event = 0;
                hx_EC_event = 0;
                hx_ED_event = 0;
                hx_zero_event = 0;
                hx83112a_nf_zero_event_count = 0;
            }

            if (hx_EB_event == length) {
                hx_esd_event = length;
                hx83112a_nf_EB_event_flag++;
                TPD_INFO("[HIMAX TP MSG]: ESD event checked - ALL 0xEB.\n");
            } else if (hx_EC_event == length) {
                hx_esd_event = length;
                hx83112a_nf_EC_event_flag++;
                TPD_INFO("[HIMAX TP MSG]: ESD event checked - ALL 0xEC.\n");
            } else if (hx_ED_event == length) {
                hx_esd_event = length;
                hx83112a_nf_ED_event_flag++;
                TPD_INFO("[HIMAX TP MSG]: ESD event checked - ALL 0xED.\n");
            } else {
                hx_esd_event = 0;
            }
        }
        /* #endif */
    }

    if (ts_status == HX_REPORT_COORD) {
        //#ifdef HX_ESD_RECOVERY
        if (hx_esd_event == length || hx_zero_event == length) {
            shaking_ret = hx83112a_nf_ic_esd_recovery(hx_esd_event, hx_zero_event, length);
            if (shaking_ret == CHECKSUM_FAIL) {
                hx83112a_nf_esd_hw_reset(chip_info);
                goto checksum_fail;
            } else if (shaking_ret == ERR_WORK_OUT) {
                goto err_workqueue_out;
            } else {
                //TPD_INFO("I2C running. Nothing to be done!\n");
                goto workqueue_out;
            }
        } else if (HX83112A_NF_ESD_RESET_ACTIVATE) {
            /* drop 1st interrupts after chip reset */
            HX83112A_NF_ESD_RESET_ACTIVATE = 0;
            TPD_INFO("[HX83112A_NF_ESD_RESET_ACTIVATE]:%s: Back from reset, ready to serve.\n", __func__);
            goto checksum_fail;
        } else if (HX83112A_NF_HW_RESET_ACTIVATE) {
            /* drop 1st interrupts after chip reset */
            HX83112A_NF_HW_RESET_ACTIVATE = 0;
            TPD_INFO("[HX83112A_NF_HW_RESET_ACTIVATE]:%s: Back from reset, ready to serve.\n", __func__);
            goto ready_to_serve;
        }
    }
    //#endif
    if ((check_sum_cal % 0x100 != 0) ) {
        TPD_INFO("[HIMAX TP MSG] checksum fail : check_sum_cal: 0x%02X\n", check_sum_cal);
        //goto checksum_fail;
        goto workqueue_out;
    }

    /* TPD_INFO("%s:End\n",__func__); */
    return NO_ERR;

ready_to_serve:
    return READY_TO_SERVE;
checksum_fail:
    return CHECKSUM_FAIL;
    //#ifdef HX_ESD_RECOVERY
err_workqueue_out:
    return ERR_WORK_OUT;
workqueue_out:
    return WORK_OUT;
    //#endif
}

void hx83112a_nf_log_touch_data(uint8_t *buf, struct hx83112a_nf_report_data *hx83112a_nf_touch_data)
{
    int loop_i = 0;
    int print_size = 0;

    if (!hx83112a_nf_touch_data->diag_cmd) {
        print_size = hx83112a_nf_touch_data->touch_info_size;
    } else {
        print_size = hx83112a_nf_touch_data->touch_all_size;
    }

    for (loop_i = 0; loop_i < print_size; loop_i++) {
        printk("0x%02X ",  buf[loop_i]);
        if((loop_i + 1) % 8 == 0) {
            printk("\n");
        }
        if (loop_i == (print_size - 1)) {
            printk("\n");
        }
    }
}

void hx83112a_nf_idle_mode(int disable)
{
    int retry = 20;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t switch_cmd = 0x00;

    TPD_INFO("%s:entering\n", __func__);
    do {
        TPD_INFO("%s,now %d times\n!", __func__, retry);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x70;
        tmp_addr[0] = 0x88;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

        if (disable)
            switch_cmd = 0x17;
        else
            switch_cmd = 0x1F;

        tmp_data[0] = switch_cmd;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s:After turn ON/OFF IDLE Mode [0] = 0x%02X, [1] = 0x%02X, [2] = 0x%02X, [3] = 0x%02X\n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        retry--;
        msleep(10);
    } while ((tmp_data[0] != switch_cmd) && retry > 0);

    TPD_INFO("%s: setting OK!\n", __func__);
}


void hx83112a_nf_reload_disable(int on)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    TPD_INFO("%s:entering\n", __func__);

    if (on) {/*reload disable*/
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
    } else {/*reload enable*/
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;
    }

    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    TPD_INFO("%s: setting OK!\n", __func__);
}

int hx83112a_nf_test_data_pop_out(struct chip_data_hx83112a_nf *chip_info, char *g_Test_list_log, char *g_Company_info_log, char *g_project_test_info_log, char *rslt_buf, char *filepath)
{
    struct file *raw_file = NULL;
    struct filename *vts_name = NULL;
    char *line = "=================================================\n";
    char *Company = "Himax for OPLUS: Driver Seneor Test\n";
    char *Info = "Test Info as follow\n";
    char *project_name_log = "OPLUS_";
    mm_segment_t fs;
    loff_t pos = 0;
    int ret_val = NO_ERR;

    TPD_INFO("%s: Entering!\n", __func__);
    TPD_INFO("data size = 0x%04X\n", (uint32_t)strlen(rslt_buf));

    /*Company Info*/
    snprintf(g_Company_info_log, 160, "%s%s%s%s", line, Company, Info, line);
    TPD_DETAIL("%s 000: %s \n", __func__, g_Company_info_log);

    /*project Info*/
    snprintf(g_project_test_info_log, 118, "Project_name: %s%d\nFW_ID: %8X\nFW_Ver: %4X\nPanel Info: TX_Num=%d RX_Num=%d\nTest stage: Mobile\n",
             project_name_log, get_project(), chip_info->fw_id, chip_info->fw_ver, chip_info->hw_res->TX_NUM, chip_info->hw_res->RX_NUM);
    TPD_DETAIL("%s 001: %s \n", __func__, g_project_test_info_log);

    vts_name = getname_kernel(filepath);
    //if (IS_ERR(vts_name))
    //    return ERR_CAST(vts_name);
    if (raw_file == NULL)
        raw_file = file_open_name(vts_name, O_TRUNC | O_CREAT | O_RDWR, 0660);

    if (IS_ERR(raw_file)) {
        TPD_INFO("%s open file failed = %ld\n", __func__, PTR_ERR(raw_file));
        ret_val = -EIO;
        goto SAVE_DATA_ERR;
    }

    fs = get_fs();
    set_fs(get_ds());
    vfs_write(raw_file, g_Company_info_log, (int)(strlen(g_Company_info_log)), &pos);
    pos = pos + (int)(strlen(g_Company_info_log));

    vfs_write(raw_file, g_project_test_info_log, (int)(strlen(g_project_test_info_log)), &pos);
    pos = pos + (int)(strlen(g_project_test_info_log));

    vfs_write(raw_file, g_Test_list_log, (int)(strlen(g_Test_list_log)), &pos);
    pos = pos + (int)(strlen(g_Test_list_log));

    vfs_write(raw_file, rslt_buf, hx83112a_nf_1kind_raw_size * HX83112A_NF_CRITERIA_ITEM * sizeof(char), &pos);
    if (raw_file != NULL)
        filp_close(raw_file, NULL);

    set_fs(fs);

SAVE_DATA_ERR:
    TPD_INFO("%s: End!\n", __func__);
    return ret_val;
}


int hx83112a_nf_test_data_get(struct chip_data_hx83112a_nf *chip_info, uint32_t RAW[], char *start_log, char *result, int now_item)
{
    uint32_t i;

    ssize_t len = 0;
    char *testdata = NULL;
    uint32_t SZ_SIZE = hx83112a_nf_1kind_raw_size;

    TPD_INFO("%s: Entering, Now type=%s!\n", __func__,
             hx83112a_nf_inspection_mode[now_item]);

    testdata = kzalloc(sizeof(char) * SZ_SIZE, GFP_KERNEL);
    if (!testdata) {
        TPD_INFO("%s:%d testdata kzalloc buf error\n", __func__, __LINE__);
        return -ENOMEM;
    }
    len += snprintf((testdata + len), SZ_SIZE - len, "%s", start_log);
    for (i = 0; i < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM; i++) {
        if (i > 1 && ((i + 1) % chip_info->hw_res->RX_NUM) == 0)
            len += snprintf((testdata + len), SZ_SIZE - len, "%5d,\n", RAW[i]);
        else
            len += snprintf((testdata + len), SZ_SIZE - len,
                            "%5d,", RAW[i]);
    }
    len += snprintf((testdata + len), SZ_SIZE - len, "\n%s", result);

    memcpy(&hx83112a_nf_rslt_data[hx83112a_nf_rslt_data_len], testdata, len);
    hx83112a_nf_rslt_data_len += len;
    TPD_INFO("%s: hx83112a_nf_rslt_data_len=%d!\n", __func__, hx83112a_nf_rslt_data_len);

    kfree(testdata);
    TPD_INFO("%s: End!\n", __func__);
    return NO_ERR;

}


int hx83112a_nf_get_rawdata(struct chip_data_hx83112a_nf *chip_info, uint32_t *RAW, uint32_t datalen)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t *tmp_rawdata;
    uint8_t retry = 0;
    uint16_t checksum_cal;
    uint32_t i = 0;

    uint8_t max_i2c_size = MAX_RECVS_SZ;
    int address = 0;
    int total_read_times = 0;
    int total_size = datalen * 2 + 4;
    int total_size_temp;
#if 1//def RAWDATA_DEBUG_PF
    uint32_t j = 0;
    uint32_t index = 0;
    uint32_t Min_DATA = 0xFFFFFFFF;
    uint32_t Max_DATA = 0x00000000;
#endif

    //1 Set Data Ready PWD
    while (retry < 200) {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = Data_PWD1;
        tmp_data[0] = Data_PWD0;
        hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data, 4);

        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        if ((tmp_data[0] == Data_PWD0 && tmp_data[1] == Data_PWD1) ||
            (tmp_data[0] == Data_PWD1 && tmp_data[1] == Data_PWD0)) {
            break;
        }

        retry++;
        msleep(1);
    }

    if (retry >= 200) {
        return RESULT_ERR;
    } else {
        retry = 0;
    }

    while (retry < 200) {
        if (tmp_data[0] == Data_PWD1 && tmp_data[1] == Data_PWD0) {
            break;
        }

        retry++;
        msleep(1);
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    }

    if (retry >= 200) {
        return RESULT_ERR;
    } else {
        retry = 0;
    }

    //tmp_rawdata = kzalloc(sizeof(uint8_t)*(datalen*2),GFP_KERNEL);
    tmp_rawdata = kzalloc(sizeof(uint8_t) * (total_size + 8), GFP_KERNEL);
    if (!tmp_rawdata) {
        return RESULT_ERR;
    }

    //2 Read Data from SRAM
    while (retry < 10) {
        checksum_cal = 0;
        total_size_temp = total_size;
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;

        if (total_size % max_i2c_size == 0) {
            total_read_times = total_size / max_i2c_size;
        } else {
            total_read_times = total_size / max_i2c_size + 1;
        }

        for (i = 0; i < (total_read_times); i++) {
            if (total_size_temp >= max_i2c_size) {
                hx83112a_nf_register_read(tmp_addr, max_i2c_size, &tmp_rawdata[i * max_i2c_size], false);
                total_size_temp = total_size_temp - max_i2c_size;
            } else {
                //TPD_INFO("last total_size_temp=%d\n",total_size_temp);
                hx83112a_nf_register_read(tmp_addr, total_size_temp % max_i2c_size, &tmp_rawdata[i * max_i2c_size], false);
            }

            address = ((i + 1) * max_i2c_size);
            tmp_addr[1] = (uint8_t)((address >> 8) & 0x00FF);
            tmp_addr[0] = (uint8_t)((address) & 0x00FF);
        }

        //
        //3 Check Checksum
        for (i = 2; i < total_size; i = i + 2) {
            checksum_cal += tmp_rawdata[i + 1] * 256 + tmp_rawdata[i];
        }

        if (checksum_cal == 0) {
            break;
        }

        retry++;
    }
    if (retry >= 10) {
        TPD_INFO("Retry over 10 times: do recovery\n");
        hx83112a_nf_esd_hw_reset(chip_info);
        return RESULT_RETRY;
    }

    //4 Copy Data
    for (i = 0; i < chip_info->hw_res->RX_NUM * chip_info->hw_res->TX_NUM; i++) {
        RAW[i] = tmp_rawdata[(i * 2) + 1 + 4] * 256 + tmp_rawdata[(i * 2) + 4];
    }


#if 1//def RAWDATA_DEBUG_PF
    for (j = 0; j < chip_info->hw_res->TX_NUM; j++) {
        if (j == 0) {
            printk("      RX%2d", j + 1);
        } else {
            printk("  RX%2d", j + 1);
        }
    }
    printk("\n");

    for (i = 0; i < chip_info->hw_res->RX_NUM; i++) {
        printk("TX%2d", i + 1);
        for (j = 0; j < chip_info->hw_res->TX_NUM; j++) {
            //if ((j == SKIPRXNUM) && (i >= SKIPTXNUM_START) && (i <= SKIPTXNUM_END)) {
            // continue;
            //} else {
            index = ((chip_info->hw_res->RX_NUM * chip_info->hw_res->TX_NUM - i) - chip_info->hw_res->RX_NUM * j) - 1;

            printk("%6d", RAW[index]);

            if (RAW[index] > Max_DATA) {
                Max_DATA = RAW[index];
            }

            if (RAW[index] < Min_DATA) {
                Min_DATA = RAW[index];
            }
            //}
        }
        //index++;
        printk("\n");
    }

    TPD_INFO("Max = %5d, Min = %5d \n", Max_DATA, Min_DATA);
#endif

    kfree(tmp_rawdata);

    return RESULT_OK;
}

void hx83112a_nf_switch_data_type(uint8_t checktype)
{
    uint8_t datatype = 0x00;

    switch (checktype) {
    case HIMAX_INSPECTION_OPEN:
        datatype = DATA_OPEN;
        break;
    case HIMAX_INSPECTION_MICRO_OPEN:
        datatype = DATA_MICRO_OPEN;
        break;
    case HIMAX_INSPECTION_SHORT:
        datatype = DATA_SHORT;
        break;
    case HIMAX_INSPECTION_RAWDATA:
        datatype = DATA_RAWDATA;
        break;
    case HIMAX_INSPECTION_NOISE:
        datatype = DATA_NOISE;
        break;
    case HIMAX_INSPECTION_BACK_NORMAL:
        datatype = DATA_BACK_NORMAL;
        break;
    case HIMAX_INSPECTION_LPWUG_RAWDATA:
        datatype = DATA_LPWUG_RAWDATA;
        break;
    case HIMAX_INSPECTION_LPWUG_NOISE:
        datatype = DATA_LPWUG_NOISE;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
        datatype = DATA_LPWUG_IDLE_RAWDATA;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        datatype = DATA_LPWUG_IDLE_NOISE;
        break;
    default:
        TPD_INFO("Wrong type=%d\n", checktype);
        break;
    }
    hx83112a_nf_diag_register_set(datatype);
}

int hx83112a_nf_switch_mode(int mode)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    TPD_INFO("%s: Entering\n", __func__);

    //Stop Handshaking
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x00;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data, 4);

    //Swtich Mode
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x7F;
    tmp_addr[0] = 0x04;
    switch (mode) {
    case HIMAX_INSPECTION_SORTING:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_SORTING_START;
        tmp_data[0] = PWD_SORTING_START;
        break;
    case HIMAX_INSPECTION_OPEN:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_OPEN_START;
        tmp_data[0] = PWD_OPEN_START;
        break;
    case HIMAX_INSPECTION_MICRO_OPEN:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_OPEN_START;
        tmp_data[0] = PWD_OPEN_START;
        break;
    case HIMAX_INSPECTION_SHORT:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_SHORT_START;
        tmp_data[0] = PWD_SHORT_START;
        break;
    case HIMAX_INSPECTION_RAWDATA:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_RAWDATA_START;
        tmp_data[0] = PWD_RAWDATA_START;
        break;
    case HIMAX_INSPECTION_NOISE:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_NOISE_START;
        tmp_data[0] = PWD_NOISE_START;
        break;
    case HIMAX_INSPECTION_LPWUG_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_NOISE:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_LPWUG_START;
        tmp_data[0] = PWD_LPWUG_START;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = PWD_LPWUG_IDLE_START;
        tmp_data[0] = PWD_LPWUG_IDLE_START;
        break;
    }
    hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data, 4);

    TPD_INFO("%s: End of setting!\n", __func__);

    return 0;

}


void hx83112a_nf_set_N_frame(uint16_t Nframe, uint8_t checktype)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x72;
    tmp_addr[0] = 0x94;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = (uint8_t)((Nframe & 0xFF00) >> 8);
    tmp_data[0] = (uint8_t)(Nframe & 0x00FF);
    hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data, 4);

    //SKIP FRMAE
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x70;
    tmp_addr[0] = 0xF4;
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

    switch (checktype) {
    case HIMAX_INSPECTION_LPWUG_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_NOISE:
        tmp_data[0] = BS_LPWUG;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        tmp_data[0] = BS_LPWUG_dile;
        break;
    case HIMAX_INSPECTION_RAWDATA:
    case HIMAX_INSPECTION_NOISE:
        tmp_data[0] = BS_RAWDATANOISE;
        break;
    default:
        tmp_data[0] = BS_OPENSHORT;
        break;
    }
    hx83112a_nf_flash_write_burst_length(tmp_addr, tmp_data, 4);
}


uint32_t hx83112a_nf_check_mode(uint8_t checktype)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t wait_pwd[2];
    // uint8_t count = 0;

    wait_pwd[0] = PWD_NONE;
    wait_pwd[1] = PWD_NONE;

    switch (checktype) {
    case HIMAX_INSPECTION_OPEN:
        wait_pwd[0] = PWD_OPEN_END;
        wait_pwd[1] = PWD_OPEN_END;
        break;
    case HIMAX_INSPECTION_MICRO_OPEN:
        wait_pwd[0] = PWD_OPEN_END;
        wait_pwd[1] = PWD_OPEN_END;
        break;
    case HIMAX_INSPECTION_SHORT:
        wait_pwd[0] = PWD_SHORT_END;
        wait_pwd[1] = PWD_SHORT_END;
        break;
    case HIMAX_INSPECTION_RAWDATA:
        wait_pwd[0] = PWD_RAWDATA_END;
        wait_pwd[1] = PWD_RAWDATA_END;
        break;
    case HIMAX_INSPECTION_NOISE:
        wait_pwd[0] = PWD_NOISE_END;
        wait_pwd[1] = PWD_NOISE_END;
        break;
    case HIMAX_INSPECTION_LPWUG_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_NOISE:
        wait_pwd[0] = PWD_LPWUG_END;
        wait_pwd[1] = PWD_LPWUG_END;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        wait_pwd[0] = PWD_LPWUG_IDLE_END;
        wait_pwd[1] = PWD_LPWUG_IDLE_END;
        break;
    default:
        TPD_INFO("Wrong type=%d\n", checktype);
        break;
    }

    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x7F;
    tmp_addr[0] = 0x04;
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    TPD_INFO("%s: hx83112a_nf_wait_sorting_mode, tmp_data[0]=%x, tmp_data[1]=%x\n", __func__, tmp_data[0], tmp_data[1]);

    if (wait_pwd[0] == tmp_data[0] && wait_pwd[1] == tmp_data[1]) {
        TPD_INFO("Change to mode=%s\n", hx83112a_nf_inspection_mode[checktype]);
        return 0;
    } else
        return 1;
}

void hx83112a_nf_get_noise_base(void)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t ratio, threshold, threshold_LPWUG;

    /*tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x70;
    tmp_addr[0] = 0x8C;
    */
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x70;
    tmp_addr[0] = 0x94; /*ratio*/
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    ratio = tmp_data[1];

    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x70;
    tmp_addr[0] = 0xA0; /*threshold_LPWUG*/
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    threshold_LPWUG = tmp_data[0];

    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x70;
    tmp_addr[0] = 0x9C; /*threshold*/
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    threshold = tmp_data[0];

    /*TPD_INFO("tmp_data[0]=0x%x tmp_data[1]=0x%x tmp_data[2]=0x%x tmp_data[3]=0x%x\n",
              tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
    */
    /*NOISEMAX = tmp_data[3]*(NOISE_P/256);*/
    NOISEMAX = ratio * threshold;
    LPWUG_NOISEMAX = ratio * threshold_LPWUG;
    TPD_INFO("NOISEMAX=%d LPWUG_NOISE_MAX=%d \n", NOISEMAX, LPWUG_NOISEMAX);
}

uint16_t hx83112a_nf_get_noise_weight(void)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint16_t weight;

    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x72;
    tmp_addr[0] = 0xC8;
    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
    weight = (tmp_data[1] << 8) | tmp_data[0];
    TPD_INFO("%s: weight = %d ", __func__, weight);

    return weight;
}

uint32_t hx83112a_nf_wait_sorting_mode(uint8_t checktype)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t wait_pwd[2];
    uint8_t count = 0;

    wait_pwd[0] = PWD_NONE;
    wait_pwd[1] = PWD_NONE;

    switch (checktype) {
    case HIMAX_INSPECTION_OPEN:
        wait_pwd[0] = PWD_OPEN_END;
        wait_pwd[1] = PWD_OPEN_END;
        break;
    case HIMAX_INSPECTION_MICRO_OPEN:
        wait_pwd[0] = PWD_OPEN_END;
        wait_pwd[1] = PWD_OPEN_END;
        break;
    case HIMAX_INSPECTION_SHORT:
        wait_pwd[0] = PWD_SHORT_END;
        wait_pwd[1] = PWD_SHORT_END;
        break;
    case HIMAX_INSPECTION_RAWDATA:
        wait_pwd[0] = PWD_RAWDATA_END;
        wait_pwd[1] = PWD_RAWDATA_END;
        break;
    case HIMAX_INSPECTION_NOISE:
        wait_pwd[0] = PWD_NOISE_END;
        wait_pwd[1] = PWD_NOISE_END;
        break;
    case HIMAX_INSPECTION_LPWUG_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_NOISE:
        wait_pwd[0] = PWD_LPWUG_END;
        wait_pwd[1] = PWD_LPWUG_END;
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        wait_pwd[0] = PWD_LPWUG_IDLE_END;
        wait_pwd[1] = PWD_LPWUG_IDLE_END;
        break;
    default:
        TPD_INFO("Wrong type=%d\n", checktype);
        break;
    }

    do {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x04;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: hx83112a_nf_wait_sorting_mode, tmp_data[0]=%x, tmp_data[1]=%x\n", __func__, tmp_data[0], tmp_data[1]);

        if (wait_pwd[0] == tmp_data[0] && wait_pwd[1] == tmp_data[1]) {
            return 0;
        }
        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xA8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 0x900000A8, tmp_data[0]=%x, tmp_data[1]=%x, tmp_data[2]=%x, tmp_data[3]=%x \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xE4;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 0x900000E4, tmp_data[0]=%x, tmp_data[1]=%x, tmp_data[2]=%x, tmp_data[3]=%x \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xF8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 0x900000F8, tmp_data[0]=%x, tmp_data[1]=%x, tmp_data[2]=%x, tmp_data[3]=%x \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
        TPD_INFO("Now retry %d times!\n", count++);
        msleep(50);
    } while (count < 50);

    return 1;
}

int hx83112a_nf_check_notch(int index)
{
    int tx = hx83112a_nf_chip_info->hw_res->TX_NUM;
    int rx = hx83112a_nf_chip_info->hw_res->RX_NUM;
    if (((tx - 4) / 2 <= index / 36 && index / 36 <= (tx - 4) / 2 + 3) && (index % 36 >= rx - 1)) {
        return 1;
    } else {
        return 0;
    }
}

int hx83112a_nf_mpTestFunc(struct chip_data_hx83112a_nf *chip_info, uint8_t checktype, uint32_t datalen)
{
    uint8_t tmp_addr[4] = {0};
    uint8_t tmp_data[4] = {0};

    uint32_t i/*, j*/ = 0;
    uint16_t weight = 0;
    uint32_t RAW[datalen];
#ifdef RAWDATA_NOISE
    uint32_t RAW_Rawdata[datalen];
#endif
    char *rslt_log = NULL;
    char *start_log = NULL;
    int ret = 0;
    int CRITERIA_RAWDATA_MIN = RAWMIN;
    int CRITERIA_RAWDATA_MAX = RAWMAX;
    int CRITERIA_LPWUG_RAWDATA_MAX = LPWUG_RAWDATA_MAX;
    int CRITERIA_LPWUG_IDLE_RAWDATA_MAX = LPWUG_IDLE_RAWDATA_MAX;

    if (hx83112a_nf_isBD12proj) {
        CRITERIA_RAWDATA_MIN = RAWMIN_BD12;
        CRITERIA_RAWDATA_MAX = RAWMAX_BD12;
        CRITERIA_LPWUG_RAWDATA_MAX = LPWUG_RAWDATA_MAX_BD12;
        CRITERIA_LPWUG_IDLE_RAWDATA_MAX = LPWUG_IDLE_RAWDATA_MAX_BD12;
    }

    if (hx83112a_nf_check_mode(checktype)) {
        TPD_INFO("Need Change Mode, target=%s", hx83112a_nf_inspection_mode[checktype]);

        hx83112a_nf_sense_off_mptest();

#ifndef HX_ZERO_FLASH
        hx83112a_nf_reload_disable(1);
#endif

        hx83112a_nf_switch_mode(checktype);

        if (checktype == HIMAX_INSPECTION_NOISE) {
            hx83112a_nf_set_N_frame(NOISEFRAME, checktype);
            /*hx83112a_nf_get_noise_base();*/
        } else if(checktype >= HIMAX_INSPECTION_LPWUG_RAWDATA) {
            TPD_INFO("N frame = %d\n", 1);
            hx83112a_nf_set_N_frame(1, checktype);
        } else {
            hx83112a_nf_set_N_frame(2, checktype);
        }


        hx83112a_nf_sense_on(1);

        ret = hx83112a_nf_wait_sorting_mode(checktype);
        if (ret) {
            TPD_INFO("%s: hx83112a_nf_wait_sorting_mode FAIL\n", __func__);
            return ret;
        }
    }

    hx83112a_nf_switch_data_type(checktype);

    ret = hx83112a_nf_get_rawdata(chip_info, RAW, datalen);
    if (ret) {
        TPD_INFO("%s: hx83112a_nf_get_rawdata FAIL\n", __func__);

        tmp_addr[3] = 0x90;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0xA8;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 900000A8: data[0]=%0x02X, data[1]=%0x02X, data[2]=%0x02X, data[3]=%0x02X, \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x40;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 10007F40: data[0]=%0x02X, data[1]=%0x02X, data[2]=%0x02X, data[3]=%0x02X, \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 10000000: data[0]=%0x02X, data[1]=%0x02X, data[2]=%0x02X, data[3]=%0x02X, \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x04;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 10007F04: data[0]=%0x02X, data[1]=%0x02X, data[2]=%0x02X, data[3]=%0x02X, \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x02;
        tmp_addr[1] = 0x04;
        tmp_addr[0] = 0xB4;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: 800204B4: data[0]=%0x02X, data[1]=%0x02X, data[2]=%0x02X, data[3]=%0x02X, \n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

        //900000A8,10007F40,10000000,10007F04,800204B4
        return ret;
    }

    /* back to normal */
    hx83112a_nf_switch_data_type(HIMAX_INSPECTION_BACK_NORMAL);

    rslt_log = kzalloc(256 * sizeof(char), GFP_KERNEL);
    if (!rslt_log) {
        TPD_INFO("%s:%d rslt_log kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }
    start_log = kzalloc(256 * sizeof(char), GFP_KERNEL);
    if (!start_log) {
        TPD_INFO("%s:%d  start_log kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }

    snprintf(start_log, 256 * sizeof(char), "\n%s%s\n",
             hx83112a_nf_inspection_mode[checktype], ": data as follow!\n");
    //Check Data
    switch (checktype) {
    case HIMAX_INSPECTION_OPEN:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > OPENMAX || RAW[i] < OPENMIN) {
                    TPD_INFO("%s: open test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_OPENMAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_OPENMIN][i]) {
                    TPD_INFO("%s: open test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            }
        }
        TPD_INFO("%s: open test PASS\n", __func__);
        break;

    case HIMAX_INSPECTION_MICRO_OPEN:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > M_OPENMAX || RAW[i] < M_OPENMIN) {
                    TPD_INFO("%s: micro open test FAIL\n", __func__);
                    ret =  RESULT_ERR;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_M_OPENMAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_M_OPENMIN][i]) {
                    TPD_INFO("%s: micro open test FAIL\n", __func__);
                    ret =  RESULT_ERR;
                }
            }
        }
        TPD_INFO("M_OPENMAX = %d, M_OPENMIN = %d\n",
                 hx83112a_nf_inspection_criteria[IDX_M_OPENMAX][1],
                 hx83112a_nf_inspection_criteria[IDX_M_OPENMIN][1]);
        TPD_INFO("%s: micro open test PASS\n", __func__);
        break;

    case HIMAX_INSPECTION_SHORT:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > SHORTMAX || RAW[i] < SHORTMIN) {
                    TPD_INFO("%s: short test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_SHORTMAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_SHORTMIN][i]) {
                    TPD_INFO("%s: short test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            }
        }
        TPD_INFO("%s: short test PASS\n", __func__);
        break;

    case HIMAX_INSPECTION_RAWDATA:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > CRITERIA_RAWDATA_MAX || RAW[i] < CRITERIA_RAWDATA_MIN) {
                    TPD_INFO("%s: rawdata test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_RAWDATA_MAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_RAWDATA_MIN][i]) {
                    TPD_INFO("%s: rawdata test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            }
        }
        TPD_INFO("%s: rawdata test PASS\n", __func__);
        break;

    case HIMAX_INSPECTION_NOISE:
        /*for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if (RAW[i] > NOISEMAX) {
                TPD_INFO("%s: noise test FAIL\n", __func__);
                return RESULT_ERR;
            }
        }*/
        hx83112a_nf_get_noise_base();
        if(hx83112a_nf_isBD12proj || hx83112a_nf_isEA006proj) {
            for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
                if (hx83112a_nf_check_notch(i)) {
                    continue;
                }
                if (RAW[i] > NOISEMAX) {
                    TPD_INFO("%s: noise test FAIL\n", __func__);
                    ret = RESULT_ERR;
                }
            }
        } else {
            snprintf(start_log, 256 * sizeof(char), "\n Threshold = %d\n", NOISEMAX);
            weight = hx83112a_nf_get_noise_weight();
            if (weight > NOISEMAX) {
                TPD_INFO("%s: noise test FAIL\n", __func__);
                ret = RESULT_ERR;
            }
        }
        TPD_INFO("%s: noise test PASS\n", __func__);

#ifdef RAWDATA_NOISE
        TPD_INFO("[MP_RAW_TEST_RAW]\n");

        hx83112a_nf_switch_data_type(HIMAX_INSPECTION_RAWDATA);
        ret = hx83112a_nf_get_rawdata(chip_info, RAW, datalen);
        if (ret == RESULT_ERR) {
            TPD_INFO("%s: hx83112a_nf_get_rawdata FAIL\n", __func__);
            ret = RESULT_ERR;
        }
#endif
        break;

    case HIMAX_INSPECTION_LPWUG_RAWDATA:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > CRITERIA_LPWUG_RAWDATA_MAX || RAW[i] < LPWUG_RAWDATA_MIN) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_RAWDATA FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ERAW;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_LPWUG_RAWDATA_MAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_LPWUG_RAWDATA_MIN][i]) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_RAWDATA FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ERAW;
                }
            }
        }
        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_RAWDATA PASS\n", __func__);
        break;
    case HIMAX_INSPECTION_LPWUG_NOISE:
        if(hx83112a_nf_isBD12proj || hx83112a_nf_isEA006proj) {
            for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
                if (hx83112a_nf_check_notch(i)) {
                    continue;
                }
                if(hx83112a_nf_isRead_csv == false) {
                    if (RAW[i] > LPWUG_NOISE_MAX || RAW[i] < LPWUG_NOISE_MIN) {
                        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_NOISE FAIL\n", __func__);
                        ret = THP_AFE_INSPECT_ENOISE;
                    }
                } else {
                    if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_LPWUG_NOISE_MAX][i] ||
                        RAW[i] < hx83112a_nf_inspection_criteria[IDX_LPWUG_NOISE_MIN][i]) {
                        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_NOISE FAIL\n", __func__);
                        ret = THP_AFE_INSPECT_ENOISE;
                    }
                }
            }
        } else {
            hx83112a_nf_get_noise_base();
            weight = hx83112a_nf_get_noise_weight();
            if(hx83112a_nf_isRead_csv == false) {
                if (weight > LPWUG_NOISEMAX || weight < LPWUG_NOISE_MIN) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_NOISE FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ENOISE;
                }
            } else {
                if (weight > LPWUG_NOISEMAX || weight < hx83112a_nf_inspection_criteria[IDX_LPWUG_NOISE_MIN][i]) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_NOISE FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ENOISE;
                }
            }
        }
        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_NOISE PASS\n", __func__);
        break;

    case HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > CRITERIA_LPWUG_IDLE_RAWDATA_MAX || RAW[i] < LPWUG_IDLE_RAWDATA_MIN) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ERAW;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_LPWUG_IDLE_RAWDATA_MAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_LPWUG_IDLE_RAWDATA_MIN][i]) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ERAW;
                }
            }
        }
        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA PASS\n", __func__);
        break;
    case HIMAX_INSPECTION_LPWUG_IDLE_NOISE:
        for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
            if (hx83112a_nf_check_notch(i)) {
                continue;
            }
            if(hx83112a_nf_isRead_csv == false) {
                if (RAW[i] > LPWUG_IDLE_NOISE_MAX || RAW[i] < LPWUG_IDLE_NOISE_MIN) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_NOISE FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ENOISE;
                }
            } else {
                if (RAW[i] > hx83112a_nf_inspection_criteria[IDX_LPWUG_IDLE_NOISE_MAX][i] ||
                    RAW[i] < hx83112a_nf_inspection_criteria[IDX_LPWUG_IDLE_NOISE_MIN][i]) {
                    TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_NOISE FAIL\n", __func__);
                    ret = THP_AFE_INSPECT_ENOISE;
                }
            }
        }
        TPD_INFO("%s: HIMAX_INSPECTION_LPWUG_IDLE_NOISE PASS\n", __func__);
        break;

    default:
        TPD_INFO("Wrong type=%d\n", checktype);
        break;
    }

    snprintf(rslt_log, 256 * sizeof(char), "\n%s%s\n",
             hx83112a_nf_inspection_mode[checktype], " Test Pass!\n");

    hx83112a_nf_test_data_get(chip_info, RAW, start_log, rslt_log, checktype);

    if (rslt_log) {
        kfree(rslt_log);
    }
    if (start_log) {
        kfree(start_log);
    }
    if (ret) {
        return ret;
    } else {
        return RESULT_OK;
    }

RET_OUT:
    if (rslt_log) {
        kfree(rslt_log);
    }
    if (start_log) {
        kfree(start_log);
    }
    return RESULT_ERR;
}

static int hx83112a_nf_saperate_comma(const struct firmware *file_entry,
                                char **result, int str_size)
{
    int count = 0;
    int str_count = 0; /* now string*/
    int char_count = 0; /* now char count in string*/

    do {
        switch (file_entry->data[count]) {
        case ASCII_COMMA:
        case ACSII_SPACE:
        case ASCII_CR:
        case ASCII_LF:
            count++;
            /* If end of line as above condifiton,
            * differencing the count of char.
            * If char_count != 0
            * it's meaning this string is parsing over .
            * The Next char is belong to next string
            */
            if (char_count != 0) {
                char_count = 0;
                str_count++;
            }
            break;
        default:
            result[str_count][char_count++] =
                file_entry->data[count];
            count++;
            break;
        }
    } while (count < file_entry->size && str_count < str_size);

    return str_count;
}

static int hx83112a_nf_diff_str(char *str1, char *str2)
{
    int i = 0;
    int result = 0; /* zero is all same, non-zero is not same index*/
    int str1_len = strlen(str1);
    int str2_len = strlen(str2);

    if (str1_len != str2_len) {
        TPD_DEBUG("%s:Size different!\n", __func__);
        return LENGTH_FAIL;
    }

    for (i = 0; i < str1_len; i++) {
        if (str1[i] != str2[i]) {
            result = i + 1;
            TPD_INFO("%s: different in %d!\n", __func__, result);
            return result;
        }
    }

    return result;
}

/* get idx of criteria whe parsing file */
int hx83112a_nf_find_crtra_id(char *input)
{
    int i = 0;
    int result = 0;

    for (i = 0 ; i < HX83112A_NF_CRITERIA_SIZE ; i++) {
        if (hx83112a_nf_diff_str(hx83112a_nf_inspt_crtra_name[i], input) == 0) {
            result = i;
            TPD_INFO("find the str=%s, idx=%d\n",
                     hx83112a_nf_inspt_crtra_name[i], i);
            break;
        }
    }
    if (i > (HX83112A_NF_CRITERIA_SIZE - 1)) {
        TPD_INFO("%s: find Fail!\n", __func__);
        return LENGTH_FAIL;
    }

    return result;
}

/* claculate 10's power function */
static int hx83112a_nf_power_cal(int pow, int number)
{
    int i = 0;
    int result = 1;

    for (i = 0; i < pow; i++)
        result *= 10;
    result = result * number;

    return result;

}

/* String to int */
static int hiamx_parse_str2int(char *str)
{
    int i = 0;
    int temp_cal = 0;
    int result = 0;
    int str_len = strlen(str);
    int negtive_flag = 0;

    for (i = 0; i < strlen(str); i++) {
        if (str[i] != '-' && str[i] > '9' && str[i] < '0') {
            TPD_INFO("%s: Parsing fail!\n", __func__);
            result = -9487;
            negtive_flag = 0;
            break;
        }
        if (str[i] == '-') {
            negtive_flag = 1;
            continue;
        }
        temp_cal = str[i] - '0';
        result += hx83112a_nf_power_cal(str_len - i - 1, temp_cal);
        /* str's the lowest char is the number's the highest number
         * So we should reverse this number before using the power
         * function
         * -1: starting number is from 0 ex:10^0 = 1, 10^1 = 10
         */
    }

    if (negtive_flag == 1)
        result = 0 - result;

    return result;
}

int hx83112a_nf_print_crtra_after_parsing(struct chip_data_hx83112a_nf *chip_info)
{
    int i = 0, j = 0;
    int all_mut_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;

    for (i = 0; i < HX83112A_NF_CRITERIA_SIZE; i++) {
        TPD_DETAIL("Now is %s\n", hx83112a_nf_inspt_crtra_name[i]);
        if (hx83112a_nf_inspt_crtra_flag[i] == 1) {
            for (j = 0; j < all_mut_len; j++) {
                TPD_DEBUG("%d, ", hx83112a_nf_inspection_criteria[i][j]);
                if (j % 16 == 15)
                    TPD_DEBUG("\n");
            }
        } else {
            TPD_DEBUG("No this Item in this criteria file!\n");
        }
        TPD_DEBUG("\n");
    }

    return 0;
}

static int hx_get_crtra_by_name(struct chip_data_hx83112a_nf *chip_info, char **result, int size_of_result_str)
{
    int i = 0;
    /* count of criteria type */
    int count_type = 0;
    /* count of criteria data */
    int count_data = 0;
    int err = THP_AFE_INSPECT_OK;
    int all_mut_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
    int temp = 0;

    /* get criteria and assign to a global array(2-Dimensional/int) */
    /* basiclly the size of criteria will be
     * (crtra_count * (all_mut_len) + crtra_count)
     * but we use file size to be the end of counter
     */
    for (i = 0; i < size_of_result_str && result[i] != NULL; i++) {
        /* It have get one page(all mutual) criteria data!
         * And we should skip the string of criteria name!
         */
        if (i == 0 || i == ((i / (all_mut_len)) + (i / (all_mut_len) * (all_mut_len)))) {
            count_data = 0;

            TPD_DEBUG("Now find str=%s, idx=%d\n", result[i], i);

            /* check the item of criteria is in criteria file
            * or not
            */
            count_type = hx83112a_nf_find_crtra_id(result[i]);
            if (count_type < 0) {
                TPD_INFO("1. %s:Name Not match!\n", __func__);
                /* E("can recognize[%d]=%s\n", count_type,
                 * hx83112a_nf_inspt_crtra_name[count_type]);
                 */
                TPD_INFO("get from file[%d]=%s\n", i, result[i]);
                TPD_INFO("Please check criteria file again!\n");
                err = THP_AFE_INSPECT_EFILE;
                goto END_FUNCTION;
            } else {
                TPD_INFO("Now str=%s, idx=%d\n",
                         hx83112a_nf_inspt_crtra_name[count_type], count_type);
                hx83112a_nf_inspt_crtra_flag[count_type] = 1;
            }
            continue;
        }
        /* change string to int*/
        temp = hiamx_parse_str2int(result[i]);
        if (temp != -9487) {
            hx83112a_nf_inspection_criteria[count_type][count_data] = temp;
        } else {
            TPD_INFO("%s: Parsing Fail in %d\n", __func__, i);
            TPD_INFO("in range:[%d]=%s\n", count_type,
                     hx83112a_nf_inspt_crtra_name[count_type]);
            TPD_INFO("btw, get from file[%d]=%s\n", i, result[i]);
            break;
        }
        /* dbg
         * I("[%d]hx83112a_nf_inspection_criteria[%d][%d]=%d\n",
         * i, count_type, count_data,
         * hx83112a_nf_inspection_criteria[count_type][count_data]);
         */
        count_data++;

    }

    /* dbg:print all of criteria from parsing file */
    hx83112a_nf_print_crtra_after_parsing(chip_info);

    TPD_INFO("Total loop=%d\n", i);
END_FUNCTION:
    return err;
}

static int hx83112a_nf_parse_criteria_file(struct chip_data_hx83112a_nf *chip_info)
{
    int err = THP_AFE_INSPECT_OK;
    const struct firmware *file_entry = NULL;
    //char *file_name = "hx_criteria.csv";
    static char **result;
    int i = 0;
    int j = 0;
    int crtra_count = HX83112A_NF_CRITERIA_SIZE;
    int data_size = 0; /* The maximum of number Data*/
    int all_mut_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
    int str_max_len = 0;
    int result_all_len = 0;
    int file_size = 0;
    int size_of_result_str = 0;

    TPD_INFO("%s, Entering\n", __func__);
    TPD_INFO("file name = %s\n", chip_info->test_limit_name);

    /* default path is /system/etc/firmware */
    err = request_firmware(&file_entry, chip_info->test_limit_name, hx83112a_nf_pri_ts->dev);
    if (err < 0) {
        TPD_INFO("%s, fail in line%d error code=%d\n", __func__, __LINE__, err);
        err = THP_AFE_INSPECT_EFILE;
        hx83112a_nf_isRead_csv = false;
        goto END_FUNC_REQ_FAIL;
    }

    /* size of criteria include name string */
    data_size = ((all_mut_len) * crtra_count) + crtra_count;

    /* init the array which store original criteria and include
     *  name string
     */
    while (hx83112a_nf_inspt_crtra_name[j] != NULL) {
        if (strlen(hx83112a_nf_inspt_crtra_name[j]) > str_max_len)
            str_max_len = strlen(hx83112a_nf_inspt_crtra_name[j]);
        j++;
    }

    if(result == NULL){
        TPD_INFO("%s: result is NULL, alloc memory.\n", __func__);
        result = kcalloc(data_size, sizeof(char *), GFP_KERNEL);
        if (result != NULL) {
            for (i = 0 ; i < data_size; i++) {
                result[i] = kcalloc(str_max_len, sizeof(char), GFP_KERNEL);
                if (result[i] == NULL) {
                    TPD_INFO("%s: rst_arr Memory allocation falied!\n", __func__);
                    goto rst_arr_mem_alloc_failed;
                }
            }
        } else {
            TPD_INFO("%s: Memory allocation falied!\n", __func__);
            goto rst_mem_alloc_failed;
        }
    } else {
        //memset(result, 0x00, data_size * sizeof(char));
        for (i = 0 ; i < data_size; i++)
            memset(result[i], 0x00, str_max_len * sizeof(char));
    }

    result_all_len = data_size;
    file_size = file_entry->size;
    TPD_INFO("Now result_all_len=%d\n", result_all_len);
    TPD_INFO("Now file_size=%d\n", file_size);

    /* dbg */
    TPD_DEBUG("first 4 bytes 0x%2X, 0x%2X, 0x%2X, 0x%2X !\n",
              file_entry->data[0], file_entry->data[1],
              file_entry->data[2], file_entry->data[3]);

    /* parse value in to result array(1-Dimensional/String) */
    size_of_result_str =
        hx83112a_nf_saperate_comma(file_entry, result, data_size);

    TPD_INFO("%s: now size_of_result_str=%d\n", __func__, size_of_result_str);

    err = hx_get_crtra_by_name(chip_info, result, size_of_result_str);
    if (err != THP_AFE_INSPECT_OK) {
        TPD_INFO("%s:Load criteria from file fail, go end!\n", __func__);
    }

    goto END_FUNC;

rst_arr_mem_alloc_failed:
    for (i = 0 ; i < data_size; i++) {
        if (result[i] != NULL) {
            kfree(result[i]);
        }
    }
    kfree(result);
rst_mem_alloc_failed:
END_FUNC:
    release_firmware(file_entry);
END_FUNC_REQ_FAIL:
    TPD_INFO("%s, END\n", __func__);
    return err;
    /* parsing Criteria end */
}

/* Get sub-string from original string by using some charaters
 * return size of result
 */

int hx83112a_nf_get_size_str_arr(char **input)
{
    int i = 0;
    int result = 0;

    while (input[i] != NULL)
        i++;

    result = i;
    TPD_DEBUG("There is %d in [0]=%s\n", result, input[0]);

    return result;
}

static int hx83112a_nf_self_test_data_init(struct chip_data_hx83112a_nf *chip_info)
{
    int ret = THP_AFE_INSPECT_OK;
    int i = 0;

    /* get test item and its items of criteria*/
    HX83112A_NF_CRITERIA_ITEM = hx83112a_nf_get_size_str_arr(hx83112a_nf_inspection_mode);
    HX83112A_NF_CRITERIA_SIZE = hx83112a_nf_get_size_str_arr(hx83112a_nf_inspt_crtra_name);
    TPD_INFO("There is %d HX83112A_NF_CRITERIA_ITEM and %d HX83112A_NF_CRITERIA_SIZE\n",
             HX83112A_NF_CRITERIA_ITEM, HX83112A_NF_CRITERIA_SIZE);

    /* init criteria data*/
    hx83112a_nf_inspt_crtra_flag = kzalloc(HX83112A_NF_CRITERIA_SIZE * sizeof(int), GFP_KERNEL);
    hx83112a_nf_inspection_criteria = kzalloc(sizeof(int *)*HX83112A_NF_CRITERIA_SIZE, GFP_KERNEL);
    if (hx83112a_nf_inspt_crtra_flag == NULL || hx83112a_nf_inspection_criteria == NULL) {
        TPD_INFO("%s: %d, Memory allocation falied!\n", __func__, __LINE__);
        return MEM_ALLOC_FAIL;
    }

    for (i = 0; i < HX83112A_NF_CRITERIA_SIZE; i++) {
        hx83112a_nf_inspection_criteria[i] = kcalloc(
                (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM), sizeof(int), GFP_KERNEL);
        if (hx83112a_nf_inspection_criteria[i] == NULL) {
            TPD_INFO("%s: %d, Memory allocation falied!\n", __func__, __LINE__);
            return MEM_ALLOC_FAIL;
        }
    }

    /* parsing criteria from file*/
    ret = hx83112a_nf_parse_criteria_file(chip_info);

    /* print get criteria string */
    for (i = 0 ; i < HX83112A_NF_CRITERIA_SIZE ; i++) {
        if (hx83112a_nf_inspt_crtra_flag[i] != 0)
            TPD_DEBUG("%s: [%d]There is String=%s\n", __func__, i, hx83112a_nf_inspt_crtra_name[i]);
    }

    return ret;
}

static void hx83112a_nf_black_screen_test(void *chip_data, char *message)
{
    int error = 0;
    int error_num = 0;
    int retry_cnt = 3;
    char buf[128] = {0};
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    char g_file_name_OK[64];
    char g_file_name_NG[64];
    char *g_Test_list_log;
    char *g_project_test_info_log;
    char *g_Company_info_log;
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    int i = 0;

    TPD_INFO("%s\n", __func__);

    hx83112a_nf_rslt_data_len = 0;

    /*init criteria data*/
    error = hx83112a_nf_self_test_data_init(chip_info);
    /*init criteria data*/

    /*Init Log Data */
    hx83112a_nf_1kind_raw_size = 5 * chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2;
    g_Company_info_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    g_Test_list_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    g_project_test_info_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    hx83112a_nf_fail_write_count = 0;
    hx83112a_nf_file_path_OK = kcalloc(256, sizeof(char), GFP_KERNEL);
    if (!hx83112a_nf_file_path_OK) {
        TPD_INFO("%s:%d hx83112a_nf_file_path_OK kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }
    hx83112a_nf_file_path_NG = kcalloc(256, sizeof(char), GFP_KERNEL);
    if (!hx83112a_nf_file_path_NG) {
        TPD_INFO("%s:%d hx83112a_nf_file_path_NG kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }

    if (hx83112a_nf_rslt_data == NULL) {
        TPD_INFO("hx83112a_nf_rslt_data is NULL");
        hx83112a_nf_rslt_data = kcalloc(hx83112a_nf_1kind_raw_size * HX83112A_NF_CRITERIA_ITEM,
                              sizeof(char), GFP_KERNEL);
        if (!hx83112a_nf_rslt_data) {
            TPD_INFO("%s:%d hx83112a_nf_rslt_data kzalloc buf error\n", __func__, __LINE__);
            goto RET_OUT;
        }
    } else {
        memset(hx83112a_nf_rslt_data, 0x00, hx83112a_nf_1kind_raw_size * HX83112A_NF_CRITERIA_ITEM *
               sizeof(char));
    }
    /*Init Log Data */

    //6. LPWUG RAWDATA
    TPD_INFO("[MP_LPWUG_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_LPWUG_RAWDATA, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "6. MP_LPWUG_TEST_RAW: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 15, "test Item:\n");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "6. MP_LPWUG_TEST_RAW: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    sprintf(message, "%s\n", buf);
    if (error != 0)
        error_num++;

    //7. LPWUG NOISE
    retry_cnt = 3;
    TPD_INFO("[MP_LPWUG_TEST_NOISE]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_LPWUG_NOISE, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "7. MP_LPWUG_TEST_NOISE: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "7. MP_LPWUG_TEST_NOISE: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    sprintf(message, "%s\n", buf);
    if (error != 0)
        error_num++;

    //8. LPWUG IDLE RAWDATA
    retry_cnt = 3;
    TPD_INFO("[MP_LPWUG_IDLE_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_LPWUG_IDLE_RAWDATA, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "8. MP_LPWUG_IDLE_TEST_RAW: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "8. MP_LPWUG_IDLE_TEST_RAW: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    sprintf(message, "%s\n", buf);
    if (error != 0)
        error_num++;

    //9. LPWUG IDLE RAWDATA
    retry_cnt = 3;
    TPD_INFO("[MP_LPWUG_IDLE_TEST_NOISE]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_LPWUG_IDLE_NOISE, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "9. MP_LPWUG_IDLE_TEST_NOISE: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "9. MP_LPWUG_IDLE_TEST_NOISE: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    sprintf(message, "%s\n", buf);
    if (error != 0)
        error_num++;

    /*Save Log Data */
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    sprintf(g_file_name_OK, "tp_testlimit_gesture_OK_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    sprintf(g_file_name_NG, "tp_testlimit_gesture_NG_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 22, "Final_result: %s\n", error_num ? "Fail" : "Pass");
    if (error) {
        snprintf(hx83112a_nf_file_path_NG,
                 (int)(strlen(HX_GES_RSLT_OUT_PATH_NG) + strlen(g_file_name_NG) + 1),
                 "%s%s", HX_GES_RSLT_OUT_PATH_NG, g_file_name_NG);
        hx83112a_nf_test_data_pop_out(chip_info, g_Test_list_log, g_Company_info_log, g_project_test_info_log, hx83112a_nf_rslt_data, hx83112a_nf_file_path_NG);
    } else {
        snprintf(hx83112a_nf_file_path_OK,
                 (int)(strlen(HX_GES_RSLT_OUT_PATH_OK) + strlen(g_file_name_OK) + 1),
                 "%s%s", HX_GES_RSLT_OUT_PATH_OK, g_file_name_OK);
        hx83112a_nf_test_data_pop_out(chip_info, g_Test_list_log, g_Company_info_log, g_project_test_info_log, hx83112a_nf_rslt_data, hx83112a_nf_file_path_OK);
    }
    /*Save Log Data */


    sprintf(message, "%d errors. %s", error_num, error_num ? "" : "All test passed.");
    TPD_INFO("%d errors. %s\n", error_num, error_num ? "" : "All test passed.");

#ifndef HX_ZERO_FLASH
    hx83112a_nf_sense_off();
    //hx83112a_nf_set_N_frame(1, HIMAX_INSPECTION_NOISE);

    hx83112a_nf_reload_disable(0);

    hx83112a_nf_sense_on(0);
#endif
    /*hx83112a_nf_mcu_0f_operation_dirly();
    msleep(5);
    g_hx83112a_nf_core_fp.fp_reload_disable(0);
    msleep(5);
    hx83112a_nf_sense_on(0x00);
    hx83112a_nf_read_OPLUS_FW_ver();*/
RET_OUT:
    if (hx83112a_nf_inspection_criteria != NULL) {
        for (i = 0; i < HX83112A_NF_CRITERIA_SIZE; i++) {
            if (hx83112a_nf_inspection_criteria[i] != NULL) {
                kfree(hx83112a_nf_inspection_criteria[i]);
                hx83112a_nf_inspection_criteria[i] = NULL;
            }
        }
        kfree(hx83112a_nf_inspection_criteria);
        hx83112a_nf_inspection_criteria = NULL;
        TPD_INFO("Now it have free the hx83112a_nf_inspection_criteria!\n");
    } else {
        TPD_INFO("No Need to free hx83112a_nf_inspection_criteria!\n");
    }

    if (hx83112a_nf_inspt_crtra_flag) {
        kfree(hx83112a_nf_inspt_crtra_flag);
        hx83112a_nf_inspt_crtra_flag = NULL;
    }
    /*
    if (hx83112a_nf_rslt_data) {
        kfree(hx83112a_nf_rslt_data);
        hx83112a_nf_rslt_data = NULL;
    }
    */
    if (hx83112a_nf_file_path_OK) {
        kfree(hx83112a_nf_file_path_OK);
        hx83112a_nf_file_path_OK = NULL;
    }
    if (hx83112a_nf_file_path_NG) {
        kfree(hx83112a_nf_file_path_NG);
        hx83112a_nf_file_path_NG = NULL;
    }
    if (g_Test_list_log) {
        kfree(g_Test_list_log);
        g_Test_list_log = NULL;
    }
    if (g_project_test_info_log) {
        kfree(g_project_test_info_log);
        g_project_test_info_log = NULL;
    }
    if (g_Company_info_log) {
        kfree(g_Company_info_log);
        g_Company_info_log = NULL;
    }
}

int hx83112a_nf_chip_self_test(struct seq_file *s, struct chip_data_hx83112a_nf *chip_info, char *g_Test_list_log)
{
    int error = 0;
    int error_num = 0;
    char buf[128] = {0};
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t back_data[4];
    uint8_t retry_cnt = 3;

    TPD_INFO("%s:Entering\n", __func__);

    //1. Open Test
    TPD_INFO("[MP_OPEN_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_OPEN, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 15, "test Item:\n");
    snprintf(buf, 128, "1. Open Test: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "1. Open Test: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    seq_printf(s, buf);
    if (error != 0)
        error_num++;

    //2. Micro-Open Test
    retry_cnt = 3;
    TPD_INFO("[MP_MICRO_OPEN_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_MICRO_OPEN, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "2. Micro Open Test: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "2. Micro Open Test: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    seq_printf(s, buf);
    if (error != 0)
        error_num++;

    //3. Short Test
    retry_cnt = 3;
    TPD_INFO("[MP_SHORT_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_SHORT, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "3. Short Test: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "3. Short Test: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    seq_printf(s, buf);
    if (error != 0)
        error_num++;

#ifndef RAWDATA_NOISE
    //4. RawData Test
    retry_cnt = 3;
    TPD_INFO("[MP_RAW_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_RAWDATA, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "4. Raw data Test: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "4. Raw data Test: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    seq_printf(s, buf);
    if (error != 0)
        error_num++;
#endif

    //5. Noise Test
    retry_cnt = 3;
    TPD_INFO("[MP_NOISE_TEST_RAW]\n");
    do {
        error = hx83112a_nf_mpTestFunc(chip_info, HIMAX_INSPECTION_NOISE, (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) + chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM);
        retry_cnt--;
    } while ((error == RESULT_RETRY) && (retry_cnt > 0));
    snprintf(buf, 128, "5. Noise Test: %s\n", error ? "Error" : "Ok");
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 32, "5. Noise Test: %s\n", error ? "NG" : "Ok");
    TPD_INFO("%s", buf);
    seq_printf(s, buf);
    if (error != 0)
        error_num++;

    //hx83112a_nf_set_SMWP_enable(ts->SMWP_enable,suspended);
    //Enable:0x10007F10 = 0xA55AA55A
    retry_cnt = 0;
    do {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x10;
        tmp_data[3] = 0xA5;
        tmp_data[2] = 0x5A;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
        back_data[3] = 0XA5;
        back_data[2] = 0X5A;
        back_data[1] = 0XA5;
        back_data[0] = 0X5A;
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: tmp_data[0] = 0x%22X, retry_cnt=%d \n", __func__, tmp_data[0], retry_cnt);
        retry_cnt++;
    } while ((tmp_data[3] != back_data[3] || tmp_data[2] != back_data[2] || tmp_data[1] != back_data[1] || tmp_data[0] != back_data[0]) && retry_cnt < HIMAX_REG_RETRY_TIMES);

    TPD_INFO("%s:End", __func__);
    hx83112a_nf_sense_off_mptest();
    hx83112a_nf_switch_mode(HIMAX_INSPECTION_LPWUG_RAWDATA);
    hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 22, "Final_result: %s\n", error_num ? "Fail" : "Pass");
    return error_num;
}

void hx83112a_nf_init_psl(void) //power saving level
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    //==============================================================
    // SCU_Power_State_PW : 0x9000_00A0 ==> 0x0000_0000 (Reset PSL)
    //==============================================================
    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0xA0;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

    TPD_INFO("%s: power saving level reset OK!\n", __func__);
}


void hx83112a_nf_chip_erase(void)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    hx83112a_nf_interface_on();

    /* init psl */
    hx83112a_nf_init_psl();

    //=====================================
    // SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
    //=====================================
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x10;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x02;
    tmp_data[1] = 0x07;
    tmp_data[0] = 0x80;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    //=====================================
    // Chip Erase
    // Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
    //                2. 0x8000_0024 ==> 0x0000_0006
    //=====================================
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x20;
    tmp_data[3] = 0x47;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x24;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x06;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    //=====================================
    // Chip Erase
    // Erase Command : 0x8000_0024 ==> 0x0000_00C7
    //=====================================
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x24;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0xC7;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    msleep(2000);

    if (!hx83112a_nf_wait_wip(100)) {
        TPD_INFO("%s:83112_Chip_Erase Fail\n", __func__);
    }

}

void hx83112a_nf_flash_programming(uint8_t *FW_content, int FW_Size)
{
    int page_prog_start = 0;
    int program_length = 48;
    int i = 0, j = 0, k = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t buring_data[256];    // Read for flash data, 128K
    // 4 bytes for 0x80002C padding

    hx83112a_nf_interface_on();

    //=====================================
    // SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
    //=====================================
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x10;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x02;
    tmp_data[1] = 0x07;
    tmp_data[0] = 0x80;
    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

    for (page_prog_start = 0; page_prog_start < FW_Size; page_prog_start = page_prog_start + 256) {
        //msleep(5);
        //=====================================
        // Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
        //                2. 0x8000_0024 ==> 0x0000_0006
        //=====================================
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x20;
        tmp_data[3] = 0x47;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x24;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x06;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        //=================================
        // SPI Transfer Control
        // Set 256 bytes page write : 0x8000_0020 ==> 0x610F_F000
        // Set read start address   : 0x8000_0028 ==> 0x0000_0000
        //=================================
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x20;
        tmp_data[3] = 0x61;
        tmp_data[2] = 0x0F;
        tmp_data[1] = 0xF0;
        tmp_data[0] = 0x00;
        // data bytes should be 0x6100_0000 + ((word_number)*4-1)*4096 = 0x6100_0000 + 0xFF000 = 0x610F_F000
        // Programmable size = 1 page = 256 bytes, word_number = 256 byte / 4 = 64
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x28;
        //tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00; // Flash start address 1st : 0x0000_0000

        if (page_prog_start < 0x100) {
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = (uint8_t)page_prog_start;
        } else if (page_prog_start >= 0x100 && page_prog_start < 0x10000) {
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = (uint8_t)(page_prog_start >> 8);
            tmp_data[0] = (uint8_t)page_prog_start;
        } else if (page_prog_start >= 0x10000 && page_prog_start < 0x1000000) {
            tmp_data[3] = 0x00;
            tmp_data[2] = (uint8_t)(page_prog_start >> 16);
            tmp_data[1] = (uint8_t)(page_prog_start >> 8);
            tmp_data[0] = (uint8_t)page_prog_start;
        }

        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);


        //=================================
        // Send 16 bytes data : 0x8000_002C ==> 16 bytes data
        //=================================
        buring_data[0] = 0x2C;
        buring_data[1] = 0x00;
        buring_data[2] = 0x00;
        buring_data[3] = 0x80;

        for (i = /*0*/page_prog_start, j = 0; i < 16 + page_prog_start/**/; i++, j++) {
            buring_data[j + 4] = FW_content[i];
        }


        if (hx83112a_nf_bus_write(0x00, 20, buring_data) < 0) {
            TPD_INFO("%s: i2c access fail!\n", __func__);
            return;
        }
        //=================================
        // Write command : 0x8000_0024 ==> 0x0000_0002
        //=================================
        tmp_addr[3] = 0x80;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x24;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x02;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        //=================================
        // Send 240 bytes data : 0x8000_002C ==> 240 bytes data
        //=================================

        for (j = 0; j < 5; j++) {
            for (i = (page_prog_start + 16 + (j * 48)), k = 0; i < (page_prog_start + 16 + (j * 48)) + program_length; i++, k++) {
                buring_data[k + 4] = FW_content[i]; //(byte)i;
            }

            if (hx83112a_nf_bus_write(0x00, program_length + 4, buring_data) < 0) {
                TPD_INFO("%s: i2c access fail!\n", __func__);
                return;
            }
        }

        if (!hx83112a_nf_wait_wip(1)) {
            TPD_INFO("%s:83112_Flash_Programming Fail\n", __func__);
        }
    }
}



int hx83112a_nf_fts_ctpm_fw_upgrade_with_sys_fs_64k(unsigned char *fw, int len, bool change_iref) /* Alice - Un */
{

    //int CRC_from_FW = 0;
    int burnFW_success = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    if (len != FW_SIZE_64k) {
        TPD_INFO("%s: The file size is not 64K bytes\n", __func__);
        return false;
    }

    /*#ifdef HX_RST_PIN_FUNC
        hx83112a_nf_ic_reset(false,false);
    #else*/
    //===AHBI2C_SystemReset==========
    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x18;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x55;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);
    //#endif

    hx83112a_nf_enter_safe_mode();
    hx83112a_nf_chip_erase();
    hx83112a_nf_flash_programming(fw, FW_SIZE_64k);

    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;

    if (hx83112a_nf_hw_check_CRC(tmp_data, FW_SIZE_64k) == 0) {
        burnFW_success = 1;
    } else {
        burnFW_success = 0;
    }
    /*RawOut select initial*/
    tmp_addr[3] = 0x80;
    tmp_addr[2] = 0x02;
    tmp_addr[1] = 0x04;
    tmp_addr[0] = 0xB4;

    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

    /*DSRAM func initial*/
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x07;
    tmp_addr[0] = 0xFC;

    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x00;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);

#ifdef HX_RST_PIN_FUNC
    hx83112a_nf_ic_reset(false, false);
#else
    //===AHBI2C_SystemReset==========
    tmp_addr[3] = 0x90;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x18;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x00;
    tmp_data[0] = 0x55;
    hx83112a_nf_register_write(tmp_addr, 4, tmp_data, false);
#endif
    return burnFW_success;

}


static size_t hx83112a_nf_proc_register_read(struct file *file, char *buf, size_t len, loff_t *pos)
{
    size_t ret = 0;
    uint16_t loop_i;
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    int max_bus_size = MAX_RECVS_SZ;
    uint8_t data[MAX_RECVS_SZ];
#else
    int max_bus_size = 128;
    uint8_t data[128];
#endif
    char *temp_buf;
    //struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    memset(data, 0x00, sizeof(data));

    if (!HX83112A_NF_HX_PROC_SEND_FLAG) {
        temp_buf = kzalloc(len, GFP_KERNEL);

        TPD_INFO("hx83112a_nf_register_show: %02X, %02X, %02X, %02X\n", hx83112a_nf_register_command[3], hx83112a_nf_register_command[2], hx83112a_nf_register_command[1], hx83112a_nf_register_command[0]);

        hx83112a_nf_register_read(hx83112a_nf_register_command, max_bus_size, data, hx83112a_nf_cfg_flag);

        ret += snprintf(temp_buf + ret, len - ret, "command:  %02X, %02X, %02X, %02X\n", hx83112a_nf_register_command[3], hx83112a_nf_register_command[2], hx83112a_nf_register_command[1], hx83112a_nf_register_command[0]);

        for (loop_i = 0; loop_i < max_bus_size; loop_i++) {
            ret += snprintf(temp_buf + ret, len - ret, "0x%2.2X ", data[loop_i]);
            if ((loop_i % 16) == 15) {
                ret += snprintf(temp_buf + ret, len - ret, "\n");
            }
        }
        ret += snprintf(temp_buf + ret, len - ret, "\n");
        if (copy_to_user(buf, temp_buf, len)) {
            TPD_INFO("%s, here:%d\n", __func__, __LINE__);
        }
        kfree(temp_buf);
        HX83112A_NF_HX_PROC_SEND_FLAG = 1;
    } else {
        HX83112A_NF_HX_PROC_SEND_FLAG = 0;
    }
    return ret;
}

static size_t hx83112a_nf_proc_register_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    char buf[81] = {0};
    char buf_tmp[16];
    uint8_t length = 0;
    unsigned long result = 0;
    uint8_t loop_i = 0;
    uint16_t base = 2;
    char *data_str = NULL;
    uint8_t w_data[20];
    uint8_t x_pos[20];
    uint8_t count = 0;
    //struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (len >= 80) {
        TPD_INFO("%s: no command exceeds 80 chars.\n", __func__);
        return -EFAULT;
    }

    if (copy_from_user(buf, buff, len)) {
        return -EFAULT;
    }
    buf[len] = '\0';

    memset(buf_tmp, 0x0, sizeof(buf_tmp));
    memset(w_data, 0x0, sizeof(w_data));
    memset(x_pos, 0x0, sizeof(x_pos));

    TPD_INFO("himax %s \n", buf);

    if ((buf[0] == 'r' || buf[0] == 'w') && buf[1] == ':' && buf[2] == 'x') {
        length = strlen(buf);

        //TPD_INFO("%s: length = %d.\n", __func__,length);
        for (loop_i = 0; loop_i < length; loop_i++) {//find postion of 'x'
            if (buf[loop_i] == 'x') {
                x_pos[count] = loop_i;
                count++;
            }
        }

        data_str = strrchr(buf, 'x');
        TPD_INFO("%s: %s.\n", __func__, data_str);
        length = strlen(data_str + 1) - 1;

        if (buf[0] == 'r') {
            if (buf[3] == 'F' && buf[4] == 'E' && length == 4) {
                length = length - base;
                hx83112a_nf_cfg_flag = true;
                memcpy(buf_tmp, data_str + base + 1, length);
            } else {
                hx83112a_nf_cfg_flag = false;
                memcpy(buf_tmp, data_str + 1, length);
            }

            hx83112a_nf_byte_length = length / 2;
            if (!kstrtoul(buf_tmp, 16, &result)) {
                for (loop_i = 0 ; loop_i < hx83112a_nf_byte_length ; loop_i++) {
                    hx83112a_nf_register_command[loop_i] = (uint8_t)(result >> loop_i * 8);
                }
            }
        } else if (buf[0] == 'w') {
            if (buf[3] == 'F' && buf[4] == 'E') {
                hx83112a_nf_cfg_flag = true;
                memcpy(buf_tmp, buf + base + 3, length);
            } else {
                hx83112a_nf_cfg_flag = false;
                memcpy(buf_tmp, buf + 3, length);
            }
            if (count < 3) {
                hx83112a_nf_byte_length = length / 2;
                if (!kstrtoul(buf_tmp, 16, &result)) {//command
                    for (loop_i = 0 ; loop_i < hx83112a_nf_byte_length ; loop_i++) {
                        hx83112a_nf_register_command[loop_i] = (uint8_t)(result >> loop_i * 8);
                    }
                }
                if (!kstrtoul(data_str + 1, 16, &result)) { //data
                    for (loop_i = 0 ; loop_i < hx83112a_nf_byte_length ; loop_i++) {
                        w_data[loop_i] = (uint8_t)(result >> loop_i * 8);
                    }
                }
                hx83112a_nf_register_write(hx83112a_nf_register_command, hx83112a_nf_byte_length, w_data, hx83112a_nf_cfg_flag);
            } else {
                hx83112a_nf_byte_length = x_pos[1] - x_pos[0] - 2;
                for (loop_i = 0; loop_i < count; loop_i++) {//parsing addr after 'x'
                    memcpy(buf_tmp, buf + x_pos[loop_i] + 1, hx83112a_nf_byte_length);
                    //TPD_INFO("%s: buf_tmp = %s\n", __func__,buf_tmp);
                    if (!kstrtoul(buf_tmp, 16, &result)) {
                        if (loop_i == 0) {
                            hx83112a_nf_register_command[loop_i] = (uint8_t)(result);
                            //TPD_INFO("%s: hx83112a_nf_register_command = %X\n", __func__,hx83112a_nf_register_command[0]);
                        } else {
                            w_data[loop_i - 1] = (uint8_t)(result);
                            //TPD_INFO("%s: w_data[%d] = %2X\n", __func__,loop_i - 1,w_data[loop_i - 1]);
                        }
                    }
                }

                hx83112a_nf_byte_length = count - 1;
                hx83112a_nf_register_write(hx83112a_nf_register_command, hx83112a_nf_byte_length, &w_data[0], hx83112a_nf_cfg_flag);
            }
        } else {
            return len;
        }

    }
    return len;
}

void hx83112a_nf_return_event_stack(void)
{
    int retry = 20;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];

    TPD_INFO("%s:entering\n", __func__);
    do {
        TPD_INFO("%s, now %d times\n!", __func__, retry);
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        retry--;
        //msleep(10);

    } while ((tmp_data[1] != 0x00 && tmp_data[0] != 0x00) && retry > 0);

    TPD_INFO("%s: End of setting!\n", __func__);

}
/*IC_BASED_END*/

int hx83112a_nf_write_read_reg(uint8_t *tmp_addr, uint8_t *tmp_data, uint8_t hb, uint8_t lb)
{
    int cnt = 0;

    do {
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

        msleep(20);
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s:Now tmp_data[0] = 0x%02X, [1] = 0x%02X, [2] = 0x%02X, [3] = 0x%02X\n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
    } while ((tmp_data[1] != hb && tmp_data[0] != lb) && cnt++ < 100);

    if (cnt >= 99) {
        TPD_INFO("hx83112a_nf_write_read_reg ERR Now register 0x%08X : high byte = 0x%02X, low byte = 0x%02X\n", tmp_addr[3], tmp_data[1], tmp_data[0]);
        return -1;
    }

    TPD_INFO("Now register 0x%08X : high byte = 0x%02X, low byte = 0x%02X\n", tmp_addr[3], tmp_data[1], tmp_data[0]);
    return NO_ERR;
}

void hx83112a_nf_get_DSRAM_data(uint8_t *info_data, uint8_t x_num, uint8_t y_num)
{
    int i = 0;
    //int cnt = 0;
    unsigned char tmp_addr[4];
    unsigned char tmp_data[4];
    uint8_t max_i2c_size = MAX_RECVS_SZ;
    int m_key_num = 0;
    int total_size = (x_num * y_num + x_num + y_num) * 2 + 4;
    int total_size_temp;
    int mutual_data_size = x_num * y_num * 2;
    int total_read_times = 0;
    int address = 0;
    uint8_t *temp_info_data; //max mkey size = 8
    uint32_t check_sum_cal = 0;
    int fw_run_flag = -1;
    //uint16_t temp_check_sum_cal = 0;

    temp_info_data = kzalloc(sizeof(uint8_t) * (total_size + 8), GFP_KERNEL);

    /*1. Read number of MKey R100070E8H to determin data size*/
    m_key_num = 0;
    //TPD_INFO("%s,m_key_num=%d\n",__func__,m_key_num);
    total_size += m_key_num * 2;

    /* 2. Start DSRAM Rawdata and Wait Data Ready */
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x00;
    tmp_data[3] = 0x00;
    tmp_data[2] = 0x00;
    tmp_data[1] = 0x5A;
    tmp_data[0] = 0xA5;
    fw_run_flag = hx83112a_nf_write_read_reg(tmp_addr, tmp_data, 0xA5, 0x5A);
    if (fw_run_flag < 0) {
        TPD_INFO("%s Data NOT ready => bypass \n", __func__);
        kfree(temp_info_data);
        return;
    }

    /* 3. Read RawData */
    total_size_temp = total_size;
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x00;
    tmp_addr[0] = 0x00;

    if (total_size % max_i2c_size == 0) {
        total_read_times = total_size / max_i2c_size;
    } else {
        total_read_times = total_size / max_i2c_size + 1;
    }

    for (i = 0; i < (total_read_times); i++) {
        if (total_size_temp >= max_i2c_size) {
            hx83112a_nf_register_read(tmp_addr, max_i2c_size, &temp_info_data[i * max_i2c_size], false);
            total_size_temp = total_size_temp - max_i2c_size;
        } else {
            //TPD_INFO("last total_size_temp=%d\n",total_size_temp);
            hx83112a_nf_register_read(tmp_addr, total_size_temp % max_i2c_size,
                                &temp_info_data[i * max_i2c_size], false);
        }

        address = ((i + 1) * max_i2c_size);
        tmp_addr[1] = (uint8_t)((address >> 8) & 0x00FF);
        tmp_addr[0] = (uint8_t)((address) & 0x00FF);
    }

    /* 4. FW stop outputing */
    //TPD_INFO("HX83112A_NF_DSRAM_Flag=%d\n",HX83112A_NF_DSRAM_Flag);
    if (HX83112A_NF_DSRAM_Flag == false) {
        //TPD_INFO("Return to Event Stack!\n");
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
    } else {
        //TPD_INFO("Continue to SRAM!\n");
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x00;
        tmp_addr[0] = 0x00;
        tmp_data[3] = 0x11;
        tmp_data[2] = 0x22;
        tmp_data[1] = 0x33;
        tmp_data[0] = 0x44;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
    }

    /* 5. Data Checksum Check */
    for (i = 2; i < total_size; i = i + 2) { /* 2:PASSWORD NOT included */
        check_sum_cal += (temp_info_data[i + 1] * 256 + temp_info_data[i]);
        printk("0x%2x:0x%4x ", temp_info_data[i], check_sum_cal);
        if (i % 32 == 0)
            printk("\n");
    }

    if (check_sum_cal % 0x10000 != 0) {
        memcpy(info_data, &temp_info_data[4], mutual_data_size * sizeof(uint8_t));
        TPD_INFO("%s check_sum_cal fail=%2x \n", __func__, check_sum_cal);
        kfree(temp_info_data);
        return;
    } else {
        memcpy(info_data, &temp_info_data[4], mutual_data_size * sizeof(uint8_t));
        TPD_INFO("%s checksum PASS \n", __func__);
    }
    kfree(temp_info_data);
}

void hx83112a_nf_ts_diag_func(struct chip_data_hx83112a_nf *chip_info, int32_t *mutual_data)
{
    int i = 0;
    int j = 0;
    unsigned int index = 0;
    int total_size = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2;
    uint8_t info_data[total_size];

    int32_t new_data;
    /* 1:common dsram,2:100 frame Max,3:N-(N-1)frame */
    int dsram_type = 0;
    char write_buf[total_size * 3];

    memset(write_buf, '\0', sizeof(write_buf));

    dsram_type = hx83112a_nf_diag_command / 10;

    TPD_INFO("%s:Entering hx83112a_nf_diag_command=%d\n!", __func__, hx83112a_nf_diag_command);

    if (dsram_type == 8) {
        dsram_type = 1;
        TPD_INFO("%s Sorting Mode run sram type1 ! \n", __func__);
    }

    hx83112a_nf_burst_enable(1);
    hx83112a_nf_get_DSRAM_data(info_data, chip_info->hw_res->RX_NUM, chip_info->hw_res->TX_NUM);

    index = 0;
    for (i = 0; i < chip_info->hw_res->TX_NUM; i++) {
        for (j = 0; j < chip_info->hw_res->RX_NUM; j++) {
            new_data = ((int8_t)info_data[index + 1] << 8 | info_data[index]);
            mutual_data[i * chip_info->hw_res->RX_NUM + j] = new_data;
            index += 2;
        }
    }
}

void hx83112a_nf_diag_parse_raw_data(struct hx83112a_nf_report_data *hx83112a_nf_touch_data, int mul_num, int self_num, uint8_t diag_cmd, int32_t *mutual_data, int32_t *self_data)
{
    int RawDataLen_word;
    int index = 0;
    int temp1, temp2, i;

    if (hx83112a_nf_touch_data->hx_rawdata_buf[0] == 0x3A
        && hx83112a_nf_touch_data->hx_rawdata_buf[1] == 0xA3
        && hx83112a_nf_touch_data->hx_rawdata_buf[2] > 0
        && hx83112a_nf_touch_data->hx_rawdata_buf[3] == diag_cmd) {
        RawDataLen_word = hx83112a_nf_touch_data->rawdata_size / 2;
        index = (hx83112a_nf_touch_data->hx_rawdata_buf[2] - 1) * RawDataLen_word;
        //TPD_INFO("Header[%d]: %x, %x, %x, %x, mutual: %d, self: %d\n", index, buf[56], buf[57], buf[58], buf[59], mul_num, self_num);
        //TPD_INFO("RawDataLen=%d , RawDataLen_word=%d , hx_touch_info_size=%d\n", RawDataLen, RawDataLen_word, hx_touch_info_size);
        for (i = 0; i < RawDataLen_word; i++) {
            temp1 = index + i;

            if (temp1 < mul_num) {
                //mutual
                mutual_data[index + i] = ((int8_t)hx83112a_nf_touch_data->hx_rawdata_buf[i * 2 + 4 + 1]) * 256 + hx83112a_nf_touch_data->hx_rawdata_buf[i * 2 + 4]; /* 4: RawData Header, 1:HSB  */
            } else {
                //self
                temp1 = i + index;
                temp2 = self_num + mul_num;

                if (temp1 >= temp2) {
                    break;
                }
                self_data[i + index - mul_num] = (((int8_t)hx83112a_nf_touch_data->hx_rawdata_buf[i * 2 + 4 + 1]) << 8) | hx83112a_nf_touch_data->hx_rawdata_buf[i * 2 + 4]; /* 4: RawData Header */
                //self_data[i+index-mul_num+1] = hx83112a_nf_touch_data->hx_rawdata_buf[i*2 + 4 + 1];
            }
        }
    }

}

bool hx83112a_nf_diag_check_sum(struct hx83112a_nf_report_data *hx83112a_nf_touch_data) /*return checksum value  */
{
    uint16_t check_sum_cal = 0;
    int i;

    //Check 128th byte CRC
    for (i = 0, check_sum_cal = 0; i < (hx83112a_nf_touch_data->touch_all_size - hx83112a_nf_touch_data->touch_info_size); i = i + 2) {
        check_sum_cal += (hx83112a_nf_touch_data->hx_rawdata_buf[i + 1] * 256 + hx83112a_nf_touch_data->hx_rawdata_buf[i]);
    }
    if (check_sum_cal % 0x10000 != 0) {
        TPD_INFO("%s fail=%2X \n", __func__, check_sum_cal);
        return 0;
        //goto bypass_checksum_failed_packet;
    }

    return 1;
}

static size_t hx83112a_nf_proc_diag_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    char messages[80] = {0};
    uint8_t command[2] = {0x00, 0x00};
    uint8_t receive[1];

    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)ts->chip_data;

    /* 0: common, other: dsram*/
    int storage_type = 0;
    /* 1:IIR, 2:DC, 3:Bank, 4:IIR2, 5:IIR2_N, 6:FIR2, 7:Baseline, 8:dump coord */
    int rawdata_type = 0;

    memset(receive, 0x00, sizeof(receive));

    if (len >= 80) {
        TPD_INFO("%s: no command exceeds 80 chars.\n", __func__);
        return -EFAULT;
    }
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    if (messages[1] == 0x0A) {
        hx83112a_nf_diag_command = messages[0] - '0';
    } else {
        hx83112a_nf_diag_command = (messages[0] - '0') * 10 + (messages[1] - '0');
    }

    storage_type = hx83112a_nf_diag_command / 10;
    rawdata_type = hx83112a_nf_diag_command % 10;

    TPD_INFO(" messages       = %s\n"
             " hx83112a_nf_diag_command = 0x%x\n"
             " storage_type   = 0x%x\n"
             " rawdata_type   = 0x%x\n",
             messages, hx83112a_nf_diag_command, storage_type, rawdata_type);

    if (hx83112a_nf_diag_command > 0 && rawdata_type == 0) {
        TPD_INFO("[Himax]hx83112a_nf_diag_command = 0x%x, storage_type=%d, rawdata_type=%d! Maybe no support!\n", hx83112a_nf_diag_command, storage_type, rawdata_type);
        hx83112a_nf_diag_command = 0x00;
    } else {
        TPD_INFO("[Himax]hx83112a_nf_diag_command = 0x%x, storage_type=%d, rawdata_type=%d\n", hx83112a_nf_diag_command, storage_type, rawdata_type);
    }

    if (storage_type == 0 && rawdata_type > 0 && rawdata_type < 8) {
        TPD_INFO("%s, common\n", __func__);
        if (HX83112A_NF_DSRAM_Flag) {
            //(1) Clear DSRAM flag
            HX83112A_NF_DSRAM_Flag = false;
            //(2) Enable ISR
            enable_irq(chip_info->hx_irq);
            //(3) FW leave sram and return to event stack
            hx83112a_nf_return_event_stack();
        }

        command[0] = hx83112a_nf_diag_command;
        hx83112a_nf_diag_register_set(command[0]);
    } else if (storage_type > 0 && storage_type < 8 && rawdata_type > 0 && rawdata_type < 8) {
        TPD_INFO("%s, dsram\n", __func__);

        //0. set diag flag
        if (HX83112A_NF_DSRAM_Flag) {
            //(1) Clear DSRAM flag
            HX83112A_NF_DSRAM_Flag = false;
            //(2) Enable ISR
            enable_irq(chip_info->hx_irq);
            //(3) FW leave sram and return to event stack
            hx83112a_nf_return_event_stack();
        }

        switch(rawdata_type) {
        case 1:
            command[0] = 0x09; //IIR
            break;

        case 2:
            command[0] = 0x0A;//RAWDATA
            break;

        case 3:
            command[0] = 0x08;//Baseline
            break;

        default:
            command[0] = 0x00;
            TPD_INFO("%s: Sram no support this type !\n", __func__);
            break;
        }
        hx83112a_nf_diag_register_set(command[0]);
        TPD_INFO("%s: Start get raw data in DSRAM\n", __func__);
        //1. Disable ISR
        disable_irq(chip_info->hx_irq);

        //2. Set DSRAM flag
        HX83112A_NF_DSRAM_Flag = true;
    } else {
        //set diag flag
        if (HX83112A_NF_DSRAM_Flag) {
            TPD_INFO("return and cancel sram thread!\n");
            //(1) Clear DSRAM flag
            HX83112A_NF_DSRAM_Flag = false;
            hx83112a_nf_return_event_stack();
        }
        command[0] = 0x00;
        hx83112a_nf_diag_command = 0x00;
        hx83112a_nf_diag_register_set(command[0]);
        TPD_INFO("return to normal hx83112a_nf_diag_command = 0x%x\n", hx83112a_nf_diag_command);
    }
    return len;
}

static size_t hx83112a_nf_proc_diag_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    size_t ret = 0;
    char *temp_buf;
    uint16_t mutual_num;
    uint16_t self_num;
    uint16_t width;
    int dsram_type = 0;
    int data_type = 0;
    int i = 0;
    int j = 0;
    int k = 0;

    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)ts->chip_data;

    if (!HX83112A_NF_HX_PROC_SEND_FLAG) {
        temp_buf = kzalloc(len, GFP_KERNEL);
        if (!temp_buf) {
            goto RET_OUT;
        }

        dsram_type = hx83112a_nf_diag_command / 10;
        data_type = hx83112a_nf_diag_command % 10;

        mutual_num = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
        self_num = chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM; //don't add KEY_COUNT
        width = chip_info->hw_res->RX_NUM;
        ret += snprintf(temp_buf + ret, len - ret, "ChannelStart (rx tx) : %4d, %4d\n\n", chip_info->hw_res->RX_NUM, chip_info->hw_res->TX_NUM);

        // start to show out the raw data in adb shell
        if ((data_type >= 1 && data_type <= 7)) {
            if (dsram_type > 0)
                hx83112a_nf_ts_diag_func(chip_info, hx83112a_nf_touch_data->diag_mutual);

            for (j = 0; j < chip_info->hw_res->RX_NUM ; j++) {
                for (i = 0; i < chip_info->hw_res->TX_NUM; i++) {
                    k = ((mutual_num - j) - chip_info->hw_res->RX_NUM * i) - 1;
                    ret += snprintf(temp_buf + ret, len - ret, "%6d", hx83112a_nf_touch_data->diag_mutual[k]);
                }
                ret += snprintf(temp_buf + ret, len - ret, " %6d\n", hx83112a_nf_diag_self[j]);
            }

            ret += snprintf(temp_buf + ret, len - ret, "\n");
            for (i = 0; i < chip_info->hw_res->TX_NUM; i++) {
                ret += snprintf(temp_buf + ret, len - ret, "%6d", hx83112a_nf_diag_self[i]);
            }
        }

        ret += snprintf(temp_buf + ret, len - ret, "\n");
        ret += snprintf(temp_buf + ret, len - ret, "ChannelEnd");
        ret += snprintf(temp_buf + ret, len - ret, "\n");

        //if ((hx83112a_nf_diag_command >= 1 && hx83112a_nf_diag_command <= 7) || dsram_type > 0)
        {
            /* print Mutual/Slef Maximum and Minimum */
            //hx83112a_nf_get_mutual_edge();
            for (i = 0; i < (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM); i++) {
                if (hx83112a_nf_touch_data->diag_mutual[i] > hx83112a_nf_max_mutual) {
                    hx83112a_nf_max_mutual = hx83112a_nf_touch_data->diag_mutual[i];
                }
                if (hx83112a_nf_touch_data->diag_mutual[i] < hx83112a_nf_min_mutual) {
                    hx83112a_nf_min_mutual = hx83112a_nf_touch_data->diag_mutual[i];
                }
            }

            //hx83112a_nf_get_self_edge();
            for (i = 0; i < (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM); i++) {
                if (hx83112a_nf_diag_self[i] > hx83112a_nf_max_self) {
                    hx83112a_nf_max_self = hx83112a_nf_diag_self[i];
                }
                if (hx83112a_nf_diag_self[i] < hx83112a_nf_min_self) {
                    hx83112a_nf_min_self = hx83112a_nf_diag_self[i];
                }
            }

            ret += snprintf(temp_buf + ret, len - ret, "Mutual Max:%3d, Min:%3d\n", hx83112a_nf_max_mutual, hx83112a_nf_min_mutual);
            ret += snprintf(temp_buf + ret, len - ret, "Self Max:%3d, Min:%3d\n", hx83112a_nf_max_self, hx83112a_nf_min_self);

            /* recovery status after print*/
            hx83112a_nf_max_mutual = 0;
            hx83112a_nf_min_mutual = 0xFFFF;
            hx83112a_nf_max_self = 0;
            hx83112a_nf_min_self = 0xFFFF;
        }
        if (copy_to_user(buff, temp_buf, len)) {
            TPD_INFO("%s, here:%d\n", __func__, __LINE__);
        }
        HX83112A_NF_HX_PROC_SEND_FLAG = 1;
RET_OUT:
        if(temp_buf) {
            kfree(temp_buf);
        }
    } else {
        HX83112A_NF_HX_PROC_SEND_FLAG = 0;
    }

    return ret;
}

uint8_t hx83112a_nf_read_DD_status(struct chip_data_hx83112a_nf *chip_info, uint8_t *hx83112a_nf_cmd_set, uint8_t *tmp_data)
{
    int cnt = 0;
    uint8_t req_size = hx83112a_nf_cmd_set[0];
    uint8_t cmd_addr[4] = {0xFC, 0x00, 0x00, 0x90}; //0x900000FC -> cmd and hand shaking
    uint8_t tmp_addr[4] = {0x80, 0x7F, 0x00, 0x10}; //0x10007F80 -> data space

    hx83112a_nf_cmd_set[3] = 0xAA;
    hx83112a_nf_register_write(cmd_addr, 4, hx83112a_nf_cmd_set, 0);

    TPD_INFO("%s: hx83112a_nf_cmd_set[0] = 0x%02X, hx83112a_nf_cmd_set[1] = 0x%02X, hx83112a_nf_cmd_set[2] = 0x%02X, hx83112a_nf_cmd_set[3] = 0x%02X\n", __func__, hx83112a_nf_cmd_set[0], hx83112a_nf_cmd_set[1], hx83112a_nf_cmd_set[2], hx83112a_nf_cmd_set[3]);

    for (cnt = 0; cnt < 100; cnt++) {
        hx83112a_nf_register_read(cmd_addr, 4, tmp_data, false);
        TPD_INFO("%s: tmp_data[0] = 0x%02X, tmp_data[1] = 0x%02X, tmp_data[2] = 0x%02X, tmp_data[3] = 0x%02X, cnt=%d\n", __func__, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3], cnt);
        msleep(10);
        if (tmp_data[3] == 0xBB) {
            TPD_INFO("%s Data ready goto moving data\n", __func__);
            break;
        } else if (cnt >= 99) {
            TPD_INFO("%s Data not ready in FW \n", __func__);
            return FW_NOT_READY;
        }
    }
    hx83112a_nf_register_read(tmp_addr, req_size, tmp_data, false);
    return NO_ERR;
}

static size_t hx83112a_nf_proc_DD_debug_read(struct file *file, char *buf, size_t len, loff_t *pos)
{
    int ret = 0;
    uint8_t tmp_data[64];
    uint8_t loop_i = 0;
    char *temp_buf;

    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)ts->chip_data;

    if (!HX83112A_NF_HX_PROC_SEND_FLAG) {
        temp_buf = kzalloc(len, GFP_KERNEL);

        if (hx83112a_nf_mutual_set_flag == 1) {
            if (hx83112a_nf_read_DD_status(chip_info, hx83112a_nf_cmd_set, tmp_data) == NO_ERR) {
                for (loop_i = 0; loop_i < hx83112a_nf_cmd_set[0]; loop_i++) {
                    if ((loop_i % 8) == 0) {
                        ret += snprintf(temp_buf + ret, len - ret, "0x%02X : ", loop_i);
                    }
                    ret += snprintf(temp_buf + ret, len - ret, "0x%02X ", tmp_data[loop_i]);
                    if ((loop_i % 8) == 7)
                        ret += snprintf(temp_buf + ret, len - ret, "\n");
                }
            }
        }
        //else
        ret += snprintf(temp_buf + ret, len - ret, "\n");
        if (copy_to_user(buf, temp_buf, len))
            TPD_INFO("%s, here:%d\n", __func__, __LINE__);
        kfree(temp_buf);
        HX83112A_NF_HX_PROC_SEND_FLAG = 1;
    } else
        HX83112A_NF_HX_PROC_SEND_FLAG = 0;
    return ret;
}

static size_t hx83112a_nf_proc_DD_debug_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    uint8_t i = 0;
    uint8_t cnt = 2;
    unsigned long result = 0;
    char buf_tmp[20];
    char buf_tmp2[4];

    if (len >= 20) {
        TPD_INFO("%s: no command exceeds 20 chars.\n", __func__);
        return -EFAULT;
    }
    if (copy_from_user(buf_tmp, buff, len)) {
        return -EFAULT;
    }
    memset(buf_tmp2, 0x0, sizeof(buf_tmp2));

    if (buf_tmp[2] == 'x' && buf_tmp[6] == 'x' && buf_tmp[10] == 'x') {
        hx83112a_nf_mutual_set_flag = 1;
        for (i = 3; i < 12; i = i + 4) {
            memcpy(buf_tmp2, buf_tmp + i, 2);
            if (!kstrtoul(buf_tmp2, 16, &result))
                hx83112a_nf_cmd_set[cnt] = (uint8_t)result;
            else
                TPD_INFO("String to oul is fail in cnt = %d, buf_tmp2 = %s", cnt, buf_tmp2);
            cnt--;
        }
        TPD_INFO("hx83112a_nf_cmd_set[2] = %02X, hx83112a_nf_cmd_set[1] = %02X, hx83112a_nf_cmd_set[0] = %02X\n", hx83112a_nf_cmd_set[2], hx83112a_nf_cmd_set[1], hx83112a_nf_cmd_set[0]);
    } else
        hx83112a_nf_mutual_set_flag = 0;

    return len;
}

int hx83112a_nf_read_FW_status(struct chip_data_hx83112a_nf *chip_info, uint8_t *state_addr, uint8_t *tmp_addr)
{
    uint8_t req_size = 0;
    uint8_t status_addr[4] = {0x44, 0x7F, 0x00, 0x10}; //0x10007F44
    uint8_t cmd_addr[4] = {0xF8, 0x00, 0x00, 0x90}; //0x900000F8

    if (state_addr[0] == 0x01) {
        state_addr[1] = 0x04;
        state_addr[2] = status_addr[0];
        state_addr[3] = status_addr[1];
        state_addr[4] = status_addr[2];
        state_addr[5] = status_addr[3];
        req_size = 0x04;
        hx83112a_nf_sense_off();
        hx83112a_nf_register_read(status_addr, req_size, tmp_addr, false);
        hx83112a_nf_sense_on(1);
    } else if (state_addr[0] == 0x02) {
        state_addr[1] = 0x30;
        state_addr[2] = cmd_addr[0];
        state_addr[3] = cmd_addr[1];
        state_addr[4] = cmd_addr[2];
        state_addr[5] = cmd_addr[3];
        req_size = 0x30;
        hx83112a_nf_register_read(cmd_addr, req_size, tmp_addr, false);
    }

    return NO_ERR;
}

static size_t hx83112a_nf_proc_FW_debug_read(struct file *file, char *buf,
        size_t len, loff_t *pos)
{
    int ret = 0;
    uint8_t loop_i = 0;
    uint8_t tmp_data[64];
    char *temp_buf;

    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)ts->chip_data;

    if (!HX83112A_NF_HX_PROC_SEND_FLAG) {
        temp_buf = kzalloc(len, GFP_KERNEL);

        hx83112a_nf_cmd_set[0] = 0x01;
        if (hx83112a_nf_read_FW_status(chip_info, hx83112a_nf_cmd_set, tmp_data) == NO_ERR) {
            ret += snprintf(temp_buf + ret, len - ret, "0x%02X%02X%02X%02X :\t", hx83112a_nf_cmd_set[5], hx83112a_nf_cmd_set[4], hx83112a_nf_cmd_set[3], hx83112a_nf_cmd_set[2]);
            for (loop_i = 0; loop_i < hx83112a_nf_cmd_set[1]; loop_i++) {
                ret += snprintf(temp_buf + ret, len - ret, "%5d\t", tmp_data[loop_i]);
            }
            ret += snprintf(temp_buf + ret, len - ret, "\n");
        }
        hx83112a_nf_cmd_set[0] = 0x02;
        if (hx83112a_nf_read_FW_status(chip_info, hx83112a_nf_cmd_set, tmp_data) == NO_ERR) {
            for (loop_i = 0; loop_i < hx83112a_nf_cmd_set[1]; loop_i = loop_i + 2) {
                if ((loop_i % 16) == 0)
                    ret += snprintf(temp_buf + ret, len - ret, "0x%02X%02X%02X%02X :\t",
                                    hx83112a_nf_cmd_set[5], hx83112a_nf_cmd_set[4], hx83112a_nf_cmd_set[3] + (((hx83112a_nf_cmd_set[2] + loop_i) >> 8) & 0xFF), (hx83112a_nf_cmd_set[2] + loop_i) & 0xFF);

                ret += snprintf(temp_buf + ret, len - ret, "%5d\t", tmp_data[loop_i] + (tmp_data[loop_i + 1] << 8));
                if ((loop_i % 16) == 14)
                    ret += snprintf(temp_buf + ret, len - ret, "\n");
            }

        }
        ret += snprintf(temp_buf + ret, len - ret, "\n");
        if (copy_to_user(buf, temp_buf, len))
            TPD_INFO("%s, here:%d\n", __func__, __LINE__);
        kfree(temp_buf);
        HX83112A_NF_HX_PROC_SEND_FLAG = 1;
    } else
        HX83112A_NF_HX_PROC_SEND_FLAG = 0;
    return ret;
}

static int hx83112a_nf_configuration_init(struct chip_data_hx83112a_nf *chip_info, bool config)
{
    int ret = 0;
    TPD_INFO("%s, configuration init = %d\n", __func__, config);
    return ret;
}

int hx83112a_nf_ic_reset(struct chip_data_hx83112a_nf *chip_info, uint8_t loadconfig, uint8_t int_off)
{
    int ret = 0;
    HX83112A_NF_HW_RESET_ACTIVATE = 1;

    TPD_INFO("%s, status: loadconfig=%d, int_off=%d\n", __func__, loadconfig, int_off);

    if (chip_info->hw_res->reset_gpio) {
        if (int_off) {

            ret = hx83112a_nf_enable_interrupt(chip_info, false);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf enable interrupt failed.\n", __func__);
                return ret;
            }
        }

        hx83112a_nf_resetgpio_set(chip_info->hw_res, false); // reset gpio

        hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio

        if (loadconfig) {
            ret = hx83112a_nf_configuration_init(chip_info, false);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf configuration init failed.\n", __func__);
                return ret;
            }
            ret = hx83112a_nf_configuration_init(chip_info, true);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf configuration init failed.\n", __func__);
                return ret;
            }
        }
        if (int_off) {
            ret = hx83112a_nf_enable_interrupt(chip_info, true);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf enable interrupt failed.\n", __func__);
                return ret;
            }
        }
    }
    return 0;
}

static size_t hx83112a_nf_proc_reset_write(struct file *file, const char *buff,
                                        size_t len, loff_t *pos)
{
    char buf_tmp[12];
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)ts->chip_data;

    if (len >= 12) {
        TPD_INFO("%s: no command exceeds 12 chars.\n", __func__);
        return -EFAULT;
    }
    if (copy_from_user(buf_tmp, buff, len)) {
        return -EFAULT;
    }
    if (buf_tmp[0] == '1')
        hx83112a_nf_ic_reset(chip_info, false, false);
    else if (buf_tmp[0] == '2')
        hx83112a_nf_ic_reset(chip_info, false, true);
    else if (buf_tmp[0] == '3')
        hx83112a_nf_ic_reset(chip_info, true, false);
    else if (buf_tmp[0] == '4')
        hx83112a_nf_ic_reset(chip_info, true, true);
    else if ((buf_tmp[0] == 'z') && (buf_tmp[1] == 'r')) {
        hx83112a_nf_mcu_0f_operation_check(0);
        hx83112a_nf_mcu_0f_operation_check(1);
    } else if ((buf_tmp[0] == 'z') && (buf_tmp[1] == 'w'))
        hx83112a_nf_mcu_0f_operation_dirly();
    return len;
}

static size_t hx83112a_nf_proc_sense_on_off_write(struct file *file, const char *buff,
        size_t len, loff_t *pos)
{
    char buf[80] = {0};
    //struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (len >= 80) {
        TPD_INFO("%s: no command exceeds 80 chars.\n", __func__);
        return -EFAULT;
    }
    if (copy_from_user(buf, buff, len)) {
        return -EFAULT;
    }

    if (buf[0] == '0') {
        hx83112a_nf_sense_off();
        TPD_INFO("Sense off \n");
    } else if(buf[0] == '1') {
        if (buf[1] == 's') {
            hx83112a_nf_sense_on(0x00);
            TPD_INFO("Sense on re-map on, run sram \n");
        } else {
            hx83112a_nf_sense_on(0x01);
            TPD_INFO("Sense on re-map off, run flash \n");
        }
    } else {
        TPD_INFO("Do nothing \n");
    }
    return len;
}
//add for himax end
#ifdef CONFIG_OPLUS_TP_APK
static void hx83112a_nf_gesture_debug_mode_set(bool on_off)
{
    uint8_t tmp_addr[4] = {0};
    uint8_t tmp_data[4] = {0};
    char buf[80] = {0};
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x7F;
    tmp_addr[0] = 0xF8;

    if (on_off) {
        hx83112a_nf_switch_algo = buf[0];
        hx83112a_nf_check_point_format = 1;
        tmp_data[3] = 0xA1;
        tmp_data[2] = 0x1A;
        tmp_data[1] = 0xA1;
        tmp_data[0] = 0x1A;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        TPD_INFO("%s: Report 40 trajectory coordinate points .\n", __func__);
    }  else {
        hx83112a_nf_switch_algo = 0;
        hx83112a_nf_check_point_format = 0;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;

        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        TPD_INFO("%s: close FW enter algorithm switch.\n", __func__);
    }
}


static void hx83112a_nf_debug_mode_set(bool on_off)
{
    uint8_t tmp_addr[4] = {0};
    uint8_t tmp_data[4] = {0};
    char buf[80] = {0};
    tmp_addr[3] = 0x10;
    tmp_addr[2] = 0x00;
    tmp_addr[1] = 0x7F;
    tmp_addr[0] = 0xF8;

    if (on_off) {
        hx83112a_nf_switch_algo = buf[0];
        hx83112a_nf_check_point_format = 0;
        tmp_data[3] = 0xA5;
        tmp_data[2] = 0x5A;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        TPD_INFO("%s: open FW enter algorithm switch.\n", __func__);
    }  else {
        hx83112a_nf_switch_algo = 0;
        hx83112a_nf_check_point_format = 0;
        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;

        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        TPD_INFO("%s: close FW enter algorithm switch.\n", __func__);
    }
}

static void hx83112a_nf_debug_sta_judge(struct chip_data_hx83112a_nf *chip_info)
{
    static struct hx83112a_nf_fw_debug_info last_sta;
    struct hx83112a_nf_fw_debug_info sta;

    memcpy(&sta, &hx83112a_nf_touch_data->hx_state_info[3], sizeof(sta));

    if (last_sta.recal0 != sta.recal0) {
        if (sta.recal0) {
            log_buf_write(hx83112a_nf_pri_ts, 1);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 2);
        }

    }

    if (last_sta.recal1 != sta.recal1) {
        if (sta.recal1) {
            log_buf_write(hx83112a_nf_pri_ts, 4);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 3);
        }

    }

    if (last_sta.paseline != sta.paseline) {
        if (sta.paseline) {
            log_buf_write(hx83112a_nf_pri_ts, 5);
        } else {
            //log_buf_write(hx83112a_nf_pri_ts, 4);
        }

    }

    if (last_sta.palm != sta.palm) {
        if (sta.palm) {
            log_buf_write(hx83112a_nf_pri_ts, 7);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 6);
        }

    }
    if (last_sta.idle != sta.idle) {
        if (sta.idle) {
            log_buf_write(hx83112a_nf_pri_ts, 9);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 8);
        }

    }

    if (last_sta.water != sta.water) {
        if (sta.water) {
            log_buf_write(hx83112a_nf_pri_ts, 11);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 10);
        }

    }

    if (last_sta.hopping != sta.hopping) {
        if (sta.hopping) {
            log_buf_write(hx83112a_nf_pri_ts, 13);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 12);
        }

    }

    if (last_sta.noise != sta.noise) {
        if (sta.noise) {
            log_buf_write(hx83112a_nf_pri_ts, 15);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 14);
        }

    }

    if (last_sta.glove != sta.glove) {
        if (sta.glove) {
            log_buf_write(hx83112a_nf_pri_ts, 17);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 16);
        }

    }

    if (last_sta.border != sta.border) {
        if (sta.border) {
            log_buf_write(hx83112a_nf_pri_ts, 19);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 18);
        }

    }

    if (last_sta.vr != sta.vr) {
        if (sta.vr) {
            log_buf_write(hx83112a_nf_pri_ts, 21);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 20);
        }

    }

    if (last_sta.big_small != sta.big_small) {
        if (sta.big_small) {
            log_buf_write(hx83112a_nf_pri_ts, 23);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 22);
        }

    }

    if (last_sta.one_block != sta.one_block) {
        if (sta.one_block) {
            log_buf_write(hx83112a_nf_pri_ts, 25);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 24);
        }

    }

    if (last_sta.blewing != sta.blewing) {
        if (sta.blewing) {
            log_buf_write(hx83112a_nf_pri_ts, 27);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 26);
        }

    }

    if (last_sta.thumb_flying != sta.thumb_flying) {
        if (sta.thumb_flying) {
            log_buf_write(hx83112a_nf_pri_ts, 29);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 28);
        }

    }

    if (last_sta.border_extend != sta.border_extend) {
        if (sta.border_extend) {
            log_buf_write(hx83112a_nf_pri_ts, 31);
        } else {
            log_buf_write(hx83112a_nf_pri_ts, 30);
        }

    }

    memcpy(&last_sta, &sta, sizeof(last_sta));

    if (tp_debug > 0) {
        TPD_INFO("The sta  is = 0x%02X,0x%02X\n",
                 hx83112a_nf_touch_data->hx_state_info[3],
                 hx83112a_nf_touch_data->hx_state_info[4]);
    }

    return;
}


#endif

static int hx83112a_nf_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    int i, x, y, z = 1, obj_attention = 0;

    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    char buf[128];
    uint16_t mutual_num;
    uint16_t self_num;
    int ret = 0;
    int check_sum_cal;
    int ts_status = HX_REPORT_COORD;
    int hx_point_num;
    uint8_t hx_state_info_pos;

    if (!hx83112a_nf_touch_data) {
        TPD_INFO("%s:%d hx83112a_nf_touch_data is NULL\n", __func__, __LINE__);
    }

    if (!hx83112a_nf_touch_data->hx_coord_buf) {
        TPD_INFO("%s:%d hx83112a_nf_touch_data->hx_coord_buf is NULL\n", __func__, __LINE__);
        return 0;
    }

    hx83112a_nf_burst_enable(0);
    if (hx83112a_nf_diag_command)
        ret = hx83112a_nf_read_event_stack(buf, 128);
    else
        ret = hx83112a_nf_read_event_stack(buf, hx83112a_nf_touch_data->touch_info_size);
    if (!ret) {
        TPD_INFO("%s: can't read data from chip in normal!\n", __func__);
        goto checksum_fail;
    }

    if (LEVEL_DEBUG == tp_debug) {
        hx83112a_nf_log_touch_data(buf, hx83112a_nf_touch_data);
    }

    check_sum_cal = hx83112a_nf_checksum_cal(chip_info, buf, ts_status);//????checksum
    if (check_sum_cal == CHECKSUM_FAIL) {
        goto checksum_fail;
    } else if (check_sum_cal == ERR_WORK_OUT) {
        goto err_workqueue_out;
    } else if (check_sum_cal == WORK_OUT) {
        goto workqueue_out;
    }

    //hx83112a_nf_assign_touch_data(buf,ts_status);//??buf??, ??hx_coord_buf

    hx_state_info_pos = hx83112a_nf_touch_data->touch_info_size - 6;
    if(ts_status == HX_REPORT_COORD) {
        memcpy(hx83112a_nf_touch_data->hx_coord_buf, &buf[0], hx83112a_nf_touch_data->touch_info_size);
        if(buf[hx_state_info_pos] != 0xFF && buf[hx_state_info_pos + 1] != 0xFF) {
            memcpy(hx83112a_nf_touch_data->hx_state_info, &buf[hx_state_info_pos], 5);
#ifdef CONFIG_OPLUS_TP_APK
            if (chip_info->debug_mode_sta) {
                hx83112a_nf_debug_sta_judge(chip_info);
            }
#endif
        } else {
            memset(hx83112a_nf_touch_data->hx_state_info, 0x00, sizeof(hx83112a_nf_touch_data->hx_state_info));
        }
    }
    if (hx83112a_nf_diag_command) {
        mutual_num = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
        self_num = chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM;
        TPD_INFO("hx83112a_nf_touch_data->touch_all_size= %d hx83112a_nf_touch_data->touch_info_size = %d, %d\n", \
                 hx83112a_nf_touch_data->touch_all_size, hx83112a_nf_touch_data->touch_info_size, hx83112a_nf_touch_data->touch_all_size - hx83112a_nf_touch_data->touch_info_size);
        memcpy(hx83112a_nf_touch_data->hx_rawdata_buf, &buf[hx83112a_nf_touch_data->touch_info_size], hx83112a_nf_touch_data->touch_all_size - hx83112a_nf_touch_data->touch_info_size);
        if (!hx83112a_nf_diag_check_sum(hx83112a_nf_touch_data)) {
            goto err_workqueue_out;
        }
        hx83112a_nf_diag_parse_raw_data(hx83112a_nf_touch_data, mutual_num, self_num, hx83112a_nf_diag_command, hx83112a_nf_touch_data->diag_mutual, hx83112a_nf_diag_self);
    }

    if (hx83112a_nf_touch_data->hx_coord_buf[HX83112A_NF_TOUCH_INFO_POINT_CNT] == 0xff)//HX83112A_NF_TOUCH_INFO_POINT_CNT buf???????
        hx_point_num = 0;
    else
        hx_point_num = hx83112a_nf_touch_data->hx_coord_buf[HX83112A_NF_TOUCH_INFO_POINT_CNT] & 0x0f;


    for (i = 0; i < 10; i++) {
        x = hx83112a_nf_touch_data->hx_coord_buf[i * 4] << 8 | hx83112a_nf_touch_data->hx_coord_buf[i * 4 + 1];
        y = (hx83112a_nf_touch_data->hx_coord_buf[i * 4 + 2] << 8 | hx83112a_nf_touch_data->hx_coord_buf[i * 4 + 3]);
        z = hx83112a_nf_touch_data->hx_coord_buf[i + 40];
        if(x >= 0 && x <= hx83112a_nf_pri_ts->resolution_info.max_x && y >= 0 && y <= hx83112a_nf_pri_ts->resolution_info.max_y) {
            points[i].x = x;
            points[i].y = y;
            points[i].width_major = z;
            points[i].touch_major = z;
            points[i].status = 1;
            obj_attention = obj_attention | (0x0001 << i);
        }
    }

    //TPD_DEBUG("%s:%d  obj_attention = 0x%x\n", __func__, __LINE__, obj_attention);

checksum_fail:
    return obj_attention;
err_workqueue_out:
workqueue_out:
    //hx83112a_nf_ic_reset(chip_info, false, true);
    return -EINVAL;

}

static int hx83112a_nf_ftm_process(void *chip_data)
{
    hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, false); // reset gpio
    return 0;
}

static int hx83112a_nf_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    chip_info->tp_type = panel_data->tp_type;
    chip_info->p_tp_fw = &panel_data->TP_FW;
    TPD_INFO("chip_info->tp_type = %d, panel_data->test_limit_name = %s, panel_data->fw_name = %s\n",
             chip_info->tp_type, panel_data->test_limit_name, panel_data->fw_name);
    return 0;
}


static int hx83112a_nf_get_chip_info(void *chip_data)
{
    return 1;
}

/**
 * hx83112a_nf_get_fw_id -   get device fw id.
 * @chip_info: struct include i2c resource.
 * Return fw version result.
 */
static uint32_t hx83112a_nf_get_fw_id(struct chip_data_hx83112a_nf *chip_info)
{
    uint32_t current_firmware = 0;
    uint8_t cmd[4];
    uint8_t data[64];

    cmd[3] = 0x10;  // oplus fw id bin address : 0xc014   , 49172    Tp ic address : 0x 10007014
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x14;
    hx83112a_nf_register_read(cmd, 4, data, false);

    TPD_DEBUG("%s : data[0] = 0x%2.2X, data[1] = 0x%2.2X, data[2] = 0x%2.2X, data[3] = 0x%2.2X\n", __func__, data[0], data[1], data[2], data[3]);

    current_firmware = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    TPD_INFO("CURRENT_FIRMWARE_ID = 0x%x\n", current_firmware);

    return current_firmware;

}

static void __init get_lcd_vendor(void)
{
    if (strstr(boot_command_line, "1080p_dsi_vdo-1-fps")) {
        hx83112a_nf_lcd_vendor = 1;
    } else if (strstr(boot_command_line, "1080p_dsi_vdo-2-fps")) {
        hx83112a_nf_lcd_vendor = 2;
    } else if (strstr(boot_command_line, "1080p_dsi_vdo-3-fps")) {
        hx83112a_nf_lcd_vendor = 3;
    } else if (strstr(boot_command_line, "1080p_dsi_vdo-7-fps")) {
        hx83112a_nf_lcd_vendor = 7;
    } else if (strstr(boot_command_line, "1080p_dsi_vdo-8-fps")) {
        hx83112a_nf_lcd_vendor = 8;
    } else if (strstr(boot_command_line, "1080p_dsi_vdo-9-fps")) {
        hx83112a_nf_lcd_vendor = 9;
    }
}

static fw_check_state hx83112a_nf_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    //fw check normal need update TP_FW  && device info
    panel_data->TP_FW = hx83112a_nf_get_fw_id(chip_info);
    if (panel_data->manufacture_info.version)
        sprintf(panel_data->manufacture_info.version, "0x%x-%d", panel_data->TP_FW, hx83112a_nf_lcd_vendor);

    return FW_NORMAL;
}

static u8 hx83112a_nf_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    if ((gesture_enable == 1) && is_suspended) {
        return IRQ_GESTURE;
    } else {
        return IRQ_TOUCH;
    }
}
/*
static int hx83112a_nf_reset_for_prepare(void *chip_data)
{
    int ret = -1;
    //int i2c_error_number = 0;
    //struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    TPD_INFO("%s.\n", __func__);
    //hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio

    return ret;
}
*/
/*
static void hx83112a_nf_resume_prepare(void *chip_data)
{
    //hx83112a_nf_reset_for_prepare(chip_data);
    #ifdef HX_ZERO_FLASH
    TPD_DETAIL("It will update fw,if there is power-off in suspend!\n");

    hx83112a_nf_zero_event_count = 0;

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);

    // trigger reset
    //hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, false); // reset gpio
    //hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, true); // reset gpio

    g_hx83112a_nf_core_fp.fp_0f_operation_dirly();
    g_hx83112a_nf_core_fp.fp_reload_disable(0);
    hx83112a_nf_sense_on(0x00);
    // need_modify
    // report all leave event
    //hx83112a_nf_report_all_leave_event(hx83112a_nf_pri_ts);

    hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, true);
    #endif
}
*/
static void hx83112a_nf_exit_esd_mode(void *chip_data)
{
    TPD_INFO("exit esd mode ok\n");
    return;
}

/*
 * return success: 0 ; fail : negative
 */
static int hx83112a_nf_reset(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    int ret = 0;
    int load_fw_times = 10;

    TPD_INFO("%s.\n", __func__);

    if (!chip_info->first_download_finished) {
        TPD_INFO("%s:First download has not finished, don't do reset.\n", __func__);
        return 0;
    }

    hx83112a_nf_zero_event_count = 0;

    clear_view_touchdown_flag(); //clear touch download flag
    //esd hw reset
    HX83112A_NF_ESD_RESET_ACTIVATE = 0;

    //hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, false);
    disable_irq_nosync(chip_info->hx_irq);


    do {
        load_fw_times--;
        g_hx83112a_nf_core_fp.fp_0f_operation_dirly();
        ret = g_hx83112a_nf_core_fp.fp_reload_disable(0);//success return 1, fail return 0
    } while (!ret && load_fw_times > 0);

    if (!load_fw_times) {
        TPD_INFO("%s: load_fw_times over 10 times\n", __func__);
    }
    hx83112a_nf_sense_on(0x00);


#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    enable_irq(chip_info->hx_irq);
#endif
    //hx83112a_nf_enable_interrupt(hx83112a_nf_chip_info, true);
    //esd hw reset
    return ret;
}

void hx83112a_nf_ultra_enter(void)
{
    uint8_t tmp_data[4];
    int rtimes = 0;

    TPD_INFO("%s:entering\n", __func__);

    /* 34 -> 11 */
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:1/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0x11;
        if (hx83112a_nf_bus_write(0x34, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x34, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x34, correct 0x11 = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0x11);

    /* 33 -> 33 */
    rtimes = 0;
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:2/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0x33;
        if (hx83112a_nf_bus_write(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x33, correct 0x33 = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0x33);

    /* 34 -> 22 */
    rtimes = 0;
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:3/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0x22;
        if (hx83112a_nf_bus_write(0x34, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x34, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x34, correct 0x22 = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0x22);

    /* 33 -> AA */
    rtimes = 0;
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:4/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0xAA;
        if (hx83112a_nf_bus_write(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x33, correct 0xAA = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0xAA);

    /* 33 -> 33 */
    rtimes = 0;
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:5/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0x33;
        if (hx83112a_nf_bus_write(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x33, correct 0x33 = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0x33);

    /* 33 -> AA */
    rtimes = 0;
    do {
        if (rtimes > 10) {
            TPD_INFO("%s:6/6 retry over 10 times!\n", __func__);
            return;
        }
        tmp_data[0] = 0xAA;
        if (hx83112a_nf_bus_write(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi write fail!\n", __func__);
            continue;
        }
        tmp_data[0] = 0x00;
        if (hx83112a_nf_bus_read(0x33, 1, tmp_data) < 0) {
            TPD_INFO("%s: spi read fail!\n", __func__);
            continue;
        }

        TPD_INFO("%s:retry times %d, addr = 0x33, correct 0xAA = current 0x%2.2X\n", __func__, rtimes, tmp_data[0]);
        rtimes++;
    } while (tmp_data[0] != 0xAA);

    TPD_INFO("%s:END\n", __func__);
}

static int hx83112a_nf_enable_black_gesture(struct chip_data_hx83112a_nf *chip_info, bool enable)
{
    int ret = 0;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->hx_spi);

    TPD_INFO("%s:enable=%d, ts->is_suspended=%d \n", __func__, enable, ts->is_suspended);

    if (ts->is_suspended) {
        if (enable) {
            hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio
            hx83112a_nf_resetgpio_set(chip_info->hw_res, false); // reset gpio
            hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio


#ifdef CONFIG_OPLUS_TP_APK
            if (chip_info->debug_gesture_sta) {
                hx83112a_nf_gesture_debug_mode_set(true);
            }
#endif // end of CONFIG_OPLUS_TP_APK
            //hx83112a_nf_sense_on(0);
            /*if (!HX83112A_NF_RESET_STATE) {
                ret =  hx83112a_nf_resetgpio_set(chip_info->hw_res, true); // reset gpio
                if (ret < 0) {
                    TPD_INFO("%s: hx83112a_nf reset gpio failed.\n", __func__);
                    return ret;
                }
            }*/
            /*ret = hx83112a_nf_enable_interrupt(chip_info, true);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf enable interrupt failed.\n", __func__);
                return ret;
            }*/
        } else {
            /*ret = hx83112a_nf_enable_interrupt(chip_info, false);
            if (ret < 0) {
                TPD_INFO("%s: hx83112a_nf enable interrupt failed.\n", __func__);
                return ret;
            }*/
            /*if (HX83112A_NF_RESET_STATE) {
                ret =  hx83112a_nf_resetgpio_set(chip_info->hw_res, false); // reset gpio
                if (ret < 0) {
                    TPD_INFO("%s: hx83112a_nf reset gpio failed.\n", __func__);
                    return ret;
                }
            }*/
            //hx83112a_nf_sense_off();
            hx83112a_nf_ultra_enter();
        }
    } else {
        hx83112a_nf_sense_on(0);
    }
    return ret;
}
/*
static int hx83112a_nf_enable_edge_limit(struct chip_data_hx83112a_nf *chip_info, bool enable)
{
    int ret = 0;
    return ret;
}
*/

static int hx83112a_nf_enable_charge_mode(struct chip_data_hx83112a_nf *chip_info, bool enable)
{
    int ret = 0;
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    TPD_INFO("%s, charge mode enable = %d\n", __func__, enable);

    /*Enable:0x10007F38 = 0xA55AA55A  */
    if (enable) {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x38;
        tmp_data[3] = 0xA5;
        tmp_data[2] = 0x5A;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
    } else {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0x38;
        tmp_data[3] = 0x77;
        tmp_data[2] = 0x88;
        tmp_data[1] = 0x77;
        tmp_data[0] = 0x88;
        hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
    }

    return ret;
}

/*on = 1:on   0:off */
static int hx83112a_nf_jitter_switch (struct chip_data_hx83112a_nf *chip_info, bool on)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int rtimes = 0;
    int ret = 0;

    TPD_INFO("%s:entering\n", __func__);

    if (!on) {//jitter off
        do {
            if (rtimes > 10) {
                TPD_INFO("%s:retry over 10, jitter off failed!\n", __func__);
                TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x5A, 0xA5, 0x5A, 0xA5\n", __func__);
                ret = -1;
                break;
            }

            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0xE0;
            tmp_data[3] = 0xA5;
            tmp_data[2] = 0x5A;
            tmp_data[1] = 0xA5;
            tmp_data[0] = 0x5A;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

            TPD_INFO("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                     rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
            rtimes++;
        } while (tmp_data[3] != 0xA5 || tmp_data[2] != 0x5A
                 || tmp_data[1] != 0xA5 || tmp_data[0] != 0x5A);
        TPD_INFO("%s:jitter off success!\n", __func__);
    } else { //jitter on
        do {
            if (rtimes > 10) {
                TPD_INFO("%s:retry over 10, jitter on failed!\n", __func__);
                TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x00, 0x00, 0x00, 0x00\n", __func__);
                ret = -1;
                break;
            }

            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0xE0;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

            TPD_INFO("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                     rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
            rtimes++;
        } while (tmp_data[3] == 0xA5 && tmp_data[2] == 0x5A
                 && tmp_data[1] == 0xA5 && tmp_data[0] == 0x5A);
        TPD_INFO("%s:jitter on success!\n", __func__);
    }
    TPD_INFO("%s:END\n", __func__);
    return ret;
}

static int hx83112a_nf_enable_headset_mode(struct chip_data_hx83112a_nf *chip_info, bool enable)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int rtimes = 0;
    int ret = 0;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->hx_spi);

    if (ts->headset_pump_support) {
        if (enable) {/* insert headset */
            do {
                if (rtimes > 10) {
                    TPD_INFO("%s:insert headset failed!\n", __func__);
                    TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x5A, 0xA5, 0x5A, 0xA5\n", __func__);
                    ret = -1;
                    break;
                }

                tmp_addr[3] = 0x10;
                tmp_addr[2] = 0x00;
                tmp_addr[1] = 0x7F;
                tmp_addr[0] = 0xE8;
                tmp_data[3] = 0xA5;
                tmp_data[2] = 0x5A;
                tmp_data[1] = 0xA5;
                tmp_data[0] = 0x5A;
                hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
                TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                           rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                rtimes++;
            } while (tmp_data[3] != 0xA5 || tmp_data[2] != 0x5A
                     || tmp_data[1] != 0xA5 || tmp_data[0] != 0x5A);

            TPD_INFO("%s:insert headset success!\n", __func__);
        } else {/* remove headset  */
            do {
                if (rtimes > 10) {
                    TPD_INFO("%s:remove headset failed!\n", __func__);
                    TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x5A, 0xA5, 0x5A, 0xA5\n", __func__);
                    ret = -1;
                    break;
                }

                tmp_addr[3] = 0x10;
                tmp_addr[2] = 0x00;
                tmp_addr[1] = 0x7F;
                tmp_addr[0] = 0xE8;
                tmp_data[3] = 0x00;
                tmp_data[2] = 0x00;
                tmp_data[1] = 0x00;
                tmp_data[0] = 0x00;
                hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
                TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                           rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                rtimes++;
            } while (tmp_data[3] != 0x00 || tmp_data[2] != 0x00
                     || tmp_data[1] != 0x00 || tmp_data[0] != 0x00);

            TPD_INFO("%s:remove headset success!\n", __func__);
        }
    }
    return ret;
}

//mode = 0:off   1:normal   2:turn right    3:turn left
static int hx83112a_nf_rotative_switch(struct chip_data_hx83112a_nf *chip_info, int mode)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int rtimes = 0;
    int ret = 0;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->hx_spi);

    TPD_DETAIL("%s:entering\n", __func__);

    if (ts->fw_edge_limit_support) {
        if (mode == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {/* vertical */
            do {
                if (rtimes > 10) {
                    TPD_INFO("%s:rotative normal failed!\n", __func__);
                    TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x5A, 0xA5, 0x5A, 0xA5\n", __func__);
                    ret = -1;
                    break;
                }

                tmp_addr[3] = 0x10;
                tmp_addr[2] = 0x00;
                tmp_addr[1] = 0x7F;
                tmp_addr[0] = 0x3C;
                tmp_data[3] = 0xA5;
                tmp_data[2] = 0x5A;
                tmp_data[1] = 0xA5;
                tmp_data[0] = 0x5A;
                hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
                TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                           rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                rtimes++;
            } while (tmp_data[3] != 0xA5 || tmp_data[2] != 0x5A
                     || tmp_data[1] != 0xA5 || tmp_data[0] != 0x5A);

            TPD_INFO("%s:rotative normal success!\n", __func__);

        } else {
            rtimes = 0;
            if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) { //turn right
                do {
                    if (rtimes > 10) {
                        TPD_INFO("%s:rotative right failed!\n", __func__);
                        TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x3A, 0xA3, 0x3A, 0xA3\n", __func__);
                        ret = -1;
                        break;
                    }

                    tmp_addr[3] = 0x10;
                    tmp_addr[2] = 0x00;
                    tmp_addr[1] = 0x7F;
                    tmp_addr[0] = 0x3C;
                    tmp_data[3] = 0xA3;
                    tmp_data[2] = 0x3A;
                    tmp_data[1] = 0xA3;
                    tmp_data[0] = 0x3A;
                    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

                    TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                               rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                    rtimes++;
                } while (tmp_data[3] != 0xA3 || tmp_data[2] != 0x3A
                         || tmp_data[1] != 0xA3 || tmp_data[0] != 0x3A);

                TPD_INFO("%s:rotative right success!\n", __func__);

            } else if(LANDSCAPE_SCREEN_90 == chip_info->touch_direction) { //turn left
                do {
                    if (rtimes > 10) {
                        TPD_INFO("%s:rotative left failed!\n", __func__);
                        TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x1A, 0xA1, 0x1A, 0xA1\n", __func__);
                        ret = -1;
                        break;
                    }

                    tmp_addr[3] = 0x10;
                    tmp_addr[2] = 0x00;
                    tmp_addr[1] = 0x7F;
                    tmp_addr[0] = 0x3C;
                    tmp_data[3] = 0xA1;
                    tmp_data[2] = 0x1A;
                    tmp_data[1] = 0xA1;
                    tmp_data[0] = 0x1A;
                    hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                    hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);

                    TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                               rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                    rtimes++;
                } while (tmp_data[3] != 0xA1 || tmp_data[2] != 0x1A
                         || tmp_data[1] != 0xA1 || tmp_data[0] != 0x1A);

                TPD_INFO("%s:rotative left success!\n", __func__);

            }
        }
    } else {
        if (mode) {//open
            do {
                if (rtimes > 10) {
                    TPD_INFO("%s:open edge limit failed!\n", __func__);
                    TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x5A, 0xA5, 0x5A, 0xA5\n", __func__);
                    ret = -1;
                    break;
                }

                tmp_addr[3] = 0x10;
                tmp_addr[2] = 0x00;
                tmp_addr[1] = 0x7F;
                tmp_addr[0] = 0x3C;
                tmp_data[3] = 0xA5;
                tmp_data[2] = 0x5A;
                tmp_data[1] = 0xA5;
                tmp_data[0] = 0x5A;
                hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
                TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                           rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                rtimes++;
            } while (tmp_data[3] != 0xA5 || tmp_data[2] != 0x5A
                     || tmp_data[1] != 0xA5 || tmp_data[0] != 0x5A);

            TPD_INFO("%s:open edge limit success!\n", __func__);

        } else {//close
            do {
                if (rtimes > 10) {
                    TPD_INFO("%s:close edge limit failed!\n", __func__);
                    TPD_INFO("%s:correct tmp_data[0, 1, 2, 3] = 0x9A, 0xA9, 0x9A, 0xA9\n", __func__);
                    ret = -1;
                    break;
                }

                tmp_addr[3] = 0x10;
                tmp_addr[2] = 0x00;
                tmp_addr[1] = 0x7F;
                tmp_addr[0] = 0x3C;
                tmp_data[3] = 0xA9;
                tmp_data[2] = 0x9A;
                tmp_data[1] = 0xA9;
                tmp_data[0] = 0x9A;
                hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

                hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
                TPD_DETAIL("%s:retry times %d, current tmp_data[0, 1, 2, 3] = 0x%2.2X, 0x%2.2X, 0x%2.2X, 0x%2.2X\n", __func__,
                           rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
                rtimes++;
            } while (tmp_data[3] != 0xA9 || tmp_data[2] != 0x9A
                     || tmp_data[1] != 0xA9 || tmp_data[0] != 0x9A);

            TPD_INFO("%s:close edge limit success!\n", __func__);
        }
    }
    TPD_DETAIL("%s:END\n", __func__);
    return ret;
}

static int hx83112a_nf_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    switch(mode) {
    case MODE_NORMAL:
        ret = hx83112a_nf_configuration_init(chip_info, true);
        if (ret < 0) {
            TPD_INFO("%s: hx83112a_nf configuration init failed.\n", __func__);
            return ret;
        }

        break;

    case MODE_SLEEP:
        /*device control: sleep mode*/
        ret = hx83112a_nf_configuration_init(chip_info, false) ;
        if (ret < 0) {
            TPD_INFO("%s: hx83112a_nf configuration init failed.\n", __func__);
            return ret;
        }

        break;

    case MODE_GESTURE:
        ret = hx83112a_nf_enable_black_gesture(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: hx83112a_nf enable gesture failed.\n", __func__);
            return ret;
        }

        break;

    case MODE_GLOVE:

        break;

    case MODE_EDGE:
        //ret = hx83112a_nf_enable_edge_limit(chip_info, flag);
        ret = hx83112a_nf_rotative_switch(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: hx83112a_nf enable edg & corner limit failed.\n", __func__);
            return ret;
        }

        break;

    case MODE_CHARGE:
        ret = hx83112a_nf_enable_charge_mode(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
        }
        break;

    case MODE_HEADSET:
        ret = hx83112a_nf_enable_headset_mode(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable headset mode : %d failed\n", __func__, flag);
        }
        break;

    case MODE_GAME:
        ret = hx83112a_nf_jitter_switch(chip_info, !flag);
        if (ret < 0) {
            TPD_INFO("%s: enable game mode : %d failed\n", __func__, !flag);
        }
        break;

    default:
        TPD_INFO("%s: Wrong mode.\n", __func__);
    }

    return ret;
}

void hx83112a_nf_set_SMWP_enable(uint8_t SMWP_enable, bool suspended)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    uint8_t back_data[4];
    uint8_t retry_cnt = 0;

    hx83112a_nf_sense_off();

    /* Enable:0x10007F10 = 0xA55AA55A */
    do {
        if (SMWP_enable) {
            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0x10;
            tmp_data[3] = 0xA5;
            tmp_data[2] = 0x5A;
            tmp_data[1] = 0xA5;
            tmp_data[0] = 0x5A;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            back_data[3] = 0XA5;
            back_data[2] = 0X5A;
            back_data[1] = 0XA5;
            back_data[0] = 0X5A;
        } else {
            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0x10;
            tmp_data[3] = 0x00;
            tmp_data[2] = 0x00;
            tmp_data[1] = 0x00;
            tmp_data[0] = 0x00;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);
            back_data[3] = 0X00;
            back_data[2] = 0X00;
            back_data[1] = 0X00;
            back_data[0] = 0x00;
        }
        hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
        TPD_INFO("%s: tmp_data[0]=%d, SMWP_enable=%d, retry_cnt=%d \n", __func__, tmp_data[0], SMWP_enable, retry_cnt);
        retry_cnt++;
    } while ((tmp_data[3] != back_data[3] || tmp_data[2] != back_data[2] || tmp_data[1] != back_data[1] || tmp_data[0] != back_data[0]) && retry_cnt < 10);

    hx83112a_nf_sense_on(0);

}


static int hx83112a_nf_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    int i = 0;
    int gesture_sign = 0;
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    uint8_t *buf;
    int gest_len;
    int check_FC = 0;

    int check_sum_cal;
    int ts_status = HX_REPORT_SMWP_EVENT;

    //TPD_DEBUG("%s:%d\n", __func__, __LINE__);

    buf = kzalloc(hx83112a_nf_touch_data->event_size * sizeof(uint8_t), GFP_KERNEL);
    if (!buf) {
        TPD_INFO("%s:%d kzalloc buf error\n", __func__, __LINE__);
        return -1;
    }

    hx83112a_nf_burst_enable(0);
    if (!hx83112a_nf_read_event_stack(buf, hx83112a_nf_touch_data->event_size)) {
        TPD_INFO("%s: can't read data from chip in gesture!\n", __func__);
        kfree(buf);
        return -1;
    }

    for (i = 0; i < 128; i++) {
        if (!i) {
            printk("%s: gesture buf data\n", __func__);
        }
        printk("%02d ", buf[i]);
        if ((i + 1) % 8 == 0) {
            printk("\n");
        }
        if (i == (128 - 1)) {
            printk("\n");
        }
    }

    check_sum_cal = hx83112a_nf_checksum_cal(chip_info, buf, ts_status);
    if (check_sum_cal == CHECKSUM_FAIL) {
        return -1;
    } else if (check_sum_cal == ERR_WORK_OUT) {
        goto err_workqueue_out;
    }

    for (i = 0; i < 4; i++) {
        if (check_FC == 0) {
            if ((buf[0] != 0x00) && ((buf[0] < 0x0E))) {
                check_FC = 1;
                gesture_sign = buf[i];
            } else {
                check_FC = 0;
                //TPD_DEBUG("ID START at %x , value = %x skip the event\n", i, buf[i]);
                break;
            }
        } else {
            if (buf[i] != gesture_sign) {
                check_FC = 0;
                //TPD_DEBUG("ID NOT the same %x != %x So STOP parse event\n", buf[i], gesture_sign);
                break;
            }
        }
        //TPD_DEBUG("0x%2.2X ", buf[i]);
    }
    //TPD_DEBUG("Himax gesture_sign= %x\n",gesture_sign );
    //TPD_DEBUG("Himax check_FC is %d\n", check_FC);

    if (buf[GEST_PTLG_ID_LEN] != GEST_PTLG_HDR_ID1 ||
        buf[GEST_PTLG_ID_LEN + 1] != GEST_PTLG_HDR_ID2) {
        goto RET_OUT;
    }

    if (buf[GEST_PTLG_ID_LEN] == GEST_PTLG_HDR_ID1 &&
        buf[GEST_PTLG_ID_LEN + 1] == GEST_PTLG_HDR_ID2) {
        gest_len = buf[GEST_PTLG_ID_LEN + 2];
        if (gest_len > 52) {
            gest_len = 52;
        }


        i = 0;
        hx83112a_nf_gest_pt_cnt = 0;
        //TPD_DEBUG("gest doornidate start  %s\n",__func__);
#ifdef CONFIG_OPLUS_TP_APK
        if(hx83112a_nf_check_point_format == 0) {
#endif
            while (i < (gest_len + 1) / 2) {

                if (i == 6) {
                    hx83112a_nf_gest_pt_x[hx83112a_nf_gest_pt_cnt] = buf[GEST_PTLG_ID_LEN + 4 + i * 2];
                } else {
                    hx83112a_nf_gest_pt_x[hx83112a_nf_gest_pt_cnt] = buf[GEST_PTLG_ID_LEN + 4 + i * 2] * hx83112a_nf_pri_ts->resolution_info.max_x / 255;
                }
                hx83112a_nf_gest_pt_y[hx83112a_nf_gest_pt_cnt] = buf[GEST_PTLG_ID_LEN + 4 + i * 2 + 1] * hx83112a_nf_pri_ts->resolution_info.max_y / 255;
                i++;
                //TPD_DEBUG("hx83112a_nf_gest_pt_x[%d]=%d \n",hx83112a_nf_gest_pt_cnt,hx83112a_nf_gest_pt_x[hx83112a_nf_gest_pt_cnt]);
                //TPD_DEBUG("hx83112a_nf_gest_pt_y[%d]=%d \n",hx83112a_nf_gest_pt_cnt,hx83112a_nf_gest_pt_y[hx83112a_nf_gest_pt_cnt]);
                hx83112a_nf_gest_pt_cnt += 1;

            }
#ifdef CONFIG_OPLUS_TP_APK
        } else {
            int j = 0;
            int nn;
            int n = 24;
            int m = 26;
            int pt_num;
            hx83112a_nf_gest_pt_cnt = 40;
            if (hx83112a_nf_pri_ts->gesture_buf) {

                pt_num = gest_len + buf[126];
                if (pt_num > 104) {
                    pt_num = 104;
                }
                hx83112a_nf_pri_ts->gesture_buf[0] = gesture_sign;
                hx83112a_nf_pri_ts->gesture_buf[1] = buf[127];

                if (hx83112a_nf_pri_ts->gesture_buf[0] == 0x07) {
                    for(j = 0; j < gest_len * 2; j = j + 2) {
                        hx83112a_nf_pri_ts->gesture_buf[3 + j] = buf[n];
                        hx83112a_nf_pri_ts->gesture_buf[3 + j + 1] = buf[n + 1];
                        n = n + 4;
                    }

                    for(nn = 0; nn < (pt_num - gest_len)   * 2 ; nn = nn + 2) {
                        hx83112a_nf_pri_ts->gesture_buf[3 + j + nn] = buf[m];
                        hx83112a_nf_pri_ts->gesture_buf[3 + j + nn + 1] = buf[m + 1];
                        m = m + 4;
                    }
                    hx83112a_nf_pri_ts->gesture_buf[2] = pt_num;
                } else {
                    hx83112a_nf_pri_ts->gesture_buf[2] = gest_len;
                    memcpy(&hx83112a_nf_pri_ts->gesture_buf[3], &buf[24], 80);
                }

            }
        }
#endif

        if (hx83112a_nf_gest_pt_cnt) {
            gesture->gesture_type = gesture_sign;/* id */
            gesture->Point_start.x = hx83112a_nf_gest_pt_x[0];/* start x */
            gesture->Point_start.y = hx83112a_nf_gest_pt_y[0];/* start y */
            gesture->Point_end.x = hx83112a_nf_gest_pt_x[1];/* end x */
            gesture->Point_end.y = hx83112a_nf_gest_pt_y[1];/* end y */
            gesture->Point_1st.x = hx83112a_nf_gest_pt_x[2]; /* 1 */
            gesture->Point_1st.y = hx83112a_nf_gest_pt_y[2];
            gesture->Point_2nd.x = hx83112a_nf_gest_pt_x[3];/* 2 */
            gesture->Point_2nd.y = hx83112a_nf_gest_pt_y[3];
            gesture->Point_3rd.x = hx83112a_nf_gest_pt_x[4];/* 3 */
            gesture->Point_3rd.y = hx83112a_nf_gest_pt_y[4];
            gesture->Point_4th.x = hx83112a_nf_gest_pt_x[5];/* 4 */
            gesture->Point_4th.y = hx83112a_nf_gest_pt_y[5];
            gesture->clockwise = hx83112a_nf_gest_pt_x[6]; /*  1, 0 */
            //TPD_DEBUG("gesture->gesture_type = %d \n", gesture->gesture_type);
            /*for (i = 0; i < 6; i++)
               TPD_DEBUG("%d [ %d  %d ]\n", i, hx83112a_nf_gest_pt_x[i], hx83112a_nf_gest_pt_y[i]);*/
        }
    }
    //TPD_DETAIL("%s, gesture_type = %d\n", __func__, gesture->gesture_type);

RET_OUT:
    if (buf) {
        kfree(buf);
    }
    return 0;

err_workqueue_out:
    //hx83112a_nf_ic_reset(chip_info, false, true);
    return -1;
}

static int hx83112a_nf_power_control(void *chip_data, bool enable)
{
    int ret = 0;
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    if (true == enable) {
        ret = tp_powercontrol_2v8(chip_info->hw_res, true);
        if (ret)
            return -1;
        ret = tp_powercontrol_1v8(chip_info->hw_res, true);
        if (ret)
            return -1;
        ret = hx83112a_nf_resetgpio_set(chip_info->hw_res, true);
        if (ret)
            return -1;
    } else {
        ret = hx83112a_nf_resetgpio_set(chip_info->hw_res, false);
        if (ret)
            return -1;
        ret = tp_powercontrol_1v8(chip_info->hw_res, false);
        if (ret)
            return -1;
        ret = tp_powercontrol_2v8(chip_info->hw_res, false);
        if (ret)
            return -1;
    }

    return ret;
}
/*
static void store_to_file(int fd, char *format, ...)
{
    va_list args;
    char buf[64] = {0};

    va_start(args, format);
    vsnprintf(buf, 64, format, args);
    va_end(args);

    if (fd >= 0) {
        sys_write(fd, buf, strlen(buf));
    }
}
*/
static int hx83112a_nf_int_pin_test(struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata, char *g_Test_list_log)
{
    int eint_status, eint_count = 0, read_gpio_num = 10;

    TPD_INFO("%s, step 0: begin to check INT-GND short item\n", __func__);
    while (read_gpio_num--) {
        msleep(5);
        eint_status = gpio_get_value(syna_testdata->irq_gpio);
        if (eint_status == 1)
            eint_count--;
        else
            eint_count++;
        TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count, eint_status);
    }
    TPD_INFO("TP EINT PIN direct short! eint_count = %d\n", eint_count);
    if (eint_count == 10) {
        TPD_INFO("error :  TP EINT PIN direct short!\n");
        seq_printf(s, "TP EINT direct stort\n");
        hx83112a_nf_fail_write_count += snprintf(g_Test_list_log + hx83112a_nf_fail_write_count, 45, "eint_status is low, TP EINT direct stort, \n");
        //store_to_file(syna_testdata->fd, "eint_status is low, TP EINT direct stort, \n");
        eint_count = 0;
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static void hx83112a_nf_auto_test(struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata)
{
    int error_count = 0;
    int ret = THP_AFE_INSPECT_OK;
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    char *p_node = NULL;
    char *fw_name_test = NULL;
    char *postfix = "_TEST.img";
    uint8_t copy_len = 0;
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    char g_file_name_OK[64];
    char g_file_name_NG[64];
    char *g_Test_list_log = NULL;
    char *g_project_test_info_log = NULL;
    char *g_Company_info_log = NULL;
    int i = 0;

    hx83112a_nf_rslt_data_len = 0;
    fw_name_test = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
    if (fw_name_test == NULL) {
        TPD_INFO("fw_name_test kzalloc error!\n");
        goto RET_OUT;
    }

    /*init criteria data*/
    ret = hx83112a_nf_self_test_data_init(chip_info);
    /*init criteria data*/

    /*Init Log Data */
    hx83112a_nf_1kind_raw_size = 5 * chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2;
    g_Company_info_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    g_Test_list_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    g_project_test_info_log = kcalloc(256, sizeof(char), GFP_KERNEL);
    hx83112a_nf_fail_write_count = 0;
    hx83112a_nf_file_path_OK = kcalloc(256, sizeof(char), GFP_KERNEL);
    if (!hx83112a_nf_file_path_OK) {
        TPD_INFO("%s:%d hx83112a_nf_file_path_OK kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }
    hx83112a_nf_file_path_NG = kcalloc(256, sizeof(char), GFP_KERNEL);
    if (!hx83112a_nf_file_path_NG) {
        TPD_INFO("%s:%d hx83112a_nf_file_path_NG kzalloc buf error\n", __func__, __LINE__);
        goto RET_OUT;
    }

    if (hx83112a_nf_rslt_data == NULL) {
        TPD_INFO("hx83112a_nf_rslt_data is NULL");
        hx83112a_nf_rslt_data = kcalloc(hx83112a_nf_1kind_raw_size * HX83112A_NF_CRITERIA_ITEM,
                              sizeof(char), GFP_KERNEL);
        if (!hx83112a_nf_rslt_data) {
            TPD_INFO("%s:%d hx83112a_nf_rslt_data kzalloc buf error\n", __func__, __LINE__);
            goto RET_OUT;
        }
    } else {
        memset(hx83112a_nf_rslt_data, 0x00, hx83112a_nf_1kind_raw_size * HX83112A_NF_CRITERIA_ITEM *
               sizeof(char));
    }
    /*Init Log Data */

    p_node = strstr(hx83112a_nf_pri_ts->panel_data.fw_name, ".");
    copy_len = p_node - hx83112a_nf_pri_ts->panel_data.fw_name;
    memcpy(fw_name_test, hx83112a_nf_pri_ts->panel_data.fw_name, copy_len);
    strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
    TPD_INFO("%s : fw_name_test is %s\n", __func__, fw_name_test);

    hx83112a_nf_mcu_0f_operation_test_dirly(fw_name_test);
    msleep(5);
    g_hx83112a_nf_core_fp.fp_reload_disable(0);
    msleep(5);
    hx83112a_nf_sense_on(0x00);
    hx83112a_nf_read_OPLUS_FW_ver(chip_info);

    ret = hx83112a_nf_enable_interrupt(chip_info, false);

    error_count += hx83112a_nf_int_pin_test(s, chip_info, syna_testdata, g_Test_list_log);
    error_count += hx83112a_nf_chip_self_test(s, chip_info, g_Test_list_log);

    /*Save Log Data */
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    sprintf(g_file_name_OK, "tp_testlimit_OK_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    sprintf(g_file_name_NG, "tp_testlimit_NG_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

    if (error_count) {
        snprintf(hx83112a_nf_file_path_NG,
                 (int)(strlen(HX_RSLT_OUT_PATH_NG) + strlen(g_file_name_NG) + 1),
                 "%s%s", HX_RSLT_OUT_PATH_NG, g_file_name_NG);
        hx83112a_nf_test_data_pop_out(chip_info, g_Test_list_log, g_Company_info_log, g_project_test_info_log, hx83112a_nf_rslt_data, hx83112a_nf_file_path_NG);

    } else {
        snprintf(hx83112a_nf_file_path_OK,
                 (int)(strlen(HX_RSLT_OUT_PATH_OK) + strlen(g_file_name_OK) + 1),
                 "%s%s", HX_RSLT_OUT_PATH_OK, g_file_name_OK);
        hx83112a_nf_test_data_pop_out(chip_info, g_Test_list_log, g_Company_info_log, g_project_test_info_log, hx83112a_nf_rslt_data, hx83112a_nf_file_path_OK);
    }
    /*Save Log Data */

    seq_printf(s, "imageid = 0x%llx, deviceid = 0x%llx\n", syna_testdata->TP_FW, syna_testdata->TP_FW);
    seq_printf(s, "%d error(s). %s\n", error_count, error_count ? "" : "All test passed.");
    TPD_INFO(" TP auto test %d error(s). %s\n", error_count, error_count ? "" : "All test passed.");

RET_OUT:
    if (fw_name_test) {
        kfree(fw_name_test);
        fw_name_test = NULL;
    }

    if (hx83112a_nf_inspection_criteria != NULL) {
        for (i = 0; i < HX83112A_NF_CRITERIA_SIZE; i++) {
            if (hx83112a_nf_inspection_criteria[i] != NULL) {
                kfree(hx83112a_nf_inspection_criteria[i]);
                hx83112a_nf_inspection_criteria[i] = NULL;
            }
        }
        kfree(hx83112a_nf_inspection_criteria);
        hx83112a_nf_inspection_criteria = NULL;
        TPD_INFO("Now it have free the hx83112a_nf_inspection_criteria!\n");
    } else {
        TPD_INFO("No Need to free hx83112a_nf_inspection_criteria!\n");
    }

    if (hx83112a_nf_inspt_crtra_flag) {
        kfree(hx83112a_nf_inspt_crtra_flag);
        hx83112a_nf_inspt_crtra_flag = NULL;
    }
    /*
    if (hx83112a_nf_rslt_data) {
        kfree(hx83112a_nf_rslt_data);
        hx83112a_nf_rslt_data = NULL;
    }
    */
    if (hx83112a_nf_file_path_OK) {
        kfree(hx83112a_nf_file_path_OK);
        hx83112a_nf_file_path_OK = NULL;
    }
    if (hx83112a_nf_file_path_NG) {
        kfree(hx83112a_nf_file_path_NG);
        hx83112a_nf_file_path_NG = NULL;
    }
    if (g_Test_list_log) {
        kfree(g_Test_list_log);
        g_Test_list_log = NULL;
    }
    if (g_project_test_info_log) {
        kfree(g_project_test_info_log);
        g_project_test_info_log = NULL;
    }
    if (g_Company_info_log) {
        kfree(g_Company_info_log);
        g_Company_info_log = NULL;
    }
}

static void hx83112a_nf_read_debug_data(struct seq_file *s, void *chip_data, int debug_data_type)
{
    uint16_t mutual_num;
    uint16_t self_num;
    uint16_t width;
    int i = 0;
    int j = 0;
    int k = 0;
    int32_t *data_mutual_sram;
    uint8_t tmp_addr[4] = {0};
    uint8_t tmp_data[4] = {0};

    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    if (!chip_info)
        return ;

    data_mutual_sram = kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * sizeof(int32_t), GFP_KERNEL);
    if (!data_mutual_sram) {
        goto RET_OUT;
    }

    mutual_num = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
    self_num = chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM; //don't add KEY_COUNT
    width = chip_info->hw_res->RX_NUM;
    seq_printf(s, "ChannelStart (rx tx) : %4d, %4d\n\n", chip_info->hw_res->RX_NUM, chip_info->hw_res->TX_NUM);

    //start to show debug data
    switch (debug_data_type) {
    case DEBUG_DATA_BASELINE:
        seq_printf(s, "Baseline data: \n");
        TPD_INFO("Baseline data: \n");
        break;

    case DEBUG_DATA_RAW:
        seq_printf(s, "Raw data: \n");
        TPD_INFO("Raw data: \n");
        break;

    case DEBUG_DATA_DELTA:
        seq_printf(s, "Delta data: \n");
        TPD_INFO("Delta data: \n");
        break;

    case DEBUG_DATA_DOWN:
        seq_printf(s, "Finger down data: \n");
        TPD_INFO("Finger down data: \n");
        break;

    default :
        seq_printf(s, "No this debug datatype \n");
        TPD_INFO("No this debug datatype \n");
        goto RET_OUT;
        break;
    }
    if (debug_data_type == DEBUG_DATA_DOWN) {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xF8;

        tmp_data[3] = 0xA5;
        tmp_data[2] = 0x5A;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        hx83112a_nf_diag_register_set(DEBUG_DATA_DELTA);
    } else {
        hx83112a_nf_diag_register_set(debug_data_type);
    }
    TPD_INFO("%s: Start get debug data in DSRAM\n", __func__);
    HX83112A_NF_DSRAM_Flag = true;

    hx83112a_nf_ts_diag_func(chip_info, data_mutual_sram);

    for (j = 0; j < chip_info->hw_res->RX_NUM; j++) {
        for (i = 0; i < chip_info->hw_res->TX_NUM; i++) {
            k = ((mutual_num - j) - chip_info->hw_res->RX_NUM * i) - 1;
            seq_printf(s, "%6d", data_mutual_sram[k]);
        }
        seq_printf(s, " %6d\n", hx83112a_nf_diag_self[j]);
    }

    seq_printf(s, "\n");
    for (i = 0; i < chip_info->hw_res->TX_NUM; i++) {
        seq_printf(s, "%6d", hx83112a_nf_diag_self[i]);
    }
    //Clear DSRAM flag
    hx83112a_nf_diag_register_set(0x00);
    HX83112A_NF_DSRAM_Flag = false;
    hx83112a_nf_return_event_stack();

    seq_printf(s, "\n");
    seq_printf(s, "ChannelEnd");
    seq_printf(s, "\n");

    TPD_INFO("%s, here:%d\n", __func__, __LINE__);

    if (debug_data_type == DEBUG_DATA_DOWN) {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xF8;

        tmp_data[3] = 0x00;
        tmp_data[2] = 0x00;
        tmp_data[1] = 0x00;
        tmp_data[0] = 0x00;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
    }

RET_OUT:
    if (data_mutual_sram) {
        kfree(data_mutual_sram);
    }

    return;
}

static void hx83112a_nf_baseline_read(struct seq_file *s, void *chip_data)
{
    hx83112a_nf_read_debug_data(s, chip_data, DEBUG_DATA_BASELINE);
    hx83112a_nf_read_debug_data(s, chip_data, DEBUG_DATA_RAW);
    return;
}

static void hx83112a_nf_delta_read(struct seq_file *s, void *chip_data)
{

    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    hx83112a_nf_read_debug_data(s, chip_data, DEBUG_DATA_DELTA);
#ifdef CONFIG_OPLUS_TP_APK
    if (chip_info->debug_mode_sta) {
        hx83112a_nf_read_debug_data(s, chip_data, DEBUG_DATA_DOWN);
    }
#endif // end of CONFIG_OPLUS_TP_APK
    return;
}

static void hx83112a_nf_main_register_read(struct seq_file *s, void *chip_data)
{
    return;
}

//Reserved node
static void hx83112a_nf_reserve_read(struct seq_file *s, void *chip_data)
{
    return;
}

static fw_update_state hx83112a_nf_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    uint32_t CURRENT_FIRMWARE_ID = 0, FIRMWARE_ID = 0;
    uint8_t cmd[4];
    uint8_t data[64];
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    const uint8_t *p_fw_id = NULL ;

    if (fw) {
        if (chip_info->g_fw_buf) {
            chip_info->g_fw_len = fw->size;
            memcpy(chip_info->g_fw_buf, fw->data, fw->size);
            chip_info->g_fw_sta = true;
        }
    }
    if (fw == NULL) {
        TPD_INFO("fw is NULL\n");
        return FW_NO_NEED_UPDATE;
    }

    p_fw_id = fw->data + 49172;

    if (!chip_info) {
        TPD_INFO("Chip info is NULL\n");
        return 0;
    }

    TPD_INFO("%s is called\n", __func__);

    //step 1:fill Fw related header, get all data.


    //step 2:Get FW version from IC && determine whether we need get into update flow.

    CURRENT_FIRMWARE_ID = (*p_fw_id << 24) | (*(p_fw_id + 1) << 16) | (*(p_fw_id + 2) << 8) | *(p_fw_id + 3);


    cmd[3] = 0x10;  // oplus fw id bin address : 0xc014   , 49172    Tp ic address : 0x 10007014
    cmd[2] = 0x00;
    cmd[1] = 0x70;
    cmd[0] = 0x14;
    hx83112a_nf_register_read(cmd, 4, data, false);
    FIRMWARE_ID = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    TPD_INFO("CURRENT TP FIRMWARE ID is 0x%x, FIRMWARE IMAGE ID is 0x%x\n", CURRENT_FIRMWARE_ID, FIRMWARE_ID);

    //disable_irq_nosync(chip_info->hx_irq);

    //step 3:Get into program mode
    /********************get into prog end************/
    //step 4:flash firmware zone
    TPD_INFO("update-----------------firmware ------------------update!\n");
    // hx83112a_nf_fts_ctpm_fw_upgrade_with_sys_fs_64k((unsigned char *)fw->data, fw->size, false);
    g_hx83112a_nf_core_fp.fp_firmware_update_0f(fw);
    g_hx83112a_nf_core_fp.fp_reload_disable(0);
    msleep (10);

    TPD_INFO("Firmware && configuration flash over\n");
    hx83112a_nf_read_OPLUS_FW_ver(chip_info);
    hx83112a_nf_sense_on(0x00);
    msleep (10);

    enable_irq(chip_info->hx_irq);
    //   hx83112a_nf_reset(chip_info);
    //   msleep(200);

    chip_info->first_download_finished = true;
    return FW_UPDATE_SUCCESS;
}

//#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
//extern unsigned int upmu_get_rgs_chrdet(void);
//static int hx83112a_nf_get_usb_state(void)
//{
//    return upmu_get_rgs_chrdet();
//}
//#else
//static int hx83112a_nf_get_usb_state(void)
//{
//    return 0;
//}
//#endif


static int hx83112a_nf_reset_gpio_control(void *chip_data, bool enable)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        TPD_INFO("%s: set reset state %d\n", __func__, enable);
        hx83112a_nf_resetgpio_set(hx83112a_nf_chip_info->hw_res, enable);
        TPD_DETAIL("%s: set reset state END\n", __func__);
    }
    return 0;
}

static void hx83112a_nf_set_touch_direction(void *chip_data, uint8_t dir)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    chip_info->touch_direction = dir;
}

static uint8_t hx83112a_nf_get_touch_direction(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    return chip_info->touch_direction;
}


int hx83112a_nf_freq_point = 0;
void hx83112a_nf_freq_hop_trigger(void *chip_data)
{
    uint8_t tmp_addr[4];
    uint8_t tmp_data[4];
    int rtimes = 0;

    TPD_INFO("send cmd to tigger frequency hopping here!!!\n");
    hx83112a_nf_freq_point = 1 - hx83112a_nf_freq_point;
    if (hx83112a_nf_freq_point) {//hop to frequency 130K
        do {
            if (rtimes > 10) {
                TPD_INFO("%s:frequency hopping failed!\n", __func__);
                TPD_INFO("%s:correct tmp_data[0,1,2,3] = 0x5A,0xA5,0x5A,0xA5\n", __func__);
                break;
            }

            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0xC4;
            tmp_data[3] = 0xA5;
            tmp_data[2] = 0x5A;
            tmp_data[1] = 0xA5;
            tmp_data[0] = 0x5A;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
            TPD_DETAIL("%s:retry times %d, current tmp_data[0,1,2,3] = 0x%2.2X,0x%2.2X,0x%2.2X,0x%2.2X\n", __func__,
                       rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
            rtimes++;
        } while (tmp_data[3] != 0xA5 || tmp_data[2] != 0x5A
                 || tmp_data[1] != 0xA5 || tmp_data[0] != 0x5A);

        if (rtimes <= 10) {
            TPD_INFO("%s:hopping frequency to 130K success!\n", __func__);
        }
    } else {//hop to frequency 75K
        do {
            if (rtimes > 10) {
                TPD_INFO("%s:frequency hopping failed!\n", __func__);
                TPD_INFO("%s:correct tmp_data[0,1,2,3] = 0x3A,0xA3,0x3A,0xA3\n", __func__);
                break;
            }

            tmp_addr[3] = 0x10;
            tmp_addr[2] = 0x00;
            tmp_addr[1] = 0x7F;
            tmp_addr[0] = 0xC4;
            tmp_data[3] = 0xA3;
            tmp_data[2] = 0x3A;
            tmp_data[1] = 0xA3;
            tmp_data[0] = 0x3A;
            hx83112a_nf_flash_write_burst(tmp_addr, tmp_data);

            hx83112a_nf_register_read(tmp_addr, 4, tmp_data, false);
            TPD_DETAIL("%s:retry times %d, current tmp_data[0,1,2,3] = 0x%2.2X,0x%2.2X,0x%2.2X,0x%2.2X\n", __func__,
                       rtimes, tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);
            rtimes++;
        } while (tmp_data[3] != 0xA3 || tmp_data[2] != 0x3A
                 || tmp_data[1] != 0xA3 || tmp_data[0] != 0x3A);

        if (rtimes <= 10) {
            TPD_INFO("%s:hopping frequency to 75K success!\n", __func__);
        }
    }
}

static struct oplus_touchpanel_operations hx83112a_nf_ops = {
    .ftm_process      = hx83112a_nf_ftm_process,
    .get_vendor       = hx83112a_nf_get_vendor,
    .get_chip_info    = hx83112a_nf_get_chip_info,
    .reset            = hx83112a_nf_reset,
    .power_control    = hx83112a_nf_power_control,
    .fw_check         = hx83112a_nf_fw_check,
    .fw_update        = hx83112a_nf_fw_update,
    .trigger_reason   = hx83112a_nf_trigger_reason,
    .get_touch_points = hx83112a_nf_get_touch_points,
    .get_gesture_info = hx83112a_nf_get_gesture_info,
    .mode_switch      = hx83112a_nf_mode_switch,
    .exit_esd_mode    = hx83112a_nf_exit_esd_mode,
//    .resume_prepare = hx83112a_nf_resume_prepare,
//    .get_usb_state    = hx83112a_nf_get_usb_state,
    .black_screen_test = hx83112a_nf_black_screen_test,
    .reset_gpio_control = hx83112a_nf_reset_gpio_control,
    .set_touch_direction    = hx83112a_nf_set_touch_direction,
    .get_touch_direction    = hx83112a_nf_get_touch_direction,
    .freq_hop_trigger = hx83112a_nf_freq_hop_trigger,
};

static struct himax_proc_operations hx83112a_nf_proc_ops = {
    .auto_test     = hx83112a_nf_auto_test,
    .himax_proc_register_write =  hx83112a_nf_proc_register_write,
    .himax_proc_register_read =  hx83112a_nf_proc_register_read,
    .himax_proc_diag_write =  hx83112a_nf_proc_diag_write,
    .himax_proc_diag_read =  hx83112a_nf_proc_diag_read,
    .himax_proc_DD_debug_read =  hx83112a_nf_proc_DD_debug_read,
    .himax_proc_DD_debug_write =  hx83112a_nf_proc_DD_debug_write,
    .himax_proc_FW_debug_read =  hx83112a_nf_proc_FW_debug_read,
    .himax_proc_reset_write =  hx83112a_nf_proc_reset_write,
    .himax_proc_sense_on_off_write =  hx83112a_nf_proc_sense_on_off_write,
#ifdef HX_ENTER_ALGORITHM_NUMBER
    //.hx83112a_nf_proc_enter_algorithm_switch_write = hx83112a_nf_enter_algorithm_number_write,
    //.hx83112a_nf_proc_enter_algorithm_switch_read  = hx83112a_nf_enter_algorithm_number_read,
#endif
};

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read    = himax_limit_read,
    .delta_read    = hx83112a_nf_delta_read,
    .baseline_read = hx83112a_nf_baseline_read,
    .main_register_read = hx83112a_nf_main_register_read,
    .reserve_read = hx83112a_nf_reserve_read,
};

#ifdef CONFIG_OPLUS_TP_APK

static void hx83112a_nf_enter_hopping_write(bool on_off)
{
    uint8_t tmp_addr[4] = {0};
    uint8_t tmp_data[4] = {0};


    if (on_off) {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xF8;

        tmp_data[3] = 0xA5;
        tmp_data[2] = 0x5A;
        tmp_data[1] = 0xA5;
        tmp_data[0] = 0x5A;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xC4;

        tmp_data[3] = 0xA1;
        tmp_data[2] = 0x1A;
        tmp_data[1] = 0xA1;
        tmp_data[0] = 0x1A;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);
        TPD_INFO("%s: open himax enter hopping write.\n", __func__);
    } else {
        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xF8;

        tmp_data[3] = 0;
        tmp_data[2] = 0;
        tmp_data[1] = 0;
        tmp_data[0] = 0;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);

        tmp_addr[3] = 0x10;
        tmp_addr[2] = 0x00;
        tmp_addr[1] = 0x7F;
        tmp_addr[0] = 0xC4;

        tmp_data[3] = 0;
        tmp_data[2] = 0;
        tmp_data[1] = 0;
        tmp_data[0] = 0;
        hx83112a_nf_register_write(tmp_addr, 4, tmp_data, 0);

        TPD_INFO("%s: close himax hopping write.\n", __func__);
    }

}


static void hx83112a_nf_apk_game_set(void *chip_data, bool on_off)
{
    hx83112a_nf_mode_switch(chip_data, MODE_GAME, on_off);
}

static bool hx83112a_nf_apk_game_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    return chip_info->lock_point_status;
}

static void hx83112a_nf_apk_debug_set(void *chip_data, bool on_off)
{
    //u8 cmd[1];
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    hx83112a_nf_debug_mode_set(on_off);
    chip_info->debug_mode_sta = on_off;
}

static bool hx83112a_nf_apk_debug_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    return chip_info->debug_mode_sta;
}

static void hx83112a_nf_apk_gesture_debug(void *chip_data, bool on_off)
{

    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    //get_gesture_fail_reason(on_off);
    chip_info->debug_gesture_sta = on_off;
}

static bool  hx83112a_nf_apk_gesture_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    return chip_info->debug_gesture_sta;
}

static int  hx83112a_nf_apk_gesture_info(void *chip_data, char *buf, int len)
{
    int ret = 0;
    int i;
    int num;
    u8 temp;
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    if(len < 2) {
        return 0;
    }
    buf[0] = 255;

    temp = hx83112a_nf_pri_ts->gesture_buf[0];
    if (temp == 0x00) {
        temp = hx83112a_nf_pri_ts->gesture_buf[1] | 0x80;
    }
    buf[0] = temp;

    //buf[0] = gesture_buf[0];
    num = hx83112a_nf_pri_ts->gesture_buf[2];

    if(num > 40) {
        num = 40;
    }
    ret = 2;

    buf[1] = num;
    //print all data
    for (i = 0; i < num; i++) {
        int x;
        int y;
        x = hx83112a_nf_pri_ts->gesture_buf[i * 2 + 3];
        x = x * hx83112a_nf_pri_ts->resolution_info.max_x / 255;

        y = hx83112a_nf_pri_ts->gesture_buf[i * 2 + 4];
        y = y * hx83112a_nf_pri_ts->resolution_info.max_y / 255;


        //TPD_INFO("nova_apk_gesture_info:gesture x is %d,y is %d.\n", x, y);

        if (len < i * 4 + 2) {
            break;
        }
        buf[i * 4 + 2] = x & 0xFF;
        buf[i * 4 + 3] = (x >> 8) & 0xFF;
        buf[i * 4 + 4] = y & 0xFF;
        buf[i * 4 + 5] = (y >> 8) & 0xFF;
        ret += 4;

    }

    return ret;
}


static void hx83112a_nf_apk_earphone_set(void *chip_data, bool on_off)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    hx83112a_nf_mode_switch(chip_data, MODE_HEADSET, on_off);
    chip_info->earphone_sta = on_off;
}

static bool hx83112a_nf_apk_earphone_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    return chip_info->earphone_sta;
}

static void hx83112a_nf_apk_charger_set(void *chip_data, bool on_off)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    hx83112a_nf_mode_switch(chip_data, MODE_CHARGE, on_off);
    chip_info->plug_status = on_off;


}

static bool hx83112a_nf_apk_charger_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    return chip_info->plug_status;

}

static void hx83112a_nf_apk_noise_set(void *chip_data, bool on_off)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    hx83112a_nf_enter_hopping_write(on_off);
    chip_info->noise_sta = on_off;

}

static bool hx83112a_nf_apk_noise_get(void *chip_data)
{
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;

    return chip_info->noise_sta;

}


static int  hx83112a_nf_apk_tp_info_get(void *chip_data, char *buf, int len)
{
    int ret;
    struct chip_data_hx83112a_nf *chip_info;
    chip_info = (struct chip_data_hx83112a_nf *)chip_data;
    ret = snprintf(buf, len, "IC:HIMAX%06X\nFW_VER:0x%04X\nCH:%dX%d\n",
                   0x83112A,
                   chip_info->fw_ver,
                   chip_info->hw_res->TX_NUM,
                   chip_info->hw_res->RX_NUM);
    if (ret > len) {
        ret = len;
    }

    return ret;
}

static void hx83112a_nf_init_oplus_apk_op(struct touchpanel_data *ts)
{
    ts->apk_op = kzalloc(sizeof(APK_OPERATION), GFP_KERNEL);
    if(ts->apk_op) {
        ts->apk_op->apk_game_set = hx83112a_nf_apk_game_set;
        ts->apk_op->apk_game_get = hx83112a_nf_apk_game_get;
        ts->apk_op->apk_debug_set = hx83112a_nf_apk_debug_set;
        ts->apk_op->apk_debug_get = hx83112a_nf_apk_debug_get;
        //apk_op->apk_proximity_set = ili_apk_proximity_set;
        //apk_op->apk_proximity_dis = ili_apk_proximity_dis;
        ts->apk_op->apk_noise_set = hx83112a_nf_apk_noise_set;
        ts->apk_op->apk_noise_get = hx83112a_nf_apk_noise_get;
        ts->apk_op->apk_gesture_debug = hx83112a_nf_apk_gesture_debug;
        ts->apk_op->apk_gesture_get = hx83112a_nf_apk_gesture_get;
        ts->apk_op->apk_gesture_info = hx83112a_nf_apk_gesture_info;
        ts->apk_op->apk_earphone_set = hx83112a_nf_apk_earphone_set;
        ts->apk_op->apk_earphone_get = hx83112a_nf_apk_earphone_get;
        ts->apk_op->apk_charger_set = hx83112a_nf_apk_charger_set;
        ts->apk_op->apk_charger_get = hx83112a_nf_apk_charger_get;
        ts->apk_op->apk_tp_info_get = hx83112a_nf_apk_tp_info_get;
        //apk_op->apk_data_type_set = ili_apk_data_type_set;
        //apk_op->apk_rawdata_get = ili_apk_rawdata_get;
        //apk_op->apk_diffdata_get = ili_apk_diffdata_get;
        //apk_op->apk_basedata_get = ili_apk_basedata_get;
        //ts->apk_op->apk_backdata_get = nova_apk_backdata_get;
        //apk_op->apk_debug_info = ili_apk_debug_info;

    } else {
        TPD_INFO("Can not kzalloc apk op.\n");
    }
}
#endif // end of CONFIG_OPLUS_TP_APK


static int hx83112a_nf_tp_probe(struct spi_device *spi)
{
    struct chip_data_hx83112a_nf *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    //step1:Alloc chip_info
    chip_info = kzalloc(sizeof(struct chip_data_hx83112a_nf), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(chip_info, 0, sizeof(*chip_info));
    hx83112a_nf_chip_info = chip_info;

    /* allocate himax report data */
    hx83112a_nf_touch_data = kzalloc(sizeof(struct hx83112a_nf_report_data), GFP_KERNEL);
    if (hx83112a_nf_touch_data == NULL) {
        goto err_register_driver;
    }

    //step2:Alloc common ts
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto err_register_driver;
    }
    memset(ts, 0, sizeof(*ts));

    chip_info->g_fw_buf = vmalloc(128 * 1024);
    if (chip_info->g_fw_buf == NULL) {
        TPD_INFO("fw buf vmalloc error\n");
        //ret = -ENOMEM;
        goto err_g_fw_buf;
    }

    //step3:binding dev for easy operate
    chip_info->hx_spi = spi;
    chip_info->syna_ops = &hx83112a_nf_proc_ops;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->s_client = spi;
    chip_info->hx_irq = spi->irq;
    ts->irq = spi->irq;
    spi_set_drvdata(spi, ts);
    ts->dev = &spi->dev;
    ts->chip_data = chip_info;
    chip_info->hw_res = &ts->hw_res;
    mutex_init(&(chip_info->spi_lock));
    chip_info->touch_direction = VERTICAL_SCREEN;
    chip_info->using_headfile = false;
    chip_info->first_download_finished = false;

    if (ts->s_client->master->flags & SPI_MASTER_HALF_DUPLEX) {
        TPD_INFO("Full duplex not supported by master\n");
        ret = -EIO;
        goto err_spi_setup;
    }
    ts->s_client->bits_per_word = 8;
    ts->s_client->mode = SPI_MODE_3;
    ts->s_client->chip_select = 0;

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    /* new usage of MTK spi API */
    memcpy(&chip_info->hx_spi_mcc, &hx83112a_nf_hx_spi_ctrdata, sizeof(struct mtk_chip_config));
    ts->s_client->controller_data = (void *)&chip_info->hx_spi_mcc;
#else
    /* old usage of MTK spi API */
    memcpy(&chip_info->hx_spi_mcc, &hx83112a_nf_hx_spi_ctrdata, sizeof(struct mt_chip_conf));
    ts->s_client->controller_data = (void *)&chip_info->hx_spi_mcc;

    ret = spi_setup(ts->s_client);
    if (ret < 0) {
        TPD_INFO("Failed to perform SPI setup\n");
        goto err_spi_setup;
    }
#endif
    chip_info->p_spuri_fp_touch = &(ts->spuri_fp_touch);

    //disable_irq_nosync(chip_info->hx_irq);

    //step4:file_operations callback binding
    ts->ts_ops = &hx83112a_nf_ops;

    hx83112a_nf_pri_ts = ts;

#ifdef CONFIG_OPLUS_TP_APK
    hx83112a_nf_init_oplus_apk_op(ts);
#endif // end of CONFIG_OPLUS_TP_APK

    //step5:register common touch
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    disable_irq_nosync(chip_info->hx_irq);
    if (hx83112a_nf_ic_package_check() == false) {
        TPD_INFO("Himax chip doesn NOT EXIST");
        goto err_register_driver;
    }
    chip_info->test_limit_name = ts->panel_data.test_limit_name;
#ifdef HX_ZERO_FLASH
    chip_info->p_firmware_headfile = &ts->panel_data.firmware_headfile;
    hx83112a_nf_auto_update_flag = true;
    chip_info->himax_0f_update_wq = create_singlethread_workqueue("HMX_0f_update_reuqest");
    INIT_DELAYED_WORK(&chip_info->work_0f_update, hx83112a_nf_mcu_0f_operation);
    //queue_delayed_work(chip_info->hx83112a_nf_0f_update_wq, &chip_info->work_0f_update, msecs_to_jiffies(2000));
#else
    hx83112a_nf_read_FW_ver();
    hx83112a_nf_calculateChecksum(false);
#endif

    hx83112a_nf_power_on_init();

    //touch data init
    ret = hx83112a_nf_report_data_init(ts->max_num, ts->hw_res.TX_NUM, ts->hw_res.RX_NUM);
    if (ret) {
        goto err_register_driver;
    }

    ts->tp_suspend_order = TP_LCD_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
    ts->skip_suspend_operate = true;
    ts->skip_reset_in_resume = true;

    //step7:create hx83112a_nf related proc files
    himax_create_proc(ts, chip_info->syna_ops);
    hx83112a_nf_irq_en_cnt = 1;
    TPD_INFO("%s, probe normal end\n", __func__);

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (ts->boot_mode == RECOVERY_BOOT)
#else
    if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY)
#endif
    {
        enable_irq(chip_info->hx_irq);
        TPD_INFO("In Recovery mode, no-flash download fw by headfile\n");
        queue_delayed_work(chip_info->himax_0f_update_wq, &chip_info->work_0f_update, msecs_to_jiffies(500));
    } else if (is_oem_unlocked()) {
        enable_irq(chip_info->hx_irq);
        TPD_INFO("It's oem unlocked, download fw in probe\n");
        queue_delayed_work(chip_info->himax_0f_update_wq, &chip_info->work_0f_update, msecs_to_jiffies(5000));
    } else {
        //disable_irq_nosync(chip_info->hx_irq);

        // update fw in probe
        if (ts->fw_update_in_probe_with_headfile) {
            const struct firmware *fw = NULL;
            hx83112a_nf_mcu_firmware_update_0f(fw);
        }
    }

    return 0;
err_spi_setup:
    if (chip_info->g_fw_buf) {
        vfree(chip_info->g_fw_buf);
    }
err_g_fw_buf:
err_register_driver:
    disable_irq_nosync(chip_info->hx_irq);

    common_touch_data_free(ts);
    ts = NULL;

    if (hx83112a_nf_touch_data) {
        kfree(hx83112a_nf_touch_data);
    }

    if (chip_info) {
        kfree(chip_info);
    }

    ret = -1;

    TPD_INFO("%s, probe error\n", __func__);

    return ret;
}

static int hx83112a_nf_tp_remove(struct spi_device *spi)
{
    struct touchpanel_data *ts = spi_get_drvdata(spi);

    ts->s_client = NULL;
    /* spin_unlock_irq(&ts->spi_lock); */
    spi_set_drvdata(spi, NULL);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int hx83112a_nf_i2c_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called gesture_enable =%d\n", __func__, ts->gesture_enable);
    tp_i2c_suspend(ts);

    return 0;
}

static int hx83112a_nf_i2c_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);
    tp_i2c_resume(ts);

    /* if (ts->black_gesture_support) {
         if (ts->gesture_enable == 1) {
             TPD_INFO("hx83112a_nf_set_SMWP_enable 1\n");
             hx83112a_nf_set_SMWP_enable(1, ts->is_suspended);
         }
     }*/
    return 0;
}

static const struct spi_device_id tp_id[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { "oplus,tp_noflash", 0 },
#else
    { TPD_DEVICE, 0 },
#endif
    { }
};

static struct of_device_id tp_match_table[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { .compatible = "oplus,tp_noflash",},
#else
    { .compatible = TPD_DEVICE,},
#endif
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = hx83112a_nf_i2c_suspend,
    .resume = hx83112a_nf_i2c_resume,
#endif
};


static struct spi_driver hx83112a_nf_common_driver = {
    .probe      = hx83112a_nf_tp_probe,
    .remove     = hx83112a_nf_tp_remove,
    .id_table   = tp_id,
    .driver = {
        .name = TPD_DEVICE,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
};

static int __init tp_driver_init(void)
{
    int status = 0;

    TPD_INFO("%s is called\n", __func__);
    if (!tp_judge_ic_match(TPD_DEVICE)) {
        return -1;
    }

    get_lcd_vendor();
    get_oem_verified_boot_state();

    status = spi_register_driver(&hx83112a_nf_common_driver);
    if (status < 0) {
        TPD_INFO("%s, Failed to register SPI driver.\n", __func__);
        return -EINVAL;
    }

    return status;
}

/* should never be called */
static void __exit tp_driver_exit(void)
{
    spi_unregister_driver(&hx83112a_nf_common_driver);
    return;
}

module_init(tp_driver_init);
module_exit(tp_driver_exit);

MODULE_DESCRIPTION("Touchscreen Driver");
MODULE_LICENSE("GPL");
