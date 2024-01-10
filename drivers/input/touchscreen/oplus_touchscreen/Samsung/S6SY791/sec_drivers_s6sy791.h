/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef SEC_H_S6SY791
#define SEC_H_S6SY791

/*********PART1:Head files**********************/
#include <linux/i2c.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include "../sec_common.h"
#include "../../touchpanel_prevention.h"

/*********PART2:Define Area**********************/
#define GESTURE_DOUBLECLICK                     0x00
#define GESTURE_UP_V                            0x01
#define GESTURE_DOWN_V                          0x02
#define GESTURE_LEFT_V                          0x03
#define GESTURE_RIGHT_V                         0x04
#define GESTURE_O                               0x05
#define GESTURE_UP                              0x06
#define GESTURE_DOWN                            0x07
#define GESTURE_LEFT                            0x08
#define GESTURE_RIGHT                           0x09
#define GESTURE_M                               0x0A
#define GESTURE_W                               0x0B
#define GESTURE_DOUBLE_LINE                     0x0C
#define GESTURE_SINGLE_TAP                      0x0E
#define GESTURE_S                               0x0F
#define GESTURE_EARSENSE                        0x0E

#define RESET_TO_NORMAL_TIME                    (70)
#define SEC_EVENT_BUFF_SIZE                     8
#define MAX_EVENT_COUNT                         32
#define SEC_FW_BLK_DEFAULT_SIZE                 (256)
#define SEC_FW_BLK_SIZE_MAX                     (512)
#define SEC_FW_HEADER_SIGN                      0x53494654
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

// t_type
#define TYPE_SPECIAL_RELEASE                    9

/* SEC_TS_INFO : Info acknowledge event */
#define SEC_ACK_BOOT_COMPLETE                   0x00
#define SEC_ACK_WET_MODE                        0x01
#define SEC_VENDOR_ACK_OFFSET_CAL_DONE          0x40
#define SEC_VENDOR_ACK_SELF_TEST_DONE           0x41
#define SEC_VENDOR_ACK_P2P_TEST_DONE            0x42
#define SEC_VENDOR_INFO_NOISE_MODE              0x64
#define SEC_VENDOR_INFO_NOISE_LEVEL_CHANGED     0x81

/* SEC_TS_ERROR : Error event */
#define SEC_ERR_EVNET_CORE_ERR                  0x0
#define SEC_ERR_EVENT_QUEUE_FULL                0x01
#define SEC_ERR_EVENT_ESD                       0x2

//earsense status
#define SEC_STATUS_EARDETECTED                  0x84

//touchhold status
#define SEC_STATUS_TOUCHHOLD                    0x6B

//boot status
#define SEC_STATUS_BOOT_MODE                    0x10
#define SEC_STATUS_APP_MODE                     0x20

#define STATE_MANAGE_ON                         1
#define STATE_MANAGE_OFF                        0

//cmd
#define SEC_READ_ONE_EVENT                      0x60
#define SEC_READ_ALL_EVENT                      0x61
#define SEC_CMD_CLEAR_EVENT_STACK               0x62
#define SEC_READ_GESTURE_EVENT                  0x63
#define SEC_CMD_GESTURE_DEBUG                   0x64
#define SEC_QUICK_LAUNCH_ENABLE                 0X65
#define SEC_READ_DEVICE_ID                      0x22    //for custom to print IC info
#define SEC_READ_ID                             0x52    //for debug with IC touch mode
#define SEC_READ_FIRMWARE_INTEGRITY             0x21
#define SEC_READ_BOOT_STATUS                    0x55
#define SEC_READ_TS_STATUS                      0xAF
#define SEC_READ_FW_VERSION                     0xA3
#define SEC_READ_CONFIG_VERSION                 0xA4
#define SEC_CMD_SENSE_ON                        0x10
#define SEC_CMD_SENSE_OFF                       0x11
#define SEC_READ_IMG_VERSION                    0xA5
#define SEC_CMD_ENTER_FW_MODE                   0x57
#define SEC_CMD_SOFT_RESET                      0x12
#define SEC_CMD_FLASH_ERASE                     0xD8
#define SEC_CMD_FLASH_WRITE                     0xD9
#define SEC_CMD_FLASH_PADDING                   0xDA
#define SEC_CMD_FLASH_READ_ADDR                 0xD0
#define SEC_CMD_FLASH_READ_SIZE                 0xD1
#define SEC_CMD_FLASH_READ_DATA                 0xD2
#define SEC_CMD_WAKEUP_GESTURE_MODE             0x39
#define SEC_CMD_SET_POWER_MODE                  0xE4
#define SET_CMD_SET_CHARGER_MODE                0x32
#define SEC_CMD_READ_CALIBRATION_REPORT         0xF1
#define SEC_CMD_FACTORY_PANELCALIBRATION        0x14
#define SEC_CMD_MUTU_RAW_TYPE                   0x70
#define SEC_CMD_SELF_RAW_TYPE                   0x71
#define SEC_READ_TOUCH_RAWDATA                  0x72    //read all frame rawdata(ordered by RX len)
#define SEC_READ_TOUCH_SELF_RAWDATA             0x73
#define SEC_READ_TOUCH_SETLEN_RAWDATA           0x74    //read out self define length rawdata(ordered by TX len)
#define SEC_CMD_TOUCH_RAWDATA_SETLEN            0x75    //set rawdata length of reading
#define SEC_CMD_TOUCH_DELTA_READ                0x76    //cmd to read delta data
#define SEC_CMD_TOUCH_RAWDATA_READ              0x77    //cmd to read rawdata data
#define SEC_CMD_TOUCH_SELFDATA_READ             0x78    //cmd to read self data
#define SEC_CMD_SELFTEST                        0xAE
#define SEC_READ_SELFTEST_RESULT                0x80
#define SEC_CMD_STATEMANAGE_ON                  0x8E
#define SEC_CMD_CHG_SYSMODE                     0xD7
#define SEC_CMD_HOVER_DETECT                    0xEE
#define SEC_CMD_SET_P2PTEST_MODE                0x83
#define SEC_CMD_P2PTEST                         0x82
#define SEC_CMD_INTERRUPT_SWITCH                0x89
#define SEC_CMD_PALM_SWITCH                     0x30
#define SEC_CMD_TOUCHHOLD_SWITCH                0x43
#define SEC_CMD_REFRESH_RATE_SWITCH             0x40

#define SEC_CMD_EDGE_SCREEN                     0xAA
#define SEC_CMD_GRIP_PARA                       0xAB
#define SEC_CMD_GRIP_AREA                       0xAC
#define SEC_CMD_GRIP_DIRECTION                  0xAD
#define SEC_CMD_GRIP_ENABLE                     0xBC

#define SEC_CMD_SENSETIVE_CTRL                  0x3F
#define SEC_CMD_NOISE_CTRL                      0x33
#define SEC_CMD_WET_SWITCH                      0x8B
#define SEC_CMD_GAME_MODE                       0x45
#define SEC_CMD_HEADSET_MODE                    0x4A

#define SEC_CMD_RTDP_ERASE                      0xC9    //Erase RTDP Memory
#define SEC_CMD_RTDP_START                      0xC0    //Start RTDP
#define SEC_CMD_RTDP_INIT_PTR                   0xC8    //init RTDP read point
#define SEC_READ_RTDP_RESULT                    0xC1    //Read RTDP result
#define SEC_READ_RTDP_DATA                      0xC3    //Read RTDP data
#define SEC_READ_RTDP_FRAMENUM                  0xC2    //Read RTDP frame num


/*********PART3:Struct Area**********************/
typedef struct {
    u32 signature;          /* signature */
    u32 version;            /* version */
    u32 totalsize;          /* total size */
    u32 checksum;           /* checksum */
    u32 img_ver;            /* image file version */
    u32 img_date;           /* image file date */
    u32 img_description;    /* image file description */
    u32 fw_ver;             /* firmware version */
    u32 fw_date;            /* firmware date */
    u32 fw_description;     /* firmware description */
    u32 para_ver;           /* parameter version */
    u32 para_date;          /* parameter date */
    u32 para_description;   /* parameter description */
    u32 num_chunk;          /* number of chunk */
    u32 reserved1;
    u32 reserved2;
} sec_fw_header;

typedef struct {
    u32 signature;
    u32 addr;
    u32 size;
    u32 reserved;
} sec_fw_chunk;

struct sec_gesture_status {
    u8 eid: 2;
    u8 gtype: 4;
    u8 stype: 2;
    u8 gestureId;
    u8 coordLen;
    u8 data;
    u8 reserved[4];
} __attribute__ ((packed));

/* 8 byte */
struct sec_event_status {
    u8 eid: 2;
    u8 stype: 4;
    u8 sf: 2;
    u8 status_id;
    u8 status_data_1;
    u8 status_data_2;
    u8 status_data_3;
    u8 status_data_4;
    u8 status_data_5;
    u8 left_event_5_0: 6;
    u8 reserved_2: 2;
} __attribute__ ((packed));

/* 8 byte */
struct sec_event_coordinate {
    u8 eid: 2;
    u8 tid: 4;
    u8 tchsta: 2;
    u8 x_11_4;
    u8 y_11_4;
    u8 y_3_0: 4;
    u8 x_3_0: 4;
    u8 major;
    u8 minor;
    u8 z: 6;
    u8 ttype_3_2: 2;
    u8 left_event: 6;
    u8 ttype_1_0: 2;
} __attribute__ ((packed));

typedef enum {
    DEAD_ZONE_TYPE_1        = 0x00,
    DEAD_ZONE_TYPE_2        = 0x01,
    LONG_PRESS_ZONE         = 0x02,
    LARGE_TOUCH_REJECTION   = 0x03,
    CORNER_LONG_PRESS_ZONE  = 0x04,
    EDGE_SCREEN_HANDLER     = 0x05,
} TOUCH_GRIP_TYPE;

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
    TYPE_RAW_DATA               = 0,    /* Total - Offset : delta data */
    TYPE_SIGNAL_DATA            = 1,    /* Signal - Filtering & Normalization */
    TYPE_AMBIENT_BASELINE       = 2,    /* Cap Baseline */
    TYPE_AMBIENT_DATA           = 3,    /* Cap Ambient */
    TYPE_REMV_BASELINE_DATA     = 4,
    TYPE_DECODED_DATA           = 5,    /* Raw */
    TYPE_REMV_AMB_DATA          = 6,    /*  TYPE_RAW_DATA - TYPE_AMBIENT_DATA */
    TYPE_OFFSET_DATA_SEC        = 19,    /* Cap Offset in SEC Manufacturing Line */
    TYPE_OFFSET_DATA_SDC        = 29,    /* Cap Offset in SDC Manufacturing Line */
    TYPE_NOI_P2P_MIN            = 30,    /* Peak-to-peak noise Min */
    TYPE_NOI_P2P_MAX            = 31,     /* Peak-to-peak noise Max */
    TYPE_DATA_DELTA             = 60,    /* delta */
    TYPE_DATA_RAWDATA           = 61,    /* rawdata */
    TYPE_INVALID_DATA           = 0xFF,    /* Invalid data type for release factory mode */
};

typedef enum {
    LEFT_LONG_SIZE         = 1,
    RIGHT_LONG_SIZE        = 2,
    TOP_SHORT_SIZE         = 3,
    BOTTOM_SHORT_SIZE      = 4,
} GRIP_EDGE_SIZE;

struct chip_data_s6sy791 {
    tp_dev                          tp_type;
    struct i2c_client               *client;
    u8                              boot_ver[3];
    bool                            is_power_down;
    struct hw_resource              *hw_res;
    bool                            cal_needed;
    uint32_t                        flash_page_size;
    uint8_t                         touch_direction;
    u8                              first_event[SEC_EVENT_BUFF_SIZE];
    struct fp_underscreen_info      fp_info;
    bool                            irq_requested;
    int                             *in_suspend;
    int                             *fp_enable;
	bool							auto_test_need_cal_support;
	bool                            old_firmware_flag_check;
	bool oos_lcd_tp_refresh_support;
	bool oos_game_switch_support;
#ifdef CONFIG_OPLUS_TP_APK

    bool lock_point_status;
    bool plug_status;
    bool debug_gesture_sta;
    bool earphone_sta;
    u8 *gesture_buf;
    int geture_points_count;
    bool in_gesture;
    u32 debug_gesture_type;
    //bool water_sta;
#endif //end of CONFIG_OPLUS_TP_APK
};

struct sec_support_grip_zone {
    char                            name[GRIP_TAG_SIZE];
    int                             (*handle_func) (struct grip_zone_area *grip_zone, bool enable);
};

#endif
