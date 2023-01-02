/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#define GESTURE_DOUBLECLICK                0x58
#define GESTURE_UP                        0x60
#define GESTURE_DOWN                    0x61
#define GESTURE_LEFT                    0x62
#define GESTURE_RIGHT                    0x63
#define GESTURE_M                        0x64
#define GESTURE_W                        0x65
#define GESTURE_C                        0x66
#define GESTURE_E                        0x67
#define GESTURE_V                        0x68
#define GESTURE_O                        0x69
#define GESTURE_S                        0x6A
#define GESTURE_Z                        0x6B

#define GESTURE_V_DOWN                    0x6C
#define GESTURE_V_LEFT                    0x6D
#define GESTURE_V_RIGHT                    0x6E
#define GESTURE_TWOLINE_DOWN            0x6F

#define KEY_GESTURE_D                    KEY_D
#define KEY_GESTURE_UP                    KEY_UP
#define KEY_GESTURE_DOWN                KEY_DOWN
#define KEY_GESTURE_LEFT                KEY_LEFT
#define KEY_GESTURE_RIGHT                KEY_RIGHT
#define KEY_GESTURE_O                    KEY_O
#define KEY_GESTURE_E                    KEY_E
#define KEY_GESTURE_M                    KEY_M
#define KEY_GESTURE_W                    KEY_W
#define KEY_GESTURE_S                    KEY_S
#define KEY_GESTURE_V                    KEY_V
#define KEY_GESTURE_C                    KEY_C
#define KEY_GESTURE_Z                    KEY_Z


/* V3.2 */
#define P3_2_GET_TP_INFORMATION        0x20
#define P3_2_GET_KEY_INFORMATION    0x22
#define P3_2_GET_FIRMWARE_VERSION    0x40
#define P3_2_GET_PROTOCOL_VERSION    0x42

/* V5.X */
#define P5_0_READ_DATA_CTRL                0xF6
#define P5_0_GET_TP_INFORMATION            0x20
#define P5_0_GET_KEY_INFORMATION        0x27
#define P5_0_GET_FIRMWARE_VERSION        0x21
#define P5_0_GET_PROTOCOL_VERSION        0x22
#define P5_0_GET_CORE_VERSION            0x23
#define P5_0_MODE_CONTROL                0xF0
#define P5_0_SET_CDC_INIT               0xF1
#define P5_0_GET_CDC_DATA               0xF2
#define P5_0_CDC_BUSY_STATE                0xF3
#define P5_0_I2C_UART                    0x40

#define P5_0_FIRMWARE_UNKNOWN_MODE        0xFF
#define P5_0_FIRMWARE_DEMO_MODE            0x00
#define P5_0_FIRMWARE_TEST_MODE            0x01
#define P5_0_FIRMWARE_DEBUG_MODE        0x02
#define P5_0_FIRMWARE_I2CUART_MODE        0x03
#define P5_0_FIRMWARE_GESTURE_MODE        0x04

#define P5_0_DEMO_PACKET_ID                0x5A
#define P5_0_DEBUG_PACKET_ID            0xA7
#define P5_0_TEST_PACKET_ID                0xF2
#define P5_0_GESTURE_PACKET_ID            0xAA
#define P5_0_I2CUART_PACKET_ID            0x7A


#define GESTURE_NORMAL_MODE          0
#define GESTURE_INFO_MPDE            1

#define P5_0_DEMO_MODE_PACKET_LENGTH    43
#define GESTURE_MORMAL_LENGTH        8

#define P5_0_DEBUG_MODE_PACKET_LENGTH    1280
#define P5_0_TEST_MODE_PACKET_LENGTH    1180

struct protocol_cmd_list {
    /* version of protocol */
    uint8_t major;
    uint8_t mid;
    uint8_t minor;

    /* Length of command */
    int fw_ver_len;
    int pro_ver_len;
    int tp_info_len;
    int key_info_len;
    int core_ver_len;
    int func_ctrl_len;
    int window_len;
    int cdc_len;
    int cdc_raw_len;

    /* TP information */
    uint8_t cmd_read_ctrl;
    uint8_t cmd_get_tp_info;
    uint8_t cmd_get_key_info;
    uint8_t cmd_get_fw_ver;
    uint8_t cmd_get_pro_ver;
    uint8_t cmd_get_core_ver;
    uint8_t cmd_mode_ctrl;
    uint8_t cmd_i2cuart;
    uint8_t cmd_cdc_busy;

    /* Function control */
    uint8_t sense_ctrl[3];
    uint8_t sleep_ctrl[3];
    uint8_t glove_ctrl[3];
    uint8_t stylus_ctrl[3];
    uint8_t tp_scan_mode[3];
    uint8_t lpwg_ctrl[3];
    uint8_t gesture_ctrl[3];
    uint8_t phone_cover_ctrl[3];
    uint8_t finger_sense_ctrl[3];
    uint8_t proximity_ctrl[3];
    uint8_t plug_ctrl[3];
    uint8_t edge_limit_ctrl[3];
    uint8_t lock_point_ctrl[3];
    uint8_t phone_cover_window[9];

    /* firmware mode */
    uint8_t unknown_mode;
    uint8_t demo_mode;
    uint8_t debug_mode;
    uint8_t test_mode;
    uint8_t i2cuart_mode;
    uint8_t gesture_mode;

    /* Pakcet ID reported by FW */
    uint8_t demo_pid;
    uint8_t debug_pid;
    uint8_t i2cuart_pid;
    uint8_t test_pid;
    uint8_t ges_pid;

    /* Length of finger report */
    int demo_len;
    int debug_len;
    int test_len;
    int gesture_len;

    /* MP Test with cdc commands */
    uint8_t cmd_cdc;
    uint8_t cmd_get_cdc;

    uint8_t cm_data;
    uint8_t cs_data;

    uint8_t rx_short;
    uint8_t rx_open;
    uint8_t tx_short;

    uint8_t mutual_dac;
    uint8_t mutual_bg;
    uint8_t mutual_signal;
    uint8_t mutual_no_bk;
    uint8_t mutual_bk_dac;
    uint8_t mutual_has_bk;
    uint16_t mutual_has_bk_16;

    uint8_t self_dac;
    uint8_t self_bk_dac;
    uint8_t self_has_bk;
    uint8_t self_no_bk;
    uint8_t self_signal;
    uint8_t self_bg;

    uint8_t key_dac;
    uint8_t key_has_bk;
    uint8_t key_bg;
    uint8_t key_no_bk;
    uint8_t key_open;
    uint8_t key_short;

    uint8_t st_no_bk;
    uint8_t st_open;
    uint8_t st_dac;
    uint8_t st_has_bk;
    uint8_t st_bg;

    uint8_t tx_rx_delta;

    uint8_t trcrq_pin;
    uint8_t resx2_pin;
    uint8_t mutual_integra_time;
    uint8_t self_integra_time;
    uint8_t key_integra_time;
    uint8_t st_integra_time;
    uint8_t peak_to_peak;
    uint8_t get_timing;
    uint8_t doze_p2p;
    uint8_t doze_raw;
};

struct flash_table {
    uint16_t mid;
    uint16_t dev_id;
    int mem_size;
    int program_page;
    int sector;
    int block;
};

typedef struct {
    int nId;
    int nX;
    int nY;
    int nStatus;
    int nFlag;
} VIRTUAL_KEYS;

typedef struct {
    uint16_t nMaxX;
    uint16_t nMaxY;
    uint16_t nMinX;
    uint16_t nMinY;

    uint8_t nMaxTouchNum;
    uint8_t nMaxKeyButtonNum;

    uint8_t nXChannelNum;
    uint8_t nYChannelNum;
    uint8_t nHandleKeyFlag;
    uint8_t nKeyCount;

    uint16_t nKeyAreaXLength;
    uint16_t nKeyAreaYLength;

    VIRTUAL_KEYS virtual_key[10];

    /* added for protocol v5 */
    uint8_t self_tx_channel_num;
    uint8_t self_rx_channel_num;
    uint8_t side_touch_type;

} TP_INFO;

typedef enum {
    SLEEP_IN_GESTURE_PS,
    SLEEP_IN_DEEP,
    SLEEP_IN_BEGIN_FTM,
    SLEEP_IN_END_FTM,
    NOT_SLEEP_MODE,
}SLEEP_TYPE;

struct core_config_data {
    uint32_t chip_id;
    uint32_t chip_type;
    uint32_t chip_pid;
    uint8_t core_type;
    uint32_t slave_i2c_addr;
    uint32_t ice_mode_addr;
    uint32_t pid_addr;
    uint32_t wdt_addr;
    uint32_t ic_reset_addr;

    uint8_t protocol_ver[4];
    uint8_t firmware_ver[9];
    uint8_t core_ver[5];

    bool do_ic_reset;
    SLEEP_TYPE ili_sleep_type;

    bool isEnableGesture;
    bool icemodeenable;
    bool spi_pro_9881ab;
    TP_INFO *tp_info;
};

struct core_gesture_data {
    uint32_t start_addr;
    uint32_t length;
    bool entry;
    uint8_t mode; //normal:0 info:1
    uint32_t ap_start_addr;
    uint32_t ap_length;
    uint32_t area_section;
    bool suspend;
};

extern struct protocol_cmd_list *protocol;

extern void core_protocol_func_control(int key, int ctrl);
extern int core_protocol_update_ver(uint8_t major, uint8_t mid, uint8_t minor);
extern int core_protocol_init(void);
extern void core_protocol_remove(void);
extern int core_write(uint8_t, uint8_t *, uint16_t);
extern int core_read(uint8_t, uint8_t *, uint16_t);

extern struct flash_table *flashtab;

extern int core_flash_poll_busy(void);
extern int core_flash_write_enable(void);
extern void core_flash_enable_protect(bool status);
extern void core_flash_init(uint16_t mid, uint16_t did);
extern void core_flash_remove(void);


extern struct core_config_data *core_config;
extern struct core_gesture_data *core_gesture;

extern int fw_cmd_len;
extern int protocol_cmd_len;
extern int tp_info_len;
extern int key_info_len;
extern int core_cmd_len;

/* R/W with Touch ICs */
extern uint32_t core_config_ice_mode_read(uint32_t addr);
extern int core_config_ice_mode_write(uint32_t addr, uint32_t data, uint32_t size);
extern uint32_t core_config_read_write_onebyte(uint32_t addr);
extern int core_config_ice_mode_disable(void);
extern int core_config_ice_mode_enable(void);

/* Touch IC status */
extern int core_config_set_watch_dog(bool enable);
extern int core_config_check_cdc_busy(int delay);
extern int core_config_check_int_status(void *chip_data, bool high);

extern int core_config_get_project_id(uint8_t *pid_data);

extern void core_config_ic_suspend(void);
extern void core_config_ic_suspend_ftm(void);

extern void core_config_ic_resume(void);
extern void core_config_ic_reset(void);
extern void core_config_mode_control(uint8_t *from_user);
extern int core_config_mp_move_code(void);

/* control features of Touch IC */
extern void core_config_sense_ctrl(bool start);
extern void core_config_sleep_ctrl(bool out);
extern void core_config_glove_ctrl(bool enable, bool seamless);
extern void core_config_stylus_ctrl(bool enable, bool seamless);
extern void core_config_tp_scan_mode(bool mode);
extern void core_config_lpwg_ctrl(bool enable);
extern void core_config_gesture_ctrl(uint8_t func);
extern void core_config_phone_cover_ctrl(bool enable);
extern void core_config_finger_sense_ctrl(bool enable);
extern void core_config_proximity_ctrl(bool enable);
extern void core_config_plug_ctrl(bool out);
extern void core_config_edge_limit_ctrl(void *chip_data, bool enable);
extern void core_config_lock_point_ctrl(bool enable);
extern void core_config_set_phone_cover(uint8_t *pattern);
extern void core_config_headset_ctrl(bool enable);

/* Touch IC information */
extern int core_config_get_core_ver(void);
extern int core_config_get_key_info(void);
extern int core_config_get_tp_info(void);
extern int core_config_get_protocol_ver(void);
extern int core_config_get_fw_ver(void *chip_data);
extern int core_config_get_chip_id(void);
extern uint32_t core_config_get_reg_data(uint32_t addr);
extern void core_config_wr_pack( int packet);
extern uint32_t core_config_rd_pack( int packet);
extern void core_get_ddi_register(void);
extern void core_get_tp_register(void);
extern void core_get_ddi_register_onlyone(uint8_t page, uint8_t reg);
extern void core_set_ddi_register_onlyone(uint8_t page, uint8_t reg, uint8_t data);

extern int core_config_init(void);
extern void core_config_remove(void);

#endif
