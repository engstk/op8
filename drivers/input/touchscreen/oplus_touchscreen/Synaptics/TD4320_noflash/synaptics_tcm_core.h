/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _SYNAPTICS_TCM_CORE_H_
#define _SYNAPTICS_TCM_CORE_H_

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/delay.h>
//#include <linux/wakelock.h>
#include <linux/platform_device.h>
#include <linux/input/synaptics_tcm.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include <linux/spi/spi.h>

#include "../../touchpanel_common.h"
#include "../synaptics_common.h"


#define TPD_DEVICE "td4320_nf"
#define UPDATE_DISPLAY_CONFIG

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


#define SYNAPTICS_TCM_ID_PRODUCT (1 << 0)
#define SYNAPTICS_TCM_ID_VERSION 0x0007

#define RD_CHUNK_SIZE 0 /* read length limit in bytes, 0 = unlimited */
#define WR_CHUNK_SIZE 1024 /* write length limit in bytes, 0 = unlimited */

#define MESSAGE_HEADER_SIZE 4
#define MESSAGE_MARKER 0xa5
#define MESSAGE_PADDING 0x5a

#define REPORT_TIMEOUT_MS 1000

#define INIT_BUFFER(buffer, is_clone) \
    mutex_init(&buffer.buf_mutex); \
    buffer.clone = is_clone

#define LOCK_BUFFER(buffer) \
    mutex_lock(&buffer.buf_mutex)

#define UNLOCK_BUFFER(buffer) \
    mutex_unlock(&buffer.buf_mutex)

#define RELEASE_BUFFER(buffer) \
    do { \
        if (buffer.clone == false) { \
            kfree(buffer.buf); \
            buffer.buf_size = 0; \
            buffer.data_length = 0; \
        } \
    } while (0)

#define MAX(a, b) \
    ({__typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b; })

#define MIN(a, b) \
    ({__typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b; })

#define STR(x) #x

#define CONCAT(a, b) a##b

#define TOUCH_REPORT_CONFIG_SIZE 128

#define DTAP_DETECT     0x01
#define CIRCLE_DETECT   0x02
#define SWIPE_DETECT    0x04
#define UNICODE_DETECT  0x08
#define VEE_DETECT      0x10
#define TRIANGLE_DETECT 0x20

enum test_code {
    TEST_TRX_TRX_SHORTS = 0,
    TEST_TRX_SENSOR_OPENS = 1,
    TEST_TRX_GROUND_SHORTS = 2,
    TEST_DYNAMIC_RANGE = 7,
    TEST_OPEN_SHORT_DETECTOR = 8,
    TEST_NOISE = 10,
    TEST_PT11 = 11,
    TEST_PT12 = 12,
    TEST_PT13 = 13,
    TEST_DYNAMIC_RANGE_DOZE = 14,
    TEST_NOISE_DOZE = 15,
};

enum touch_status {
    LIFT = 0,
    FINGER = 1,
    GLOVED_FINGER = 2,
    NOP = -1,
};

enum touch_report_code {
    TOUCH_END = 0,
    TOUCH_FOREACH_ACTIVE_OBJECT,
    TOUCH_FOREACH_OBJECT,
    TOUCH_FOREACH_END,
    TOUCH_PAD_TO_NEXT_BYTE,
    TOUCH_TIMESTAMP,
    TOUCH_OBJECT_N_INDEX,
    TOUCH_OBJECT_N_CLASSIFICATION,
    TOUCH_OBJECT_N_X_POSITION,
    TOUCH_OBJECT_N_Y_POSITION,
    TOUCH_OBJECT_N_Z,
    TOUCH_OBJECT_N_X_WIDTH,
    TOUCH_OBJECT_N_Y_WIDTH,
    TOUCH_OBJECT_N_TX_POSITION_TIXELS,
    TOUCH_OBJECT_N_RX_POSITION_TIXELS,
    TOUCH_0D_BUTTONS_STATE,
    TOUCH_GESTURE_DOUBLE_TAP,
    TOUCH_FRAME_RATE,
    TOUCH_POWER_IM,
    TOUCH_CID_IM,
    TOUCH_RAIL_IM,
    TOUCH_CID_VARIANCE_IM,
    TOUCH_NSM_FREQUENCY,
    TOUCH_NSM_STATE,
    TOUCH_NUM_OF_ACTIVE_OBJECTS,
    TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME,
    TOUCH_TUNING_GAUSSIAN_WIDTHS = 0x80,
    TOUCH_TUNING_SMALL_OBJECT_PARAMS,
    TOUCH_TUNING_0D_BUTTONS_VARIANCE,
    TOUCH_REPORT_GESTURE_SWIPE = 193,
    TOUCH_REPORT_GESTURE_CIRCLE = 194,
    TOUCH_REPORT_GESTURE_UNICODE = 195,
    TOUCH_REPORT_GESTURE_VEE = 196,
    TOUCH_REPORT_GESTURE_TRIANGLE = 197,
    TOUCH_REPORT_GESTURE_INFO = 198,
    TOUCH_REPORT_GESTURE_COORDINATE = 199,
};

enum module_type {
    TCM_TOUCH = 0,
    TCM_DEVICE = 1,
    TCM_TESTING = 2,
    TCM_REFLASH = 3,
    TCM_RECOVERY = 4,
    TCM_ZEROFLASH = 5,
    TCM_DIAGNOSTICS = 6,
    TCM_LAST,
};

enum boot_mode {
    MODE_APPLICATION = 0x01,
    MODE_HOST_DOWNLOAD = 0x02,
    MODE_BOOTLOADER = 0x0b,
    MODE_TDDI_BOOTLOADER = 0x0c,
};

enum boot_status {
    BOOT_STATUS_OK = 0x00,
    BOOT_STATUS_BOOTING = 0x01,
    BOOT_STATUS_APP_BAD_DISPLAY_CRC = 0xfc,
    BOOT_STATUS_BAD_DISPLAY_CONFIG = 0xfd,
    BOOT_STATUS_BAD_APP_FIRMWARE = 0xfe,
    BOOT_STATUS_WARM_BOOT = 0xff,
};

enum app_status {
    APP_STATUS_OK = 0x00,
    APP_STATUS_BOOTING = 0x01,
    APP_STATUS_UPDATING = 0x02,
    APP_STATUS_BAD_APP_CONFIG = 0xff,
};

enum firmware_mode {
    FW_MODE_BOOTLOADER = 0,
    FW_MODE_APPLICATION = 1,
};

enum dynamic_config_id {
    DC_UNKNOWN = 0x00,
    DC_NO_DOZE,
    DC_DISABLE_NOISE_MITIGATION,
    DC_INHIBIT_FREQUENCY_SHIFT,
    DC_REQUESTED_FREQUENCY,
    DC_DISABLE_HSYNC,
    DC_REZERO_ON_EXIT_DEEP_SLEEP,
    DC_CHARGER_CONNECTED,
    DC_NO_BASELINE_RELAXATION,
    DC_IN_WAKEUP_GESTURE_MODE,
    DC_STIMULUS_FINGERS,
    DC_GRIP_SUPPRESSION_ENABLED,
    DC_ENABLE_THICK_GLOVE,
    DC_ENABLE_GLOVE,
    DC_PS_STATUS = 0xC1,
    DC_DISABLE_ESD = 0xC2,
    DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL = 0xC3,
    DC_DARKZONE_ENABLE = 0xC4,
    DC_GAME_MODE_ENABLED = 0xCB,
    DC_GRIP_SUPPRESSION_ENABLED_NEW = 0xCD,
    DC_GRIP_DARKZONE_X = 0xCE,
    DC_GRIP_DARKZONE_Y = 0xCF,
    DC_HEADSET_MODE_ENABLED = 0xD1,
    DC_FREQUENCE_HOPPING = 0xD2,
};

enum command {
    CMD_NONE = 0x00,
    CMD_CONTINUE_WRITE = 0x01,
    CMD_IDENTIFY = 0x02,
    CMD_RESET = 0x04,
    CMD_ENABLE_REPORT = 0x05,
    CMD_DISABLE_REPORT = 0x06,
    CMD_GET_BOOT_INFO = 0x10,
    CMD_ERASE_FLASH = 0x11,
    CMD_WRITE_FLASH = 0x12,
    CMD_READ_FLASH = 0x13,
    CMD_RUN_APPLICATION_FIRMWARE = 0x14,
    CMD_SPI_MASTER_WRITE_THEN_READ = 0x15,
    CMD_REBOOT_TO_ROM_BOOTLOADER = 0x16,
    CMD_RUN_BOOTLOADER_FIRMWARE = 0x1f,
    CMD_GET_APPLICATION_INFO = 0x20,
    CMD_GET_STATIC_CONFIG = 0x21,
    CMD_SET_STATIC_CONFIG = 0x22,
    CMD_GET_DYNAMIC_CONFIG = 0x23,
    CMD_SET_DYNAMIC_CONFIG = 0x24,
    CMD_GET_TOUCH_REPORT_CONFIG = 0x25,
    CMD_SET_TOUCH_REPORT_CONFIG = 0x26,
    CMD_REZERO = 0x27,
    CMD_COMMIT_CONFIG = 0x28,
    CMD_DESCRIBE_DYNAMIC_CONFIG = 0x29,
    CMD_PRODUCTION_TEST = 0x2a,
    CMD_SET_CONFIG_ID = 0x2b,
    CMD_ENTER_DEEP_SLEEP = 0x2c,
    CMD_EXIT_DEEP_SLEEP = 0x2d,
    CMD_GET_TOUCH_INFO = 0x2e,
    CMD_GET_DATA_LOCATION = 0x2f,
    CMD_DOWNLOAD_CONFIG = 0x30,
    CMD_GET_NSM_INFO = 0xc3,
    CMD_EXIT_ESD = 0xc4,
};

enum status_code {
    STATUS_IDLE = 0x00,
    STATUS_OK = 0x01,
    STATUS_BUSY = 0x02,
    STATUS_CONTINUED_READ = 0x03,
    STATUS_RECEIVE_BUFFER_OVERFLOW = 0x0c,
    STATUS_PREVIOUS_COMMAND_PENDING = 0x0d,
    STATUS_NOT_IMPLEMENTED = 0x0e,
    STATUS_ERROR = 0x0f,
    STATUS_INVALID = 0xff,
};

enum report_type {
    REPORT_IDENTIFY = 0x10,
    REPORT_TOUCH = 0x11,
    REPORT_DELTA = 0x12,
    REPORT_RAW = 0x13,
    REPORT_HDL_STATUS = 0x1b,
    REPORT_PRINTF = 0x82,
    REPORT_STATUS = 0xc0,
    REPORT_DEBUG = 0x14,
    REPORT_HDL = 0xfe,
};

enum command_status {
    CMD_IDLE = 0,
    CMD_BUSY = 1,
    CMD_ERROR = -1,
};

enum flash_area {
    BOOTLOADER = 0,
    BOOT_CONFIG,
    APP_FIRMWARE,
    APP_CONFIG,
    DISP_CONFIG,
    CUSTOM_OTP,
    CUSTOM_LCM,
    CUSTOM_OEM,
    PPDT,
};

enum flash_data {
    LCM_DATA = 1,
    OEM_DATA,
    PPDT_DATA,
};

struct syna_tcm_buffer {
    bool clone;
    unsigned char *buf;
    unsigned int buf_size;
    unsigned int data_length;
    struct mutex buf_mutex;
};

struct syna_tcm_report {
    unsigned char id;
    struct syna_tcm_buffer buffer;
};

struct syna_tcm_identification {
    unsigned char version;
    unsigned char mode;
    unsigned char part_number[16];
    unsigned char build_id[4];
    unsigned char max_write_size[2];
};

struct syna_tcm_boot_info {
    unsigned char version;
    unsigned char status;
    unsigned char asic_id[2];
    unsigned char write_block_size_words;
    unsigned char erase_page_size_words[2];
    unsigned char max_write_payload_size[2];
    unsigned char last_reset_reason;
    unsigned char pc_at_time_of_last_reset[2];
    unsigned char boot_config_start_block[2];
    unsigned char boot_config_size_blocks[2];
    unsigned char display_config_start_block[4];
    unsigned char display_config_length_blocks[2];
    unsigned char backup_display_config_start_block[4];
    unsigned char backup_display_config_length_blocks[2];
    unsigned char custom_otp_start_block[2];
    unsigned char custom_otp_length_blocks[2];
};

struct syna_tcm_app_info {
    unsigned char version[2];
    unsigned char status[2];
    unsigned char static_config_size[2];
    unsigned char dynamic_config_size[2];
    unsigned char app_config_start_write_block[2];
    unsigned char app_config_size[2];
    unsigned char max_touch_report_config_size[2];
    unsigned char max_touch_report_payload_size[2];
    unsigned char customer_config_id[16];
    unsigned char max_x[2];
    unsigned char max_y[2];
    unsigned char max_objects[2];
    unsigned char num_of_buttons[2];
    unsigned char num_of_image_rows[2];
    unsigned char num_of_image_cols[2];
    unsigned char has_hybrid_data[2];
};

struct syna_tcm_touch_info {
    unsigned char image_2d_scale_factor[4];
    unsigned char image_0d_scale_factor[4];
    unsigned char hybrid_x_scale_factor[4];
    unsigned char hybrid_y_scale_factor[4];
};

struct syna_tcm_message_header {
    unsigned char marker;
    unsigned char code;
    unsigned char length[2];
};

struct input_params {
    unsigned int max_x;
    unsigned int max_y;
    unsigned int max_objects;
};

struct object_data {
    unsigned char status;
    unsigned int x_pos;
    unsigned int y_pos;
    unsigned int x_width;
    unsigned int y_width;
    unsigned int z;
    unsigned int tx_pos;
    unsigned int rx_pos;
};

struct touch_data {
    struct object_data *object_data;
    unsigned char data_point[24]; //6 points
    unsigned int extra_gesture_info;
    unsigned int timestamp;
    unsigned int buttons_state;
    unsigned int gesture_double_tap;
    unsigned int lpwg_gesture;
    unsigned int frame_rate;
    unsigned int power_im;
    unsigned int cid_im;
    unsigned int rail_im;
    unsigned int cid_variance_im;
    unsigned int nsm_frequency;
    unsigned int nsm_state;
    unsigned int num_of_active_objects;
    unsigned int num_of_cpu_cycles;
};

struct touch_hcd {
    bool report_touch;
    unsigned int max_objects;
    struct mutex report_mutex;
    struct touch_data touch_data;
    struct syna_tcm_buffer out;
    struct syna_tcm_buffer resp;
};

struct reflash_hcd {
    bool disp_cfg_update;
    unsigned int image_size;
    unsigned int page_size;
    unsigned int write_block_size;
    unsigned int max_write_payload_size;
};

struct syna_tcm_test {
    unsigned int num_of_reports;
    unsigned char report_type;
    unsigned int report_index;
    struct syna_tcm_buffer report;
};

struct syna_tcm_hcd {
    /*must be first*/
    struct invoke_method cb;
    struct spi_device *s_client;
    struct hw_resource *hw_res;
    struct touch_hcd *touch_hcd;
    struct syna_tcm_test *test_hcd;
    struct synaptics_proc_operations *syna_ops;
    struct firmware_headfile *p_firmware_headfile;

    struct workqueue_struct *helper_workqueue;
    struct work_struct helper_work;

    atomic_t command_status;
    char *iHex_name;
    char *limit_name;
    char *fw_name;
    int *in_suspend;

    unsigned short ubl_addr;
    unsigned char trigger_reason;
    unsigned char command;
    unsigned char async_report_id;
    unsigned char status_report_code;
    unsigned int read_length;
    unsigned int payload_length;
    unsigned int rd_chunk_size;
    unsigned int wr_chunk_size;
    unsigned int app_status;
    unsigned int max_touch_num;
    unsigned int hdl_finished_flag;
    uint8_t  touch_direction;

    bool first_tp_fw_update_done;
    bool using_fae;

    struct completion config_complete;
    struct mutex reset_mutex;
    struct mutex rw_ctrl_mutex;
    struct mutex command_mutex;
    struct mutex identify_mutex;
    struct mutex io_ctrl_mutex;
    struct syna_tcm_buffer in;
    struct syna_tcm_buffer out;
    struct syna_tcm_buffer resp;
    struct syna_tcm_buffer temp;
    struct syna_tcm_buffer config;
    struct syna_tcm_buffer default_config;
    struct syna_tcm_report report;
    struct syna_tcm_app_info app_info;
    struct syna_tcm_boot_info boot_info;
    struct syna_tcm_touch_info touch_info;
    struct syna_tcm_identification id_info;
    struct zeroflash_hcd *zeroflash_hcd;
    struct monitor_data *monitor_data;
    unsigned char response_code;
    bool init_okay;
    atomic_t host_downloading;
    int (*write_message)(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *payload,
        unsigned int length, unsigned char **resp_buf,
        unsigned int *resp_buf_size, unsigned int *resp_length,
        unsigned char *response_code, unsigned int polling_delay_ms);
    bool reset_watchdog_running;
    struct hrtimer watchdog;
    struct work_struct timeout_work;

    unsigned int grip_darkzone_x;
    unsigned int grip_darkzone_y;

    bool irq_trigger_hdl_support;
    bool health_monitor_support;
};

struct device_hcd {
    dev_t dev_num;
    bool raw_mode;
    bool concurrent;
    unsigned int ref_count;
    int irq;
    int flag;
    struct cdev char_dev;
    struct class *class;
    struct device *device;
    struct mutex extif_mutex;
    struct syna_tcm_buffer out;
    struct syna_tcm_buffer resp;
    struct syna_tcm_buffer report;
    struct syna_tcm_hcd *tcm_hcd;
    int (*reset)(void *chip_data);
    int (*write_message)(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *payload,
        unsigned int length, unsigned char **resp_buf,
        unsigned int *resp_buf_size, unsigned int *resp_length,
        unsigned int polling_delay_ms);
    int (*read_message)(struct syna_tcm_hcd *tcm_hcd, unsigned char *in_buf, unsigned int length);
    int (*report_touch) (struct syna_tcm_hcd *tcm_hcd);
};

/*
struct area_descriptor {
    unsigned char magic_value[4];
    unsigned char id_string[16];
    unsigned char flags[4];
    unsigned char flash_addr_words[4];
    unsigned char length[4];
    unsigned char checksum[4];
};

struct block_data {
    const unsigned char *data;
    unsigned int size;
    unsigned int flash_addr;
};

struct image_info {
    unsigned int packrat_number;
    struct block_data boot_config;
    struct block_data app_firmware;
    struct block_data app_config;
    struct block_data disp_config;
};
*/
struct zeroflash_image_header {
    unsigned char magic_value[4];
    unsigned char num_of_areas[4];
};

struct rmi_f35_query {
    unsigned char version:4;
    unsigned char has_debug_mode:1;
    unsigned char has_data5:1;
    unsigned char has_query1:1;
    unsigned char has_query2:1;
    unsigned char chunk_size;
    unsigned char has_ctrl7:1;
    unsigned char has_host_download:1;
    unsigned char has_spi_master:1;
    unsigned char advanced_recovery_mode:1;
    unsigned char reserved:4;
} __packed;

struct rmi_f35_data {
    unsigned char error_code:5;
    unsigned char recovery_mode_forced:1;
    unsigned char nvm_programmed:1;
    unsigned char in_recovery:1;
} __packed;

struct rmi_pdt_entry {
    unsigned char query_base_addr;
    unsigned char command_base_addr;
    unsigned char control_base_addr;
    unsigned char data_base_addr;
    unsigned char intr_src_count:3;
    unsigned char reserved_1:2;
    unsigned char fn_version:2;
    unsigned char reserved_2:1;
    unsigned char fn_number;
} __packed;

struct rmi_addr {
    unsigned short query_base;
    unsigned short command_base;
    unsigned short control_base;
    unsigned short data_base;
};

struct firmware_status {
    unsigned short invalid_static_config:1;
    unsigned short need_disp_config:1;
    unsigned short need_app_config:1;
    unsigned short reserved:13;
} __packed;


struct zeroflash_hcd {
    bool has_hdl;
    bool f35_ready;
    const unsigned char *image;
    unsigned char *buf;
    char *fw_name;
    const struct firmware *fw_entry;
    struct work_struct config_work;
    struct work_struct firmware_work;
    struct workqueue_struct *config_workqueue;
    struct workqueue_struct *firmware_workqueue;
    struct rmi_addr f35_addr;
    struct image_info image_info;
    struct firmware_status fw_status;
    struct syna_tcm_buffer out;
    struct syna_tcm_buffer resp;
    struct syna_tcm_hcd *tcm_hcd;
    bool init_done;
};

static inline int secure_memcpy(unsigned char *dest, unsigned int dest_size,
        const unsigned char *src, unsigned int src_size,
        unsigned int count)
{
    if (dest == NULL || src == NULL)
        return -EINVAL;

    if (count > dest_size || count > src_size) {
        pr_err("%s: src_size = %d, dest_size = %d, count = %d\n",
                __func__, src_size, dest_size, count);
        return -EINVAL;
    }

    memcpy((void *)dest, (const void *)src, count);

    return 0;
}

static inline int syna_tcm_realloc_mem(struct syna_tcm_hcd *tcm_hcd,
        struct syna_tcm_buffer *buffer, unsigned int size)
{
    int retval;
    unsigned char *temp;

    if (size > buffer->buf_size) {
        temp = buffer->buf;

        buffer->buf = kmalloc(size, GFP_KERNEL);
        if (!(buffer->buf)) {
            TPD_INFO("%s: Failed to allocate memory\n",
                    __func__);
            kfree(temp);
            buffer->buf_size = 0;
            return -ENOMEM;
        }

        retval = secure_memcpy(buffer->buf,
                size,
                temp,
                buffer->buf_size,
                buffer->buf_size);
        if (retval < 0) {
            TPD_INFO("%s: Failed to copy data\n", __func__);
            kfree(temp);
            kfree(buffer->buf);
            buffer->buf_size = 0;
            return retval;
        }

        kfree(temp);
        buffer->buf_size = size;
    }

    return 0;
}

static inline int syna_tcm_alloc_mem(struct syna_tcm_hcd *tcm_hcd,
        struct syna_tcm_buffer *buffer, unsigned int size)
{
    if (size > buffer->buf_size) {
        kfree(buffer->buf);
        buffer->buf = kmalloc(size, GFP_KERNEL);
        if (!(buffer->buf)) {
            TPD_INFO("%s: Failed to allocate memory, size %d\n",__func__, size);
            buffer->buf_size = 0;
            buffer->data_length = 0;
            return -ENOMEM;
        }
        buffer->buf_size = size;
    }

    memset(buffer->buf, 0, buffer->buf_size);
    buffer->data_length = 0;

    return 0;
}


static inline unsigned int ceil_div(unsigned int dividend, unsigned divisor)
{
    return (dividend + divisor - 1) / divisor;
}

int syna_tcm_td4320_nf_rmi_read(struct syna_tcm_hcd *tcm_hcd,
        unsigned short addr, unsigned char *data, unsigned int length);

int syna_tcm_td4320_nf_rmi_write(struct syna_tcm_hcd *tcm_hcd,
        unsigned short addr, unsigned char *data, unsigned int length);

struct zeroflash_hcd * syna_remote_zeroflash_init(struct syna_tcm_hcd *tcm_hcd);
void zeroflash_download_firmware(void);
void zeroflash_download_config(void);
extern int zeroflash_init_done;
int syna_reset_gpio(void *chip_data, bool enable);
void syna_tcm_hdl_done(struct syna_tcm_hcd *tcm_hcd);
int zeroflash_download_firmware_directly(void *chip_data, const struct firmware *fw);
void zeroflash_update_fw_image(void);
int zeroflash_parse_fw_image(void);

void syna_tcm_start_reset_timer(struct syna_tcm_hcd *tcm_hcd);
void syna_tcm_stop_reset_timer(struct syna_tcm_hcd *tcm_hcd);

int syna_tcm_raw_read(struct syna_tcm_hcd *tcm_hcd,
        unsigned char *in_buf, unsigned int length);
#endif
