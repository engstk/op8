// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */



#ifndef __FT8006S_AA_CORE_H__
#define __FT8006S_AA_CORE_H__

/*********PART1:Head files**********************/
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include <linux/version.h>

#ifdef CONFIG_SPI_MT65XX
#include <linux/platform_data/spi-mt65xx.h>
#endif


#include "../focal_common.h"

/*********PART2:Define Area**********************/

#define RESET_TO_NORMAL_TIME                    200        /*Sleep time after reset*/
#define POWEWRUP_TO_RESET_TIME                  10

#define INTERVAL_READ_REG                       200  /* unit:ms */
#define TIMEOUT_READ_REG                        1000 /* unit:ms */

#define FTS_VAL_BOOT_ID                         0x86


#define FTS_REG_POINTS                          0x01
#define FTS_REG_POINTS_N                        0x10
#define FTS_REG_POINTS_LB                       0x3E

#define FTS_REG_GAME_MODE_EN                    0xB8
#define FTS_REG_REPORT_RATE                     0x88/*0x12:180hz, 0x0C:120hz*/
#define FTS_REG_CHARGER_MODE_EN                 0x8B
#define FTS_REG_EDGE_LIMIT                      0x8C
#define FTS_REG_HEADSET_MODE_EN                 0xC3
#define FTS_REG_FOD_EN                          0xCF
#define FTS_REG_FOD_INFO                        0xE1
#define FTS_REG_FOD_INFO_LEN                    9

#define FTS_REG_INT_CNT                         0x8F
#define FTS_REG_FLOW_WORK_CNT                   0x91
#define FTS_REG_CHIP_ID                         0xA3
#define FTS_REG_CHIP_ID                         0xA3
#define FTS_REG_CHIP_ID2                        0x9F
#define FTS_REG_POWER_MODE                      0xA5
#define FTS_REG_FW_VER                          0xA6
#define FTS_REG_VENDOR_ID                       0xA8
#define FTS_REG_GESTURE_EN                      0xD0
#define FTS_REG_GESTURE_OUTPUT_ADDRESS          0xD3
#define FTS_REG_MODULE_ID                       0xE3
#define FTS_REG_LIC_VER                         0xE4
#define FTS_REG_AUTOCLB_ADDR                    0xEE
#define FTS_REG_SAMSUNG_SPECIFAL                0xFA
#define FTS_REG_HEALTH_1                        0xFD
#define FTS_REG_HEALTH_2                        0xFE

#define FTS_REG_FACTORY_MODE_DETACH_FLAG        0xB4



#define FTS_MAX_POINTS_SUPPORT                  10
#define FTS_MAX_ID                              0x0A
#define FTS_POINTS_ONE                          15  /*2 + 6*10 + 1*/
#define FTS_POINTS_TWO                          47  /*8*10 - 1*/
#define FTS_MAX_POINTS_LENGTH          ((FTS_POINTS_ONE) + (FTS_POINTS_TWO))

#define FTS_GESTURE_DATA_LEN                    28

#define FTS_REPORT_BUFFER_SIZE              FTS_MAX_POINTS_LENGTH + FTS_GESTURE_DATA_LEN                            

#define FTS_FLASH_PACKET_LENGTH_SPI             (32 * 1024 - 16)

#define BYTES_PER_TIME                          (128)  /* max:128 */

/*
 * factory test registers
 */
#define ENTER_WORK_FACTORY_RETRIES              5
#define DEVIDE_MODE_ADDR                        0x00
#define FTS_FACTORY_MODE_VALUE                  0x40
#define FTS_WORK_MODE_VALUE                     0x00
#define FACTORY_TEST_RETRY                      50
#define FACTORY_TEST_DELAY                      18
#define FACTORY_TEST_RETRY_DELAY                100

#define FACTORY_REG_LINE_ADDR                   0x01
#define FACTORY_REG_CHX_NUM                     0x02
#define FACTORY_REG_CHY_NUM                     0x03
#define FACTORY_REG_CLB                         0x04
#define FACTORY_REG_DATA_SELECT                 0x06

#define TEST_RETVAL_00                          0x00
#define TEST_RETVAL_AA                          0xAA

#define FTS_EVENT_FOD                           0x26


#define FTX_MAX_COMMMAND_LENGTH                 16


#define MAX_PACKET_SIZE                         128

struct fts_autotest_offset {
    int32_t *node_valid;
    int32_t *fts_short_data_P;
    int32_t *fts_short_data_N;
    int32_t *fts_open_data_P;
    int32_t *fts_open_data_N;
    int32_t *fts_cb_data_P;
    int32_t *fts_cb_data_N;
    int32_t *fts_raw_data_P;
    int32_t *fts_raw_data_N;
    int32_t *fts_lcdnoise_data_P;
    int32_t *fts_lcdnoise_data_N;
    int32_t *fts_uniformity_data_P;
    int32_t *fts_uniformity_data_N;
};


struct fts_fod_info {
    u8 fp_id;
    u8 event_type;
    u8 fp_area_rate;
    u8 tp_area;
    u16 fp_x;
    u16 fp_y;
    u8 fp_down;
    u8 fp_down_report;
};

struct ftxxxx_proc {
    struct proc_dir_entry *proc_entry;
    u8 opmode;
    u8 cmd_len;
    u8 cmd[FTX_MAX_COMMMAND_LENGTH];
};

struct fts_ts_data {
    bool esd_check_enabled;
    bool use_panelfactory_limit;

    u8 rbuf[FTS_REPORT_BUFFER_SIZE];
    u8 irq_type;
    u8 fwver;
    u8 touch_direction;
    u8 fp_en;
    u8 fp_down;

    int rl_cnt;
    int scb_cnt;
    int srawdata_cnt;
    int last_mode;
    int csv_fd;
    int irq_num;
    int probe_done;

    int *short_data;
    int *open_data;
    int *cb_data;
    int *raw_data;
    int *lcdnoise_data;

    bool black_screen_test;

    char *test_limit_name;
    char *fw_name;

    u8 *h_fw_file;
    u32 h_fw_size;
//    const uint8_t *h_fw_file;
//    size_t h_fw_size;


    tp_dev tp_type;             /*tp_devices.h*/
	
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
    struct mtk_chip_config          spi_ctrl;
#else
    struct mt_chip_conf             spi_ctrl;
#endif
#endif // end of CONFIG_TOUCHPANEL_MTK_PLATFORM

    struct i2c_client *client;
    struct spi_device *spi;
    struct device *dev;
    struct hw_resource *hw_res;
    struct fts_proc_operations *syna_ops;
    struct fts_fod_info fod_info;
    struct ftxxxx_proc proc;
    struct seq_file *s;
    struct fts_autotest_offset *fts_autotest_offset;
    struct touchpanel_data *ts;
    struct firmware_headfile       *p_firmware_headfile;
};


extern struct fts_ts_data *fts_data;

int fts_test_entry(struct fts_ts_data *ts_data, bool black_screen);
int fts_read(struct spi_device *spi, u8 *cmd, u32 cmdlen, u8 *data, u32 datalen);
int fts_read_reg(struct spi_device *spi, u8 addr, u8 *value);
int fts_write(struct spi_device *spi, u8 *writebuf, u32 writelen);
int fts_write_reg(struct spi_device *spi, u8 addr, u8 value);
#endif /*__FT3518_CORE_H__*/
