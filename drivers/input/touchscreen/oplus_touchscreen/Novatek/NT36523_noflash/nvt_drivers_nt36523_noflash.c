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
#include <linux/sysfs.h>
#include <linux/of_gpio.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>


#include <linux/version.h>


//#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include "nvt_drivers_nt36523_noflash.h"

/*******Part0:LOG TAG Declear********************/

static ktime_t start, end;

/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "novatek,nf_nt36523"
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
#define MESSAGE_SIZE              (256)

static int8_t nvt_cmd_store(struct chip_data_nt36523 *chip_info, uint8_t u8Cmd);

static fw_update_state nvt_fw_update_sub(void *chip_data, const struct firmware *fw, bool force);
static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw, bool force);
static int nvt_reset(void *chip_data);
static int nvt_get_chip_info(void *chip_data);
static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length);
static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length);
static int nvt_enable_pen_mode(struct chip_data_nt36523 *chip_info, bool enable);
static struct chip_data_nt36523 *g_chip_info = NULL;
static size_t fw_need_write_size = 0;

void __attribute__((weak)) switch_spi7cs_state(bool normal) {return;}
/***************************** start of id map table******************************************/
static const struct nvt_ts_mem_map NT36523_memory_map = {
	.EVENT_BUF_ADDR           = 0x2FE00,
	.RAW_PIPE0_ADDR           = 0x30FA0,
	.RAW_PIPE1_ADDR           = 0x30FA0,
	.BASELINE_ADDR            = 0x36510,
	.BASELINE_BTN_ADDR        = 0,
	.DIFF_PIPE0_ADDR          = 0x373E8,
	.DIFF_PIPE1_ADDR          = 0x38068,
	.RAW_BTN_PIPE0_ADDR       = 0,
	.RAW_BTN_PIPE1_ADDR       = 0,
	.DIFF_BTN_PIPE0_ADDR      = 0,
	.DIFF_BTN_PIPE1_ADDR      = 0,
	.PEN_2D_BL_TIP_X_ADDR     = 0x2988A,
	.PEN_2D_BL_TIP_Y_ADDR     = 0x29A1A,
	.PEN_2D_BL_RING_X_ADDR    = 0x29BAA,
	.PEN_2D_BL_RING_Y_ADDR    = 0x29D3A,
	.PEN_2D_DIFF_TIP_X_ADDR   = 0x29ECA,
	.PEN_2D_DIFF_TIP_Y_ADDR   = 0x2A05A,
	.PEN_2D_DIFF_RING_X_ADDR  = 0x2A1EA,
	.PEN_2D_DIFF_RING_Y_ADDR  = 0x2A37A,
	.PEN_2D_RAW_TIP_X_ADDR    = 0x2A50A,
	.PEN_2D_RAW_TIP_Y_ADDR    = 0x2A69A,
	.PEN_2D_RAW_RING_X_ADDR   = 0x2A82A,
	.PEN_2D_RAW_RING_Y_ADDR   = 0x2A9BA,
	.PEN_1D_DIFF_TIP_X_ADDR   = 0x2AB4A,
	.PEN_1D_DIFF_TIP_Y_ADDR   = 0x2ABAE,
	.PEN_1D_DIFF_RING_X_ADDR  = 0x2AC12,
	.PEN_1D_DIFF_RING_Y_ADDR  = 0x2AC76,
	.READ_FLASH_CHECKSUM_ADDR = 0x24000,
	.RW_FLASH_DATA_ADDR       = 0x24002,
	.DOZE_GM_S1D_SCAN_RAW_ADDR = 0x31F40,
	.DOZE_GM_BTN_SCAN_RAW_ADDR = 0,
	/* Phase 2 Host Download */
	.BOOT_RDY_ADDR            = 0x3F10D,
	.TX_AUTO_COPY_EN          = 0x3F7E8,
	.SPI_DMA_TX_INFO          = 0x3F7F1,
	/* BLD CRC */
	.BLD_LENGTH_ADDR          = 0x3F138,	//0x3F138 ~ 0x3F13A	(3 bytes)
	.ILM_LENGTH_ADDR          = 0x3F118,	//0x3F118 ~ 0x3F11A	(3 bytes)
	.DLM_LENGTH_ADDR          = 0x3F130,	//0x3F130 ~ 0x3F132	(3 bytes)
	.BLD_DES_ADDR             = 0x3F114,	//0x3F114 ~ 0x3F116	(3 bytes)
	.ILM_DES_ADDR             = 0x3F128,	//0x3F128 ~ 0x3F12A	(3 bytes)
	.DLM_DES_ADDR             = 0x3F12C,	//0x3F12C ~ 0x3F12E	(3 bytes)
	.G_ILM_CHECKSUM_ADDR      = 0x3F100,	//0x3F100 ~ 0x3F103	(4 bytes)
	.G_DLM_CHECKSUM_ADDR      = 0x3F104,	//0x3F104 ~ 0x3F107	(4 bytes)
	.R_ILM_CHECKSUM_ADDR      = 0x3F120,	//0x3F120 ~ 0x3F123 (4 bytes)
	.R_DLM_CHECKSUM_ADDR      = 0x3F124,	//0x3F124 ~ 0x3F127 (4 bytes)
	.BLD_CRC_EN_ADDR          = 0x3F30E,
	.DMA_CRC_EN_ADDR          = 0x3F136,
	.BLD_ILM_DLM_CRC_ADDR     = 0x3F133,
	.DMA_CRC_FLAG_ADDR        = 0x3F134,
};



static const struct nvt_ts_trim_id_table trim_id_table[] = {
	{
		.id = {0x20, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
        .support_hw_crc = 2
	},
	{
		.id = {0x0C, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
        .support_hw_crc = 2
	},
	{
		.id = {0x0B, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
        .support_hw_crc = 2
	},
	{
		.id = {0x0A, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
        .support_hw_crc = 2
	},
	{
		.id = {0xFF, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {0, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
        .support_hw_crc = 2
	},
};

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
static const struct mtk_chip_config spi_ctrdata = {
    .rx_mlsb = 1,
    .tx_mlsb = 1,
    .cs_pol = 0,
    .cs_setuptime = 30,
};
#else
static const struct mt_chip_conf spi_ctrdata = {
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
#endif //CONFIG_SPI_MT65XX
#endif // end of  CONFIG_TOUCHPANEL_MTK_PLATFORM

/*******************************************************
Description:
    Novatek touchscreen write data to specify address.

return:
    Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static int32_t nvt_write_addr(struct spi_device *client, uint32_t addr, uint8_t data)
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

static void nvt_sw_reset_idle(struct chip_data_nt36523 *chip_info)
{
	//---MCU idle cmds to SWRST_N8_ADDR---
    TPD_INFO("%s is called!\n", __func__);
	nvt_write_addr(chip_info->s_client, SWRST_N8_ADDR, 0xAA);

	msleep(15);
}

/*******************************************************
Description:
    Novatek touchscreen reset MCU (boot) function.

return:
    n.a.
*******************************************************/
void nvt_bootloader_reset_noflash(struct chip_data_nt36523 *chip_info)
{
    //---reset cmds to SWRST_N8_ADDR---
    TPD_INFO("%s is called!\n", __func__);
    nvt_write_addr(chip_info->s_client, SWRST_N8_ADDR, 0x69);

    mdelay(5);  //wait tBRST2FR after Bootload RST
}

/*******************************************************
Description:
    Novatek touchscreen set index/page/addr address.

return:
    Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static int32_t nvt_set_page(struct chip_data_nt36523 *chip_info, uint32_t addr)
{
    uint8_t buf[4] = {0};

    buf[0] = 0xFF;      //set index/page/addr command
    buf[1] = (addr >> 15) & 0xFF;
    buf[2] = (addr >> 7) & 0xFF;

    return CTP_SPI_WRITE(chip_info->s_client, buf, 3);
}

static void nvt_printk_fw_history(void *chip_data, uint32_t NVT_MMAP_HISTORY_ADDR)
{
    uint8_t i = 0;
    uint8_t buf[66];
	char str[128];
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_set_page(chip_info, NVT_MMAP_HISTORY_ADDR);

    buf[0] = (uint8_t) (NVT_MMAP_HISTORY_ADDR & 0x7F);
    CTP_SPI_READ(chip_info->s_client, buf, 65);	//read 64bytes history

    //print all data
    TPD_INFO("fw history 0x%x: \n", NVT_MMAP_HISTORY_ADDR);
	for (i = 0; i < 4; i++) {
        snprintf(str, sizeof(str), "%2x %2x %2x %2x %2x %2x %2x %2x    %2x %2x %2x %2x %2x %2x %2x %2x\n",
                 buf[1+i*16], buf[2+i*16], buf[3+i*16], buf[4+i*16],
                 buf[5+i*16], buf[6+i*16], buf[7+i*16], buf[8+i*16],
                 buf[9+i*16], buf[10+i*16], buf[11+i*16], buf[12+i*16],
                 buf[13+i*16], buf[14+i*16], buf[15+i*16], buf[16+i*16]);
        TPD_INFO("%s", str);
    }
}


static uint8_t nvt_wdt_fw_recovery(struct chip_data_nt36523 *chip_info, uint8_t *point_data)
{
    uint32_t recovery_cnt_max = 3;
    uint8_t recovery_enable = false;
    uint8_t i = 0;
    int errno_fw_state = 0;

    chip_info->recovery_cnt++;

    /* Pattern Check */
    for (i = 1 ; i < 7 ; i++) {
        if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
            chip_info->recovery_cnt = 0;
            break;
        } else if (point_data[i] == 0xFD) {

        } else if (point_data[i] == 0xFE) {
            errno_fw_state += 1;
        }
    }

    if (chip_info->recovery_cnt > recovery_cnt_max) {
        recovery_enable = true;
        chip_info->recovery_cnt = 0;
        if (errno_fw_state) {
            nvt_sw_reset_idle(chip_info);
        }
        nvt_printk_fw_history(chip_info, NVT_MMAP_HISTORY_EVENT0);
        nvt_printk_fw_history(chip_info, NVT_MMAP_HISTORY_EVENT1);
        nvt_printk_fw_history(chip_info, NVT_MMAP_HISTORY_EVENT2);
        nvt_printk_fw_history(chip_info, NVT_MMAP_HISTORY_EVENT3);
    }

    if (chip_info->recovery_cnt) {
        TPD_INFO("recovery_cnt is %d (0x%02X)\n", chip_info->recovery_cnt, point_data[1]);
    }

    return recovery_enable;
}

/*********************************************************
Description:
        Novatek touchscreen host esd recovery function.

return:
        Executive outcomes. false-detect 0x77. true-not detect 0x77
**********************************************************/
static bool nvt_fw_recovery(uint8_t *point_data)
{
    uint8_t i = 0;
    bool detected = true;

    /* check pattern */
    for (i = 1 ; i < 7 ; i++) {
        if (point_data[i] != 0x77) {
            detected = false;
            break;
        }
    }

    return detected;
}

static void nvt_esd_check_update_timer(struct chip_data_nt36523 *chip_info)
{
    TPD_DEBUG("%s\n", __func__);

    /* update interrupt timer */
    chip_info->irq_timer = jiffies;
}

static void nvt_esd_check_enable(struct chip_data_nt36523 *chip_info, bool enable)
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
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    unsigned int timer = jiffies_to_msecs(jiffies - chip_info->irq_timer);

    if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && chip_info->esd_check_enabled) {
        TPD_INFO("do ESD recovery, timer = %d, retry = %d\n", timer, chip_info->esd_retry);
        /* do esd recovery, bootloader reset */
        nvt_reset(chip_info);
        tp_touch_btnkey_release();
        /* update interrupt timer */
        chip_info->irq_timer = jiffies;
        /* update esd_retry counter */
        chip_info->esd_retry++;
    }

    return 0;
}

/*******************************************************
Description:
	Novatek touchscreen check and stop crc reboot loop.

return:
	n.a.
*******************************************************/
void nvt_esd_reverse_phase(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	nvt_set_page(chip_info, 0x3F035);

	buf[0] = 0x35;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 2);

	if (buf[1] == 0xA5) {
		TPD_INFO("IC is in phase III.\n");

		buf[0] = 0x35;
		buf[1] = 0x00;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		buf[0] = 0x35;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			TPD_INFO("Force IC to switch back to phase II OK.\n");
		else
			TPD_INFO("Failed to switch back to phase II, buf[1]=0x%02X\n", buf[1]);
	}
}

void nvt_stop_crc_reboot(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;

	//IC is in CRC fail reboot loop, needs to be stopped!
	for (retry = 5; retry > 0; retry--) {

		//---reset idle : 1st---
		nvt_write_addr(chip_info->s_client, SWRST_N8_ADDR, 0xAA);

		//---reset idle : 2rd---
		nvt_write_addr(chip_info->s_client, SWRST_N8_ADDR, 0xAA);

		msleep(1);

		//---clear CRC_ERR_FLAG---
		nvt_set_page(chip_info, 0x3F135);

		buf[0] = 0x35;
		buf[1] = 0xA5;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		//---check CRC_ERR_FLAG---
		nvt_set_page(chip_info, 0x3F135);

		buf[0] = 0x35;
		buf[1] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0xA5) {
			nvt_esd_reverse_phase(chip_info);
			break;
		}
	}

	if (retry == 0)
		TPD_INFO("CRC auto reboot is not able to be stopped! buf[1]=0x%02X\n", buf[1]);
}

/*******************************************************
Description:
        Novatek touchscreen check chip version trim function.

return:
        Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int8_t nvt_ts_check_chip_ver_trim(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[8] = {0};
    int32_t retry = 0;
    int32_t list = 0;
    int32_t i = 0;
    int32_t found_nvt_chip = 0;
    int32_t ret = -1;

    //---Check for 5 times---
    for (retry = 5; retry > 0; retry--) {

        nvt_bootloader_reset_noflash(chip_info);

        nvt_set_page(chip_info, CHIP_VER_TRIM_ADDR);       //read chip id

        buf[0] = CHIP_VER_TRIM_ADDR & 0x7F;  //offset
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
                chip_info->trim_id_table.support_hw_crc = trim_id_table[list].support_hw_crc;
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
        chip_info->trim_id_table.mmap = &NT36523_memory_map;
        chip_info->trim_id_table.support_hw_crc = 2;
        ret = 0;
    }

out:
    TPD_INFO("list = %d, support_hw_crc is %d\n", list, chip_info->trim_id_table.support_hw_crc);
    return ret;
}

/*******************************************************
Description:
        Novatek touchscreen check FW reset state function.

return:
        Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
static int32_t nvt_check_fw_reset_state_noflash(struct chip_data_nt36523 *chip_info, RST_COMPLETE_STATE check_reset_state)
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

static int nvt_enter_sleep(struct chip_data_nt36523 *chip_info, bool config)
{
    int ret = -1;

    if (config) {
        ret = nvt_cmd_store(chip_info, CMD_ENTER_SLEEP);
        if (ret < 0) {
            TPD_INFO("%s: enter sleep mode failed!\n", __func__);
            return -1;
        } else {
            chip_info->is_sleep_writed = true;
            TPD_INFO("%s: enter sleep mode sucess!\n", __func__);
        }
		msleep(50);
    }

    return ret;
}

static int32_t nvt_get_fw_info_noflash(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[64] = {0};
    uint32_t retry_count = 0;
    int32_t ret = 0;

info_retry:
    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

    //---read fw info---
    buf[0] = EVENT_MAP_FWINFO;
    CTP_SPI_READ(chip_info->s_client, buf, 39);

    //---clear x_num, y_num if fw info is broken---
    if ((buf[1] + buf[2]) != 0xFF) {
        TPD_INFO("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
        chip_info->fw_ver = 0;

        if(retry_count < 3) {
            retry_count++;
            TPD_INFO("retry_count=%d\n", retry_count);
            goto info_retry;
        } else {
            TPD_INFO("Set default fw_ver=0!\n");
            ret = -1;
        }
    } else {
        chip_info->fw_ver = buf[1];
        chip_info->fw_sub_ver = buf[14];
        chip_info->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
		TPD_INFO("fw_ver=0x%02X, fw_type=0x%02X, PID=0x%04X\n",
				chip_info->fw_ver, chip_info->fw_sub_ver, chip_info->nvt_pid);
        ret = 0;
    }

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

    for (i = 0 ; i < len + 1 ; i++)
        checksum += data[i];

    checksum += len;
    checksum = ~checksum + 1;

    return checksum;
}

static int32_t nvt_bin_header_parser(struct chip_data_nt36523 *chip_info, const u8 *fwdata, size_t fwsize)
{
    uint32_t list = 0;
    uint32_t pos = 0x00;
    uint32_t tmp_end = 0x00;
    uint8_t info_sec_num = 0;
    uint8_t ovly_sec_num = 0;
    uint8_t ovly_info = 0;
    uint8_t find_bin_header = 0;

    /* Find the header size */
    tmp_end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);

    /* check cascade next header */
    chip_info->cascade_2nd_header_info = (fwdata[0x20] & 0x02) >> 1;
    TPD_INFO("cascade_2nd_header_info = %d\n", chip_info->cascade_2nd_header_info);

    if (chip_info->cascade_2nd_header_info) {
        pos = 0x30;	// info section start at 0x30 offset
        while (pos < (tmp_end / 2)) {
            info_sec_num ++;
            pos += 0x10;	/* each header info is 16 bytes */
        }

        info_sec_num = info_sec_num + 1; //next header section
    } else {
        pos = 0x30;	// info section start at 0x30 offset
        while (pos < tmp_end) {
            info_sec_num ++;
            pos += 0x10;	/* each header info is 16 bytes */
        }
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
    chip_info->bin_map = (struct nvt_ts_bin_map *)kzalloc((chip_info->partition + 1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);
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
            chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list * 12]);
            chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list * 12]);
            chip_info->bin_map[list].size = byte_to_word(&fwdata[8 + list * 12]);
            chip_info->bin_map[list].crc = byte_to_word(&fwdata[0x18 + list * 4]);
            if (list == 0)
                snprintf(chip_info->bin_map[list].name, 12, "ILM");
            else if (list == 1)
                snprintf(chip_info->bin_map[list].name, 12, "DLM");
        }

        /*
         * [2] parsing others header info
         * SRAM_addr : size : BIN_addr : crc (16-bytes)
         */
        if ((list >= chip_info->ilm_dlm_num) && (list < (chip_info->ilm_dlm_num + info_sec_num))) {
			if (find_bin_header == 0) {
                /* others partition located at 0x30 offset */
                pos = 0x30 + (0x10 * (list - chip_info->ilm_dlm_num));
			} else if (find_bin_header && chip_info->cascade_2nd_header_info) {
				/* cascade 2nd header info */
				pos = tmp_end - 0x10;
			}

            chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
            chip_info->bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
            chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
            chip_info->bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
            /* detect header end to protect parser function */
            if ((chip_info->bin_map[list].BIN_addr < tmp_end) && (chip_info->bin_map[list].size != 0)) {
                snprintf(chip_info->bin_map[list].name, 12, "Header");
                find_bin_header = 1;
            } else {
                snprintf(chip_info->bin_map[list].name, 12, "Info-%u", (list - chip_info->ilm_dlm_num));
            }
        }

        /*
         * [3] parsing overlay section header info
         * SRAM_addr : size : BIN_addr : crc (16-bytes)
         */
        if (list >= (chip_info->ilm_dlm_num + info_sec_num)) {
            /* overlay info located at DLM (list = 1) start addr */
            pos = chip_info->bin_map[1].BIN_addr + (0x10 * (list - chip_info->ilm_dlm_num - info_sec_num));

            chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
            chip_info->bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
            chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
            chip_info->bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
            snprintf(chip_info->bin_map[list].name, 12, "Overlay-%u", (list - chip_info->ilm_dlm_num - info_sec_num));
        }

        /* BIN size error detect */
        if ((chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size) > fwsize) {
            TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                     chip_info->bin_map[list].BIN_addr, chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size);
            return -EINVAL;
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
static int32_t Download_Init(struct chip_data_nt36523 *chip_info)
{
    /* allocate buffer for transfer firmware */
    //TPD_INFO("SPI_TANSFER_LEN = %ld\n", SPI_TANSFER_LEN);

    if (chip_info->fwbuf == NULL) {
        chip_info->fwbuf = (uint8_t *)kzalloc((SPI_TANSFER_LEN + 1 + DUMMY_BYTES), GFP_KERNEL);
        if(chip_info->fwbuf == NULL) {
            TPD_INFO("kzalloc for fwbuf failed!\n");
            return -ENOMEM;
        }
    }

    return 0;
}

/*******************************************************
Description:
        Novatek touchscreen Write_Partition function to write
firmware into each partition.

return:
        n.a.
*******************************************************/
/*
static int32_t Write_Partition(struct chip_data_nt36523 *chip_info, const u8 *fwdata, size_t fwsize)
{
    uint32_t list = 0;
    uint32_t BIN_addr, SRAM_addr, size;
    int32_t ret = 0;
    u32 *len_array = NULL;
    u8 array_len;
    u8 *buf;
    u8 count = 0;

    array_len = chip_info->partition * 2;
    len_array = kzalloc(sizeof(u32) * array_len, GFP_KERNEL);

    if (len_array == NULL) {
        return -1;
    }
    buf = chip_info->fw_buf_dma;

    for (list = 0; list < chip_info->partition; list++) {
        SRAM_addr = chip_info->bin_map[list].SRAM_addr;
        size = chip_info->bin_map[list].size;
        BIN_addr = chip_info->bin_map[list].BIN_addr;
        //TPD_INFO("SRAM_addr %x, size %d, BIN_addr %x\n", SRAM_addr, size, BIN_addr);
        if (size == 0) {
            array_len = array_len - 2;
            continue;
        }
        // Check data size
        if ((BIN_addr + size) > fwsize) {
            TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                     BIN_addr, BIN_addr + size);
            ret = -1;
            goto out;
        }
        buf[0] = 0xFF;      //set index/page/addr command
        buf[1] = (SRAM_addr >> 15) & 0xFF;
        buf[2] = (SRAM_addr >> 7) & 0xFF;
        len_array[count * 2] = 3;

        buf = buf + 3;
        buf[0] = (SRAM_addr & 0x7F) | 0x80; //offset
        size = size + 1;
        memcpy(buf + 1, &fwdata[BIN_addr], size);
        len_array[count * 2 + 1] = size + 1;

        buf = buf + size + 1;
        count++;
    }

    spi_write_firmware(chip_info->s_client,
                       chip_info->fw_buf_dma,
                       len_array,
                       array_len);

out:
    kfree(len_array);
    return ret;
}
*/

static int32_t Write_Partition(struct chip_data_nt36523 *chip_info, const u8 *fwdata, size_t fwsize)
{
    uint32_t list = 0;
    char *name;
    uint32_t BIN_addr, SRAM_addr, size;
    uint32_t i = 0;
    uint16_t len = 0;
    int32_t count = 0;
    int32_t ret = 0;

    memset(chip_info->fwbuf, 0, (SPI_TANSFER_LEN + 1));

    for (list = 0; list < chip_info->partition; list++) {
        // initialize variable
        SRAM_addr = chip_info->bin_map[list].SRAM_addr;
        size = chip_info->bin_map[list].size;
        BIN_addr = chip_info->bin_map[list].BIN_addr;
        name = chip_info->bin_map[list].name;

        //              TPD_INFO("[%d][%s] SRAM (0x%08X), SIZE (0x%08X), BIN (0x%08X)\n",
        //                              list, name, SRAM_addr, size, BIN_addr);

        // Check data size
        if ((BIN_addr + size) > fwsize) {
            TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
                     BIN_addr, BIN_addr + size);
            ret = -1;
            goto out;
        }

        // ignore reserved partition (Reserved Partition size is zero)
        if (!size)
            continue;
        else
            size = size + 1;

        // write data to SRAM
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

            SRAM_addr += SPI_TANSFER_LEN;
            BIN_addr += SPI_TANSFER_LEN;
            size -= SPI_TANSFER_LEN;
        }
    }

out:
    return ret;
}

static void nvt_bld_crc_enable(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[4] = {0};

    //---set xdata index to BLD_CRC_EN_ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_CRC_EN_ADDR);

    //---read data from index---
    buf[0] = chip_info->trim_id_table.mmap->BLD_CRC_EN_ADDR & (0x7F);
    buf[1] = 0xFF;
    CTP_SPI_READ(chip_info->s_client, buf, 2);

    //---write data to index---
    buf[0] = chip_info->trim_id_table.mmap->BLD_CRC_EN_ADDR & (0x7F);
    buf[1] = buf[1] | (0x01 << 7);
    CTP_SPI_WRITE(chip_info->s_client, buf, 2);
}

/*******************************************************
Description:
    Novatek touchscreen clear status & enable fw crc function.

return:
    N/A.
*******************************************************/
static void nvt_fw_crc_enable(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[4] = {0};

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---clear fw reset status---
    buf[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
    buf[1] = 0x00;
    CTP_SPI_WRITE(chip_info->s_client, buf, 2);

    //---enable fw crc---
    buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
    buf[1] = 0xAE;  //enable fw crc command
    CTP_SPI_WRITE(chip_info->s_client, buf, 2);
}

/*******************************************************
Description:
        Novatek touchscreen set boot ready function.

return:
        Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static void nvt_boot_ready(struct chip_data_nt36523 *chip_info, uint8_t ready)
{
    //---write BOOT_RDY status cmds---
    nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->BOOT_RDY_ADDR, 1);

    mdelay(5);

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
        Novatek touchscreen eng reset cmd
    function.

return:
        n.a.
*******************************************************/
static void nvt_eng_reset(struct chip_data_nt36523 *chip_info)
{
    //---eng reset cmds to ENG_RST_ADDR---
    TPD_INFO("%s is called!\n", __func__);
    nvt_write_addr(chip_info->s_client, chip_info->ENG_RST_ADDR, 0x5A);

    mdelay(1);      //wait tMCU_Idle2TP_REX_Hi after TP_RST
}

/*******************************************************
Description:
	Novatek touchscreen enable auto copy mode function.

return:
	N/A.
*******************************************************/
void nvt_tx_auto_copy_mode(struct chip_data_nt36523 *chip_info)
{
	//---write TX_AUTO_COPY_EN cmds---
	nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN, 0x69);

	TPD_INFO("tx auto copy mode enable\n");
}

/*******************************************************
Description:
	Novatek touchscreen check spi dma tx info function.

return:
	N/A.
*******************************************************/
int32_t nvt_check_spi_dma_tx_info(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->SPI_DMA_TX_INFO);

		//---read fw status---
		buf[0] = chip_info->trim_id_table.mmap->SPI_DMA_TX_INFO & 0x7F;
		buf[1] = 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen set bootload crc reg bank function.
This function will set hw crc reg before enable crc function.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_crc_bank(struct chip_data_nt36523 *chip_info,
                                 uint32_t DES_ADDR, uint32_t SRAM_ADDR,
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
    chip_info->fwbuf[0] = LENGTH_ADDR & 0x7F;
    chip_info->fwbuf[1] = (size) & 0xFF;
    chip_info->fwbuf[2] = (size >> 8) & 0xFF;
    chip_info->fwbuf[3] = (size >> 16) & 0x01;
    if (chip_info->trim_id_table.support_hw_crc == 1) {
        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 3);
    } else if (chip_info->trim_id_table.support_hw_crc > 1) {
        CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 4);
    }

    /* write golden dlm checksum */
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
	Novatek touchscreen check DMA hw crc function.
This function will check hw crc result is pass or not.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_hw_crc(struct chip_data_nt36523 *chip_info)
{
    /* [0] ILM */
    /* write register bank */
    nvt_set_bld_crc_bank(chip_info,
                         chip_info->trim_id_table.mmap->ILM_DES_ADDR, chip_info->bin_map[0].SRAM_addr,
                         chip_info->trim_id_table.mmap->ILM_LENGTH_ADDR, chip_info->bin_map[0].size,
                         chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR, chip_info->bin_map[0].crc);

    /* [1] DLM */
    /* write register bank */
    nvt_set_bld_crc_bank(chip_info,
                         chip_info->trim_id_table.mmap->DLM_DES_ADDR, chip_info->bin_map[1].SRAM_addr,
                         chip_info->trim_id_table.mmap->DLM_LENGTH_ADDR, chip_info->bin_map[1].size,
                         chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR, chip_info->bin_map[1].crc);
}

/*******************************************************
Description:
    Novatek touchscreen read BLD hw crc info function.
This function will check crc results from register.

return:
    n.a.
*******************************************************/
static void nvt_read_bld_hw_crc(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[8] = {0};
    uint32_t g_crc = 0, r_crc = 0;

    /* CRC Flag */
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR);
    buf[0] = chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
    buf[1] = 0x00;
    CTP_SPI_READ(chip_info->s_client, buf, 2);
    TPD_INFO("crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d\n",
            (buf[1] >> 2) & 0x01, (buf[1] >> 0) & 0x01, (buf[1] >> 1) & 0x01);

    /* ILM CRC */
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR);
    buf[0] = chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR & 0x7F;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    CTP_SPI_READ(chip_info->s_client, buf, 5);
    g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR);
    buf[0] = chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR & 0x7F;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    CTP_SPI_READ(chip_info->s_client, buf, 5);
    r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

    TPD_INFO("ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
            chip_info->bin_map[0].crc, g_crc, r_crc);

    /* DLM CRC */
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR);
    buf[0] = chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR & 0x7F;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    CTP_SPI_READ(chip_info->s_client, buf, 5);
    g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR);
    buf[0] = chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR & 0x7F;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    CTP_SPI_READ(chip_info->s_client, buf, 5);
    r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

    TPD_INFO("dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
            chip_info->bin_map[1].crc, g_crc, r_crc);

    return;
}

#if NVT_TOUCH_ESD_DISP_RECOVERY
static int32_t nvt_check_crc_done_ilm_err(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 2);

	TPD_INFO("CRC DONE, ILM DLM FLAG = 0x%02X\n", buf[1]);
	if (((buf[1] & ILM_CRC_FLAG) && (buf[1] & CRC_DONE)) ||
			(buf[1] == 0xFE)) {
		return 1;
	} else {
		return 0;
	}
}

static int32_t nvt_f2c_read_write(struct chip_data_nt36523 *chip_info,
		uint8_t F2C_RW, uint32_t DDIC_REG_ADDR, uint16_t len, uint8_t *data)
{
	uint8_t buf[8] = {0};
	uint8_t retry = 0;
	uint8_t f2c_control = 0;
	uint32_t f2c_retry = 0;
	uint32_t retry_max = 1000;
	int32_t ret = 0;

	nvt_sw_reset_idle(chip_info);

	//Setp1: Set REG CPU_IF_ADDR[15:0]
	nvt_set_page(chip_info, CPU_IF_ADDR_LOW);
	buf[0] = CPU_IF_ADDR_LOW & 0x7F;
	buf[1] = (DDIC_REG_ADDR) & 0xFF;
	buf[2] = (DDIC_REG_ADDR >> 8) & 0xFF;
	CTP_SPI_WRITE(chip_info->s_client, buf, 3);


	//Step2: Set REG FFM_ADDR[15:0]
	nvt_set_page(chip_info, FFM_ADDR_LOW);
	buf[0] = FFM_ADDR_LOW & 0x7F;
	buf[1] = (TOUCH_DATA_ADDR) & 0xFF;
	buf[2] = (TOUCH_DATA_ADDR >> 8) & 0xFF;
	buf[3] = 0x00;
	if (chip_info->trim_id_table.support_hw_crc == 1) {
		CTP_SPI_WRITE(chip_info->s_client, buf, 3);
	} else if (chip_info->trim_id_table.support_hw_crc > 1) {
		CTP_SPI_WRITE(chip_info->s_client, buf, 4);
	}

	//Step3: Write Data to TOUCH_DATA_ADDR
	nvt_write_addr(chip_info->s_client, TOUCH_DATA_ADDR, *data);

	//Step4: Set REG F2C_LENGT[H7:0]
	nvt_write_addr(chip_info->s_client, F2C_LENGTH, len);

	//Enable CP_TP_CPU_REQ
	nvt_write_addr(chip_info->s_client, CP_TP_CPU_REQ, 1);

nvt_f2c_retry:
	//Step5: Set REG CPU_Polling_En, F2C_RW, CPU_IF_ADDR_INC, F2C_EN
	nvt_set_page(chip_info, FFM2CPU_CTL);
	buf[0] = FFM2CPU_CTL & 0x7F;
	buf[1] = 0xFF;
	ret = CTP_SPI_READ(chip_info->s_client, buf,  1 + len);//1 is AddrL
	if (ret) {
		TPD_INFO("Read FFM2CPU control failed!\n");
		return ret;
	}

	f2c_control = buf[1] |
		(0x01 << BIT_F2C_EN) |
		(0x01 << BIT_CPU_IF_ADDR_INC) |
		(0x01 << BIT_CPU_POLLING_EN);

	if (F2C_RW == F2C_RW_READ) {
		f2c_control = f2c_control & (~(1 << BIT_F2C_RW));
	} else if (F2C_RW == F2C_RW_WRITE) {
		f2c_control = f2c_control | (1 << BIT_F2C_RW);
	}

	nvt_write_addr(chip_info->s_client, FFM2CPU_CTL, f2c_control);

	//Step6: wait F2C_EN = 0
	retry = 0;
	while (1) {
		nvt_set_page(chip_info, FFM2CPU_CTL);
		buf[0] = FFM2CPU_CTL & 0x7F;
		buf[1] = 0xFF;
		buf[2] = 0xFF;
		ret = CTP_SPI_READ(chip_info->s_client, buf,  3);
		if (ret) {
			TPD_INFO("Read FFM2CPU control failed!\n");
			return ret;
		}

		if ((buf[1] & 0x01) == 0x00)
			break;

		usleep_range(1000, 1000);
		retry++;

		if(unlikely(retry > 20)) {
			TPD_INFO("Wait F2C_EN = 0 failed! retry %d\n", retry);
			return -EIO;
		}
	}
	//Step7: Check REG TH_CPU_CHK  status (1: Success,  0: Fail), if 0, can Retry Step5.
	if (((buf[2] & 0x04) >> 2) != 0x01) {
		f2c_retry++;
		if (f2c_retry <= retry_max) {
			goto nvt_f2c_retry;
		} else {
			TPD_INFO("check TH_CPU_CHK failed!, buf[1]=0x%02X, buf[2]=0x%02X\n", buf[1], buf[2]);
			return -EIO;
		}
	}

	if (F2C_RW == F2C_RW_READ) {
		nvt_set_page(chip_info, TOUCH_DATA_ADDR);
		buf[0] = TOUCH_DATA_ADDR & 0x7F;
		buf[1] = 0xFF;
		ret = CTP_SPI_READ(chip_info->s_client, buf,  1 + len);//1 is AddrL
		if (ret) {
			TPD_INFO("Read data failed!\n");
			return ret;
		}
		*data = buf[1];
	}

	return ret;
}

static int32_t nvt_f2c_disp_off(struct chip_data_nt36523 *chip_info)
{
	uint8_t data = 0x00;

	return nvt_f2c_read_write(chip_info, F2C_RW_WRITE, DISP_OFF_ADDR, 1, &data);
}
#endif /* NVT_TOUCH_ESD_DISP_RECOVERY */

/*******************************************************
Description:
        Novatek touchscreen Download_Firmware with HW CRC
function. It's complete download firmware flow.

return:
        n.a.
*******************************************************/
static int32_t Download_Firmware_HW_CRC(struct chip_data_nt36523 *chip_info, const struct firmware *fw)
{
    uint8_t retry = 0;
    int32_t ret = 0;
    uint8_t buf[8] = {0};
    TPD_DETAIL("Enter Download_Firmware_HW_CRC\n");
    start = ktime_get();

    while (1) {
        nvt_esd_check_update_timer(chip_info);

        /* bootloader reset to reset MCU */
        nvt_bootloader_reset_noflash(chip_info);

        /* set ilm & dlm reg bank */
        nvt_set_bld_hw_crc(chip_info);

        /* Start Write Firmware Process */
		if (chip_info->cascade_2nd_header_info) {
			/* for cascade */
			nvt_tx_auto_copy_mode(chip_info);

            ret = Write_Partition(chip_info, fw->data, fw->size);
            if (ret) {
                TPD_INFO("Write_Firmware failed. (%d)\n", ret);
                goto fail;
            }

            ret = nvt_check_spi_dma_tx_info(chip_info);
            if (ret) {
                TPD_INFO("spi dma tx info failed. (%d)\n", ret);
                goto fail;
            }
		} else {
            ret = Write_Partition(chip_info, fw->data, fw->size);
            if (ret) {
                TPD_INFO("Write_Firmware failed. (%d)\n", ret);
                goto fail;
            }
		}

        /* enable hw bld crc function */
        nvt_bld_crc_enable(chip_info);

        /* clear fw reset status & enable fw crc check */
        nvt_fw_crc_enable(chip_info);

        /* Set Boot Ready Bit */
        nvt_boot_ready(chip_info, true);

        ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_INIT);
        if (ret) {
            TPD_INFO("nvt_check_fw_reset_state_noflash failed. (%d)\n", ret);
            goto fail;
        } else {
            break;
        }

fail:
		/* dummy read to check 0xFC */
		CTP_SPI_READ(chip_info->s_client, buf, 2);
		TPD_INFO("dummy read = 0x%x\n", buf[1]);
		if (buf[1] == 0xFC) {
			nvt_stop_crc_reboot(chip_info);
		}
        retry++;
        if(unlikely(retry > 2) || chip_info->using_headfile) {
            TPD_INFO("error, retry=%d\n", retry);
			nvt_read_bld_hw_crc(chip_info);
#if NVT_TOUCH_ESD_DISP_RECOVERY
			if (nvt_check_crc_done_ilm_err(chip_info)) {
				TPD_INFO("set display off to trigger display esd recovery.\n");
				nvt_f2c_disp_off(chip_info);
			}
#endif /* #if NVT_TOUCH_ESD_DISP_RECOVERY */
            break;
        }
    }

    end = ktime_get();

    return ret;
}

int32_t nvt_nf_detect_chip(struct chip_data_nt36523 *chip_info)
{
    int32_t ret = 0;
    int i;
    uint8_t buf[8] = {0};

    ret = nvt_set_page(chip_info, CHIP_VER_TRIM_ADDR);

    buf[0] = CHIP_VER_TRIM_ADDR & 0x7F;  //offset
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = 0x00;
    ret = CTP_SPI_READ(chip_info->s_client, buf, 2);
    for(i = 1; i < 7; i++) {
        TPD_INFO("buf[%d] is 0x%02X\n", i, buf[i]);
        if(buf[i] != 0) {
            return 0;
        }
    }
    return -ENODEV;
}


/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
//extern int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data);

static int nvt_ftm_process(void *chip_data)
{
    int ret = -1;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    const struct firmware *fw = NULL;

    TPD_INFO("%s is called!\n", __func__);

    ret = nvt_get_chip_info(chip_info);
    if (!ret) {
        ret = nvt_fw_update_sub(chip_info, fw, 0);
        if(ret > 0) {
            TPD_INFO("%s fw update failed!\n", __func__);
        } else {
            ret = nvt_enter_sleep(chip_info, true);
        }
    }

    switch_spi7cs_state(false);
    return ret;
}

static int nvt_power_control(void *chip_data, bool enable)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_eng_reset(chip_info);       //make sure before nvt_bootloader_reset_noflash
	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
	}

    return 0;
}

static int nvt_get_chip_info(void *chip_data)
{
    int ret = -1;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    //---check chip version trim---
    ret = nvt_ts_check_chip_ver_trim(chip_info);
    if (ret) {
        TPD_INFO("chip is not identified\n");
        ret = -EINVAL;
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

static int nvt_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') && \
        (panel_data->fw_name[len - 2] == 'm') && (panel_data->fw_name[len - 1] == 'g')) {
//        panel_data->fw_name[len - 3] = 'b';
//        panel_data->fw_name[len - 2] = 'i';
//        panel_data->fw_name[len - 1] = 'n';
    }
    chip_info->tp_type = panel_data->tp_type;
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", chip_info->tp_type, panel_data->fw_name);

    return 0;
}

static int nvt_reset_gpio_control(void *chip_data, bool enable)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        TPD_INFO("%s: set reset state %d\n", __func__, enable);
        gpio_set_value(chip_info->hw_res->reset_gpio, enable);
    }

    return 0;
}
/*
static int nvt_cs_gpio_control(void *chip_data, bool enable)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    if (gpio_is_valid(chip_info->cs_gpio)) {
        TPD_INFO("%s: set cs state %d\n", __func__, enable);
        gpio_set_value(chip_info->cs_gpio, enable);
    }

    return 0;
}
*/
static u32 nvt_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
    int32_t ret = -1;

    memset(chip_info->point_data, 0, POINT_DATA_LEN);
    ret = CTP_SPI_READ(chip_info->s_client, chip_info->point_data, POINT_DATA_LEN + 1);
    if (ret < 0) {
        TPD_INFO("CTP_SPI_READ failed.(%d)\n", ret);
        return IRQ_IGNORE;
    }

    //some kind of protect mechanism, after WDT firware redownload and try to save tp
    ret = nvt_wdt_fw_recovery(chip_info, chip_info->point_data);
    if (ret) {
		if ((gesture_enable == 1) && (is_suspended == 1)) {
			/* auto go back to wakeup gesture mode */
			TPD_INFO("Recover for fw reset %02X\n", chip_info->point_data[1]);
			nvt_reset(chip_info);
			ret = nvt_cmd_store(chip_info, 0x13);
			return IRQ_IGNORE;
		}
		TPD_INFO("Recover for fw reset %02X, IRQ_EXCEPTION\n", chip_info->point_data[1]);
		return IRQ_EXCEPTION;
    }

    if (nvt_fw_recovery(chip_info->point_data)) {  //receive 0x77
        nvt_esd_check_enable(chip_info, true);
        return IRQ_IGNORE;
    }

    ret = nvt_ts_point_data_checksum(chip_info->point_data, POINT_DATA_CHECKSUM_LEN);
    if (ret) {
        return IRQ_IGNORE;
    }

#if PEN_DATA_CHECKSUM
	if (ts->pen_support) {
		ret = nvt_ts_pen_data_checksum(chip_info->point_data + 66, PEN_DATA_LEN);
		if (ret) {
			return IRQ_IGNORE;
		}
	}
#endif

    if ((gesture_enable == 1) && (is_suspended == 1)) {
        return IRQ_GESTURE;
    } else if (is_suspended == 1) {
        return IRQ_IGNORE;
    }
	if (ts->pen_support)
		return (IRQ_TOUCH | IRQ_PEN);
	else
		return IRQ_TOUCH;
}

#ifdef CONFIG_OPLUS_TP_APK
static void nova_write_log_buf(struct chip_data_nt36523 *chip_info, u8 main_id, u8 sec_id)
{
    log_buf_write(chip_info->ts, main_id);
    sec_id = sec_id | 0x80;
    log_buf_write(chip_info->ts, sec_id);
}


static void nova_debug_info(struct chip_data_nt36523 *chip_info, u8 *buf)
{
    static struct nvt_fw_debug_info debug_last;

    chip_info->nvt_fw_debug_info.rek_info = (uint8_t) (buf[0] >> 4) & 0x07;
    chip_info->nvt_fw_debug_info.rst_info = (uint8_t) (buf[0]) & 0x07;

    chip_info->nvt_fw_debug_info.esd      = (uint8_t) (buf[1] >> 5) & 0x01;
    chip_info->nvt_fw_debug_info.palm     = (uint8_t) (buf[1] >> 4) & 0x01;
    chip_info->nvt_fw_debug_info.bending  = (uint8_t) (buf[1] >> 3) & 0x01;
    chip_info->nvt_fw_debug_info.water    = (uint8_t) (buf[1] >> 2) & 0x01;
    chip_info->nvt_fw_debug_info.gnd      = (uint8_t) (buf[1] >> 1) & 0x01;
    chip_info->nvt_fw_debug_info.er       = (uint8_t) (buf[1]) & 0x01;

    chip_info->nvt_fw_debug_info.hopping  = (uint8_t) (buf[2] >> 4) & 0x0F;
    chip_info->nvt_fw_debug_info.fog      = (uint8_t) (buf[2] >> 2) & 0x01;
    chip_info->nvt_fw_debug_info.film     = (uint8_t) (buf[2] >> 1) & 0x01;
    chip_info->nvt_fw_debug_info.notch    = (uint8_t) (buf[2]) & 0x01;

    if (debug_last.rek_info != chip_info->nvt_fw_debug_info.rek_info) {
        nova_write_log_buf(chip_info, 1, chip_info->nvt_fw_debug_info.rek_info);

    }
    if (debug_last.rst_info != chip_info->nvt_fw_debug_info.rst_info) {
        nova_write_log_buf(chip_info, 2, chip_info->nvt_fw_debug_info.rst_info);

    }
    if (debug_last.esd != chip_info->nvt_fw_debug_info.esd) {
        log_buf_write(chip_info->ts, 3 + chip_info->nvt_fw_debug_info.esd);
    }
    if (debug_last.palm != chip_info->nvt_fw_debug_info.palm) {
        log_buf_write(chip_info->ts, 5 + chip_info->nvt_fw_debug_info.palm);
    }
    if (debug_last.bending != chip_info->nvt_fw_debug_info.bending) {
        log_buf_write(chip_info->ts, 7 + chip_info->nvt_fw_debug_info.bending);
    }
    if (debug_last.water != chip_info->nvt_fw_debug_info.water) {
        log_buf_write(chip_info->ts, 9 + chip_info->nvt_fw_debug_info.water);
    }
    if (debug_last.gnd != chip_info->nvt_fw_debug_info.gnd) {
        log_buf_write(chip_info->ts, 11 + chip_info->nvt_fw_debug_info.gnd);
    }
    if (debug_last.er != chip_info->nvt_fw_debug_info.er) {
        log_buf_write(chip_info->ts, 13 + chip_info->nvt_fw_debug_info.er);
    }
    if (debug_last.hopping != chip_info->nvt_fw_debug_info.hopping) {
        nova_write_log_buf(chip_info, 15, chip_info->nvt_fw_debug_info.hopping);
    }
    if (debug_last.fog != chip_info->nvt_fw_debug_info.fog) {
        log_buf_write(chip_info->ts, 17 + chip_info->nvt_fw_debug_info.fog);
    }
    if (debug_last.film != chip_info->nvt_fw_debug_info.film) {
        log_buf_write(chip_info->ts, 19 + chip_info->nvt_fw_debug_info.film);
    }
    if (debug_last.notch != chip_info->nvt_fw_debug_info.notch) {
        log_buf_write(chip_info->ts, 21 + chip_info->nvt_fw_debug_info.notch);
    }

    memcpy(&debug_last, &chip_info->nvt_fw_debug_info, sizeof(debug_last));

    //msleep(2000);
    if (tp_debug > 0) {
        pr_err("REK_INFO:0x%02X, RST_INFO:0x%02X\n",
               chip_info->nvt_fw_debug_info.rek_info,
               chip_info->nvt_fw_debug_info.rst_info);

        pr_err("ESD:0x%02X, PALM:0x%02X, BENDING:0x%02X, WATER:0x%02X, GND:0x%02X, ER:0x%02X\n",
               chip_info->nvt_fw_debug_info.esd,
               chip_info->nvt_fw_debug_info.palm,
               chip_info->nvt_fw_debug_info.bending,
               chip_info->nvt_fw_debug_info.water,
               chip_info->nvt_fw_debug_info.gnd,
               chip_info->nvt_fw_debug_info.er);

        pr_err("HOPPING:0x%02X, FOG:0x%02X, FILM:0x%02X, NOTCH:0x%02X\n\n",
               chip_info->nvt_fw_debug_info.hopping,
               chip_info->nvt_fw_debug_info.fog,
               chip_info->nvt_fw_debug_info.film,
               chip_info->nvt_fw_debug_info.notch);

    }
}

#endif // end of CONFIG_OPLUS_TP_APK

#if PEN_DATA_CHECKSUM
static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	/* Calculate checksum */
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i];
	}
	checksum = (~checksum + 1);

	/* Compare ckecksum and dump fail data */
	if (checksum != buf[length - 1]) {
		TPD_INFO("pen packet checksum not match. (buf[%d]=0x%02X, checksum=0x%02X)\n",
			length - 1, buf[length - 1], checksum);
		/* --- dump pen buf --- */
		for (i = 0; i < length; i++) {
			TPD_INFO("%02X ", buf[i]);
		}
		TPD_INFO("\n");

		return -1;
	}

	return 0;
}
#endif /* end of PEN_DATA_CHECKSUM */

static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length)
{
    uint8_t checksum = 0;
    int32_t i = 0;

    // Generate checksum
    for (i = 0; i < length - 1; i++) {
        checksum += buf[i + 1];
    }
    checksum = (~checksum + 1);

    // Compare ckecksum and dump fail data
    if (checksum != buf[length]) {
        TPD_INFO("i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
                 length, buf[length], checksum);

        for (i = 0; i < 10; i++) {
            TPD_INFO("%02X %02X %02X %02X %02X %02X\n",
                     buf[1 + i * 6], buf[2 + i * 6], buf[3 + i * 6], buf[4 + i * 6], buf[5 + i * 6], buf[6 + i * 6]);
        }

        for (i = 0; i < (length - 60); i++) {
            TPD_INFO("%02X ", buf[1 + 60 + i]);
        }

        return -1;
    }

    return 0;
}


static int nvt_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    int obj_attention = 0;
    int i = 0;
    uint32_t position = 0;
    uint32_t input_x = 0;
    uint32_t input_y = 0;
    uint32_t input_w = 0;
    uint32_t input_p = 0;
    uint8_t pointid = 0;
    uint8_t *point_data = chip_info->point_data;

    for(i = 0; i < max_num; i++) {
        position = 1 + 6 * i;
        pointid = (uint8_t)(point_data[position + 0] >> 3) - 1;
        if (pointid >= max_num) {
            continue;
        }

        if (((point_data[position] & 0x07) == STATUS_FINGER_ENTER) ||
                ((point_data[position] & 0x07) == STATUS_FINGER_MOVING)) {
            chip_info->irq_timer = jiffies;    //reset esd check trigger base time

            input_x = (uint32_t)(point_data[position + 1] << 4) + (uint32_t) (point_data[position + 3] >> 4);
            input_y = (uint32_t)(point_data[position + 2] << 4) + (uint32_t) (point_data[position + 3] & 0x0F);

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

#ifdef CONFIG_OPLUS_TP_APK
    if (chip_info->debug_mode_sta) {
        nova_debug_info(chip_info, &point_data[109]);
    }
#endif // end of CONFIG_OPLUS_TP_APK
    return obj_attention;
}

static void nvt_get_pen_points(void *chip_data, struct pen_info *points)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t pen_format_id = 0;
	uint8_t *point_data = chip_info->point_data;
	static bool btn_no_rejected = false, btn_reject_status = false;

	/* parse and handle pen report */
	pen_format_id = point_data[66];
	if (pen_format_id != 0xFF) {
		if (pen_format_id == 0x01) {
			/* report pen data */
			points->x = (uint32_t)(point_data[67] << 8) + (uint32_t)(point_data[68]);
			points->y = (uint32_t)(point_data[69] << 8) + (uint32_t)(point_data[70]);
			points->z = (uint32_t)(point_data[71] << 8) + (uint32_t)(point_data[72]);
			points->tilt_x = (int8_t)point_data[73];
			points->tilt_y = (int8_t)point_data[74];
			points->d = (uint32_t)(point_data[75] << 8) + (uint32_t)(point_data[76]);
			points->btn1 = (uint32_t)(point_data[77] & 0x01);
			points->btn2 = (uint32_t)((point_data[77] >> 1) & 0x01);
			points->battery = (uint32_t)point_data[78];
			points->status = 1;

			if (!points->z && (points->btn1 || points->btn2)) {
				btn_no_rejected = true;
			} else if (!(points->btn1 || points->btn2)) {
				btn_no_rejected = false;
				btn_reject_status = false;
			}
			if (!btn_no_rejected && points->z && (points->btn1 || points->btn2) && !btn_reject_status) {
				btn_reject_status = true;
				TPD_INFO("reject this accident btn(%d %d).\n", points->btn1, points->btn2);
			}
			if (btn_reject_status) {
				points->btn1 = 0;
				points->btn2 = 0;
			}
		} else {
			btn_no_rejected = false;
			btn_reject_status = false;
			TPD_INFO("Unknown pen format id(%02x)!\n", pen_format_id);
		}
	} else {
		btn_no_rejected = false;
		btn_reject_status = false;
	}
	return;
}

static int8_t nvt_extend_cmd2_store(struct chip_data_nt36523 *chip_info,
		uint8_t u8Cmd, uint8_t u8SubCmd, uint8_t u8SubCmd1)
{
    int i, retry = 5;
    uint8_t buf[4] = {0};

	/* ---set xdata index to EVENT BUF ADDR---(set page) */
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    for (i = 0; i < retry; i++) {
		if (buf[1] != u8Cmd) {
			/* ---set cmd status--- */
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = u8Cmd;
			buf[2] = u8SubCmd;
			buf[3] = u8SubCmd1;
			CTP_SPI_WRITE(chip_info->s_client, buf, 4);
		}

        msleep(20);

        /* ---read cmd status--- */
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, 2);
        if (buf[1] == 0x00)
            break;
    }

    if (unlikely(i == retry)) {
		TPD_INFO("send Cmd 0x%02X 0x%02X 0x%02X failed, buf[1]=0x%02X\n",
				u8Cmd, u8SubCmd, u8SubCmd1, buf[1]);
        return -1;
    } else {
        TPD_INFO("send Cmd 0x%02X 0x%02X 0x%02X success, tried %d times\n",
				u8Cmd, u8SubCmd, u8SubCmd1, i);
    }

    return 0;
}

static int8_t nvt_extend_cmd_store(struct chip_data_nt36523 *chip_info, uint8_t u8Cmd, uint8_t u8SubCmd)
{
    int i, retry = 5;
    uint8_t buf[4] = {0};

    //---set xdata index to EVENT BUF ADDR---(set page)
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    for (i = 0; i < retry; i++) {
		if (buf[1] != u8Cmd) {
			//---set cmd status---
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = u8Cmd;
			buf[2] = u8SubCmd;
			CTP_SPI_WRITE(chip_info->s_client, buf, 3);
		}

        msleep(20);

        //---read cmd status---
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, 3);
        if (buf[1] == 0x00)
            break;
    }

    if (unlikely(i == retry)) {
        TPD_INFO("send Cmd 0x%02X 0x%02X failed, buf[1]=0x%02X\n", u8Cmd, u8SubCmd, buf[1]);
        return -1;
    } else {
        TPD_INFO("send Cmd 0x%02X 0x%02X success, tried %d times\n", u8Cmd, u8SubCmd, i);
    }

    return 0;
}

static int8_t nvt_cmd_store(struct chip_data_nt36523 *chip_info, uint8_t u8Cmd)
{
    int i, retry = 5;
    uint8_t buf[3] = {0};

    //---set xdata index to EVENT BUF ADDR---(set page)
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    for (i = 0; i < retry; i++) {
		if (buf[1] != u8Cmd) {
			//---set cmd status---
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = u8Cmd;
			CTP_SPI_WRITE(chip_info->s_client, buf, 2);
		}

        msleep(20);

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
        TPD_INFO("send Cmd 0x%02X success, tried %d times\n", u8Cmd, i);
    }

    return 0;
}



static void nvt_ts_wakeup_gesture_coordinate(uint8_t *data, uint8_t max_num)
{
    uint32_t position = 0;
    uint32_t input_x = 0;
    uint32_t input_y = 0;
    int32_t i = 0;
    uint8_t input_id = 0;

    for (i = 0; i < max_num; i++) {
        position = 1 + 6 * i;
        input_id = (uint8_t)(data[position + 0] >> 3);
        if ((input_id == 0) || (input_id > max_num))
            continue;

        if (((data[position] & 0x07) == 0x01) || ((data[position] & 0x07) == 0x02)) {
            input_x = (uint32_t)(data[position + 1] << 4) + (uint32_t) (data[position + 3] >> 4);
            input_y = (uint32_t)(data[position + 2] << 4) + (uint32_t) (data[position + 3] & 0x0F);
        }
        TPD_INFO("(%d: %d, %d)\n", i, input_x, input_y);
    }
}

#ifdef CONFIG_OPLUS_TP_APK

static void nvt_read_debug_gesture_coordinate_buffer(struct chip_data_nt36523 *chip_info,
        uint32_t xdata_addr, u8 *xdata, int32_t xdata_len)
{
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
    uint32_t head_addr = 0;
    int32_t dummy_len = 0;
    //int32_t data_len = 128;	/* max buffer size 1024 */
    int32_t residual_len = 0;
    uint8_t *xdata_tmp = NULL;

    //---set xdata sector xdata_addr & length---
    head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
    dummy_len = xdata_addr - head_addr;
    residual_len = (head_addr + dummy_len + xdata_len) % XDATA_SECTOR_SIZE;

    /*if (xdata_len/sizeof(int32_t) < data_len) {
    	TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n", xdata_len, data_len);
    	return;
    }*/

    //malloc buffer space
    xdata_tmp = kzalloc(xdata_len + XDATA_SECTOR_SIZE * 4, GFP_KERNEL);
    if (xdata_tmp == NULL) {
        TPD_INFO("%s malloc memory failed\n", __func__);
        return;
    }

    //read xdata : step 1
    for (i = 0; i < ((dummy_len + xdata_len) / XDATA_SECTOR_SIZE); i++) {
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

            nvt_set_page(chip_info, xdata_addr + xdata_len - residual_len + (SPI_TANSFER_LENGTH * j));

            //---read data---
            buf[0] = SPI_TANSFER_LENGTH * j;
            CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

            //---copy buf to xdata_tmp---
            for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
                xdata_tmp[(dummy_len + xdata_len - residual_len) + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
                //printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k));
            }

        }
        //printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
    }

    //---remove dummy data---
    pr_cont("nova read gesture xdata\n");
    for (i = 0; i < xdata_len ; i++) {
        xdata[i] = xdata_tmp[dummy_len + i];
        pr_cont("0x%02X,", xdata[i]);
    }
    pr_cont("\n");
    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    kfree(xdata_tmp);
}

static void nvt_dbg_gesture_record_coor_read(struct chip_data_nt36523 *chip_info, u8 pointdata)
{
    u8 *xdata = NULL;
    uint32_t buf_len = 0;
    uint8_t points_num[2] = {0};
    uint8_t data_len[2] = {0};

    buf_len = 512;
    xdata = kzalloc(buf_len, GFP_KERNEL);
    if (!xdata) {
        TPD_INFO("%s, malloc memory failed\n", __func__);
        return;
    }

    nvt_read_debug_gesture_coordinate_buffer(chip_info, NVT_MMAP_DEBUG_FINGER_DOWN_DIFFDATA,
            xdata, buf_len);
    points_num[0] = xdata[0];
    data_len[0] = 3 * points_num[0];
    memcpy(&chip_info->ts->gesture_buf[2], &xdata[1], data_len[0] * sizeof(int32_t));

    nvt_read_debug_gesture_coordinate_buffer(chip_info, NVT_MMAP_DEBUG_STATUS_CHANGE_DIFFDATA,
            xdata, buf_len);
    points_num[1] = xdata[0];
    data_len[1] = 3 * points_num[1];
    memcpy(&chip_info->ts->gesture_buf[2 + data_len[0]], &xdata[1], data_len[1] * sizeof(int32_t));

    chip_info->ts->gesture_buf[0] = pointdata;
    chip_info->ts->gesture_buf[1] = points_num[0] + points_num[1];

    if(xdata) {
        kfree(xdata);
    }
}
#endif // end of CONFIG_OPLUS_TP_APK

static int nvt_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    uint8_t gesture_id = 0;
    uint8_t func_type = 0;
    int ret = -1;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    uint8_t *point_data = chip_info->point_data;
    uint8_t max_num = 10;	//TODO: define max_num by oplus common driver

#if NVT_PM_WAIT_I2C_SPI_RESUME_COMPLETE
	if (chip_info->dev_pm_suspend) {
		ret = wait_for_completion_timeout(&chip_info->dev_pm_resume_completion, msecs_to_jiffies(500));
		if (!ret) {
			TPD_INFO("system (i2c/spi) can't finished resuming procedure, skip it!\n");
			return -1;
		}
	}
#endif /* NVT_PM_WAIT_I2C_RESUME_COMPLETE */

    gesture_id = (uint8_t)(point_data[1] >> 3);
    func_type = (uint8_t)point_data[2];
    if ((gesture_id == 30) && (func_type == 1)) {
        gesture_id = (uint8_t)point_data[3];
    } else if (gesture_id > 30) {
        TPD_INFO("invalid gesture id= %d, no gesture event\n", gesture_id);
        return 0;
    }

#ifdef CONFIG_OPLUS_TP_APK
    TPD_INFO("gesture id is %d,data[1] %d,data[2] %d,data[3] %d\n",
             gesture_id, point_data[1], point_data[2], point_data[3]);
    if (chip_info->debug_gesture_sta) {
        if (chip_info->ts->gesture_buf) {
            nvt_dbg_gesture_record_coor_read(chip_info, gesture_id);
        }
    }
#endif // end of CONFIG_OPLUS_TP_APK

    if ((gesture_id > 0) && (gesture_id <= max_num)) {
        nvt_ts_wakeup_gesture_coordinate(point_data, max_num);
        return 0;
    }

	switch (gesture_id) {   /* judge gesture type */
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

	case PEN_DETECT:
        gesture->gesture_type  = PENDETECT;
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

static int nvt_reset(void *chip_data)
{
	int val = 0, ret = -1;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
    //const struct firmware *fw = NULL;

    TPD_INFO("%s.\n", __func__);
    mutex_lock(&chip_info->mutex_testing);

    if(!ts->fw_update_app_support || chip_info->probe_done) {

        ret = nvt_fw_update(chip_info, NULL, 0);
        if (FW_NO_NEED_UPDATE == ret) {
			TPD_INFO("g_fw_buf update no need!\n");
		} else if (FW_UPDATE_ERROR == ret) {
			val = -1;
            TPD_INFO("g_fw_buf update failed!\n");
        }
    }
    /*
    if(chip_info->g_fw != NULL) {
        release_firmware(chip_info->g_fw);
    }
    */
#ifdef CONFIG_OPLUS_TP_APK
    if(chip_info->debug_mode_sta) {
        if(ts->apk_op && ts->apk_op->apk_debug_set) {
            ts->apk_op->apk_debug_set(ts->chip_data, true);
        }
    }
#endif // end of CONFIG_OPLUS_TP_APK

    chip_info->is_sleep_writed = false;
    mutex_unlock(&chip_info->mutex_testing);
	return val;
}

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused int nvt_enable_debug_gesture_coordinate_record_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);

    if (enable) {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_ON);
    } else {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_OFF);
    }

    return ret;
}

static __maybe_unused int nvt_enable_debug_gesture_coordinate_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);

    if (enable) {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_ON);
    } else {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_OFF);
    }

    return ret;
}

#endif // end of CONFIG_OPLUS_TP_APK

static int nvt_enable_black_gesture(struct chip_data_nt36523 *chip_info, bool enable)
{
    int ret = -1;

    TPD_INFO("%s, enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (chip_info->is_sleep_writed) {
        //disable_irq_nosync(chip_info->irq_num);
        chip_info->need_judge_irq_throw = true;
        //if (get_lcd_status() != 1) {
            nvt_reset(chip_info);
        //}
        chip_info->need_judge_irq_throw = false;
    }


    if (enable) {
 //       if (get_lcd_status() > 0) {
 //           TPD_INFO("Will power on soon!");
 //           return ret;
 //       }
#ifdef CONFIG_OPLUS_TP_APK
        if (chip_info->debug_gesture_sta) {
            nvt_enable_debug_gesture_coordinate_record_mode(chip_info, true);
        }
#endif // end of CONFIG_OPLUS_TP_APK

        if ((chip_info->gesture_state & (1 << PENDETECT)) && *chip_info->is_pen_connected && !(*chip_info->is_pen_attracted)) {
            ret = nvt_enable_pen_mode(chip_info, true);
        } else {
            ret = nvt_enable_pen_mode(chip_info, false);
        }

        ret |= nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
        msleep(50);
        TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success");
    } else {
        ret = 0;
    }

    return ret;
}

static uint8_t edge_limit_level = 40; /* 0 ~ 255 */
static int nvt_enable_edge_limit(struct chip_data_nt36523 *chip_info, int state)
{
    int8_t ret = -1;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
    TPD_INFO("%s:state = %d, limit_corner = %d, level = %d, chip_info->is_sleep_writed = %d\n", __func__, state, ts->limit_corner, edge_limit_level, chip_info->is_sleep_writed);

    if (state == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {
        ret = nvt_extend_cmd_store(chip_info,
                                   EVENTBUFFER_EDGE_LIMIT_VERTICAL, edge_limit_level);
    } else {
        if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
            ret = nvt_extend_cmd_store(chip_info,
                                       EVENTBUFFER_EDGE_LIMIT_RIGHT_UP, edge_limit_level);
        } else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
            ret = nvt_extend_cmd_store(chip_info,
                                       EVENTBUFFER_EDGE_LIMIT_LEFT_UP, edge_limit_level);
        }
    }

    return ret;
}

static int nvt_enable_charge_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_IN);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_OUT);
    }

    return ret;
}

static int nvt_enable_game_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable, chip_info->is_sleep_writed);

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_GAME_ON);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_GAME_OFF);
    }

    return ret;
}

static int nvt_enable_headset_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
              enable, chip_info->is_sleep_writed);

    if (enable) {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_IN);
        TPD_INFO("%s: EVENTBUFFER_HS_PLUG_IN\n", __func__);
    } else {
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_OUT);
    }

    return ret;
}

static int __maybe_unused nvt_set_jitter_para(struct chip_data_nt36523 *chip_info, int level)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:level = %d, chip_info->is_sleep_writed = %d\n", __func__,
              level, chip_info->is_sleep_writed);

	ret = nvt_extend_cmd2_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_JITTER_LEVEL, level);

    return ret;
}

static int __maybe_unused nvt_set_smooth_para(struct chip_data_nt36523 *chip_info, int level)
{
    int8_t ret = -1;

    TPD_DEBUG("%s:level = %d, chip_info->is_sleep_writed = %d\n", __func__,
              level, chip_info->is_sleep_writed);

	ret = nvt_extend_cmd2_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_SMOOTH_LEVEL, level);

    return ret;
}

static int nvt_enable_pen_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	if (enable) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_ON);
	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_OFF);
	}

	return ret;
}

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused int nvt_enable_hopping_polling_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);

    nvt_esd_check_update_timer(chip_info);

    if (enable)
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_POLLING_ON);
    else
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_POLLING_OFF);

    return ret;
}

static __maybe_unused int nvt_enable_hopping_fix_freq_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);

    nvt_esd_check_update_timer(chip_info);

    if (enable)
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_FIX_FREQ_ON);
    else
        ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_FIX_FREQ_OFF);

    return ret;
}

static __maybe_unused int nvt_enable_debug_msg_diff_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);

    nvt_esd_check_update_timer(chip_info);

    if (enable) {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_MSG_DIFF_ON);
    } else {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_MSG_DIFF_OFF);
    }

    return ret;
}

static __maybe_unused int nvt_enable_water_polling_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
    int8_t ret = -1;

    TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
             enable, chip_info->is_sleep_writed);


    nvt_esd_check_update_timer(chip_info);


    if (enable) {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WATER_POLLING_ON);
    } else {
        ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WATER_POLLING_OFF);
    }

    return ret;
}
#endif // end of CONFIG_OPLUS_TP_APK

static int nvt_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_esd_check_update_timer(chip_info);

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

    case MODE_GAME:
        ret = nvt_enable_game_mode(chip_info, flag);
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

    case MODE_PEN_SCAN:
        ret = nvt_enable_pen_mode(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable pen mode : %d failed\n", __func__, flag);
        }
        break;

    /* Debug Function */
    /*case MODE_HOPPING_POLLING:	//TODO: add define in oplus h file, MODE_HOPPING_POLLING
    	ret = nvt_enable_hopping_polling_mode(chip_info, flag);
    	if (ret < 0) {
    		TPD_INFO("%s: enable hopping polling mode : %d failed\n", __func__, flag);
    	}
    	break;

    case MODE_HOPPING_FIX_FREQ:	//TODO: add define in oplus h file, MODE_HOPPING_FIX_FREQ
    	ret = nvt_enable_hopping_fix_freq_mode(chip_info, flag);
    	if (ret < 0) {
    		TPD_INFO("%s: enable hopping fix freq mode : %d failed\n", __func__, flag);
    	}
    	break;*/

    /*case DEBUG_MODE_MESSAGE_DIFF:	//TODO: add define in oplus h file, DEBUG_MODE_MESSAGE_DIFF
    	ret = nvt_enable_debug_msg_diff_mode(chip_info, flag);
    	if (ret < 0) {
    		TPD_INFO("%s: enable debug message diff %d failed\n", __func__, flag);
    	}
    	break;

    case DEBUG_MODE_GESTURE_COORDINATE:	//TODO: add define in oplus h file, DEBUG_MODE_GESTURE_COORDINATE
    	ret = nvt_enable_debug_gesture_coordinate_mode(chip_info, flag);
    	if (ret < 0) {
    		TPD_INFO("%s: enable debug gesture coordinate %d failed\n", __func__, flag);
    	}
    	break;

    case DEBUG_MODE_GESTURE_COORDINATE_RECORD:	//TODO: add define in oplus h file, DEBUG_MODE_GESTURE_COORDINATE_RECORD
    	ret = nvt_enable_debug_gesture_coordinate_record_mode(chip_info, flag);
    	if (ret < 0) {
    		TPD_INFO("%s: enable debug gesture coordinate record %d failed\n", __func__, flag);
    	}
    	break;*/

    default:
        TPD_INFO("%s: Wrong mode.\n", __func__);
    }

    return ret;
}

static fw_check_state nvt_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    uint8_t ver_len = 0;
    int ret = 0;
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
	char tmp_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_esd_check_update_timer(chip_info);

    ret |= nvt_get_fw_info_noflash(chip_info);
    if (ret < 0) {
        TPD_INFO("%s: get fw info failed\n", __func__);
        return FW_ABNORMAL;
    } else {
        panel_data->TP_FW = chip_info->fw_ver;
        snprintf(dev_version, MAX_DEVICE_VERSION_LENGTH,
                 "%02X", panel_data->TP_FW);
        TPD_INFO("%s: dev_version = %s \n", __func__, dev_version);
        if (panel_data->manufacture_info.version) {
            strlcpy(tmp_version, panel_data->manufacture_info.version, strlen(panel_data->manufacture_info.version)+1);
            if (panel_data->vid_len == 0) {
                ver_len = strlen(panel_data->manufacture_info.version);
                if (ver_len <= 11) {
                    //strlcat(panel_data->manufacture_info.version, dev_version, MAX_DEVICE_VERSION_LENGTH);
                    snprintf(panel_data->manufacture_info.version + 9, sizeof(dev_version), dev_version);
                } else {
                    strlcpy(&panel_data->manufacture_info.version[12], dev_version, 3);
                }
            } else {
                ver_len = panel_data->vid_len;
                if (ver_len > MAX_DEVICE_VERSION_LENGTH - 4) {
                    ver_len = MAX_DEVICE_VERSION_LENGTH - 4;
                }

                strlcpy(&panel_data->manufacture_info.version[ver_len], dev_version, MAX_DEVICE_VERSION_LENGTH - ver_len);
            }

        }
    }

    return FW_NORMAL;
}

static int32_t nvt_clear_fw_status(struct chip_data_nt36523 *chip_info)
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

static void nvt_enable_noise_collect(struct chip_data_nt36523 *chip_info, int32_t frame_num)
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

static int8_t nvt_switch_FreqHopEnDis(struct chip_data_nt36523 *chip_info, uint8_t FreqHopEnDis)
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

static void nvt_change_mode(struct chip_data_nt36523 *chip_info, uint8_t mode)
{
    uint8_t buf[8] = {0};

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

    //---set mode---
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = mode;
    CTP_SPI_WRITE(chip_info->s_client, buf, 2);

    if (mode == NORMAL_MODE) {
		msleep(20);
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = HANDSHAKING_HOST_READY;
        CTP_SPI_WRITE(chip_info->s_client, buf, 2);
        msleep(20);
    }
}

static uint8_t nvt_get_fw_pipe_noflash(struct chip_data_nt36523 *chip_info)
{
    int ret = -1;
    uint8_t buf[8] = {0};

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

static void nvt_read_mdata(struct chip_data_nt36523 *chip_info, uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len)
{
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
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

    if (xdata_len / sizeof(int32_t) < data_len / 2) {
        TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n", xdata_len, data_len);
        return;
    }

    //malloc buffer space
    xdata_tmp = kzalloc(xdata_len + XDATA_SECTOR_SIZE * 4, GFP_KERNEL);
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

#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
static void nvt_read_get_num_mdata(struct chip_data_nt36523 *chip_info , uint32_t xdata_addr , int32_t *xdata , uint32_t num)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;
	uint8_t *xdata_tmp = NULL;

	/* ---set xdata sector address & length--- */
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	/* malloc buffer space */
	xdata_tmp = kzalloc(num + XDATA_SECTOR_SIZE * 4, GFP_KERNEL);
	if (xdata_tmp == NULL) {
		TPD_INFO("%s malloc memory failed\n", __func__);
		return;
	}

	/* read xdata : step 1 */
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
        /* ---read xdata by SPI_TANSFER_LENGTH */
        for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
		/* ---change xdata index--- */
		nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) + (SPI_TANSFER_LENGTH * j));

		/* ---read data--- */
		buf[0] = SPI_TANSFER_LENGTH * j;
		CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

		/* ---copy buf to xdata_tmp--- */
		for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
                xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
                /* printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k)); */
		}
        }
        /* printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i)); */
	}

	/* read xdata : step2 */
	if (residual_len != 0) {
        /* ---read xdata by SPI_TANSFER_LENGTH */
        for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
		/* ---change xdata index--- */
		nvt_set_page(chip_info, xdata_addr + data_len - residual_len + (SPI_TANSFER_LENGTH * j));

		/* ---read data--- */
		buf[0] = SPI_TANSFER_LENGTH * j;
		CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

		/* ---copy buf to xdata_tmp--- */
		for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
                xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
                /* printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k)); */
		}
        }
        /* printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len)); */
	}

	/* ---remove dummy data and 2bytes-to-1data--- */
	for (i = 0; i < (data_len / 2); i++) {
        xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	kfree(xdata_tmp);
}
#endif

#ifdef CONFIG_OPLUS_TP_APK
static void nvt_read_debug_mdata(struct chip_data_nt36523 *chip_info,
                                 uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len)
{
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
    uint32_t head_addr = 0;
    int32_t dummy_len = 0;
    int32_t data_len = 0;
    int32_t residual_len = 0;
    uint8_t *xdata_tmp = NULL;

    //---set xdata sector address & length---
    head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
    dummy_len = xdata_addr - head_addr;
    data_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM;
    residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

    if (xdata_len / sizeof(int32_t) < data_len) {
        TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n", xdata_len, data_len);
        return;
    }

    //malloc buffer space
    xdata_tmp = kzalloc(xdata_len + XDATA_SECTOR_SIZE * 4, GFP_KERNEL);
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

    //---remove dummy data and scaling data (*8)---
    for (i = 0; i < (data_len); i++) {
        xdata[i] = (int16_t) (((int8_t) xdata_tmp[dummy_len + i]) * 8);
    }

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    kfree(xdata_tmp);
}
#endif // end of CONFIG_OPLUS_TP_APK

static int32_t nvt_polling_hand_shake_status(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 250;
	msleep(20);
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

static int32_t nvt_check_fw_status(struct chip_data_nt36523 *chip_info)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 50;
	msleep(20);
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

static uint8_t nvt_get_fw_pipe(struct chip_data_nt36523 *chip_info)
{
    int ret = -1;
    uint8_t buf[8] = {0};

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

static int32_t nvt_read_fw_noise(struct chip_data_nt36523 *chip_info, int32_t config_Diff_Test_Frame, int32_t *xdata, int32_t *xdata_n, int32_t xdata_len)
{
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t iArrayIndex = 0;
    int32_t frame_num = 0;

    if (xdata_len / sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
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


    return 0;
}

#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
static int32_t nvt_enter_digital_test(struct chip_data_nt36523 *chip_info, uint8_t enter_digital_test)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 70;

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

    //---set mode---
    if (enter_digital_test == true) {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0x32;
        buf[2] = 0x01;
        buf[3] = 0x08;
        buf[4] = 0x01;
        CTP_SPI_WRITE(chip_info->s_client, buf, 5);
    } else {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0x32;
        buf[2] = 0x00;
        buf[3] = 0x07;
        CTP_SPI_WRITE(chip_info->s_client, buf, 4);
    }

    //---poling fw handshake
    for (i = 0; i < retry; i++) {
        //---set xdata index to EVENT BUF ADDR---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

        //---read fw status---
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = 0x00;
        CTP_SPI_READ(chip_info->s_client, buf, 2);

        if (buf[1] == 0xAA)
            break;

		msleep(20);
	}

    if (i >= retry) {
        TPD_INFO("polling hand shake status failed, buf[1]=0x%02X\n", buf[1]);

        // Read back 5 bytes from offset EVENT_MAP_HOST_CMD for debug check
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0x00;
        buf[2] = 0x00;
        buf[3] = 0x00;
        buf[4] = 0x00;
        buf[5] = 0x00;
        CTP_SPI_READ(chip_info->s_client, buf, 6);
        TPD_INFO("Read back 5 bytes from offset EVENT_MAP_HOST_CMD: 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
                 buf[1], buf[2], buf[3], buf[4], buf[5]);

        return -1;
    } else {
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = 0xCC;
        CTP_SPI_WRITE(chip_info->s_client, buf, 2);
    }

    msleep(20);

    return 0;
}
#endif

static void store_to_file(int fd, char *format, ...)
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
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    }
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

static int32_t nvt_get_fw_need_write_size(const struct firmware *fw)
{
    int32_t i = 0;
    int32_t total_sectors_to_check = 0;

    total_sectors_to_check = fw->size / FLASH_SECTOR_SIZE;
    /* printk("total_sectors_to_check = %d\n", total_sectors_to_check); */

    for (i = total_sectors_to_check; i > 0; i--) {
        /* printk("current end flag address checked = 0x%X\n", i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN); */
        /* check if there is end flag "NVT" at the end of this sector */
        if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
            fw_need_write_size = i * FLASH_SECTOR_SIZE;
            TPD_INFO("fw_need_write_size = %zu(0x%zx), NVT end flag\n", fw_need_write_size, fw_need_write_size);
            return 0;
        }

        /* check if there is end flag "MOD" at the end of this sector */
        if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "MOD", NVT_FLASH_END_FLAG_LEN) == 0) {
            fw_need_write_size = i * FLASH_SECTOR_SIZE;
            TPD_INFO("fw_need_write_size = %zu(0x%zx), MOD end flag\n", fw_need_write_size, fw_need_write_size);
            return 0;
        }
    }

    TPD_INFO("end flag \"NVT\" \"MOD\" not found!\n");
    return -EPERM;
}

static fw_update_state nvt_fw_update_sub(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    uint8_t point_data[POINT_DATA_LEN + 2] = {0};
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
    struct firmware *request_fw_headfile = NULL;

    //check bin file size(116kb)
    //if(fw != NULL && fw->size != FW_BIN_SIZE) {
        //TPD_INFO("bin file size not match (%zu), change to use headfile fw.\n", fw->size);
        //goto out_fail;
        //release_firmware(fw);
        //fw = NULL;
    //}

    //request firmware failed, get from headfile
    if(fw == NULL) {
        request_fw_headfile = kzalloc(sizeof(struct firmware), GFP_KERNEL);
        if(request_fw_headfile == NULL) {
            TPD_INFO("%s kzalloc failed!\n", __func__);
            goto out_fail;
        }

        if (chip_info->g_fw_sta) {
            TPD_INFO("request firmware failed, get from g_fw_buf\n");
            request_fw_headfile->size = chip_info->g_fw_len;
            request_fw_headfile->data = chip_info->g_fw_buf;
            fw = request_fw_headfile;

        } else {
            TPD_INFO("request firmware failed, get from headfile\n");
		if(chip_info->p_firmware_headfile->firmware_data) {
                	request_fw_headfile->size = chip_info->p_firmware_headfile->firmware_size;
                	request_fw_headfile->data = chip_info->p_firmware_headfile->firmware_data;
                	fw = request_fw_headfile;
		} else {
                TPD_INFO("firmware_data is NULL! exit firmware update!\n");
                goto out_fail;
		}
        }
    }

    //check bin file size(116kb)
    //if(fw->size != FW_BIN_SIZE) {
    //    TPD_INFO("fw file size not match. (%zu)\n", fw->size);
    //    goto out_fail;
    //}

    if (nvt_get_fw_need_write_size(fw)) {
        TPD_INFO("get fw need to write size fail!\n");
        goto out_fail;
    }

    // check if FW version add FW version bar equals 0xFF
    if (*(fw->data + FW_BIN_VER_OFFSET) + * (fw->data + FW_BIN_VER_BAR_OFFSET) != 0xFF) {
        TPD_INFO("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
        TPD_INFO("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n", *(fw->data + FW_BIN_VER_OFFSET), *(fw->data + FW_BIN_VER_BAR_OFFSET));
        goto out_fail;
    }

    //fw checksum compare
    ret = nvt_check_bin_checksum(fw->data, fw->size);
    if (ret) {
        if (fw != request_fw_headfile) {
            TPD_INFO("Image fw check checksum failed, reload fw from array\n");

            goto out_fail;

        } else {
            TPD_INFO("array fw check checksum failed, but use still\n");
        }
    } else {
        TPD_INFO("fw check checksum ok\n");
    }

    /* BIN Header Parser */
    ret = nvt_bin_header_parser(chip_info, fw->data, fw->size);
    if (ret) {
        TPD_INFO("bin header parser failed\n");
        goto out_fail;
    }

    /* initial buffer and variable */
    ret = Download_Init(chip_info);
    if (ret) {
        TPD_INFO("Download Init failed. (%d)\n", ret);
        goto out_fail;
    }

    /* download firmware process */
    ret = Download_Firmware_HW_CRC(chip_info, fw);
    if (ret) {
        TPD_INFO("Download Firmware failed. (%d)\n", ret);
        goto out_fail;
    }

    TPD_INFO("Update firmware success! <%ld us>\n",
             (long) ktime_us_delta(end, start));

    /* Get FW Info */
    ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 1);
    if (ret < 0 || nvt_fw_recovery(point_data)) {
        nvt_esd_check_enable(chip_info, true);
    }
    ret = nvt_get_fw_info_noflash(chip_info);
    if (ret) {
        TPD_INFO("nvt_get_fw_info_noflash failed. (%d)\n", ret);
        goto out_fail;
    }
    ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 1);
    if (ret < 0 || nvt_fw_recovery(point_data)) {
        nvt_esd_check_enable(chip_info, true);
    }
    nvt_fw_check(ts->chip_data, &ts->resolution_info, &ts->panel_data);

    if(chip_info->bin_map != NULL) {
        kfree(chip_info->bin_map);
        chip_info->bin_map = NULL;
    }

    if(request_fw_headfile != NULL) {
        kfree(request_fw_headfile);
        request_fw_headfile = NULL;
        //fw = NULL;
    }
    return FW_UPDATE_SUCCESS;

out_fail:
    if(chip_info->bin_map != NULL) {
        kfree(chip_info->bin_map);
        chip_info->bin_map = NULL;
    }
    if(request_fw_headfile != NULL) {
        kfree(request_fw_headfile);
        request_fw_headfile = NULL;
        //fw = NULL;
    }
	if (ret) {
		return FW_UPDATE_ERROR;
	} else {
		return FW_NO_NEED_UPDATE;
	}
}



static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    if (fw) {
        if (chip_info->g_fw_buf) {
            chip_info->g_fw_len = fw->size;
			if (fw->size > FW_BUF_SIZE) {
				TPD_INFO("error! FW_BUF_SIZE = %d, size = %ld\n", FW_BUF_SIZE, fw->size);
				return -1;
			}
            memcpy(chip_info->g_fw_buf, fw->data, fw->size);
            chip_info->g_fw_sta = true;
        }
    }
    return nvt_fw_update_sub(chip_data, fw, force);

}

static void nvt_store_testrecord_to_file(int fd, char *item,
        uint8_t result, uint8_t *record, int txnum, int rxnum)
{
    uint8_t i, j;
    int32_t iArrayIndex = 0;

    if (result == NVT_MP_FAIL) {
        store_to_file(fd, "%s Fail!", item);
        /* check fail point */
        for (j = 0; j < rxnum; j++) {
            for (i = 0; i < txnum; i++) {
                iArrayIndex = j * txnum + i;
                if (record[iArrayIndex] != 0) {
                    store_to_file(fd, ", (%d,%d)", i, j);
                }
            }
        }
        store_to_file(fd, "\n");
    } else if (result == NVT_MP_FAIL_READ_DATA) {
        store_to_file(fd, "%s read data Fail!\n", item);
    } else if (result == NVT_MP_UNKNOW) {
        store_to_file(fd, "%s unknow!\n", item);
    } else if (result == NVT_MP_PASS) {
        store_to_file(fd, "%s OK!\n", item);
    }

    return;
}

static void nvt_store_testdata_to_file(int fd, char *item, uint8_t result, int32_t *rawdata, int txnum, int rxnum)
{
    uint8_t i, j;
    int32_t iArrayIndex = 0;

    if (fd < 0) {
        TPD_INFO("Open log file failed.\n");
        goto exit;
    }

    if (result == NVT_MP_FAIL_READ_DATA) {
        store_to_file(fd, "read %s data failed!\n", item);
        goto exit;
    } else if (result == NVT_MP_UNKNOW) {
        store_to_file(fd, "read %s unknow!\n", item);
        goto exit;
    }

    store_to_file(fd, "%s:\n", item);
    for (j = 0; j < rxnum; j++) {
        for (i = 0; i < txnum; i++) {
            iArrayIndex = j * txnum + i;
            store_to_file(fd, "%d, ", rawdata[iArrayIndex]);
        }
        store_to_file(fd, "\n");
    }

exit:
    return;
}

static void nvt_enable_doze_noise_collect(struct chip_data_nt36523 *chip_info, int32_t frame_num)
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

static int32_t nvt_read_doze_fw_noise(struct chip_data_nt36523 *chip_info, int32_t config_Doze_Noise_Test_Frame, int32_t doze_X_Channel, int32_t *xdata, int32_t *xdata_n, int32_t xdata_len)
{
    uint8_t buf[128] = {0};
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t rx_num = chip_info->hw_res->RX_NUM;
    int32_t iArrayIndex = 0;
    int32_t frame_num = 0;

    if (xdata_len / sizeof(int32_t) < rx_num * doze_X_Channel) {
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
    msleep(frame_num * 83);

    if (nvt_polling_hand_shake_status(chip_info)) {
        return -EAGAIN;
    }

    for (x = 0; x < doze_X_Channel; x++) {
        //---change xdata index---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + rx_num * 2 * x);

        //---read data---
        buf[0] = (chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + rx_num * 2 * x) & 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, rx_num * 2 + 1);

        for (y = 0; y < rx_num; y++) {
	xdata[y * doze_X_Channel + x] = (uint16_t)(buf[y * 2 + 1] + 256 * buf[y * 2 + 2]);
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

static int32_t nvt_read_doze_baseline(struct chip_data_nt36523 *chip_info, int32_t doze_X_Channel, int32_t *xdata, int32_t xdata_len)
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
    if (xdata_len / sizeof(int32_t) < rm_num * doze_X_Channel) {
        TPD_INFO("read doze baseline buffer(%d) less than data size(%d)\n", xdata_len, rm_num * doze_X_Channel);
        return -1;
    }

    for (x = 0; x < doze_X_Channel; x++) {
        //---change xdata index---
        nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR + rm_num * 2 * x);

        //---read data---
        buf[0] = (chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR + rm_num * 2 * x) & 0xFF;
        CTP_SPI_READ(chip_info->s_client, buf, rm_num * 2 + 1);
        for (y = 0; y < rm_num; y++) {
	xdata[y * doze_X_Channel + x] = (uint16_t)(buf[y * 2 + 1] + 256 * buf[y * 2 + 2]);
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


static void nvt_black_screen_test(void *chip_data, char *message)
{
    /* test data */
    int32_t *raw_data = NULL;
    int32_t *noise_p_data = NULL;
    int32_t *noise_n_data = NULL;
    int32_t *fdm_noise_p_data = NULL;
    int32_t *fdm_noise_n_data = NULL;
    int32_t *fdm_raw_data = NULL;
    /* test result */
    uint8_t rawdata_result = NVT_MP_UNKNOW;
    uint8_t noise_p_result = NVT_MP_UNKNOW;
    uint8_t noise_n_result = NVT_MP_UNKNOW;
    uint8_t fdm_noise_p_result = NVT_MP_UNKNOW;
    uint8_t fdm_noise_n_result = NVT_MP_UNKNOW;
    uint8_t fdm_rawdata_result = NVT_MP_UNKNOW;
    /* record data */
    uint8_t *raw_record = NULL;
    uint8_t *noise_p_record = NULL;
    uint8_t *noise_n_record = NULL;
    uint8_t *fdm_noise_p_record = NULL;
    uint8_t *fdm_noise_n_record = NULL;
    uint8_t *fdm_raw_record = NULL;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    int tx_num = chip_info->hw_res->TX_NUM;
    int rx_num = chip_info->hw_res->RX_NUM;
    const struct firmware *fw = NULL;
    struct nvt_test_header *ph = NULL;
    int i, j, iArrayIndex, err_cnt = 0;
    int fd = -1, ret = -1;
    mm_segment_t old_fs;
    char buf[128] = {0};
    uint8_t data_buf[128], all_test_result[8];
    int32_t buf_len = 0, record_len = 0;
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    char *p_node = NULL;
    char *fw_name_test = NULL;
    char *postfix = "_TEST.bin";
    uint8_t copy_len = 0;
    int32_t *lpwg_rawdata_P = NULL, *lpwg_rawdata_N = NULL;
    int32_t *lpwg_diff_rawdata_P = NULL, *lpwg_diff_rawdata_N = NULL;
    int32_t *fdm_rawdata_P = NULL, *fdm_rawdata_N = NULL;
    int32_t *fdm_diff_rawdata_P = NULL, *fdm_diff_rawdata_N = NULL;

    nvt_esd_check_enable(chip_info, false);

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
    chip_info->need_judge_irq_throw = true;

	/* ret = nvt_fw_update_sub(chip_info, fw, 0); */

	/* if(ret > 0) {
		TPD_INFO("fw update failed!\n");
		goto RELEASE_FIRMWARE;
	} */

	TPD_INFO("%s : update test firmware successed\n", __func__);

    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
	/* ret = nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
    TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success"); */
    msleep(500);	/* for FDM (500ms) */

    if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
        TPD_INFO("check fw reset state failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, check fw reset state failed!\n");
        goto RELEASE_FIRMWARE;
    }

    if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
        TPD_INFO("switch frequency hopping disable failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, switch frequency hopping disable failed!\n");
        goto RELEASE_FIRMWARE;
    }

    if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
        TPD_INFO("check fw reset state failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, check fw reset state failed!\n");
        goto RELEASE_FIRMWARE;
    }

    msleep(100);

    //---Enter Test Mode---
    if (nvt_clear_fw_status(chip_info)) {
        TPD_INFO("clear fw status failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, clear fw status failed!\n");
        goto RELEASE_FIRMWARE;
    }

    nvt_change_mode(chip_info, TEST_MODE_2);

    if (nvt_check_fw_status(chip_info)) {
        TPD_INFO("check fw status failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, clear fw status failed!\n");
        goto RELEASE_FIRMWARE;
    }

    if (nvt_get_fw_info_noflash(chip_info)) {
        TPD_INFO("get fw info failed!\n");
        snprintf(message, MESSAGE_SIZE,
                 "1 error, get fw info failed!\n");
        goto RELEASE_FIRMWARE;
    }

    TPD_INFO("malloc raw_data space\n");
    buf_len = tx_num * rx_num * sizeof(int32_t);
    raw_data = kzalloc(buf_len, GFP_KERNEL);
    noise_p_data = kzalloc(buf_len, GFP_KERNEL);
    noise_n_data = kzalloc(buf_len, GFP_KERNEL);
    fdm_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
    fdm_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
    fdm_raw_data = kzalloc(buf_len, GFP_KERNEL);
    if (!(raw_data && noise_p_data && noise_n_data
          && fdm_raw_data && fdm_noise_p_data && fdm_noise_n_data)) {
        TPD_INFO("kzalloc space failed\n");
        snprintf(message, MESSAGE_SIZE, "1 error, kzalloc space failed\n");
        goto RELEASE_DATA;
    }

    TPD_INFO("malloc raw_record space\n");
    record_len = tx_num * rx_num * sizeof(uint8_t);
    raw_record = kzalloc(record_len, GFP_KERNEL);
    noise_p_record = kzalloc(record_len, GFP_KERNEL);
    noise_n_record = kzalloc(record_len, GFP_KERNEL);
    fdm_noise_p_record = kzalloc(record_len, GFP_KERNEL);
    fdm_noise_n_record = kzalloc(record_len, GFP_KERNEL);
    fdm_raw_record = kzalloc(record_len, GFP_KERNEL);
    if (!(raw_record && noise_p_record && noise_n_record
          && fdm_raw_record && fdm_noise_p_record && fdm_noise_n_record)) {
        TPD_INFO("kzalloc space failed\n");
        snprintf(message, MESSAGE_SIZE, "1 error, kzalloc space failed\n");
        goto RELEASE_DATA;
    }

    ret = request_firmware(&fw, chip_info->test_limit_name, &chip_info->s_client->dev);
    TPD_INFO("Roland--->fw path is %s\n", chip_info->test_limit_name);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", chip_info->test_limit_name, ret);
        snprintf(message, MESSAGE_SIZE,
                 "1 error, Request firmware failed: %s\n",
                 chip_info->test_limit_name);

        goto RELEASE_DATA;
    }
    ph = (struct nvt_test_header *)(fw->data);

    lpwg_rawdata_P = (int32_t *)(fw->data + ph->array_LPWG_Rawdata_P_offset);
    lpwg_rawdata_N = (int32_t *)(fw->data + ph->array_LPWG_Rawdata_N_offset);
    lpwg_diff_rawdata_P = (int32_t *)(fw->data + ph->array_LPWG_Diff_P_offset);
    lpwg_diff_rawdata_N = (int32_t *)(fw->data + ph->array_LPWG_Diff_N_offset);
    fdm_rawdata_P = (int32_t *)(fw->data + ph->array_FDM_Rawdata_P_offset);
    fdm_rawdata_N = (int32_t *)(fw->data + ph->array_FDM_Rawdata_N_offset);
    fdm_diff_rawdata_P = (int32_t *)(fw->data + ph->array_FDM_Diff_P_offset);
    fdm_diff_rawdata_N = (int32_t *)(fw->data + ph->array_FDM_Diff_N_offset);
    //---FW Rawdata Test---
    TPD_INFO("LPWG mode FW Rawdata Test \n");
    memset(raw_data, 0, buf_len);
    memset(raw_record, 0, record_len);
    rawdata_result = NVT_MP_PASS;
    if (nvt_get_fw_pipe(chip_info) == 0)
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR, raw_data, buf_len);
    else
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR, raw_data, buf_len);
    if ((ph->config_Lmt_LPWG_Rawdata_P != 0) && (ph->config_Lmt_LPWG_Rawdata_N != 0)) {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
                if((raw_data[iArrayIndex] > ph->config_Lmt_LPWG_Rawdata_P) \
                   || (raw_data[iArrayIndex] < ph->config_Lmt_LPWG_Rawdata_N)) {
                    rawdata_result = NVT_MP_FAIL;
                    raw_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d[%d %d]\n",
                             i, j, raw_data[iArrayIndex], ph->config_Lmt_LPWG_Rawdata_N, ph->config_Lmt_LPWG_Rawdata_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Rawdata[%d][%d] = %d[%d %d]\n",
                                 i, j, raw_data[iArrayIndex],
                                 ph->config_Lmt_LPWG_Rawdata_N,
                                 ph->config_Lmt_LPWG_Rawdata_P);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
    } else {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);
                if((raw_data[iArrayIndex] > lpwg_rawdata_P[iArrayIndex]) \
                   || (raw_data[iArrayIndex] < lpwg_rawdata_N[iArrayIndex])) {
                    rawdata_result = NVT_MP_FAIL;
                    raw_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d\n", i, j, raw_data[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Rawdata[%d][%d] = %d[%d %d]\n",
                                 i, j, raw_data[iArrayIndex],
                                 lpwg_rawdata_N[iArrayIndex],
                                 lpwg_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
    }

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);

    //---Noise Test---
    TPD_INFO("LPWG mode FW Noise Test \n");
    memset(noise_p_data, 0, buf_len); //store max
    memset(noise_n_data, 0, buf_len); //store min
    memset(noise_p_record, 0, record_len);
    memset(noise_n_record, 0, record_len);
    noise_p_result = NVT_MP_PASS;
    noise_n_result = NVT_MP_PASS;
    if (nvt_read_fw_noise(chip_info, ph->config_Diff_Test_Frame, noise_p_data, noise_n_data, buf_len) != 0) {
        TPD_INFO("LPWG mode read Noise data failed!\n");    // 1: ERROR
        noise_p_result = NVT_MP_FAIL_READ_DATA;
        noise_n_result = NVT_MP_FAIL_READ_DATA;
        snprintf(buf, 128, "LPWG mode read Noise data failed!\n");
        err_cnt++;
        goto TEST_END;
    }
    TPD_INFO("LPWG Noise RawData_Diff_Max:\n");
    if ((ph->config_Lmt_LPWG_Diff_P != 0) && (ph->config_Lmt_LPWG_Diff_N != 0)) {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", noise_p_data[iArrayIndex]);
                if((noise_p_data[iArrayIndex] > ph->config_Lmt_LPWG_Diff_P) \
                   || (noise_p_data[iArrayIndex] < ph->config_Lmt_LPWG_Diff_N)) {
                    noise_p_result = NVT_MP_FAIL;
                    noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d[%d %d]\n",
                             i, j, noise_p_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N, ph->config_Lmt_LPWG_Diff_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
                                 i, j, noise_p_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N, ph->config_Lmt_LPWG_Diff_P);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
        TPD_INFO("LPWG Noise RawData_Diff_Min:\n");
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", noise_n_data[iArrayIndex]);
                if((noise_n_data[iArrayIndex] > ph->config_Lmt_LPWG_Diff_P) \
                   || (noise_n_data[iArrayIndex] < ph->config_Lmt_LPWG_Diff_N)) {
                    noise_n_result = NVT_MP_FAIL;
                    noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d[%d %d]\n",
                             i, j, noise_n_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N,  ph->config_Lmt_LPWG_Diff_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
                                 i, j, noise_n_data[iArrayIndex], ph->config_Lmt_LPWG_Diff_N,  ph->config_Lmt_LPWG_Diff_P);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
    } else {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", noise_p_data[iArrayIndex]);
                if((noise_p_data[iArrayIndex] > lpwg_diff_rawdata_P[iArrayIndex]) \
                   || (noise_p_data[iArrayIndex] < lpwg_diff_rawdata_N[iArrayIndex])) {
                    noise_p_result = NVT_MP_FAIL;
                    noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d\n", i, j, raw_data[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
                                 i, j, noise_p_data[iArrayIndex], lpwg_diff_rawdata_N[iArrayIndex], lpwg_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
        TPD_INFO("LPWG Noise RawData_Diff_Min:\n");
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                TPD_DEBUG_NTAG("%d, ", noise_n_data[iArrayIndex]);
                if((noise_n_data[iArrayIndex] > lpwg_diff_rawdata_P[iArrayIndex]) \
                   || (noise_n_data[iArrayIndex] < lpwg_diff_rawdata_N[iArrayIndex])) {
                    noise_n_result = NVT_MP_FAIL;
                    noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d\n", i, j, noise_n_data[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
                                 i, j, noise_n_data[iArrayIndex], lpwg_diff_rawdata_N[iArrayIndex], lpwg_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            TPD_DEBUG_NTAG("\n");
        }
    }

	/* ---Leave Test Mode--- */
	nvt_change_mode(chip_info, NORMAL_MODE);
	/* ---FDM Noise Test--- */
    TPD_INFO("FDM FW Noise Test \n");
    memset(fdm_noise_p_data, 0, buf_len); //store max
    memset(fdm_noise_n_data, 0, buf_len); //store min
    memset(fdm_noise_p_record, 0, record_len);
    memset(fdm_noise_n_record, 0, record_len);
    fdm_noise_p_result = NVT_MP_PASS;
    fdm_noise_n_result = NVT_MP_PASS;
    if (nvt_read_doze_fw_noise(chip_info, ph->config_FDM_Noise_Test_Frame, ph->fdm_X_Channel, fdm_noise_p_data, fdm_noise_n_data, buf_len) != 0) {
        TPD_INFO("read FDM Noise data failed!\n");
        fdm_noise_p_result = NVT_MP_FAIL_READ_DATA;
        fdm_noise_n_result = NVT_MP_FAIL_READ_DATA;
        snprintf(buf, 128, "read FDM Noise data failed!\n");
        err_cnt++;
        goto TEST_END;
    }

    if ((ph->config_Lmt_FDM_Diff_P != 0) && (ph->config_Lmt_FDM_Diff_N != 0)) {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_noise_p_data[iArrayIndex] > ph->config_Lmt_FDM_Diff_P) \
                   || (fdm_noise_p_data[iArrayIndex] < ph->config_Lmt_FDM_Diff_N)) {
                    fdm_noise_p_result = NVT_MP_FAIL;
                    fdm_noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_p_data[iArrayIndex], ph->config_Lmt_FDM_Diff_N, ph->config_Lmt_FDM_Diff_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_p_data[iArrayIndex], ph->config_Lmt_FDM_Diff_N, ph->config_Lmt_FDM_Diff_P);
                    }
                    err_cnt++;
                }
            }
        }

        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_noise_n_data[iArrayIndex] > ph->config_Lmt_FDM_Diff_P) \
                   || (fdm_noise_n_data[iArrayIndex] < ph->config_Lmt_FDM_Diff_N)) {
                    fdm_noise_n_result = NVT_MP_FAIL;
                    fdm_noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], ph->config_Lmt_FDM_Diff_N, ph->config_Lmt_FDM_Diff_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], ph->config_Lmt_FDM_Diff_N, ph->config_Lmt_FDM_Diff_P);
                    }
                    err_cnt++;
                }
            }
        }
    } else {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_noise_n_data[iArrayIndex] > fdm_diff_rawdata_P[iArrayIndex]) \
                   || (fdm_noise_n_data[iArrayIndex] < fdm_diff_rawdata_N[iArrayIndex])) {
                    fdm_noise_p_result = NVT_MP_FAIL;
                    fdm_noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], fdm_diff_rawdata_N[iArrayIndex], fdm_diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], fdm_diff_rawdata_N[iArrayIndex], fdm_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_noise_n_data[iArrayIndex] > fdm_diff_rawdata_P[iArrayIndex]) \
                   || (fdm_noise_n_data[iArrayIndex] < fdm_diff_rawdata_N[iArrayIndex])) {
                    fdm_noise_n_result = NVT_MP_FAIL;
                    fdm_noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], fdm_diff_rawdata_N[iArrayIndex], fdm_diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_noise_n_data[iArrayIndex], fdm_diff_rawdata_N[iArrayIndex], fdm_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
    }

    //---FDM FW Rawdata Test---
    TPD_INFO("FDM FW Rawdata Test \n");
    memset(fdm_raw_data, 0, buf_len);
    memset(fdm_raw_record, 0, record_len);
    fdm_rawdata_result = NVT_MP_PASS;
    if(nvt_read_doze_baseline(chip_info, ph->fdm_X_Channel, fdm_raw_data, buf_len) != 0) {
        TPD_INFO("read FDM FW Rawdata failed!\n");
        fdm_rawdata_result = NVT_MP_FAIL_READ_DATA;
        snprintf(buf, 128,
                 "read FDM FW Rawdata failed!\n");
        err_cnt++;
        goto TEST_END;
    }

    if ((ph->config_Lmt_FDM_Rawdata_P != 0) && (ph->config_Lmt_FDM_Rawdata_N != 0)) {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_raw_data[iArrayIndex] > ph->config_Lmt_FDM_Rawdata_P) \
                   || (fdm_raw_data[iArrayIndex] < ph->config_Lmt_FDM_Rawdata_N)) {
                    fdm_rawdata_result = NVT_MP_FAIL;
                    fdm_raw_record[iArrayIndex] = 1;
                    TPD_INFO("FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_raw_data[iArrayIndex], ph->config_Lmt_FDM_Rawdata_N, ph->config_Lmt_FDM_Rawdata_P);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_raw_data[iArrayIndex], ph->config_Lmt_FDM_Rawdata_N, ph->config_Lmt_FDM_Rawdata_P);
                    }
                    err_cnt++;
                }
            }
        }
    } else {
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->fdm_X_Channel; i++) {
                iArrayIndex = j * ph->fdm_X_Channel + i;
                if((fdm_raw_data[iArrayIndex] > fdm_rawdata_P[iArrayIndex]) \
                   || (fdm_raw_data[iArrayIndex] < fdm_rawdata_N[iArrayIndex])) {
                    fdm_rawdata_result = NVT_MP_FAIL;
                    fdm_raw_record[iArrayIndex] = 1;
                    TPD_INFO("FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_raw_data[iArrayIndex], fdm_rawdata_N[iArrayIndex], fdm_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        snprintf(buf, 128,
                                 "FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, fdm_raw_data[iArrayIndex], fdm_rawdata_N[iArrayIndex], fdm_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
    }

TEST_END:
    //create a file to store test data in /sdcard/ScreenOffTpTestReport
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    if (!err_cnt) {
        snprintf(all_test_result, 8, "PASS");
        snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOff/OK/tp_testlimit_%s_%02d%02d%02d-%02d%02d%02d-utc.csv",
                 (char *)all_test_result,
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    } else {
        snprintf(all_test_result, 8, "FAIL");
        snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOff/NG/tp_testlimit_%s_%02d%02d%02d-%02d%02d%02d-utc.csv",
                 (char *)all_test_result,
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOff/OK", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOff/NG", 0666);
    fd = ksys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
    sys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/OK", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/NG", 0666);
    fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    if (fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", data_buf);
        goto OUT;
    }

    /* Project Name */
    store_to_file(fd, "OPLUS_%d\n", 19131);
    /* Project ID */
    store_to_file(fd, "NVTPID= %04X\n", chip_info->nvt_pid);
    /* FW Version */
    store_to_file(fd, "FW Version= V0x%02X\n", chip_info->fw_ver);
    /* Panel Info */
    store_to_file(fd, "Panel Info= %d x %d\n", tx_num, rx_num);
    /* Test Stage */
    store_to_file(fd, "Test Platform= Mobile\n");

    store_to_file(fd, "This is %s module\n",
                  chip_info->ts->panel_data.manufacture_info.manufacture);

    /* (Pass/Fail) */
    store_to_file(fd, "Test Results= %s\n", all_test_result);

    /* Test Item */
    /* 1. rawdata        */
    nvt_store_testrecord_to_file(fd, "LPWG mode FW Rawdata",
                                 rawdata_result, raw_record, tx_num, rx_num);
    /* 2. noise max      */
    nvt_store_testrecord_to_file(fd, "LPWG Noise RawData_Diff_Max",
                                 noise_p_result, noise_p_record, tx_num, rx_num);
    /* 3. noise min      */
    nvt_store_testrecord_to_file(fd, "LPWG Noise RawData_Diff_Min",
                                 noise_n_result, noise_n_record, tx_num, rx_num);
    /* 4. fdm noise max */
    nvt_store_testrecord_to_file(fd, "FDM RawData_Diff_Max",
                                 fdm_noise_p_result, fdm_noise_p_record, ph->fdm_X_Channel, rx_num);
    /* 5. fdm noise min */
    nvt_store_testrecord_to_file(fd, "FDM RawData_Diff_Min",
                                 fdm_noise_n_result, fdm_noise_n_record, ph->fdm_X_Channel, rx_num);
    /* 6. fdm rawdata   */
    nvt_store_testrecord_to_file(fd, "FDM FW Rawdata",
                                 fdm_rawdata_result, fdm_raw_record, ph->fdm_X_Channel, rx_num);

    /* 1. rawdata        */
    nvt_store_testdata_to_file(fd, "LPWG mode FW Rawdata",
                               rawdata_result, raw_data, tx_num, rx_num);
    /* 2. noise max      */
    nvt_store_testdata_to_file(fd, "LPWG Noise RawData_Diff_Max",
                               noise_p_result, noise_p_data, tx_num, rx_num);
    /* 3. noise min      */
    nvt_store_testdata_to_file(fd, "LPWG Noise RawData_Diff_Min",
                               noise_n_result, noise_n_data, tx_num, rx_num);
    /* 4. fdm noise max */
    nvt_store_testdata_to_file(fd, "FDM RawData_Diff_Max",
                               fdm_noise_p_result, fdm_noise_p_data, ph->fdm_X_Channel, rx_num);
    /* 5. fdm noise min */
    nvt_store_testdata_to_file(fd, "FDM RawData_Diff_Min",
                               fdm_noise_n_result, fdm_noise_n_data, ph->fdm_X_Channel, rx_num);
    /* 6. fdm rawdata   */
    nvt_store_testdata_to_file(fd, "FDM FW Rawdata",
                               fdm_rawdata_result, fdm_raw_data, ph->fdm_X_Channel, rx_num);

OUT:
    if (fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_close(fd);
#else
        sys_close(fd);
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    }
    set_fs(old_fs);

    snprintf(message, MESSAGE_SIZE, "%d error(s). %s%s\n", err_cnt, err_cnt?"":"All test passed.",buf);
    TPD_INFO("%d errors. %s", err_cnt, buf);

RELEASE_DATA:
    if (raw_record)
        kfree(raw_record);
    if (noise_p_record)
        kfree(noise_p_record);
    if (noise_n_record)
        kfree(noise_n_record);
    if (fdm_noise_p_record)
        kfree(fdm_noise_p_record);
    if (fdm_noise_n_record)
        kfree(fdm_noise_n_record);
    if (fdm_raw_record)
        kfree(fdm_raw_record);

    if (raw_data)
        kfree(raw_data);
    if (noise_p_data)
        kfree(noise_p_data);
    if (noise_n_data)
        kfree(noise_n_data);
    if (fdm_noise_p_data)
        kfree(fdm_noise_p_data);
    if (fdm_noise_n_data)
        kfree(fdm_noise_n_data);
    if (fdm_raw_data)
        kfree(fdm_raw_data);

RELEASE_FIRMWARE:
    release_firmware(fw);
    kfree(fw_name_test);
    fw_name_test = NULL;
    chip_info->need_judge_irq_throw = false;
}

static void nvt_set_touch_direction(void *chip_data, uint8_t dir)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    chip_info->touch_direction = dir;
}

static uint8_t nvt_get_touch_direction(void *chip_data)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    return chip_info->touch_direction;
}

static bool nvt_irq_throw_away(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    if (chip_info->need_judge_irq_throw) {
        TPD_INFO("wake up the throw away irq!\n");
        return true;

    }

    return false;
}

static void nvt_set_gesture_state(void *chip_data, int state)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    chip_info->gesture_state = state;
}

static struct oplus_touchpanel_operations nvt_ops = {
    .ftm_process                = nvt_ftm_process,
    .reset                      = nvt_reset,
    .power_control              = nvt_power_control,
    .get_chip_info              = nvt_get_chip_info,
    .u32_trigger_reason         = nvt_trigger_reason,
    .get_touch_points           = nvt_get_touch_points,
    .get_pen_points             = nvt_get_pen_points,
    .get_gesture_info           = nvt_get_gesture_info,
    .mode_switch                = nvt_mode_switch,
    .fw_check                   = nvt_fw_check,
    .fw_update                  = nvt_fw_update,
    .get_vendor                 = nvt_get_vendor,
//    .get_usb_state              = nvt_get_usb_state,
    .black_screen_test          = nvt_black_screen_test,
    .esd_handle                 = nvt_esd_handle,
    .reset_gpio_control         = nvt_reset_gpio_control,
//.cs_gpio_control            = nvt_cs_gpio_control,
    .set_touch_direction        = nvt_set_touch_direction,
    .get_touch_direction        = nvt_get_touch_direction,
    .tp_irq_throw_away          = nvt_irq_throw_away,
    .set_gesture_state          = nvt_set_gesture_state,
};

static void nvt_data_read(struct seq_file *s, struct chip_data_nt36523 *chip_info, DEBUG_READ_TYPE read_type)
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
    xdata = kzalloc(buf_len, GFP_KERNEL);
    if (!xdata) {
        TPD_INFO("%s, malloc memory failed\n", __func__);
        return;
    }
    memset(xdata, 0, buf_len);

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

#ifdef CONFIG_OPLUS_TP_APK
static void nvt_debug_data_read(struct seq_file *s, struct chip_data_nt36523 *chip_info, DEBUG_READ_TYPE read_type)
{
    int ret = -1;
    int i, j;
    //uint8_t pipe;
    int32_t *xdata = NULL;
    int32_t buf_len = 0;

    TPD_INFO("nvt get fw info start");
    ret = nvt_get_fw_info_noflash(chip_info);
    if (ret < 0) {
        TPD_INFO("get_fw_info error, return\n");
        return;
    }

    buf_len = chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * sizeof(int32_t);
    xdata = kzalloc(buf_len, GFP_KERNEL);
    if (!xdata) {
        TPD_INFO("%s, malloc memory failed\n", __func__);
        return;
    }

    switch (read_type) {
    case NVT_DEBUG_FINGER_DOWN_DIFFDATA:
        seq_printf(s, "debug finger down diff data:\n");
        nvt_read_debug_mdata(chip_info, NVT_MMAP_DEBUG_FINGER_DOWN_DIFFDATA, xdata, buf_len);
        break;

    case NVT_DEBUG_STATUS_CHANGE_DIFFDATA:
        seq_printf(s, "debug status change diff data:\n");
        nvt_read_debug_mdata(chip_info, NVT_MMAP_DEBUG_STATUS_CHANGE_DIFFDATA, xdata, buf_len);
        break;
    default:
        break;
    }

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
#endif // end of CONFIG_OPLUS_TP_APK

static void nvt_delta_read(struct seq_file *s, void *chip_data)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_esd_check_update_timer(chip_info);

    nvt_data_read(s, chip_info, NVT_DIFFDATA);

#ifdef CONFIG_OPLUS_TP_APK
    if (chip_info->debug_mode_sta) {
        nvt_debug_data_read(s, chip_info, NVT_DEBUG_FINGER_DOWN_DIFFDATA);
    }
#endif // end of CONFIG_OPLUS_TP_APK
}

static void nvt_baseline_read(struct seq_file *s, void *chip_data)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_esd_check_update_timer(chip_info);

    nvt_data_read(s, chip_info, NVT_BASEDATA);
    nvt_data_read(s, chip_info, NVT_RAWDATA);
}

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused void nvt_dbg_diff_finger_down_read(struct seq_file *s, void *chip_data)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_debug_data_read(s, chip_info, NVT_DEBUG_FINGER_DOWN_DIFFDATA);
}

static __maybe_unused void nvt_dbg_diff_status_change_read(struct seq_file *s, void *chip_data)
{
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_debug_data_read(s, chip_info, NVT_DEBUG_STATUS_CHANGE_DIFFDATA);
}
#endif // end of CONFIG_OPLUS_TP_APK

static void nvt_read_fw_history(struct seq_file *s, void *chip_data, uint32_t NVT_MMAP_HISTORY_ADDR)
{
    uint8_t i = 0;
    uint8_t buf[66];
	char str[128];
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_set_page(chip_info, NVT_MMAP_HISTORY_ADDR);

    buf[0] = (uint8_t) (NVT_MMAP_HISTORY_ADDR & 0x7F);
    CTP_SPI_READ(chip_info->s_client, buf, 65);	//read 64bytes history

    //print all data
    seq_printf(s, "fw history 0x%x: \n", NVT_MMAP_HISTORY_ADDR);
	for (i = 0; i < 4; i++) {
        snprintf(str, sizeof(str), "%2x %2x %2x %2x %2x %2x %2x %2x    %2x %2x %2x %2x %2x %2x %2x %2x\n",
                 buf[1+i*16], buf[2+i*16], buf[3+i*16], buf[4+i*16],
                 buf[5+i*16], buf[6+i*16], buf[7+i*16], buf[8+i*16],
                 buf[9+i*16], buf[10+i*16], buf[11+i*16], buf[12+i*16],
                 buf[13+i*16], buf[14+i*16], buf[15+i*16], buf[16+i*16]);
        seq_printf(s, "%s", str);
    }
}


static void nvt_main_register_read(struct seq_file *s, void *chip_data)
{
    uint8_t buf[4];
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

    if (!chip_info)
        return ;

    nvt_esd_check_update_timer(chip_info);

    //read fw history
    nvt_read_fw_history(s, chip_data, NVT_MMAP_HISTORY_EVENT0);
    nvt_read_fw_history(s, chip_data, NVT_MMAP_HISTORY_EVENT1);
    nvt_read_fw_history(s, chip_data, NVT_MMAP_HISTORY_EVENT2);
    nvt_read_fw_history(s, chip_data, NVT_MMAP_HISTORY_EVENT3);
    //---set xdata index to EVENT BUF ADDR---
    nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

    //---read cmd status---
    buf[0] = 0x5E;
    buf[1] = 0xFF;
    buf[2] = 0xFF;
    CTP_SPI_READ(chip_info->s_client, buf, 3);

    seq_printf(s, "PWR_FLAG:%d\n", (buf[1] >> PWR_FLAG) & 0x01);
    seq_printf(s, "EDGE_REJECT:%d\n", (buf[1] >> EDGE_REJECT_L) & 0x03);
    seq_printf(s, "JITTER_FLAG:%d\n", (buf[1] >> JITTER_FLAG) & 0x01);
    seq_printf(s, "HEADSET_FLAG:%d\n", (buf[1] >> HEADSET_FLAG) & 0x01);
    seq_printf(s, "HOPPING_FIX_FREQ_FLAG:%d\n", (buf[1] >> HOPPING_FIX_FREQ_FLAG) & 0x01);
    seq_printf(s, "HOPPING_POLLING_FLAG:%d\n", (buf[1] >> HOPPING_POLLING_FLAG) & 0x01);

    seq_printf(s, "DEBUG_DIFFDATA_FLAG:%d\n", (buf[2] >> DEBUG_DIFFDATA_FLAG) & 0x01);
    seq_printf(s, "DEBUG_WKG_COORD_FLAG:%d\n", (buf[2] >> DEBUG_WKG_COORD_FLAG) & 0x01);
    seq_printf(s, "DEBUG_WKG_COORD_RECORD_FLAG:%d\n", (buf[2] >> DEBUG_WKG_COORD_RECORD_FLAG) & 0x01);
    seq_printf(s, "DEBUG_WATER_POLLING_FLAG:%d\n", (buf[2] >> DEBUG_WATER_POLLING_FLAG) & 0x01);

}

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read    = nvt_limit_read,
    .baseline_read = nvt_baseline_read,
    .delta_read = nvt_delta_read,
    //.dbg_diff_finger_down_read = nvt_dbg_diff_finger_down_read, //TODO: link to oplus common driver
    //.dbg_diff_status_change_read = nvt_dbg_diff_status_change_read, //TODO: link to oplus common driver
    //.dbg_gesture_record_coor_read = nvt_dbg_gesture_record_coor_read, //TODO: link to oplus common driver
    .main_register_read = nvt_main_register_read,
};

static void nvt_enable_short_test(struct chip_data_nt36523 *chip_info)
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

static int32_t nvt_read_fw_short(struct chip_data_nt36523 *chip_info, int32_t *xdata, int32_t xdata_len)
{
    uint32_t raw_pipe_addr = 0;
    uint8_t *rawdata_buf = NULL;
    uint32_t x = 0;
    uint32_t y = 0;
    uint8_t buf[128] = {0};
    int32_t iArrayIndex = 0;

    if (xdata_len / sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
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

static void nvt_enable_open_test(struct chip_data_nt36523 *chip_info)
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

static int32_t nvt_read_fw_open(struct chip_data_nt36523 *chip_info, int32_t *xdata, int32_t xdata_len)
{
    uint32_t raw_pipe_addr = 0;
    uint8_t *rawdata_buf = NULL;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t tx_num = chip_info->hw_res->TX_NUM;
    uint32_t rx_num = chip_info->hw_res->RX_NUM;
    uint8_t buf[128] = {0};

    if (xdata_len / sizeof(int32_t) < chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) {
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
            //xdata[(rx_num-y-1) * tx_num + (tx_num-x-1)] = (int16_t)((rawdata_buf[(y*tx_num + x)*2] + 256 * rawdata_buf[(y*tx_num + x)*2 + 1]));
            xdata[y * tx_num + x] = (int16_t)((rawdata_buf[(y * tx_num + x) * 2] + 256 * rawdata_buf[(y * tx_num + x) * 2 + 1]));
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
    /* test data */
    int32_t *raw_data = NULL;
    int32_t *cc_data = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	int32_t *pen_tip_x_data = NULL;
	int32_t *pen_tip_y_data = NULL;
	int32_t *pen_ring_x_data = NULL;
	int32_t *pen_ring_y_data = NULL;
#endif
    int32_t *noise_p_data = NULL;
    int32_t *noise_n_data = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	int32_t *pen_tip_x_noise_p_data = NULL;
	int32_t *pen_tip_x_noise_n_data = NULL;
	int32_t *pen_tip_y_noise_p_data = NULL;
	int32_t *pen_tip_y_noise_n_data = NULL;
	int32_t *pen_ring_x_noise_p_data = NULL;
	int32_t *pen_ring_x_noise_n_data = NULL;
	int32_t *pen_ring_y_noise_p_data = NULL;
	int32_t *pen_ring_y_noise_n_data = NULL;
#endif
    int32_t *doze_noise_p_data = NULL;
    int32_t *doze_noise_n_data = NULL;
    int32_t *doze_raw_data = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    int32_t *digital_noise_p_data = NULL;
    int32_t *digital_noise_n_data = NULL;
#endif
    int32_t *short_data = NULL;
    int32_t *open_data = NULL;
    /* test result */
    uint8_t rawdata_result = NVT_MP_UNKNOW;
    uint8_t cc_result = NVT_MP_UNKNOW;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	uint8_t pen_tip_x_result = NVT_MP_UNKNOW;
	uint8_t pen_tip_y_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_x_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_y_result = NVT_MP_UNKNOW;
#endif
    uint8_t noise_p_result = NVT_MP_UNKNOW;
    uint8_t noise_n_result = NVT_MP_UNKNOW;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	uint8_t pen_tip_x_noise_p_result = NVT_MP_UNKNOW;
	uint8_t pen_tip_x_noise_n_result = NVT_MP_UNKNOW;
	uint8_t pen_tip_y_noise_p_result = NVT_MP_UNKNOW;
	uint8_t pen_tip_y_noise_n_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_x_noise_p_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_x_noise_n_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_y_noise_p_result = NVT_MP_UNKNOW;
	uint8_t pen_ring_y_noise_n_result = NVT_MP_UNKNOW;
#endif
    uint8_t doze_noise_p_result = NVT_MP_UNKNOW;
    uint8_t doze_noise_n_result = NVT_MP_UNKNOW;
    uint8_t doze_rawdata_result = NVT_MP_UNKNOW;
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    uint8_t digital_noise_p_result = NVT_MP_UNKNOW;
    uint8_t digital_noise_n_result = NVT_MP_UNKNOW;
#endif
    uint8_t short_result = NVT_MP_UNKNOW;
    uint8_t open_result = NVT_MP_UNKNOW;
    /* record data */
    uint8_t *raw_record = NULL;
    uint8_t *cc_record = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	uint8_t *pen_tip_x_record = NULL;
	uint8_t *pen_tip_y_record = NULL;
	uint8_t *pen_ring_x_record = NULL;
	uint8_t *pen_ring_y_record = NULL;
#endif
    uint8_t *noise_p_record = NULL;
    uint8_t *noise_n_record = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	uint8_t *pen_tip_x_noise_p_record = NULL;
	uint8_t *pen_tip_x_noise_n_record = NULL;
	uint8_t *pen_tip_y_noise_p_record = NULL;
	uint8_t *pen_tip_y_noise_n_record = NULL;
	uint8_t *pen_ring_x_noise_p_record = NULL;
	uint8_t *pen_ring_x_noise_n_record = NULL;
	uint8_t *pen_ring_y_noise_p_record = NULL;
	uint8_t *pen_ring_y_noise_n_record = NULL;
#endif
    uint8_t *doze_noise_p_record = NULL;
    uint8_t *doze_noise_n_record = NULL;
    uint8_t *doze_raw_record = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    uint8_t *digital_noise_p_record = NULL;
    uint8_t *digital_noise_n_record = NULL;
#endif
    uint8_t *short_record = NULL;
    uint8_t *open_record = NULL;
    int i, j, buf_len = 0, record_len = 0;
    int err_cnt = 0;
    int32_t iArrayIndex = 0;
    int ret = -1;
    const struct firmware *fw = NULL;
    struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
    int tx_num = chip_info->hw_res->TX_NUM;
    int rx_num = chip_info->hw_res->RX_NUM;
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
	int pen_tx_num = chip_info->hw_res->pen_tx_num;
	int pen_rx_num = chip_info->hw_res->pen_rx_num;
#endif
    struct nvt_test_header *ph = NULL;
    int32_t *fw_rawdata_P = NULL, *fw_rawdata_N = NULL;
    int32_t *open_rawdata_P = NULL, *open_rawdata_N = NULL;
    int32_t *short_rawdata_P = NULL, *short_rawdata_N = NULL;
    int32_t *diff_rawdata_P = NULL, *diff_rawdata_N = NULL;
    int32_t *cc_data_P = NULL, *cc_data_N = NULL;
    int32_t *doze_rawdata_P = NULL, *doze_rawdata_N = NULL;
    int32_t *doze_diff_rawdata_P = NULL, *doze_diff_rawdata_N = NULL;
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    int32_t *digital_diff_rawdata_P = NULL, *digital_diff_rawdata_N = NULL;
#endif
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	int32_t *pen_tip_x_data_P = NULL, *pen_tip_x_data_N = NULL;
	int32_t *pen_tip_y_data_P = NULL, *pen_tip_y_data_N = NULL;
	int32_t *pen_ring_x_data_P = NULL, *pen_ring_x_data_N = NULL;
	int32_t *pen_ring_y_data_P = NULL, *pen_ring_y_data_N = NULL;
	int32_t *pen_tip_x_noise_data_P = NULL, *pen_tip_x_noise_data_N = NULL;
	int32_t *pen_tip_y_noise_data_P = NULL, *pen_tip_y_noise_data_N = NULL;
	int32_t *pen_ring_x_noise_data_P = NULL, *pen_ring_x_noise_data_N = NULL;
	int32_t *pen_ring_y_noise_data_P = NULL, *pen_ring_y_noise_data_N = NULL;
#endif
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    char *p_node = NULL;
    char *fw_name_test = NULL;
    char *postfix = "_TEST.bin";
    uint8_t copy_len = 0;
    mm_segment_t old_fs;
    uint8_t data_buf[128], all_test_result[8];

    nvt_esd_check_enable(chip_info, false);

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
    ret = nvt_fw_update_sub(chip_info, fw, 0);


    //ret = nvt_fw_update(chip_info, fw, 0);
    if(ret > 0) {
        TPD_INFO("fw update failed!\n");
        goto RELEASE_FIRMWARE;
    }
    TPD_INFO("update test firmware successed!\n");
	if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_REK)) {
		TPD_INFO("check fw reset state failed!\n");
		seq_printf(s, "check fw reset state failed!\n");
		goto RELEASE_FIRMWARE;
	}
    if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
        TPD_INFO("switch frequency hopping disable failed!\n");
        seq_printf(s, "switch frequency hopping disable failed!\n");
        goto RELEASE_FIRMWARE;
    }

    if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
        TPD_INFO("check fw reset state failed!\n");
        seq_printf(s, "check fw reset state failed!\n");
        goto RELEASE_FIRMWARE;
    }

    msleep(100);

    //---Enter Test Mode---
    TPD_INFO("enter test mode\n");
    if (nvt_clear_fw_status(chip_info)) {
        TPD_INFO("clear fw status failed!\n");
        seq_printf(s, "clear fw status failed!\n");
        goto RELEASE_FIRMWARE;
    }

    nvt_change_mode(chip_info, MP_MODE_CC);
    if (nvt_check_fw_status(chip_info)) {
        TPD_INFO("check fw status failed!\n");
        seq_printf(s, "check fw status failed!\n");
        goto RELEASE_FIRMWARE;
    }

    if (nvt_get_fw_info_noflash(chip_info)) {
        TPD_INFO("get fw info failed!\n");
        seq_printf(s, "get fw info failed!\n");
        goto RELEASE_FIRMWARE;
    }

    TPD_INFO("malloc raw_data space\n");
    buf_len = tx_num * rx_num * sizeof(int32_t);
    raw_data = kzalloc(buf_len, GFP_KERNEL);
    cc_data = kzalloc(buf_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	pen_tip_x_data = kzalloc(buf_len, GFP_KERNEL);
	pen_tip_y_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_x_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_y_data = kzalloc(buf_len, GFP_KERNEL);
#endif
    noise_p_data = kzalloc(buf_len, GFP_KERNEL);
    noise_n_data = kzalloc(buf_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	pen_tip_x_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
	pen_tip_x_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
	pen_tip_y_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
	pen_tip_y_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_x_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_x_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_y_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
	pen_ring_y_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
#endif
    doze_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
    doze_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
    doze_raw_data = kzalloc(buf_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    digital_noise_p_data = kzalloc(buf_len, GFP_KERNEL);
    digital_noise_n_data = kzalloc(buf_len, GFP_KERNEL);
#endif
    short_data = kzalloc(buf_len, GFP_KERNEL);
    open_data = kzalloc(buf_len, GFP_KERNEL);
    if (!(raw_data && cc_data && noise_p_data && noise_n_data
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	&& pen_tip_x_data && pen_tip_y_data
	&& pen_ring_x_data && pen_ring_y_data
	&& pen_tip_x_noise_p_data && pen_tip_x_noise_n_data
	&& pen_tip_y_noise_p_data && pen_tip_y_noise_n_data
	&& pen_ring_x_noise_p_data && pen_ring_x_noise_n_data
	&& pen_ring_y_noise_p_data && pen_ring_y_noise_n_data
#endif
          && doze_raw_data && doze_noise_p_data && doze_noise_n_data
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
          && digital_noise_p_data && digital_noise_n_data
#endif
          && short_data && open_data)) {
        err_cnt++;
        seq_printf(s, "malloc memory failed!\n");
        if (raw_data)
            kfree(raw_data);
        if (cc_data)
            kfree(cc_data);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_data)
		kfree(pen_tip_x_data);
	if (pen_tip_y_data)
		kfree(pen_tip_y_data);
	if (pen_ring_x_data)
		kfree(pen_ring_x_data);
	if (pen_ring_y_data)
		kfree(pen_ring_y_data);
#endif
        if (noise_p_data)
            kfree(noise_p_data);
        if (noise_n_data)
            kfree(noise_n_data);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_noise_p_data)
		kfree(pen_tip_x_noise_p_data);
	if (pen_tip_x_noise_n_data)
		kfree(pen_tip_x_noise_n_data);
	if (pen_tip_y_noise_p_data)
		kfree(pen_tip_y_noise_p_data);
	if (pen_tip_y_noise_n_data)
		kfree(pen_tip_y_noise_n_data);
	if (pen_ring_x_noise_p_data)
		kfree(pen_ring_x_noise_p_data);
	if (pen_ring_x_noise_n_data)
		kfree(pen_ring_x_noise_n_data);
	if (pen_ring_y_noise_p_data)
		kfree(pen_ring_y_noise_p_data);
	if (pen_ring_y_noise_n_data)
		kfree(pen_ring_y_noise_n_data);
#endif
        if (doze_noise_p_data)
            kfree(doze_noise_p_data);
        if (doze_noise_n_data)
            kfree(doze_noise_n_data);
        if (doze_raw_data)
            kfree(doze_raw_data);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
        if (digital_noise_p_data)
            kfree(digital_noise_p_data);
        if (digital_noise_n_data)
            kfree(digital_noise_n_data);
#endif
        if (short_data)
            kfree(short_data);
        if (open_data)
            kfree(open_data);
        goto RELEASE_FIRMWARE;
    }

    TPD_INFO("malloc raw_record space\n");
    record_len = tx_num * rx_num * sizeof(uint8_t);
    raw_record = kzalloc(record_len, GFP_KERNEL);
    cc_record = kzalloc(record_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	pen_tip_x_record = kzalloc(record_len, GFP_KERNEL);
	pen_tip_y_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_x_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_y_record = kzalloc(record_len, GFP_KERNEL);
#endif
    noise_p_record = kzalloc(record_len, GFP_KERNEL);
    noise_n_record = kzalloc(record_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	pen_tip_x_noise_p_record = kzalloc(record_len, GFP_KERNEL);
	pen_tip_x_noise_n_record = kzalloc(record_len, GFP_KERNEL);
	pen_tip_y_noise_p_record = kzalloc(record_len, GFP_KERNEL);
	pen_tip_y_noise_n_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_x_noise_p_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_x_noise_n_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_y_noise_p_record = kzalloc(record_len, GFP_KERNEL);
	pen_ring_y_noise_n_record = kzalloc(record_len, GFP_KERNEL);
#endif
    doze_noise_p_record = kzalloc(record_len, GFP_KERNEL);
    doze_noise_n_record = kzalloc(record_len, GFP_KERNEL);
    doze_raw_record = kzalloc(record_len, GFP_KERNEL);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    digital_noise_p_record = kzalloc(record_len, GFP_KERNEL);
    digital_noise_n_record = kzalloc(record_len, GFP_KERNEL);
#endif
    short_record = kzalloc(record_len, GFP_KERNEL);
    open_record = kzalloc(record_len, GFP_KERNEL);
    if (!(raw_record && cc_record && noise_p_record && noise_n_record
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	&& pen_tip_x_record && pen_tip_y_record
	&& pen_ring_x_record && pen_ring_y_record
	&& pen_tip_x_noise_p_record && pen_tip_x_noise_n_record
	&& pen_tip_y_noise_p_record && pen_tip_y_noise_n_record
	&& pen_ring_x_noise_p_record && pen_ring_x_noise_n_record
	&& pen_ring_y_noise_p_record && pen_ring_y_noise_n_record
#endif
          && doze_raw_record && doze_noise_p_record && doze_noise_n_record
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
          && digital_noise_p_record && digital_noise_n_record
#endif
          && short_record && open_record)) {
        err_cnt++;
        seq_printf(s, "malloc memory failed!\n");
        if (raw_record)
            kfree(raw_record);
        if (cc_record)
            kfree(cc_record);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_record)
		kfree(pen_tip_x_record);
	if (pen_tip_y_record)
		kfree(pen_tip_y_record);
	if (pen_ring_x_record)
		kfree(pen_ring_x_record);
	if (pen_ring_y_record)
		kfree(pen_ring_y_record);
#endif
        if (noise_p_record)
            kfree(noise_p_record);
        if (noise_n_record)
            kfree(noise_n_record);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_noise_p_record)
		kfree(pen_tip_x_noise_p_record);
	if (pen_tip_x_noise_n_record)
		kfree(pen_tip_x_noise_n_record);
	if (pen_tip_y_noise_p_record)
		kfree(pen_tip_y_noise_p_record);
	if (pen_tip_y_noise_n_record)
		kfree(pen_tip_y_noise_n_record);
	if (pen_ring_x_noise_p_record)
		kfree(pen_ring_x_noise_p_record);
	if (pen_ring_x_noise_n_record)
		kfree(pen_ring_x_noise_n_record);
	if (pen_ring_y_noise_p_record)
		kfree(pen_ring_y_noise_p_record);
	if (pen_ring_y_noise_n_record)
		kfree(pen_ring_y_noise_n_record);
#endif
        if (doze_noise_p_record)
            kfree(doze_noise_p_record);
        if (doze_noise_n_record)
            kfree(doze_noise_n_record);
        if (doze_raw_record)
            kfree(doze_raw_record);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
        if (digital_noise_p_record)
            kfree(digital_noise_p_record);
        if (digital_noise_n_record)
            kfree(digital_noise_n_record);
#endif
        if (short_record)
            kfree(short_record);
        if (open_record)
            kfree(open_record);
        goto RELEASE_DATA;
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
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    digital_diff_rawdata_P = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_Digital_Diff_P_offset);
    digital_diff_rawdata_N = (int32_t *)(nvt_testdata->fw->data + ph->array_FW_Digital_Diff_N_offset);
#endif
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if(ts->pen_support) {
		pen_tip_x_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_x_data_p_offset);
		pen_tip_x_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_x_data_n_offset);
		pen_tip_y_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_y_data_p_offset);
		pen_tip_y_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_y_data_n_offset);
		pen_ring_x_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_x_data_p_offset);
		pen_ring_x_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_x_data_n_offset);
		pen_ring_y_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_y_data_p_offset);
		pen_ring_y_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_y_data_n_offset);
		pen_tip_x_noise_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_x_diff_p_offset);
		pen_tip_x_noise_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_x_diff_n_offset);
		pen_tip_y_noise_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_y_diff_p_offset);
		pen_tip_y_noise_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_tip_y_diff_n_offset);
		pen_ring_x_noise_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_x_diff_p_offset);
		pen_ring_x_noise_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_x_diff_n_offset);
		pen_ring_y_noise_data_P = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_y_diff_p_offset);
		pen_ring_y_noise_data_N = (int32_t *)(nvt_testdata->fw->data + ph->array_pen_ring_y_diff_n_offset);
	} /* ts->pen_support */
#endif

    //---FW Rawdata Test---
    TPD_INFO("FW Rawdata Test \n");
    memset(raw_data, 0, buf_len);
    memset(raw_record, 0, record_len);
    rawdata_result = NVT_MP_PASS;
    nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->BASELINE_ADDR, raw_data, buf_len);
    for (j = 0; j < rx_num; j++) {
        for (i = 0; i < tx_num; i++) {
            iArrayIndex = j * tx_num + i;
            if((raw_data[iArrayIndex] > fw_rawdata_P[iArrayIndex]) \
               || (raw_data[iArrayIndex] < fw_rawdata_N[iArrayIndex])) {
                rawdata_result = NVT_MP_FAIL;
                raw_record[iArrayIndex] = 1;
                TPD_INFO("rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], fw_rawdata_N[iArrayIndex], fw_rawdata_P[iArrayIndex]);
                if (!err_cnt) {
                    seq_printf(s, "rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, raw_data[iArrayIndex], fw_rawdata_N[iArrayIndex], fw_rawdata_P[iArrayIndex]);
                }
                err_cnt++;
            }
        }
    }

    TPD_INFO("FW cc data test \n");
    memset(cc_data, 0, buf_len);
    memset(cc_record, 0, record_len);
    cc_result = NVT_MP_PASS;
    if (nvt_get_fw_pipe(chip_info) == 0)
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, cc_data, buf_len);
    else
        nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, cc_data, buf_len);

        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                if((cc_data[iArrayIndex] > cc_data_P[iArrayIndex]) \
                   || (cc_data[iArrayIndex] < cc_data_N[iArrayIndex])) {
                    cc_result = NVT_MP_FAIL;
                    cc_record[iArrayIndex] = 1;
                    TPD_INFO("cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, cc_data[iArrayIndex], cc_data_N[iArrayIndex], cc_data_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j, cc_data[iArrayIndex], cc_data_N[iArrayIndex], cc_data_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
	}
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		TPD_INFO("Pen Rawdata Test \n");
		memset(pen_tip_x_data, 0, buf_len);
		memset(pen_tip_y_data, 0, buf_len);
		memset(pen_ring_x_data, 0, buf_len);
		memset(pen_ring_y_data, 0, buf_len);
		memset(pen_tip_x_record, 0, record_len);
		memset(pen_tip_y_record, 0, record_len);
		memset(pen_ring_x_record, 0, record_len);
		memset(pen_ring_y_record, 0, record_len);
		pen_tip_x_result = NVT_MP_PASS;
		pen_tip_y_result = NVT_MP_PASS;
		pen_ring_x_result = NVT_MP_PASS;
		pen_ring_y_result = NVT_MP_PASS;

		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_X_ADDR,
				pen_tip_x_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_Y_ADDR,
				pen_tip_y_data, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_X_ADDR,
				pen_ring_x_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_Y_ADDR,
				pen_ring_y_data, pen_tx_num * rx_num);

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_tip_x_data[iArrayIndex] > pen_tip_x_data_P[iArrayIndex]) \
						|| (pen_tip_x_data[iArrayIndex] < pen_tip_x_data_N[iArrayIndex])) {
					pen_tip_x_result = NVT_MP_FAIL;
					pen_tip_x_record[iArrayIndex] = 1;
					TPD_INFO("pen tip x data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_x_data[iArrayIndex],
							pen_tip_x_data_N[iArrayIndex], pen_tip_x_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip x data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_data[iArrayIndex],
								pen_tip_x_data_N[iArrayIndex], pen_tip_x_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_tip_y_data[iArrayIndex] > pen_tip_y_data_P[iArrayIndex]) \
						|| (pen_tip_y_data[iArrayIndex] < pen_tip_y_data_N[iArrayIndex])) {
					pen_tip_y_result = NVT_MP_FAIL;
					pen_tip_y_record[iArrayIndex] = 1;
					TPD_INFO("pen tip y data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_y_data[iArrayIndex],
							pen_tip_y_data_N[iArrayIndex], pen_tip_y_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip y data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_data[iArrayIndex],
								pen_tip_y_data_N[iArrayIndex], pen_tip_y_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_ring_x_data[iArrayIndex] > pen_ring_x_data_P[iArrayIndex]) \
						|| (pen_ring_x_data[iArrayIndex] < pen_ring_x_data_N[iArrayIndex])) {
					pen_ring_x_result = NVT_MP_FAIL;
					pen_ring_x_record[iArrayIndex] = 1;
					TPD_INFO("pen ring x data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_x_data[iArrayIndex],
							pen_ring_x_data_N[iArrayIndex], pen_ring_x_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring x data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_data[iArrayIndex],
								pen_ring_x_data_N[iArrayIndex], pen_ring_x_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_ring_y_data[iArrayIndex] > pen_ring_y_data_P[iArrayIndex]) \
						|| (pen_ring_y_data[iArrayIndex] < pen_ring_y_data_N[iArrayIndex])) {
					pen_ring_y_result = NVT_MP_FAIL;
					pen_ring_y_record[iArrayIndex] = 1;
					TPD_INFO("pen ring y data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_y_data[iArrayIndex],
							pen_ring_y_data_N[iArrayIndex], pen_ring_y_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring y data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_data[iArrayIndex],
								pen_ring_y_data_N[iArrayIndex], pen_ring_y_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}
	} /* ts->pen_support */
#endif

    //---Leave Test Mode---
    nvt_change_mode(chip_info, NORMAL_MODE);

    //---Noise Test---
    TPD_INFO("FW Noise Test \n");
    memset(noise_p_data, 0, buf_len); //store max
    memset(noise_n_data, 0, buf_len); //store min
    memset(noise_p_record, 0, record_len);
    memset(noise_n_record, 0, record_len);
    noise_p_result = NVT_MP_PASS;
    noise_n_result = NVT_MP_PASS;
    if (nvt_read_fw_noise(chip_info, ph->config_Diff_Test_Frame, noise_p_data, noise_n_data, buf_len) != 0) {
        TPD_INFO("read Noise data failed!\n");
        noise_p_result = NVT_MP_FAIL_READ_DATA;
        noise_n_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Noise data failed!\n");
        err_cnt++;
        goto TEST_END;
    }


        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                if((noise_p_data[iArrayIndex] > diff_rawdata_P[iArrayIndex]) \
                   || (noise_p_data[iArrayIndex] < diff_rawdata_N[iArrayIndex])) {
                    noise_p_result = NVT_MP_FAIL;
                    noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, noise_p_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, noise_p_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                if((noise_n_data[iArrayIndex] > diff_rawdata_P[iArrayIndex]) \
                   || (noise_n_data[iArrayIndex] < diff_rawdata_N[iArrayIndex])) {
                    noise_n_result = NVT_MP_FAIL;
                    noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, noise_n_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, noise_n_data[iArrayIndex], diff_rawdata_N[iArrayIndex], diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		memset(pen_tip_x_noise_p_data, 0, buf_len);
		memset(pen_tip_x_noise_n_data, 0, buf_len);
		memset(pen_tip_y_noise_p_data, 0, buf_len);
		memset(pen_tip_y_noise_n_data, 0, buf_len);
		memset(pen_ring_x_noise_p_data, 0, buf_len);
		memset(pen_ring_x_noise_n_data, 0, buf_len);
		memset(pen_ring_y_noise_p_data, 0, buf_len);
		memset(pen_ring_y_noise_n_data, 0, buf_len);
		memset(pen_tip_x_noise_p_record, 0, record_len);
		memset(pen_tip_x_noise_n_record, 0, record_len);
		memset(pen_tip_y_noise_p_record, 0, record_len);
		memset(pen_tip_y_noise_n_record, 0, record_len);
		memset(pen_ring_x_noise_p_record, 0, record_len);
		memset(pen_ring_x_noise_n_record, 0, record_len);
		memset(pen_ring_y_noise_p_record, 0, record_len);
		memset(pen_ring_y_noise_n_record, 0, record_len);
		pen_tip_x_noise_p_result = NVT_MP_PASS;
		pen_tip_x_noise_n_result = NVT_MP_PASS;
		pen_tip_y_noise_p_result = NVT_MP_PASS;
		pen_tip_y_noise_n_result = NVT_MP_PASS;
		pen_ring_x_noise_p_result = NVT_MP_PASS;
		pen_ring_x_noise_n_result = NVT_MP_PASS;
		pen_ring_y_noise_p_result = NVT_MP_PASS;
		pen_ring_y_noise_n_result = NVT_MP_PASS;

		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_X_ADDR,
				pen_tip_x_noise_p_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_X_ADDR,
				pen_tip_x_noise_n_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_Y_ADDR,
				pen_tip_y_noise_p_data, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_Y_ADDR,
				pen_tip_y_noise_n_data, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_X_ADDR,
				pen_ring_x_noise_p_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_X_ADDR,
				pen_ring_x_noise_n_data, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_Y_ADDR,
				pen_ring_y_noise_p_data, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_Y_ADDR,
				pen_ring_y_noise_n_data, pen_tx_num * rx_num);

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_tip_x_noise_p_data[iArrayIndex] > pen_tip_x_noise_data_P[iArrayIndex]) \
						|| (pen_tip_x_noise_p_data[iArrayIndex] < pen_tip_x_noise_data_N[iArrayIndex])) {
					pen_tip_x_noise_p_result = NVT_MP_FAIL;
					pen_tip_x_noise_p_record[iArrayIndex] = 1;
					TPD_INFO("pen tip x noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_x_noise_p_data[iArrayIndex],
							pen_tip_x_noise_data_N[iArrayIndex], pen_tip_x_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip x noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_noise_p_data[iArrayIndex],
								pen_tip_x_noise_data_N[iArrayIndex], pen_tip_x_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_tip_x_noise_n_data[iArrayIndex] > pen_tip_x_noise_data_P[iArrayIndex]) \
						|| (pen_tip_x_noise_n_data[iArrayIndex] < pen_tip_x_noise_data_N[iArrayIndex])) {
					pen_tip_x_noise_n_result = NVT_MP_FAIL;
					pen_tip_x_noise_n_record[iArrayIndex] = 1;
					TPD_INFO("pen tip x noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_x_noise_n_data[iArrayIndex],
							pen_tip_x_noise_data_N[iArrayIndex], pen_tip_x_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip x noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_noise_n_data[iArrayIndex],
								pen_tip_x_noise_data_N[iArrayIndex], pen_tip_x_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_tip_y_noise_p_data[iArrayIndex] > pen_tip_y_noise_data_P[iArrayIndex]) \
						|| (pen_tip_y_noise_p_data[iArrayIndex] < pen_tip_y_noise_data_N[iArrayIndex])) {
					pen_tip_y_noise_p_result = NVT_MP_FAIL;
					pen_tip_y_noise_p_record[iArrayIndex] = 1;
					TPD_INFO("pen tip y noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_y_noise_p_data[iArrayIndex],
							pen_tip_y_noise_data_N[iArrayIndex], pen_tip_y_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip y noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_noise_p_data[iArrayIndex],
								pen_tip_y_noise_data_N[iArrayIndex], pen_tip_y_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_tip_y_noise_n_data[iArrayIndex] > pen_tip_y_noise_data_P[iArrayIndex]) \
						|| (pen_tip_y_noise_n_data[iArrayIndex] < pen_tip_y_noise_data_N[iArrayIndex])) {
					pen_tip_y_noise_n_result = NVT_MP_FAIL;
					pen_tip_y_noise_n_record[iArrayIndex] = 1;
					TPD_INFO("pen tip y noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_tip_y_noise_n_data[iArrayIndex],
							pen_tip_y_noise_data_N[iArrayIndex], pen_tip_y_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen tip y noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_noise_n_data[iArrayIndex],
								pen_tip_y_noise_data_N[iArrayIndex], pen_tip_y_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_ring_x_noise_p_data[iArrayIndex] > pen_ring_x_noise_data_P[iArrayIndex]) \
						|| (pen_ring_x_noise_p_data[iArrayIndex] < pen_ring_x_noise_data_N[iArrayIndex])) {
					pen_ring_x_noise_p_result = NVT_MP_FAIL;
					pen_ring_x_noise_p_record[iArrayIndex] = 1;
					TPD_INFO("pen ring x noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_x_noise_p_data[iArrayIndex],
							pen_ring_x_noise_data_N[iArrayIndex], pen_ring_x_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring x noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_noise_p_data[iArrayIndex],
								pen_ring_x_noise_data_N[iArrayIndex], pen_ring_x_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < pen_rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;
				if((pen_ring_x_noise_n_data[iArrayIndex] > pen_ring_x_noise_data_P[iArrayIndex]) \
						|| (pen_ring_x_noise_n_data[iArrayIndex] < pen_ring_x_noise_data_N[iArrayIndex])) {
					pen_ring_x_noise_n_result = NVT_MP_FAIL;
					pen_ring_x_noise_n_record[iArrayIndex] = 1;
					TPD_INFO("pen ring x noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_x_noise_n_data[iArrayIndex],
							pen_ring_x_noise_data_N[iArrayIndex], pen_ring_x_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring x noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_noise_n_data[iArrayIndex],
								pen_ring_x_noise_data_N[iArrayIndex], pen_ring_x_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_ring_y_noise_p_data[iArrayIndex] > pen_ring_y_noise_data_P[iArrayIndex]) \
						|| (pen_ring_y_noise_p_data[iArrayIndex] < pen_ring_y_noise_data_N[iArrayIndex])) {
					pen_ring_y_noise_p_result = NVT_MP_FAIL;
					pen_ring_y_noise_p_record[iArrayIndex] = 1;
					TPD_INFO("pen ring y noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_y_noise_p_data[iArrayIndex],
							pen_ring_y_noise_data_N[iArrayIndex], pen_ring_y_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring y noise max data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_noise_p_data[iArrayIndex],
								pen_ring_y_noise_data_N[iArrayIndex], pen_ring_y_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < pen_tx_num; i++) {
				iArrayIndex = j * pen_tx_num + i;
				if((pen_ring_y_noise_n_data[iArrayIndex] > pen_ring_y_noise_data_P[iArrayIndex]) \
						|| (pen_ring_y_noise_n_data[iArrayIndex] < pen_ring_y_noise_data_N[iArrayIndex])) {
					pen_ring_y_noise_n_result = NVT_MP_FAIL;
					pen_ring_y_noise_n_record[iArrayIndex] = 1;
					TPD_INFO("pen ring y noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
							i, j, pen_ring_y_noise_n_data[iArrayIndex],
							pen_ring_y_noise_data_N[iArrayIndex], pen_ring_y_noise_data_P[iArrayIndex]);
					if (!err_cnt) {
						seq_printf(s, "pen ring y noise min data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_noise_n_data[iArrayIndex],
								pen_ring_y_noise_data_N[iArrayIndex], pen_ring_y_noise_data_P[iArrayIndex]);
					}
					err_cnt++;
				}
			}
		}
	} /* ts->pen_support */
#endif

	/* ---Leave Test Mode--- */
	nvt_change_mode(chip_info, NORMAL_MODE);
	/* ---Doze Noise Test--- */
    TPD_INFO("Doze FW Noise Test \n");
    memset(doze_noise_p_data, 0, buf_len); //store max
    memset(doze_noise_n_data, 0, buf_len); //store min
    memset(doze_noise_p_record, 0, record_len);
    memset(doze_noise_n_record, 0, record_len);
    doze_noise_p_result = NVT_MP_PASS;
    doze_noise_n_result = NVT_MP_PASS;
    if (nvt_read_doze_fw_noise(chip_info, ph->config_Doze_Noise_Test_Frame, ph->doze_X_Channel, doze_noise_p_data, doze_noise_n_data, buf_len) != 0) {
        TPD_INFO("read Doze Noise data failed!\n");
        doze_noise_p_result = NVT_MP_FAIL_READ_DATA;
        doze_noise_n_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Doze Noise data failed!\n");
        err_cnt++;
        goto TEST_END;
    }


        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->doze_X_Channel; i++) {
                iArrayIndex = j * ph->doze_X_Channel + i;
                if((doze_noise_n_data[iArrayIndex] > doze_diff_rawdata_P[iArrayIndex]) \
                   || (doze_noise_n_data[iArrayIndex] < doze_diff_rawdata_N[iArrayIndex])) {
                    doze_noise_p_result = NVT_MP_FAIL;
                    doze_noise_p_record[iArrayIndex] = 1;
                    TPD_INFO("Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_noise_n_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_noise_n_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->doze_X_Channel; i++) {
                iArrayIndex = j * ph->doze_X_Channel + i;
                if((doze_noise_n_data[iArrayIndex] > doze_diff_rawdata_P[iArrayIndex]) \
                   || (doze_noise_n_data[iArrayIndex] < doze_diff_rawdata_N[iArrayIndex])) {
                    doze_noise_n_result = NVT_MP_FAIL;
                    doze_noise_n_record[iArrayIndex] = 1;
                    TPD_INFO("Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_noise_n_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_noise_n_data[iArrayIndex], doze_diff_rawdata_N[iArrayIndex], doze_diff_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }

    //---Doze FW Rawdata Test---
    TPD_INFO("Doze FW Rawdata Test \n");
    memset(doze_raw_data, 0, buf_len);
    memset(doze_raw_record, 0, record_len);
    doze_rawdata_result = NVT_MP_PASS;
    if(nvt_read_doze_baseline(chip_info, ph->doze_X_Channel, doze_raw_data, buf_len) != 0) {
        TPD_INFO("read Doze FW Rawdata failed!\n");
        doze_rawdata_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Doze FW Rawdata failed!\n");
        err_cnt++;
        goto TEST_END;
    }


        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < ph->doze_X_Channel; i++) {
                iArrayIndex = j * ph->doze_X_Channel + i;
                if((doze_raw_data[iArrayIndex] > doze_rawdata_P[iArrayIndex]) \
                   || (doze_raw_data[iArrayIndex] < doze_rawdata_N[iArrayIndex])) {
                    doze_rawdata_result = NVT_MP_FAIL;
                    doze_raw_record[iArrayIndex] = 1;
                    TPD_INFO("Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_raw_data[iArrayIndex], doze_rawdata_N[iArrayIndex], doze_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, doze_raw_data[iArrayIndex], doze_rawdata_N[iArrayIndex], doze_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
        }
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
	/* ---Digital Noise Test--- */
    TPD_INFO("FW Digital Noise Test \n");
    memset(digital_noise_p_data, 0, buf_len); //store max
    memset(digital_noise_n_data, 0, buf_len); //store min
    memset(digital_noise_p_record, 0, record_len);
    memset(digital_noise_n_record, 0, record_len);
    digital_noise_p_result = NVT_MP_PASS;
    digital_noise_n_result = NVT_MP_PASS;

    nvt_enter_digital_test(chip_info, true);

    if (nvt_read_fw_noise(chip_info, ph->config_Diff_Test_Frame, digital_noise_p_data, digital_noise_n_data, buf_len) != 0) {
        TPD_INFO("read Digital Noise data failed!\n");
        digital_noise_p_result = NVT_MP_FAIL_READ_DATA;
        digital_noise_n_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Digital Noise data failed!\n");
        err_cnt++;
        goto TEST_END;
    }

	/* ---Leave Test Mode--- */
	nvt_change_mode(chip_info, NORMAL_MODE);
    nvt_enter_digital_test(chip_info, false);

    for (j = 0; j < rx_num; j++) {
        for (i = 0; i < tx_num; i++) {
            iArrayIndex = j * tx_num + i;
            if((digital_noise_p_data[iArrayIndex] > digital_diff_rawdata_P[iArrayIndex]) \
               || (digital_noise_p_data[iArrayIndex] < digital_diff_rawdata_N[iArrayIndex])) {
                digital_noise_p_result = NVT_MP_FAIL;
                digital_noise_p_record[iArrayIndex] = 1;
                TPD_INFO("Digital Noise Max Test failed at data[%d][%d] = %d [%d,%d]\n",
                         i, j, digital_noise_p_data[iArrayIndex],
                         digital_diff_rawdata_N[iArrayIndex], digital_diff_rawdata_P[iArrayIndex]);
                if (!err_cnt) {
                    seq_printf(s, "Digital Noise Max Test failed at data[%d][%d] = %d [%d,%d]\n",
                               i, j, digital_noise_p_data[iArrayIndex],
                               digital_diff_rawdata_N[iArrayIndex], digital_diff_rawdata_P[iArrayIndex]);
                }
                err_cnt++;
            }
        }
    }
    for (j = 0; j < rx_num; j++) {
        for (i = 0; i < tx_num; i++) {
            iArrayIndex = j * tx_num + i;
            if((digital_noise_n_data[iArrayIndex] > digital_diff_rawdata_P[iArrayIndex]) \
               || (digital_noise_n_data[iArrayIndex] < digital_diff_rawdata_N[iArrayIndex])) {
                digital_noise_n_result = NVT_MP_FAIL;
                digital_noise_n_record[iArrayIndex] = 1;
                TPD_INFO("Digital Noise Min Test failed at data[%d][%d] = %d [%d,%d]\n",
                         i, j, digital_noise_n_data[iArrayIndex],
                         digital_diff_rawdata_N[iArrayIndex], digital_diff_rawdata_P[iArrayIndex]);
                if (!err_cnt) {
                    seq_printf(s, "Digital Noise Min Test failed at data[%d][%d] = %d [%d,%d]\n",
                               i, j, digital_noise_n_data[iArrayIndex],
                               digital_diff_rawdata_N[iArrayIndex], digital_diff_rawdata_P[iArrayIndex]);
                }
                err_cnt++;
            }
        }
    }
#endif

    //--Short Test---
    TPD_INFO("FW Short Test \n");
    memset(short_data, 0, buf_len);
    memset(short_record, 0, record_len);
    short_result = NVT_MP_PASS;
    if (nvt_read_fw_short(chip_info, short_data, buf_len) != 0) {
        TPD_INFO("read Short test data failed!\n");
        short_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Short test data failed!\n");
        err_cnt++;
        goto TEST_END;
    }

        for (j = 0; j < rx_num; j++) {
            for (i = 0; i < tx_num; i++) {
                iArrayIndex = j * tx_num + i;
                if((short_data[iArrayIndex] > short_rawdata_P[iArrayIndex]) \
                   || (short_data[iArrayIndex] < short_rawdata_N[iArrayIndex])) {
                    short_result = NVT_MP_FAIL;
                    short_record[iArrayIndex] = 1;
                    TPD_INFO("Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, short_data[iArrayIndex], short_rawdata_N[iArrayIndex], short_rawdata_P[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, short_data[iArrayIndex], short_rawdata_N[iArrayIndex], short_rawdata_P[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
	}

    //---Open Test---
    TPD_INFO("FW Open Test \n");
    memset(open_data, 0, buf_len);
    memset(open_record, 0, record_len);
    open_result = NVT_MP_PASS;
    if (nvt_read_fw_open(chip_info, open_data, buf_len) != 0) {
        TPD_INFO("read Open test data failed!\n");
        open_result = NVT_MP_FAIL_READ_DATA;
        seq_printf(s, "read Open test data failed!\n");
        err_cnt++;
        goto TEST_END;
    }
    for (j = 0; j < rx_num; j++) {
        for (i = 0; i < tx_num; i++) {
            iArrayIndex = j * tx_num + i;
            if((open_data[iArrayIndex] > open_rawdata_P[iArrayIndex]) \
               || (open_data[iArrayIndex] < open_rawdata_N[iArrayIndex])) {
                open_result = NVT_MP_FAIL;
                open_record[iArrayIndex] = 1;
                TPD_INFO("Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, open_data[iArrayIndex], open_rawdata_N[iArrayIndex], open_rawdata_P[iArrayIndex]);
                if (!err_cnt) {
                    seq_printf(s, "Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j, open_data[iArrayIndex], open_rawdata_N[iArrayIndex], open_rawdata_P[iArrayIndex]);
                }
                err_cnt++;
            }
        }
    }

TEST_END:
    /* Open file to store test result */
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    if (!err_cnt) {
        snprintf(all_test_result, 8, "PASS");
        snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOn/OK/tp_testlimit_%s_%02d%02d%02d-%02d%02d%02d-utc.csv",
                 (char *)all_test_result,
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    } else {
        snprintf(all_test_result, 8, "FAIL");
        snprintf(data_buf, 128, "/sdcard/TpTestReport/screenOn/NG/tp_testlimit_%s_%02d%02d%02d-%02d%02d%02d-utc.csv",
                 (char *)all_test_result,
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_mkdir("/sdcard/TpTestReport", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    nvt_testdata->fd = ksys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
    sys_mkdir("/sdcard/TpTestReport", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    nvt_testdata->fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    if (nvt_testdata->fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", data_buf);
        //seq_printf(s, "Open log file '%s' failed.\n", data_buf);
        //err_cnt++;
        goto OUT;
    }

    /* Project Name */
    store_to_file(nvt_testdata->fd, "OPLUS_%d\n", 19131);
    /* Project ID */
    store_to_file(nvt_testdata->fd, "NVTPID= %04X\n", chip_info->nvt_pid);
    /* FW Version */
    store_to_file(nvt_testdata->fd, "FW Version= V0x%02X\n", chip_info->fw_ver);
    /* Panel Info */
    store_to_file(nvt_testdata->fd, "Panel Info= %d x %d\n", tx_num, rx_num);
    /* Test Stage */
    store_to_file(nvt_testdata->fd, "Test Platform= Mobile\n");

	store_to_file(nvt_testdata->fd, "This is %s module\n", chip_info->ts->panel_data.manufacture_info.manufacture);

    /* (Pass/Fail) */
    store_to_file(nvt_testdata->fd, "Test Results= %s\n", all_test_result);

    /* Test Item */
    /* 1. rawdata        */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "rawData", rawdata_result, raw_record, tx_num, rx_num);
	/* 2. cc             */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "ccData", cc_result, cc_record, tx_num, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		/* 3. pen tip x rawdata    */
		nvt_store_testrecord_to_file(nvt_testdata->fd , "pen tip x rawData" , pen_tip_x_result , pen_tip_x_record , tx_num , pen_rx_num);

		/* 4. pen tip y rawdata    */
		nvt_store_testrecord_to_file(nvt_testdata->fd , "pen tip y rawData" , pen_tip_y_result , pen_tip_y_record , pen_tx_num , rx_num);

		/* 5. pen ring x rawdata    */
		nvt_store_testrecord_to_file(nvt_testdata->fd , "pen ring x rawData" , pen_ring_x_result , pen_ring_x_record , tx_num , pen_rx_num);

		/* 6. pen ring y rawdata    */
		nvt_store_testrecord_to_file(nvt_testdata->fd , "pen ring y rawData" , pen_ring_y_result , pen_ring_y_record , pen_tx_num , rx_num);
	} /* ts->pen_support */
#endif
	/* 7. noise max      */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "RawData_Diff_Max", noise_p_result, noise_p_record, tx_num, rx_num);
	/* 8. noise min      */
	nvt_store_testrecord_to_file(nvt_testdata->fd , "RawData_Diff_Min" , noise_n_result , noise_n_record , tx_num, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		/* 9. pen tip x noise max    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen tip x noise max", pen_tip_x_noise_p_result, pen_tip_x_noise_p_record, tx_num, pen_rx_num);
		/*10. pen tip x noise min    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen tip x noise min", pen_tip_x_noise_n_result, pen_tip_x_noise_n_record, tx_num, pen_rx_num);

		/*11. pen tip y noise max    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen tip y noise max", pen_tip_y_noise_p_result, pen_tip_y_noise_p_record, pen_tx_num, rx_num);
		/*12. pen tip y noise min    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen tip y noise min", pen_tip_y_noise_n_result, pen_tip_y_noise_n_record, pen_tx_num, rx_num);

		/*13. pen ring x noise max    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen ring x noise max", pen_ring_x_noise_p_result, pen_ring_x_noise_p_record, tx_num, pen_rx_num);
		/*14. pen ring x noise min    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen ring x noise min", pen_ring_x_noise_n_result, pen_ring_x_noise_n_record, tx_num, pen_rx_num);

		/*15. pen ring y noise max    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen ring y noise max", pen_ring_y_noise_p_result, pen_ring_y_noise_p_record, pen_tx_num, rx_num);
		/*16. pen ring y noise min    */
		nvt_store_testrecord_to_file(nvt_testdata->fd, "pen ring y noise min", pen_ring_y_noise_p_result, pen_ring_y_noise_p_record, pen_tx_num, rx_num);
	} /* ts->pen_support */
#endif
	/* 17. doze noise max */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "Doze RawData_Diff_Max", doze_noise_p_result, doze_noise_p_record, ph->doze_X_Channel, rx_num);
	/* 18. doze noise min */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "Doze RawData_Diff_Min", doze_noise_n_result, doze_noise_n_record, ph->doze_X_Channel, rx_num);
	/* 19. doze rawdata   */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "Doze FW Rawdata", doze_rawdata_result, doze_raw_record, ph->doze_X_Channel, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
	/* 20. noise max      */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "Digital_RawData_Diff_Max", digital_noise_p_result, digital_noise_p_record, tx_num, rx_num);
	/* 21. noise min      */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "Digital_RawData_Diff_Min", digital_noise_n_result, digital_noise_n_record, tx_num, rx_num);
#endif
	/*22. short          */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "RawData_Short", short_result, short_record, tx_num, rx_num);
	/*23. open           */
	nvt_store_testrecord_to_file(nvt_testdata->fd, "RawData_Open", open_result, open_record, tx_num, rx_num);
	/* Test data */
	/* 1. rawdata        */
	nvt_store_testdata_to_file(nvt_testdata->fd, "rawData", rawdata_result, raw_data, tx_num, rx_num);
	/* 2. cc             */
	nvt_store_testdata_to_file(nvt_testdata->fd, "ccData", cc_result, cc_data, tx_num, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		/* 3. pen tip x rawdata    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip x rawData", pen_tip_x_result, pen_tip_x_data, tx_num, pen_rx_num);

		/* 4. pen tip y rawdata    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip y rawData", pen_tip_y_result, pen_tip_y_data, pen_tx_num, rx_num);

		/* 5. pen ring x rawdata    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen ring x rawData", pen_ring_x_result, pen_ring_x_data, tx_num, pen_rx_num);

		/* 6. pen ring y rawdata    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen ring y rawData", pen_ring_y_result, pen_ring_y_data, pen_tx_num, rx_num);
	} /* ts->pen_support */
#endif
	/* 7. noise max      */
    nvt_store_testdata_to_file(nvt_testdata->fd, "RawData_Diff_Max",
                               noise_p_result, noise_p_data, tx_num, rx_num);
    /* 4. noise min      */
    nvt_store_testdata_to_file(nvt_testdata->fd, "RawData_Diff_Min",
                               noise_n_result, noise_n_data, tx_num, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (ts->pen_support) {
		/* 9. pen tip x noise max    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip x noise max", pen_tip_x_noise_p_result, pen_tip_x_noise_p_data, tx_num, pen_rx_num);
		/*10. pen tip x noise min    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip x noise min", pen_tip_x_noise_n_result, pen_tip_x_noise_n_data, tx_num, pen_rx_num);

		/*11. pen tip y noise max    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip y noise max", pen_tip_y_noise_p_result, pen_tip_y_noise_p_data, pen_tx_num, rx_num);
		/*12. pen tip y noise min    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen tip y noise min", pen_tip_y_noise_n_result, pen_tip_y_noise_n_data, pen_tx_num, rx_num);

		/*13. pen ring x noise max    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen ring x noise max", pen_ring_x_noise_p_result, pen_ring_x_noise_p_data, tx_num, pen_rx_num);
		/*14. pen ring x noise min    */
		nvt_store_testdata_to_file(nvt_testdata->fd, "pen ring x noise min", pen_ring_x_noise_n_result, pen_ring_x_noise_n_data, tx_num, pen_rx_num);

		/*15. pen ring y noise max    */
		nvt_store_testdata_to_file(nvt_testdata->fd , "pen ring y noise max" , pen_ring_y_noise_p_result , pen_ring_y_noise_p_data, pen_tx_num, rx_num);
		/*16. pen ring y noise min    */
		nvt_store_testdata_to_file(nvt_testdata->fd , "pen ring y noise min" , pen_ring_y_noise_p_result , pen_ring_y_noise_p_data , pen_tx_num , rx_num);
	} /* ts->pen_support */
#endif
	/* 17. doze noise max */
	nvt_store_testdata_to_file(nvt_testdata->fd, "Doze RawData_Diff_Max", doze_noise_p_result, doze_noise_p_data, ph->doze_X_Channel, rx_num);
	/* 18. doze noise min */
	nvt_store_testdata_to_file(nvt_testdata->fd, "Doze RawData_Diff_Min", doze_noise_n_result, doze_noise_n_data, ph->doze_X_Channel, rx_num);
	/* 19. doze rawdata   */
	nvt_store_testdata_to_file(nvt_testdata->fd, "Doze FW Rawdata", doze_rawdata_result, doze_raw_data, ph->doze_X_Channel, rx_num);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
	/* 20. noise max      */
	nvt_store_testdata_to_file(nvt_testdata->fd, "Digital RawData_Diff_Max", digital_noise_p_result, digital_noise_p_data, tx_num, rx_num);
	/* 21. noise min      */
	nvt_store_testdata_to_file(nvt_testdata->fd, "Digital RawData_Diff_Min", digital_noise_n_result, digital_noise_n_data, tx_num, rx_num);
#endif
	/*22. short          */
	nvt_store_testdata_to_file(nvt_testdata->fd , "RawData_Short" , short_result , short_data , tx_num , rx_num);
	/*23. open           */
	nvt_store_testdata_to_file(nvt_testdata->fd , "RawData_Open" , open_result , open_data, tx_num , rx_num);

OUT:
    if (nvt_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_close(nvt_testdata->fd);
#else
        sys_close(nvt_testdata->fd);
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    }
    set_fs(old_fs);

    //RELEASE_RRECORD_DATA:
    if (raw_record)
        kfree(raw_record);
    if (cc_record)
        kfree(cc_record);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_record)
		kfree(pen_tip_x_record);
	if (pen_tip_y_record)
		kfree(pen_tip_y_record);
	if (pen_ring_x_record)
		kfree(pen_ring_x_record);
	if (pen_ring_y_record)
		kfree(pen_ring_y_record);
#endif
    if (noise_p_record)
        kfree(noise_p_record);
    if (noise_n_record)
        kfree(noise_n_record);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_noise_p_record)
		kfree(pen_tip_x_noise_p_record);
	if (pen_tip_x_noise_n_record)
		kfree(pen_tip_x_noise_n_record);
	if (pen_tip_y_noise_p_record)
		kfree(pen_tip_y_noise_p_record);
	if (pen_tip_y_noise_n_record)
		kfree(pen_tip_y_noise_n_record);
	if (pen_ring_x_noise_p_record)
		kfree(pen_ring_x_noise_p_record);
	if (pen_ring_x_noise_n_record)
		kfree(pen_ring_x_noise_n_record);
	if (pen_ring_y_noise_p_record)
		kfree(pen_ring_y_noise_p_record);
	if (pen_ring_y_noise_n_record)
		kfree(pen_ring_y_noise_n_record);
#endif
    if (doze_noise_p_record)
        kfree(doze_noise_p_record);
    if (doze_noise_n_record)
        kfree(doze_noise_n_record);
    if (doze_raw_record)
        kfree(doze_raw_record);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    if (digital_noise_p_record)
        kfree(digital_noise_p_record);
    if (digital_noise_n_record)
        kfree(digital_noise_n_record);
#endif
    if (short_record)
        kfree(short_record);
    if (open_record)
        kfree(open_record);

RELEASE_DATA:
    if (raw_data)
        kfree(raw_data);
    if (cc_data)
        kfree(cc_data);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_data)
		kfree(pen_tip_x_data);
	if (pen_tip_y_data)
		kfree(pen_tip_y_data);
	if (pen_ring_x_data)
		kfree(pen_ring_x_data);
	if (pen_ring_y_data)
		kfree(pen_ring_y_data);
#endif
    if (noise_p_data)
        kfree(noise_p_data);
    if (noise_n_data)
        kfree(noise_n_data);
#ifdef CONFIG_TOUCHPANEL_NT_PEN_SUPPORT
	if (pen_tip_x_noise_p_data)
		kfree(pen_tip_x_noise_p_data);
	if (pen_tip_x_noise_n_data)
		kfree(pen_tip_x_noise_n_data);
	if (pen_tip_y_noise_p_data)
		kfree(pen_tip_y_noise_p_data);
	if (pen_tip_y_noise_n_data)
		kfree(pen_tip_y_noise_n_data);
	if (pen_ring_x_noise_p_data)
		kfree(pen_ring_x_noise_p_data);
	if (pen_ring_x_noise_n_data)
		kfree(pen_ring_x_noise_n_data);
	if (pen_ring_y_noise_p_data)
		kfree(pen_ring_y_noise_p_data);
	if (pen_ring_y_noise_n_data)
		kfree(pen_ring_y_noise_n_data);
#endif
    if (doze_noise_p_data)
        kfree(doze_noise_p_data);
    if (doze_noise_n_data)
        kfree(doze_noise_n_data);
    if (doze_raw_data)
        kfree(doze_raw_data);
#ifdef CONFIG_TOUCHPANEL_NT_DIGITALNOISE_TEST
    if (digital_noise_p_data)
        kfree(digital_noise_p_data);
    if (digital_noise_n_data)
        kfree(digital_noise_n_data);
#endif
    if (short_data)
        kfree(short_data);
    if (open_data)
        kfree(open_data);

RELEASE_FIRMWARE:
    seq_printf(s, "FW:0x%02x\n", chip_info->fw_ver);
    seq_printf(s, "%d error(s). %s\n", err_cnt, err_cnt ? "" : "All test passed.");
    TPD_INFO(" TP auto test %d error(s). %s\n", err_cnt, err_cnt ? "" : "All test passed.");
    release_firmware(fw);
    kfree(fw_name_test);
    return;
}

static struct nvt_proc_operations nvt_proc_ops = {
    .auto_test     = nvt_auto_test,
};

#ifdef CONFIG_OPLUS_TP_APK

static void nova_apk_game_set(void *chip_data, bool on_off)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    nvt_mode_switch(chip_data, MODE_GAME, on_off);
    chip_info->lock_point_status = on_off;
}

static bool nova_apk_game_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    return chip_info->lock_point_status;
}

static void nova_apk_debug_set(void *chip_data, bool on_off)
{
    //u8 cmd[1];
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    nvt_enable_debug_msg_diff_mode(chip_info, on_off);
    chip_info->debug_mode_sta = on_off;
}

static bool nova_apk_debug_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    return chip_info->debug_mode_sta;
}

static void nova_apk_gesture_debug(void *chip_data, bool on_off)
{

    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    //get_gesture_fail_reason(on_off);
    chip_info->debug_gesture_sta = on_off;
}

static bool  nova_apk_gesture_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    return chip_info->debug_gesture_sta;
}

static int  nova_apk_gesture_info(void *chip_data, char *buf, int len)
{
    int ret = 0;
    int i;
    int num;
    u8 temp;
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    if(len < 2) {
        return 0;
    }
    buf[0] = 255;

    temp = chip_info->ts->gesture_buf[0];

	switch (temp) {   /* judge gesture type */
	case RIGHT_SLIDE_DETECT :
        buf[0]  = Left2RightSwip;
        break;

	case LEFT_SLIDE_DETECT :
        buf[0]  = Right2LeftSwip;
        break;

	case DOWN_SLIDE_DETECT  :
        buf[0]  = Up2DownSwip;
        break;

	case UP_SLIDE_DETECT :
        buf[0]  = Down2UpSwip;
        break;

	case DTAP_DETECT:
        buf[0]  = DouTap;
        break;

	case UP_VEE_DETECT :
        buf[0]  = UpVee;
        break;

	case DOWN_VEE_DETECT :
        buf[0]  = DownVee;
        break;

	case LEFT_VEE_DETECT:
        buf[0] = LeftVee;
        break;

	case RIGHT_VEE_DETECT :
        buf[0]  = RightVee;
        break;

	case CIRCLE_DETECT  :
        buf[0] = Circle;
        break;

	case DOUSWIP_DETECT  :
        buf[0]  = DouSwip;
        break;

	case M_DETECT  :
        buf[0]  = Mgestrue;
        break;

	case W_DETECT :
        buf[0]  = Wgestrue;
        break;

	case PEN_DETECT:
        buf[0] = PENDETECT;
        break;
    	default:
        buf[0] = temp | 0x80;
        break;
    }

    //buf[0] = gesture_buf[0];
    num = chip_info->ts->gesture_buf[1];

    if(num > 208) {
        num = 208;
    }
    ret = 2;

    buf[1] = num;
    //print all data
    for (i = 0; i < num; i++) {
        int x;
        int y;
        x = chip_info->ts->gesture_buf[i * 3 + 2] << 4;
        x = x + (chip_info->ts->gesture_buf[i * 3 + 4] >> 4);
        //x = x * (chip_info->resolution_x) ;

        y = chip_info->ts->gesture_buf[i * 3 + 3] << 4;
        y = y + (chip_info->ts->gesture_buf[i * 3 + 4] & 0x0F);
        //y = y * (chip_info->resolution_y) / ( 2);

        //TPD_INFO("nova_apk_gesture_info:gesture x is %d,y is %d.\n", x, y);

        if (len < i * 4 + 2) {
            break;
        }
        buf[i * 4 + 2] = x & 0xFF;
        buf[i * 4 + 3] = (x >> 8) & 0xFF;
        buf[i * 4 + 4] = y & 0xFF;
        buf[i * 4 + 5] = (y >> 8) & 0xFF;
        ret += 4;

    }

    return ret;
}


static void nova_apk_earphone_set(void *chip_data, bool on_off)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    nvt_mode_switch(chip_data, MODE_HEADSET, on_off);
    chip_info->earphone_sta = on_off;
}

static bool nova_apk_earphone_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    return chip_info->earphone_sta;
}

static void nova_apk_charger_set(void *chip_data, bool on_off)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    nvt_mode_switch(chip_data, MODE_CHARGE, on_off);
    chip_info->plug_status = on_off;


}

static bool nova_apk_charger_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    return chip_info->plug_status;

}

static void nova_apk_noise_set(void *chip_data, bool on_off)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    //ilitek_mode_switch(chip_data, MODE_CHARGE, on_off);
    nvt_enable_hopping_polling_mode(chip_info, on_off);

    chip_info->noise_sta = on_off;

}

static bool nova_apk_noise_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    return chip_info->noise_sta;

}

static void nova_apk_water_set(void *chip_data, int type)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    //ilitek_mode_switch(chip_data, MODE_CHARGE, on_off);
    if (type > 0) {
        nvt_enable_water_polling_mode(chip_info, true);
    } else {
        nvt_enable_water_polling_mode(chip_info, false);
    }

    chip_info->water_sta = type;

}

static int nova_apk_water_get(void *chip_data)
{
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;

    return chip_info->water_sta;

}

static int  nova_apk_tp_info_get(void *chip_data, char *buf, int len)
{
    int ret;
    struct chip_data_nt36523 *chip_info;
    chip_info = (struct chip_data_nt36523 *)chip_data;
    ret = snprintf(buf, len, "IC:NOVA%04X\nFW_VER:0x%02X\nCH:%dX%d\n",
                   0x672C,
                   chip_info->fw_ver,
                   chip_info->hw_res->TX_NUM,
                   chip_info->hw_res->RX_NUM);
    if (ret > len) {
        ret = len;
    }

    return ret;
}

static void nova_init_oplus_apk_op(struct touchpanel_data *ts)
{
    ts->apk_op = kzalloc(sizeof(APK_OPERATION), GFP_KERNEL);
    if(ts->apk_op) {
        ts->apk_op->apk_game_set = nova_apk_game_set;
        ts->apk_op->apk_game_get = nova_apk_game_get;
        ts->apk_op->apk_debug_set = nova_apk_debug_set;
        ts->apk_op->apk_debug_get = nova_apk_debug_get;
        //apk_op->apk_proximity_set = ili_apk_proximity_set;
        //apk_op->apk_proximity_dis = ili_apk_proximity_dis;
        ts->apk_op->apk_noise_set = nova_apk_noise_set;
        ts->apk_op->apk_noise_get = nova_apk_noise_get;
        ts->apk_op->apk_gesture_debug = nova_apk_gesture_debug;
        ts->apk_op->apk_gesture_get = nova_apk_gesture_get;
        ts->apk_op->apk_gesture_info = nova_apk_gesture_info;
        ts->apk_op->apk_earphone_set = nova_apk_earphone_set;
        ts->apk_op->apk_earphone_get = nova_apk_earphone_get;
        ts->apk_op->apk_charger_set = nova_apk_charger_set;
        ts->apk_op->apk_charger_get = nova_apk_charger_get;
        ts->apk_op->apk_tp_info_get = nova_apk_tp_info_get;
        ts->apk_op->apk_water_set = nova_apk_water_set;
        ts->apk_op->apk_water_get = nova_apk_water_get;
        //apk_op->apk_data_type_set = ili_apk_data_type_set;
        //apk_op->apk_rawdata_get = ili_apk_rawdata_get;
        //apk_op->apk_diffdata_get = ili_apk_diffdata_get;
        //apk_op->apk_basedata_get = ili_apk_basedata_get;
        //ts->apk_op->apk_backdata_get = nova_apk_backdata_get;
        //apk_op->apk_debug_info = ili_apk_debug_info;

    } else {
        TPD_INFO("Can not kzalloc apk op.\n");
    }
}
#endif // end of CONFIG_OPLUS_TP_APK


/*********** Start of SPI Driver and Implementation of it's callbacks*************************/
static int nvt_tp_probe(struct spi_device *client)
{
    struct chip_data_nt36523 *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    /* 1. alloc chip info */
    chip_info = kzalloc(sizeof(struct chip_data_nt36523), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(chip_info, 0, sizeof(*chip_info));
    g_chip_info = chip_info;
    chip_info->probe_done = 0;
    /* 2. Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }
    memset(ts, 0, sizeof(*ts));

    chip_info->g_fw_buf = vmalloc(FW_BUF_SIZE);

    if (chip_info->g_fw_buf == NULL) {
        TPD_INFO("fw buf vmalloc error\n");
        //ret = -ENOMEM;
        goto err_g_fw_buf;
    }
    chip_info->g_fw_sta = false;

    chip_info->fw_buf_dma = kzalloc(FW_BUF_SIZE, GFP_KERNEL | GFP_DMA);
    if (chip_info->fw_buf_dma == NULL) {
        TPD_INFO("fw kzalloc error\n");
        //ret = -ENOMEM;
        goto err_fw_dma;
    }

    /* 3. bind client and dev for easy operate */
    chip_info->s_client = client;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->s_client = client;
    ts->irq = client->irq;
    spi_set_drvdata(client, ts);
    ts->dev = &client->dev;
    ts->chip_data = chip_info;
    chip_info->irq_num = ts->irq;
    chip_info->hw_res = &ts->hw_res;
    chip_info->ENG_RST_ADDR = 0x7FFF80;
    chip_info->recovery_cnt = 0;
    chip_info->partition = 0;
    chip_info->ilm_dlm_num = 2;
    chip_info->cascade_2nd_header_info = 0;
	chip_info->p_firmware_headfile = &ts->panel_data.firmware_headfile;
    chip_info->touch_direction = VERTICAL_SCREEN;
    mutex_init(&chip_info->mutex_testing);
    chip_info->using_headfile = false;
    chip_info->is_pen_attracted = &ts->is_pen_attracted;
    chip_info->is_pen_connected = &ts->is_pen_connected;

    //---prepare for spi parameter---
    if (ts->s_client->master->flags & SPI_MASTER_HALF_DUPLEX) {
        TPD_INFO("Full duplex not supported by master\n");
        ret = -EIO;
        goto err_spi_setup;
    }
    ts->s_client->bits_per_word = 8;
    ts->s_client->mode = SPI_MODE_0;
    ts->s_client->chip_select = 0; //modify reg=0 for more tp vendor share same spi interface

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
    /* new usage of MTK spi API */
    memcpy(&chip_info->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
    ts->s_client->controller_data = (void *)&chip_info->spi_ctrl;
#else
    /* old usage of MTK spi API */
    //memcpy(&chip_info->spi_ctrl, &spi_ctrdata, sizeof(struct mt_chip_conf));
    //ts->s_client->controller_data = (void *)&chip_info->spi_ctrl;

    ret = spi_setup(ts->s_client);
    if (ret < 0) {
        TPD_INFO("Failed to perform SPI setup\n");
        goto err_spi_setup;
    }
#endif	//CONFIG_SPI_MT65XX
#else // else of CONFIG_TOUCHPANEL_MTK_PLATFORM
    ret = spi_setup(ts->s_client);
    if (ret < 0) {
        TPD_INFO("Failed to perform SPI setup\n");
        goto err_spi_setup;
    }

#endif // end of CONFIG_TOUCHPANEL_MTK_PLATFORM

    TPD_INFO("mode=%d, max_speed_hz=%d\n", ts->s_client->mode, ts->s_client->max_speed_hz);

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

    //reset esd handle time interval
    if (ts->esd_handle_support) {
        chip_info->esd_check_enabled = false;
        ts->esd_info.esd_work_time = msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD); // change esd check interval to 1.5s
        TPD_INFO("%s:change esd handle time to %d s\n", __func__, ts->esd_info.esd_work_time / HZ);
    }

    /*6. create nvt test files*/
    nvt_flash_proc_init(ts, "NVTSPI");
    nvt_create_proc(ts, &nvt_proc_ops);

    chip_info->ts = ts;
#ifdef CONFIG_OPLUS_TP_APK
    nova_init_oplus_apk_op(ts);
#endif // end of CONFIG_OPLUS_TP_APK

    //if (chip_info->cs_gpio_need_pull) {
        //nvt_cs_gpio_control(chip_info,true);
       // TPD_INFO("cs_gpio need pull up \n");
    //}

#if NVT_PM_WAIT_I2C_SPI_RESUME_COMPLETE
	chip_info->dev_pm_suspend = false;
	init_completion(&chip_info->dev_pm_resume_completion);
#endif /* NVT_PM_WAIT_I2C_SPI_RESUME_COMPLETE */

    // update fw in probe
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (ts->boot_mode == RECOVERY_BOOT || is_oem_unlocked() || ts->fw_update_in_probe_with_headfile)
#else
    if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY || is_oem_unlocked() || ts->fw_update_in_probe_with_headfile)
#endif
    {
        TPD_INFO("In Recovery mode, no-flash download fw by headfile\n");
        ret = nvt_fw_update(chip_info, NULL, 0);
        if(ret > 0) {
            TPD_INFO("fw update failed!\n");
        }
    }

    chip_info->probe_done = 1;
    TPD_INFO("%s, probe normal end\n", __func__);
    return 0;

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

err_spi_setup:
    if (chip_info->fw_buf_dma) {
        kfree(chip_info->fw_buf_dma);
    }

    spi_set_drvdata(client, NULL);
err_fw_dma:
    if (chip_info->g_fw_buf) {
        vfree(chip_info->g_fw_buf);
    }
err_g_fw_buf:
    if (ts) {
        kfree(ts);
    }

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

    TPD_INFO("%s: is called\n", __func__);

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
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { "oplus,tp_noflash", 0 },
#else
    {TPD_DEVICE, 0},
#endif
    {},
};

static struct of_device_id tp_match_table[] =
{
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { .compatible = "oplus,tp_noflash",},
#else
    { .compatible = TPD_DEVICE},
#endif
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
    //.id_table   = tp_id,
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
	get_oem_verified_boot_state();

	TPD_INFO("%s is called 1 \n", __func__);
    if (spi_register_driver(&tp_spi_driver)!= 0) {
        TPD_INFO("unable to add spi driver.\n");
        return -1;
    }
	TPD_INFO("%s is called 2 \n", __func__);
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
