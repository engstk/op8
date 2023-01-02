// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include "synaptics_tcm_core.h"
#include <linux/hrtimer.h>

#define PREDICTIVE_READING
#define MIN_READ_LENGTH 9
#define RESPONSE_TIMEOUT_MS_SHORT 300
#define RESPONSE_TIMEOUT_MS_DEFAULT 1000
#define RESPONSE_TIMEOUT_MS_LONG 3000

#define ERASE_FLASH_DELAY_MS 5000
#define WRITE_FLASH_DELAY_MS 200

#define APP_STATUS_POLL_TIMEOUT_MS 1000
#define APP_STATUS_POLL_MS 100


static DECLARE_COMPLETION(response_complete);
static DECLARE_COMPLETION(report_complete);

extern struct device_hcd * syna_td4320_nf_remote_device_init(struct syna_tcm_hcd *tcm_hcd);
extern int syna_td4320_nf_remote_device_destory(struct syna_tcm_hcd *tcm_hcd);
extern void wait_zeroflash_firmware_work(void);

static void syna_main_register(struct seq_file *s, void *chip_data);

static int syna_tcm_write_message(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *payload,
        unsigned int length, unsigned char **resp_buf,
        unsigned int *resp_buf_size, unsigned int *resp_length,
        unsigned int polling_delay_ms);
static void syna_tcm_test_report(struct syna_tcm_hcd *tcm_hcd);
static int syna_tcm_helper(struct syna_tcm_hcd *tcm_hcd);

int ubl_byte_delay_us = 20;
int ubl_max_freq = 1000000;
int block_delay_us = 0;
int byte_delay_us = 0;
struct syna_tcm_hcd *g_tcm_hcd = NULL;
int esd_irq_disabled = 0;

static unsigned char *buf;
static unsigned int buf_size;
static struct spi_transfer *xfer;

static int syna_tcm_spi_alloc_mem(struct syna_tcm_hcd *tcm_hcd,
        unsigned int count, unsigned int size)
{
    static unsigned int xfer_count;

    if (count > xfer_count) {
        kfree(xfer);
        xfer = kcalloc(count, sizeof(*xfer), GFP_KERNEL);
        if (!xfer) {
            TPD_INFO("Failed to allocate memory for xfer\n");
            xfer_count = 0;
            return -ENOMEM;
        }
        xfer_count = count;
    } else {
        memset(xfer, 0, count * sizeof(*xfer));
    }

    if (size > buf_size) {
        if (buf_size)
            kfree(buf);
        buf = kmalloc(size, GFP_KERNEL);
        if (!buf) {
            TPD_INFO("Failed to allocate memory for buf\n");
            buf_size = 0;
            return -ENOMEM;
        }
        buf_size = size;
    }

    return 0;
}

#ifdef CONFIG_SPI_MT65XX
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#endif

inline int syna_tcm_td4320_nf_rmi_read(struct syna_tcm_hcd *tcm_hcd,
        unsigned short addr, unsigned char *data, unsigned int length)
{
    int retval;
    unsigned int idx;
    unsigned int mode;
    unsigned int byte_count;
    struct spi_message msg;
    struct spi_device *spi = tcm_hcd->s_client;

    mutex_lock(&tcm_hcd->io_ctrl_mutex);
    spi_message_init(&msg);

    byte_count = length + 2;

    TPD_DEBUG("ENTER syna_tcm_td4320_nf_rmi_read\n");

    if (ubl_byte_delay_us == 0)
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, 2, byte_count);
    else
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, byte_count, 3);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory\n");
        goto exit;
    }

    buf[0] = (unsigned char)(addr >> 8) | 0x80;
    buf[1] = (unsigned char)addr;

    if (ubl_byte_delay_us == 0) {
        xfer[0].len = 2;
        xfer[0].tx_buf = buf;
        xfer[0].speed_hz = ubl_max_freq;
        spi_message_add_tail(&xfer[0], &msg);
        memset(&buf[2], 0xff, length);
        xfer[1].len = length;
        xfer[1].tx_buf = &buf[2];
        xfer[1].rx_buf = data;
        if (block_delay_us)
            xfer[1].delay_usecs = block_delay_us;
        xfer[1].speed_hz = ubl_max_freq;
        spi_message_add_tail(&xfer[1], &msg);
    } else {
        buf[2] = 0xff;
        for (idx = 0; idx < byte_count; idx++) {
            xfer[idx].len = 1;
            if (idx < 2) {
                xfer[idx].tx_buf = &buf[idx];
            } else {
                xfer[idx].tx_buf = &buf[2];
                xfer[idx].rx_buf = &data[idx - 2];
            }
            xfer[idx].delay_usecs = ubl_byte_delay_us;
            if (block_delay_us && (idx == byte_count - 1))
                xfer[idx].delay_usecs = block_delay_us;
            xfer[idx].speed_hz = ubl_max_freq;
            spi_message_add_tail(&xfer[idx], &msg);
        }
    }

    mode = spi->mode;
    spi->mode = SPI_MODE_3;

#ifdef CONFIG_SPI_MT65XX
    mt_spi_enable_master_clk(spi);
#endif
    retval = spi_sync(spi, &msg);
    if (retval == 0) {
        retval = length;
    } else {
        TPD_INFO("Failed to complete SPI transfer, error = %d\n",
                retval);
    }
#ifdef CONFIG_SPI_MT65XX
    mt_spi_disable_master_clk(spi);
#endif

    spi->mode = mode;

exit:
    mutex_unlock(&tcm_hcd->io_ctrl_mutex);
    return retval;
}

inline int syna_tcm_td4320_nf_rmi_write(struct syna_tcm_hcd *tcm_hcd,
        unsigned short addr, unsigned char *data, unsigned int length)
{
    int retval;
    unsigned int mode;
    unsigned int byte_count;
    struct spi_message msg;
    struct spi_device *spi = tcm_hcd->s_client;

    mutex_lock(&tcm_hcd->io_ctrl_mutex);

    spi_message_init(&msg);

    byte_count = length + 2;

    retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, byte_count);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory\n");
        goto exit;
    }

    buf[0] = (unsigned char)(addr >> 8) & ~0x80;
    buf[1] = (unsigned char)addr;
    retval = secure_memcpy(&buf[2],
            buf_size - 2,
            data,
            length,
            length);
    if (retval < 0) {
        TPD_INFO("Failed to copy write data\n");
        goto exit;
    }

    xfer[0].len = byte_count;
    xfer[0].tx_buf = buf;
    if (block_delay_us)
        xfer[0].delay_usecs = block_delay_us;
    spi_message_add_tail(&xfer[0], &msg);

    mode = spi->mode;
    spi->mode = SPI_MODE_3;

#ifdef CONFIG_SPI_MT65XX
    mt_spi_enable_master_clk(spi);
#endif
    retval = spi_sync(spi, &msg);
    if (retval == 0) {
        retval = length;
    } else {
        TPD_INFO("Failed to complete SPI transfer, error = %d\n",
                retval);
    }
#ifdef CONFIG_SPI_MT65XX
    mt_spi_disable_master_clk(spi);
#endif

    spi->mode = mode;

exit:
    mutex_unlock(&tcm_hcd->io_ctrl_mutex);
    return retval;
}

static inline int syna_tcm_read(struct syna_tcm_hcd *tcm_hcd,
        unsigned char *data, unsigned int length)
{
    int retval;
    unsigned int idx;
    struct spi_message msg;
    struct spi_device *spi = tcm_hcd->s_client;

    mutex_lock(&tcm_hcd->io_ctrl_mutex);
    spi_message_init(&msg);

    if (byte_delay_us == 0)
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, length);
    else
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, length, 1);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory\n");
        goto exit;
    }

    if (byte_delay_us == 0) {
        memset(buf, 0xff, length);
        xfer[0].len = length;
        xfer[0].tx_buf = buf;
        xfer[0].rx_buf = data;
        if (block_delay_us)
            xfer[0].delay_usecs = block_delay_us;
        spi_message_add_tail(&xfer[0], &msg);
    } else {
        buf[0] = 0xff;
        for (idx = 0; idx < length; idx++) {
            xfer[idx].len = 1;
            xfer[idx].tx_buf = buf;
            xfer[idx].rx_buf = &data[idx];
            xfer[idx].delay_usecs = byte_delay_us;
            if (block_delay_us && (idx == length - 1))
                xfer[idx].delay_usecs = block_delay_us;
            spi_message_add_tail(&xfer[idx], &msg);
        }
    }

#ifdef CONFIG_SPI_MT65XX
    mt_spi_enable_master_clk(spi);
#endif
    retval = spi_sync(spi, &msg);
    if (retval == 0) {
        retval = length;
    } else {
        TPD_INFO("Failed to complete SPI transfer, error = %d\n",
                retval);
    }
#ifdef CONFIG_SPI_MT65XX
    mt_spi_disable_master_clk(spi);
#endif

exit:
    mutex_unlock(&tcm_hcd->io_ctrl_mutex);
    return retval;
}

static inline int syna_tcm_write(struct syna_tcm_hcd *tcm_hcd,
        unsigned char *data, unsigned int length)
{
    int retval;
    unsigned int idx;
    struct spi_message msg;
    struct spi_device *spi = tcm_hcd->s_client;

    mutex_lock(&tcm_hcd->io_ctrl_mutex);

    spi_message_init(&msg);

    if (byte_delay_us == 0)
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, 0);
    else
        retval = syna_tcm_spi_alloc_mem(tcm_hcd, length, 0);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory\n");
        goto exit;
    }

    if (byte_delay_us == 0) {
        xfer[0].len = length;
        xfer[0].tx_buf = data;
        if (block_delay_us)
            xfer[0].delay_usecs = block_delay_us;
        spi_message_add_tail(&xfer[0], &msg);
    } else {
        for (idx = 0; idx < length; idx++) {
            xfer[idx].len = 1;
            xfer[idx].tx_buf = &data[idx];
            xfer[idx].delay_usecs = byte_delay_us;
            if (block_delay_us && (idx == length - 1))
                xfer[idx].delay_usecs = block_delay_us;
            spi_message_add_tail(&xfer[idx], &msg);
        }
    }
#ifdef CONFIG_SPI_MT65XX
    mt_spi_enable_master_clk(spi);
#endif
    retval = spi_sync(spi, &msg);
    if (retval == 0) {
        retval = length;
    } else {
        TPD_INFO("Failed to complete SPI transfer, error = %d\n",
                retval);
    }
#ifdef CONFIG_SPI_MT65XX
    mt_spi_disable_master_clk(spi);
#endif

exit:
    mutex_unlock(&tcm_hcd->io_ctrl_mutex);

    return retval;
}


int zeroflash_check_uboot(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char fn_number;
    TPD_INFO("ENTER zeroflash_check_uboot\n");
    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            0x00ee,
            &fn_number,
            sizeof(fn_number));
    if (retval < 0) {
        TPD_INFO("Failed to read RMI function number\n");
        return retval;
    }

    TPD_DETAIL("Found F$%02x\n",
            fn_number);

    if (fn_number != 0x35) {
        TPD_INFO("Failed to find F$35, but F$%02x\n", fn_number);
        return -ENODEV;
    }
    return 0;
}

static int syna_get_report_data(struct syna_tcm_hcd *tcm_hcd, unsigned int offset,
        unsigned int bits, unsigned int *data)
{
    unsigned char mask;
    unsigned char byte_data;
    unsigned int output_data;
    unsigned int bit_offset;
    unsigned int byte_offset;
    unsigned int data_bits;
    unsigned int available_bits;
    unsigned int remaining_bits;
    unsigned char *touch_report;

    touch_report = tcm_hcd->report.buffer.buf;
    output_data = 0;
    remaining_bits = bits;
    bit_offset = offset % 8;
    byte_offset = offset / 8;

    if (bits == 0 || bits > 32) {
        TPD_DEBUG("larger than 32 bits:%d\n", bits);
        secure_memcpy((unsigned char *)data, bits / 8, &touch_report[byte_offset], bits / 8, bits / 8);
        return 0;
    }

    if (offset + bits > tcm_hcd->report.buffer.data_length * 8) {
        *data = 0;
        return 0;
    }

    while (remaining_bits) {
        byte_data = touch_report[byte_offset];
        byte_data >>= bit_offset;

        available_bits = 8 - bit_offset;
        data_bits = MIN(available_bits, remaining_bits);
        mask = 0xff >> (8 - data_bits);

        byte_data &= mask;

        output_data |= byte_data << (bits - remaining_bits);

        bit_offset = 0;
        byte_offset += 1;
        remaining_bits -= data_bits;
    }

    *data = output_data;

    return 0;
}

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int syna_parse_report(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    bool active_only = 0;
    bool num_of_active_objects;
    unsigned char code;
    unsigned int size, idx, obj;
    unsigned int next, data, bits, offset, objects;
    unsigned int active_objects = 0;
    unsigned int report_size, config_size;
    unsigned char *config_data;
    struct touch_hcd *touch_hcd;
    struct touch_data *touch_data;
    struct object_data *object_data;
    static unsigned int end_of_foreach;

    touch_hcd = tcm_hcd->touch_hcd;
    touch_data = &touch_hcd->touch_data;
    object_data = touch_hcd->touch_data.object_data;
    config_data = tcm_hcd->config.buf;
    config_size = tcm_hcd->config.data_length;
    report_size = tcm_hcd->report.buffer.data_length;
    size = sizeof(*object_data) * touch_hcd->max_objects;
    memset(touch_hcd->touch_data.object_data, 0x00, size);

    num_of_active_objects = false;

    idx = 0;
    offset = 0;
    objects = 0;
    while (idx < config_size) {
        code = config_data[idx++];
        switch (code) {
        case TOUCH_END:
            goto exit;
        case TOUCH_FOREACH_ACTIVE_OBJECT:
            obj = 0;
            next = idx;
            active_only = true;
            break;
        case TOUCH_FOREACH_OBJECT:
            obj = 0;
            next = idx;
            active_only = false;
            break;
        case TOUCH_FOREACH_END:
            end_of_foreach = idx;
            if (active_only) {
                if (num_of_active_objects) {
                    objects++;
                    if (objects < active_objects)
                        idx = next;
                } else if (offset < report_size * 8) {
                    idx = next;
                }
            } else {
                obj++;
                if (obj < touch_hcd->max_objects)
                    idx = next;
            }
            break;
        case TOUCH_PAD_TO_NEXT_BYTE:
            offset = ceil_div(offset, 8) * 8;
            break;
        case TOUCH_TIMESTAMP:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get timestamp\n");
                return retval;
            }
            touch_data->timestamp = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_INDEX:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &obj);
            if (retval < 0) {
                TPD_INFO("Failed to get object index\n");
                return retval;
            }
            offset += bits;
            break;
        case TOUCH_OBJECT_N_CLASSIFICATION:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object classification\n");
                return retval;
            }
            object_data[obj].status = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_X_POSITION:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object x position\n");
                return retval;
            }
            object_data[obj].x_pos = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_Y_POSITION:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object y position\n");
                return retval;
            }
            object_data[obj].y_pos = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_Z:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object z\n");
                return retval;
            }
            object_data[obj].z = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_X_WIDTH:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object x width\n");
                return retval;
            }
            object_data[obj].x_width = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_Y_WIDTH:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object y width\n");
                return retval;
            }
            object_data[obj].y_width = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object tx position\n");
                return retval;
            }
            object_data[obj].tx_pos = data;
            offset += bits;
            break;
        case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get object rx position\n");
                return retval;
            }
            object_data[obj].rx_pos = data;
            offset += bits;
            break;
        case TOUCH_0D_BUTTONS_STATE:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get 0D buttons state\n");
                return retval;
            }
            touch_data->buttons_state = data;
            offset += bits;
            break;
        case TOUCH_GESTURE_DOUBLE_TAP:
        case TOUCH_REPORT_GESTURE_SWIPE:
        case TOUCH_REPORT_GESTURE_CIRCLE:
        case TOUCH_REPORT_GESTURE_UNICODE:
        case TOUCH_REPORT_GESTURE_VEE:
        case TOUCH_REPORT_GESTURE_TRIANGLE:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get gesture double tap\n");
                return retval;
            }
            touch_data->lpwg_gesture = tcm_hcd->report.buffer.buf[0];
            offset += bits;
            break;
        case TOUCH_REPORT_GESTURE_INFO:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get gesture double tap\n");
                return retval;
            }
            touch_data->extra_gesture_info = data;
            offset += bits;
            break;
        case TOUCH_REPORT_GESTURE_COORDINATE:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, (unsigned int *)(&touch_data->data_point[0]));
            if (retval < 0) {
                TPD_INFO("Failed to get gesture double tap\n");
                return retval;
            }
            offset += bits;
            break;
        case TOUCH_FRAME_RATE:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get frame rate\n");
                return retval;
            }
            touch_data->frame_rate = data;
            offset += bits;
            break;
        case TOUCH_POWER_IM:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get power IM\n");
                return retval;
            }
            touch_data->power_im = data;
            offset += bits;
            break;
        case TOUCH_CID_IM:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get CID IM\n");
                return retval;
            }
            touch_data->cid_im = data;
            offset += bits;
            break;
        case TOUCH_RAIL_IM:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get rail IM\n");
                return retval;
            }
            touch_data->rail_im = data;
            offset += bits;
            break;
        case TOUCH_CID_VARIANCE_IM:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get CID variance IM\n");
                return retval;
            }
            touch_data->cid_variance_im = data;
            offset += bits;
            break;
        case TOUCH_NSM_FREQUENCY:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get NSM frequency\n");
                return retval;
            }
            touch_data->nsm_frequency = data;
            offset += bits;
            break;
        case TOUCH_NSM_STATE:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get NSM state\n");
                return retval;
            }
            touch_data->nsm_state = data;
            offset += bits;
            break;
        case TOUCH_NUM_OF_ACTIVE_OBJECTS:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get number of active objects\n");
                return retval;
            }
            active_objects = data;
            num_of_active_objects = true;
            touch_data->num_of_active_objects = data;
            offset += bits;
            if (touch_data->num_of_active_objects == 0)
                idx = end_of_foreach;
            break;
        case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
            bits = config_data[idx++];
            retval = syna_get_report_data(tcm_hcd, offset, bits, &data);
            if (retval < 0) {
                TPD_INFO("Failed to get number of CPU cycles used since last frame\n");
                return retval;
            }
            touch_data->num_of_cpu_cycles = data;
            offset += bits;
            break;
        case TOUCH_TUNING_GAUSSIAN_WIDTHS:
            bits = config_data[idx++];
            offset += bits;
            break;
        case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
            bits = config_data[idx++];
            offset += bits;
            break;
        case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
            bits = config_data[idx++];
            offset += bits;
            break;
        }
    }

exit:
    return 0;
}

static int syna_get_input_params(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;

    LOCK_BUFFER(tcm_hcd->config);


    TPD_DETAIL("syna_get_input_params\n");

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_GET_TOUCH_REPORT_CONFIG,
            NULL,
            0,
            &tcm_hcd->config.buf,
            &tcm_hcd->config.buf_size,
            &tcm_hcd->config.data_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
        UNLOCK_BUFFER(tcm_hcd->config);
        return retval;
    }
    TPD_DETAIL("syna_get_input_params end\n");

    UNLOCK_BUFFER(tcm_hcd->config);

    return 0;
}

static int syna_set_default_report_config(struct syna_tcm_hcd *tcm_hcd)
{
    int retval = 0;
    int length = 0;

    LOCK_BUFFER(tcm_hcd->config);

    length = tcm_hcd->default_config.buf_size;

    if (tcm_hcd->default_config.buf) {
        retval = syna_tcm_alloc_mem(tcm_hcd,
                    &tcm_hcd->config,
                    length);
        if (retval < 0) {
            TPD_INFO("Failed to alloc mem\n");
            goto exit;
        }

        memcpy(tcm_hcd->config.buf, tcm_hcd->default_config.buf, length);
        tcm_hcd->config.buf_size = tcm_hcd->default_config.buf_size;
        tcm_hcd->config.data_length = tcm_hcd->default_config.data_length;
    }

exit:
    UNLOCK_BUFFER(tcm_hcd->config);

    return retval;
}

static int syna_get_default_report_config(struct syna_tcm_hcd *tcm_hcd)
{
    int retval = 0;
    unsigned int length;

    length = le2_to_uint(tcm_hcd->app_info.max_touch_report_config_size);

    LOCK_BUFFER(tcm_hcd->default_config);

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_GET_TOUCH_REPORT_CONFIG,
            NULL,
            0,
            &tcm_hcd->default_config.buf,
            &tcm_hcd->default_config.buf_size,
            &tcm_hcd->default_config.data_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
        goto exit;
    }

exit:
    UNLOCK_BUFFER(tcm_hcd->default_config);
    return retval;
}

static int syna_set_normal_report_config(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned int idx = 0;
    unsigned int length;
    struct touch_hcd *touch_hcd = tcm_hcd->touch_hcd;

    TPD_INFO("%s:set normal report\n", __func__);
    length = le2_to_uint(tcm_hcd->app_info.max_touch_report_config_size);

    if (length < TOUCH_REPORT_CONFIG_SIZE) {
        TPD_INFO("Invalid maximum touch report config size\n");
        return -EINVAL;
    }

    LOCK_BUFFER(touch_hcd->out);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &touch_hcd->out,
            length);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
        UNLOCK_BUFFER(touch_hcd->out);
        return retval;
    }

    //touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
    //touch_hcd->out.buf[idx++] = 8;
    touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
    touch_hcd->out.buf[idx++] = 4;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
    touch_hcd->out.buf[idx++] = 4;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
    touch_hcd->out.buf[idx++] = 12;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
    touch_hcd->out.buf[idx++] = 12;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_WIDTH;
    touch_hcd->out.buf[idx++] = 8;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_WIDTH;
    touch_hcd->out.buf[idx++] = 8;
    touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
    touch_hcd->out.buf[idx++] = TOUCH_END;

    LOCK_BUFFER(touch_hcd->resp);

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_SET_TOUCH_REPORT_CONFIG,
            touch_hcd->out.buf,
            length,
            &touch_hcd->resp.buf,
            &touch_hcd->resp.buf_size,
            &touch_hcd->resp.data_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
        UNLOCK_BUFFER(touch_hcd->resp);
        UNLOCK_BUFFER(touch_hcd->out);
        return retval;
    }

    UNLOCK_BUFFER(touch_hcd->resp);
    UNLOCK_BUFFER(touch_hcd->out);

    return retval;
}

static int syna_set_gesture_report_config(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned int idx = 0;
    unsigned int length;
    struct touch_hcd *touch_hcd = tcm_hcd->touch_hcd;

    TPD_DEBUG("%s: set gesture report\n", __func__);
    length = le2_to_uint(tcm_hcd->app_info.max_touch_report_config_size);

    if (length < TOUCH_REPORT_CONFIG_SIZE) {
        TPD_INFO("Invalid maximum touch report config size\n");
        return -EINVAL;
    }

    LOCK_BUFFER(touch_hcd->out);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &touch_hcd->out,
            length);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
        UNLOCK_BUFFER(touch_hcd->out);
        return retval;
    }

    touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_CIRCLE;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_SWIPE;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_UNICODE;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_VEE;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_TRIANGLE;
    touch_hcd->out.buf[idx++] = 1;
    touch_hcd->out.buf[idx++] = TOUCH_PAD_TO_NEXT_BYTE;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
    touch_hcd->out.buf[idx++] = 16;
    touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_COORDINATE;
    touch_hcd->out.buf[idx++] = 192;
    touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
    touch_hcd->out.buf[idx++] = 4;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
    touch_hcd->out.buf[idx++] = 4;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
    touch_hcd->out.buf[idx++] = 12;
    touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
    touch_hcd->out.buf[idx++] = 12;
    touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
    touch_hcd->out.buf[idx++] = TOUCH_END;

    LOCK_BUFFER(touch_hcd->resp);

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_SET_TOUCH_REPORT_CONFIG,
            touch_hcd->out.buf,
            length,
            &touch_hcd->resp.buf,
            &touch_hcd->resp.buf_size,
            &touch_hcd->resp.data_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
        UNLOCK_BUFFER(touch_hcd->resp);
        UNLOCK_BUFFER(touch_hcd->out);
        return retval;
    }

    UNLOCK_BUFFER(touch_hcd->resp);
    UNLOCK_BUFFER(touch_hcd->out);

    return 0;
}

static int syna_set_input_reporting(struct syna_tcm_hcd *tcm_hcd, bool suspend)
{
    int retval = 0;
    struct touch_hcd *touch_hcd = tcm_hcd->touch_hcd;

    TPD_DETAIL("%s: mode 0x%x, state %d\n", __func__, tcm_hcd->id_info.mode, suspend);
    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        TPD_INFO("Application firmware not running\n");
        return 0;
    }

    touch_hcd->report_touch = false;

    mutex_lock(&touch_hcd->report_mutex);

    if (!suspend) {
        retval = syna_set_normal_report_config(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to set report config\n");
            goto default_config;
        }
    } else {
        retval = syna_set_gesture_report_config(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to set report config\n");
            goto default_config;
        }
    }

    retval = syna_get_input_params(tcm_hcd);
    if (retval < 0) {
        TPD_INFO("Failed to get input parameters\n");
    }

    goto exit;

default_config:
    /*if failed to set report config, use default report config */
    retval = syna_set_default_report_config(tcm_hcd);
    if (retval < 0) {
        TPD_INFO("Failed to set default report config");
    }

exit:
    mutex_unlock(&touch_hcd->report_mutex);

    touch_hcd->report_touch = retval < 0 ? false : true;

    return retval;
}

static void syna_set_trigger_reason(struct syna_tcm_hcd *tcm_hcd, irq_reason trigger_reason)
{
    SET_BIT(tcm_hcd->trigger_reason, trigger_reason);
    if (tcm_hcd->cb.invoke_common){
        tcm_hcd->cb.invoke_common();
    }

    tcm_hcd->trigger_reason = 0;
}

static void syna_tcm_resize_chunk_size(struct syna_tcm_hcd *tcm_hcd)
{
    unsigned int max_write_size;

    max_write_size = le2_to_uint(tcm_hcd->id_info.max_write_size);
    tcm_hcd->wr_chunk_size = MIN(max_write_size, WR_CHUNK_SIZE);
    if (tcm_hcd->wr_chunk_size == 0)
        tcm_hcd->wr_chunk_size = max_write_size;

}

/**
 * syna_tcm_dispatch_report() - dispatch report received from device
 *
 * @tcm_hcd: handle of core module
 *
 * The report generated by the device is forwarded to the synchronous inbox of
 * each registered application module for further processing. In addition, the
 * report notifier thread is woken up for asynchronous notification of the
 * report occurrence.
 */
static void syna_tcm_dispatch_report(struct syna_tcm_hcd *tcm_hcd)
{
    int ret = 0;
    LOCK_BUFFER(tcm_hcd->in);
    LOCK_BUFFER(tcm_hcd->report.buffer);

    tcm_hcd->report.buffer.buf = &tcm_hcd->in.buf[MESSAGE_HEADER_SIZE];
    tcm_hcd->report.buffer.buf_size = tcm_hcd->in.buf_size;
    tcm_hcd->report.buffer.buf_size -= MESSAGE_HEADER_SIZE;
    tcm_hcd->report.buffer.data_length = tcm_hcd->payload_length;
    tcm_hcd->report.id = tcm_hcd->status_report_code;

    if (tcm_hcd->report.id == REPORT_TOUCH) {
        ret = syna_parse_report(tcm_hcd);
        if (ret < 0) {
            TPD_INFO("Failed to parse report\n");
            goto exit;
        }
        

        if (*tcm_hcd->in_suspend) {
            syna_set_trigger_reason(tcm_hcd, IRQ_GESTURE);
            
            
        } else {
            syna_set_trigger_reason(tcm_hcd, IRQ_TOUCH);
            
        }
    } else if (tcm_hcd->report.id == REPORT_IDENTIFY) {
        if (tcm_hcd->cb.async_work && tcm_hcd->id_info.mode == MODE_HOST_DOWNLOAD){
            tcm_hcd->cb.async_work();
        }
    } else if (tcm_hcd->report.id == REPORT_HDL_STATUS) {
           //secure_memcpy((unsigned char * )dest,unsigned int dest_size,const unsigned char * src,unsigned int src_size,unsigned int count)
           zeroflash_download_config();
    } else {
        syna_tcm_test_report(tcm_hcd);
    }

exit:
    UNLOCK_BUFFER(tcm_hcd->report.buffer);
    UNLOCK_BUFFER(tcm_hcd->in);
    return;
}


/**
 * syna_tcm_dispatch_response() - dispatch response received from device
 *
 * @tcm_hcd: handle of core module
 *
 * The response to a command is forwarded to the sender of the command.
 */
static void syna_tcm_dispatch_response(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;

    if (atomic_read(&tcm_hcd->command_status) != CMD_BUSY)
        return;

    tcm_hcd->response_code = tcm_hcd->status_report_code;
    LOCK_BUFFER(tcm_hcd->resp);

    if (tcm_hcd->payload_length == 0) {
        UNLOCK_BUFFER(tcm_hcd->resp);
        atomic_set(&tcm_hcd->command_status, CMD_IDLE);
        goto exit;
    }

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &tcm_hcd->resp,
            tcm_hcd->payload_length);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory for tcm_hcd->resp.buf\n");
        UNLOCK_BUFFER(tcm_hcd->resp);
        atomic_set(&tcm_hcd->command_status, CMD_ERROR);
        goto exit;
    }

    LOCK_BUFFER(tcm_hcd->in);

    retval = secure_memcpy(tcm_hcd->resp.buf,
            tcm_hcd->resp.buf_size,
            &tcm_hcd->in.buf[MESSAGE_HEADER_SIZE],
            tcm_hcd->in.buf_size - MESSAGE_HEADER_SIZE,
            tcm_hcd->payload_length);
    if (retval < 0) {
        TPD_INFO("Failed to copy payload\n");
        UNLOCK_BUFFER(tcm_hcd->in);
        UNLOCK_BUFFER(tcm_hcd->resp);
        atomic_set(&tcm_hcd->command_status, CMD_ERROR);
        goto exit;
    }

    tcm_hcd->resp.data_length = tcm_hcd->payload_length;

    UNLOCK_BUFFER(tcm_hcd->in);
    UNLOCK_BUFFER(tcm_hcd->resp);

    atomic_set(&tcm_hcd->command_status, CMD_IDLE);

exit:
    complete(&response_complete);

    return;
}

/**
 * syna_tcm_dispatch_message() - dispatch message received from device
 *
 * @tcm_hcd: handle of core module
 *
 * The information received in the message read in from the device is dispatched
 * to the appropriate destination based on whether the information represents a
 * report or a response to a command.
 */
static void syna_tcm_dispatch_message(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned int payload_length;
    

    if (tcm_hcd->status_report_code == REPORT_IDENTIFY) {
        payload_length = tcm_hcd->payload_length;

        LOCK_BUFFER(tcm_hcd->in);

        retval = secure_memcpy((unsigned char *)&tcm_hcd->id_info,
                sizeof(tcm_hcd->id_info),
                &tcm_hcd->in.buf[MESSAGE_HEADER_SIZE],
                tcm_hcd->in.buf_size - MESSAGE_HEADER_SIZE,
                MIN(sizeof(tcm_hcd->id_info), payload_length));
        if (retval < 0) {
            TPD_INFO("Failed to copy identification info\n");
            UNLOCK_BUFFER(tcm_hcd->in);
            return;
        }

        UNLOCK_BUFFER(tcm_hcd->in);

        syna_tcm_resize_chunk_size(tcm_hcd);

        TPD_DETAIL("Received identify report (firmware mode = 0x%02x)\n",
                tcm_hcd->id_info.mode);

        if (atomic_read(&tcm_hcd->command_status) == CMD_BUSY) {
            switch (tcm_hcd->command) {
            case CMD_RESET:
            case CMD_RUN_BOOTLOADER_FIRMWARE:
            case CMD_RUN_APPLICATION_FIRMWARE:
                atomic_set(&tcm_hcd->command_status, CMD_IDLE);
                complete(&response_complete);
                break;
            default:
                TPD_INFO("Device has been reset\n");
                atomic_set(&tcm_hcd->command_status, CMD_ERROR);
                complete(&response_complete);
                break;
            }
        }

        if (tcm_hcd->id_info.mode == MODE_HOST_DOWNLOAD) {
            return;
        }


        syna_tcm_helper(tcm_hcd);

    }

    if (tcm_hcd->status_report_code >= REPORT_IDENTIFY)
        syna_tcm_dispatch_report(tcm_hcd);
    else
        syna_tcm_dispatch_response(tcm_hcd);

    return;
}

/**
 * syna_tcm_continued_read() - retrieve entire payload from device
 *
 * @tcm_hcd: handle of core module
 *
 * Read transactions are carried out until the entire payload is retrieved from
 * the device and stored in the handle of the core module.
 */
static int syna_tcm_continued_read(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char marker;
    unsigned char code;
    unsigned int idx;
    unsigned int offset;
    unsigned int chunks;
    unsigned int chunk_space;
    unsigned int xfer_length;
    unsigned int total_length;
    unsigned int remaining_length;

    total_length = MESSAGE_HEADER_SIZE + tcm_hcd->payload_length + 1;

    remaining_length = total_length - tcm_hcd->read_length;

    LOCK_BUFFER(tcm_hcd->in);

    retval = syna_tcm_realloc_mem(tcm_hcd,
            &tcm_hcd->in,
            total_length);
    if (retval < 0) {
        TPD_INFO("Failed to reallocate memory for tcm_hcd->in.buf\n");
        UNLOCK_BUFFER(tcm_hcd->in);
        return retval;
    }

    /* available chunk space for payload = total chunk size minus header
     * marker byte and header code byte */
    if (tcm_hcd->rd_chunk_size == 0)
        chunk_space = remaining_length;
    else
        chunk_space = tcm_hcd->rd_chunk_size - 2;

    chunks = ceil_div(remaining_length, chunk_space);

    chunks = chunks == 0 ? 1 : chunks;

    offset = tcm_hcd->read_length;

    LOCK_BUFFER(tcm_hcd->temp);

    for (idx = 0; idx < chunks; idx++) {
        if (remaining_length > chunk_space)
            xfer_length = chunk_space;
        else
            xfer_length = remaining_length;

        if (xfer_length == 1) {
            tcm_hcd->in.buf[offset] = MESSAGE_PADDING;
            offset += xfer_length;
            remaining_length -= xfer_length;
            continue;
        }

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &tcm_hcd->temp,
                xfer_length + 2);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory for tcm_hcd->temp.buf\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            UNLOCK_BUFFER(tcm_hcd->in);
            return retval;
        }

        retval = syna_tcm_read(tcm_hcd,
                tcm_hcd->temp.buf,
                xfer_length + 2);
        if (retval < 0) {
            TPD_INFO("Failed to read from device\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            UNLOCK_BUFFER(tcm_hcd->in);
            return retval;
        }

        marker = tcm_hcd->temp.buf[0];
        code = tcm_hcd->temp.buf[1];

        if (marker != MESSAGE_MARKER) {
            TPD_INFO("Incorrect header marker (0x%02x)\n",
                    marker);
            UNLOCK_BUFFER(tcm_hcd->temp);
            UNLOCK_BUFFER(tcm_hcd->in);
            return -EIO;
        }

        if (code != STATUS_CONTINUED_READ) {
            TPD_INFO("Incorrect header code (0x%02x)\n",
                    code);
            UNLOCK_BUFFER(tcm_hcd->temp);
            UNLOCK_BUFFER(tcm_hcd->in);
            return -EIO;
        }

        retval = secure_memcpy(&tcm_hcd->in.buf[offset],
                total_length - offset,
                &tcm_hcd->temp.buf[2],
                xfer_length,
                xfer_length);
        if (retval < 0) {
            TPD_INFO("Failed to copy payload\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            UNLOCK_BUFFER(tcm_hcd->in);
            return retval;
        }

        offset += xfer_length;

        remaining_length -= xfer_length;
    }

    UNLOCK_BUFFER(tcm_hcd->temp);
    UNLOCK_BUFFER(tcm_hcd->in);

    return 0;
}

/**
 * syna_tcm_raw_read() - retrieve specific number of data bytes from device
 *
 * @tcm_hcd: handle of core module
 * @in_buf: buffer for storing data retrieved from device
 * @length: number of bytes to retrieve from device
 *
 * Read transactions are carried out until the specific number of data bytes are
 * retrieved from the device and stored in in_buf.
 */
int syna_tcm_raw_read(struct syna_tcm_hcd *tcm_hcd,
        unsigned char *in_buf, unsigned int length)
{
    int retval;
    unsigned char code;
    unsigned int idx;
    unsigned int offset;
    unsigned int chunks;
    unsigned int chunk_space;
    unsigned int xfer_length;
    unsigned int remaining_length;

    if (length < 2) {
        TPD_INFO("Invalid length information\n");
        return -EINVAL;
    }

    /* minus header marker byte and header code byte */
    remaining_length = length - 2;

    /* available chunk space for data = total chunk size minus header marker
     * byte and header code byte */
    if (tcm_hcd->rd_chunk_size == 0)
        chunk_space = remaining_length;
    else
        chunk_space = tcm_hcd->rd_chunk_size - 2;

    chunks = ceil_div(remaining_length, chunk_space);

    chunks = chunks == 0 ? 1 : chunks;

    offset = 0;

    LOCK_BUFFER(tcm_hcd->temp);

    for (idx = 0; idx < chunks; idx++) {
        if (remaining_length > chunk_space)
            xfer_length = chunk_space;
        else
            xfer_length = remaining_length;

        if (xfer_length == 1) {
            in_buf[offset] = MESSAGE_PADDING;
            offset += xfer_length;
            remaining_length -= xfer_length;
            continue;
        }

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &tcm_hcd->temp,
                xfer_length + 2);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory for tcm_hcd->temp.buf\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            return retval;
        }

        retval = syna_tcm_read(tcm_hcd,
                tcm_hcd->temp.buf,
                xfer_length + 2);
        if (retval < 0) {
            TPD_INFO("Failed to read from device\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            return retval;
        }

        code = tcm_hcd->temp.buf[1];

        if (idx == 0) {
            retval = secure_memcpy(&in_buf[0],
                    length,
                    &tcm_hcd->temp.buf[0],
                    xfer_length + 2,
                    xfer_length + 2);
        } else {
            if (code != STATUS_CONTINUED_READ) {
                TPD_INFO("Incorrect header code (0x%02x)\n",
                        code);
                UNLOCK_BUFFER(tcm_hcd->temp);
                return -EIO;
            }

            retval = secure_memcpy(&in_buf[offset],
                    length - offset,
                    &tcm_hcd->temp.buf[2],
                    xfer_length,
                    xfer_length);
        }
        if (retval < 0) {
            TPD_INFO("Failed to copy data\n");
            UNLOCK_BUFFER(tcm_hcd->temp);
            return retval;
        }

        if (idx == 0)
            offset += (xfer_length + 2);
        else
            offset += xfer_length;

        remaining_length -= xfer_length;
    }

    UNLOCK_BUFFER(tcm_hcd->temp);

    return 0;
}

/**
 * syna_tcm_raw_write() - write command/data to device without receiving
 * response
 *
 * @tcm_hcd: handle of core module
 * @command: command to send to device
 * @data: data to send to device
 * @length: length of data in bytes
 *
 * A command and its data, if any, are sent to the device.
 */
static int syna_tcm_raw_write(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *data, unsigned int length)
{
    int retval;
    unsigned int idx;
    unsigned int chunks;
    unsigned int chunk_space;
    unsigned int xfer_length;
    unsigned int remaining_length;

    remaining_length = length;

    /* available chunk space for data = total chunk size minus command
     * byte */
    if (tcm_hcd->wr_chunk_size == 0)
        chunk_space = remaining_length;
    else
        chunk_space = tcm_hcd->wr_chunk_size - 1;

    chunks = ceil_div(remaining_length, chunk_space);

    chunks = chunks == 0 ? 1 : chunks;

    LOCK_BUFFER(tcm_hcd->out);

    for (idx = 0; idx < chunks; idx++) {
        if (remaining_length > chunk_space)
            xfer_length = chunk_space;
        else
            xfer_length = remaining_length;

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &tcm_hcd->out,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory for tcm_hcd->out.buf\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            return retval;
        }

        if (idx == 0)
            tcm_hcd->out.buf[0] = command;
        else
            tcm_hcd->out.buf[0] = CMD_CONTINUE_WRITE;

        if (xfer_length) {
            retval = secure_memcpy(&tcm_hcd->out.buf[1],
                    xfer_length,
                    &data[idx * chunk_space],
                    remaining_length,
                    xfer_length);
            if (retval < 0) {
                TPD_INFO("Failed to copy data\n");
                UNLOCK_BUFFER(tcm_hcd->out);
                return retval;
            }
        }

        retval = syna_tcm_write(tcm_hcd,
                tcm_hcd->out.buf,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to write to device\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            return retval;
        }

        remaining_length -= xfer_length;
    }

    UNLOCK_BUFFER(tcm_hcd->out);

    return 0;
}

/*add this for debug. remove before pvt*/
static void syna_tcm_debug_message(char *buf, int len)
{
    int i = 0;
    TPD_INFO("message hex:");
    for (i = 0; i < len; i++) {
        if (i > 32)
            break;

        printk("0x%x ", buf[i]);
    }

    printk("\n");
}

/**
 * syna_tcm_read_message() - read message from device
 *
 * @tcm_hcd: handle of core module
 * @in_buf: buffer for storing data in raw read mode
 * @length: length of data in bytes in raw read mode
 *
 * If in_buf is not NULL, raw read mode is used and syna_tcm_raw_read() is
 * called. Otherwise, a message including its entire payload is retrieved from
 * the device and dispatched to the appropriate destination.
 */
 void syna_log_data(unsigned char *data, int length)
{
    int i;
    TPD_DEBUG("syna data begin\n");
    for (i = 0; i < length; i++) {
        TPD_DEBUG("syna data[%d]:%x, ", i, data[i]);
    }
    TPD_DEBUG("syna data end\n");
}
static int syna_tcm_read_message(struct syna_tcm_hcd *tcm_hcd,
        unsigned char *in_buf, unsigned int length)
{
    int retval;
    bool retry;
    unsigned int total_length;
    struct syna_tcm_message_header *header;

    TPD_DEBUG("%s\n", __func__);
    mutex_lock(&tcm_hcd->rw_ctrl_mutex);

    if (in_buf != NULL) {
        retval = syna_tcm_raw_read(tcm_hcd, in_buf, length);
        goto exit;
    }

    retry = true;
retry:
    LOCK_BUFFER(tcm_hcd->in);

    retval = syna_tcm_read(tcm_hcd,
            tcm_hcd->in.buf,
            tcm_hcd->read_length);
    if (retval < 0) {
        TPD_INFO("Failed to read from device\n");
        UNLOCK_BUFFER(tcm_hcd->in);
        if(retry){
            usleep_range(5000, 10000);
            retry = false;
            goto retry;
        }
        
        goto exit;
    }

    header = (struct syna_tcm_message_header *)tcm_hcd->in.buf;

    if (header->marker != MESSAGE_MARKER) {
        TPD_INFO("header->marker = %02x\n",header->marker);
        UNLOCK_BUFFER(tcm_hcd->in);
        retval = -ENXIO;
        if(retry){
            usleep_range(5000, 10000);
            retry = false;
            goto retry;
        }
        goto exit;
    }

    tcm_hcd->status_report_code = header->code;
    tcm_hcd->payload_length = le2_to_uint(header->length);

    TPD_DEBUG("Header code = 0x%02x Payload len = %d\n",
            tcm_hcd->status_report_code, tcm_hcd->payload_length);

    if (tcm_hcd->status_report_code <= STATUS_ERROR ||
            tcm_hcd->status_report_code == STATUS_INVALID) {
        switch (tcm_hcd->status_report_code) {
        case STATUS_OK:
            break;
        case STATUS_CONTINUED_READ:
            TPD_INFO("Out-of-sync continued read\n");
        case STATUS_IDLE:
        case STATUS_BUSY:
            tcm_hcd->payload_length = 0;
            UNLOCK_BUFFER(tcm_hcd->in);
            retval = 0;
            goto exit;
        default:
            TPD_INFO("Incorrect header code (0x%02x)\n",
                    tcm_hcd->status_report_code);
            if (tcm_hcd->status_report_code != STATUS_ERROR) {
                UNLOCK_BUFFER(tcm_hcd->in);
                retval = -EIO;
                goto exit;
            }
        }
    }

    total_length = MESSAGE_HEADER_SIZE + tcm_hcd->payload_length + 1;

#ifdef PREDICTIVE_READING
    if (total_length <= tcm_hcd->read_length) {
        goto check_padding;
    } else if (total_length - 1 == tcm_hcd->read_length) {
        tcm_hcd->in.buf[total_length - 1] = MESSAGE_PADDING;
        goto check_padding;
    }
#else
    if (tcm_hcd->payload_length == 0) {
        tcm_hcd->in.buf[total_length - 1] = MESSAGE_PADDING;
        goto check_padding;
    }
#endif

    UNLOCK_BUFFER(tcm_hcd->in);

    retval = syna_tcm_continued_read(tcm_hcd);
    if (retval < 0) {
        TPD_INFO("Failed to do continued read\n");
        goto exit;
    };

    LOCK_BUFFER(tcm_hcd->in);

    tcm_hcd->in.buf[0] = MESSAGE_MARKER;
    tcm_hcd->in.buf[1] = tcm_hcd->status_report_code;
    tcm_hcd->in.buf[2] = (unsigned char)tcm_hcd->payload_length;
    tcm_hcd->in.buf[3] = (unsigned char)(tcm_hcd->payload_length >> 8);

check_padding:
    if (tcm_hcd->in.buf[total_length - 1] != MESSAGE_PADDING) {
        TPD_INFO("Incorrect message padding byte (0x%02x)\n",
                tcm_hcd->in.buf[total_length - 1]);
        UNLOCK_BUFFER(tcm_hcd->in);
        retval = -EIO;
        goto exit;
    }

    UNLOCK_BUFFER(tcm_hcd->in);

#ifdef PREDICTIVE_READING
    total_length = MAX(total_length, MIN_READ_LENGTH);
    tcm_hcd->read_length = MIN(total_length, tcm_hcd->rd_chunk_size);
    if (tcm_hcd->rd_chunk_size == 0)
        tcm_hcd->read_length = total_length;
#endif

    /*add for debug, remove before pvt*/
    if (LEVEL_DEBUG == tp_debug) {
        syna_tcm_debug_message(&tcm_hcd->in.buf[4], tcm_hcd->payload_length);
    }
    syna_log_data(&tcm_hcd->in.buf[0],tcm_hcd->payload_length + 4);
    

    syna_tcm_dispatch_message(tcm_hcd);    

    retval = 0;

exit:
    if (retval < 0) {
        if (atomic_read(&tcm_hcd->command_status) == CMD_BUSY) {
            atomic_set(&tcm_hcd->command_status, CMD_ERROR);
            complete(&response_complete);
        }
    }

    mutex_unlock(&tcm_hcd->rw_ctrl_mutex);

    return retval;
}

/**
 * syna_tcm_write_message() - write message to device and receive response
 *
 * @tcm_hcd: handle of core module
 * @command: command to send to device
 * @payload: payload of command
 * @length: length of payload in bytes
 * @resp_buf: buffer for storing command response
 * @resp_buf_size: size of response buffer in bytes
 * @resp_length: length of command response in bytes
 * @polling_delay_ms: delay time after sending command before resuming polling
 *
 * If resp_buf is NULL, raw write mode is used and syna_tcm_raw_write() is
 * called. Otherwise, a command and its payload, if any, are sent to the device
 * and the response to the command generated by the device is read in.
 */
static int syna_tcm_write_message(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *payload,
        unsigned int length, unsigned char **resp_buf,
        unsigned int *resp_buf_size, unsigned int *resp_length,
        unsigned int timeout)
{
    int retval;
    unsigned int idx;
    unsigned int chunks;
    unsigned int chunk_space;
    unsigned int xfer_length;
    unsigned int remaining_length;
    unsigned int command_status = 0;
    unsigned int timeout_ms = 0;

    mutex_lock(&tcm_hcd->command_mutex);

    if (!tcm_hcd->init_okay) {
        TPD_INFO("%s:Command = 0x%02x NOT RUN: init nok\n", __func__, command);
        retval = -EIO;
        goto exit;
    }

    mutex_lock(&tcm_hcd->rw_ctrl_mutex);

    if (resp_buf == NULL) {
        retval = syna_tcm_raw_write(tcm_hcd, command, payload, length);
        mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
        goto exit;
    }

    atomic_set(&tcm_hcd->command_status, CMD_BUSY);
    reinit_completion(&response_complete);
    tcm_hcd->command = command;

    LOCK_BUFFER(tcm_hcd->resp);

    tcm_hcd->resp.buf = *resp_buf;
    tcm_hcd->resp.buf_size = *resp_buf_size;
    tcm_hcd->resp.data_length = 0;

    UNLOCK_BUFFER(tcm_hcd->resp);

    /* adding two length bytes as part of payload */
    remaining_length = length + 2;

    /* available chunk space for payload = total chunk size minus command
     * byte */
    if (tcm_hcd->wr_chunk_size == 0)
        chunk_space = remaining_length;
    else
        chunk_space = tcm_hcd->wr_chunk_size - 1;

    chunks = ceil_div(remaining_length, chunk_space);

    chunks = chunks == 0 ? 1 : chunks;

    TPD_DETAIL("%s:Command = 0x%02x\n", __func__, command);

    LOCK_BUFFER(tcm_hcd->out);

    for (idx = 0; idx < chunks; idx++) {
        if (remaining_length > chunk_space)
            xfer_length = chunk_space;
        else
            xfer_length = remaining_length;

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &tcm_hcd->out,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory for tcm_hcd->out.buf\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
            goto exit;
        }

        if (idx == 0) {
            tcm_hcd->out.buf[0] = command;
            tcm_hcd->out.buf[1] = (unsigned char)length;
            tcm_hcd->out.buf[2] = (unsigned char)(length >> 8);

            if (xfer_length > 2) {
                retval = secure_memcpy(&tcm_hcd->out.buf[3],
                        xfer_length - 2,
                        payload,
                        remaining_length - 2,
                        xfer_length - 2);
                if (retval < 0) {
                    TPD_INFO("Failed to copy payload\n");
                    UNLOCK_BUFFER(tcm_hcd->out);
                    mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
                    goto exit;
                }
            }
        } else {
            tcm_hcd->out.buf[0] = CMD_CONTINUE_WRITE;

            retval = secure_memcpy(&tcm_hcd->out.buf[1],
                    xfer_length,
                    &payload[idx * chunk_space - 2],
                    remaining_length,
                    xfer_length);
            if (retval < 0) {
                TPD_INFO("Failed to copy payload\n");
                UNLOCK_BUFFER(tcm_hcd->out);
                mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
                goto exit;
            }
        }
        //TPD_DETAIL("%s:[%d] buf[0]-0x%02x buf[1]-0x%02x buf[2]-0x%02x buf[3]-0x%02x \n", __func__, idx, tcm_hcd->out.buf[0], tcm_hcd->out.buf[1], tcm_hcd->out.buf[2], tcm_hcd->out.buf[3]);
        retval = syna_tcm_write(tcm_hcd,
                tcm_hcd->out.buf,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to write to device\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
            goto exit;
        }

        remaining_length -= xfer_length;
    }

    UNLOCK_BUFFER(tcm_hcd->out);

    mutex_unlock(&tcm_hcd->rw_ctrl_mutex);

    if (timeout == 0) {
        timeout_ms = RESPONSE_TIMEOUT_MS_DEFAULT;
    } else {
        timeout_ms = timeout;
    }

    retval = wait_for_completion_timeout(&response_complete,
            msecs_to_jiffies(timeout_ms));
    if (retval == 0) {
        TPD_INFO("Timed out waiting for response (command 0x%02x)\n",
                tcm_hcd->command);
        retval = -EIO;
    } else {
        command_status = atomic_read(&tcm_hcd->command_status);

        if (command_status != CMD_IDLE ||
                tcm_hcd->status_report_code == STATUS_ERROR) {
            TPD_INFO("Failed to get valid response\n");
            retval = -EIO;
            goto exit;
        }

        retval = 0;
    }

exit:
    if (command_status == CMD_IDLE) {
        LOCK_BUFFER(tcm_hcd->resp);

        if (tcm_hcd->status_report_code == STATUS_ERROR) {
            if (tcm_hcd->resp.data_length) {
                TPD_INFO("Error code = 0x%02x\n",
                        tcm_hcd->resp.buf[0]);
            }
        }

        if (resp_buf != NULL) {
            *resp_buf = tcm_hcd->resp.buf;
            *resp_buf_size = tcm_hcd->resp.buf_size;
            *resp_length = tcm_hcd->resp.data_length;
        }

        UNLOCK_BUFFER(tcm_hcd->resp);
    }

    tcm_hcd->command = CMD_NONE;
    atomic_set(&tcm_hcd->command_status, CMD_IDLE);
    mutex_unlock(&tcm_hcd->command_mutex);

    return retval;
}
#define RESPONSE_TIMEOUT_MS 3000
#define WRITE_DELAY_US_MIN 100
#define WRITE_DELAY_US_MAX 300


static int syna_tcm_write_message_zeroflash(struct syna_tcm_hcd *tcm_hcd,
        unsigned char command, unsigned char *payload,
        unsigned int length, unsigned char **resp_buf,
        unsigned int *resp_buf_size, unsigned int *resp_length,
        unsigned char *response_code, unsigned int polling_delay_ms)
{
    int retval;
    unsigned int idx;
    unsigned int chunks;
    unsigned int chunk_space;
    unsigned int xfer_length;
    unsigned int remaining_length;
    unsigned int command_status;

    if (response_code != NULL)
        *response_code = STATUS_INVALID;


    mutex_lock(&tcm_hcd->command_mutex);

    if (!tcm_hcd->init_okay) {
        TPD_INFO("%s:Command = 0x%02x NOT RUN: init nok\n", __func__, command);
        retval = -EIO;
        goto exit;
    }

    mutex_lock(&tcm_hcd->rw_ctrl_mutex);

    if (resp_buf == NULL) {
        retval = syna_tcm_raw_write(tcm_hcd, command, payload, length);
        mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
        goto exit;
    }



    atomic_set(&tcm_hcd->command_status, CMD_BUSY);


    reinit_completion(&response_complete);


    tcm_hcd->command = command;

    LOCK_BUFFER(tcm_hcd->resp);

    tcm_hcd->resp.buf = *resp_buf;
    tcm_hcd->resp.buf_size = *resp_buf_size;
    tcm_hcd->resp.data_length = 0;

    UNLOCK_BUFFER(tcm_hcd->resp);

    /* adding two length bytes as part of payload */
    remaining_length = length + 2;

    /* available chunk space for payload = total chunk size minus command
     * byte */
    if (tcm_hcd->wr_chunk_size == 0)
        chunk_space = remaining_length;
    else
        chunk_space = tcm_hcd->wr_chunk_size - 1;

    chunks = ceil_div(remaining_length, chunk_space);

    chunks = chunks == 0 ? 1 : chunks;

    TPD_DETAIL("%s:Command = 0x%02x\n", __func__, command);

    LOCK_BUFFER(tcm_hcd->out);

    for (idx = 0; idx < chunks; idx++) {
        if (remaining_length > chunk_space)
            xfer_length = chunk_space;
        else
            xfer_length = remaining_length;

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &tcm_hcd->out,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory for tcm_hcd->out.buf\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
            goto exit;
        }

        if (idx == 0) {
            tcm_hcd->out.buf[0] = command;
            tcm_hcd->out.buf[1] = (unsigned char)length;
            tcm_hcd->out.buf[2] = (unsigned char)(length >> 8);

            if (xfer_length > 2) {
                retval = secure_memcpy(&tcm_hcd->out.buf[3],
                        tcm_hcd->out.buf_size - 3,
                        payload,
                        remaining_length - 2,
                        xfer_length - 2);
                if (retval < 0) {
                    TPD_INFO("Failed to copy payload\n");
                    UNLOCK_BUFFER(tcm_hcd->out);
                    mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
                    goto exit;
                }
            }
        } else {
            tcm_hcd->out.buf[0] = CMD_CONTINUE_WRITE;

            retval = secure_memcpy(&tcm_hcd->out.buf[1],
                    tcm_hcd->out.buf_size - 1,
                    &payload[idx * chunk_space - 2],
                    remaining_length,
                    xfer_length);
            if (retval < 0) {
                TPD_INFO("Failed to copy payload\n");
                UNLOCK_BUFFER(tcm_hcd->out);
                mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
                goto exit;
            }
        }

        retval = syna_tcm_write(tcm_hcd,
                tcm_hcd->out.buf,
                xfer_length + 1);
        if (retval < 0) {
            TPD_INFO("Failed to write to device\n");
            UNLOCK_BUFFER(tcm_hcd->out);
            mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
            goto exit;
        }

        remaining_length -= xfer_length;

        if (chunks > 1)
            usleep_range(1000, 1000);
    }

    UNLOCK_BUFFER(tcm_hcd->out);

    mutex_unlock(&tcm_hcd->rw_ctrl_mutex);


    if (!esd_irq_disabled) {
        retval = wait_for_completion_timeout(&response_complete,
                msecs_to_jiffies(RESPONSE_TIMEOUT_MS));
    } else {
        retval = 0;
    }
    if (retval == 0) {
        TPD_INFO("Timed out waiting for response (command 0x%02x)\n",
                tcm_hcd->command);
        retval = -EIO;
        goto exit;
    }

    command_status = atomic_read(&tcm_hcd->command_status);
    if (command_status != CMD_IDLE) {
        TPD_INFO("Failed to get valid response (command 0x%02x)\n",
                tcm_hcd->command);
        retval = -EIO;
        goto exit;
    }

    LOCK_BUFFER(tcm_hcd->resp);

    if (tcm_hcd->response_code != STATUS_OK) {
        if (tcm_hcd->resp.data_length) {
            TPD_INFO("Error code = 0x%02x (command 0x%02x)\n",
                    tcm_hcd->resp.buf[0], tcm_hcd->command);
        }
        retval = -EIO;
    } else {
        retval = 0;
    }

    *resp_buf = tcm_hcd->resp.buf;
    *resp_buf_size = tcm_hcd->resp.buf_size;
    *resp_length = tcm_hcd->resp.data_length;

    if (response_code != NULL)
        *response_code = tcm_hcd->response_code;

    UNLOCK_BUFFER(tcm_hcd->resp);

exit:
    tcm_hcd->command = CMD_NONE;

    atomic_set(&tcm_hcd->command_status, CMD_IDLE);

    mutex_unlock(&tcm_hcd->command_mutex);

    return retval;
}

static int syna_tcm_get_app_info(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int timeout;

    timeout = APP_STATUS_POLL_TIMEOUT_MS;

    resp_buf = NULL;
    resp_buf_size = 0;

get_app_info:
    retval = syna_tcm_write_message(tcm_hcd,
            CMD_GET_APPLICATION_INFO,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",
                STR(CMD_GET_APPLICATION_INFO));
        goto exit;
    }

    retval = secure_memcpy((unsigned char *)&tcm_hcd->app_info,
            sizeof(tcm_hcd->app_info),
            resp_buf,
            resp_buf_size,
            MIN(sizeof(tcm_hcd->app_info), resp_length));
    if (retval < 0) {
        TPD_INFO("Failed to copy application info\n");
        goto exit;
    }

    tcm_hcd->app_status = le2_to_uint(tcm_hcd->app_info.status);

    if (tcm_hcd->app_status == APP_STATUS_BOOTING ||
            tcm_hcd->app_status == APP_STATUS_UPDATING) {
        if (timeout > 0) {
            msleep(APP_STATUS_POLL_MS);
            timeout -= APP_STATUS_POLL_MS;
            goto get_app_info;
        }
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}

static int syna_tcm_get_boot_info(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    resp_buf = NULL;
    resp_buf_size = 0;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_GET_BOOT_INFO,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",
                STR(CMD_GET_BOOT_INFO));
        goto exit;
    }

    retval = secure_memcpy((unsigned char *)&tcm_hcd->boot_info,
            sizeof(tcm_hcd->boot_info),
            resp_buf,
            resp_buf_size,
            MIN(sizeof(tcm_hcd->boot_info), resp_length));
    if (retval < 0) {
        TPD_INFO("Failed to copy boot info\n");
        goto exit;
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}

static int syna_tcm_identify(struct syna_tcm_hcd *tcm_hcd, bool id)
{
    int retval;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    resp_buf = NULL;
    resp_buf_size = 0;

    mutex_lock(&tcm_hcd->identify_mutex);

    if (!id)
        goto get_info;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_IDENTIFY,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",
                STR(CMD_IDENTIFY));
        goto exit;
    }

    retval = secure_memcpy((unsigned char *)&tcm_hcd->id_info,
            sizeof(tcm_hcd->id_info),
            resp_buf,
            resp_buf_size,
            MIN(sizeof(tcm_hcd->id_info), resp_length));
    if (retval < 0) {
        TPD_INFO("Failed to copy identification info\n");
        goto exit;
    }

    syna_tcm_resize_chunk_size(tcm_hcd);

get_info:
    if (tcm_hcd->id_info.mode == MODE_APPLICATION) {
        retval = syna_tcm_get_app_info(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to get application info\n");
            goto exit;
        }
    } else {
        retval = syna_tcm_get_boot_info(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to get boot info\n");
            goto exit;
        }
    }

    retval = 0;

exit:
    mutex_unlock(&tcm_hcd->identify_mutex);

    kfree(resp_buf);

    return retval;
}

static int syna_tcm_run_application_firmware(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    bool retry;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    retry = true;

    resp_buf = NULL;
    resp_buf_size = 0;

retry:
    retval = syna_tcm_write_message(tcm_hcd,
            CMD_RUN_APPLICATION_FIRMWARE,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",
                STR(CMD_RUN_APPLICATION_FIRMWARE));
        goto exit;
    }

    retval = syna_tcm_identify(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to do identification\n");
        goto exit;
    }

    if (tcm_hcd->id_info.mode != MODE_APPLICATION) {
        TPD_INFO("Failed to run application firmware (boot status = 0x%02x)\n",
                tcm_hcd->boot_info.status);
        if (retry) {
            retry = false;
            goto retry;
        }
        retval = -EINVAL;
        goto exit;
    } else if (tcm_hcd->app_status != APP_STATUS_OK) {
        TPD_INFO("Application status = 0x%02x\n", tcm_hcd->app_status);
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}


/*
static int syna_tcm_run_bootloader_firmware(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    resp_buf = NULL;
    resp_buf_size = 0;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_RUN_BOOTLOADER_FIRMWARE,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_BOOTLOADER_FIRMWARE));
        goto exit;
    }

    retval = syna_tcm_identify(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to do identification\n");
        goto exit;
    }

    if (tcm_hcd->id_info.mode == MODE_APPLICATION) {
        TPD_INFO("Failed to enter bootloader mode\n");
        retval = -EINVAL;
        goto exit;
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}

static int syna_tcm_switch_mode(struct syna_tcm_hcd *tcm_hcd,
        enum firmware_mode mode)
{
    int retval;

    mutex_lock(&tcm_hcd->reset_mutex);

    switch (mode) {
    case FW_MODE_BOOTLOADER:
        retval = syna_tcm_run_bootloader_firmware(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to switch to bootloader mode\n");
            goto exit;
        }
        break;
    case FW_MODE_APPLICATION:
        retval = syna_tcm_run_application_firmware(tcm_hcd);
        if (retval < 0) {
            TPD_INFO("Failed to switch to application mode\n");
            goto exit;
        }
        break;
    default:
        TPD_INFO("Invalid firmware mode\n");
        retval = -EINVAL;
        goto exit;
    }

    retval = 0;

exit:

    mutex_unlock(&tcm_hcd->reset_mutex);

    return retval;
}
*/
static int syna_tcm_get_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
        enum dynamic_config_id id, unsigned short *value)
{
    int retval;
    unsigned char out_buf;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    resp_buf = NULL;
    resp_buf_size = 0;
    out_buf = (unsigned char)id;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_GET_DYNAMIC_CONFIG,
            &out_buf,
            sizeof(out_buf),
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_SHORT);
    if (retval < 0 || resp_length < 2) {
        retval = -EINVAL;
        TPD_INFO("Failed to read dynamic config\n");
        goto exit;
    }

    *value = (unsigned short)le2_to_uint(resp_buf);
exit:
    kfree(resp_buf);
    return retval;
}

static int syna_tcm_set_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
        enum dynamic_config_id id, unsigned short value)
{
    int retval;
    unsigned char out_buf[3];
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    TPD_DEBUG("%s:config 0x%x, value %d\n", __func__, id, value);
    resp_buf = NULL;
    resp_buf_size = 0;

    out_buf[0] = (unsigned char)id;
    out_buf[1] = (unsigned char)value;
    out_buf[2] = (unsigned char)(value >> 8);

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_SET_DYNAMIC_CONFIG,
            out_buf,
            sizeof(out_buf),
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_SHORT);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",
                STR(CMD_SET_DYNAMIC_CONFIG));
        goto exit;
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}
/*
static void syna_tcm_write_ps_status(void *chip_data, int ps_status)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    TPD_INFO("%s: value %d\n", __func__, ps_status);
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_PS_STATUS, ps_status);
    if (retval < 0) {
        TPD_INFO("Failed to write ps status\n");
    }

    return;
}
*/
static int syna_tcm_sleep(struct syna_tcm_hcd *tcm_hcd, bool en)
{
    int retval;
    unsigned char command;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    command = en ? CMD_ENTER_DEEP_SLEEP : CMD_EXIT_DEEP_SLEEP;

    resp_buf = NULL;
    resp_buf_size = 0;

    retval = syna_tcm_write_message(tcm_hcd,
            command,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n",en ?STR(CMD_ENTER_DEEP_SLEEP):STR(CMD_EXIT_DEEP_SLEEP));
        goto exit;
    }

    retval = 0;

exit:
    kfree(resp_buf);

    return retval;
}

static int syna_tcm_reset(void *chip_data)
{
    int retval;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    resp_buf = NULL;
    resp_buf_size = 0;

    mutex_lock(&tcm_hcd->reset_mutex);
/*
    retval = syna_tcm_write_message(tcm_hcd,
                CMD_RESET,
                NULL,
                0,
                &resp_buf,
                &resp_buf_size,
                &resp_length,
                0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_RESET));
        goto exit;
    }

    msleep(200);
 */
    retval = syna_tcm_identify(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to do identification\n");
        goto exit;
    }

    if (tcm_hcd->id_info.mode == MODE_APPLICATION)
        goto dispatch_reset;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_RUN_APPLICATION_FIRMWARE,
            NULL,
            0,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_APPLICATION_FIRMWARE));
    }

    retval = syna_tcm_identify(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to do identification\n");
        goto exit;
    }

dispatch_reset:
    TPD_INFO("Firmware mode = 0x%02x, boot status 0x%02x, app status 0x%02x\n",
        tcm_hcd->id_info.mode,
        tcm_hcd->boot_info.status,
        tcm_hcd->app_status);

exit:
    mutex_unlock(&tcm_hcd->reset_mutex);

    kfree(resp_buf);

    return retval;
}

static int syna_get_chip_info(void *chip_data){
    int ret = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    TPD_INFO("%s: Enter\n", __func__);

    ret = syna_tcm_reset(tcm_hcd);  // reset to get bootloader info or boot info
    if (ret < 0) {
        TPD_INFO("failed to reset device\n");
    }

    ret = syna_get_default_report_config(tcm_hcd);
    if (ret < 0) {
        TPD_INFO("failed to get default report config\n");
    }
    return 0;
}

static int syna_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    tcm_hcd->iHex_name = panel_data->extra;
    tcm_hcd->limit_name = panel_data->test_limit_name;
    tcm_hcd->fw_name = panel_data->fw_name;
    return 0;
}

static u8 syna_trigger_reason(void *chip_data, int gesture_enable, int is_suspended){
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    return tcm_hcd->trigger_reason;
}

static int syna_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    unsigned int idx;
    unsigned int status;
    struct object_data *object_data;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct touch_hcd *touch_hcd = tcm_hcd->touch_hcd;
    static unsigned int obj_attention = 0x00;


    if (points == NULL){
        return obj_attention;
        
    }
    object_data = touch_hcd->touch_data.object_data;

    for (idx = 0; idx < touch_hcd->max_objects; idx++) {
        status = object_data[idx].status;
        if (status != LIFT) {
            obj_attention |= (0x1 << idx);
        } else {
            if ((~obj_attention) & ((0x1) << idx))
                continue;
            else
                obj_attention &= (~(0x1 << idx));
        }

        points[idx].x = object_data[idx].x_pos;;
        points[idx].y = object_data[idx].y_pos;
        points[idx].z = (object_data[idx].x_width + object_data[idx].y_width) / 2;
        points[idx].width_major = (object_data[idx].x_width + object_data[idx].y_width) / 2;
        points[idx].touch_major = (object_data[idx].x_width + object_data[idx].y_width) / 2;
        points[idx].status = 1;
    }

    return obj_attention;
}

static int syna_tcm_set_gesture_mode(struct syna_tcm_hcd *tcm_hcd, bool enable)
{
    int retval;
    unsigned short config;

    /*this command may take too much time, if needed can add flag to skip this */
    retval = syna_tcm_get_dynamic_config(tcm_hcd, DC_IN_WAKEUP_GESTURE_MODE, &config);
    if (retval < 0) {
        TPD_INFO("Failed to get dynamic config\n");
        return retval;
    }

    TPD_DEBUG("config id is %d\n", config);

    if (enable) {
        if (!config) {
            retval = syna_set_input_reporting(tcm_hcd, true);
            if (retval < 0) {
                TPD_INFO("Failed to set input reporting\n");
                return retval;
            }

            retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_IN_WAKEUP_GESTURE_MODE, true);
            if (retval < 0) {
                TPD_INFO("Failed to set dynamic gesture config\n");
                return retval;
            }
        }
    }

    /*set to sleep*/
    retval = syna_tcm_sleep(tcm_hcd, !enable);
    if (retval < 0) {
        TPD_INFO("Failed to set sleep mode");
    }

    return retval;
}

static int syna_tcm_normal_mode(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;

    retval = syna_set_input_reporting(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to set input reporting\n");
        return retval;
    }

    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_IN_WAKEUP_GESTURE_MODE, false);
    if (retval < 0) {
        TPD_INFO("Failed to set dynamic gesture config\n");
        return retval;
    }

    return retval;
}

static int synaptics_corner_limit_handle(struct syna_tcm_hcd *tcm_hcd, bool enable)
{
        int ret = -1;

        if((LANDSCAPE_SCREEN_90 == tcm_hcd->touch_direction) && !enable) {
                //set area parameter
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x01);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_DARKZONE_ENABLE, 0x03);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_DARKZONE_ENABLE\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_DARKZONE_X, tcm_hcd->grip_darkzone_x);        //x part
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_DARKZONE_X\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_DARKZONE_Y, tcm_hcd->grip_darkzone_y);        //y part
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_DARKZONE_Y\n", __func__);
                    return ret;
                }
                TPD_INFO("CORNER_NOTCH_LEFT mode set corner mode\n");
        } else if ((LANDSCAPE_SCREEN_270 == tcm_hcd->touch_direction) && !enable) {
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x01);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_DARKZONE_ENABLE, 0x0C);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_DARKZONE_ENABLE\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_DARKZONE_X, tcm_hcd->grip_darkzone_x);        //x part
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_DARKZONE_X\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_DARKZONE_Y, tcm_hcd->grip_darkzone_y);        //y part
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_DARKZONE_Y\n", __func__);
                    return ret;
                }
                TPD_INFO("CORNER_NOTCH_RIGHT mode set corner mode\n");
        } else if ((VERTICAL_SCREEN == tcm_hcd->touch_direction) && enable) {
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x00);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
                    return ret;
                }
                ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_DARKZONE_ENABLE, 0x05);
                if (ret < 0) {
                    TPD_INFO("%s:failed to set DC_DARKZONE_ENABLE\n", __func__);
                    return ret;
                }
                TPD_INFO("CORNER_CLOSE set corner mode\n");
        }

        return ret;
}

static int synaptics_enable_edge_limit(struct syna_tcm_hcd *tcm_hcd, bool enable)
{
        int ret;

        ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_SUPPRESSION_ENABLED_NEW, 1);

        if (ret < 0) {
            TPD_INFO("%s:failed to enable grip suppression\n", __func__);
            return ret;
        }

        ret = synaptics_corner_limit_handle(tcm_hcd, enable);

        return ret;
}

static int synaptics_enable_headset_mode(struct syna_tcm_hcd *tcm_hcd, bool enable)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:enable = %d\n", __func__, enable);

    if (enable) {
        ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_HEADSET_MODE_ENABLED, 1);
        if (ret < 0) {
            TPD_INFO("%s:failed to enable headset mode\n", __func__);
            return ret;
        }
        TPD_INFO("%s:HEADSET PLUG IN\n", __func__);
    } else {
        ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_HEADSET_MODE_ENABLED, 0);
        if (ret < 0) {
            TPD_INFO("%s:failed to disable headset mode\n", __func__);
            return ret;
        }
        TPD_INFO("%s:HEADSET PLUG OUT\n", __func__);
    }

    return ret;
}

static int synaptics_enable_game_mode(struct syna_tcm_hcd *tcm_hcd, bool enable)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:enable = %d\n", __func__, enable);

    if (enable) {
        ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GAME_MODE_ENABLED, 1);
        if (ret < 0) {
            TPD_INFO("%s:failed to enable game mode\n", __func__);
            return ret;
        }
    } else {
        ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GAME_MODE_ENABLED, 0);
        if (ret < 0) {
            TPD_INFO("%s:failed to disable game mode\n", __func__);
            return ret;
        }
    }

    return ret;
}

static int syna_mode_switch(void *chip_data, work_mode mode, bool flag){
    int ret = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    TPD_INFO("syna_mode_switch begin, mode = %d\n", mode);
    switch(mode) {
        case MODE_NORMAL:
            TPD_DETAIL("syna_mode_switch MODE_NORMAL\n");
            //ret = syna_tcm_normal_mode(tcm_hcd);
            if (ret < 0) {
                TPD_INFO("normal mode switch failed\n");
            }
            break;
        case MODE_GESTURE:
            ret = syna_tcm_set_gesture_mode(tcm_hcd, flag);
            if (ret < 0) {
                TPD_INFO("%s:Failed to set gesture mode\n", __func__);
            }
            break;
        case MODE_SLEEP:
            ret = syna_tcm_sleep(tcm_hcd, true);
            if (ret < 0) {
                TPD_INFO("%s: failed to switch to sleep", __func__);
            }
            break;
        case MODE_CHARGE:
            ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, flag?1:0);
            if (ret < 0) {
                TPD_INFO("%s:failed to set charger mode\n", __func__);
            }
            break;

        case MODE_HEADSET:
            ret = synaptics_enable_headset_mode(tcm_hcd, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable headset mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_GAME:
            ret = synaptics_enable_game_mode(tcm_hcd, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable game mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_EDGE:
            //ret = syna_tcm_set_dynamic_config(tcm_hcd, DC_GRIP_SUPPRESSION_ENABLED, flag?1:0);
            //if (ret < 0) {
            //    TPD_INFO("%s:failed to set grip suppression\n", __func__);
            //}
            ret = synaptics_enable_edge_limit(tcm_hcd, flag);
            if (ret < 0) {
                TPD_INFO("%s: synaptics enable edg limit failed.\n", __func__);
            }
            break;
        default:
            break;
    }
    return 0;
}

static int syna_ftm_process(void *chip_data)
{
    TPD_INFO("%s: go into sleep\n", __func__);
    syna_get_chip_info(chip_data);
    syna_mode_switch(chip_data, MODE_SLEEP, true);
    return 0;
}

static int  syna_tcm_reinit_device (void *chip_data)
{
    complete_all(&response_complete);
    complete_all(&report_complete);

    return 0;
}

static int syna_hw_reset(struct syna_tcm_hcd *tcm_hcd, struct hw_resource *hw_res)
{
    if (gpio_is_valid(hw_res->reset_gpio)) {
        TPD_INFO("hardware reset: %d\n", hw_res->reset_gpio);
        gpio_set_value(hw_res->reset_gpio, false);
        msleep(20);
        gpio_set_value(hw_res->reset_gpio, true);
        msleep(200);
        return 0;
    }

    return -EINVAL;
}

int syna_reset_gpio(void *chip_data, bool enable)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    TPD_DEBUG("%s:gpio enable %d\n", __func__, enable);
    if (gpio_is_valid(tcm_hcd->hw_res->reset_gpio)) {
        gpio_set_value(tcm_hcd->hw_res->reset_gpio, enable);
    }

    return 0;
}

static int syna_power_control(void *chip_data, bool enable)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    TPD_DEBUG("%s: %d\n", __func__, enable);

    return syna_hw_reset(tcm_hcd, tcm_hcd->hw_res);
}

static fw_check_state syna_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    char *fw_ver = NULL;

    if(!tcm_hcd->using_fae){
        retval = wait_for_completion_timeout(&tcm_hcd->config_complete, msecs_to_jiffies(RESPONSE_TIMEOUT_MS_LONG * 2));
        if (retval == 0) {
            TPD_INFO("Timed out waiting for response config_complete\n");
        }
    }

    TPD_INFO("fw id %d, custom config id 0x%s\n", panel_data->TP_FW, tcm_hcd->app_info.customer_config_id);

    if (strlen(tcm_hcd->app_info.customer_config_id) == 0) {
        return FW_NORMAL;
    }

    fw_ver = kzalloc(9, GFP_KERNEL);
    memcpy(fw_ver, tcm_hcd->app_info.customer_config_id, 8);
    fw_ver[8] = '\0';

    panel_data->TP_FW = le4_to_uint(tcm_hcd->id_info.build_id);
    if (panel_data->TP_FW == 0) {
        kfree(fw_ver);
        return FW_NORMAL;
    }

    if (panel_data->manufacture_info.version) {
        sprintf(panel_data->manufacture_info.version, "0x%s", fw_ver);
    }

    kfree(fw_ver);

    return FW_NORMAL;
}

void syna_fw_version_update(void *chip_data)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct touchpanel_data *ts = spi_get_drvdata(tcm_hcd->s_client);
    char *fw_ver = NULL;

    if (strlen(tcm_hcd->app_info.customer_config_id) == 0) {
        return;
    }

    fw_ver = kzalloc(9, GFP_KERNEL);
    memcpy(fw_ver, tcm_hcd->app_info.customer_config_id, 8);
    fw_ver[8] = '\0';

    ts->panel_data.TP_FW = le4_to_uint(tcm_hcd->id_info.build_id);
    if (ts->panel_data.TP_FW == 0) {
        kfree(fw_ver);
        return;
    }

    if (ts->panel_data.manufacture_info.version) {
        sprintf(ts->panel_data.manufacture_info.version, "0x%s", fw_ver);
    }

    TPD_DETAIL("Update fw id %d, custom config id 0x%s\n", ts->panel_data.TP_FW, fw_ver);

    kfree(fw_ver);

    return;
}

static int syna_tcm_helper(struct syna_tcm_hcd *tcm_hcd)
{
    if (tcm_hcd->id_info.mode != MODE_APPLICATION &&
        !mutex_is_locked(&tcm_hcd->reset_mutex)) {
        TPD_INFO("%s: use helper\n", __func__);
        queue_work(tcm_hcd->helper_workqueue, &tcm_hcd->helper_work);
    }

    return 0;
}

static void syna_tcm_helper_work(struct work_struct *work)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = container_of(work, struct syna_tcm_hcd,
        helper_work);

    mutex_lock(&tcm_hcd->reset_mutex);
    retval = syna_tcm_run_application_firmware(tcm_hcd);
    if (retval < 0) {
        TPD_INFO("Failed to switch to app mode\n");
    }

    mutex_unlock(&tcm_hcd->reset_mutex);
}

static int syna_tcm_async_work(void *chip_data)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION) {
        return 0;
    }

    retval = syna_tcm_identify(tcm_hcd, false);
    if (retval < 0) {
        TPD_INFO("Failed to do identification\n");
        return retval;
    }

    //syna_set_trigger_reason(tcm_hcd, IRQ_FW_AUTO_RESET);
    return 0;
}
/*
static int syna_tcm_erase_flash(struct syna_tcm_hcd *tcm_hcd, unsigned int page_start, unsigned int page_count)
{
    int ret;
    unsigned char out_buf[2];
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    TPD_INFO("start page %d, page count %d\n", page_start, page_count);
    resp_buf = NULL;
    resp_buf_size = 0;

    out_buf[0] = (unsigned char)page_start;
    out_buf[1] = (unsigned char)page_count;

    ret = syna_tcm_write_message(tcm_hcd,
            CMD_ERASE_FLASH,
            out_buf,
            sizeof(out_buf),
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            ERASE_FLASH_DELAY_MS);
    if (ret < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_ERASE_FLASH));
    }

    kfree(resp_buf);
    return ret;
}

static int syna_tcm_write_flash(struct syna_tcm_hcd *tcm_hcd, struct reflash_hcd *reflash_hcd,
                unsigned int address, const unsigned char *data, unsigned int datalen)
{
    int retval;
    unsigned int w_len, xfer_len, remaining_len;
    unsigned int flash_addr, block_addr;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int offset;
    struct syna_tcm_buffer out;

    resp_buf = NULL;
    resp_buf_size = 0;
    memset(&out, 0, sizeof(out));
    INIT_BUFFER(out, false);

    w_len = tcm_hcd->wr_chunk_size - 5;
    w_len = w_len - (w_len % reflash_hcd->write_block_size);
    w_len = MIN(w_len, reflash_hcd->max_write_payload_size);
    offset = 0;

    remaining_len = datalen;

    while(remaining_len) {
        if (remaining_len > w_len) {
            xfer_len = w_len;
        } else {
            xfer_len = remaining_len;
        }

        retval = syna_tcm_alloc_mem(tcm_hcd,
                &out,
                xfer_len + 2);
        if (retval < 0) {
            TPD_INFO("Failed to alloc memory\n");
            break;
        }

        flash_addr = address + offset;
        block_addr = flash_addr / reflash_hcd->write_block_size;
        out.buf[0] = (unsigned char)block_addr;
        out.buf[1] = (unsigned char)(block_addr >> 8);

        retval = secure_memcpy(&out.buf[2],
                xfer_len,
                &data[offset],
                datalen - offset,
                xfer_len);
        if (retval < 0) {
            TPD_INFO("Failed to copy write data\n");
            break;
        }

        retval = syna_tcm_write_message(tcm_hcd,
                CMD_WRITE_FLASH,
                out.buf,
                xfer_len + 2,
                &resp_buf,
                &resp_buf_size,
                &resp_length,
                WRITE_FLASH_DELAY_MS);
        if (retval < 0) {
            TPD_INFO("Failed to write message %s, Addr 0x%08x, Len 0x%d\n",
                STR(CMD_WRITE_FLASH), flash_addr, xfer_len);
            break;
        }

        offset += xfer_len;
        remaining_len -= xfer_len;
    }

    RELEASE_BUFFER(out);
    kfree(resp_buf);
    return 0;
}
*/
extern int td4320_nf_try_to_recovery_ic(struct syna_tcm_hcd *tcm_hcd, char *iHex);

static fw_update_state syna_tcm_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct touchpanel_data *ts = spi_get_drvdata(tcm_hcd->s_client);
    TPD_INFO("syna_tcm_fw_update begin\n");

    //disable_irq_nosync(tcm_hcd->s_client->irq);
    if(fw == NULL){
        TPD_INFO("syna_tcm_fw_update get NULL fw\n");
    }
    if (ts->fw_update_app_support && force == 1) {
        if(tcm_hcd->zeroflash_hcd->fw_entry != NULL) {
            release_firmware(tcm_hcd->zeroflash_hcd->fw_entry);
            tcm_hcd->zeroflash_hcd->fw_entry = NULL;
            tcm_hcd->zeroflash_hcd->image = NULL;
        }
        tcm_hcd->using_fae = true;
        return ret;
    } else {
        tcm_hcd->using_fae = false;
    }
    ret = zeroflash_download_firmware_directly(tcm_hcd, fw);

    enable_irq(tcm_hcd->s_client->irq);

    TPD_INFO("syna_tcm_fw_update end\n");

    tcm_hcd->first_tp_fw_update_done = true;

    return ret;
}


static int syna_get_gesture_info(void *chip_data, struct gesture_info * gesture)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct touch_hcd *touch_hcd = tcm_hcd->touch_hcd;
    struct touch_data *touch_data = &touch_hcd->touch_data;

    gesture->clockwise = 2;
    switch (touch_data->lpwg_gesture) {
        case DTAP_DETECT:
            gesture->gesture_type = DouTap;
            break;
        case CIRCLE_DETECT:
            gesture->gesture_type = Circle;
            if (touch_data->extra_gesture_info == 0x10)
                gesture->clockwise = 0;
            else if (touch_data->extra_gesture_info == 0x20)
                gesture->clockwise = 1;
            break;
        case SWIPE_DETECT:
            if (touch_data->extra_gesture_info == 0x41) {//x+
                gesture->gesture_type = Left2RightSwip;
            } else if (touch_data->extra_gesture_info == 0x42) {//x-
                gesture->gesture_type = Right2LeftSwip;
            } else if (touch_data->extra_gesture_info == 0x44) {//y+
                gesture->gesture_type = Up2DownSwip;
            } else if (touch_data->extra_gesture_info == 0x48) {//y-
                gesture->gesture_type = Down2UpSwip;
            } else if (touch_data->extra_gesture_info == 0x81) {//2x-
                gesture->gesture_type = DouSwip;
            } else if (touch_data->extra_gesture_info == 0x82) {//2x+
                gesture->gesture_type = DouSwip;
            } else if (touch_data->extra_gesture_info == 0x84) {//2y+
                gesture->gesture_type = DouSwip;
            } else if (touch_data->extra_gesture_info == 0x88) {//2y-
                gesture->gesture_type = DouSwip;
            }
            break;
        case UNICODE_DETECT:
            if (touch_data->extra_gesture_info == 0x6d) {
                gesture->gesture_type = Mgestrue;

            } else if (touch_data->extra_gesture_info == 0x77) {
                gesture->gesture_type = Wgestrue;
            }
            break;
        case VEE_DETECT:
            if (touch_data->extra_gesture_info == 0x02) {//up
                gesture->gesture_type = UpVee;
            } else if (touch_data->extra_gesture_info == 0x01) {//down
                gesture->gesture_type = DownVee;
            } else if (touch_data->extra_gesture_info == 0x08) {//left
                gesture->gesture_type = LeftVee;
            } else if (touch_data->extra_gesture_info == 0x04) {//right
                gesture->gesture_type = RightVee;
            }
            break;
        case TRIANGLE_DETECT:
        default:
            TPD_DEBUG("not support\n");
            break;
    }
    if (gesture->gesture_type != UnkownGesture) {
        gesture->Point_start.x = (touch_data->data_point[0] | (touch_data->data_point[1] << 8));
        gesture->Point_start.y = (touch_data->data_point[2] | (touch_data->data_point[3] << 8));
        gesture->Point_end.x    = (touch_data->data_point[4] | (touch_data->data_point[5] << 8));
        gesture->Point_end.y    = (touch_data->data_point[6] | (touch_data->data_point[7] << 8));
        gesture->Point_1st.x    = (touch_data->data_point[8] | (touch_data->data_point[9] << 8));
        gesture->Point_1st.y    = (touch_data->data_point[10] | (touch_data->data_point[11] << 8));
        gesture->Point_2nd.x    = (touch_data->data_point[12] | (touch_data->data_point[13] << 8));
        gesture->Point_2nd.y    = (touch_data->data_point[14] | (touch_data->data_point[15] << 8));
        gesture->Point_3rd.x    = (touch_data->data_point[16] | (touch_data->data_point[17] << 8));
        gesture->Point_3rd.y    = (touch_data->data_point[18] | (touch_data->data_point[19] << 8));
        gesture->Point_4th.x    = (touch_data->data_point[20] | (touch_data->data_point[21] << 8));
        gesture->Point_4th.y    = (touch_data->data_point[22] | (touch_data->data_point[23] << 8));
    }

    TPD_DEBUG("lpwg:0x%x, extra:%x type:%d\n", touch_data->lpwg_gesture,
                    touch_data->extra_gesture_info, gesture->gesture_type);

    return 0;
}


static void store_to_file(int fd, char* format, ...)
{
    va_list args;
    char buf[64] = {0};

    va_start(args, format);
    vsnprintf(buf, 64, format, args);
    va_end(args);

    if(fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(fd, buf, strlen(buf));
#else
        sys_write(fd, buf, strlen(buf));
#endif
    }
}

#define MAX_BUFFER_SIZE 1024
static void store_to_buf(char *buffer, char* format, ...)
{
    va_list args;
    char buf[64] = {0};
    static int count = 0;

    if (buffer == NULL) {
        count = 0;
        return;
    }

    va_start(args, format);
    vsnprintf(buf, 64, format, args);
    va_end(args);

    if (count + strlen(buf) + 2 > MAX_BUFFER_SIZE) {
        pr_err("exceed the max buffer size\n");
        count = 0;
        return;
    }

    memcpy(buffer + count, buf, strlen(buf));
    count += strlen(buf);
    *(buffer + count) = '\n';
    count++;
}

static int syna_tcm_get_frame_size_words(struct syna_tcm_hcd *tcm_hcd, bool image_only)
{
    unsigned int rows;
    unsigned int cols;
    unsigned int hybrid;
    unsigned int buttons;
    int size= 0;
    struct syna_tcm_app_info *app_info = &tcm_hcd->app_info;

    rows = le2_to_uint(app_info->num_of_image_rows);
    cols = le2_to_uint(app_info->num_of_image_cols);
    hybrid = le2_to_uint(app_info->has_hybrid_data);
    buttons = le2_to_uint(app_info->num_of_buttons);

    size = rows * cols;

    if (!image_only) {
        if (hybrid)
            size += rows + cols;
        size += buttons;
    }

    return size;
}

static int syna_check_notch(int row, int col)
{
    if (row == 35 && (col > 5 && col < 10)) {
        return 1;
    } else {
        return 0;
    }
}

static int syna_testing_limit_compare(char *s,
    struct syna_testdata *syna_testdata, char *test_name, char *buf)
{
    int idx = 0;
    int fd = syna_testdata->fd;
    unsigned int rows, cols;
    unsigned int row, col;
    int error_count = 0;
    short data;
    unsigned int offset;
    struct limit_block *limit_block;
    int16_t hi_data, lo_data;
    int16_t *limit_data;

    TPD_DEBUG("start %s compare\n", test_name);
    offset = synaptics_get_limit_data(test_name, syna_testdata->fw->data);

    if (offset == 0) {
        store_to_buf(s, "Limit param error");
        error_count++;
        return error_count;
    }

    limit_block = (struct limit_block *)(syna_testdata->fw->data + offset);

    rows = syna_testdata->RX_NUM;
    cols = syna_testdata->TX_NUM;

    TPD_INFO("compare mode %d, test name %s, size %d\n", limit_block->mode, limit_block->name, limit_block->size);
    if ((limit_block->mode && limit_block->size < rows*cols*2) ||
        (!limit_block->mode && limit_block->size != 4)) {
        store_to_buf(s, "limit data is not valid");
        error_count++;
        return error_count;
    }

    idx = 0;
    limit_data = &limit_block->data;
    for (row = 0; row < syna_testdata->RX_NUM; row++) {
        printk("[%02d] ", row);
        store_to_file(fd, "[%02d],", row);
        for(col = 0; col < syna_testdata->TX_NUM; col++) {
            data = (short)le2_to_uint(&buf[idx * 2]);
            printk("[%4d] ", data);
            store_to_file(fd, "%4d,", data);

            if (syna_check_notch(row, col)) {
                idx++;
                continue;
            }

            if (limit_block->mode) {
                lo_data = (int16_t)limit_data[2*idx];
                hi_data = (int16_t)limit_data[2*idx + 1];
            } else {
                lo_data = (int16_t)limit_data[0];
                hi_data = (int16_t)limit_data[1];
            }
            if (data > hi_data || data < lo_data) {
                if (!error_count) {
                    TPD_INFO("error:[row,col][%2d %2d]=%4d\n", row, col, data);
                    TPD_INFO("range lo hi [%5d %5d]\n", lo_data, hi_data);
                    store_to_buf(s, "error:[row,col][%2d %2d]=%4d", row, col, data);
                }
                error_count++;
            }
            idx++;
        }
        store_to_file(fd, "\n");
        printk("\n");
    }

    return error_count;
}

static int syna_testing_pt11(char *s, struct syna_tcm_hcd *tcm_hcd, struct syna_testdata *syna_testdata)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int frame_size;
    int error_count = 0;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        store_to_buf(s, "Not in application mode");
        error_count++;
        return error_count;
    }

    store_to_file(syna_testdata->fd, "\nPT11 Test:\n");
    out[0] = TEST_PT11;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_LONG);
    if (retval < 0) {
        store_to_buf(s, "Failed to write %s", STR(CMD_PRODUCTION_TEST));
        error_count++;
        goto exit;
    }

    frame_size = syna_tcm_get_frame_size_words(tcm_hcd, false);
    if (frame_size != resp_buf_size / 2) {
        store_to_buf(s, "Frame size mismatch");
        error_count++;
        goto exit;
    }

    error_count = syna_testing_limit_compare(s, syna_testdata, "PT11", resp_buf);

    if (error_count) {
        store_to_buf(s, "PT11 Test End");
    }
exit:
    kfree(resp_buf);
    return error_count;
}

static int syna_testing_dynamic_range(char *s, struct syna_tcm_hcd *tcm_hcd, struct syna_testdata *syna_testdata, char *type)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int frame_size;
    int error_count = 0;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        store_to_buf(s, "Not in application mode");
        error_count++;
        return error_count;
    }

    store_to_file(syna_testdata->fd, "\n%s Test:\n", type);
    out[0] = TEST_DYNAMIC_RANGE;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_LONG);
    if (retval < 0) {
        store_to_buf(s, "Failed to write %s", STR(CMD_PRODUCTION_TEST));
        error_count++;
        goto exit;
    }

    frame_size = syna_tcm_get_frame_size_words(tcm_hcd, false);
    if (frame_size != resp_buf_size / 2) {
        store_to_buf(s, "Frame size mismatch %d", resp_buf_size);
        error_count++;
        goto exit;
    }

    error_count = syna_testing_limit_compare(s, syna_testdata, type, resp_buf);
   if (error_count) {
        store_to_buf(s, "DRT Test End");
    }
exit:
    kfree(resp_buf);
    return error_count;
}

static int syna_testing_noise(char *s, struct syna_tcm_hcd *tcm_hcd, struct syna_testdata *syna_testdata, char *type)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int frame_size;
    int error_count = 0;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        store_to_buf(s, "Not in application mode");
        error_count++;
        return error_count;
    }

    store_to_file(syna_testdata->fd, "%s Test:\n", type);

    out[0] = TEST_NOISE;
    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_LONG);
    if (retval < 0) {
        store_to_buf(s, "Failed to write %s", STR(CMD_PRODUCTION_TEST));
        error_count++;
        goto exit;
    }

    frame_size = syna_tcm_get_frame_size_words(tcm_hcd, false);
    if (frame_size != resp_buf_size / 2) {
        store_to_buf(s, "Frame size mismatch");
        error_count++;
        goto exit;
    }

    error_count = syna_testing_limit_compare(s, syna_testdata, type, resp_buf);
    if (error_count) {
        store_to_buf(s, "Noise Test End");
    }
exit:
    kfree(resp_buf);
    return error_count;
}

#ifdef CONFIG_TOUCHPANEL_SYNAPTICS_TD4330_NOFLASH
#define MAX_ROWS_PER_BURST 9
#else
#define MAX_ROWS_PER_BURST 6
#endif


static int syna_testing_Doze_dynamic_range(char *s, struct syna_tcm_hcd *tcm_hcd, struct syna_testdata *syna_testdata, char *type)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int data_size;
    int cols = 0, tmp = 0;
    int error_count = 0;
    int number_of_buttons = 0;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        store_to_buf(s, "Not in application mode");
        error_count++;
        return error_count;
    }

    store_to_file(syna_testdata->fd, "\n%s Test:\n", type);

    cols = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);
    data_size =  cols* MAX_ROWS_PER_BURST;
    number_of_buttons  = le2_to_uint(tcm_hcd->app_info.num_of_buttons);
    if (number_of_buttons) {
        data_size++;
    }

    out[0] = TEST_DYNAMIC_RANGE_DOZE;
    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_LONG);
    if (retval < 0) {
        store_to_buf(s, "Failed to write %s", STR(CMD_PRODUCTION_TEST));
        error_count++;
        goto exit;
    }

    if (data_size != resp_buf_size / 2) {
        store_to_buf(s, "Data size mismatch: %d", resp_buf_size);
        error_count++;
        goto exit;
    }

    tmp = syna_testdata->RX_NUM;
    syna_testdata->RX_NUM = MAX_ROWS_PER_BURST;

    error_count = syna_testing_limit_compare(s, syna_testdata, type, resp_buf);
    if (error_count) {
        store_to_buf(s, "Doze Dynamic Test End");
    }
    syna_testdata->RX_NUM = tmp;
exit:

    kfree(resp_buf);
    return error_count;

}

static int syna_testing_Doze_noise(char *s, struct syna_tcm_hcd *tcm_hcd, struct syna_testdata *syna_testdata, char *type)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int data_size;
    int cols = 0, tmp = 0;
    int error_count = 0;
    int num_of_buttons;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        store_to_buf(s, "Not in application mode");
        error_count++;
        return error_count;
    }

    store_to_file(syna_testdata->fd, "\n%s Test:\n", type);

    cols = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);
    data_size =  cols* MAX_ROWS_PER_BURST;
    num_of_buttons = le2_to_uint(tcm_hcd->app_info.num_of_buttons);
    if (num_of_buttons) {
        data_size++;
    }

    out[0] = TEST_NOISE_DOZE;
    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            RESPONSE_TIMEOUT_MS_LONG);
    if (retval < 0) {
        store_to_buf(s, "Failed to write %s", STR(CMD_PRODUCTION_TEST));
        error_count++;
        goto exit;
    }

    if (data_size != resp_buf_size / 2) {
        store_to_buf(s, "Data size mismatch: %d", resp_buf_size);
        error_count++;
        goto exit;
    }

    tmp = syna_testdata->RX_NUM;
    syna_testdata->RX_NUM = MAX_ROWS_PER_BURST;

    error_count = syna_testing_limit_compare(s, syna_testdata, type, resp_buf);
    if (error_count) {
        store_to_buf(s, "Doze Noise Test End");
    }
    syna_testdata->RX_NUM = tmp;
exit:

    kfree(resp_buf);
    return error_count;
}

static int syna_int_pin_test(char *s, void *chip_data, struct syna_testdata *syna_testdata)
{
    int eint_status, eint_count = 0, read_gpio_num = 10;

    while(read_gpio_num--) {
        msleep(5);
        eint_status = gpio_get_value(syna_testdata->irq_gpio);
        if (eint_status == 1)
            eint_count--;
        else
            eint_count++;
        TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count, eint_status);
    }
    if (eint_count == 10) {
        TPD_INFO("error :  TP EINT PIN direct short!\n");
        store_to_buf(s, "TP EINT direct stort");
        eint_count = 0;
        return 1;
    }

    return 0;
}

static void syna_tcm_black_screen_test(void *chip_data, char *message)
{

    int retval = 0;
    int fd;
    mm_segment_t old_fs;
    int error_count = 0;
    uint8_t data_buf[128];
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    const struct firmware *fw = NULL;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct syna_testdata syna_testdata;
    char *buffer;

    /*start black screen test*/
    buffer = (char *)kzalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if (!buffer) {
        return;
    }

    store_to_buf(NULL, "");
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    sprintf(data_buf, "/sdcard/tp_testlimit_gesture_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    old_fs = get_fs();
    set_fs(KERNEL_DS);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    fd = ksys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
    fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif
    if (fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", data_buf);
        store_to_buf(buffer, "Open failed failed");
//        error_count++;
        set_fs(old_fs);
//        goto sys_err;
    }

    retval = request_firmware(&fw, tcm_hcd->limit_name, &tcm_hcd->s_client->dev);
    if (retval < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", tcm_hcd->limit_name, retval);
        store_to_buf(buffer, "No Limit data");
        error_count++;
        goto firware_err;
    }

    syna_testdata.RX_NUM = tcm_hcd->hw_res->RX_NUM;
    syna_testdata.TX_NUM = tcm_hcd->hw_res->TX_NUM;
    syna_testdata.fd = fd;
    syna_testdata.fw = fw;

    error_count += syna_testing_noise(buffer, tcm_hcd, &syna_testdata, "GestureNoise");
    error_count += syna_testing_dynamic_range(buffer, tcm_hcd, &syna_testdata, "GestureDynamic");

    release_firmware(fw);

firware_err:
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_close(fd);
#else
    sys_close(fd);
#endif
    set_fs(old_fs);

//sys_err:
    sprintf(message, "%d errors. %s", error_count, buffer);
    TPD_INFO("%d errors. %s\n", error_count, buffer);

    kfree(buffer);
    return;

}

static void syna_auto_test(struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata)
{
    int retval = 0;
    int error_count = 0;
    char *buffer;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    buffer = (char *)kzalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if (!buffer) {
        return;
    }

    store_to_buf(NULL, ""); /* must add this */

    /* disable ESD mode first */
    
if(0){
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_DISABLE_ESD, false);
}
    if (retval < 0) {
        error_count++;
        store_to_buf(buffer, "Close Esd error");
    }

    /*normal test*/
    error_count += syna_int_pin_test(buffer, tcm_hcd, syna_testdata);
    error_count += syna_testing_noise(buffer, tcm_hcd, syna_testdata, "Noise");
    error_count += syna_testing_dynamic_range(buffer, tcm_hcd, syna_testdata, "Dynamic");
    error_count += syna_testing_pt11(buffer, tcm_hcd, syna_testdata);

    /*start Doze test*/
    error_count += syna_testing_Doze_dynamic_range(buffer, tcm_hcd, syna_testdata, "DozeDynamic");
    error_count += syna_testing_Doze_noise(buffer, tcm_hcd, syna_testdata, "DozeNoise");

    seq_printf(s, buffer);
    seq_printf(s, "imageid = %lld customID 0x%s\n", syna_testdata->TP_FW, tcm_hcd->app_info.customer_config_id);
    seq_printf(s, "%d error(s). %s\n", error_count, error_count?"":"All test passed.");
    TPD_INFO(" TP auto test %d error(s). %s\n", error_count, error_count?"":"All test passed.");

    kfree(buffer);
}

static int syna_tcm_collect_reports(struct syna_tcm_hcd *tcm_hcd, enum report_type report_type, unsigned int num_of_reports)
{
    int retval;
    bool completed = false;
    unsigned int timeout;
    struct syna_tcm_test *test_hcd = tcm_hcd->test_hcd;
    unsigned char out[2] = {0};
    unsigned char *resp_buf = NULL;
    unsigned int resp_buf_size = 0;
    unsigned int resp_length = 0;

    test_hcd->report_index = 0;
    test_hcd->report_type = report_type;
    test_hcd->num_of_reports = num_of_reports;

    reinit_completion(&report_complete);

    out[0] = test_hcd->report_type;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_ENABLE_REPORT,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        TPD_INFO("Failed to write message %s\n", STR(CMD_ENABLE_REPORT));
        completed = false;
        goto exit;
    }

    timeout = REPORT_TIMEOUT_MS * num_of_reports;

    retval = wait_for_completion_timeout(&report_complete,
            msecs_to_jiffies(timeout));
    if (retval == 0) {
        TPD_INFO("Timed out waiting for report collection\n");
    } else {
        completed = true;
    }

    out[0] = test_hcd->report_type;

    retval = syna_tcm_write_message(tcm_hcd,
        CMD_DISABLE_REPORT,
        out,
        1,
        &resp_buf,
        &resp_buf_size,
        &resp_length,
        0);
    if (retval < 0) {
        TPD_INFO("Failed to write message %s\n", STR(CMD_DISABLE_REPORT));
    }

    if (!completed) {
        retval = -EIO;
    }
exit:

    return retval;
}

static void syna_tcm_test_report(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned int offset, report_size;
    struct syna_tcm_test *test_hcd = tcm_hcd->test_hcd;

    if (tcm_hcd->report.id != test_hcd->report_type) {
        TPD_INFO("Not request report type\n");
        return;
    }

    report_size = tcm_hcd->report.buffer.data_length;
    LOCK_BUFFER(test_hcd->report);

    if (test_hcd->report_index == 0) {
        retval = syna_tcm_alloc_mem(tcm_hcd,
            &test_hcd->report,
            report_size*test_hcd->num_of_reports);
        if (retval < 0) {
            TPD_INFO("Failed to allocate memory\n");

            UNLOCK_BUFFER(test_hcd->report);
            return;
        }
    }

    if (test_hcd->report_index < test_hcd->num_of_reports) {
        offset = report_size * test_hcd->report_index;
        retval = secure_memcpy(test_hcd->report.buf + offset,
                test_hcd->report.buf_size - offset,
                tcm_hcd->report.buffer.buf,
                tcm_hcd->report.buffer.buf_size,
                tcm_hcd->report.buffer.data_length);
        if (retval < 0) {
            TPD_INFO("Failed to copy report data\n");

            UNLOCK_BUFFER(test_hcd->report);
            return;
        }

        test_hcd->report_index++;
        test_hcd->report.data_length += report_size;
    }

    UNLOCK_BUFFER(test_hcd->report);

    if (test_hcd->report_index == test_hcd->num_of_reports) {
        complete(&report_complete);
    }
    return;
}

static void syna_tcm_format_print(struct seq_file *s, struct syna_tcm_hcd *tcm_hcd, char *buffer)
{
    unsigned int row, col;
    unsigned int rows, cols;
    short *pdata_16;
    struct syna_tcm_test *test_hcd = tcm_hcd->test_hcd;

    rows = le2_to_uint(tcm_hcd->app_info.num_of_image_rows);
    cols = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);

    if (buffer == NULL)
        pdata_16 = (short *)&test_hcd->report.buf[0];
    else
        pdata_16 = (short *)buffer;

    for (row = 0; row < rows; row++) {
        seq_printf(s, "[%02d] ", row);
        for (col = 0; col < cols; col++) {
            seq_printf(s, "%5d ", *pdata_16);
            pdata_16++;
        }
        seq_printf(s, "\n");
    }

    seq_printf(s, "\n");

    return;
}

static void syna_main_register(struct seq_file *s, void *chip_data)
{
    int retval = 0;
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;

    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    resp_buf = NULL;
    resp_buf_size = 0;

    retval = syna_tcm_write_message(tcm_hcd,
        CMD_GET_NSM_INFO,
        NULL,
        0,
        &resp_buf,
        &resp_buf_size,
        &resp_length,
        0);
    if (retval < 0) {
        TPD_INFO("Failed to write command %s\n", STR(CMD_GET_NSM_INFO));
        if (s) {
            seq_printf(s, "Failed to write command %s\n", STR(CMD_GET_NSM_INFO));
        }
        goto exit;
    }

    if (resp_length < 10) {
        TPD_INFO("Error response data\n");
        if (s) {
           seq_printf(s, "Error response data\n");
        }
        goto exit;
    }

    TPD_INFO("Reset reason:0x%02x%02x\n", resp_buf[1], resp_buf[0]);
    TPD_INFO("power im: 0x%02x%02x\n", resp_buf[3], resp_buf[2]);
    TPD_INFO("nsm Frequency: 0x%02x%02x\n", resp_buf[5], resp_buf[4]);
    TPD_INFO("nsm State: 0x%02x%02x\n", resp_buf[7], resp_buf[6]);
    TPD_INFO("esd State: 0x%02x%02x\n", resp_buf[8], resp_buf[9]);
    TPD_INFO("Buid ID:%d, Custom ID:0x%s\n",
                le4_to_uint(tcm_hcd->id_info.build_id),
                tcm_hcd->app_info.customer_config_id);

    if (!s) {
        goto exit;
    }

    seq_printf(s, "Reset reason:0x%02x%02x\n", resp_buf[1], resp_buf[0]);
    seq_printf(s, "power im: 0x%02x%02x\n", resp_buf[3], resp_buf[2]);
    seq_printf(s, "nsm Frequency: 0x%02x%02x\n", resp_buf[5], resp_buf[4]);
    seq_printf(s, "nsm State: 0x%02x%02x\n", resp_buf[7], resp_buf[6]);
    seq_printf(s, "esd State: 0x%02x%02x\n", resp_buf[8], resp_buf[9]);
    seq_printf(s, "Buid ID:%d, Custom ID:0x%s\n",
                le4_to_uint(tcm_hcd->id_info.build_id),
                tcm_hcd->app_info.customer_config_id);
exit:
    kfree(resp_buf);

    return;
}

static void syna_delta_read(struct seq_file *s, void *chip_data)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 1);
    if (retval < 0){
        TPD_INFO("Failed to exit doze\n");
    }

    msleep(20); // delay 20ms

    retval = syna_tcm_collect_reports(tcm_hcd, REPORT_DELTA, 1);
    if (retval < 0) {
        seq_printf(s, "Failed to read delta data\n");
        return;
    }

    syna_tcm_format_print(s, tcm_hcd, NULL);

    /*set normal doze*/
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 0);
    if (retval < 0){
        TPD_INFO("Failed to switch to normal\n");
    }
    return;
}

static void syna_baseline_read(struct seq_file *s, void *chip_data)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 1);
    if (retval < 0){
        TPD_INFO("Failed to exit doze\n");
    }

    msleep(20); // delay 20ms

    retval = syna_tcm_collect_reports(tcm_hcd, REPORT_RAW, 1);
    if (retval < 0) {
        seq_printf(s, "Failed to read baseline data\n");
        return;
    }

    syna_tcm_format_print(s, tcm_hcd, NULL);

    /*set normal doze*/
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 0);
    if (retval < 0){
        TPD_INFO("Failed to switch to normal\n");
    }

    return;
}

static void syna_tcm_drt_read(struct seq_file *s, void *chip_data)
{
    int retval;
    unsigned char out[2] = {0};
    unsigned char *resp_buf;
    unsigned int resp_buf_size;
    unsigned int resp_length;
    unsigned int frame_size;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    resp_buf = NULL;
    resp_buf_size = 0;

    if (tcm_hcd->id_info.mode != MODE_APPLICATION || tcm_hcd->app_status != APP_STATUS_OK) {
        seq_printf(s, "Not in app mode\n");
        return;
    }

    out[0] = TEST_DYNAMIC_RANGE;

    retval = syna_tcm_write_message(tcm_hcd,
            CMD_PRODUCTION_TEST,
            out,
            1,
            &resp_buf,
            &resp_buf_size,
            &resp_length,
            0);
    if (retval < 0) {
        seq_printf(s, "Failed to write %s\n", STR(CMD_PRODUCTION_TEST));
        goto exit;
    }

    frame_size = syna_tcm_get_frame_size_words(tcm_hcd, false);
    if (frame_size != resp_buf_size / 2) {
        seq_printf(s, "Size not match\n");
        goto exit;
    }

    syna_tcm_format_print(s, tcm_hcd, resp_buf);

exit:
    kfree(resp_buf);
    return;
}

static struct synaptics_proc_operations syna_proc_ops = {
    .auto_test     = syna_auto_test,
};

static void syna_reserve_read(struct seq_file *s, void *chip_data)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 1);
    if (retval < 0){
        TPD_INFO("Failed to exit doze\n");
    }

    msleep(20); // delay 20ms

    retval = syna_tcm_collect_reports(tcm_hcd, REPORT_DEBUG, 1);
    if (retval < 0) {
        seq_printf(s, "Failed to read delta data\n");
        return;
    }

    syna_tcm_format_print(s, tcm_hcd, NULL);

    /*set normal doze*/
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_NO_DOZE, 0);
    if (retval < 0){
        TPD_INFO("Failed to switch to normal\n");
    }

    return;
}

int freq_point = 0;
static void syna_freq_hop_trigger(void *chip_data)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    TPD_INFO("send cmd to tigger frequency hopping here!!!\n");

    freq_point = 1 - freq_point;
    retval = syna_tcm_set_dynamic_config(tcm_hcd, DC_FREQUENCE_HOPPING, freq_point);
    if (retval < 0){
        TPD_INFO("Failed to hop frequency\n");
    }

}

static struct debug_info_proc_operations syna_debug_proc_ops = {
    .delta_read    = syna_delta_read,
    .baseline_read = syna_baseline_read,
    .main_register_read = syna_main_register,
    .DRT           = syna_tcm_drt_read,
    .limit_read    = synaptics_limit_read,
    .reserve_read  = syna_reserve_read,
};

static int syna_device_report_touch(struct syna_tcm_hcd *tcm_hcd)
{
    int ret = syna_parse_report(tcm_hcd);
    if (ret < 0) {
        TPD_INFO("Failed to parse report\n");
        return -EINVAL;
    }

    syna_set_trigger_reason(tcm_hcd, IRQ_TOUCH);
    return 0;
}


void syna_tcm_hdl_done(struct syna_tcm_hcd *tcm_hcd)
{
    int ret = 0;

    TPD_DETAIL("%s: Enter\n", __func__);

    ret = syna_tcm_identify(tcm_hcd, true);
    if (ret < 0) {
        TPD_INFO("Failed to do identification\n");
        return;
    }

    ret = syna_get_default_report_config(tcm_hcd);
    if (ret < 0) {
        TPD_INFO("failed to get default report config\n");
    }

    ret = syna_tcm_normal_mode(tcm_hcd);
    if (ret < 0) {
        TPD_INFO("failed to set normal mode\n");
    }

    enable_irq(tcm_hcd->s_client->irq);

    //syna_tcm_get_app_info(tcm_hcd);

    syna_fw_version_update(tcm_hcd);

    g_tcm_hcd->hdl_finished_flag = 1;

    return;
}


static int syna_tcm_irq_handle(void *chip_data)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    if (zeroflash_init_done == 0) {
        TPD_INFO("hdl not ready, disable irq\n");
        disable_irq_nosync(tcm_hcd->s_client->irq);
        return retval;
    }

    syna_tcm_stop_reset_timer(tcm_hcd);

    retval =  syna_tcm_read_message(tcm_hcd, NULL, 0);
    if (retval == -ENXIO) {
        TPD_INFO("Failed to read message, start to do hdl\n");
        disable_irq_nosync(tcm_hcd->s_client->irq);
        if (tcm_hcd->first_tp_fw_update_done){
            g_tcm_hcd->hdl_finished_flag = 0;
            zeroflash_download_firmware();
        }
        return 0;
    }
    return retval;
}

static void syna_tcm_resume_timedout_operate(void *chip_data)
{
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    TPD_INFO("syna_tcm_resume_timedout_operate ENTER!!\n");
    if (!g_tcm_hcd->hdl_finished_flag) {
        disable_irq_nosync(tcm_hcd->s_client->irq);
        syna_reset_gpio(tcm_hcd, false);
        usleep_range(5000, 5000);
        syna_reset_gpio(tcm_hcd, true);
        msleep(20);
        enable_irq(tcm_hcd->s_client->irq);

        syna_tcm_start_reset_timer(tcm_hcd);
    } else {
        TPD_INFO("hdl has done, don't reset again!!\n");
    }
}

static void synaptics_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

        tcm_hcd->touch_direction = dir;
}

static uint8_t synaptics_get_touch_direction(void *chip_data)
{
        struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;

        return tcm_hcd->touch_direction;
}

static struct oplus_touchpanel_operations syna_tcm_ops = {
    .ftm_process       = syna_ftm_process,
    .get_vendor        = syna_get_vendor,
    .get_chip_info     = syna_get_chip_info,
    .get_touch_points  = syna_get_touch_points,
    .get_gesture_info  = syna_get_gesture_info,
    .power_control     = syna_power_control,
    .reset_gpio_control= syna_reset_gpio,
    .reset             = syna_tcm_reset,
    .trigger_reason    = syna_trigger_reason,
    .mode_switch       = syna_mode_switch,
    .irq_handle_unlock = syna_tcm_irq_handle,
    .fw_check          = syna_fw_check,
    .fw_update         = syna_tcm_fw_update,
    .async_work        = syna_tcm_async_work,
    //.write_ps_status   = syna_tcm_write_ps_status,
    .black_screen_test = syna_tcm_black_screen_test,
    .reinit_device     = syna_tcm_reinit_device,
    .resume_timedout_operate = syna_tcm_resume_timedout_operate,
    .set_touch_direction    = synaptics_set_touch_direction,
    .get_touch_direction    = synaptics_get_touch_direction,
    .freq_hop_trigger = syna_freq_hop_trigger,
};

/*
*Interface for lcd to wait tp resume before suspend
*/
void tp_wait_hdl_finished(void)
{
    int retry_cnt = 0;
    struct syna_tcm_hcd *tcm_hcd = g_tcm_hcd;

    if (!g_tcm_hcd) {
        return;
    }

    syna_tcm_stop_reset_timer(tcm_hcd);

    TPD_DEBUG("tp_wait_hdl_finished Begin\n");
    do
    {
        if(retry_cnt) {
            msleep(100);
        }
        retry_cnt++;
        TPD_DETAIL("Wait hdl finished retry %d times...  \n", retry_cnt);
    }
    while(!g_tcm_hcd->hdl_finished_flag && retry_cnt < 20);

    TPD_DEBUG("tp_wait_hdl_finished End\n");
}

/*
*Interface for lcd to control tp irq
*mode:0-esd 1-black gesture
*/
int tp_control_irq(bool enable, int mode)
{
    if (mode == 0){
        if (enable) {
            TPD_INFO("%s enable\n", __func__);
            esd_irq_disabled = 0;
            enable_irq(g_tcm_hcd->s_client->irq);
        } else {
            TPD_INFO("%s disable\n", __func__);
            g_tcm_hcd->response_code = STATUS_ERROR;
            atomic_set(&g_tcm_hcd->command_status, CMD_IDLE);
            complete(&response_complete);
            esd_irq_disabled = 1;
            wait_zeroflash_firmware_work();
            disable_irq_nosync(g_tcm_hcd->s_client->irq);
        }
    } else if (mode == 1) {
        if (enable) {
            TPD_INFO("%s enable\n", __func__);
            if (!g_tcm_hcd->irq_trigger_hdl_support) {
                enable_irq_wake(g_tcm_hcd->s_client->irq);
            }
        } else {
            TPD_INFO("%s disable\n", __func__);
            if (!g_tcm_hcd->irq_trigger_hdl_support) {
                disable_irq_wake(g_tcm_hcd->s_client->irq);
            } else {
                disable_irq_nosync(g_tcm_hcd->s_client->irq);
            }
        }
    }

    return 0;
}

//100ms
#define RESET_TIMEOUT_TIME 100 * 1000 * 1000

static void syna_reset_timeout_work(struct work_struct *work)
{
    struct syna_tcm_hcd *tcm_hcd = g_tcm_hcd;

    TPD_INFO("%s reset timeout work\n", __func__);
    disable_irq_nosync(tcm_hcd->s_client->irq);
    syna_reset_gpio(tcm_hcd, false);
    usleep_range(5000, 5000);
    syna_reset_gpio(tcm_hcd, true);
    msleep(20);
    enable_irq(tcm_hcd->s_client->irq);
    return;
}

void syna_tcm_start_reset_timer(struct syna_tcm_hcd *tcm_hcd)
{
    if (tcm_hcd->reset_watchdog_running == 0) {
        TPD_DETAIL("%s hrtimer_start!!\n", __func__);

        hrtimer_start(&tcm_hcd->watchdog,
                ktime_set(0, RESET_TIMEOUT_TIME),
                HRTIMER_MODE_REL);
        tcm_hcd->reset_watchdog_running = 1;
    }

}

void syna_tcm_stop_reset_timer(struct syna_tcm_hcd *tcm_hcd)
{
    if (tcm_hcd->reset_watchdog_running == 1) {
        TPD_DETAIL("%s hrtimer_cancel!!\n", __func__);
        hrtimer_cancel(&tcm_hcd->watchdog);
        tcm_hcd->reset_watchdog_running = 0;
    }
}

static enum hrtimer_restart syna_tcm_reset_timeout(struct hrtimer *timer)
{
    struct syna_tcm_hcd *tcm_hcd = g_tcm_hcd;

    schedule_work(&(tcm_hcd->timeout_work));
    hrtimer_forward_now(&tcm_hcd->watchdog, ktime_set(0, RESET_TIMEOUT_TIME));
    return HRTIMER_RESTART;  //restart the timer
}

static int syna_tcm_init_device(struct syna_tcm_hcd *tcm_hcd)
{
    int retval = 0;

    mutex_init(&tcm_hcd->reset_mutex);
    mutex_init(&tcm_hcd->rw_ctrl_mutex);
    mutex_init(&tcm_hcd->command_mutex);
    mutex_init(&tcm_hcd->identify_mutex);
    mutex_init(&tcm_hcd->io_ctrl_mutex);

    INIT_BUFFER(tcm_hcd->in, false);
    INIT_BUFFER(tcm_hcd->out, false);
    INIT_BUFFER(tcm_hcd->resp, true);
    INIT_BUFFER(tcm_hcd->temp, false);
    INIT_BUFFER(tcm_hcd->config, false);
    INIT_BUFFER(tcm_hcd->default_config, false);
    INIT_BUFFER(tcm_hcd->report.buffer, true);

    hrtimer_init(&tcm_hcd->watchdog, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    tcm_hcd->watchdog.function = syna_tcm_reset_timeout;
    INIT_WORK(&tcm_hcd->timeout_work, syna_reset_timeout_work);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &tcm_hcd->in,
            tcm_hcd->read_length + 2);
    TPD_INFO("%s read_length:%d\n", __func__, tcm_hcd->read_length);
    if (retval < 0) {
        TPD_INFO("Failed to allocate memory for tcm_hcd->in.buf\n");
        return retval;
    }

    tcm_hcd->helper_workqueue = create_singlethread_workqueue("syna_tcm_helper");
    INIT_WORK(&tcm_hcd->helper_work, syna_tcm_helper_work);

    tcm_hcd->touch_hcd = (struct touch_hcd *)kzalloc(sizeof(struct touch_hcd), GFP_KERNEL);
    if (!tcm_hcd->touch_hcd) {
        retval = -ENOMEM;
        return retval;
    }

    tcm_hcd->touch_hcd->touch_data.object_data =
                        (struct object_data *)kzalloc(sizeof(struct object_data)*tcm_hcd->max_touch_num, GFP_KERNEL);
    if (!tcm_hcd->touch_hcd->touch_data.object_data) {
        retval = -ENOMEM;
        goto free_touch;
    }

    tcm_hcd->touch_hcd->max_objects = tcm_hcd->max_touch_num;
    mutex_init(&tcm_hcd->touch_hcd->report_mutex);

    INIT_BUFFER(tcm_hcd->touch_hcd->out, false);
    INIT_BUFFER(tcm_hcd->touch_hcd->resp, false);

    tcm_hcd->test_hcd = (struct syna_tcm_test *)kzalloc(sizeof(struct syna_tcm_test), GFP_KERNEL);
    if (!tcm_hcd->test_hcd) {
        retval = -ENOMEM;
        goto free_object_data;
    }

    INIT_BUFFER(tcm_hcd->test_hcd->report, false);

    return retval;

free_object_data:
    kfree(tcm_hcd->touch_hcd->touch_data.object_data);
free_touch:
    kfree(tcm_hcd->touch_hcd);

    return retval;
}

static void syna_tcm_parse_dts(struct syna_tcm_hcd *tcm_hcd, struct spi_device *spi)
{
    int rc;
    int temp = 0;
    int temp_array[2];
    struct device *dev;
    struct device_node *np;

    dev = &spi->dev;
    np = dev->of_node;
    rc = of_property_read_u32_array(np, "synaptics,grip-darkzone-area", temp_array, 2);
    if (rc) {
        tcm_hcd->grip_darkzone_x = 0x3C;
        tcm_hcd->grip_darkzone_y = 0xFA;
    }else{
        tcm_hcd->grip_darkzone_x = temp_array[0];
        tcm_hcd->grip_darkzone_y = temp_array[1];
    }

    rc = of_property_read_u32(np, "synaptics,max_speed_hz", &temp);
    if (rc) {
        TPD_INFO("synaptics,max_speed_hz not specified\n");
    } else {
        tcm_hcd->s_client->max_speed_hz = temp;
        TPD_INFO("max_speed_hz set to %d\n", tcm_hcd->s_client->max_speed_hz);
    }

    tcm_hcd->irq_trigger_hdl_support = of_property_read_bool(np, "synaptics,irq_trigger_hdl_support");
}

static int syna_tcm_spi_probe(struct spi_device *spi)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd;
    struct touchpanel_data *ts = NULL;
    struct device_hcd *device_hcd;
    //struct zeroflash_hcd *zeroflash_hcd;

    TPD_INFO("%s: enter\n", __func__);

    tcm_hcd = kzalloc(sizeof(*tcm_hcd), GFP_KERNEL);
    if (!tcm_hcd) {
        TPD_INFO("no more memory\n");
        return -ENOMEM;
    }

    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("failed to alloc common data\n");
        retval = -1;
        goto ts_alloc_failed;
    }

    g_tcm_hcd = tcm_hcd;
    tcm_hcd->s_client = spi;
    tcm_hcd->hw_res = &ts->hw_res;
    tcm_hcd->rd_chunk_size = RD_CHUNK_SIZE;
    tcm_hcd->wr_chunk_size = WR_CHUNK_SIZE;
    tcm_hcd->read_length = MIN_READ_LENGTH;
    tcm_hcd->max_touch_num = 10; // default
    tcm_hcd->syna_ops = &syna_proc_ops;
    tcm_hcd->ubl_addr = 0x2c;
    tcm_hcd->write_message = syna_tcm_write_message_zeroflash;
    tcm_hcd->first_tp_fw_update_done = false;
    tcm_hcd->using_fae = false;
    ts->chip_data = tcm_hcd;
    ts->ts_ops = &syna_tcm_ops;
    ts->debug_info_ops = &syna_debug_proc_ops;
    ts->dev = &spi->dev;
    ts->s_client = spi;
    ts->s_client->mode = SPI_MODE_3;
    ts->s_client->bits_per_word = 8;

    ts->irq = spi->irq;
    ts->irq_flags_cover = 0x2008;
    ts->has_callback = true;
    ts->use_resume_notify = true;
    tcm_hcd->in_suspend = &ts->is_suspended;
    tcm_hcd->init_okay = false;
    spi_set_drvdata(spi, ts);
    retval = syna_tcm_init_device(tcm_hcd);
    if (retval < 0) {
        TPD_INFO("Failed to init device information\n");
        goto err_alloc_mem;
    }

    atomic_set(&tcm_hcd->command_status, CMD_IDLE);

    syna_tcm_parse_dts(tcm_hcd, spi);
    retval = spi_setup(spi);
    if (retval < 0) {
        TPD_INFO("Failed to set up SPI protocol driver\n");
        return retval;
    }
    zeroflash_check_uboot(tcm_hcd);

    retval = register_common_touch_device(ts);
    if (retval < 0 && (retval != -EFTM)) {
        TPD_INFO("Failed to init device information\n");
        goto err_register_driver;
    }
    tcm_hcd->p_firmware_headfile = &ts->panel_data.firmware_headfile;
    tcm_hcd->health_monitor_support = ts->health_monitor_support;
    if (tcm_hcd->health_monitor_support) {
        tcm_hcd->monitor_data = &ts->monitor_data;
    }
    ts->int_mode = 1;
    zeroflash_check_uboot(tcm_hcd);

    ts->tp_suspend_order = LCD_TP_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
    ts->skip_reset_in_resume = true;
    ts->skip_suspend_operate = true;
    ts->mode_switch_type = SINGLE;
    if (!ts->irq_trigger_hdl_support) {
        ts->irq_trigger_hdl_support = tcm_hcd->irq_trigger_hdl_support;
    }

    synaptics_create_proc(ts, tcm_hcd->syna_ops);
    init_completion(&tcm_hcd->config_complete);

    device_hcd = syna_td4320_nf_remote_device_init(tcm_hcd);
    if (device_hcd) {
        device_hcd->irq = tcm_hcd->s_client->irq;
        device_hcd->read_message = syna_tcm_read_message;
        device_hcd->write_message = syna_tcm_write_message;
        device_hcd->reset = syna_tcm_reset;
        device_hcd->report_touch = syna_device_report_touch;
    }
    tcm_hcd->init_okay = false;
    g_tcm_hcd->hdl_finished_flag = 0;

    zeroflash_check_uboot(tcm_hcd);
    tcm_hcd->init_okay = true;
    syna_remote_zeroflash_init(tcm_hcd);

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (ts->boot_mode == RECOVERY_BOOT)
#else
    if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY)
#endif
    {
        TPD_INFO("In Recovery mode, no-flash download fw by headfile\n");
        syna_tcm_fw_update(tcm_hcd, NULL, 0);
    }

    if (is_oem_unlocked()) {
        TPD_INFO("Replace system image for cts, download fw by headfile\n");
        syna_tcm_fw_update(tcm_hcd, NULL, 0);
    }

    return 0;

err_alloc_mem:
    RELEASE_BUFFER(tcm_hcd->report.buffer);
    RELEASE_BUFFER(tcm_hcd->config);
    RELEASE_BUFFER(tcm_hcd->temp);
    RELEASE_BUFFER(tcm_hcd->resp);
    RELEASE_BUFFER(tcm_hcd->out);
    RELEASE_BUFFER(tcm_hcd->in);

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

ts_alloc_failed:
    kfree(tcm_hcd);

    return retval;
}

static int syna_tcm_remove(struct spi_device *spi)
{
    struct touchpanel_data *ts = spi_get_drvdata(spi);
    struct syna_tcm_hcd *tcm_hcd =  (struct syna_tcm_hcd *)ts->chip_data;

    RELEASE_BUFFER(tcm_hcd->report.buffer);
    RELEASE_BUFFER(tcm_hcd->config);
    RELEASE_BUFFER(tcm_hcd->temp);
    RELEASE_BUFFER(tcm_hcd->resp);
    RELEASE_BUFFER(tcm_hcd->out);
    RELEASE_BUFFER(tcm_hcd->in);

    kfree(tcm_hcd);
    kfree(ts);

    return 0;
}

static struct of_device_id syna_match_table[] = {
    { .compatible = "oplus,tp_noflash",},
    { }
};

static int syna_i2c_suspend(struct device *dev){
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    TPD_INFO("%s: is called\n", __func__);
    tp_i2c_suspend(ts);
    return 0;
}

static int syna_i2c_resume(struct device *dev){
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);
    tp_i2c_resume(ts);
    return 0;
}

static const struct dev_pm_ops syna_pm_ops = {
#ifdef CONFIG_FB
    .suspend = syna_i2c_suspend,
    .resume = syna_i2c_resume,
#endif
};

static const struct spi_device_id syna_tmc_id[] = {
    { TPD_DEVICE, 0 },
    { }
};

static struct spi_driver syna_spi_driver = {
    .probe      = syna_tcm_spi_probe,
    .remove     = syna_tcm_remove,
    .id_table   = syna_tmc_id,
    .driver     = {
        .name   = TPD_DEVICE,
        .of_match_table =  syna_match_table,
        .pm = &syna_pm_ops,
    },
};


static int __init syna_tcm_module_init(void)
{

    TPD_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE))
        return -1;

    get_oem_verified_boot_state();
    if (spi_register_driver(&syna_spi_driver)!= 0) {
        TPD_INFO("unable to add spi driver.\n");
        return -1;
    }

    return 0;
}

static void __exit syna_tcm_module_exit(void)
{
    spi_unregister_driver(&syna_spi_driver);
    return;
}

module_init(syna_tcm_module_init);
module_exit(syna_tcm_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics TCM Touch Driver");
MODULE_LICENSE("GPL v2");
