// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/crc32.h>
#include <linux/firmware.h>
#include "synaptics_tcm_core.h"

#define FW_IMAGE_NAME "synaptics/hdl_firmware.img"

#define BOOT_CONFIG_ID "BOOT_CONFIG"

#define F35_APP_CODE_ID "F35_APP_CODE"

#define APP_CONFIG_ID "APP_CONFIG"

#define DISP_CONFIG_ID "DISPLAY"

#define IMAGE_FILE_MAGIC_VALUE 0x4818472b

#define FLASH_AREA_MAGIC_VALUE 0x7c05e516

#define PDT_START_ADDR 0x00e9

#define PDT_END_ADDR 0x00ee

#define UBL_FN_NUMBER 0x35

#define F35_CTRL3_OFFSET 18

#define F35_CTRL7_OFFSET 22

#define F35_WRITE_FW_TO_PMEM_COMMAND 4

#define RESET_TO_HDL_DELAY_MS 12

#ifdef CONFIG_TOUCHPANEL_SYNAPTICS_TD4330_NOFLASH
#define DOWNLOAD_APP_FAST_RETRY

#define DOWNLOAD_RETRY_COUNT 50
#else
#define DOWNLOAD_RETRY_COUNT 10
#endif

enum f35_error_code {
    SUCCESS = 0,
    UNKNOWN_FLASH_PRESENT,
    MAGIC_NUMBER_NOT_PRESENT,
    INVALID_BLOCK_NUMBER,
    BLOCK_NOT_ERASED,
    NO_FLASH_PRESENT,
    CHECKSUM_FAILURE,
    WRITE_FAILURE,
    INVALID_COMMAND,
    IN_DEBUG_MODE,
    INVALID_HEADER,
    REQUESTING_FIRMWARE,
    INVALID_CONFIGURATION,
    DISABLE_BLOCK_PROTECT_FAILURE,
};

enum config_download {
    HDL_INVALID = 0,
    HDL_TOUCH_CONFIG_TO_PMEM,
    HDL_DISPLAY_CONFIG_TO_PMEM,
    HDL_DISPLAY_CONFIG_TO_RAM,
};

struct zeroflash_hcd *g_zeroflash_hcd = NULL;

int zeroflash_init_done = 0;
int check_uboot_failed_count = 0;

#if 0
#ifdef DOWNLOAD_APP_FAST_RETRY
static int zeroflash_check_f35(void)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    unsigned char fn_number;
    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            PDT_END_ADDR,
            &fn_number,
            sizeof(fn_number));
    if (fn_number != UBL_FN_NUMBER) {
        TPD_INFO("Failed to find F$35\n");
        return -ENODEV;
    } else {
        return 0;
    }
}
#endif
#endif

static int zeroflash_check_uboot(void)
{
    int retval;
    unsigned char fn_number;
    struct rmi_f35_query query;
    struct rmi_pdt_entry p_entry;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;

    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            PDT_END_ADDR,
            &fn_number,
            sizeof(fn_number));
    if (retval < 0) {
        TPD_INFO(
                "Failed to read RMI function number\n");
        goto check_uboot_failed;
    }

    TPD_DETAIL(
            "Found F$%02x\n",
            fn_number);

    if (fn_number != UBL_FN_NUMBER) {
        TPD_INFO(
                "Failed to find F$35, but F$%02x\n", fn_number);
        retval = -ENODEV;
        goto check_uboot_failed;
    }

    if (g_zeroflash_hcd->f35_ready) {
        check_uboot_failed_count = 0;
        return 0;
    }

    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            PDT_START_ADDR,
            (unsigned char *)&p_entry,
            sizeof(p_entry));
    if (retval < 0) {
        TPD_INFO("Failed to read PDT entry\n");
        goto check_uboot_failed;
    }

    g_zeroflash_hcd->f35_addr.query_base = p_entry.query_base_addr;
    g_zeroflash_hcd->f35_addr.command_base = p_entry.command_base_addr;
    g_zeroflash_hcd->f35_addr.control_base = p_entry.control_base_addr;
    g_zeroflash_hcd->f35_addr.data_base = p_entry.data_base_addr;

    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            g_zeroflash_hcd->f35_addr.query_base,
            (unsigned char *)&query,
            sizeof(query));
    if (retval < 0) {
        TPD_INFO("Failed to read F$35 query\n");
        goto check_uboot_failed;
    }

    g_zeroflash_hcd->f35_ready = true;

    if (query.has_query2 && query.has_ctrl7 && query.has_host_download) {
        g_zeroflash_hcd->has_hdl = true;
    } else {
        TPD_INFO("Host download not supported\n");
        g_zeroflash_hcd->has_hdl = false;
        retval = -ENODEV;
        goto check_uboot_failed;
    }

    check_uboot_failed_count = 0;
    return 0;
check_uboot_failed:
    check_uboot_failed_count++;
    if (check_uboot_failed_count > 1) {
        syna_reset_gpio(tcm_hcd, false);
        usleep_range(5000, 5000);
        syna_reset_gpio(tcm_hcd, true);
        msleep(20);
        check_uboot_failed_count = 0;
    }
    return retval;
}

int zeroflash_parse_fw_image(void)
{
    unsigned int idx;
    unsigned int addr;
    unsigned int offset;
    unsigned int length;
    unsigned int checksum;
    unsigned int flash_addr;
    unsigned int magic_value;
    unsigned int num_of_areas;
    struct zeroflash_image_header *header;
    struct image_info *image_info;
    struct area_descriptor *descriptor;
    //struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    const unsigned char *image;
    const unsigned char *content;

    image = g_zeroflash_hcd->image;
    image_info = &g_zeroflash_hcd->image_info;
    header = (struct zeroflash_image_header *)image;

    magic_value = le4_to_uint(header->magic_value);
    if (magic_value != IMAGE_FILE_MAGIC_VALUE) {
        TPD_INFO(
                "Invalid image file magic value\n");
        return -EINVAL;
    }

    memset(image_info, 0x00, sizeof(*image_info));

    offset = sizeof(*header);
    num_of_areas = le4_to_uint(header->num_of_areas);

    for (idx = 0; idx < num_of_areas; idx++) {
        addr = le4_to_uint(image + offset);
        descriptor = (struct area_descriptor *)(image + addr);
        offset += 4;

        magic_value = le4_to_uint(descriptor->magic_value);
        if (magic_value != FLASH_AREA_MAGIC_VALUE)
            continue;

        length = le4_to_uint(descriptor->length);
        content = (unsigned char *)descriptor + sizeof(*descriptor);
        flash_addr = le4_to_uint(descriptor->flash_addr_words) * 2;
        checksum = le4_to_uint(descriptor->checksum);

        if (0 == strncmp((char *)descriptor->id_string,
                BOOT_CONFIG_ID,
                strlen(BOOT_CONFIG_ID))) {
            if (checksum != (crc32(~0, content, length) ^ ~0)) {
                TPD_INFO(
                        "Boot config checksum error\n");
                return -EINVAL;
            }
            image_info->boot_config.size = length;
            image_info->boot_config.data = content;
            image_info->boot_config.flash_addr = flash_addr;
            TPD_INFO(
                    "Boot config size = %d\n",
                    length);
            TPD_INFO(
                    "Boot config flash address = 0x%08x\n",
                    flash_addr);
        } else if (0 == strncmp((char *)descriptor->id_string,
                F35_APP_CODE_ID,
                strlen(F35_APP_CODE_ID))) {
            if (checksum != (crc32(~0, content, length) ^ ~0)) {
                TPD_INFO(
                        "Application firmware checksum error\n");
                return -EINVAL;
            }
            image_info->app_firmware.size = length;
            image_info->app_firmware.data = content;
            image_info->app_firmware.flash_addr = flash_addr;
            TPD_INFO(
                    "Application firmware size = %d\n",
                    length);
            TPD_INFO(
                    "Application firmware flash address = 0x%08x\n",
                    flash_addr);
        } else if (0 == strncmp((char *)descriptor->id_string,
                APP_CONFIG_ID,
                strlen(APP_CONFIG_ID))) {
            if (checksum != (crc32(~0, content, length) ^ ~0)) {
                TPD_INFO(
                        "Application config checksum error\n");
                return -EINVAL;
            }
            image_info->app_config.size = length;
            image_info->app_config.data = content;
            image_info->app_config.flash_addr = flash_addr;
            image_info->packrat_number = le4_to_uint(&content[14]);
            TPD_INFO(
                    "Application config size = %d\n",
                    length);
            TPD_INFO(
                    "Application config flash address = 0x%08x\n",
                    flash_addr);
        } else if (0 == strncmp((char *)descriptor->id_string,
                DISP_CONFIG_ID,
                strlen(DISP_CONFIG_ID))) {
            if (checksum != (crc32(~0, content, length) ^ ~0)) {
                TPD_INFO(
                        "Display config checksum error\n");
                return -EINVAL;
            }
            image_info->disp_config.size = length;
            image_info->disp_config.data = content;
            image_info->disp_config.flash_addr = flash_addr;
            TPD_INFO(
                    "Display config size = %d\n",
                    length);
            TPD_INFO(
                    "Display config flash address = 0x%08x\n",
                    flash_addr);
        }
    }

    return 0;
}

static int zeroflash_get_fw_image(void)
{
    int retval = 0;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    struct touchpanel_data *ts = spi_get_drvdata(tcm_hcd->s_client);
    //add for fae
    char *p_node = NULL;
    char *fw_name_fae = NULL;
    char *postfix = "_FAE";
    uint8_t copy_len = 0;
    //add for fae

    if (g_zeroflash_hcd->fw_entry != NULL)
        return 0;

    do {
        if (tcm_hcd->using_fae) {
            fw_name_fae = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
            if(fw_name_fae == NULL) {
                TPD_INFO("fw_name_fae kzalloc error!\n");
            } else {
                p_node  = strstr(ts->panel_data.fw_name, ".");
                if (p_node) {
                    copy_len = p_node - ts->panel_data.fw_name;
                    memcpy(fw_name_fae, ts->panel_data.fw_name, copy_len);
                    strlcat(fw_name_fae, postfix, MAX_FW_NAME_LENGTH);
                    strlcat(fw_name_fae, p_node, MAX_FW_NAME_LENGTH);
                    //memcpy(private_ts->panel_data.fw_name, fw_name_fae, MAX_FW_NAME_LENGTH);
                    //TPD_INFO("update fw_name to %s\n", fw_name_fae);
                    retval = request_firmware(&g_zeroflash_hcd->fw_entry,  fw_name_fae, &tcm_hcd->s_client->dev);
                }
            }
        }
        if(fw_name_fae == NULL){
            if(ts->fw_update_app_support) {
                retval = request_firmware_select(&g_zeroflash_hcd->fw_entry,
                        g_zeroflash_hcd->fw_name,
                        &tcm_hcd->s_client->dev);
            } else {
                retval = request_firmware(&g_zeroflash_hcd->fw_entry,
                        g_zeroflash_hcd->fw_name,
                        &tcm_hcd->s_client->dev);
            }
        } else {
            kfree(fw_name_fae);
            fw_name_fae = NULL;
        }
        if (retval < 0) {
            TPD_INFO(
                    "Failed to request %s\n",
                    g_zeroflash_hcd->fw_name);
            msleep(100);
        } else {
            break;
        }
    } while (1);

    if (g_zeroflash_hcd->fw_entry != NULL) {
        TPD_INFO(
            "Firmware image size = %d\n",
            (unsigned int)g_zeroflash_hcd->fw_entry->size);

        g_zeroflash_hcd->image = g_zeroflash_hcd->fw_entry->data;
    }

    retval = zeroflash_parse_fw_image();
    if (retval < 0) {
        TPD_INFO(
            "Failed to parse firmware image\n");
        if (g_zeroflash_hcd->fw_entry != NULL){
            release_firmware(g_zeroflash_hcd->fw_entry);
            g_zeroflash_hcd->fw_entry = NULL;
            g_zeroflash_hcd->image = NULL;
        }
        return retval;
    }

    return 0;
}

void zeroflash_check_download_config(void)
{
    struct firmware_status *fw_status;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;

    fw_status = &g_zeroflash_hcd->fw_status;

    TPD_DETAIL("fw status:need app:%d need display:%d\n", g_zeroflash_hcd->fw_status.need_app_config,
        g_zeroflash_hcd->fw_status.need_disp_config);
    if (!fw_status->need_app_config && !fw_status->need_disp_config) {
        /*
        if (atomic_read(&tcm_hcd->helper.task) == HELP_NONE) {
            atomic_set(&tcm_hcd->helper.task,
                    HELP_SEND_RESET_NOTIFICATION);
            queue_work(tcm_hcd->helper.workqueue,
                    &tcm_hcd->helper.work);
        }*/
        TPD_INFO(
                "zero reflash done..............\n");

        atomic_set(&tcm_hcd->host_downloading, 0);
        syna_tcm_hdl_done(tcm_hcd);
        if (tcm_hcd->cb.async_work)
               tcm_hcd->cb.async_work();
        complete(&tcm_hcd->config_complete);
        //wake_up_interruptible(&tcm_hcd->hdl_wq);
        return;
    }
    //msleep(50);
    queue_work(g_zeroflash_hcd->config_workqueue, &g_zeroflash_hcd->config_work);

    return;
}
void zeroflash_download_config(void)
{
    int retval;
    struct firmware_status *fw_status;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;

    fw_status = &g_zeroflash_hcd->fw_status;
    retval = secure_memcpy((unsigned char*)fw_status, sizeof(g_zeroflash_hcd->fw_status),
        tcm_hcd->report.buffer.buf,
        tcm_hcd->report.buffer.buf_size,
        sizeof(g_zeroflash_hcd->fw_status));
    if (retval < 0) {
        TPD_INFO("Failed to copy fw status\n");
    }

    TPD_INFO("zeroflash_download_config fw status:need app:%d need display:%d\n", g_zeroflash_hcd->fw_status.need_app_config,
        g_zeroflash_hcd->fw_status.need_disp_config);
    if (!fw_status->need_app_config && !fw_status->need_disp_config) {
        /*
        if (atomic_read(&tcm_hcd->helper.task) == HELP_NONE) {
            atomic_set(&tcm_hcd->helper.task,
                    HELP_SEND_RESET_NOTIFICATION);
            queue_work(tcm_hcd->helper.workqueue,
                    &tcm_hcd->helper.work);
        }*/
        TPD_INFO(
                "zero reflash done..............\n");
        atomic_set(&tcm_hcd->host_downloading, 0);
        //wake_up_interruptible(&tcm_hcd->hdl_wq);
        return;
    }
    queue_work(g_zeroflash_hcd->config_workqueue, &g_zeroflash_hcd->config_work);

    return;
}

void zeroflash_download_firmware(void)
{
    if (zeroflash_init_done){
        if (zeroflash_check_uboot() < 0) {
            if (g_zeroflash_hcd->tcm_hcd->health_monitor_support) {
                g_zeroflash_hcd->tcm_hcd->monitor_data->reserve1++;
            }
            TPD_INFO(
                    "uboot check fail\n");
            msleep(50);
            enable_irq(g_zeroflash_hcd->tcm_hcd->s_client->irq);
            return;
        }

        queue_work(g_zeroflash_hcd->firmware_workqueue, &g_zeroflash_hcd->firmware_work);
    } else {
    }
    return;
}

static int zeroflash_download_disp_config(void)
{
    int retval;
    unsigned char response_code;
    struct image_info *image_info;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    static unsigned int retry_count;

    TPD_DETAIL(
            "Downloading display config\n");

    image_info = &g_zeroflash_hcd->image_info;

    if (image_info->disp_config.size == 0) {
        TPD_INFO(
                "No display config in image file\n");
        return -EINVAL;
    }

    LOCK_BUFFER(g_zeroflash_hcd->out);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &g_zeroflash_hcd->out,
            image_info->disp_config.size + 2);
    if (retval < 0) {
        TPD_INFO(
                "Failed to allocate memory for zeroflash_hcd->out.buf\n");
        goto unlock_out;
    }

    g_zeroflash_hcd->out.buf[0] = 1;
    g_zeroflash_hcd->out.buf[1] = HDL_DISPLAY_CONFIG_TO_PMEM;

    retval = secure_memcpy(&g_zeroflash_hcd->out.buf[2],
            g_zeroflash_hcd->out.buf_size - 2,
            image_info->disp_config.data,
            image_info->disp_config.size,
            image_info->disp_config.size);
    if (retval < 0) {
        TPD_INFO(
                "Failed to copy display config data\n");
        goto unlock_out;
    }

    g_zeroflash_hcd->out.data_length = image_info->disp_config.size + 2;

    LOCK_BUFFER(g_zeroflash_hcd->resp);

    retval = tcm_hcd->write_message(tcm_hcd,
            CMD_DOWNLOAD_CONFIG,
            g_zeroflash_hcd->out.buf,
            g_zeroflash_hcd->out.data_length,
            &g_zeroflash_hcd->resp.buf,
            &g_zeroflash_hcd->resp.buf_size,
            &g_zeroflash_hcd->resp.data_length,
            &response_code,
            0);
    if (retval < 0) {
        TPD_INFO(
                "Failed to write command %s\n",
                STR(CMD_DOWNLOAD_CONFIG));
        if (response_code != STATUS_ERROR)
            goto unlock_resp;
        retry_count++;
        if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT)
            goto unlock_resp;
    } else {
        retry_count = 0;
    }

    retval = secure_memcpy((unsigned char *)&g_zeroflash_hcd->fw_status,
            sizeof(g_zeroflash_hcd->fw_status),
            g_zeroflash_hcd->resp.buf,
            g_zeroflash_hcd->resp.buf_size,
            sizeof(g_zeroflash_hcd->fw_status));
    TPD_INFO("zeroflash_download_disp_config fw status:need app:%d need display:%d\n", g_zeroflash_hcd->fw_status.need_app_config,
        g_zeroflash_hcd->fw_status.need_disp_config);
    
    if (retval < 0) {
        TPD_INFO(
                "Failed to copy firmware status\n");
        goto unlock_resp;
    }

    TPD_INFO("Display config downloaded\n");

    retval = 0;

unlock_resp:
    UNLOCK_BUFFER(g_zeroflash_hcd->resp);

unlock_out:
    UNLOCK_BUFFER(g_zeroflash_hcd->out);

    return retval;
}

static int zeroflash_download_app_config(void)
{
    int retval;
    unsigned char padding;
    unsigned char response_code;
    struct image_info *image_info;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    static unsigned int retry_count;

    TPD_DETAIL("Downloading application config\n");

    image_info = &g_zeroflash_hcd->image_info;

    if (image_info->app_config.size == 0) {
        TPD_INFO(
                "No application config in image file\n");
        return -EINVAL;
    }

    padding = image_info->app_config.size % 8;
    if (padding)
        padding = 8 - padding;

    LOCK_BUFFER(g_zeroflash_hcd->out);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &g_zeroflash_hcd->out,
            image_info->app_config.size + 2 + padding);
    if (retval < 0) {
        TPD_INFO(
                "Failed to allocate memory for zeroflash_hcd->out.buf\n");
        goto unlock_out;
    }

    g_zeroflash_hcd->out.buf[0] = 1;
    g_zeroflash_hcd->out.buf[1] = HDL_TOUCH_CONFIG_TO_PMEM;

    retval = secure_memcpy(&g_zeroflash_hcd->out.buf[2],
            g_zeroflash_hcd->out.buf_size - 2,
            image_info->app_config.data,
            image_info->app_config.size,
            image_info->app_config.size);
    if (retval < 0) {
        TPD_INFO(
                "Failed to copy application config data\n");
        goto unlock_out;
    }

    g_zeroflash_hcd->out.data_length = image_info->app_config.size + 2;
    g_zeroflash_hcd->out.data_length += padding;

    LOCK_BUFFER(g_zeroflash_hcd->resp);

    retval = tcm_hcd->write_message(tcm_hcd,
            CMD_DOWNLOAD_CONFIG,
            g_zeroflash_hcd->out.buf,
            g_zeroflash_hcd->out.data_length,
            &g_zeroflash_hcd->resp.buf,
            &g_zeroflash_hcd->resp.buf_size,
            &g_zeroflash_hcd->resp.data_length,
            &response_code,
            0);
    if (retval < 0) {
        TPD_INFO(
                "Failed to write command %s\n",
                STR(CMD_DOWNLOAD_CONFIG));
        if (response_code != STATUS_ERROR)
            goto unlock_resp;
        retry_count++;
        if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT)
            goto unlock_resp;
    } else {
        retry_count = 0;
    }

    retval = secure_memcpy((unsigned char *)&g_zeroflash_hcd->fw_status,
            sizeof(g_zeroflash_hcd->fw_status),
            g_zeroflash_hcd->resp.buf,
            g_zeroflash_hcd->resp.buf_size,
            sizeof(g_zeroflash_hcd->fw_status));

    
    TPD_INFO("zeroflash_download_app_config fw status:need app:%d need display:%d\n", g_zeroflash_hcd->fw_status.need_app_config,
        g_zeroflash_hcd->fw_status.need_disp_config);

    if (retval < 0) {
        TPD_INFO(
                "Failed to copy firmware status\n");
        goto unlock_resp;
    }

    TPD_INFO("Application config downloaded\n");

    retval = 0;

unlock_resp:
    UNLOCK_BUFFER(g_zeroflash_hcd->resp);

unlock_out:
    UNLOCK_BUFFER(g_zeroflash_hcd->out);

    return retval;
}

static void zeroflash_download_config_work(struct work_struct *work)
{
    int retval;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;

    retval = zeroflash_get_fw_image();
    if (retval < 0) {
        TPD_INFO(
                "Failed to get firmware image\n");
        return;
    }

    TPD_DETAIL("Start of config download\n");
    TPD_DETAIL("fw status:need app:%d need display:%d\n", g_zeroflash_hcd->fw_status.need_app_config,
        g_zeroflash_hcd->fw_status.need_disp_config);
    if (g_zeroflash_hcd->fw_status.need_app_config) {
        retval = zeroflash_download_app_config();
        if (retval < 0) {
            if (tcm_hcd->health_monitor_support) {
                tcm_hcd->monitor_data->reserve2++;
            }
            TPD_INFO(
                    "Failed to download application config\n");
            return;
        }
        goto exit;
    }

    if (g_zeroflash_hcd->fw_status.need_disp_config) {
        retval = zeroflash_download_disp_config();
        if (retval < 0) {
            if (tcm_hcd->health_monitor_support) {
                tcm_hcd->monitor_data->reserve2++;
            }
            TPD_INFO(
                    "Failed to download display config\n");
            return;
        }
        goto exit;
    }

exit:
    TPD_DETAIL("End of config download\n");

    zeroflash_check_download_config();
    //zeroflash_download_config();

    return;
}

static int zeroflash_download_app_fw(void)
{
    int retval;
    unsigned char command;
    struct image_info *image_info;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
#if RESET_TO_HDL_DELAY_MS
    //const struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
#endif

#ifdef DOWNLOAD_APP_FAST_RETRY
    unsigned char tmp_buf[256] = {0};
    int retry_cnt = 0;
#endif

    TPD_DETAIL("Downloading application firmware\n");

    image_info = &g_zeroflash_hcd->image_info;

    if (image_info->app_firmware.size == 0) {
        TPD_INFO(
                "No application firmware in image file\n");
        return -EINVAL;
    }

    LOCK_BUFFER(g_zeroflash_hcd->out);

    retval = syna_tcm_alloc_mem(tcm_hcd,
            &g_zeroflash_hcd->out,
            image_info->app_firmware.size);
    if (retval < 0) {
        TPD_INFO(
                "Failed to allocate memory for zeroflash_hcd->out.buf\n");
        UNLOCK_BUFFER(g_zeroflash_hcd->out);
        return retval;
    }

    retval = secure_memcpy(g_zeroflash_hcd->out.buf,
            g_zeroflash_hcd->out.buf_size,
            image_info->app_firmware.data,
            image_info->app_firmware.size,
            image_info->app_firmware.size);
    if (retval < 0) {
        TPD_INFO(
                "Failed to copy application firmware data\n");
        UNLOCK_BUFFER(g_zeroflash_hcd->out);
        return retval;
    }

    g_zeroflash_hcd->out.data_length = image_info->app_firmware.size;

    command = F35_WRITE_FW_TO_PMEM_COMMAND;

#ifdef DOWNLOAD_APP_FAST_RETRY
retry_app_download:
#endif

#if 0
#if RESET_TO_HDL_DELAY_MS
    //gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
    syna_reset_gpio(tcm_hcd, 0);
    usleep_range(1000, 1000);
    syna_reset_gpio(tcm_hcd, 1);
    mdelay(RESET_TO_HDL_DELAY_MS);
#endif

#ifdef DOWNLOAD_APP_FAST_RETRY
    //check f35 again
    retval = zeroflash_check_f35();
    if (retval < 0) {
        retry_cnt++;
        if (retry_cnt <= 3) {
            TPD_INFO("can not read F35, goto retry\n");
            goto retry_app_download;
        } else {
            TPD_INFO("retry three times, but still fail,return\n");
            return retval;
        }
    }
#endif
#endif
    retval = syna_tcm_td4320_nf_rmi_write(tcm_hcd,
            g_zeroflash_hcd->f35_addr.control_base + F35_CTRL3_OFFSET,
            &command,
            sizeof(command));
    if (retval < 0) {
        TPD_INFO(
                "Failed to write F$35 command\n");
        UNLOCK_BUFFER(g_zeroflash_hcd->out);
        return retval;
    }

    retval = syna_tcm_td4320_nf_rmi_write(tcm_hcd,
            g_zeroflash_hcd->f35_addr.control_base + F35_CTRL7_OFFSET,
            g_zeroflash_hcd->out.buf,
            g_zeroflash_hcd->out.data_length);
    if (retval < 0) {
        TPD_INFO(
                "Failed to write application firmware data\n");
        UNLOCK_BUFFER(g_zeroflash_hcd->out);
        return retval;
    }

    UNLOCK_BUFFER(g_zeroflash_hcd->out);
#ifdef DOWNLOAD_APP_FAST_RETRY
    //read and check the identify response
    msleep(20);
    syna_tcm_raw_read(tcm_hcd, tmp_buf, sizeof(tmp_buf));
    if ((tmp_buf[0] != MESSAGE_MARKER) && (tmp_buf[1] != REPORT_IDENTIFY)) {
        retry_cnt++;
        if (retry_cnt <= 3) {
            TPD_INFO("can not read a identify report, goto retry\n");

#if RESET_TO_HDL_DELAY_MS
            //gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
            syna_reset_gpio(tcm_hcd, 0);
            usleep_range(1000, 1000);
            syna_reset_gpio(tcm_hcd, 1);
            mdelay(RESET_TO_HDL_DELAY_MS);
#endif
            goto retry_app_download;
        }
    } else {
        //successful read message
        //tcm_hcd->host_download_mode = true;
        TPD_INFO("download firmware success\n");
    }
#endif
    TPD_INFO("Application firmware downloaded\n");

    return 0;
}

int zeroflash_download_firmware_directly(void *chip_data, const struct firmware *fw)
{
    int retval;
    struct rmi_f35_data data;
    struct syna_tcm_hcd *tcm_hcd = (struct syna_tcm_hcd *)chip_data;
    struct touchpanel_data *ts = spi_get_drvdata(tcm_hcd->s_client);
    static unsigned int retry_count;
    struct firmware *request_fw_headfile = NULL;
    int reload = 0;

    TPD_DEBUG("%s Enter\n", __func__);


    if(fw == NULL){
        TPD_INFO("zeroflash_download_firmware_directly get NULL fw\n");
    }

    retval = zeroflash_check_uboot();
    if (retval < 0) {
        TPD_INFO(
                "Microbootloader support unavailable\n");
        goto exit;
    }

    atomic_set(&tcm_hcd->host_downloading, 1);

    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            g_zeroflash_hcd->f35_addr.data_base,
            (unsigned char *)&data,
            sizeof(data));
    if (retval < 0) {
        TPD_INFO(
                "Failed to read F$35 data\n");
        goto exit;
    }

    if (data.error_code != REQUESTING_FIRMWARE) {
        TPD_INFO(
                "Microbootloader error code = 0x%02x\n",
                data.error_code);
        if (data.error_code != CHECKSUM_FAILURE) {
            retval = -EIO;
            goto exit;
        } else {
            retry_count++;
        }
    } else {
        retry_count = 0;
    }

    if (g_zeroflash_hcd->fw_entry == NULL) {
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
        if (ts->boot_mode == RECOVERY_BOOT)
#else
        if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY)
#endif
        {
            retval = -1;
        } else if (is_oem_unlocked()) {
            retval = -1;
        } else {
            retval = zeroflash_get_fw_image();
        }
        if (retval < 0) {
            TPD_INFO("Get FW from headfile\n");
            request_fw_headfile = kzalloc(sizeof(struct firmware), GFP_KERNEL);
            if(request_fw_headfile == NULL) {
                TPD_INFO("%s kzalloc failed!\n", __func__);
                retval = FW_UPDATE_ERROR;
                goto exit;
            }else if(tcm_hcd->p_firmware_headfile->firmware_data) {
                request_fw_headfile->size = tcm_hcd->p_firmware_headfile->firmware_size;
                request_fw_headfile->data = tcm_hcd->p_firmware_headfile->firmware_data;
                g_zeroflash_hcd->fw_entry = request_fw_headfile;
            }
        }
    }
reload_fw:
    if (fw != NULL && !reload) {
            g_zeroflash_hcd->image = fw->data;
    } else {
            if(request_fw_headfile == NULL) {
                TPD_INFO("Get FW from headfile\n");
                request_fw_headfile = kzalloc(sizeof(struct firmware), GFP_KERNEL);
                if(request_fw_headfile == NULL) {
                    TPD_INFO("%s kzalloc failed!\n", __func__); 
                    retval = FW_UPDATE_ERROR;
                    goto exit;
                }else if(tcm_hcd->p_firmware_headfile->firmware_data) {
                    request_fw_headfile->size = tcm_hcd->p_firmware_headfile->firmware_size;
                    request_fw_headfile->data = tcm_hcd->p_firmware_headfile->firmware_data;
                } else {
                    TPD_INFO("firmware_data is NULL! exit firmware update!\n");
                    retval = FW_UPDATE_ERROR;
                    goto exit;
                }
            }
            g_zeroflash_hcd->image = request_fw_headfile->data;
    }

    retval = zeroflash_parse_fw_image();

    if (retval < 0) {
        TPD_INFO(
                "Failed to get firmware image\n");
        goto update_fail;
    }

    TPD_DETAIL("Start of firmware download\n");

    retval = zeroflash_download_app_fw();
    if (retval < 0) {
        TPD_INFO(
                "Failed to download application firmware\n");
        goto update_fail;
    }

    TPD_DETAIL("End of firmware download\n");

    retval = FW_UPDATE_SUCCESS;

    goto exit;
update_fail:
    if (!reload) {
        if (tcm_hcd->health_monitor_support) {
            tcm_hcd->monitor_data->fw_download_retry++;
        }
        reload = 1;
        goto reload_fw;
    }

exit:
    if(request_fw_headfile != NULL) {
        kfree(request_fw_headfile);
        request_fw_headfile = NULL;
    }
    if (retval < 0 && tcm_hcd->health_monitor_support) {
        tcm_hcd->monitor_data->fw_download_fail++;
    }
    return retval;
}


static void zeroflash_download_firmware_work(struct work_struct *work)
{
    int retval;
    struct rmi_f35_data data;
    struct syna_tcm_hcd *tcm_hcd = g_zeroflash_hcd->tcm_hcd;
    static unsigned int retry_count;

    atomic_set(&tcm_hcd->host_downloading, 1);
    retval = zeroflash_check_uboot();
    if (retval < 0) {
        TPD_INFO(
                "Microbootloader support unavailable\n");
        goto exit;
    }

    atomic_set(&tcm_hcd->host_downloading, 1);

    retval = syna_tcm_td4320_nf_rmi_read(tcm_hcd,
            g_zeroflash_hcd->f35_addr.data_base,
            (unsigned char *)&data,
            sizeof(data));
    if (retval < 0) {
        TPD_INFO(
                "Failed to read F$35 data\n");
        goto exit;
    }

    if (data.error_code != REQUESTING_FIRMWARE) {
        TPD_INFO(
                "Microbootloader error code = 0x%02x\n",
                data.error_code);
        if (data.error_code != CHECKSUM_FAILURE) {
            retval = -EIO;
            goto exit;
        } else {
            retry_count++;
        }
    } else {
        retry_count = 0;
    }

    retval = zeroflash_get_fw_image();
    if (retval < 0) {
        TPD_INFO(
                "Failed to get firmware image\n");
        goto exit;
    }

    TPD_DETAIL("Start of firmware download\n");

    retval = zeroflash_download_app_fw();
    if (retval < 0) {
        TPD_INFO(
                "Failed to download application firmware\n");
        goto exit;
    }

    syna_tcm_start_reset_timer(tcm_hcd);
    TPD_DETAIL("End of firmware download\n");

exit:
    if (retval < 0)
        retry_count++;

    if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT) {
        if (tcm_hcd->health_monitor_support) {
            tcm_hcd->monitor_data->fw_download_fail++;
        }
        disable_irq(tcm_hcd->s_client->irq);

        TPD_DETAIL("zeroflash_download_firmware_work disable_irq \n");


    } else {
        if (retval < 0) {
            if (tcm_hcd->health_monitor_support) {
                tcm_hcd->monitor_data->fw_download_retry++;
            }
            syna_reset_gpio(tcm_hcd, 0);
            msleep(20);
            syna_reset_gpio(tcm_hcd, 1);
            msleep(20);
            TPD_INFO("something wrong happen, add hw reset try to download\n");
        }
        //msleep(20);
        enable_irq(tcm_hcd->s_client->irq);

        TPD_DETAIL("zeroflash_download_firmware_work enable_irq \n");
    }

    return;
}

static int zeroflash_init(struct syna_tcm_hcd *tcm_hcd)
{
    g_zeroflash_hcd = kzalloc(sizeof(*g_zeroflash_hcd), GFP_KERNEL);
    if (!g_zeroflash_hcd) {
        TPD_INFO(
                "Failed to allocate memory for zeroflash_hcd\n");
        return -ENOMEM;
    }

    g_zeroflash_hcd->tcm_hcd = tcm_hcd;
    tcm_hcd->zeroflash_hcd = g_zeroflash_hcd;
    g_zeroflash_hcd->fw_name = tcm_hcd->fw_name;

    INIT_BUFFER(g_zeroflash_hcd->out, false);
    INIT_BUFFER(g_zeroflash_hcd->resp, false);

    g_zeroflash_hcd->config_workqueue =
            create_singlethread_workqueue("syna_tcm_zeroflash_config");
    g_zeroflash_hcd->firmware_workqueue =
            create_singlethread_workqueue("syna_tcm_zeroflash_firmware");
    INIT_WORK(&g_zeroflash_hcd->config_work,
            zeroflash_download_config_work);
    INIT_WORK(&g_zeroflash_hcd->firmware_work,
            zeroflash_download_firmware_work);

    g_zeroflash_hcd->init_done = true;
    zeroflash_init_done = 1;
    //if (tcm_hcd->init_okay == false/* &&
    //        tcm_hcd->hw_if->bus_io->type == BUS_SPI*/)
    //    zeroflash_download_firmware();

    return 0;
}


struct zeroflash_hcd * syna_remote_zeroflash_init(struct syna_tcm_hcd *tcm_hcd)
{
    zeroflash_init(tcm_hcd);

    return g_zeroflash_hcd;
}

void wait_zeroflash_firmware_work(void)
{
    cancel_work_sync(&g_zeroflash_hcd->config_work);
    flush_workqueue(g_zeroflash_hcd->config_workqueue);
    cancel_work_sync(&g_zeroflash_hcd->firmware_work);
    flush_workqueue(g_zeroflash_hcd->firmware_workqueue);
}

/*

static int syna_remote_zeroflash_remove(struct syna_tcm_hcd *tcm_hcd)
{
    if (!g_zeroflash_hcd)
        return 0;

    if (g_zeroflash_hcd->fw_entry)
        release_firmware(g_zeroflash_hcd->fw_entry);

    cancel_work_sync(&g_zeroflash_hcd->config_work);
    cancel_work_sync(&g_zeroflash_hcd->firmware_work);
    flush_workqueue(g_zeroflash_hcd->workqueue);
    destroy_workqueue(g_zeroflash_hcd->workqueue);

    RELEASE_BUFFER(g_zeroflash_hcd->resp);
    RELEASE_BUFFER(g_zeroflash_hcd->out);

    kfree(g_zeroflash_hcd);
    g_zeroflash_hcd = NULL;

    return 0;
}

static int syna_remote_zeroflash_syncbox(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;
    unsigned char *fw_status;

    if (!g_zeroflash_hcd)
        return 0;

    switch (tcm_hcd->report.id) {
    case REPORT_STATUS:
        fw_status = (unsigned char *)&g_zeroflash_hcd->fw_status;
        retval = secure_memcpy(fw_status,
                sizeof(g_zeroflash_hcd->fw_status),
                tcm_hcd->report.buffer.buf,
                tcm_hcd->report.buffer.buf_size,
                sizeof(g_zeroflash_hcd->fw_status));
        if (retval < 0) {
            TPD_INFO(
                    "Failed to copy firmware status\n");
            return retval;
        }
        zeroflash_download_config();
        break;
    case REPORT_HDL:
        retval = tcm_hcd->enable_irq(tcm_hcd, false, true);
        if (retval < 0) {
            TPD_INFO(
                    "Failed to disable interrupt\n");
            return retval;
        }
        zeroflash_download_firmware();
        break;
    default:
        break;
    }

    return 0;
}


static int syna_remote_zeroflash_reset(struct syna_tcm_hcd *tcm_hcd)
{
    int retval;

    if (!g_zeroflash_hcd) {
        retval = zeroflash_init(tcm_hcd);
        return retval;
    }

    return 0;
}
*/
