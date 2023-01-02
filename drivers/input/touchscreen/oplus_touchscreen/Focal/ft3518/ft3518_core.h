/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FT3518_CORE_H__
#define __FT3518_CORE_H__

/*********PART1:Head files**********************/
#include <linux/i2c.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include <linux/version.h>

#include "../focal_common.h"

/*********PART2:Define Area**********************/

#define RESET_TO_NORMAL_TIME                    200        /*Sleep time after reset*/
#define POWEWRUP_TO_RESET_TIME                  10

#define INTERVAL_READ_REG                       200  /* unit:ms */
#define TIMEOUT_READ_REG                        1000 /* unit:ms */

#define FTS_VAL_CHIP_ID                         0x54
#define FTS_VAL_CHIP_ID2                        0x52
#define FTS_VAL_BL_ID                           0x54
#define FTS_VAL_BL_ID2                          0x5C


#define FTS_REG_POINTS                          0x01
#define FTS_REG_POINTS_N                        0x10
#define FTS_REG_POINTS_LB                       0x3E

#define FTS_REG_SMOOTH_LEVEL                    0x85
#define FTS_REG_GAME_MODE_EN                    0x86
#define FTS_REG_HIGH_FRAME_TIME                 0x8A
#define FTS_REG_REPORT_RATE                     0x88/*0x12:180hz, 0x0C:120hz*/
#define FTS_REG_CHARGER_MODE_EN                 0x8B
#define FTS_REG_EDGE_LIMIT                      0x8C
#define FTS_REG_SENSITIVE_LEVEL                 0x90
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
#define FTS_REG_GESTURE_CONFIG1                 0xD1
#define FTS_REG_GESTURE_CONFIG2                 0xD2
#define FTS_REG_GESTURE_CONFIG3                 0xD5
#define FTS_REG_GESTURE_CONFIG4                 0xD6
#define FTS_REG_GESTURE_CONFIG5                 0xD7
#define FTS_REG_GESTURE_CONFIG6                 0xD8
#define FTS_REG_GESTURE_OUTPUT_ADDRESS          0xD3
#define FTS_REG_MODULE_ID                       0xE3
#define FTS_REG_LIC_VER                         0xE4
#define FTS_REG_AUTOCLB_ADDR                    0xEE
#define FTS_REG_SAMSUNG_SPECIFAL                0xFA
#define FTS_REG_HEALTH_1                        0xFD
#define FTS_REG_HEALTH_2                        0xFE


#define FTS_MAX_POINTS_SUPPORT                  10
#define FTS_MAX_ID                              0x0A
#define FTS_POINTS_ONE                          15  /*2 + 6*10 + 1*/
#define FTS_POINTS_TWO                          47  /*8*10 - 1*/
#define FTS_MAX_POINTS_LENGTH          ((FTS_POINTS_ONE) + (FTS_POINTS_TWO))

#define FTS_GESTURE_DATA_LEN                    28


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

/* mc_sc */
#define FACTORY_REG_LINE_ADDR                   0x01
#define FACTORY_REG_CHX_NUM                     0x02
#define FACTORY_REG_CHY_NUM                     0x03
#define FACTORY_REG_CLB                         0x04
#define FACTORY_REG_DATA_SELECT                 0x06
#define FACTORY_REG_FRE_LIST                    0x0A
#define FACTORY_REG_TOUCH_THR                   0x0D
#define FACTORY_REG_NORMALIZE                   0x16
#define FACTORY_REG_MAX_DIFF                    0x1B
#define FACTORY_REG_FRAME_NUM                   0x1C

#define FACTORY_REG_RAWDATA_ADDR_MC_SC          0x36
#define FACTORY_REG_FIR                         0xFB
#define FACTORY_REG_WC_SEL                      0x09
#define FACTORY_REG_MC_SC_MODE                  0x44
#define FACTORY_REG_MC_SC_CB_ADDR_OFF           0x45
#define FACTORY_REG_MC_SC_CB_ADDR               0x4E
#define FACTROY_REG_SHORT_TEST_EN               0x07
#define FACTROY_REG_SHORT_CA                    0x01
#define FACTROY_REG_SHORT_CC                    0x02
#define FACTROY_REG_SHORT_CG                    0x03
#define FACTROY_REG_SHORT_OFFSET                0x04
#define FACTROY_REG_SHORT_AB_CH                 0x58
#define FACTROY_REG_SHORT_DELAY                 0x5A
#define FACTORY_REG_SHORT_ADDR_MC               0xF4


#define TEST_RETVAL_00                          0x00
#define TEST_RETVAL_AA                          0xAA

#define FTS_EVENT_FOD                           0x26


#define MAX_PACKET_SIZE                         128

#define FTS_120HZ_REPORT_RATE                   0x0C
#define FTS_180HZ_REPORT_RATE                   0x12


struct mc_sc_threshold {
    int noise_coefficient;
    int short_cg;
    int short_cc;
    int *node_valid;
    int *node_valid_sc;
    int *rawdata_h_min;
    int *rawdata_h_max;
    int *tx_linearity_max;
    int *tx_linearity_min;
    int *rx_linearity_max;
    int *rx_linearity_min;
    int *scap_cb_off_min;
    int *scap_cb_off_max;
    int *scap_cb_on_min;
    int *scap_cb_on_max;
    int *scap_rawdata_off_min;
    int *scap_rawdata_off_max;
    int *scap_rawdata_on_min;
    int *scap_rawdata_on_max;
    int *panel_differ_min;
    int *panel_differ_max;
};

struct fts_test {
    struct mc_sc_threshold thr;
};

struct fts_autotest_offset {
    int32_t *fts_raw_data_P;
    int32_t *fts_raw_data_N;
    int32_t *fts_noise_data_P;
    int32_t *fts_noise_data_N;
    int32_t *fts_uniformity_data_P;
    int32_t *fts_uniformity_data_N;
    int32_t *fts_scap_cb_data_P;
    int32_t *fts_scap_cb_data_N;
    int32_t *fts_scap_raw_data_P;
    int32_t *fts_scap_raw_data_N;
    int32_t *fts_scap_cb_data_waterproof_P;
    int32_t *fts_scap_cb_data_waterproof_N;
    int32_t *fts_panel_differ_data_P;
    int32_t *fts_panel_differ_data_N;
    int32_t *fts_scap_raw_waterproof_data_P;
    int32_t *fts_scap_raw_waterproof_data_N;
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

struct fts_ts_data {
    bool esd_check_need_stop;   /*true:esd check do nothing*/
    bool esd_check_enabled;
    bool use_panelfactory_limit;

    u8 rbuf[FTS_MAX_POINTS_LENGTH];
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
    int *noise_rawdata;
    int *rawdata;
    int *scap_cb;
    int *scap_rawdata;
    int *panel_differ_raw;
    int *rawdata_linearity;

    char *test_limit_name;
    char *fw_name;

    tp_dev tp_type;             /*tp_devices.h*/

    struct i2c_client *client;
    struct device *dev;
    struct hw_resource *hw_res;
    struct fts_proc_operations *syna_ops;
    struct fts_fod_info fod_info;
    struct seq_file *s;
    struct fts_test mpt;
    struct fts_autotest_offset *fts_autotest_offset;
    struct touchpanel_data *ts;
    struct monitor_data_v2 *monitor_data_v2;
	struct exception_data  *exception_data;                /*health monitor data*/
    int gesture_state;
    bool black_gesture_indep;
    bool high_resolution_support;
	bool high_resolution_support_x8;
	bool need_pinctrl_pull_up_reset;
};


extern struct fts_ts_data *fts_data;

int fts_test_entry(struct fts_ts_data *ts_data);
int fts_rstpin_reset(void *chip_data);
#endif /*__FT3518_CORE_H__*/
