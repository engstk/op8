// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/input/mt.h>
//#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include "nvt_drivers_nt36525_noflash.h"

/*******Part0:LOG TAG Declear********************/

static uint8_t ilm_dlm_num = 2;
static struct timeval start, end;
__attribute__((weak)) int tp_trigger_lcd_reset(void) {return 0;}

/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "novatek,nf_nt36525"
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
/******************** End of Log Tag Declear and level define*********************************/
static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw, bool chk_ignore);
static void nvt_change_mode(struct chip_data_nt36525 *chip_info, uint8_t mode);
static int32_t nvt_read_fw_noise(struct chip_data_nt36525 *chip_info, int32_t config_Diff_Test_Frame, int32_t *xdata, int32_t *xdata_n, int32_t xdata_len);
static int8_t nvt_switch_FreqHopEnDis(struct chip_data_nt36525 *chip_info, uint8_t FreqHopEnDis);
static void nvt_read_mdata(struct chip_data_nt36525 *chip_info, uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len);
static uint8_t nvt_get_fw_pipe(struct chip_data_nt36525 *chip_info);
static int32_t nvt_check_fw_status(struct chip_data_nt36525 *chip_info);
static void nvt_change_mode(struct chip_data_nt36525 *chip_info, uint8_t mode);
static int32_t nvt_clear_fw_status(struct chip_data_nt36525 *chip_info);
static void store_to_file(int fd, char* format, ...);
static int nvt_get_chip_info(void *chip_data);
static bool nvt_fw_recovery(uint8_t *point_data);
static void nvt_esd_check_enable(struct chip_data_nt36525 *chip_info, bool enable);
static uint8_t nvt_wdt_fw_recovery(struct chip_data_nt36525 *chip_info, uint8_t *point_data);
static int8_t nvt_cmd_store(struct chip_data_nt36525 *chip_info, uint8_t u8Cmd);
static fw_update_state nvt_fw_update_choice(struct chip_data_nt36525 *chip_info, const struct firmware *fw, bool chk_ignore);

static struct chip_data_nt36525 *g_chip_info = NULL;

/***************************** start of id map table******************************************/

static const struct nvt_ts_mem_map NT36525_memory_map = {
    .EVENT_BUF_ADDR           = 0x11A00,
    .RAW_PIPE0_ADDR           = 0x10000,
    .RAW_PIPE0_Q_ADDR         = 0,
    .RAW_PIPE1_ADDR           = 0x12000,
    .RAW_PIPE1_Q_ADDR         = 0,
    .BASELINE_ADDR            = 0x10B08,
    .BASELINE_Q_ADDR          = 0,
    .BASELINE_BTN_ADDR        = 0x12B08,
    .BASELINE_BTN_Q_ADDR      = 0,
    .DIFF_PIPE0_ADDR          = 0x1064C,
    .DIFF_PIPE0_Q_ADDR        = 0,
    .DIFF_PIPE1_ADDR          = 0x1264C,
    .DIFF_PIPE1_Q_ADDR        = 0,
    .RAW_BTN_PIPE0_ADDR       = 0x10634,
    .RAW_BTN_PIPE0_Q_ADDR     = 0,
    .RAW_BTN_PIPE1_ADDR       = 0x12634,
    .RAW_BTN_PIPE1_Q_ADDR     = 0,
    .DIFF_BTN_PIPE0_ADDR      = 0x10AFC,
    .DIFF_BTN_PIPE0_Q_ADDR    = 0,
    .DIFF_BTN_PIPE1_ADDR      = 0x12AFC,
    .DIFF_BTN_PIPE1_Q_ADDR    = 0,
    .READ_FLASH_CHECKSUM_ADDR = 0x14000,
    .RW_FLASH_DATA_ADDR       = 0x14002,
    /* Phase 2 Host Download */
    .BOOT_RDY_ADDR            = 0x1F141,
    .POR_CD_ADDR              = 0x1F61C,
    /* BLD CRC */
    .R_ILM_CHECKSUM_ADDR      = 0x1BF00,
    .DOZE_GM_S1D_SCAN_RAW_ADDR= 0x12B28,
    .DOZE_GM_BTN_SCAN_RAW_ADDR= 0x12BB0,
};

static const struct nvt_ts_trim_id_table trim_id_table[] = {
    {.id = {0xFF, 0xFF, 0xFF, 0x25, 0x65, 0x03}, .mask = {0, 0, 0, 1, 1, 1},
        .mmap = &NT36525_memory_map, .carrier_system = 0},
};

const struct mt_chip_conf spi_ctrdata = {
        .setuptime = 25,
        .holdtime = 25,
        .high_time = 3, /* 16.6MHz */
        .low_time = 3,
        .cs_idletime = 2,
        .ulthgh_thrsh = 0,

        .cpol = 0,
        .cpha = 0,

        .rx_mlsb = 1,
        .tx_mlsb = 1,

        .tx_endian = 0,
        .rx_endian = 0,

        .com_mod = DMA_TRANSFER,

        .pause = 0,
        .finish_intr = 1,
        .deassert = 0,
        .ulthigh = 0,
        .tckdly = 0,
};

/*******************************************************
Description:
    Novatek touchscreen write data to specify address.

return:
    Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_write_addr(struct spi_device *client, uint32_t addr, uint8_t data)
{
    int32_t ret = 0;
    uint8_t buf[4] = {0};

    //---set xdata index---
    buf[0] = 0xFF;  //set index/page/addr command
    buf[1] = (addr >> 15) & 0xFF;
    buf[2] = (addr >> 7) & 0xFF;
    ret = CTP_SPI_WRITE(client, buf, 3);
    if (ret) {
        TPD_INFO("set page 0x%06X failed, ret = %d\n", addr, ret);
        return ret;
    }

    //---write data to index---
    buf[0] = addr & (0x7F);
    buf[1] = data;
    ret = CTP_SPI_WRITE(client, buf, 2);
    if (ret) {
        TPD_INFO("write data to 0x%06X failed, ret = %d\n", addr, ret);
        return ret;
    }

    return ret;
}

/*******************************************************
Description:
    Novatek touchscreen reset MCU (boot) function.

return:
    n.a.
*******************************************************/
static void nvt_bootloader_reset(struct chip_data_nt36525 *chip_info)
{
    //---reset cmds to SWRST_N8_ADDR---
    nvt_write_addr(chip_info->s_client, SWRST_N8_ADDR, 0x69);

    mdelay(5);  //wait tBRST2FR after Bootload RST

    nvt_write_addr(chip_info->s_client, SPI_READ_FAST, 0x01);
}

/*******************************************************
Description:
    Novatek touchscreen set index/page/addr address.

return:
    Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_set_page(struct chip_data_nt36525 *chip_info, uint32_t addr)
{
    uint8_t buf[4] = {0};

    buf[0] = 0xFF;      //set index/page/addr command
    buf[1] = (addr >> 15) & 0xFF;
    buf[2] = (addr >> 7) & 0xFF;

    return CTP_SPI_WRITE(chip_info->s_client, buf, 3);
}

/*******************************************************
Description:
        Novatek touchscreen check chip version trim function.

return:
        Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int8_t nvt_ts_check_chip_ver_trim(struct chip_data_nt36525 *chip_info)
{
        uint8_t buf[8] = {0};
        int32_t retry = 0;
        int32_t list = 0;
        int32_t i = 0;
        int32_t found_nvt_chip = 0;
        int32_t ret = -1;
        int boot_mode;

        //---Check for 5 times---
        for (retry = 5; retry > 0; retry--) {

                nvt_bootloader_reset(chip_info);

                //---set xdata index to 0x1F600---
                nvt_set_page(chip_info, 0x1F600);       //read chip id

                buf[0] = 0x4E;  //offset
                buf[1] = 0x00;
                buf[2] = 0x00;
                buf[3] = 0x00;
                buf[4] = 0x00;
                buf[5] = 0x00;
                buf[6] = 0x00;
                CTP_SPI_READ(chip_info->s_client, buf, 7);
                TPD_INFO("buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
                        buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

                // compare read chip id on supported list
                for (list = 0; list < (sizeof(trim_id_table) / sizeof(struct nvt_ts_trim_id_table)); list++) {
                        found_nvt_chip = 0;

                        // compare each byte
                        for (i = 0; i < NVT_ID_BYTE_MAX; i++) {
                                if (trim_id_table[list].mask[i]) {
                                        if (buf[i + 1] != trim_id_table[list].id[i])    //set parameter from chip id
                                                break;
                                }
                        }

                        if (i == NVT_ID_BYTE_MAX) {
                                found_nvt_chip = 1;
                        }

                        if (found_nvt_chip) {
                                TPD_INFO("This is NVT touch IC\n");
                                chip_info->trim_id_table.mmap = trim_id_table[list].mmap;
                                chip_info->trim_id_table.carrier_system = trim_id_table[list].carrier_system;
                                ret = 0;
                                goto out;
                        } else {
                                chip_info->trim_id_table.mmap = NULL;
                                ret = -1;
                        }
                }

                msleep(10);
        }

        if (chip_info->trim_id_table.mmap == NULL) {  //set default value
                chip_info->trim_id_table.mmap = &NT36525_memory_map;
                chip_info->trim_id_table.carrier_system = 0;
                boot_mode = get_boot_mode();
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
                if ((boot_mode == META_BOOT || boot_mode == FACTORY_BOOT))
#else
                if ((boot_mode == MSM_BOOT_MODE__FACTORY || boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN)) {
#endif
                    TPD_INFO("in ftm mode, maybe not connect lcd\n");
                    ret = -1;
                } else {
                    ret = 0;
                }
        }

out:
        return ret;
}

/*******************************************************
Description:
        Novatek touchscreen check FW reset state function.

return:
        Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
static int32_t nvt_check_fw_reset_state_noflash(struct chip_data_nt36525 *chip_info, RST_COMPLETE_STATE check_reset_state)
{
        uint8_t buf[8] = {0};
        int32_t ret = 0;
        int32_t retry = 0;
        int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 100;

        while (1) {
                msleep(10);

                //---read reset state---
                buf[0] = EVENT_MAP_RESET_COMPLETE;
                buf[1] = 0x00;
                CTP_SPI_READ(chip_info->s_client, buf, 6);

                if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
                        ret = 0;
                        break;
                }

                retry++;
                if(unlikely(retry > retry_max)) {
                        TPD_INFO("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
                        ret = -1;
                        break;
                }
        }

        return ret;
}


/*******************************************************
Description:
        Novatek touchscreen get novatek project id information
        function.

return:
        Executive outcomes. 0---success. -1---fail.
*******************************************************/
int32_t nvt_read_pid_noflash(struct chip_data_nt36525 *chip_info)
{
        uint8_t buf[3] = {0};
        int32_t ret = 0;

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_PROJECTID);

        //---read project id---
        buf[0] = EVENT_MAP_PROJECTID;
        buf[1] = 0x00;
        buf[2] = 0x00;
        CTP_SPI_READ(chip_info->s_client, buf, 3);

        chip_info->nvt_pid = (buf[2] << 8) + buf[1];

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

        TPD_INFO("PID=%04X\n", chip_info->nvt_pid);

        return ret;
}
static int nvt_enter_sleep(struct chip_data_nt36525 *chip_info, bool config)
{
    int ret = -1;
    if (config) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

        ret = nvt_cmd_store(chip_info, CMD_ENTER_SLEEP);
        if (ret < 0) {
            TPD_INFO("%s: enter sleep mode failed!\n", __func__);
            return -1;
        } else {
            chip_info->is_sleep_writed = true;
            TPD_INFO("%s: enter sleep mode sucess!\n", __func__);
        }
    }

    return ret;
}

static void copy_fw_to_buffer(struct chip_data_nt36525 *chip_info, const struct firmware *fw)
{
    if (fw) {
        //free already exist fw data buffer
        if (chip_info->tp_firmware.data) {
            kfree((void *)(chip_info->tp_firmware.data));
            chip_info->tp_firmware.data = NULL;
        }

        //new fw data buffer
        chip_info->tp_firmware.data = kmalloc(fw->size, GFP_KERNEL);
        if (chip_info->tp_firmware.data == NULL) {
            TPD_INFO("kmalloc tp firmware data error\n");

            chip_info->tp_firmware.data = kmalloc(fw->size, GFP_KERNEL);
            if (chip_info->tp_firmware.data == NULL) {
                TPD_INFO("retry kmalloc tp firmware data error\n");
                return;
            }
        }

        //copy bin fw to data buffer
        memcpy((u8 *)chip_info->tp_firmware.data, (u8 *)(fw->data), fw->size);
        if (0 == memcmp((u8 *)chip_info->tp_firmware.data, (u8 *)(fw->data), fw->size)) {
            TPD_INFO("copy_fw_to_buffer fw->size=%zu\n", fw->size);
            chip_info->tp_firmware.size = fw->size;
        } else {
            TPD_INFO("copy_fw_to_buffer fw error\n");
            chip_info->tp_firmware.size = 0;
        }
    }

    return;
}

static int nvt_reset(void *chip_data)
{
    int ret = -1;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
    const struct firmware *fw = NULL;

    TPD_INFO("%s. chip_info->tp_firmware.size=%zu\n", __func__, chip_info->tp_firmware.size);
    mutex_lock(&chip_info->reset_lock);

    //check fw exist and fw checksum ok
    if (chip_info->tp_firmware.size && chip_info->tp_firmware.data) {
        fw = &(chip_info->tp_firmware);
    }

    ret = nvt_fw_update(chip_info, fw, 0);
    if(ret > 0) {
        TPD_INFO("fw update failed!\n");
    }

    chip_info->is_sleep_writed = false;
    mutex_unlock(&chip_info->reset_lock);

    return 0;
}

/*******************************************************
Description:
        Novatek touchscreen eng reset cmd
    function.

return:
        n.a.
*******************************************************/
void nvt_eng_reset(struct chip_data_nt36525 *chip_info)
{
    //---eng reset cmds to ENG_RST_ADDR---
    nvt_write_addr(chip_info->s_client, chip_info->ENG_RST_ADDR, 0x5A);

    mdelay(1);      //wait tMCU_Idle2TP_REX_Hi after TP_RST
}

static int8_t nvt_cmd_store(struct chip_data_nt36525 *chip_info, uint8_t u8Cmd)
{
    int i, retry = 5;
    uint8_t buf[3] = {0};

    //---set xdata index to EVENT BUF ADDR---(set page)
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    for (i = 0; i < retry; i++) {
        //---set cmd status---
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = u8Cmd;
        CTP_SPI_WRITE(chip_info->s_client, buf, 2);

        msleep(40);

        //---read cmd status---
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, 2);
        if (buf[1] == 0x00)
            break;
    }

    if (unlikely(i == retry)) {
        TPD_INFO("send Cmd 0x%02X failed, buf[1]=0x%02X\n", u8Cmd, buf[1]);
        return -1;
    } else {
        TPD_DEBUG("send Cmd 0x%02X success, tried %d times\n", u8Cmd, i);
    }

    return 0;
}

static int nvt_enable_black_gesture(struct chip_data_nt36525 *chip_info, bool enable)
{
    int ret = -1;

    TPD_DEBUG("%s, enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        nvt_reset(chip_info);
    }

    if (enable) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
        ret = nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
        TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success");
    } else {
        ret = 0;
    }

    return ret;
}

static int nvt_enable_edge_limit(struct chip_data_nt36525 *chip_info, int state)
{
    int8_t ret = -1;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
    TPD_INFO("%s:state = %d, limit_corner = %d, chip_info->is_sleep_writed = %d\n", __func__, state, ts->limit_corner, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        nvt_reset(chip_info);
    }

    if (ts->fw_edge_limit_support) {
        if (state == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {
            ret = nvt_cmd_store(chip_info, EVENTBUFFER_EDGE_LIMIT_VERTICAL);
        } else {
            if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
                ret = nvt_cmd_store(chip_info, EVENTBUFFER_EDGE_LIMIT_RIGHT_UP);
            } else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
                ret = nvt_cmd_store(chip_info, EVENTBUFFER_EDGE_LIMIT_LEFT_UP);
            }
        }
    } else {
        if (state) {
            ret = nvt_cmd_store(chip_info, EVENTBUFFER_EDGE_LIMIT_ON);
        } else {
            ret = nvt_cmd_store(chip_info, EVENTBUFFER_EDGE_LIMIT_OFF);
        }
    }

    return ret;
}

static int nvt_enable_charge_mode(struct chip_data_nt36525 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        nvt_reset(chip_info);
    }

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_IN);
        TPD_INFO("%s: EVENTBUFFER_PWR_PLUG_IN\n", __func__);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_OUT);
    }

    return ret;
}

static int nvt_enable_headset_mode(struct chip_data_nt36525 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        nvt_reset(chip_info);
    }

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_IN);
        TPD_INFO("%s: EVENTBUFFER_HS_PLUG_IN\n", __func__);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_OUT);
    }

    return ret;
}

int32_t nvt_get_fw_info_noflash(struct chip_data_nt36525 *chip_info)
{
        uint8_t buf[64] = {0};
        uint32_t retry_count = 0;
        int32_t ret = 0;

info_retry:
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

        //---read fw info---
        buf[0] = EVENT_MAP_FWINFO;
        CTP_SPI_READ(chip_info->s_client, buf, 17);
        chip_info->fw_ver = buf[1];
        chip_info->fw_sub_ver = buf[14];
        TPD_INFO("fw_ver = 0x%x, fw_sub_ver = 0x%x\n", chip_info->fw_ver, chip_info->fw_sub_ver);

        //---clear x_num, y_num if fw info is broken---
        if ((buf[1] + buf[2]) != 0xFF) {
                TPD_INFO("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
                chip_info->fw_ver = 0;

                if(retry_count < 3) {
                        retry_count++;
                        TPD_INFO("retry_count=%d\n", retry_count);
                        goto info_retry;
                } else {
                        TPD_INFO("Set default fw_ver=0, x_num=18, y_num=32, abs_x_max=1080, abs_y_max=1920, max_button_num=0!\n");
                        ret = -1;
                }
        } else {
                ret = 0;
        }

        //---Get Novatek PID---
        nvt_read_pid_noflash(chip_info);

        return ret;
}

static uint32_t byte_to_word(const uint8_t *data)
{
        return data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
}

static uint32_t CheckSum(const u8 *data, size_t len)
{
        uint32_t i = 0;
        uint32_t checksum = 0;

        for (i = 0 ; i < len+1 ; i++)
                checksum += data[i];

        checksum += len;
        checksum = ~checksum +1;

        return checksum;
}

static int32_t nvt_bin_header_parser(struct chip_data_nt36525 *chip_info, const u8 *fwdata, size_t fwsize)
{
        uint32_t list = 0;
        uint32_t pos = 0x00;
        uint32_t end = 0x00;
        uint8_t info_sec_num = 0;
        uint8_t ovly_sec_num = 0;
        uint8_t ovly_info = 0;

        /* Find the header size */
        end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);
        pos = 0x30;     // info section start at 0x30 offset
        while (pos < end) {
                info_sec_num ++;
                pos += 0x10;    /* each header info is 16 bytes */
        }

        /*
         * Find the DLM OVLY section
         * [0:3] Overlay Section Number
         * [4]   Overlay Info
         */
        ovly_info = (fwdata[0x28] & 0x10) >> 4;
        ovly_sec_num = (ovly_info) ? (fwdata[0x28] & 0x0F) : 0;

        /*
         * calculate all partition number
         * ilm_dlm_num (ILM & DLM) + ovly_sec_num + info_sec_num
         */
        chip_info->partition = chip_info->ilm_dlm_num + ovly_sec_num + info_sec_num;
        TPD_INFO("ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d\n",
                        ovly_info, chip_info->ilm_dlm_num, ovly_sec_num, info_sec_num, chip_info->partition);

        /* allocated memory for header info */
        chip_info->bin_map = (struct nvt_ts_bin_map *)kzalloc((chip_info->partition+1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);
        if(chip_info->bin_map == NULL) {
                TPD_INFO("kzalloc for bin_map failed!\n");
                return -ENOMEM;
        }

        for (list = 0; list < chip_info->partition; list++) {
                /*
                 * [1] parsing ILM & DLM header info
                 * BIN_addr : SRAM_addr : size (12-bytes)
                 * crc located at 0x18 & 0x1C
                 */
                if (list < chip_info->ilm_dlm_num) {
                        chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list*12]);
                        chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list*12]);
                        chip_info->bin_map[list].size = byte_to_word(&fwdata[8 + list*12]);
                        if ((chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size) < fwsize)
                                chip_info->bin_map[list].crc = CheckSum(&fwdata[chip_info->bin_map[list].BIN_addr], chip_info->bin_map[list].size);
                        else {
                                TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                                                chip_info->bin_map[list].BIN_addr, chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size);
                                return -EINVAL;
                        }
                        if (list == 0)
                                sprintf(chip_info->bin_map[list].name, "ILM");
                        else if (list == 1)
                                sprintf(chip_info->bin_map[list].name, "DLM");
                }

                /*
                 * [2] parsing others header info
                 * SRAM_addr : size : BIN_addr : crc (16-bytes)
                 */
                if ((list >= chip_info->ilm_dlm_num) && (list < (chip_info->ilm_dlm_num + info_sec_num))) {
                        /* others partition located at 0x30 offset */
                        pos = 0x30 + (0x10 * (list - chip_info->ilm_dlm_num));

                        chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
                        chip_info->bin_map[list].size = byte_to_word(&fwdata[pos+4]);
                        chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
                        if ((chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size) < fwsize)
                                chip_info->bin_map[list].crc = CheckSum(&fwdata[chip_info->bin_map[list].BIN_addr], chip_info->bin_map[list].size);
                        else {
                                TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                                                chip_info->bin_map[list].BIN_addr, chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size);
                                return -EINVAL;
                        }
                        /* detect header end to protect parser function */
                        if ((chip_info->bin_map[list].BIN_addr == 0) && (chip_info->bin_map[list].size != 0)) {
                                sprintf(chip_info->bin_map[list].name, "Header");
                        } else {
                                sprintf(chip_info->bin_map[list].name, "Info-%d", (list - chip_info->ilm_dlm_num));
                        }
                }

                /*
                 * [3] parsing overlay section header info
                 * SRAM_addr : size : BIN_addr : crc (16-bytes)
                 */
                if (list >= (chip_info->ilm_dlm_num + info_sec_num)) {
                        /* overlay info located at DLM (list = 1) start addr */
                        pos = chip_info->bin_map[1].BIN_addr + (0x10 * (list- chip_info->ilm_dlm_num - info_sec_num));

                        chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
                        chip_info->bin_map[list].size = byte_to_word(&fwdata[pos+4]);
                        chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
                        if ((chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size) < fwsize)
                                chip_info->bin_map[list].crc = CheckSum(&fwdata[chip_info->bin_map[list].BIN_addr], chip_info->bin_map[list].size);
                        else {
                                TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                                                chip_info->bin_map[list].BIN_addr, chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size);
                                return -EINVAL;
                        }
                        sprintf(chip_info->bin_map[list].name, "Overlay-%d", (list- chip_info->ilm_dlm_num - info_sec_num));
                }
    }

        return 0;
}

/*******************************************************
Description:
        Novatek touchscreen init variable and allocate buffer
for download firmware function.

return:
        n.a.
*******************************************************/
static int32_t Download_Init(struct chip_data_nt36525 *chip_info)
{
        /* allocate buffer for transfer firmware */
        //NVT_LOG("SPI_TANSFER_LEN = %ld\n", SPI_TANSFER_LEN);

        if (chip_info->fwbuf == NULL) {
                chip_info->fwbuf = (uint8_t *)kzalloc((SPI_TANSFER_LEN+1), GFP_KERNEL);
                if(chip_info->fwbuf == NULL) {
                        TPD_INFO("kzalloc for fwbuf failed!\n");
                        return -ENOMEM;
                }
        }

        return 0;
}

#if NVT_DUMP_SRAM
/*******************************************************
Description:
        Novatek touchscreen dump flash partition function.

return:
        n.a.
*******************************************************/
static void nvt_read_ram_test(struct chip_data_nt36525 *chip_info, uint32_t addr, uint16_t len, char *name)
{
        char file[256] = "";
        uint8_t *fbufp = NULL;
        int32_t ret = 0;
        struct file *fp = NULL;
        mm_segment_t org_fs;

        sprintf(file, "/sdcard/dump_%s.bin", name);
        TPD_INFO("Dump [%s] from 0x%08X to 0x%08X\n", file, addr, addr+len);

        fbufp = (uint8_t *)kzalloc(len + 1, GFP_KERNEL);
        if(fbufp == NULL) {
                TPD_INFO("kzalloc for fbufp failed!\n");
                return;
        }

        org_fs = get_fs();
        set_fs(KERNEL_DS);
        fp = filp_open(file, O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (fp == NULL || IS_ERR(fp)) {
                TPD_INFO("open file failed\n");
                goto open_file_fail;
        }

        /* SPI read */
        //---set xdata index to addr---
        nvt_set_page(chip_info, addr);

        fbufp[0] = addr & 0x7F; //offset
        CTP_SPI_READ(chip_info->s_client, fbufp, len+1);

        /* Write to file */
        ret = vfs_write(fp, (char __user *)fbufp+1, len, &offset);
        if (ret <= 0) {
                TPD_INFO("write file failed\n");
                goto open_file_fail;
        }

open_file_fail:
        if (!IS_ERR(fp)) {
                filp_close(fp, NULL);
                set_fs(org_fs);
                fp = NULL;
        }

        if (fbufp) {
                kfree(fbufp);
                fbufp = NULL;
        }

        return;
}
#endif

/*******************************************************
Description:
        Novatek touchscreen Write_Partition function to write
firmware into each partition.

return:
        n.a.
*******************************************************/
static int32_t Write_Partition(struct chip_data_nt36525 *chip_info, const u8 *fwdata, size_t fwsize)
{
        uint32_t list = 0;
        char *name;
        uint32_t BIN_addr, SRAM_addr, size;
        uint32_t i = 0;
        uint16_t len = 0;
        int32_t count = 0;
        int32_t ret = 0;

        memset(chip_info->fwbuf, 0, (SPI_TANSFER_LEN+1));

        for (list = 0; list < chip_info->partition; list++) {
                /* initialize variable */
                SRAM_addr = chip_info->bin_map[list].SRAM_addr;
                size = chip_info->bin_map[list].size;
                BIN_addr = chip_info->bin_map[list].BIN_addr;
                name = chip_info->bin_map[list].name;

//              TPD_INFO("[%d][%s] SRAM (0x%08X), SIZE (0x%08X), BIN (0x%08X)\n",
//                              list, name, SRAM_addr, size, BIN_addr);

                /* Check data size */
                if ((BIN_addr + size) > fwsize) {
                        TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                                        BIN_addr, BIN_addr + size);
                        ret = -1;
                        goto out;
                }

                /* ignore reserved partition (Reserved Partition size is zero) */
                if (!size)
                        continue;
                else
                        size = size +1;

                /* write data to SRAM */
                if (size % SPI_TANSFER_LEN)
                        count = (size / SPI_TANSFER_LEN) + 1;
                else
                        count = (size / SPI_TANSFER_LEN);

                for (i = 0 ; i < count ; i++) {
                        len = (size < SPI_TANSFER_LEN) ? size : SPI_TANSFER_LEN;

                        //---set xdata index to start address of SRAM---
                        nvt_set_page(chip_info, SRAM_addr);

                        //---write data into SRAM---
                        chip_info->fwbuf[0] = SRAM_addr & 0x7F; //offset
                        memcpy(chip_info->fwbuf + 1, &fwdata[BIN_addr], len);   //payload
                        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, len + 1);

#if NVT_DUMP_SRAM
                        /* dump for debug download firmware */
                        nvt_read_ram_test(chip_info, SRAM_addr, len, name);
#endif
                        SRAM_addr += SPI_TANSFER_LEN;
                        BIN_addr += SPI_TANSFER_LEN;
                        size -= SPI_TANSFER_LEN;
                }

#if NVT_DUMP_SRAM
                offset = 0;
#endif
        }

out:
        return ret;
}

static void nvt_set_bld_crc(struct chip_data_nt36525 *chip_info, uint32_t DES_ADDR, uint32_t SRAM_ADDR,
                uint32_t LENGTH_ADDR, uint32_t size,
                uint32_t G_CHECKSUM_ADDR, uint32_t crc)
{
        /* write destination address */
        nvt_set_page(chip_info, DES_ADDR);
        chip_info->fwbuf[0] = DES_ADDR & 0x7F;
        chip_info->fwbuf[1] = (SRAM_ADDR) & 0xFF;
        chip_info->fwbuf[2] = (SRAM_ADDR >> 8) & 0xFF;
        chip_info->fwbuf[3] = (SRAM_ADDR >> 16) & 0xFF;
        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 4);

        /* write length */
        nvt_set_page(chip_info, LENGTH_ADDR);
        chip_info->fwbuf[0] = LENGTH_ADDR & 0x7F;
        chip_info->fwbuf[1] = (size) & 0xFF;
        chip_info->fwbuf[2] = (size >> 8) & 0xFF;
        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 3);

        /* write golden dlm checksum */
        nvt_set_page(chip_info, G_CHECKSUM_ADDR);
        chip_info->fwbuf[0] = G_CHECKSUM_ADDR & 0x7F;
        chip_info->fwbuf[1] = (crc) & 0xFF;
        chip_info->fwbuf[2] = (crc >> 8) & 0xFF;
        chip_info->fwbuf[3] = (crc >> 16) & 0xFF;
        chip_info->fwbuf[4] = (crc >> 24) & 0xFF;
        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 5);

        return;
}


/*******************************************************
Description:
        Novatek touchscreen check hw crc function.
This function will check hw crc result is pass or not.

return:
        n.a.
*******************************************************/
static int32_t Check_HW_CRC(struct chip_data_nt36525 *chip_info, uint8_t is_ilm_dlm)
{
        uint32_t list = 0;
        uint8_t retry = 0, crc_flag = 0;
        int32_t ret = 0;

        if (is_ilm_dlm) {
                /* polling for ilm & dlm crc check finish */
                while (1) {
                        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR);
                        chip_info->fwbuf[0] = chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
                        chip_info->fwbuf[1] = 0xFF;
                        ret = CTP_SPI_READ(chip_info->s_client, chip_info->fwbuf, 2);
                        if (ret) {
                                TPD_INFO("Read dma crc flag failed\n");
                                return ret;
                        }

                        /*
                         * [0] ILM_CRC_FLAG: 0-ILM CRC pass, 1-ILM CRC error
                         * [1] DLM_CRC_FLAG: 0-DLM CRC pass, 1-DLM CRC error
                         * [2] CRC_DONE    : 0-CRC checking, 1-finish
                         */
                        crc_flag = (chip_info->fwbuf[1] & 0x07);
                        if (crc_flag == 0x04)
                                break;

                        msleep(1);

                        retry++;
                        if (unlikely(retry > 20)) {
                                nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR);
                                chip_info->fwbuf[0] = chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR & 0x7F;
                                chip_info->fwbuf[1] = 0x00;
                                chip_info->fwbuf[2] = 0x00;
                                chip_info->fwbuf[3] = 0x00;
                                chip_info->fwbuf[4] = 0x00;
                                chip_info->fwbuf[5] = 0x00;
                                chip_info->fwbuf[6] = 0x00;
                                chip_info->fwbuf[7] = 0x00;
                                chip_info->fwbuf[8] = 0x00;
                                ret = CTP_SPI_READ(chip_info->s_client, chip_info->fwbuf, 9);
                                if (ret) {
                                        TPD_INFO("Read bld crc failed\n");
                                        return ret;
                                }

                                TPD_INFO("ilm dlm crc error 0x%02X\n", crc_flag);
                                TPD_INFO("ILM_BIN_CRC=0x%08X, ILM_HW_CRC=0x%08X\n",
                                                chip_info->bin_map[0].crc, byte_to_word(&chip_info->fwbuf[1]));
                                TPD_INFO("DLM_BIN_CRC=0x%08X, DLM_HW_CRC=0x%08X\n",
                                                chip_info->bin_map[1].crc, byte_to_word(&chip_info->fwbuf[5]));
                                return -EIO;
                        }
                }
        } else {
                for (list = ilm_dlm_num; list < chip_info->partition; list++) {
                        /* ignore reserved partition (Reserved Partition size is zero) */
                        if (!chip_info->bin_map[list].size)
                                continue;

                        /* Detect Header (Header size - 4) */
                        if ((chip_info->bin_map[list].BIN_addr == 0) && (chip_info->bin_map[list].size != 0))
                                chip_info->bin_map[list].size = chip_info->bin_map[list].size - 4;

                        /* write register bank */
                        nvt_set_bld_crc(chip_info, chip_info->trim_id_table.mmap->BLD_DES_ADDR, chip_info->bin_map[list].SRAM_addr,
                                        chip_info->trim_id_table.mmap->BLD_LENGTH_ADDR, chip_info->bin_map[list].size,
                                        chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR, chip_info->bin_map[list].crc);

                        /* bld dma crc enable */
                        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DMA_CRC_EN_ADDR);
                        chip_info->fwbuf[0] = chip_info->trim_id_table.mmap->DMA_CRC_EN_ADDR & 0x7F;
                        chip_info->fwbuf[1] = 0x01;     //enable
                        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 2);

                        /* polling for dma crc check finish */
                        while (1) {
                                nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DMA_CRC_FLAG_ADDR);
                                chip_info->fwbuf[0] = chip_info->trim_id_table.mmap->DMA_CRC_FLAG_ADDR & 0x7F;
                                chip_info->fwbuf[1] = 0xFF;
                                ret = CTP_SPI_READ(chip_info->s_client, chip_info->fwbuf, 2);
                                if (ret) {
                                        TPD_INFO("Read dma crc flag failed\n");
                                        return ret;
                                }

                                /*
                                 * [0] DMA_CRC_FLAG: 0-DMA CRC pass, 1-DMA CRC error
                                 * [1] DMA_CRC_DONE: 0-CRC checking, 1-finish
                                 */
                                crc_flag = (chip_info->fwbuf[1] & 0x03);
                                if (crc_flag == 0x02)
                                        break;

                                msleep(1);

                                retry++;
                                if (unlikely(retry > 20)) {
                                        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR);
                                        chip_info->fwbuf[0] = chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR & 0x7F;
                                        chip_info->fwbuf[1] = 0x00;
                                        chip_info->fwbuf[2] = 0x00;
                                        chip_info->fwbuf[3] = 0x00;
                                        chip_info->fwbuf[4] = 0x00;
                                        ret = CTP_SPI_READ(chip_info->s_client, chip_info->fwbuf, 5);
                                        if (ret) {
                                                TPD_INFO("Read bld crc failed\n");
                                                return ret;
                                        }

                                        TPD_INFO("partition %d dma crc error 0x%02X\n", list, crc_flag);
                                        TPD_INFO("partition %d BIN_CRC=0x%08X, HW_CRC=0x%08X\n",
                                                        list, chip_info->bin_map[list].crc, byte_to_word(&chip_info->fwbuf[1]));
                                        return -EIO;
                                }
                        }
                }

                /* [0] ILM */
                /* write register bank */
                nvt_set_bld_crc(chip_info, chip_info->trim_id_table.mmap->ILM_DES_ADDR, chip_info->bin_map[0].SRAM_addr,
                                chip_info->trim_id_table.mmap->ILM_LENGTH_ADDR, chip_info->bin_map[0].size,
                                chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR, chip_info->bin_map[0].crc);

                /* [1] DLM */
                /* write register bank */
                nvt_set_bld_crc(chip_info, chip_info->trim_id_table.mmap->DLM_DES_ADDR, chip_info->bin_map[1].SRAM_addr,
                                chip_info->trim_id_table.mmap->DLM_LENGTH_ADDR, chip_info->bin_map[1].size,
                                chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR, chip_info->bin_map[1].crc);
        }

        return ret;
}

/*******************************************************
Description:
        Novatek touchscreen set boot ready function.

return:
        Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
void nvt_boot_ready(struct chip_data_nt36525 *chip_info, uint8_t ready)
{
        //---write BOOT_RDY status cmds---
        nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->BOOT_RDY_ADDR, 1);

        mdelay(5);

        //---write BOOT_RDY status cmds---
        nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->BOOT_RDY_ADDR, 0);

        //---write POR_CD cmds---
        nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->POR_CD_ADDR, 0xA0);

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
        Novatek touchscreen check checksum function.
This function will compare file checksum and fw checksum.

return:
        n.a.
*******************************************************/
static int32_t Check_CheckSum(struct chip_data_nt36525 *chip_info)
{
        uint32_t fw_checksum = 0;
        uint32_t len = chip_info->partition * 4;
        uint32_t list = 0;
        int32_t ret = 0;

        memset(chip_info->fwbuf, 0, (len+1));

        //---set xdata index to checksum---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR);

        /* read checksum */
        chip_info->fwbuf[0] = (chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR) & 0x7F;
        ret = CTP_SPI_READ(chip_info->s_client, chip_info->fwbuf, len+1);
        if (ret) {
                TPD_INFO("Read fw checksum failed\n");
                return ret;
        }

        /*
         * Compare each checksum from fw
         * ILM + DLM + Overlay + Info
         * ilm_dlm_num (ILM & DLM) + ovly_sec_num + info_sec_num
         */
        for (list = 0; list < chip_info->partition; list++) {
                fw_checksum = byte_to_word(&chip_info->fwbuf[1+list*4]);

                /* ignore reserved partition (Reserved Partition size is zero) */
                if(!chip_info->bin_map[list].size)
                        continue;

                if (chip_info->bin_map[list].crc != fw_checksum) {
                        TPD_INFO("[%d] BIN_checksum=0x%08X, FW_checksum=0x%08X\n",
                                        list, chip_info->bin_map[list].crc, fw_checksum);

                        TPD_INFO("firmware checksum not match!!\n");
                        ret = -EBADF;
                        break;
                }
        }

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

        return ret;
}

/*******************************************************
Description:
        Novatek touchscreen Download_Firmware with HW CRC
function. It's complete download firmware flow.

return:
        n.a.
*******************************************************/
static int32_t Download_Firmware_HW_CRC(struct chip_data_nt36525 *chip_info, const struct firmware *fw)
{
        uint8_t retry = 0;
        int32_t ret = 0;

        do_gettimeofday(&start);

        while (1) {
                nvt_esd_check_enable(chip_info, true); //update esd trigger base time
                /*
                 * Send eng reset cmd before download FW
                 * Keep TP_RESX low when send eng reset cmd
                 */
                if (chip_info->hw_res->reset_gpio) {
                    gpio_set_value(chip_info->hw_res->reset_gpio, 1);
                }
                mdelay(10);     //wait tRSTA2BRST after TP_RST
                nvt_bootloader_reset(chip_info);

                /* Start Write Firmware Process */
                ret = Write_Partition(chip_info, fw->data, fw->size);
                if (ret) {
                        TPD_INFO("Write_Firmware failed. (%d)\n", ret);
                        goto fail;
                }

                /* clear fw reset status */
                nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);

                ret = Check_HW_CRC(chip_info, 0);       //Check overlay & Info
                if (ret) {
                        TPD_INFO("check hw crc failed, retry=%d\n", retry);
                        goto fail;
                }

                /* Set Boot Ready Bit */
                nvt_boot_ready(chip_info, true);

                ret = Check_HW_CRC(chip_info, 1);       //Check ILM & DLM
                if (ret) {
                        TPD_INFO("check hw crc failed, retry=%d\n", retry);
                        goto fail;
                }

                ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_INIT);
                if (ret) {
                        TPD_INFO("nvt_check_fw_reset_state_noflash failed. (%d)\n", ret);
                        goto fail;
                } else {
                        break;
                }

fail:
                retry++;
                if(unlikely(retry > 2)) {
                        TPD_INFO("error, retry=%d\n", retry);
                        break;
                }
        }

        do_gettimeofday(&end);

        return ret;
}

/*******************************************************
Description:
        Novatek touchscreen Download_Firmware function. It's
complete download firmware flow.

return:
        n.a.
*******************************************************/
static int32_t Download_Firmware(struct chip_data_nt36525 *chip_info, const struct firmware *fw)
{
        uint8_t retry = 0;
        int32_t ret = 0;

        do_gettimeofday(&start);

        while (1) {
                nvt_esd_check_enable(chip_info, true); //update esd trigger base time
                /*
                 * Send eng reset cmd before download FW
                 * Keep TP_RESX low when send eng reset cmd
                 */
                if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
                    gpio_set_value(chip_info->hw_res->reset_gpio, 0);
                }

                mdelay(1);      //wait 1ms
                nvt_eng_reset(chip_info);
                if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
                    gpio_set_value(chip_info->hw_res->reset_gpio, 1);
                }

                mdelay(10);      //wait tRSTA2BRST after TP_RST
                nvt_bootloader_reset(chip_info);

                /* clear fw reset status */
                nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);

                /* Start Write Firmware Process */
                ret = Write_Partition(chip_info, fw->data, fw->size);
                if (ret) {
                        TPD_INFO("Write_Firmware failed. (%d)\n", ret);
                        goto fail;
                }

                /* Set Boot Ready Bit */
                nvt_boot_ready(chip_info, true);

                ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_INIT);
                if (ret) {
                        TPD_INFO("nvt_check_fw_reset_state_noflash failed. (%d)\n", ret);
                        goto fail;
                }

                ret = Check_CheckSum(chip_info);        //Check FW checksum
                if (ret) {
                        TPD_INFO("check checksum failed, retry=%d\n", retry);
                        goto fail;
                } else {
                        break;
                }

fail:
                retry++;
                if(unlikely(retry > 2)) {
                        TPD_INFO("error, retry=%d\n", retry);
                        break;
                }
        }

        do_gettimeofday(&end);

        return ret;
}

/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
static int nvt_ftm_process(void *chip_data)
{
    int ret = -1;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
    const struct firmware *fw = NULL;

    TPD_INFO("%s is called!\n", __func__);
    ret = nvt_get_chip_info(chip_info);
    if (!ret) {
        ret = nvt_fw_update(chip_info, fw, 0);
        if(ret > 0) {
                TPD_INFO("%s fw update failed!\n", __func__);
        } else {
                ret = nvt_enter_sleep(chip_info, true);
        }
    }

    return ret;
}

static int nvt_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len-3] == 'i') && \
        (panel_data->fw_name[len-2] == 'm') && (panel_data->fw_name[len-1] == 'g')) {
        panel_data->fw_name[len-3] = 'b';
        panel_data->fw_name[len-2] = 'i';
        panel_data->fw_name[len-1] = 'n';
    }
    chip_info->tp_type = panel_data->tp_type;
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", chip_info->tp_type, panel_data->fw_name);

    return 0;
}

static int nvt_get_chip_info(void *chip_data)
{
    int ret = -1;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    //---check chip version trim---
    ret = nvt_ts_check_chip_ver_trim(chip_info);
    if (ret) {
        TPD_INFO("chip is not identified\n");
        ret = -EINVAL;
    }

    return ret;
}

static int nvt_power_control(void *chip_data, bool enable)
{
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
    }

    return 0;
}
static fw_check_state nvt_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    uint8_t ver_len = 0;
    int ret = 0;
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    ret |= nvt_get_fw_info_noflash(chip_info);
    if (ret < 0) {
        TPD_INFO("%s: get fw info failed\n", __func__);
        return FW_ABNORMAL;
    } else {
        panel_data->TP_FW = chip_info->fw_ver;
        sprintf(dev_version, "%02x", panel_data->TP_FW);
        if (panel_data->manufacture_info.version) {
            ver_len = strlen(panel_data->manufacture_info.version);
            strlcpy(&(panel_data->manufacture_info.version[12]), dev_version, 3);
        }
    }
    TPD_INFO("dev_version=%s panel_data->manufacture_info.version=%s\n",
             dev_version, panel_data->manufacture_info.version);
    return FW_NORMAL;
}

/*******************************************************
Description:
    Novatek touchscreen nvt_check_bin_checksum function.
Compare checksum from bin and calculated results to check
bin file is correct or not.

return:
    n.a.
*******************************************************/
static int32_t nvt_check_bin_checksum(const u8 *fwdata, size_t fwsize)
{
    uint32_t checksum_calculated = 0;
    uint32_t checksum_bin = 0;
    int32_t ret = 0;

    /* calculate the checksum reslut */
    checksum_calculated = CheckSum(fwdata, fwsize - FW_BIN_CHECKSUM_LEN - 1);
    /* get the checksum from file directly */
    checksum_bin = byte_to_word(fwdata + (fwsize - FW_BIN_CHECKSUM_LEN));

    if (checksum_calculated != checksum_bin) {
        TPD_INFO("%s checksum_calculated = 0x%08X\n", __func__, checksum_calculated);
        TPD_INFO("%s checksum_bin = 0x%08X\n", __func__, checksum_bin);
        ret = -EINVAL;
    }

    return ret;
}

static fw_update_state nvt_fw_update_choice(struct chip_data_nt36525 *chip_info, const struct firmware *fw, bool chk_ignore)
{
    int ret = 0;
    struct firmware *request_fw_headfile = NULL;

    //request firmware failed, get from headfile
    if(fw == NULL) {
        TPD_INFO("request firmware failed, get from headfile\n");
        request_fw_headfile = kzalloc(sizeof(struct firmware), GFP_KERNEL);
        if(request_fw_headfile == NULL) {
                TPD_INFO("%s kzalloc failed!\n", __func__);
                return FW_NO_NEED_UPDATE;
        }
        if (chip_info->tp_type == TP_AUO) {
            request_fw_headfile->size = sizeof(FIRMWARE_DATA_AUO);
            request_fw_headfile->data = FIRMWARE_DATA_AUO;
        } else if (chip_info->tp_type == TP_INNOLUX) {
            request_fw_headfile->size = sizeof(FIRMWARE_DATA_INNOLUX);
            request_fw_headfile->data = FIRMWARE_DATA_INNOLUX;
        } else if (chip_info->tp_type == TP_BOE) {
            request_fw_headfile->size = sizeof(FIRMWARE_DATA_BOE);
            request_fw_headfile->data = FIRMWARE_DATA_BOE;
        } else {
            request_fw_headfile->size = sizeof(FIRMWARE_DATA);
            request_fw_headfile->data = FIRMWARE_DATA;
        }

        fw = request_fw_headfile;
    }

    //check bin file size(116kb)
    if(fw->size != FW_BIN_SIZE) {
        TPD_INFO("bin file size not match. (%zu)\n", fw->size);
        goto init_fail;
    }

    // check if FW version add FW version bar equals 0xFF
    if (*(fw->data + FW_BIN_VER_OFFSET) + *(fw->data + FW_BIN_VER_BAR_OFFSET) != 0xFF) {
            TPD_INFO("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
            TPD_INFO("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n", *(fw->data+FW_BIN_VER_OFFSET), *(fw->data+FW_BIN_VER_BAR_OFFSET));
            goto init_fail;
    }

    //fw checksum compare
    ret = nvt_check_bin_checksum(fw->data, fw->size);
    if (ret) {
        if (fw != request_fw_headfile) {
            TPD_INFO("Image fw check checksum failed, reload fw from array\n");
            if (!chk_ignore) {
                goto init_fail;
            }
        } else {
            TPD_INFO("array fw check checksum failed, but use still\n");
        }
    } else {
        TPD_INFO("fw check checksum ok\n");
    }

    /* show fw type info (0:MP, 1:Normal)*/
    TPD_INFO("%s FW type is 0x%02X\n", __func__, *(fw->data + FW_BIN_TYPE_OFFSET));

    nvt_esd_check_enable(chip_info, true);
    /* BIN Header Parser */
    ret = nvt_bin_header_parser(chip_info, fw->data, fw->size);
    if (ret) {
            TPD_INFO("bin header parser failed\n");
            goto download_fail;
    }

    /* initial buffer and variable */
    ret = Download_Init(chip_info);
    if (ret) {
        TPD_INFO("Download Init failed. (%d)\n", ret);
        goto init_fail;
    }

    /* download firmware process */
        if (chip_info->trim_id_table.support_hw_crc)
                ret = Download_Firmware_HW_CRC(chip_info, fw);
        else
                ret = Download_Firmware(chip_info, fw);
        if (ret) {
                TPD_INFO("Download Firmware failed. (%d)\n", ret);
                goto download_fail;
        }

        TPD_INFO("Update firmware success! <%ld ms>\n",
                        ((end.tv_sec - start.tv_sec) * 1000000L + (end.tv_usec - start.tv_usec)) / 1000);

        /* Get FW Info */
        ret = nvt_get_fw_info_noflash(chip_info);
        if (ret) {
                TPD_INFO("nvt_get_fw_info_noflash failed. (%d)\n", ret);
                goto download_fail;
        }

        if(chip_info->bin_map != NULL) {
                kfree(chip_info->bin_map);
        }

        if(request_fw_headfile != NULL) {
                kfree(request_fw_headfile);
                request_fw_headfile = NULL;
                fw = NULL;
        }
        return FW_UPDATE_SUCCESS;

download_fail:
        if(chip_info->bin_map != NULL) {
                kfree(chip_info->bin_map);
        }
init_fail:
        if(request_fw_headfile != NULL) {
                kfree(request_fw_headfile);
                request_fw_headfile = NULL;
                fw = NULL;
        }
        if (ret == -EBADF) {
            return FW_UPDATE_ERROR;
        } else {
            return FW_NO_NEED_UPDATE;
        }
}
static u8 nvt_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    if ((gesture_enable == 1) && is_suspended) {
        return IRQ_GESTURE;
    } else if (is_suspended) {
        return IRQ_IGNORE;
    }

    return IRQ_TOUCH;
}

static int nvt_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    int obj_attention = 0;
    int i = 0;
    int32_t ret = -1;
    uint32_t position = 0;
    uint32_t input_x = 0;
    uint32_t input_y = 0;
    uint32_t input_w = 0;
    uint32_t input_p = 0;
    uint8_t pointid = 0;
    uint8_t point_data[POINT_DATA_LEN + 2] = {0};
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 2);
    if (ret < 0) {
        TPD_INFO("CTP_SPI_READ failed.(%d)\n", ret);
        return -1;
    }

    //some kind of protect mechanism, after WDT firware redownload and try to save tp
    ret = nvt_wdt_fw_recovery(chip_info, point_data);
    if (ret) {
        TPD_INFO("Recover for fw reset %02X\n", point_data[1]);
        nvt_reset(chip_info);
        return -1;
    }

    if (nvt_fw_recovery(point_data)) {  //receive 0x77
        nvt_esd_check_enable(chip_info, true);
        return -1;
    }

    for(i = 0; i < max_num; i++) {
        position = 1 + 6 * i;
        pointid = (uint8_t)(point_data[position + 0] >> 3) - 1;
        if (pointid >= max_num) {
            continue;
        }

        if (((point_data[position] & 0x07) == 0x01) || ((point_data[position] & 0x07) == 0x02)) {
            chip_info->irq_timer = jiffies;    //reset esd check trigger base time

            input_x = (uint32_t)(point_data[position + 1] << 4) + (uint32_t) (point_data[position + 3] >> 4);
            input_y = (uint32_t)(point_data[position + 2] << 4) + (uint32_t) (point_data[position + 3] & 0x0F);
            if ((input_x < 0) || (input_y < 0)) {
                continue;
            }

            input_w = (uint32_t)(point_data[position + 4]);
            if (input_w == 0) {
                input_w = 1;
            }
            if (i < 2) {
                input_p = (uint32_t)(point_data[position + 5]) + (uint32_t)(point_data[i + 63] << 8);
                if (input_p > 1000) {
                    input_p = 1000;
                }
            } else {
                input_p = (uint32_t)(point_data[position + 5]);
            }
            if (input_p == 0) {
                input_p = 1;
            }

            obj_attention = obj_attention | (1 << pointid);
            points[pointid].x = input_x;
            points[pointid].y = input_y;
            points[pointid].z = input_p;
            points[pointid].width_major = input_w;
            points[pointid].touch_major = input_w;
            points[pointid].status = 1;
        }
    }

    return obj_attention;
}

static int nvt_get_gesture_info(void *chip_data, struct gesture_info * gesture)
{
    uint8_t gesture_id = 0;
    uint8_t func_type = 0;
    int ret = -1;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
    uint8_t point_data[POINT_DATA_LEN + 2] = {0};

    memset(point_data, 0, sizeof(point_data));
    ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 2);
    if (ret < 0) {
        TPD_INFO("%s: read gesture data failed\n", __func__);
        return -1;
    }

    //some kind of protect mechanism, after WDT firware redownload and try to save tp
    ret = nvt_wdt_fw_recovery(chip_info, point_data);
    if (ret) {
        TPD_INFO("receive all %02X, no gesture interrupts. recover for fw reset\n",
                  point_data[1]);
        nvt_reset(chip_info);
        /* auto go back to wakeup gesture mode */
        ret = nvt_cmd_store(chip_info, 0x13);
        return 0;
    }

    if (nvt_fw_recovery(point_data)) {  //receive 0x77
        TPD_INFO("receive all 0x77, no gesture interrupts\n");
        return 0;
    }

    gesture_id = (uint8_t)(point_data[1] >> 3);
    func_type = (uint8_t)point_data[2];
    if ((gesture_id == 30) && (func_type == 1)) {
        gesture_id = (uint8_t)point_data[3];
    } else if (gesture_id > 30) {
        TPD_INFO("invalid gesture id= %d, no gesture event\n", gesture_id);
        return 0;
    }
    switch (gesture_id)     //judge gesture type
    {
        case RIGHT_SLIDE_DETECT :
            gesture->gesture_type  = Left2RightSwip;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case LEFT_SLIDE_DETECT :
            gesture->gesture_type  = Right2LeftSwip;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case DOWN_SLIDE_DETECT  :
            gesture->gesture_type  = Up2DownSwip;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case UP_SLIDE_DETECT :
            gesture->gesture_type  = Down2UpSwip;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case DTAP_DETECT:
            gesture->gesture_type  = DouTap;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end     = gesture->Point_start;
            break;

        case UP_VEE_DETECT :
            gesture->gesture_type  = UpVee;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case DOWN_VEE_DETECT :
            gesture->gesture_type  = DownVee;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case LEFT_VEE_DETECT:
            gesture->gesture_type = LeftVee;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case RIGHT_VEE_DETECT :
            gesture->gesture_type  = RightVee;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            break;

        case CIRCLE_DETECT  :
            gesture->gesture_type = Circle;
            gesture->clockwise = (point_data[43] == 0x20) ? 1 : 0;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;    //ymin
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            gesture->Point_2nd.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;  //xmin
            gesture->Point_2nd.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_3rd.x   = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;  //ymax
            gesture->Point_3rd.y   = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
            gesture->Point_4th.x   = (point_data[20] & 0xFF) | (point_data[21] & 0x0F) << 8;  //xmax
            gesture->Point_4th.y   = (point_data[22] & 0xFF) | (point_data[23] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[24] & 0xFF) | (point_data[25] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[26] & 0xFF) | (point_data[27] & 0x0F) << 8;
            break;

        case DOUSWIP_DETECT  :
            gesture->gesture_type  = DouSwip;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            gesture->Point_2nd.x   = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;
            gesture->Point_2nd.y   = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
            break;

        case M_DETECT  :
            gesture->gesture_type  = Mgestrue;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            gesture->Point_2nd.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_2nd.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_3rd.x   = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;
            gesture->Point_3rd.y   = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[20] & 0xFF) | (point_data[21] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[22] & 0xFF) | (point_data[23] & 0x0F) << 8;
            break;

        case W_DETECT :
            gesture->gesture_type  = Wgestrue;
            gesture->Point_start.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
            gesture->Point_start.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
            gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
            gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
            gesture->Point_2nd.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
            gesture->Point_2nd.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
            gesture->Point_3rd.x   = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;
            gesture->Point_3rd.y   = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
            gesture->Point_end.x   = (point_data[20] & 0xFF) | (point_data[21] & 0x0F) << 8;
            gesture->Point_end.y   = (point_data[22] & 0xFF) | (point_data[23] & 0x0F) << 8;
            break;

        default:
            gesture->gesture_type = UnkownGesture;
            break;
    }

    TPD_INFO("%s, gesture_id: 0x%x, func_type: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
                __func__, gesture_id, func_type, gesture->gesture_type, gesture->clockwise, \
                gesture->Point_start.x, gesture->Point_start.y, \
                gesture->Point_end.x, gesture->Point_end.y, \
                gesture->Point_1st.x, gesture->Point_1st.y, \
                gesture->Point_2nd.x, gesture->Point_2nd.y, \
                gesture->Point_3rd.x, gesture->Point_3rd.y, \
                gesture->Point_4th.x, gesture->Point_4th.y);

    return 0;
}

static int nvt_enable_jitter_mode(struct chip_data_nt36525 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        nvt_reset(chip_info);
    }

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_JITTER_ON);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_JITTER_OFF);
    }

    return ret;
}

static int nvt_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    switch(mode) {
        case MODE_NORMAL:
            ret = 0;
        break;

        case MODE_SLEEP:
            ret = nvt_enter_sleep(chip_info, true);
            if (ret < 0) {
                TPD_INFO("%s: nvt enter sleep failed\n", __func__);
            }
            nvt_esd_check_enable(chip_info, false);
        break;

        case MODE_GESTURE:
            ret = nvt_enable_black_gesture(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: nvt enable gesture failed.\n", __func__);
                return ret;
            }

            if (flag) {
                nvt_esd_check_enable(chip_info, false);
            }
            break;

        case MODE_EDGE:
            ret = nvt_enable_edge_limit(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: nvt enable edg limit failed.\n", __func__);
                return ret;
            }
            break;

        case MODE_CHARGE:
            ret = nvt_enable_charge_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_HEADSET:
            ret = nvt_enable_headset_mode(chip_info, flag);
            if (ret < 0) {
                TPD_INFO("%s: enable headset mode : %d failed\n", __func__, flag);
            }
            break;

        case MODE_GAME:
            ret = nvt_enable_jitter_mode(chip_info, flag);
            break;

        default:
            TPD_INFO("%s: Wrong mode.\n", __func__);
    }

    return ret;
}

//#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
//extern unsigned int upmu_get_rgs_chrdet(void);
//static int nvt_get_usb_state(void)
//{
//    return upmu_get_rgs_chrdet();
//}
//#else
//static int nvt_get_usb_state(void)
//{
//    return 0;
//}
//#endif

#if 0   //Roland
/*******************************************************
Description:
        Novatek touchscreen release update firmware function.

return:
        n.a.
*******************************************************/
static void update_firmware_release(const struct firmware *fw)
{
        if (fw) {
                release_firmware(fw);
        }

        fw = NULL;
}
#endif

static fw_update_state nvt_fw_update_for_common(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    copy_fw_to_buffer(chip_info, fw);

    ret = nvt_fw_update(chip_info, fw, 0);
    return ret;
}

int trigger_count = 0;
static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw, bool chk_ignore)
{
    int ret = 0;
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    ret = nvt_fw_update_choice(chip_info, fw, chk_ignore);
    if (ret != FW_UPDATE_SUCCESS) {
        TPD_INFO("firmware update failed, update with headfile!\n");
        ret = nvt_fw_update_choice(chip_info, NULL, chk_ignore);
    }
    if (ret != FW_UPDATE_SUCCESS && ret != FW_UPDATE_ERROR) {
        kobject_uevent(&chip_info->dev->kobj, KOBJ_CHANGE);
        TPD_INFO("tp_trigger_lcd_reset\n");
        tp_trigger_lcd_reset();
        trigger_count++;
    } else {
        trigger_count = 0;
    }
    if (trigger_count >= 5) {
        trigger_count = 0;
        TPD_INFO("TP firmware download failed exceed 5 times, triger panic\n");
        panic("TP firmware download failed exceed 5 times\n");
    }

    return ret;
}

static void nvt_black_screen_test(void *chip_data, char *message)
{
        int32_t *raw_data = NULL, *raw_data_n = NULL;
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
        int tx_num = chip_info->hw_res->TX_NUM;
        int rx_num = chip_info->hw_res->RX_NUM;
        const struct firmware *fw = NULL;
        struct nvt_test_header *ph = NULL;
        int i, j, iArrayIndex, err_cnt = 0;
        int fd = -1, ret = -1;
        mm_segment_t old_fs;
        char buf[128] = {0};
        uint8_t data_buf[128];
        int32_t buf_len = 0;
        struct timespec now_time;
        struct rtc_time rtc_now_time;
        char *p_node = NULL;
        char *fw_name_test = NULL;
        char *postfix = "_TEST.bin";
        uint8_t copy_len = 0;
		int32_t *lpwg_rawdata_P = NULL, *lpwg_rawdata_N = NULL;
        int32_t *lpwg_diff_rawdata_P = NULL, *lpwg_diff_rawdata_N = NULL;

        //update test firmware
        fw_name_test = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
        if(fw_name_test == NULL) {
                TPD_INFO("fw_name_test kzalloc error!\n");
                return;
        }

        p_node  = strstr(chip_info->fw_name, ".");
        copy_len = p_node - chip_info->fw_name;
        memcpy(fw_name_test, chip_info->fw_name, copy_len);
        strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
        TPD_INFO("%s : fw_name_test is %s\n", __func__, fw_name_test);

        //update test firmware
        ret = request_firmware(&fw, fw_name_test, chip_info->dev);
        if (ret != 0) {
                TPD_INFO("request test firmware failed! ret = %d\n", ret);
                kfree(fw_name_test);
                fw_name_test = NULL;
                return;
        }

        ret = nvt_fw_update(chip_info, fw, 1);
        if(ret > 0) {
                TPD_INFO("fw update failed!\n");
                goto RELEASE_FIRMWARE;
        }

        TPD_INFO("%s : update test firmware successed\n", __func__);

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
        ret = nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
        TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success");
        msleep(100);

        if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
                TPD_INFO("switch frequency hopping disable failed!\n");
                sprintf(message, "1 error, switch frequency hopping disable failed!\n");
                goto RELEASE_FIRMWARE;
        }

        if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
                TPD_INFO("check fw reset state failed!\n");
                sprintf(message, "1 error, check fw reset state failed!\n");
                goto RELEASE_FIRMWARE;
        }

        msleep(100);

        //---Enter Test Mode---
        if (nvt_clear_fw_status(chip_info)) {
                TPD_INFO("clear fw status failed!\n");
                sprintf(message, "1 error, clear fw status failed!\n");
                goto RELEASE_FIRMWARE;
        }

        nvt_change_mode(chip_info, TEST_MODE_2);

        if (nvt_check_fw_status(chip_info)) {
                TPD_INFO("check fw status failed!\n");
                sprintf(message, "1 error, clear fw status failed!\n");
                goto RELEASE_FIRMWARE;
        }

        if (nvt_get_fw_info_noflash(chip_info)) {
                TPD_INFO("get fw info failed!\n");
                sprintf(message, "1 error, get fw info failed!\n");
                goto RELEASE_FIRMWARE;
        }

        TPD_INFO("malloc raw_data space\n");
        buf_len = tx_num * rx_num * sizeof(int32_t);
        raw_data = kzalloc(buf_len, GFP_KERNEL);
        raw_data_n = kzalloc(buf_len, GFP_KERNEL);
        if (!(raw_data && raw_data_n)) {
                TPD_INFO("kzalloc space failed\n");
                sprintf(message, "1 error, kzalloc space failed\n");
                if (raw_data)
                    kfree(raw_data);
                if (raw_data_n)
                    kfree(raw_data_n);
                return;
        }

        ret = request_firmware(&fw, chip_info->test_limit_name, &chip_info->s_client->dev);
        TPD_INFO("Roland--->fw path is %s\n", chip_info->test_limit_name);
        if (ret < 0) {
                TPD_INFO("Request firmware failed - %s (%d)\n", chip_info->test_limit_name, ret);
                sprintf(message, "1 error, Request firmware failed: %s\n", chip_info->test_limit_name);
                kfree(raw_data);
                kfree(raw_data_n);
                raw_data = NULL;
                raw_data_n = NULL;
                return;
        }
        ph = (struct nvt_test_header *)(fw->data);

        //create a file to store test data in /sdcard/ScreenOffTpTestReport
        getnstimeofday(&now_time);
        rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
        snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOff/tp_testlimit_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
        old_fs = get_fs();
        set_fs(KERNEL_DS);
        sys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
        fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (fd < 0) {
                TPD_INFO("Open log file '%s' failed.\n", data_buf);
                err_cnt++;
                sprintf(buf, "Open log file '%s' failed.\n", data_buf);
                goto OUT;
        }

        lpwg_rawdata_P = (int32_t *)(fw->data + ph->array_LPWG_Rawdata_P_offset);
        lpwg_rawdata_N = (int32_t *)(fw->data + ph->array_LPWG_Rawdata_N_offset);
        lpwg_diff_rawdata_P = (int32_t *)(fw->data + ph->array_LPWG_Diff_P_offset);
        lpwg_diff_rawdata_N = (int32_t *)(fw->data + ph->array_LPWG_Diff_N_offset);
        //---FW Rawdata Test---
        TPD_INFO("LPWG mode FW Rawdata Test \n");
        memset(raw_data, 0, buf_len);
        if (nvt_get_fw_pipe(chip_info) == 0)
                nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR, raw_data, buf_len);
        else
                nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR, raw_data, buf_len);
        store_to_file(fd, "LPWG mode FW Rawdata:\n");
        if ((ph->config_Lmt_LPWG_Rawdata_P != 0) && (ph->config_Lmt_LPWG_Rawdata_N != 0)) {
	        for (j = 0; j < rx_num; j++) {
	                for (i = 0; i < tx_num; i++) {
	                        iArrayIndex = j * tx_num + i;
	                        TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
	                        if (fd >= 0) {
	                                store_to_file(fd, "%d, ", raw_data[iArrayIndex]);
	                        }
	                        if((raw_data[iArrayIndex] > ph->config_Lmt_LPWG_Rawdata_P) \
	                                || (raw_data[iArrayIndex] < ph->config_Lmt_LPWG_Rawdata_N)) {
	                                TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d[%d %d]\n", 
	                                        i, j, raw_data[iArrayIndex], ph->config_Lmt_LPWG_Rawdata_N, ph->config_Lmt_LPWG_Rawdata_P);
	                                if (!err_cnt) {
	                                        sprintf(buf, "LPWG Rawdata[%d][%d] = %d[%d %d]\n",
	                                                i, j, raw_data[iArrayIndex], ph->config_Lmt_LPWG_Rawdata_N, ph->config_Lmt_LPWG_Rawdata_P);
	                                }
	                                err_cnt++;
	                        }
	                }
	                if (fd >= 0) {
	                        store_to_file(fd, "\n");
	                }
	                TPD_DEBUG_NTAG("\n");
	        }
	    } else {
	        for (j = 0; j < rx_num; j++) {
	            for (i = 0; i < tx_num; i++) {
	                iArrayIndex = j * tx_num + i;
	                TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
	                if (fd >= 0) {
	                    store_to_file(fd, "%d, ", raw_data[iArrayIndex]);
	                }
	                if((raw_data[iArrayIndex] > lpwg_rawdata_P[iArrayIndex]) \
	                        || (raw_data[iArrayIndex] < lpwg_rawdata_N[iArrayIndex])) {
	                    TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d\n", i, j, raw_data[iArrayIndex]);
	                    if (!err_cnt) {
	                        sprintf(buf, "LPWG Rawdata[%d][%d] = %d[%d %d]\n",
	                            i, j, raw_data[iArrayIndex], lpwg_rawdata_N[iArrayIndex], lpwg_rawdata_P[iArrayIndex]);
	                    }
	                    err_cnt++;
	                }
	            }
	            if (fd >= 0) {
	                store_to_file(fd, "\n");
	            }
	            TPD_DEBUG_NTAG("\n");
	        }
	    }

        //---Leave Test Mode---
        nvt_change_mode(chip_info, NORMAL_MODE);

        //---Noise Test---
        TPD_INFO("LPWG mode FW Noise Test \n");
        memset(raw_data, 0, buf_len);  //store max
        memset(raw_data_n, 0, buf_len); //store min
        if (nvt_read_fw_noise(chip_info, ph->config_Diff_Test_Frame, raw_data, raw_data_n, buf_len) != 0) {
                TPD_INFO("LPWG mode read Noise data failed!\n");    // 1: ERROR
                sprintf(buf, "LPWG mode read Noise data failed!\n");
                err_cnt++;
                goto OUT;
        }
        TPD_INFO("LPWG Noise RawData_Diff_Max:\n");
        store_to_file(fd, "LPWG Noise RawData_Diff_Max:\n");
    	if ((ph->config_Lmt_LPWG_Diff_P != 0) && (ph->config_Lmt_LPWG_Diff_N != 0)) {
	        for (j = 0; j < rx_num; j++) {
	                for (i = 0; i < tx_num; i++) {
	                        iArrayIndex = j * tx_num + i;
	                        TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
	                        if (fd >= 0) {
	                                store_to_file(fd, "%d, ", raw_data[iArrayIndex]);
	                        }
	                        if((raw_data[iArrayIndex] > ph->config_Lmt_LPWG_Diff_P) \
	                                || (raw_data[iArrayIndex] < ph->config_Lmt_LPWG_Diff_N)) {
	                                TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d[%d %d]\n",
	                                        i, j, raw_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N, ph->config_Lmt_LPWG_Diff_P);
	                                if (!err_cnt) {
	                                        sprintf(buf, "LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
	                                                i, j, raw_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N, ph->config_Lmt_LPWG_Diff_P);
	                                }
	                                err_cnt++;
	                        }
	                }
	                if (fd >= 0) {
	                        store_to_file(fd, "\n");
	                }
	                TPD_DEBUG_NTAG("\n");
	        }
	        TPD_INFO("LPWG Noise RawData_Diff_Min:\n");
	        store_to_file(fd, "LPWG Noise RawData_Diff_Min:\n");
	        for (j = 0; j < rx_num; j++) {
	                for (i = 0; i < tx_num; i++) {
	                        iArrayIndex = j * tx_num + i;
	                        TPD_DEBUG_NTAG("%d, ", raw_data_n[iArrayIndex]);
	                        if (fd >= 0) {
	                                store_to_file(fd, "%d, ", raw_data_n[iArrayIndex]);
	                        }
	                        if((raw_data_n[iArrayIndex] > ph->config_Lmt_LPWG_Diff_P) \
	                                || (raw_data_n[iArrayIndex] < ph->config_Lmt_LPWG_Diff_N)) {
	                                TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d[%d %d]\n",
	                                        i, j, raw_data_n[iArrayIndex], ph->config_Lmt_LPWG_Diff_N,  ph->config_Lmt_LPWG_Diff_P);
	                                if (!err_cnt) {
	                                        sprintf(buf, "LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
	                                                i, j, raw_data_n[iArrayIndex], ph->config_Lmt_LPWG_Diff_N,  ph->config_Lmt_LPWG_Diff_P);
	                                }
	                                err_cnt++;
	                        }
	                }
	                if (fd >= 0) {
	                        store_to_file(fd, "\n");
	                }
	                TPD_DEBUG_NTAG("\n");
	        }
	    } else {
	        for (j = 0; j < rx_num; j++) {
	            for (i = 0; i < tx_num; i++) {
	                iArrayIndex = j * tx_num + i;
	                TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
	                if (fd >= 0) {
	                    store_to_file(fd, "%d, ", raw_data[iArrayIndex]);
	                }
	                if((raw_data[iArrayIndex] > lpwg_diff_rawdata_P[iArrayIndex]) \
	                        || (raw_data[iArrayIndex] < lpwg_diff_rawdata_N[iArrayIndex])) {
	                    TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d\n", i, j, raw_data[iArrayIndex]);
	                    if (!err_cnt) {
	                        sprintf(buf, "LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
	                            i, j, raw_data[iArrayIndex], lpwg_diff_rawdata_N[iArrayIndex], lpwg_diff_rawdata_P[iArrayIndex]);
	                    }
	                    err_cnt++;
	                }
	            }
	            if (fd >= 0) {
	                store_to_file(fd, "\n");
	            }
	            TPD_DEBUG_NTAG("\n");
	        }
	        TPD_INFO("LPWG Noise RawData_Diff_Min:\n");
	        store_to_file(fd, "LPWG Noise RawData_Diff_Min:\n");
	        for (j = 0; j < rx_num; j++) {
	            for (i = 0; i < tx_num; i++) {
	                iArrayIndex = j * tx_num + i;
	                TPD_DEBUG_NTAG("%d, ", raw_data_n[iArrayIndex]);
	                if (fd >= 0) {
	                    store_to_file(fd, "%d, ", raw_data_n[iArrayIndex]);
	                }
	                if((raw_data_n[iArrayIndex] > lpwg_diff_rawdata_P[iArrayIndex]) \
	                        || (raw_data_n[iArrayIndex] < lpwg_diff_rawdata_N[iArrayIndex])) {
	                    TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d\n", i, j, raw_data_n[iArrayIndex]);
	                    if (!err_cnt) {
	                        sprintf(buf, "LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
	                            i, j, raw_data_n[iArrayIndex], lpwg_diff_rawdata_N[iArrayIndex], lpwg_diff_rawdata_P[iArrayIndex]);
	                    }
	                    err_cnt++;
	                }
	            }
	            if (fd >= 0) {
	                store_to_file(fd, "\n");
	            }
	            TPD_DEBUG_NTAG("\n");
	        }
	    }

OUT:
        if (fd >= 0) {
                sys_close(fd);
        }
        set_fs(old_fs);

        if (raw_data)
                kfree(raw_data);
        if (raw_data_n)
                kfree(raw_data_n);
        sprintf(message, "%d errors. %s", err_cnt, buf);
        TPD_INFO("%d errors. %s", err_cnt, buf);
RELEASE_FIRMWARE:
        release_firmware(fw);
        kfree(fw_name_test);
        fw_name_test = NULL;
}

static bool nvt_fw_recovery(uint8_t *point_data)
{
    uint8_t i = 0;
    bool detected = true;

    /* check pattern */
    for (i=1 ; i<7 ; i++) {
        if (point_data[i] != 0x77) {
            detected = false;
            break;
        }
    }

    return detected;
}

static uint8_t nvt_wdt_fw_recovery(struct chip_data_nt36525 *chip_info, uint8_t *point_data)
{
    uint32_t recovery_cnt_max = 10;
    uint8_t recovery_enable = false;
    uint8_t i = 0;

    chip_info->recovery_cnt++;

    /* Pattern Check */
    for (i=1 ; i<7 ; i++) {
        if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
            chip_info->recovery_cnt = 0;
            break;
        }
    }

    if (chip_info->recovery_cnt > recovery_cnt_max){
        recovery_enable = true;
        chip_info->recovery_cnt = 0;
    }

    if (chip_info->recovery_cnt) {
        TPD_INFO("%s recovery_cnt=%d  point_data[1]=0x%x ", __func__, chip_info->recovery_cnt, point_data[i]);
    }

    return recovery_enable;
}

static void nvt_esd_check_enable(struct chip_data_nt36525 *chip_info, bool enable)
{
    TPD_DEBUG("%s enable=%d\n", __func__, enable);

    /* update interrupt timer */
    chip_info->irq_timer = jiffies;
    /* enable/disable esd check flag */
    chip_info->esd_check_enabled = enable;
    /* clear esd_retry counter, if protect function is enabled */
    chip_info->esd_retry = enable ? 0 : chip_info->esd_retry;
}

static int nvt_esd_handle(void *chip_data)
{
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
    unsigned int timer = jiffies_to_msecs(jiffies - chip_info->irq_timer);

    //TPD_INFO("esd_check = %d (retry %d)\n", chip_info->esd_check_enabled, chip_info->esd_retry);    //DEBUG

    // if (chip_info->esd_retry >= 5)
        // nvt_esd_check_enable(chip_info, false);

    if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && chip_info->esd_check_enabled) {
        TPD_INFO("do ESD recovery, timer = %d, retry = %d\n", timer, chip_info->esd_retry);
        /* do esd recovery, bootloader reset */
        tp_touch_btnkey_release();
        nvt_reset(chip_info);
        /* update interrupt timer */
        chip_info->irq_timer = jiffies;
        /* update esd_retry counter */
        chip_info->esd_retry++;
    }

    return 0;
}

static void nvt_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

        chip_info->touch_direction = dir;
}

static uint8_t nvt_get_touch_direction(void *chip_data)
{
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

        return chip_info->touch_direction;
}

static struct oplus_touchpanel_operations nvt_ops = {
    .ftm_process                = nvt_ftm_process,
    .get_vendor                 = nvt_get_vendor,
    .get_chip_info              = nvt_get_chip_info,
    .reset                      = nvt_reset,
    .power_control              = nvt_power_control,
    .fw_check                   = nvt_fw_check,
    .fw_update                  = nvt_fw_update_for_common,
    .trigger_reason             = nvt_trigger_reason,
    .get_touch_points           = nvt_get_touch_points,
    .get_gesture_info           = nvt_get_gesture_info,
    .mode_switch                = nvt_mode_switch,
//    .get_usb_state              = nvt_get_usb_state,
    .black_screen_test          = nvt_black_screen_test,
    .esd_handle                 = nvt_esd_handle,
    .set_touch_direction        = nvt_set_touch_direction,
    .get_touch_direction        = nvt_get_touch_direction,
};

static int32_t nvt_clear_fw_status(struct chip_data_nt36525 *chip_info)
{
        uint8_t buf[8] = {0};
        int32_t i = 0;
        const int32_t retry = 20;

        for (i = 0; i < retry; i++) {
                //---set xdata index to EVENT BUF ADDR---
                nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

                //---clear fw status---
                buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
                buf[1] = 0x00;
                CTP_SPI_WRITE(chip_info->s_client, buf, 2);

                //---read fw status---
                buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
                buf[1] = 0xFF;
                CTP_SPI_READ(chip_info->s_client, buf, 2);

                if (buf[1] == 0x00)
                        break;

                msleep(10);
        }

        if (i >= retry) {
                TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
                return -1;
        } else {
                return 0;
        }
}

static void nvt_change_mode(struct chip_data_nt36525 *chip_info, uint8_t mode)
{
        uint8_t buf[8] = {0};

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

        //---set mode---
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = mode;
        CTP_SPI_WRITE(chip_info->s_client, buf, 2);

        if (mode == NORMAL_MODE) {
                buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
                buf[1] = HANDSHAKING_HOST_READY;
                CTP_SPI_WRITE(chip_info->s_client, buf, 2);
                msleep(20);
        }
}

static int32_t nvt_check_fw_status(struct chip_data_nt36525 *chip_info)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 50;

    for (i = 0; i < retry; i++) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

        //---read fw status---
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = 0x00;
        CTP_SPI_READ(chip_info->s_client, buf, 2);

        if ((buf[1] & 0xF0) == 0xA0)
            break;

        msleep(10);
    }

    if (i >= retry) {
        TPD_INFO("%s failed, i=%d, buf[1]=0x%02X\n", __func__, i, buf[1]);
        return -1;
    } else {
        return 0;
    }
}

static uint8_t nvt_get_fw_pipe(struct chip_data_nt36525 *chip_info)
{
    int ret = -1;
    uint8_t buf[8]= {0};

    //---set xdata index to EVENT BUF ADDR---
    ret = nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

    //---read fw status---
    buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
    buf[1] = 0x00;
    ret |= CTP_SPI_READ(chip_info->s_client, buf, 2);
    if (ret < 0) {
        TPD_INFO("%s: read or write failed\n", __func__);
    }

    return (buf[1] & 0x01);
}

static uint8_t nvt_get_fw_pipe_noflash(struct chip_data_nt36525 *chip_info)
{
    int ret = -1;
    uint8_t buf[8]= {0};

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

    //---read fw status---
    buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
    buf[1] = 0x00;
    ret |= CTP_SPI_READ(chip_info->s_client, buf, 2);
    if (ret < 0) {
        TPD_INFO("%s: read or write failed\n", __func__);
    }

    return (buf[1] & 0x01);
}

static void nvt_read_mdata(struct chip_data_nt36525 *chip_info, uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len)
{
        int32_t i = 0;
        int32_t j = 0;
        int32_t k = 0;
        uint8_t buf[SPI_TANSFER_LENGTH + 1] = {0};
        uint32_t head_addr = 0;
        int32_t dummy_len = 0;
        int32_t data_len = 0;
        int32_t residual_len = 0;
        uint8_t *xdata_tmp = NULL;

        //---set xdata sector address & length---
        head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
        dummy_len = xdata_addr - head_addr;
        data_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2;
        residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

        if (xdata_len/sizeof(int32_t) < data_len/2) {
                TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n", xdata_len, data_len);
                return;
        }

        //malloc buffer space
        xdata_tmp = kzalloc(2048 ,GFP_KERNEL);
        if (xdata_tmp == NULL) {
                TPD_INFO("%s malloc memory failed\n", __func__);
                return;
        }

        //read xdata : step 1
        for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
                //---read xdata by SPI_TANSFER_LENGTH
                for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
                        //---change xdata index---
                        nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) + (SPI_TANSFER_LENGTH * j));

                        //---read data---
                        buf[0] = SPI_TANSFER_LENGTH * j;
                        CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

                        //---copy buf to xdata_tmp---
                        for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
                                xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
                                //printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k));
                        }
                }
                //printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));
        }

        //read xdata : step2
        if (residual_len != 0) {
                //---read xdata by SPI_TANSFER_LENGTH
                for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
                        //---change xdata index---
                        nvt_set_page(chip_info, xdata_addr + data_len - residual_len + (SPI_TANSFER_LENGTH * j));

                        //---read data---
                        buf[0] = SPI_TANSFER_LENGTH * j;
                        CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

                        //---copy buf to xdata_tmp---
                        for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
                                xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
                                //printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k));
                        }
                }
                //printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
        }

        //---remove dummy data and 2bytes-to-1data---
        for (i = 0; i < (data_len / 2); i++) {
                xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
        }

        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

        kfree(xdata_tmp);
}


static void store_to_file(int fd, char* format, ...)
{
    va_list args;
    char buf[64] = {0};

    va_start(args, format);
    vsnprintf(buf, 64, format, args);
    va_end(args);

    if(fd >= 0) {
        sys_write(fd, buf, strlen(buf));
    }
}
static void nvt_data_read(struct seq_file *s, struct chip_data_nt36525 *chip_info, DEBUG_READ_TYPE read_type)
{
        int ret = -1;
        int i, j;
        uint8_t pipe;
        int32_t *xdata = NULL;
        int32_t buf_len = 0;

        TPD_INFO("nvt clear fw status start\n");
        ret = nvt_clear_fw_status(chip_info);
        if (ret < 0) {
                TPD_INFO("clear_fw_status error, return\n");
                return;
        }

        nvt_change_mode(chip_info, TEST_MODE_2);
        TPD_INFO("nvt check fw status start\n");
        ret = nvt_check_fw_status(chip_info);
        if (ret < 0) {
                TPD_INFO("check_fw_status error, return\n");
                return;
        }

        TPD_INFO("nvt get fw info start");
        ret = nvt_get_fw_info_noflash(chip_info);
        if (ret < 0) {
                TPD_INFO("get_fw_info error, return\n");
                return;
        }

        buf_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * sizeof(int32_t);
        xdata = kzalloc(buf_len ,GFP_KERNEL);
        if (!xdata) {
                TPD_INFO("%s, malloc memory failed\n", __func__);
                return;
        }

        pipe = nvt_get_fw_pipe_noflash(chip_info);
        TPD_INFO("nvt_get_fw_pipe:%d\n", pipe);
        switch (read_type) {
        case NVT_RAWDATA:
                seq_printf(s, "raw_data:\n");
                if (pipe == 0)
                        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR, xdata, buf_len);
                else
                        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR, xdata, buf_len);
        break;
        case NVT_DIFFDATA:
                seq_printf(s, "diff_data:\n");
                if (pipe == 0)
                        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, xdata, buf_len);
                else
                        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, xdata, buf_len);
        break;
        case NVT_BASEDATA:
                seq_printf(s, "basline_data:\n");
                nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->BASELINE_ADDR, xdata, buf_len);
                break;
                default:
                seq_printf(s, "read type not support\n");
        break;
        }

        nvt_change_mode(chip_info, NORMAL_MODE);
        TPD_INFO("change normal mode end\n");

        //print all data
        for (i = 0; i < chip_info->hw_res->RX_NUM; i++) {
                seq_printf(s, "[%2d]", i);
                for (j = 0; j < chip_info->hw_res->TX_NUM; j++) {
                        seq_printf(s, "%5d, ", xdata[i * chip_info->hw_res->TX_NUM + j]);
                }
                seq_printf(s, "\n");
        }

        kfree(xdata);
}

static void nvt_delta_read(struct seq_file *s, void *chip_data)
{
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

        nvt_esd_check_enable(chip_info, false);
        nvt_data_read(s, chip_info, NVT_DIFFDATA);
        nvt_esd_check_enable(chip_info, true);
}

static void nvt_baseline_read(struct seq_file *s, void *chip_data)
{
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

        nvt_esd_check_enable(chip_info, false);
        nvt_data_read(s, chip_info, NVT_BASEDATA);
        nvt_data_read(s, chip_info, NVT_RAWDATA);
        nvt_esd_check_enable(chip_info, true);
}

static void nvt_main_register_read(struct seq_file *s, void *chip_data)
{
    uint8_t buf[3];
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;

    if (!chip_info)
        return ;

    nvt_esd_check_enable(chip_info, false);
    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---read cmd status---
    buf[0] = 0x5E;
    buf[1] = 0xFF;
    CTP_SPI_READ(chip_info->s_client, buf, 2);

    seq_printf(s, "PWR_FLAG:%d\n", (buf[1]>> PWR_FLAG) & 0x01);
    seq_printf(s, "EDGE_REGECT:%d\n", (buf[1]>> EDGE_REGECT) & 0x01);
    seq_printf(s, "HEADSET_FLAG:%d\n", (buf[1]>> HEADSET_FLAG) & 0x01);
    nvt_esd_check_enable(chip_info, true);
}

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read    = nvt_limit_read,
    .baseline_read = nvt_baseline_read,
    .delta_read = nvt_delta_read,
    .main_register_read = nvt_main_register_read,
};

static int8_t nvt_switch_FreqHopEnDis(struct chip_data_nt36525 *chip_info, uint8_t FreqHopEnDis)
{
    uint8_t buf[8] = {0};
    uint8_t retry = 0;
    int8_t ret = 0;

    for (retry = 0; retry < 20; retry++) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

        //---switch FreqHopEnDis---
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = FreqHopEnDis;
        CTP_SPI_WRITE(chip_info->s_client, buf, 2);

        msleep(35);

        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, 2);

        if (buf[1] == 0x00)
            break;
    }

    if (unlikely(retry == 20)) {
        TPD_INFO("switch FreqHopEnDis 0x%02X failed, buf[1]=0x%02X\n", FreqHopEnDis, buf[1]);
        ret = -1;
    }

    return ret;
}

static void nvt_enable_noise_collect(struct chip_data_nt36525 *chip_info, int32_t frame_num)
{
    int ret = -1;
    uint8_t buf[8] = {0};

    //---set xdata index to EVENT BUF ADDR---
    ret = nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

    //---enable noise collect---
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0x47;
    buf[2] = 0xAA;
    buf[3] = frame_num;
    buf[4] = 0x00;
    ret |= CTP_SPI_WRITE(chip_info->s_client, buf, 5);
    if (ret < 0) {
        TPD_INFO("%s failed\n", __func__);
    }
}
static int32_t nvt_polling_hand_shake_status(struct chip_data_nt36525 *chip_info)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 50;

    for (i = 0; i < retry; i++) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

        //---read fw status---
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = 0x00;
        CTP_SPI_READ(chip_info->s_client, buf, 2);

        if ((buf[1] == 0xA0) || (buf[1] == 0xA1))
            break;

        msleep(10);
    }

    if (i >= retry) {
        TPD_INFO("polling hand shake status failed, buf[1]=0x%02X\n", buf[1]);
        return -1;
    } else {
        return 0;
    }
}

static int32_t nvt_read_fw_noise(struct chip_data_nt36525 *chip_info, int32_t config_Diff_Test_Frame, int32_t *xdata, int32_t *xdata_n, int32_t xdata_len)
{
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t iArrayIndex = 0;
    int32_t frame_num = 0;

    if (xdata_len/sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
        TPD_INFO("read fw nosie buffer(%d) less than data size(%d)\n", xdata_len, chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM);
        return -1;
    }

    //---Enter Test Mode---
    if (nvt_clear_fw_status(chip_info)) {
        return -EAGAIN;
    }

    frame_num = config_Diff_Test_Frame / 10;
    if (frame_num <= 0)
        frame_num = 1;
    TPD_INFO("%s: frame_num=%d\n", __func__, frame_num);
    nvt_enable_noise_collect(chip_info, frame_num);
    // need wait PS_Config_Diff_Test_Frame * 8.3ms
    msleep(frame_num * 83);

    if (nvt_polling_hand_shake_status(chip_info)) {
        return -EAGAIN;
    }

    if (nvt_get_fw_info_noflash(chip_info)) {
        return -EAGAIN;
    }

    if (nvt_get_fw_pipe(chip_info) == 0) {
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, xdata, xdata_len);
    } else {
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, xdata, xdata_len);
    }

    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
        for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
            iArrayIndex = y * chip_info->hw_res->TX_NUM + x;
            xdata_n[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF);
            xdata[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF);
        }
    }

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);
    return 0;
}
static void nvt_enable_doze_noise_collect(struct chip_data_nt36525 *chip_info, int32_t frame_num)
{
    int ret = -1;
    uint8_t buf[8] = {0};

    //---set xdata index to EVENT BUF ADDR---
    ret = nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---enable noise collect---
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0x4B;
    buf[2] = 0xAA;
    buf[3] = frame_num;
    buf[4] = 0x00;
    ret |= CTP_SPI_WRITE(chip_info->s_client, buf, 5);
    if (ret < 0) {
        TPD_INFO("%s failed\n", __func__);
    }
}

static int32_t nvt_read_doze_fw_noise(struct chip_data_nt36525 *chip_info, int32_t config_Doze_Noise_Test_Frame, int32_t doze_X_Channel,int32_t *xdata, int32_t *xdata_n, int32_t xdata_len)
{
    uint8_t buf[128] = {0};
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t rx_num = chip_info->hw_res->RX_NUM;
    int32_t iArrayIndex = 0;
    int32_t frame_num = 0;

    if (xdata_len/sizeof(int32_t) < rx_num * doze_X_Channel) {
        TPD_INFO("read doze nosie buffer(%d) less than data size(%d)\n", xdata_len, rx_num * doze_X_Channel);
        return -1;
    }

    //---Enter Test Mode---
    if (nvt_clear_fw_status(chip_info)) {
        return -EAGAIN;
    }

    frame_num = config_Doze_Noise_Test_Frame / 10;
    if (frame_num <= 0)
        frame_num = 1;
    TPD_INFO("%s: frame_num=%d\n", __func__, frame_num);
    nvt_enable_doze_noise_collect(chip_info, frame_num);
    // need wait PS_Config_Doze_Noise_Test_Frame * 8.3ms
    msleep(frame_num * 250);

    if (nvt_polling_hand_shake_status(chip_info)) {
        return -EAGAIN;
    }

    for (x = 0; x < doze_X_Channel; x++) {
        //---change xdata index---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + rx_num * doze_X_Channel * x);

        //---read data---
        buf[0] = (chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + rx_num * doze_X_Channel * x) & 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, rx_num * 2 + 1);

        for (y = 0; y < rx_num; y++) {
            xdata[y * doze_X_Channel + x] = (uint16_t)(buf[y * doze_X_Channel + 1] + 256 * buf[y * doze_X_Channel + 2]);
        }
    }

    for (y = 0; y < rx_num; y++) {
        for (x = 0; x < doze_X_Channel; x++) {
            iArrayIndex = y * doze_X_Channel + x;
            xdata_n[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF) * 4;
            xdata[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF) * 4;    //scaling up
        }
    }

    //---Leave Test Mode---
    //nvt_change_mode(NORMAL_MODE);    //No return to normal mode. Continuous to get doze rawdata

    return 0;
}

static int32_t nvt_read_doze_baseline(struct chip_data_nt36525 *chip_info, int32_t doze_X_Channel, int32_t *xdata, int32_t xdata_len)
{
    uint8_t buf[256] = {0};
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t rm_num = chip_info->hw_res->RX_NUM;
    int32_t iArrayIndex = 0;

    //---Enter Test Mode---
    //nvt_change_mode(TEST_MODE_2);

    //if (nvt_check_fw_status()) {
    //    return -EAGAIN;
    //}
    if (xdata_len/sizeof(int32_t) < rm_num * doze_X_Channel) {
        TPD_INFO("read doze baseline buffer(%d) less than data size(%d)\n", xdata_len, rm_num * doze_X_Channel);
        return -1;
    }

    for (x = 0; x < doze_X_Channel; x++) {
        //---change xdata index---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR + rm_num * doze_X_Channel * x);

        //---read data---
        buf[0] = (chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR + rm_num * doze_X_Channel * x) & 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, rm_num * 2 + 1);
        for (y = 0; y < rm_num; y++) {
            xdata[y * 2 + x] = (uint16_t)(buf[y * 2 + 1] + 256 * buf[y * 2 + 2]);
        }
    }

    for (y = 0; y < rm_num; y++) {
        for (x = 0; x < doze_X_Channel; x++) {
            iArrayIndex = y * doze_X_Channel + x;
            xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
        }
    }

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);
    return 0;
}

static void nvt_enable_short_test(struct chip_data_nt36525 *chip_info)
{
    uint8_t buf[8] = {0};

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---enable short test---
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0x43;
    buf[2] = 0xAA;
    buf[3] = 0x02;
    buf[4] = 0x00;
    CTP_SPI_WRITE(chip_info->s_client, buf, 5);
}

static int32_t nvt_read_fw_short(struct chip_data_nt36525 *chip_info, int32_t *xdata, int32_t xdata_len)
{
    uint32_t raw_pipe_addr = 0;
    uint8_t *rawdata_buf = NULL;
    uint32_t x = 0;
    uint32_t y = 0;
    uint8_t buf[128] = {0};
    int32_t iArrayIndex = 0;

    if (xdata_len/sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
        TPD_INFO("read fw short buffer(%d) less than data size(%d)\n", xdata_len, chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM);
        return -1;
    }

    //---Enter Test Mode---
    if (nvt_clear_fw_status(chip_info)) {
        return -EAGAIN;
    }

    nvt_enable_short_test(chip_info);

    if (nvt_polling_hand_shake_status(chip_info)) {
        return -EAGAIN;
    }

    rawdata_buf = (uint8_t *)kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, GFP_KERNEL);
    if (!rawdata_buf) {
        TPD_INFO("kzalloc for rawdata_buf failed!\n");
        return -ENOMEM;
    }

    if (nvt_get_fw_pipe(chip_info) == 0)
        raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR;
    else
        raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR;

    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
        //---change xdata index---
        nvt_set_page(chip_info, raw_pipe_addr + y * chip_info->hw_res->TX_NUM * 2);
        buf[0] = (uint8_t)((raw_pipe_addr + y * chip_info->hw_res->TX_NUM * 2) & 0xFF);
        CTP_SPI_READ(chip_info->s_client, buf, chip_info->hw_res->TX_NUM * 2 + 1);
        memcpy(rawdata_buf + y * chip_info->hw_res->TX_NUM * 2, buf + 1, chip_info->hw_res->TX_NUM * 2);
    }

    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
        for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
            iArrayIndex = y * chip_info->hw_res->TX_NUM + x;
            xdata[iArrayIndex] = (int16_t)(rawdata_buf[iArrayIndex * 2] + 256 * rawdata_buf[iArrayIndex * 2 + 1]);
        }
    }

    if (rawdata_buf) {
        kfree(rawdata_buf);
        rawdata_buf = NULL;
    }

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);
    return 0;
}

static void nvt_enable_open_test(struct chip_data_nt36525 *chip_info)
{
    uint8_t buf[8] = {0};

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---enable open test---
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0x45;
    buf[2] = 0xAA;
    buf[3] = 0x02;
    buf[4] = 0x00;
    CTP_SPI_WRITE(chip_info->s_client, buf, 5);
}

static int32_t nvt_read_fw_open(struct chip_data_nt36525 *chip_info, int32_t *xdata, int32_t xdata_len)
{
    uint32_t raw_pipe_addr = 0;
    uint8_t *rawdata_buf = NULL;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t tx_num = chip_info->hw_res->TX_NUM;
    uint32_t rx_num = chip_info->hw_res->RX_NUM;
    uint8_t buf[128] = {0};

    if (xdata_len/sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
        TPD_INFO("read fw open buffer(%d) less than data size(%d)\n", xdata_len, chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM);
        return -1;
    }

    //---Enter Test Mode---
    if (nvt_clear_fw_status(chip_info)) {
        return -EAGAIN;
    }

    nvt_enable_open_test(chip_info);

    if (nvt_polling_hand_shake_status(chip_info)) {
        return -EAGAIN;
    }

    rawdata_buf = (uint8_t *)kzalloc(tx_num * rx_num * 2, GFP_KERNEL);
    if (!rawdata_buf) {
        TPD_INFO("kzalloc for rawdata_buf failed!\n");
        return -ENOMEM;
    }

    if (nvt_get_fw_pipe(chip_info) == 0)
        raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR;
    else
        raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR;

    for (y = 0; y < rx_num; y++) {
        //---change xdata index---
        nvt_set_page(chip_info, raw_pipe_addr + y * tx_num * 2);
        buf[0] = (uint8_t)((raw_pipe_addr + y * tx_num * 2) & 0xFF);
        CTP_SPI_READ(chip_info->s_client, buf, tx_num * 2 + 1);
        memcpy(rawdata_buf + y * tx_num * 2, buf + 1, tx_num * 2);
    }

    for (y = 0; y < rx_num; y++) {
        for (x = 0; x < tx_num; x++) {
            xdata[y * tx_num + x] = (int16_t)((rawdata_buf[(y*tx_num + x)*2] + 256 * rawdata_buf[(y*tx_num + x)*2 + 1]));
        }
    }

    if (rawdata_buf) {
        kfree(rawdata_buf);
        rawdata_buf = NULL;
    }

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);
    return 0;
}

static void nvt_auto_test(struct seq_file *s, void *chip_data, struct nvt_testdata *nvt_testdata)
{
        int32_t *raw_data = NULL;
        int32_t *raw_data_n = NULL;
        int i, j, buf_len = 0;
        int err_cnt = 0;
        int32_t iArrayIndex = 0;
        int ret = -1;
        const struct firmware *fw = NULL;
        struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)chip_data;
        struct nvt_test_header *ph = NULL;
        int32_t *fw_rawdata_P = NULL, *fw_rawdata_N = NULL;
        int32_t *open_rawdata_P = NULL, *open_rawdata_N = NULL;
        int32_t *short_rawdata_P = NULL, *short_rawdata_N = NULL;
        int32_t *diff_rawdata_P = NULL, *diff_rawdata_N = NULL;
        int32_t *cc_data_P = NULL, *cc_data_N = NULL;
        int32_t *doze_rawdata_P = NULL, *doze_rawdata_N = NULL;
        int32_t *doze_diff_rawdata_P = NULL, *doze_diff_rawdata_N = NULL;
        char *p_node = NULL;
        char *fw_name_test = NULL;
        char *postfix = "_TEST.bin";
        uint8_t copy_len = 0;

        fw_name_test = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
        if(fw_name_test == NULL) {
                TPD_INFO("fw_name_test kzalloc error!\n");
                return;
        }

        p_node  = strstr(chip_info->fw_name, ".");
        copy_len = p_node - chip_info->fw_name;
        memcpy(fw_name_test, chip_info->fw_name, copy_len);
        strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
        TPD_INFO("fw_name_test is %s\n", fw_name_test);

        //update test firmware
        ret = request_firmware(&fw, fw_name_test, chip_info->dev);
        if (ret != 0) {
                TPD_INFO("request test firmware failed! ret = %d\n", ret);
                kfree(fw_name_test);
                fw_name_test = NULL;
                return;
        }

        nvt_esd_check_enable(chip_info, false);

        ret = nvt_fw_update(chip_info, fw, 1);
        if(ret > 0) {
                TPD_INFO("fw update failed!\n");
                goto RELEASE_FIRMWARE;
        }
        TPD_INFO("update test firmware successed!\n");

        if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
                TPD_INFO("switch frequency hopping disable failed!\n");
                store_to_file(nvt_testdata->fd, "switch frequency hopping disable failed!\n");
                seq_printf(s, "switch frequency hopping disable failed!\n");
                goto RELEASE_FIRMWARE;
        }

        if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
                TPD_INFO("check fw reset state failed!\n");
                store_to_file(nvt_testdata->fd, "check fw reset state failed!\n");
                seq_printf(s, "check fw reset state failed!\n");
                goto RELEASE_FIRMWARE;
        }

        msleep(100);

        //---Enter Test Mode---
        TPD_INFO("enter test mode\n");
        if (nvt_clear_fw_status(chip_info)) {
                TPD_INFO("clear fw status failed!\n");
                store_to_file(nvt_testdata->fd, "clear fw status failed!\n");
                seq_printf(s, "clear fw status failed!\n");
                goto RELEASE_FIRMWARE;
        }

        nvt_change_mode(chip_info, MP_MODE_CC);
        if (nvt_check_fw_status(chip_info)) {
                TPD_INFO("check fw status failed!\n");
                store_to_file(nvt_testdata->fd, "check fw status failed!\n");
                seq_printf(s, "check fw status failed!\n");
                goto RELEASE_FIRMWARE;
        }

        if (nvt_get_fw_info_noflash(chip_info)) {
                TPD_INFO("get fw info failed!\n");
                store_to_file(nvt_testdata->fd, "get fw info failed!\n");
                seq_printf(s, "get fw info failed!\n");
                goto RELEASE_FIRMWARE;
        }

        TPD_INFO("malloc raw_data space\n");
        buf_len = nvt_testdata->TX_NUM * nvt_testdata->RX_NUM * sizeof(int32_t);
        raw_data = kzalloc(buf_len, GFP_KERNEL);
        raw_data_n = kzalloc(buf_len, GFP_KERNEL);
        if (!(raw_data && raw_data_n)) {
                err_cnt++;
                store_to_file(nvt_testdata->fd, "malloc memory failed!\n");
                seq_printf(s, "malloc memory failed!\n");
                goto OUT;
        }

        //get test data
        ph = (struct nvt_test_header *)(nvt_testdata->fw->data);
        fw_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_fw_rawdata_P_offset);
        fw_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_fw_rawdata_N_offset);
        open_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_open_rawdata_P_offset);
        open_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_open_rawdata_N_offset);
        short_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_Short_Rawdata_P_offset);
        short_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_Short_Rawdata_N_offset);
        diff_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_Diff_P_offset);
        diff_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_Diff_N_offset);
        cc_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_CC_P_offset);
        cc_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_CC_N_offset);
        doze_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_Doze_Rawdata_P_offset);
        doze_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_Doze_Rawdata_N_offset);
        doze_diff_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_Doze_Diff_P_offset);
        doze_diff_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_Doze_Diff_N_offset);

        //---FW Rawdata Test---
        TPD_INFO("FW Rawdata Test \n");
        memset(raw_data, 0, buf_len);
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->BASELINE_ADDR, raw_data, buf_len);
        store_to_file(nvt_testdata->fd, "rawData:\n");
        for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                        iArrayIndex = j * nvt_testdata->TX_NUM + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > fw_rawdata_P[iArrayIndex]) \
                                || (raw_data[iArrayIndex] < fw_rawdata_N[iArrayIndex])) {
                                TPD_INFO("rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], fw_rawdata_N[iArrayIndex], fw_rawdata_P[iArrayIndex]);
                                if (!err_cnt) {
                                        seq_printf(s, "rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], fw_rawdata_N[iArrayIndex], fw_rawdata_P[iArrayIndex]);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
        }

        TPD_INFO("FW cc data test \n");
        memset(raw_data, 0, buf_len);
        if (nvt_get_fw_pipe(chip_info) == 0)
            nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, raw_data, buf_len);
        else
            nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, raw_data, buf_len);
        store_to_file(nvt_testdata->fd, "ccData:\n");
        if ((ph->config_Lmt_FW_CC_P != 0) && (ph->config_Lmt_FW_CC_N != 0)) {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                        iArrayIndex = j * nvt_testdata->TX_NUM + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > ph->config_Lmt_FW_CC_P) \
                                || (raw_data[iArrayIndex] < ph->config_Lmt_FW_CC_N)) {
                                TPD_INFO("cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_FW_CC_N, ph->config_Lmt_FW_CC_P);
                                if (!err_cnt) {
                                        seq_printf(s, "cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_FW_CC_N, ph->config_Lmt_FW_CC_P);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
            }
        } else {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                    iArrayIndex = j * nvt_testdata->TX_NUM + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                    }
                    if((raw_data[iArrayIndex] > cc_data_P[iArrayIndex]) \
                            || (raw_data[iArrayIndex] < cc_data_N[iArrayIndex])) {
                        TPD_INFO("cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], cc_data_N[iArrayIndex], cc_data_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], cc_data_N[iArrayIndex], cc_data_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
        }

        //---Leave Test Mode---
        nvt_change_mode(chip_info, NORMAL_MODE);

        //---Noise Test---
        TPD_INFO("FW Noise Test \n");
        memset(raw_data, 0, buf_len);  //store max
        memset(raw_data_n, 0, buf_len); //store min
        if (nvt_read_fw_noise(chip_info, ph->config_Diff_Test_Frame, raw_data, raw_data_n, buf_len) != 0) {
                TPD_INFO("read Noise data failed!\n");
                store_to_file(nvt_testdata->fd, "read Noise data failed!\n");
                seq_printf(s, "read Noise data failed!\n");
                err_cnt++;
                goto OUT;
        }

        store_to_file(nvt_testdata->fd, "RawData_Diff_Max:\n");
        if ((ph->config_Lmt_FW_Diff_P != 0) && (ph->config_Lmt_FW_Diff_N != 0)) {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                        iArrayIndex = j * nvt_testdata->TX_NUM + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > ph->config_Lmt_FW_Diff_P) \
                                || (raw_data[iArrayIndex] < ph->config_Lmt_FW_Diff_N)) {
                                TPD_INFO("Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_FW_Diff_N, ph->config_Lmt_FW_Diff_P);
                                if (!err_cnt) {
                                        seq_printf(s, "Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_FW_Diff_N, ph->config_Lmt_FW_Diff_P);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
            }

            store_to_file(nvt_testdata->fd, "RawData_Diff_Min:\n");
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                    for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                            iArrayIndex = j * nvt_testdata->TX_NUM + i;
                            if (nvt_testdata->fd >= 0) {
                                    store_to_file(nvt_testdata->fd, "%d, ", raw_data_n[iArrayIndex]);
                            }
                            if((raw_data_n[iArrayIndex] > ph->config_Lmt_FW_Diff_P) \
                                    || (raw_data_n[iArrayIndex] < ph->config_Lmt_FW_Diff_N)) {
                                    TPD_INFO("Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], ph->config_Lmt_FW_Diff_N, ph->config_Lmt_FW_Diff_P);
                                    if (!err_cnt) {
                                            seq_printf(s, "Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], ph->config_Lmt_FW_Diff_N, ph->config_Lmt_FW_Diff_P);
                                    }
                                    err_cnt++;
                            }
                    }
                    if (nvt_testdata->fd >= 0) {
                            store_to_file(nvt_testdata->fd, "\n");
                    }
            }
        } else {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                    iArrayIndex = j * nvt_testdata->TX_NUM + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                    }
                    if((raw_data[iArrayIndex] > diff_rawdata_P[iArrayIndex]) \
                            || (raw_data[iArrayIndex] < diff_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
            store_to_file(nvt_testdata->fd, "RawData_Diff_Min:\n");
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                    iArrayIndex = j * nvt_testdata->TX_NUM + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data_n[iArrayIndex]);
                    }
                    if((raw_data_n[iArrayIndex] > diff_rawdata_P[iArrayIndex]) \
                            || (raw_data_n[iArrayIndex] < diff_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
        }

        //---Doze Noise Test---
        TPD_INFO("Doze FW Noise Test \n");
        memset(raw_data, 0, buf_len);  //store max
        memset(raw_data_n, 0, buf_len); //store min
        if (nvt_read_doze_fw_noise(chip_info, ph->config_Doze_Noise_Test_Frame, ph->doze_X_Channel, raw_data, raw_data_n, buf_len) != 0) {
                TPD_INFO("read Doze Noise data failed!\n");
                store_to_file(nvt_testdata->fd, "read Doze Noise data failed!\n");
                seq_printf(s, "read Doze Noise data failed!\n");
                err_cnt++;
                goto OUT;
        }

        store_to_file(nvt_testdata->fd, "Doze RawData_Diff_Max:\n");
        if ((ph->config_Lmt_Doze_Diff_P != 0) && (ph->config_Lmt_Doze_Diff_N != 0)) {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < ph->doze_X_Channel; i++) {
                        iArrayIndex = j * ph->doze_X_Channel + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > ph->config_Lmt_Doze_Diff_P) \
                                || (raw_data[iArrayIndex] < ph->config_Lmt_Doze_Diff_N)) {
                                TPD_INFO("Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Doze_Diff_N, ph->config_Lmt_Doze_Diff_P);
                                if (!err_cnt) {
                                        seq_printf(s, "Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Doze_Diff_N, ph->config_Lmt_Doze_Diff_P);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
            }

            store_to_file(nvt_testdata->fd, "Doze RawData_Diff_Min:\n");
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                    for (i = 0; i < ph->doze_X_Channel; i++) {
                            iArrayIndex = j * ph->doze_X_Channel + i;
                            if (nvt_testdata->fd >= 0) {
                                    store_to_file(nvt_testdata->fd, "%d, ", raw_data_n[iArrayIndex]);
                            }
                            if((raw_data_n[iArrayIndex] > ph->config_Lmt_Doze_Diff_P) \
                                    || (raw_data_n[iArrayIndex] < ph->config_Lmt_Doze_Diff_N)) {
                                    TPD_INFO("Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], ph->config_Lmt_Doze_Diff_N, ph->config_Lmt_Doze_Diff_P);
                                    if (!err_cnt) {
                                            seq_printf(s, "Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], ph->config_Lmt_Doze_Diff_N, ph->config_Lmt_Doze_Diff_P);
                                    }
                                    err_cnt++;
                            }
                    }
                    if (nvt_testdata->fd >= 0) {
                            store_to_file(nvt_testdata->fd, "\n");
                    }
            }
        } else {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < ph->doze_X_Channel; i++) {
                    iArrayIndex = j * ph->doze_X_Channel + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                    }
                    if((raw_data[iArrayIndex] > doze_diff_rawdata_P[iArrayIndex]) \
                            || (raw_data[iArrayIndex] < doze_diff_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
            store_to_file(nvt_testdata->fd, "Doze RawData_Diff_Min:\n");
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < ph->doze_X_Channel; i++) {
                    iArrayIndex = j * ph->doze_X_Channel + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data_n[iArrayIndex]);
                    }
                    if((raw_data_n[iArrayIndex] > doze_diff_rawdata_P[iArrayIndex]) \
                            || (raw_data_n[iArrayIndex] < doze_diff_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data_n[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
        }

        //---Doze FW Rawdata Test---
        TPD_INFO("Doze FW Rawdata Test \n");
        memset(raw_data, 0, buf_len);
        if(nvt_read_doze_baseline(chip_info, ph->doze_X_Channel, raw_data, buf_len) != 0) {
                TPD_INFO("read Doze FW Rawdata failed!\n");
                store_to_file(nvt_testdata->fd, "read Doze FW Rawdata failed!\n");
                seq_printf(s, "read Doze FW Rawdata failed!\n");
                err_cnt++;
                goto OUT;
        }

        store_to_file(nvt_testdata->fd, "Doze FW Rawdata:\n");
        if ((ph->config_Lmt_Doze_Rawdata_P != 0) && (ph->config_Lmt_Doze_Rawdata_N != 0)) {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < ph->doze_X_Channel; i++) {
                iArrayIndex = j * ph->doze_X_Channel + i;
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                }
                if((raw_data[iArrayIndex] > ph->config_Lmt_Doze_Rawdata_P) \
                        || (raw_data[iArrayIndex] < ph->config_Lmt_Doze_Rawdata_N)) {
                                TPD_INFO("Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Doze_Rawdata_N, ph->config_Lmt_Doze_Rawdata_P);
                                if (!err_cnt) {
                                        seq_printf(s, "Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Doze_Rawdata_N, ph->config_Lmt_Doze_Rawdata_P);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
        }
        } else {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < ph->doze_X_Channel; i++) {
                    iArrayIndex = j * ph->doze_X_Channel + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                    }
                    if((raw_data[iArrayIndex] > doze_rawdata_P[iArrayIndex]) \
                            || (raw_data[iArrayIndex] < doze_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], doze_rawdata_N[iArrayIndex], doze_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], doze_rawdata_N[iArrayIndex], doze_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
        }

        //--Short Test---
        TPD_INFO("FW Short Test \n");
        memset(raw_data, 0, buf_len);
        if (nvt_read_fw_short(chip_info, raw_data, buf_len) != 0) {
                TPD_INFO("read Short test data failed!\n");
                store_to_file(nvt_testdata->fd, "read Short test data failed!\n");
                seq_printf(s, "read Short test data failed!\n");
                err_cnt++;
                goto OUT;
        }
        store_to_file(nvt_testdata->fd, "RawData_Short:\n");
        if ((ph->config_Lmt_Short_Rawdata_P != 0) && (ph->config_Lmt_Short_Rawdata_N != 0)) {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                        iArrayIndex = j * nvt_testdata->TX_NUM + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > ph->config_Lmt_Short_Rawdata_P) \
                                || (raw_data[iArrayIndex] < ph->config_Lmt_Short_Rawdata_N)) {
                                TPD_INFO("Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Short_Rawdata_N, ph->config_Lmt_Short_Rawdata_P);
                                if (!err_cnt) {
                                        seq_printf(s, "Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], ph->config_Lmt_Short_Rawdata_N, ph->config_Lmt_Short_Rawdata_P);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
        }
        } else {
            for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                    iArrayIndex = j * nvt_testdata->TX_NUM + i;
                    if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                    }
                    if((raw_data[iArrayIndex] > short_rawdata_P[iArrayIndex]) \
                            || (raw_data[iArrayIndex] < short_rawdata_N[iArrayIndex])) {
                        TPD_INFO("Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], short_rawdata_N[iArrayIndex], short_rawdata_P[iArrayIndex]);
                        if (!err_cnt) {
                            seq_printf(s, "Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], short_rawdata_N[iArrayIndex], short_rawdata_P[iArrayIndex]);
                        }
                        err_cnt++;
                    }
                }
                if (nvt_testdata->fd >= 0) {
                    store_to_file(nvt_testdata->fd, "\n");
                }
            }
        }

        //---Open Test---
        TPD_INFO("FW Open Test \n");
        memset(raw_data, 0, buf_len);
        if (nvt_read_fw_open(chip_info, raw_data, buf_len) != 0) {
                TPD_INFO("read Open test data failed!\n");
                store_to_file(nvt_testdata->fd, "read Open test data failed!\n");
                seq_printf(s, "read Open test data failed!\n");
                err_cnt++;
                goto OUT;
        }
        store_to_file(nvt_testdata->fd, "RawData_Open:\n");
        for (j = 0; j < nvt_testdata->RX_NUM; j++) {
                for (i = 0; i < nvt_testdata->TX_NUM; i++) {
                        iArrayIndex = j * nvt_testdata->TX_NUM + i;
                        if (nvt_testdata->fd >= 0) {
                                store_to_file(nvt_testdata->fd, "%d, ", raw_data[iArrayIndex]);
                        }
                        if((raw_data[iArrayIndex] > open_rawdata_P[iArrayIndex]) \
                                || (raw_data[iArrayIndex] < open_rawdata_N[iArrayIndex])) {
                                TPD_INFO("Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], open_rawdata_N[iArrayIndex], open_rawdata_P[iArrayIndex]);
                                if (!err_cnt) {
                                        seq_printf(s, "Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], open_rawdata_N[iArrayIndex], open_rawdata_P[iArrayIndex]);
                                }
                                err_cnt++;
                        }
                }
                if (nvt_testdata->fd >= 0) {
                        store_to_file(nvt_testdata->fd, "\n");
                }
        }

OUT:
        if (raw_data) {
                kfree(raw_data);
        }
        if (raw_data_n) {
                kfree(raw_data_n);
        }
RELEASE_FIRMWARE:
        nvt_esd_check_enable(chip_info, true);
        seq_printf(s, "FW:0x%llx\n", nvt_testdata->TP_FW);
        seq_printf(s, "%d error(s). %s\n", err_cnt, err_cnt?"":"All test passed.");
        TPD_INFO(" TP auto test %d error(s). %s\n", err_cnt, err_cnt?"":"All test passed.");
        release_firmware(fw);
        kfree(fw_name_test);
        return;
}

static struct nvt_proc_operations nvt_proc_ops = {
    .auto_test     = nvt_auto_test,
};

/*********** Start of SPI Driver and Implementation of it's callbacks*************************/
static int nvt_tp_probe(struct spi_device *client)
{
    struct chip_data_nt36525 *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    /* 1. alloc chip info */
    chip_info = kzalloc(sizeof(struct chip_data_nt36525), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(chip_info, 0, sizeof(*chip_info));
    g_chip_info = chip_info;

    /* 2. Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }
    memset(ts, 0, sizeof(*ts));

    /* 3. bind client and dev for easy operate */
    chip_info->s_client = client;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->s_client = client;
    ts->irq = client->irq;
    spi_set_drvdata(client, ts);
    ts->dev = &client->dev;
    ts->chip_data = chip_info;
    chip_info->hw_res = &ts->hw_res;
    chip_info->ENG_RST_ADDR = 0x7FFF80;
    chip_info->recovery_cnt = 0;
    chip_info->partition = 0;
    chip_info->ilm_dlm_num = 2;

    //---prepare for spi parameter---
    if (ts->s_client->master->flags & SPI_MASTER_HALF_DUPLEX) {
        TPD_INFO("Full duplex not supported by master\n");
        ret = -EIO;
        goto err_spi_setup;
    }
    ts->s_client->bits_per_word = 8;
    ts->s_client->mode = SPI_MODE_0;
    memcpy(&chip_info->spi_ctrl, &spi_ctrdata, sizeof(struct mt_chip_conf));
    TPD_INFO("mode=%d, max_speed_hz=%d\n", ts->s_client->mode, ts->s_client->max_speed_hz);
    ts->s_client->controller_data = (void *)&chip_info->spi_ctrl;
    ret = spi_setup(ts->s_client);
    if (ret < 0) {
        TPD_INFO("Failed to perform SPI setup\n");
        goto err_spi_setup;
    }

    /* 4. file_operations callbacks binding */
    ts->ts_ops = &nvt_ops;

    /* 5. register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    ts->tp_suspend_order = TP_LCD_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
    chip_info->is_sleep_writed = false;
    chip_info->fw_name = ts->panel_data.fw_name;
    chip_info->dev = ts->dev;
    chip_info->test_limit_name = ts->panel_data.test_limit_name;
    mutex_init(&chip_info->reset_lock);

    //reset esd handle time interval
    if (ts->esd_handle_support) {
        chip_info->esd_check_enabled = false;
        ts->esd_info.esd_work_time = msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD); // change esd check interval to 1.5s
        TPD_INFO("%s:change esd handle time to %d ms\n", __func__, ts->esd_info.esd_work_time/HZ);
    }

    /*6. create nvt test files*/
    nvt_flash_proc_init(ts, "NVTSPI");
    nvt_create_proc(ts, &nvt_proc_ops);

    TPD_INFO("%s, probe normal end\n", __func__);
    return 0;

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

err_spi_setup:
    spi_set_drvdata(client, NULL);

ts_malloc_failed:
    kfree(chip_info);
    chip_info = NULL;
    ret = -1;

    TPD_INFO("%s, probe error\n", __func__);
    return ret;
}

static int nvt_tp_remove(struct spi_device *client)
{
    struct touchpanel_data *ts = spi_get_drvdata(client);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int nvt_spi_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct chip_data_nt36525 *chip_info = (struct chip_data_nt36525 *)(ts->chip_data);

    TPD_INFO("%s: is called\n", __func__);
    nvt_esd_check_enable(chip_info, false);
    tp_i2c_suspend(ts);

    return 0;
}

static int nvt_spi_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);

    tp_i2c_resume(ts);

    return 0;
}

static const struct spi_device_id tp_id[] =
{
    {TPD_DEVICE, 0},
    {},
};

static struct of_device_id tp_match_table[] =
{
    { .compatible = TPD_DEVICE, },
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = nvt_spi_suspend,
    .resume = nvt_spi_resume,
#endif
};

static struct spi_driver tp_spi_driver = {
    .probe      = nvt_tp_probe,
    .remove     = nvt_tp_remove,
    .id_table   = tp_id,
    .driver = {
        .name   = TPD_DEVICE,
        .owner  = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
};


static int32_t __init nvt_driver_init(void)
{
    TPD_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE))
        return -1;

    if (spi_register_driver(&tp_spi_driver)!= 0) {
        TPD_INFO("unable to add spi driver.\n");
        return -1;
    }
    return 0;
}

static void __exit nvt_driver_exit(void)
{
    spi_unregister_driver(&tp_spi_driver);
}

module_init(nvt_driver_init);
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");
