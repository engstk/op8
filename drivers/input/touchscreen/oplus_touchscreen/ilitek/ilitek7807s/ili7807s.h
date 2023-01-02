/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __ILI7807S_H
#define __ILI7807S_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/fs.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/uaccess.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <net/sock.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>

#include <linux/namei.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/firmware.h>

#ifdef CONFIG_OF
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#else
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif

#include "../../touchpanel_common.h"
#include "../../touchpanel_healthinfo.h"
#include <soc/oplus/system/oplus_project.h>

#define DRIVER_VERSION                  "3.0.4.0.210906"

/* Options */
#define SPI_CLK                         9      /* follow by clk list */
#define SPI_RETRY                       5
#define TR_BUF_SIZE                     (2*K) /* Buffer size of touch report */
#define TR_BUF_LIST_SIZE                (1*K) /* Buffer size of touch report for debug data */
#define SPI_TX_BUF_SIZE                 4096
#define SPI_RX_BUF_SIZE                 4096
#define WQ_ESD_DELAY                    2000
#define AP_INT_TIMEOUT                  600 /*600ms*/
#define MP_INT_TIMEOUT                  5000 /*5s*/

#define TDDI_RST_BIND                   DISABLE
#define ENABLE_GESTURE                  DISABLE
#define TP_SUSPEND_PRIO                 ENABLE
#define RESUME_BY_DDI                   DISABLE
#define MP_INT_LEVEL                    DISABLE
#define PLL_CLK_WAKEUP_TP_RESUME        DISABLE

/* Plaform compatibility */
#define SPI_DMA_TRANSFER_SPLIT          ENABLE

/* Path */
#define DEBUG_DATA_FILE_SIZE            (10*K)
#define DEBUG_DATA_FILE_PATH            "/sdcard/ILITEK_log.csv"

#define CSV_LCM_ON_OK_PATH            "/sdcard/TpTestReport/screenOn/OK"
#define CSV_LCM_OFF_OK_PATH           "/sdcard/TpTestReport/screenOff/OK"
#define CSV_LCM_ON_NG_PATH            "/sdcard/TpTestReport/screenOn/NG"
#define CSV_LCM_OFF_NG_PATH           "/sdcard/TpTestReport/screenOff/NG"

#define POWER_STATUS_PATH             "/sys/class/power_supply/battery/status"
#define DUMP_FLASH_PATH               "/sdcard/flash_dump"
#define DUMP_IRAM_PATH                "/sdcard/iram_dump"
#define DEF_INI_NAME_PATH             "/sdcard/mp.ini"
#define DEF_FW_FILP_PATH              "/sdcard/ILITEK_FW"

/* Debug messages */
#define DEBUG_NONE      0
#define DEBUG_ALL       1
#define DEBUG_OUTPUT    DEBUG_NONE

#define TPD_DEVICE      "ilitek,ili7807s"
#define DTS_OF_NAME     "tchip,ilitek"
#define DEVICE_ID       "ILITEK_TDDI"


#define ILI_INFO(fmt, arg...)                                           \
    ({                                                                      \
        pr_info("[TP]ILITEK: [INFO](%s, %d): " fmt, __func__, __LINE__, ##arg); \
    })                                                                      \

#define ILI_ERR(fmt, arg...)                                            \
    ({                                                                      \
        pr_err("[TP]ILITEK: [ERR](%s, %d): " fmt, __func__, __LINE__, ##arg);   \
    })                                                                      \

extern bool ili_debug_en;
#define ILI_DBG(fmt, arg...)                                            \
    do {                                                                    \
        if (LEVEL_DEBUG == tp_debug || ili_debug_en)                                            \
            pr_info("[TP]ILITEK: [DEBUG](%s, %d): " fmt, __func__, __LINE__, ##arg);        \
    } while (0)

#define ERR_ALLOC_MEM(X)         ((IS_ERR(X) || X == NULL) ? 1 : 0)
#define K                        (1024)
#define M                        (K * K)
#define ENABLE                   1
#define START                    1
#define ON                       1
#define ILI_WRITE                1
#define ILI_READ                 0
#define DISABLE                  0
#define END                      0
#define OFF                      0
#define NONE                    -1
#define DO_SPI_RECOVER          -2

enum TP_SPI_CLK_LIST {
    TP_SPI_CLK_1M = 1000000,
    TP_SPI_CLK_2M = 2000000,
    TP_SPI_CLK_3M = 3000000,
    TP_SPI_CLK_4M = 4000000,
    TP_SPI_CLK_5M = 5000000,
    TP_SPI_CLK_6M = 6000000,
    TP_SPI_CLK_7M = 7000000,
    TP_SPI_CLK_8M = 8000000,
    TP_SPI_CLK_9M = 9000000,
    TP_SPI_CLK_10M = 10000000,
    TP_SPI_CLK_11M = 11000000,
    TP_SPI_CLK_12M = 12000000,
    TP_SPI_CLK_13M = 13000000,
    TP_SPI_CLK_14M = 14000000,
    TP_SPI_CLK_15M = 15000000,
};

enum TP_RST_METHOD {
    TP_IC_WHOLE_RST = 0,
    TP_IC_CODE_RST,
    TP_HW_RST_ONLY,
};

enum TP_FW_UPGRADE_TYPE {
    UPGRADE_FLASH = 0,
    UPGRADE_IRAM
};

enum TP_FW_UPGRADE_STATUS {
    FW_STAT_INIT = 0,
    FW_UPDATING = 90,
    FW_UPDATE_PASS = 100,
    FW_UPDATE_FAIL = -1
};

enum TP_FW_OPEN_METHOD {
    REQUEST_FIRMWARE = 0,
    FILP_OPEN
};

enum TP_SLEEP_STATUS {
    TP_SUSPEND = 0,
    TP_DEEP_SLEEP = 1,
    TP_RESUME = 2
};

enum TP_PROXIMITY_STATUS {
    DDI_POWER_OFF = 0,
    DDI_POWER_ON = 1,
    WAKE_UP_GESTURE_RECOVERY = 2,
    WAKE_UP_SWITCH_GESTURE_MODE = 3
};


enum TP_SLEEP_CTRL {
    SLEEP_IN = 0x0,
    SLEEP_OUT = 0x1,
    DEEP_SLEEP_IN = 0x3,
    SLEEP_IN_FTM_BEGIN = 0x04,
    SLEEP_IN_FTM_END = 0x05,
    NOT_SLEEP_MODE
};

enum TP_FW_BLOCK_NUM {
    AP = 1,
    DATA = 2,
    TUNING = 3,
    GESTURE = 4,
    MP = 5,
    DDI = 6,
    TAG = 7,
    PARA_BACKUP = 8,
    RESERVE_BLOCK3 = 9,
    RESERVE_BLOCK4 = 10,
    RESERVE_BLOCK5 = 11,
    RESERVE_BLOCK6 = 12,
    RESERVE_BLOCK7 = 13,
    RESERVE_BLOCK8 = 14,
    RESERVE_BLOCK9 = 15,
    RESERVE_BLOCK10 = 16,
};

enum TP_FW_BLOCK_TAG {
    BLOCK_TAG_AF = 0xAF,
    BLOCK_TAG_B0 = 0xB0
};

enum TP_RECORD_DATA {
    DISABLE_RECORD = 0,
    ENABLE_RECORD,
    DATA_RECORD
};

enum TP_DATA_FORMAT {
    DATA_FORMAT_DEMO = 0,
    DATA_FORMAT_DEBUG,
    DATA_FORMAT_DEMO_DEBUG_INFO,
    DATA_FORMAT_GESTURE_SPECIAL_DEMO,
    DATA_FORMAT_GESTURE_INFO,
    DATA_FORMAT_GESTURE_NORMAL,
    DATA_FORMAT_GESTURE_DEMO,
    DATA_FORMAT_GESTURE_DEBUG,
    DATA_FORMAT_DEBUG_LITE_ROI,
    DATA_FORMAT_DEBUG_LITE_WINDOW,
    DATA_FORMAT_DEBUG_LITE_AREA
};

enum NODE_MODE_SWITCH {
    AP_MODE = 0,
    TEST_MODE,
    DEBUG_MODE,
    DEBUG_LITE_ROI,
    DEBUG_LITE_WINDOW,
    DEBUG_LITE_AREA
};

enum TP_ERR_CODE {
    EMP_CMD = 100,
    EMP_PROTOCOL,
    EMP_FILE,
    EMP_INI,
    EMP_TIMING_INFO,
    EMP_INVAL,
    EMP_PARSE,
    EMP_NOMEM,
    EMP_GET_CDC,
    EMP_INT,
    EMP_CHECK_BUY,
    EMP_MODE,
    EMP_FW_PROC,
    EMP_FORMUL_NULL,
    EMP_PARA_NULL,
    EFW_CONVERT_FILE,
    EFW_ICE_MODE,
    EFW_CRC,
    EFW_REST,
    EFW_ERASE,
    EFW_PROGRAM,
    EFW_INTERFACE,
};


enum TP_IC_TYPE {
    ILI_A = 0x0A,
    ILI_B,
    ILI_C,
    ILI_D,
    ILI_E,
    ILI_F,
    ILI_G,
    ILI_H,
    ILI_I,
    ILI_J,
    ILI_K,
    ILI_L,
    ILI_M,
    ILI_N,
    ILI_O,
    ILI_P,
    ILI_Q,
    ILI_R,
    ILI_S,
    ILI_T,
    ILI_U,
    ILI_V,
    ILI_W,
    ILI_X,
    ILI_Y,
    ILI_Z,
};

struct gesture_symbol {
    u8 double_tap                 : 1;
    u8 alphabet_line_2_top        : 1;
    u8 alphabet_line_2_bottom     : 1;
    u8 alphabet_line_2_left       : 1;
    u8 alphabet_line_2_right      : 1;
    u8 alphabet_m                 : 1;
    u8 alphabet_w                 : 1;
    u8 alphabet_c                 : 1;
    u8 alphabet_E                 : 1;
    u8 alphabet_V                 : 1;
    u8 alphabet_O                 : 1;
    u8 alphabet_S                 : 1;
    u8 alphabet_Z                 : 1;
    u8 alphabet_V_down            : 1;
    u8 alphabet_V_left            : 1;
    u8 alphabet_V_right           : 1;
    u8 alphabet_two_line_2_bottom : 1;
    u8 alphabet_F                 : 1;
    u8 alphabet_AT                : 1;
    u8 reserve0                   : 5;
};

struct report_info_block {
    u8 nReportByPixel       : 1;
    u8 nIsHostDownload      : 1;
    u8 nIsSPIICE            : 1;
    u8 nIsSPISLAVE          : 1;
    u8 nIsI2C               : 1;
    u8 nReserved00          : 3;
    u8 nReserved01          : 8;
    u8 nReserved02          : 8;
    u8 nReserved03          : 8;
};

#define TDDI_DEV_ID                             "ILITEK_TDDI"

/* define the width and heigth of a screen. */
#define TOUCH_SCREEN_X_MIN                      0
#define TOUCH_SCREEN_Y_MIN                      0
#define TOUCH_SCREEN_X_MAX                      720
#define TOUCH_SCREEN_Y_MAX                      1440
#define MAX_TOUCH_NUM                           10

#define POSITION_RESOLUTION_X_DEFAULT			1080
/* define the range on panel */
#define TPD_HEIGHT                              2048
#define TPD_WIDTH                               2048

#define TPD_HIGH_RESOLUTION_HEIGHT		8192
#define TPD_HIGH_RESOLUTION_WIDTH		8192

/* Firmware upgrade */
#define CORE_VER_1410                           0x01040100
#define CORE_VER_1420                           0x01040200
#define CORE_VER_1430                           0x01040300
#define CORE_VER_1460                           0x01040600
#define CORE_VER_1470                           0x01040700

#define MAX_HEX_FILE_SIZE                       (160*K)
#define ILI_FILE_HEADER                         256
#define DLM_START_ADDRESS                       0x20610
#define DLM_HEX_ADDRESS                         0x10000
#define MP_HEX_ADDRESS                          0x13000
#define DDI_RSV_BK_ST_ADDR                      0x1E000
#define DDI_RSV_BK_END_ADDR                     0x1FFFF
#define DDI_RSV_BK_SIZE                         (1*K)
#define RSV_BK_ST_ADDR                          0x1E000
#define RSV_BK_END_ADDR                         0x1E3FF
#define FW_VER_ADDR                             0xFFE0
#define FW_BLOCK_INFO_NUM                       17
#define SPI_UPGRADE_LEN                         2048
#define SPI_BUF_SIZE                            MAX_HEX_FILE_SIZE
#define INFO_HEX_ST_ADDR                        0x4F
#define INFO_MP_HEX_ADDR                        0x1F

/* Dummy Registers */
#define WDT_DUMMY_BASED_ADDR                            0x5101C
#define WDT7_DUMMY0                                     WDT_DUMMY_BASED_ADDR
#define WDT8_DUMMY1                                     (WDT_DUMMY_BASED_ADDR + 0x04)
#define WDT9_DUMMY2                                     (WDT_DUMMY_BASED_ADDR + 0x08)

/* The example for the gesture virtual keys */
#define GESTURE_DOUBLECLICK                             0x58
#define GESTURE_UP                                      0x60
#define GESTURE_DOWN                                    0x61
#define GESTURE_LEFT                                    0x62
#define GESTURE_RIGHT                                   0x63
#define GESTURE_M                                       0x64
#define GESTURE_W                                       0x65
#define GESTURE_C                                       0x66
#define GESTURE_E                                       0x67
#define GESTURE_V                                       0x68
#define GESTURE_O                                       0x69
#define GESTURE_S                                       0x6A
#define GESTURE_Z                                       0x6B
#define GESTURE_V_DOWN                                  0x6C
#define GESTURE_V_LEFT                                  0x6D
#define GESTURE_V_RIGHT                                 0x6E
#define GESTURE_TWOLINE_DOWN                            0x6F
#define GESTURE_F                                       0x70
#define GESTURE_AT                                      0x71

#define ESD_GESTURE_PWD                                 0xF38A94EF
#define SPI_ESD_GESTURE_RUN                             0xA67C9DFE
#define I2C_ESD_GESTURE_RUN                             0xA67C9DFE
#define SPI_ESD_GESTURE_PWD_ADDR                        0x25FF8
#define I2C_ESD_GESTURE_PWD_ADDR                        0x40054

#define ESD_GESTURE_CORE146_PWD                         0xF38A
#define SPI_ESD_GESTURE_CORE146_RUN                     0x5B92
#define I2C_ESD_GESTURE_CORE146_RUN                     0xA67C
#define SPI_ESD_GESTURE_CORE146_PWD_ADDR                0x4005C
#define I2C_ESD_GESTURE_CORE146_PWD_ADDR                0x4005C

#define DOUBLE_TAP                                      ( ON )//BIT0
#define ALPHABET_LINE_2_TOP                             ( ON )//BIT1
#define ALPHABET_LINE_2_BOTTOM                          ( ON )//BIT2
#define ALPHABET_LINE_2_LEFT                            ( ON )//BIT3
#define ALPHABET_LINE_2_RIGHT                           ( ON )//BIT4
#define ALPHABET_M                                      ( ON )//BIT5
#define ALPHABET_W                                      ( ON )//BIT6
#define ALPHABET_C                                      ( OFF )//BIT7
#define ALPHABET_E                                      ( OFF )//BIT8
#define ALPHABET_V                                      ( ON )//BIT9
#define ALPHABET_O                                      ( ON )//BIT10
#define ALPHABET_S                                      ( OFF )//BIT11
#define ALPHABET_Z                                      ( OFF )//BIT12
#define ALPHABET_V_DOWN                                 ( ON )//BIT13
#define ALPHABET_V_LEFT                                 ( ON )//BIT14
#define ALPHABET_V_RIGHT                                ( ON )//BIT15
#define ALPHABET_TWO_LINE_2_BOTTOM                      ( ON )//BIT16
#define ALPHABET_F                                      ( OFF )//BIT17
#define ALPHABET_AT                                     ( OFF )//BIT18

/* FW data format */
#define DATA_FORMAT_DEMO_CMD                            0x00
#define DATA_FORMAT_DEBUG_CMD                           0x02
#define DATA_FORMAT_DEMO_DEBUG_INFO_CMD                 0x04
#define DATA_FORMAT_GESTURE_NORMAL_CMD                  0x01
#define DATA_FORMAT_GESTURE_INFO_CMD                    0x02
#define DATA_FORMAT_DEBUG_LITE_CMD                      0x05
#define DATA_FORMAT_DEBUG_LITE_ROI_CMD                  0x01
#define DATA_FORMAT_DEBUG_LITE_WINDOW_CMD               0x02
#define DATA_FORMAT_DEBUG_LITE_AREA_CMD                 0x03
#define P5_X_DEMO_MODE_PACKET_LEN                       43
#define P5_X_DEMO_MODE_PACKET_INFO_LEN			3
#define P5_X_DEMO_MODE_PACKET_LEN_HIGH_RESOLUTION	72
#define P5_X_INFO_HEADER_LENGTH                         3
#define P5_X_INFO_CHECKSUM_LENGTH                       1
#define P5_X_DEMO_DEBUG_INFO_ID0_LENGTH                 14
#define P5_X_DEBUG_MODE_PACKET_LENGTH                   1280
#define P5_X_TEST_MODE_PACKET_LENGTH                    1180
#define P5_X_GESTURE_NORMAL_LENGTH                      8
#define P5_X_GESTURE_INFO_LENGTH                        170
#define P5_X_GESTURE_INFO_LENGTH_HIGH_RESOLUTION	221
#define P5_X_DEBUG_LITE_LENGTH                          300
#define P5_X_CORE_VER_THREE_LENGTH                      5
#define P5_X_CORE_VER_FOUR_LENGTH                       6

/* Protocol */
#define PROTOCOL_VER_500                                0x050000
#define PROTOCOL_VER_510                                0x050100
#define PROTOCOL_VER_520                                0x050200
#define PROTOCOL_VER_530                                0x050300
#define PROTOCOL_VER_540                                0x050400
#define PROTOCOL_VER_550                                0x050500
#define PROTOCOL_VER_560                                0x050600
#define PROTOCOL_VER_570                                0x050700
#define P5_X_READ_DATA_CTRL                             0xF6
#define P5_X_GET_TP_INFORMATION                         0x20
#define P5_X_GET_KEY_INFORMATION                        0x27
#define P5_X_GET_PANEL_INFORMATION                      0x29
#define P5_X_GET_FW_VERSION                             0x21
#define P5_X_GET_PROTOCOL_VERSION                       0x22
#define P5_X_GET_CORE_VERSION                           0x23
#define P5_X_GET_CORE_VERSION_NEW                       0x24
#define P5_X_MODE_CONTROL                               0xF0
#define P5_X_SET_CDC_INIT                               0xF1
#define P5_X_GET_CDC_DATA                               0xF2
#define P5_X_CDC_BUSY_STATE                             0xF3
#define P5_X_MP_TEST_MODE_INFO                          0xFE
#define P5_X_I2C_UART                                   0x40
#define CMD_GET_FLASH_DATA                              0x41
#define CMD_CTRL_INT_ACTION                             0x1B
#define P5_X_FW_UNKNOWN_MODE                            0xFF
#define P5_X_FW_AP_MODE                                 0x00
#define P5_X_FW_TEST_MODE                               0x01
#define P5_X_FW_GESTURE_MODE                            0x0F
#define P5_X_FW_DELTA_DATA_MODE                         0x03
#define P5_X_FW_RAW_DATA_MODE                           0x08
#define P5_X_FW_DELTA_SNR_DATA_MODE                     0x13
#define P5_X_DEMO_PACKET_ID                             0x5A
#define P5_X_DEBUG_PACKET_ID                            0xA7
#define P5_X_DEMO_HIGH_RESOLUTION_PACKET_ID		0x5B
#define P5_X_DEBUG_HIGH_RESOLUTION_PACKET_ID		0xA8

#define P5_X_TEST_PACKET_ID                             0xF2
#define P5_X_GESTURE_PACKET_ID                          0xAA
#define P5_X_GESTURE_FAIL_ID                            0xAE
#define P5_X_I2CUART_PACKET_ID                          0x7A
#define P5_X_DEBUG_LITE_PACKET_ID                       0x9A
#define P5_X_SLAVE_MODE_CMD_ID                          0x5F
#define P5_X_INFO_HEADER_PACKET_ID                      0xB7
#define P5_X_DEMO_DEBUG_INFO_PACKET_ID                  0x5C
#define P5_X_EDGE_PLAM_CTRL_1                           0x01
#define P5_X_EDGE_PLAM_CTRL_2                           0x12
#define SPI_WRITE                                       0x82
#define SPI_READ                                        0x83
#define SPI_ACK                                         0xA3

/* Chips */
#define ILI9881_CHIP                                    0x9881
#define ILI7807_CHIP                                    0x7807
#define ILI9881N_AA                                     0x98811700
#define ILI9881O_AA                                     0x98811800
#define ILI9882_CHIP                                    0x9882
#define TDDI_PID_ADDR                                   0x4009C
#define TDDI_OTP_ID_ADDR                                0x400A0
#define TDDI_ANA_ID_ADDR                                0x400A4
#define TDDI_PC_COUNTER_ADDR                            0x44008
#define TDDI_PC_LATCH_ADDR                              0x51010
#define TDDI_CHIP_RESET_ADDR                            0x40050
#define RAWDATA_NO_BK_SHIFT                             8192

#define ILITEK_DELTA_SNR_MASK (0x1 << 4)

struct ilitek_ts_data {
    struct spi_device *spi;
    struct device *dev;
    struct wakeup_source *ws;
    struct touchpanel_data *ts;
    struct hw_resource *hw_res;
    unsigned long irq_timer;
    bool ignore_first_irq;
    struct firmware_headfile
        *p_firmware_headfile;/*for ili firmware*/
    tp_dev tp_type;
    char *fw_name;
    char *test_limit_name;
    int pointid_info;
    struct point_info points[10];
    u8 touch_direction;
    int sleep_type;
    bool esd_check_enabled;

    struct ilitek_ic_info *chip;
    struct ilitek_protocol_info *protocol;
    struct gesture_coordinate *gcoord;


    struct mutex touch_mutex;
    struct mutex debug_mutex;
    struct mutex debug_read_mutex;
    spinlock_t irq_spin;

    /* physical path to the input device in the system hierarchy */
    const char *phys;

    bool boot;
    u32 fw_pc;
    u32 fw_latch;

    u16 max_x;
    u16 max_y;
    u16 min_x;
    u16 min_y;
    u16 panel_wid;
    u16 panel_hei;
    u8 xch_num;
    u8 ych_num;
    u8 stx;
    u8 srx;
    u8 *update_buf;
    u8 *tr_buf;
    u8 *spi_tx;
    u8 *spi_rx;
    struct firmware tp_fw;

    int actual_tp_mode;
    int tp_data_mode;
    int tp_data_format;
    int tp_data_len;

    int irq_num;
    int tp_rst;
    int tp_int;
    int wait_int_timeout;

    int fw_retry;
    int fw_update_stat;
    int fw_open;
    u8  fw_info[75];
    u8  fw_mp_ver[4];
    bool wq_ctrl;
    bool wq_esd_ctrl;
    bool wq_bat_ctrl;

    bool netlink;
    bool report;
    bool gesture;
    bool mp_retry;

    int gesture_mode;
    int gesture_demo_ctrl;
    struct gesture_symbol ges_sym;

    struct report_info_block rib;

    /* Sending report data to users for the debug */
    bool dnp; //debug node open
    int dbf; //debug data frame
    int odi; //out data index
    wait_queue_head_t inq;
    struct debug_buf_list *dbl;
    int raw_count;
    int delta_count;
    int bg_count;

    int reset;
    int rst_edge_delay;
    int fw_upgrade_mode;
    int mp_ret_len;
    bool wtd_ctrl;
    bool force_fw_update;
    bool oplus_fw_update;
    bool irq_after_recovery;
    bool ddi_rest_done;
    bool resume_by_ddi;
    bool tp_suspend;
    bool info_from_hex;
    bool prox_near;
    bool gesture_load_code;
    bool trans_xy;
    bool ss_ctrl;
    bool node_update;
    bool int_pulse;
    bool pll_clk_wakeup;
	bool position_high_resolution;
	bool eng_flow;

    atomic_t irq_stat;
    atomic_t tp_reset;
    atomic_t ice_stat;
    atomic_t fw_stat;
    atomic_t mp_stat;
    atomic_t tp_sleep;
    atomic_t tp_sw_mode;
    atomic_t cmd_int_check;
    atomic_t esd_stat;

#ifdef CONFIG_OPLUS_TP_APK
    bool plug_status;
    bool lock_point_status;
    bool debug_mode_sta;
    bool debug_gesture_sta;
    bool earphone_sta;
    bool charger_sta;
    bool noise_sta;
#endif //end of CONFIG_OPLUS_TP_APK

    int (*spi_write_then_read)(struct spi_device *spi,
                               const void *txbuf,
                               unsigned n_tx, void *rxbuf, unsigned n_rx);
    int (*wrapper)(u8 *wdata, u32 wlen, u8 *rdata,
                   u32 rlen, bool spi_irq, bool i2c_irq);
    void (*demo_debug_info[5])(u8 *, int);
    int (*detect_int_stat)(bool status);

    struct monitor_data_v2 *monitor_data_v2;
};
extern struct ilitek_ts_data *ilits;

struct ili_demo_debug_info_id0 {
    u32 id                  : 8;
    u32 sys_powr_state_e    : 3;
    u32 sys_state_e         : 3;
    u32 tp_state_e          : 2;

    u32 touch_palm_state    : 2;
    u32 app_an_statu_e      : 3;
    u32 app_sys_bg_err      : 1;
    u32 g_b_wrong_bg        : 1;
    u32 reserved0           : 1;

    u32 normal_mode         : 1;
    u32 charger_mode        : 1;
    u32 glove_mode          : 1;
    u32 stylus_mode         : 1;
    u32 multi_mode          : 1;
    u32 noise_mode          : 1;
    u32 palm_plus_mode      : 1;
    u32 floating_mode       : 1;

    u32 algo_pt_status0     : 3;
    u32 algo_pt_status1     : 3;
    u32 algo_pt_status2     : 3;
    u32 algo_pt_status3     : 3;
    u32 algo_pt_status4     : 3;
    u32 algo_pt_status5     : 3;
    u32 algo_pt_status6     : 3;
    u32 algo_pt_status7     : 3;
    u32 algo_pt_status8     : 3;
    u32 algo_pt_status9     : 3;
    u32 reserved2           : 2;

    u32 hopping_flg         : 1;
    u32 hopping_index       : 5;
    u32 frequency_h         : 2;
    u32 frequency_l         : 8;
    u32 reserved3           : 8;
    u32 reserved4           : 8;
};

struct debug_buf_list {
    bool mark;
    unsigned char *data;
};

struct ilitek_touch_info {
    u16 id;
    u16 x;
    u16 y;
    u16 pressure;
};

struct gesture_coordinate {
    u16 code;
    u8 clockwise;
    int type;
    struct ilitek_touch_info pos_start;
    struct ilitek_touch_info pos_end;
    struct ilitek_touch_info pos_1st;
    struct ilitek_touch_info pos_2nd;
    struct ilitek_touch_info pos_3rd;
    struct ilitek_touch_info pos_4th;
};

struct ilitek_protocol_info {
    u32 ver;
    int fw_ver_len;
    int pro_ver_len;
    int tp_info_len;
    int key_info_len;
    int panel_info_len;
    int core_ver_len;
    int func_ctrl_len;
    int window_len;
    int cdc_len;
    int mp_info_len;
};

struct ilitek_ic_func_ctrl {
    const char *name;
    u8 cmd[6];
    int len;
};

struct ilitek_ic_info {
    u8 type;
    u8 ver;
    u16 id;
    u32 pid;
    u32 pid_addr;
    u32 pc_counter_addr;
    u32 pc_latch_addr;
    u32 reset_addr;
    u32 otp_addr;
    u32 ana_addr;
    u32 otp_id;
    u32 ana_id;
    u32 fw_ver;
    u32 core_ver;
    u32 fw_mp_ver;
    u32 max_count;
    u32 reset_key;
    u16 wtd_key;
    int no_bk_shift;
    bool dma_reset;
    int (*dma_crc)(u32 start_addr, u32 block_size);
};

/* Prototypes for tddi firmware/flash functions */
extern int ili_fw_dump_iram_data(u32 start,
                                 u32 end, bool save, bool mcu);
extern int ili_fw_upgrade(int op);

/* Prototypes for tddi mp test */
extern int ili_mp_test_main(char *apk,
                            bool lcm_on, struct seq_file *s, char *message);

/* Prototypes for tddi core functions */
extern int ili_touch_esd_gesture_iram(void);
extern void ili_set_gesture_symbol(void);
extern int ili_move_gesture_code_iram(int mode);
extern int ili_move_mp_code_iram(void);
extern void ili_touch_press(u16 x, u16 y,
                            u16 pressure, u16 id);
extern void ili_touch_release(u16 x, u16 y,
                              u16 id);
extern void ili_report_ap_mode(u8 *buf, int len);
extern void ili_report_debug_mode(u8 *buf,
                                  int rlen);
extern void ili_report_debug_lite_mode(u8 *buf,
                                       int rlen);
extern void ili_report_gesture_mode(u8 *buf,
                                    int rlen);
extern void ili_report_i2cuart_mode(u8 *buf,
                                    int rlen);
extern void ili_ic_set_ddi_reg_onepage(u8 page,
                                       u8 reg, u8 data, bool mcu);
extern void ili_ic_get_ddi_reg_onepage(u8 page,
                                       u8 reg, u8 *data, bool mcu);
extern int ili_ic_whole_reset(bool mcu);
extern int ili_ic_code_reset(bool mcu);
extern int ili_ic_func_ctrl(const char *name,
                            int ctrl);

extern int ili_ic_int_trigger_ctrl(bool pulse);
extern void ili_ic_get_pc_counter(int stat);
extern int ili_ic_check_int_level(bool level);
extern int ili_ic_check_int_pulse(bool pulse);
extern int ili_ic_check_busy(int count,
                             int delay);
extern int ili_ic_get_panel_info(void);
extern int ili_ic_get_tp_info(void);
extern int ili_ic_get_core_ver(void);
extern int ili_ic_get_protocl_ver(void);
extern int ili_ic_get_fw_ver(void);
extern int ili_ic_get_info(void);
extern int ili_ic_dummy_check(void);
extern int ili_ice_mode_bit_mask_write(u32 addr,
                                       u32 mask, u32 value);
extern int ili_ice_mode_write(u32 addr, u32 data,
                              int len);
extern int ili_ice_mode_read(u32 addr, u32 *data,
                             int len);
extern int ili_ice_mode_ctrl(bool enable,
                             bool mcu);
extern void ili_ic_init(void);
extern void ili_fw_uart_ctrl(u8 ctrl);

/* Prototypes for tddi events */
#if RESUME_BY_DDI
extern void ili_resume_by_ddi(void);
#endif
extern int ili_proximity_far(int mode);
extern int ili_proximity_near(int mode);
extern int ili_switch_tp_mode(u8 data);
extern int ili_set_tp_data_len(int format,
                               bool send, u8 *data);
extern int ili_fw_upgrade_handler(void);
extern int ili_wq_esd_i2c_check(void);
extern int ili_gesture_recovery(void);
extern void ili_spi_recovery(void);
extern int ili_mp_test_handler(char *apk,
                               bool lcm_on, struct seq_file *s, char *message);
extern int ili_report_handler(void);
extern int ili_sleep_handler(int mode);
extern int ili_reset_ctrl(int mode);
extern int ili_tddi_init(void);

/* Prototypes for i2c/spi interface */
extern int ili_core_spi_setup(int num);
extern int ili_spi_write_then_read_split(
    struct spi_device *spi,
    const void *txbuf, unsigned n_tx,
    void *rxbuf, unsigned n_rx);
extern int ili_spi_write_then_read_direct(
    struct spi_device *spi,
    const void *txbuf, unsigned n_tx,
    void *rxbuf, unsigned n_rx);

/* Prototypes for platform level */
extern void ili_irq_disable(void);
extern void ili_irq_enable(void);
extern void ili_tp_reset(void);

/* Prototypes for miscs */
extern void ili_node_init(void);
extern void ili_dump_data(void *data, int type,
                          int len, int row_len, const char *name);
extern u8 ili_calc_packet_checksum(u8 *packet,
                                   int len);
extern void ili_netlink_reply_msg(void *raw,
                                  int size);
extern int ili_katoi(char *str);
extern int ili_str2hex(char *str);

extern int ili_mp_read_write(u8 *wdata, u32 wlen,
                             u8 *rdata, u32 rlen);
/* Prototypes for additional functionalities */
extern void ili_gesture_fail_reason(bool enable);
extern int ili_get_tp_recore_ctrl(int data);
extern int ili_get_tp_recore_data(u16 *out_buf,
                                  u32 out_len);
extern void ili_demo_debug_info_mode(u8 *buf,
                                     size_t rlen);

static inline void ili_kfree(void **mem)
{
    if (*mem != NULL) {
        kfree(*mem);
        *mem = NULL;
    }
}

static inline void ili_vfree(void **mem)
{
    if (*mem != NULL) {
        vfree(*mem);
        *mem = NULL;
    }
}

static inline void *ipio_memcpy(void *dest,
                                const void *src, int n, int dest_size)
{
    if (n > dest_size) {
        n = dest_size;
    }

    return memcpy(dest, src, n);
}

static inline int ipio_strcmp(const char *s1,
                              const char *s2)
{
    return (strlen(s1) != strlen(s2)) ? -1 : strncmp(
               s1, s2, strlen(s1));
}

#endif /* __ILI7807S_H */
