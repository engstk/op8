/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef SEC_H_S6D7AT0
#define SEC_H_S6D7AT0

/*********PART1:Head files**********************/
#include <linux/i2c.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include "../sec_common.h"

/*********PART2:Define Area**********************/
#define GESTURE_DOUBLECLICK                     0x01
#define GESTURE_UP_V                            0x02
#define GESTURE_DOWN_V                          0x03
#define GESTURE_LEFT_V                          0x04
#define GESTURE_RIGHT_V                         0x05
#define GESTURE_O                               0x06
#define GESTURE_UP                              0x0B
#define GESTURE_DOWN                            0x0A
#define GESTURE_LEFT                            0x09
#define GESTURE_RIGHT                           0x08
#define GESTURE_M                               0x0C
#define GESTURE_W                               0x0D
#define GESTURE_DOUBLE_LINE                     0x07
#define GESTURE_EARSENSE                        0x0E

#define RESET_TO_NORMAL_TIME                    (70)
#define SEC_EVENT_BUFF_SIZE                     8
#define MAX_EVENT_COUNT                         32
#define SEC_FW_BLK_DEFAULT_SIZE                 (256)
#define SEC_FW_BLK_SIZE_MAX                     (512)
#define SEC_FW_HEADER_SIGN                      0x54464953
#define SEC_FW_CHUNK_SIGN                       0x53434654
#define SEC_SELFTEST_REPORT_SIZE                80

#define SEC_COORDINATE_ACTION_NONE              0
#define SEC_COORDINATE_ACTION_PRESS             1
#define SEC_COORDINATE_ACTION_MOVE              2
#define SEC_COORDINATE_ACTION_RELEASE           3

/* SEC event id */
#define SEC_COORDINATE_EVENT                    0
#define SEC_STATUS_EVENT                        1
#define SEC_GESTURE_EVENT                       2
#define SEC_EMPTY_EVENT                         3

//sec status type
#define TYPE_STATUS_EVENT_ERR                   1
#define TYPE_STATUS_EVENT_INFO                  2
#define TYPE_STATUS_EVENT_VENDOR_INFO           7

/* SEC_TS_INFO : Info acknowledge event */
#define SEC_ACK_BOOT_COMPLETE                   0x00
#define SEC_ACK_WET_MODE                        0x01
#define SEC_ACK_PALM_MODE                       0x02
#define SEC_VENDOR_ACK_OFFSET_CAL_DONE          0x40
#define SEC_VENDOR_ACK_SELF_TEST_DONE           0x41
#define SEC_VENDOR_ACK_MIS_CAL_CHECK_DONE       0x43
#define SEC_VENDOR_ACK_NOISE_MODE               0x64

/* SEC_TS_ERROR : Error event */
#define SEC_ERR_EVNET_CORE_ERR                  0x0
#define SEC_ERR_EVENT_QUEUE_FULL                0x01
#define SEC_ERR_EVENT_ESD                       0x2

//earsense status
#define SEC_STATUS_EARDETECTED                  0x84

//boot status
#define SEC_STATUS_BOOT_MODE                    0x10
#define SEC_STATUS_APP_MODE                     0x20

#define STATE_MANAGE_ON                         1
#define STATE_MANAGE_OFF                        0

#define CS_LOW                                  0
#define CS_HIGH                                 1
#define BYTE_PER_PAGE                           256
#define SEC_TS_WAIT_RETRY_CNT                   100

#define SEC_TS_FLASH_WIP_MASK                   0x01
#define SEC_TS_FLASH_SIZE_256                   256

//touchfunction
#define SEC_BIT_SETFUNC_TOUCH                   (1 << 0)
#define SEC_BIT_SETFUNC_CHARGER                 (1 << 4)
#define SEC_BIT_SETFUNC_PALM                    (1 << 5)
#define SEC_BIT_SETFUNC_HEADSET                 (1 << 6)

#define SEC_FLASH_SELFTEST_REPORT_ADDR          0x02b054
#define SEC_FLASH_LP_SELFTEST_REPORT_ADDR       0x034014
#define SELFTEST_FRAME_MAX_SIZE                 32*32
#define SELFTEST_FRAME_SIZE                     32*18*2

#define SEC_TOUCH_IRQ_LOW_CNT                   2
#define SEC_TOUCH_ESD_CNT                       2
#define SEC_TOUCH_ESD_CHECK_PERIOD              200

//cmd
#define SEC_CMD_SENSE_ON                        0x10
#define SEC_CMD_SENSE_OFF                       0x11
#define SEC_CMD_SOFT_RESET                      0x12
#define SEC_CMD_VROM_RESET                      0x42

#define SEC_CMD_FACTORY_PANELCALIBRATION        0x14
#define SEC_CMD_READ_CALIBRATION_REPORT         0xF1
#define SEC_CMD_MIS_CAL_CHECK                   0xA7
#define SEC_CMD_MIS_CAL_READ                    0xA8
#define SEC_CMD_MIS_CAL_SPEC                    0xA9

#define SEC_READ_FIRMWARE_INTEGRITY             0x21
#define SEC_READ_DEVICE_ID                      0x22    //for custom to print IC info
#define SEC_READ_ID                             0x52    //for debug with IC touch mode

#define SEC_CMD_SET_TOUCHFUNCTION               0x30
#define SEC_CMD_WAKEUP_GESTURE_MODE             0x39
#define SEC_CMD_SET_POWER_MODE                  0xE4
#define SEC_CMD_SET_CHARGER_MODE                0x32
#define SEC_CMD_ENTER_FW_MODE                   0x57
#define SEC_CMD_EDGE_DEADZONE                   0xE5

#define SEC_READ_ONE_EVENT                      0x60
#define SEC_READ_ALL_EVENT                      0x61
#define SEC_CMD_CLEAR_EVENT_STACK               0x62
#define SEC_READ_GESTURE_EVENT                  0x90

#define SEC_CMD_MUTU_RAW_TYPE                   0x70
#define SEC_CMD_SELF_RAW_TYPE                   0x71
#define SEC_READ_TOUCH_RAWDATA                  0x72    //read all frame rawdata(ordered by RX len)
#define SEC_READ_TOUCH_SELF_RAWDATA             0x73
#define SEC_READ_TOUCH_SETLEN_RAWDATA           0x74    //read out self define length rawdata(ordered by TX len)
#define SEC_CMD_TOUCH_RAWDATA_SETLEN            0x75    //set rawdata length of reading
#define SEC_CMD_TOUCH_DELTA_READ                0x76    //cmd to read delta data
#define SEC_CMD_TOUCH_RAWDATA_READ              0x77    //cmd to read rawdata data
#define SEC_CMD_TOUCH_SELFDATA_READ             0x78    //cmd to read self data

#define SEC_CMD_SENSITIVITY_MODE                0x77
#define SEC_READ_SENSITIVITY_VALUE              0x78
#define SEC_CMD_AFE_SENSING                     0x7C
#define SEC_CMD_HOPPING                         0x7C

#define SEC_READ_BOOT_STATUS                    0x55
#define SEC_READ_TS_STATUS                      0xAF
#define SEC_READ_FW_VERSION                     0xA3
#define SEC_READ_CONFIG_VERSION                 0xA4
#define SEC_READ_IMG_VERSION                    0xA5

#define SEC_CMD_SELFTEST                        0xAE
#define SEC_CMD_LP_SELFTEST                     0xBE
#define SEC_READ_SELFTEST_RESULT                0x80
#define SEC_CMD_SELFTEST_CHOICE                 0x5F
#define SEC_CMD_SELFTEST_READ                   0xAD

#define SEC_CMD_STATEMANAGE_ON                  0x8E
#define SEC_CMD_CHG_SYSMODE                     0xD7

#define SEC_CMD_GRIP_DEADZONE                   0xE5

#define SEC_CMD_HOVER_DETECT                    0xEE
#define SEC_CMD_SET_P2PTEST_MODE                0x83
#define SEC_CMD_P2PTEST                         0x82
#define SEC_CMD_INTERRUPT_SWITCH                0x89
#define SEC_CMD_INTERRUPT_LEVEL                 0x88
#define SEC_CMD_PALM_SWITCH                     0x30
#define SEC_CMD_GRIP_SWITCH                     0xAA
#define SEC_CMD_SENSETIVE_CTRL                  0x3F
#define SEC_CMD_STOP_FILTER                     0x38

#define SEC_CMD_NVM_SAVE                        0x0A
#define SEC_CMD_NVM_WRITE                       0x0C
#define SEC_CMD_NVM_READ                        0x0B

#define SEC_CMD_CM_OFFSET_WRITE                 0x0D
#define SEC_CMD_CM_OFFSET_READ_SET              0x0E
#define SEC_CMD_CM_OFFSET_READ                  0x0F

#define SEC_CMD_FLASH_ERASE                     0xD8
#define SEC_CMD_FLASH_WRITE                     0xD9
#define SEC_CMD_FLASH_PADDING                   0xDA
#define SEC_CMD_FLASH_READ_ADDR                 0xD0
#define SEC_CMD_FLASH_READ_SIZE                 0xD1
#define SEC_TS_CMD_FLASH_READ_MEM               0xDC

#define SEC_CMD_CS_CONTROL                      0x8B
#define SEC_CMD_FLASH_SEND_DATA                 0xEB
#define SEC_CMD_FLASH_READ_DATA                 0xEC
#define FLASH_CMD_RDSR                          0x05
#define FLASH_CMD_WREN                          0x06
#define FLASH_CMD_CE                            0x60
#define FLASH_CMD_SE                            0x20
#define FLASH_CMD_PP                            0x02


/*********PART3:Struct Area**********************/
typedef struct {
    u32 signature;            /* signature */
    u32 img_ver;            /* App img version */
    u32 totalsize;            /* total size */
    u32 param_area;            /* parameter area */
    u32 flag;            /* mode select/bootloader mode */
    u32 setting;            /* HWB settings */
    u32 checksum;            /* checksum */
    u32 boot_addr;
    u32 fw_ver;
    u32 boot_dddr2;
    u32 flash_addr[3];
    u32 chunk_num[3];
} sec_fw_header;

typedef struct {
    u32 signature;
    u32 addr;
    u32 size;
    u32 reserved;
} sec_fw_chunk;

struct sec_gesture_status {
    u8 eid:2;
    u8 gtype:4;
    u8 stype:2;
    u8 gestureId;
    u8 coordLen;
    u8 data;
    u8 reserved[4];
} __attribute__ ((packed));

/* 8 byte */
struct sec_event_status {
    u8 eid:2;
    u8 stype:4;
    u8 sf:2;
    u8 status_id;
    u8 status_data_1;
    u8 status_data_2;
    u8 status_data_3;
    u8 status_data_4;
    u8 status_data_5;
    u8 left_event_5_0:6;
    u8 reserved_2:2;
} __attribute__ ((packed));

/* 8 byte */
struct sec_event_coordinate {
    u8 eid:2;
    u8 tid:4;
    u8 tchsta:2;
    u8 x_11_4;
    u8 y_11_4;
    u8 y_3_0:4;
    u8 x_3_0:4;
    u8 major;
    u8 minor;
    u8 z:6;
    u8 ttype_3_2:2;
    u8 left_event:6;
    u8 ttype_1_0:2;
} __attribute__ ((packed));

typedef enum {
    TOUCH_SYSTEM_MODE_BOOT          = 0,
    TOUCH_SYSTEM_MODE_CALIBRATION   = 1,
    TOUCH_SYSTEM_MODE_TOUCH         = 2,
    TOUCH_SYSTEM_MODE_SELFTEST      = 3,
    TOUCH_SYSTEM_MODE_FLASH         = 4,
    TOUCH_SYSTEM_MODE_LOWPOWER      = 5,
    TOUCH_SYSTEM_MODE_LISTEN
} TOUCH_SYSTEM_MODE;

typedef enum {
    TOUCH_MODE_STATE_IDLE           = 0,
    TOUCH_MODE_STATE_HOVER          = 1,
    TOUCH_MODE_STATE_TOUCH          = 2,
    TOUCH_MODE_STATE_NOISY          = 3,
    TOUCH_MODE_STATE_CAL            = 4,
    TOUCH_MODE_STATE_CAL2           = 5,
    TOUCH_MODE_STATE_WAKEUP         = 10
} TOUCH_MODE_STATE;

enum {
    TYPE_RAW_DATA                   = 0,    /* Total - Offset : delta data */
    TYPE_SIGNAL_DATA                = 1,    /* Signal - Filtering & Normalization */
    TYPE_AMBIENT_BASELINE           = 2,    /* Cap Baseline */
    TYPE_AMBIENT_DATA               = 3,    /* Cap Ambient */
    TYPE_REMV_BASELINE_DATA         = 4,
    TYPE_DECODED_DATA               = 5,    /* Raw */
    TYPE_REMV_AMB_DATA              = 6,    /*  TYPE_RAW_DATA - TYPE_AMBIENT_DATA */
    TYPE_OFFSET_DATA_SEC            = 19,    /* Cap Offset in SEC Manufacturing Line */
    TYPE_OFFSET_DATA_SDC            = 29,    /* Cap Offset in SDC Manufacturing Line */
    TYPE_NOI_P2P_MIN                = 30,    /* Peak-to-peak noise Min */
    TYPE_NOI_P2P_MAX                = 31,     /* Peak-to-peak noise Max */
    TYPE_DATA_DELTA                 = 60,    /* delta */
    TYPE_DATA_RAWDATA               = 61,    /* rawdata */
    TYPE_INVALID_DATA               = 0xFF,    /* Invalid data type for release factory mode */
};

typedef enum {
    TOUCH_SELFTEST_ITEM_SENSOR_UNI  = 0,
    TOUCH_SELFTEST_ITEM_RAW_VAR_X   = 1,
    TOUCH_SELFTEST_ITEM_RAW_VAR_Y   = 2,
    TOUCH_SELFTEST_ITEM_P2P_MIN     = 3,
    TOUCH_SELFTEST_ITEM_P2P_MAX     = 4,
    TOUCH_SELFTEST_ITEM_OPEN        = 5,
    TOUCH_SELFTEST_ITEM_SHORT       = 6,
    TOUCH_SELFTEST_ITEM_HIGH_Z      = 7,
    TOUCH_SELFTEST_ITEM_OFFSET      = 8,
    TOUCH_SELFTEST_ITEM_RAWDATA     = 9,
    TOUCH_SELFTEST_ITEM_LP_RAWDATA  = 10,
    TOUCH_SELFTEST_ITEM_LP_P2P_MIN  = 11,
    TOUCH_SELFTEST_ITEM_LP_P2P_MAX  = 12
} TOUCH_SELFTEST_ITEM;


struct chip_data_s6d7at0 {
    tp_dev                          tp_type;
    struct i2c_client               *client;
    u8                              boot_ver[3];
    bool                            is_power_down;
    bool                            sec_fw_watchdog_surport;
    struct hw_resource              *hw_res;
    uint32_t                        flash_page_size;
    u8                              first_event[SEC_EVENT_BUFF_SIZE];
    int32_t                         irq_low_cnt;
    int32_t                         esd_stay_cnt;
    uint8_t                         touch_direction;
    bool                            edge_limit_support;
    bool                            fw_edge_limit_support;
};
#endif
