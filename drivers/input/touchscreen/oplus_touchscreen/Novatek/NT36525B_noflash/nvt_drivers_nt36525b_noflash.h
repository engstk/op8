/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef NVT_H_NT36525B_NOFLASH
#define NVT_H_NT36525B_NOFLASH

/*********PART1:Head files**********************/
#include <linux/spi/spi.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#ifdef CONFIG_SPI_MT65XX
#include <linux/platform_data/spi-mt65xx.h>
#endif

#include "../novatek_common.h"
#include "oplus_spi.h"

#define POINT_DATA_CHECKSUM      (1)
#define POINT_DATA_CHECKSUM_LEN 65
#define NVT_TOUCH_ESD_CHECK_PERIOD (2000)
#define NVT_ID_BYTE_MAX 6
#define POINT_DATA_LEN 78
#define FW_BIN_SIZE_116KB       (118784)
#define FW_BIN_SIZE FW_BIN_SIZE_116KB
#define FW_BIN_VER_OFFSET       (0x1A000)
#define FW_BIN_VER_BAR_OFFSET   (0x1A001)
#define SPI_READ_FAST           (0x1F654)
#define SWRST_N8_ADDR           (0x03F0FE) /*525B*/
#define FW_BIN_TYPE_OFFSET      (0x1A00D)
#define FW_BIN_CHECKSUM_LEN     (4)


#define NVT_MMAP_DEBUG_FINGER_DOWN_DIFFDATA   (0x26A78) //debug finger diff (finger down)
#define NVT_MMAP_DEBUG_STATUS_CHANGE_DIFFDATA (0x26CB8) //debug finger diff (status change)

#define DTAP_DETECT                     15
#define UP_VEE_DETECT                   14
#define DOWN_VEE_DETECT                 33
#define LEFT_VEE_DETECT                 31
#define RIGHT_VEE_DETECT                32
#define CIRCLE_DETECT                   18
#define DOUSWIP_DETECT                  34
#define RIGHT_SLIDE_DETECT              24
#define LEFT_SLIDE_DETECT               23
#define DOWN_SLIDE_DETECT               22
#define UP_SLIDE_DETECT                 21
#define M_DETECT                        17
#define W_DETECT                        13

#define EVENTBUFFER_PWR_PLUG_IN          0x53
#define EVENTBUFFER_PWR_PLUG_OUT         0x51
#define EVENTBUFFER_HOPPING_POLLING_ON   0x73
#define EVENTBUFFER_HOPPING_POLLING_OFF  0x74
#define EVENTBUFFER_HOPPING_FIX_FREQ_ON  0x75
#define EVENTBUFFER_HOPPING_FIX_FREQ_OFF 0x76
#define EVENTBUFFER_HS_PLUG_IN           0x77
#define EVENTBUFFER_HS_PLUG_OUT          0x78
#define EVENTBUFFER_EDGE_LIMIT_VERTICAL  0x7A
#define EVENTBUFFER_EDGE_LIMIT_LEFT_UP   0x7B
#define EVENTBUFFER_EDGE_LIMIT_RIGHT_UP  0x7C
#define EVENTBUFFER_GAME_ON            0x7D
#define EVENTBUFFER_GAME_OFF           0x7E

#define EVENTBUFFER_EXT_CMD                       0x7F
#define EVENTBUFFER_EXT_DBG_MSG_DIFF_ON           0x01
#define EVENTBUFFER_EXT_DBG_MSG_DIFF_OFF          0x02
#define EVENTBUFFER_EXT_DBG_WKG_COORD_ON          0x03
#define EVENTBUFFER_EXT_DBG_WKG_COORD_OFF         0x04
#define EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_ON   0x05
#define EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_OFF  0x06
#define EVENTBUFFER_EXT_DBG_WATER_POLLING_ON      0x07
#define EVENTBUFFER_EXT_DBG_WATER_POLLING_OFF     0x08

#define NVT_TOUCH_FW_DEBUG_INFO (1)
#define NVT_DUMP_SRAM   (0)

#define SPI_TANSFER_LENGTH 256

#define XDATA_SECTOR_SIZE       256
#define NORMAL_MODE             0x00
#define TEST_MODE_1             0x21
#define TEST_MODE_2             0x22
#define MP_MODE_CC              0x41
#define FREQ_HOP_DISABLE        0x66
#define FREQ_HOP_ENABLE         0x65
#define HANDSHAKING_HOST_READY  0xBB

#define CMD_OPEN_BLACK_GESTURE  0x13
#define CMD_ENTER_SLEEP         0x11

typedef enum {
    NVT_RAWDATA,    //raw data
    NVT_DIFFDATA,   //diff data
    NVT_BASEDATA,   //baseline data
    NVT_DEBUG_FINGER_DOWN_DIFFDATA,   //debug finger diff (finger down)
    NVT_DEBUG_STATUS_CHANGE_DIFFDATA, //debug finger diff (status change)
} DEBUG_READ_TYPE;

typedef enum {
    EDGE_REJECT_L = 0,
    EDGE_REJECT_H = 1,
    PWR_FLAG = 2,
    HOPPING_FIX_FREQ_FLAG = 3,
    HOPPING_POLLING_FLAG = 4,
    JITTER_FLAG = 6,
    HEADSET_FLAG = 7
} CMD_OFFSET;

typedef enum {
    DEBUG_DIFFDATA_FLAG = 0,
    DEBUG_WKG_COORD_FLAG = 1,
    DEBUG_WKG_COORD_RECORD_FLAG = 2,
    DEBUG_WATER_POLLING_FLAG = 3
} CMD_EXTEND_OFFSET;

typedef enum {
    EVENT_MAP_HOST_CMD                      = 0x50,
    EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE   = 0x51,
    EVENT_MAP_RESET_COMPLETE                = 0x60,
    EVENT_MAP_FWINFO                        = 0x78,
    EVENT_MAP_PROJECTID                     = 0x9A,
} SPI_EVENT_MAP;

typedef enum {
    RESET_STATE_INIT = 0xA0,// IC reset
    RESET_STATE_REK,                // ReK baseline
    RESET_STATE_REK_FINISH, // baseline is ready
    RESET_STATE_NORMAL_RUN, // normal run
    RESET_STATE_MAX  = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
    NVT_MP_PASS = 0,
    NVT_MP_FAIL = 1,
    NVT_MP_FAIL_READ_DATA = 2,
    NVT_MP_UNKNOW = 3
} NVT_MP_TEST_RESULT;

struct nvt_ts_mem_map {
    uint32_t EVENT_BUF_ADDR;
    uint32_t RAW_PIPE0_ADDR;
    uint32_t RAW_PIPE0_Q_ADDR;
    uint32_t RAW_PIPE1_ADDR;
    uint32_t RAW_PIPE1_Q_ADDR;
    uint32_t BASELINE_ADDR;
    uint32_t BASELINE_Q_ADDR;
    uint32_t BASELINE_BTN_ADDR;
    uint32_t BASELINE_BTN_Q_ADDR;
    uint32_t DIFF_PIPE0_ADDR;
    uint32_t DIFF_PIPE0_Q_ADDR;
    uint32_t DIFF_PIPE1_ADDR;
    uint32_t DIFF_PIPE1_Q_ADDR;
    uint32_t RAW_BTN_PIPE0_ADDR;
    uint32_t RAW_BTN_PIPE0_Q_ADDR;
    uint32_t RAW_BTN_PIPE1_ADDR;
    uint32_t RAW_BTN_PIPE1_Q_ADDR;
    uint32_t DIFF_BTN_PIPE0_ADDR;
    uint32_t DIFF_BTN_PIPE0_Q_ADDR;
    uint32_t DIFF_BTN_PIPE1_ADDR;
    uint32_t DIFF_BTN_PIPE1_Q_ADDR;
    uint32_t READ_FLASH_CHECKSUM_ADDR;
    uint32_t RW_FLASH_DATA_ADDR;
    /* Phase 2 Host Download */
    uint32_t BOOT_RDY_ADDR;
    uint32_t POR_CD_ADDR;
    /* BLD CRC */
    uint32_t BLD_LENGTH_ADDR;
    uint32_t ILM_LENGTH_ADDR;
    uint32_t DLM_LENGTH_ADDR;
    uint32_t BLD_DES_ADDR;
    uint32_t ILM_DES_ADDR;
    uint32_t DLM_DES_ADDR;
    uint32_t G_ILM_CHECKSUM_ADDR;
    uint32_t G_DLM_CHECKSUM_ADDR;
    uint32_t R_ILM_CHECKSUM_ADDR;
    uint32_t R_DLM_CHECKSUM_ADDR;
    uint32_t BLD_CRC_EN_ADDR;
    uint32_t DMA_CRC_EN_ADDR;
    uint32_t BLD_ILM_DLM_CRC_ADDR;
    uint32_t DMA_CRC_FLAG_ADDR;
    uint32_t DOZE_GM_S1D_SCAN_RAW_ADDR;
    uint32_t DOZE_GM_BTN_SCAN_RAW_ADDR;
};

struct nvt_ts_bin_map {
    char name[12];
    uint32_t BIN_addr;
    uint32_t SRAM_addr;
    uint32_t size;
    uint32_t crc;
};

struct nvt_ts_trim_id_table {
    uint8_t id[NVT_ID_BYTE_MAX];
    uint8_t mask[NVT_ID_BYTE_MAX];
    const struct nvt_ts_mem_map *mmap;
    uint8_t carrier_system;
    uint8_t support_hw_crc;
};

struct nvt_ts_firmware {
    size_t size;
    const u8 *data;
};

struct nvt_fw_debug_info {
    uint8_t rek_info;
    uint8_t rst_info;
    uint8_t hopping;
    uint8_t esd;
    uint8_t palm;
    uint8_t bending;
    uint8_t water;
    uint8_t gnd;
    uint8_t er;
    uint8_t fog;
    uint8_t film;
    uint8_t notch;
};

struct chip_data_nt36525b {
    bool                            is_sleep_writed;
    char                            *fw_name;
    char                            *test_limit_name;
    struct firmware_headfile        *p_firmware_headfile;
    tp_dev                          tp_type;
    uint8_t                         fw_ver;
    uint8_t                         fw_sub_ver;
    uint8_t                         recovery_cnt;
    uint8_t                         ilm_dlm_num;
    uint8_t                         *fwbuf;
    uint16_t                        nvt_pid;
    uint32_t                        ENG_RST_ADDR;
    uint32_t                        partition;
    struct spi_device               *s_client;
    struct hw_resource              *hw_res;
    struct nvt_ts_trim_id_table     trim_id_table;
    struct nvt_ts_bin_map           *bin_map;
    bool                            esd_check_enabled;
    unsigned long                   irq_timer;
    uint8_t                         esd_retry;
    struct device                   *dev;
    //const struct firmware           *g_fw;
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
    struct mtk_chip_config          spi_ctrl;
#else
    struct mt_chip_conf             spi_ctrl;
#endif
#endif // end of CONFIG_TOUCHPANEL_MTK_PLATFORM
    uint8_t                         touch_direction;    //show touchpanel current direction
    struct mutex                    mutex_testing;
    int                             probe_done;
    bool                            using_headfile;
    int                             lcd_reset_gpio;
    struct nvt_fw_debug_info        nvt_fw_debug_info;
    int irq_num;
    struct touchpanel_data *ts;
    u8 *g_fw_buf;
    size_t g_fw_len;
    bool g_fw_sta;
    u8 *fw_buf_dma;
    bool need_judge_irq_throw;
#ifdef CONFIG_OPLUS_TP_APK

    bool lock_point_status;
    bool plug_status;
    bool debug_mode_sta;
    bool debug_gesture_sta;
    bool earphone_sta;
    bool charger_sta;
    bool noise_sta;
    int water_sta;
#endif //end of CONFIG_OPLUS_TP_APK
};


#endif
