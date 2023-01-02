// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek.h"

#define UPDATE_PASS            0
#define UPDATE_FAIL            -1


static struct touch_fw_data {
    u8 block_number;
    u32 start_addr;
    u32 end_addr;
    u32 new_fw_cb;
    int hex_tag;
} tfd;

static struct flash_block_info {
    char *name;
    u32 start;
    u32 end;
    u32 len;
    u32 mem_start;
    u32 fix_mem_start;
    u8 mode;
} fbi[FW_BLOCK_INFO_NUM + 1];

static u8 gestrue_fw[(10 * K)];

static u32 HexToDec(char *phex, s32 len)
{
    u32 ret = 0, temp = 0, i;
    s32 shift = (len - 1) * 4;

    for (i = 0; i < len; shift -= 4, i++) {
        if ((phex[i] >= '0') && (phex[i] <= '9'))
            temp = phex[i] - '0';
        else if ((phex[i] >= 'a') && (phex[i] <= 'f'))
            temp = (phex[i] - 'a') + 10;
        else if ((phex[i] >= 'A') && (phex[i] <= 'F'))
            temp = (phex[i] - 'A') + 10;
        else
            return -1;

        ret |= (temp << shift);
    }
    return ret;
}

static u32 CalculateCRC32(u32 start_addr, u32 len, u8 *pfw)
{
    u32 i = 0;
    u32 j = 0;
    u32 crc_poly = 0x04C11DB7;
    u32 tmp_crc = 0xFFFFFFFF;

    for (i = start_addr; i < start_addr + len; i++) {
        tmp_crc ^= (pfw[i] << 24);

        for (j = 0; j < 8; j++) {
            if ((tmp_crc & 0x80000000) != 0) {
                tmp_crc = (tmp_crc << 1) ^ crc_poly;
            } else {
                tmp_crc = tmp_crc << 1;
            }
        }
    }
    return tmp_crc;
}

static int host_download_dma_check(u32 start_addr, u32 block_size, u32 *dma_crc)
{
    int count = 50;
    u32 reg_data = 0;

    /* dma1 src1 adress */
    ilitek_ice_mode_write(0x072104, start_addr, 4);
    /* dma1 src1 format */
    ilitek_ice_mode_write(0x072108, 0x80000001, 4);
    /* dma1 dest address */
    ilitek_ice_mode_write(0x072114, 0x00030000, 4);
    /* dma1 dest format */
    ilitek_ice_mode_write(0x072118, 0x80000000, 4);
    /* Block size*/
    ilitek_ice_mode_write(0x07211C, block_size, 4);

    idev->chip->hd_dma_check_crc_off();

    /* crc on */
    ilitek_ice_mode_write(0x041016, 0x01, 1);
    /* Dma1 stop */
    ilitek_ice_mode_write(0x072100, 0x00000000, 4);
    /* clr int */
    ilitek_ice_mode_write(0x048006, 0x1, 1);
    /* Dma1 start */
    ilitek_ice_mode_write(0x072100, 0x01000000, 4);

    /* Polling BIT0 */
    while (count > 0) {
        int ret = 0;
        mdelay(1);
        ret = ilitek_ice_mode_read(0x048006, &reg_data, sizeof(u8));
        TPD_DEBUG("busy = %x\n", reg_data);
        if (ret >= 0 && (reg_data & 0x01) == 1)
            break;
        count--;
    }

    if (count <= 0) {
        ipio_err("BIT0 is busy\n");
        return -1;
    }
    if (ilitek_ice_mode_read(0x04101C, dma_crc, sizeof(u32)) < 0) {
        ipio_err("read dma crc error\n");
        return -1;
    } else {
        return 0;
    }
}

static int ilitek_tddi_fw_iram_read(u8 *buf, u32 start, u32 end)
{
    int i;
    int addr = 0, r_len = SPI_UPGRADE_LEN;
    u8 cmd[4] = {0};

    if (!buf) {
        ipio_err("buf in null\n");
        return -ENOMEM;
    }

    for (addr = start, i = 0; addr < end; i += r_len, addr += r_len) {
        if ((addr + r_len) > (end + 1))
            r_len = end % r_len;

        cmd[0] = 0x25;
        cmd[3] = (char)((addr & 0x00FF0000) >> 16);
        cmd[2] = (char)((addr & 0x0000FF00) >> 8);
        cmd[1] = (char)((addr & 0x000000FF));

        if (idev->write(cmd, 4)) {
            ipio_err("Failed to write iram data\n");
            return -ENODEV;
        }

        if (idev->read(buf + i, r_len)) {
            ipio_err("Failed to Read iram data\n");
            return -ENODEV;
        }
    }
    return 0;
}

static void ilitek_tddi_fw_print_iram_data(u32 start, u32 size)
{
    int i, len;
    int tmp = ipio_debug_level;
    u8 *buf = NULL;
    u32 end = start + size;

    len = end - start;

    buf = vmalloc(len * sizeof(u8));
    if (ERR_ALLOC_MEM(buf)) {
        ipio_err("Failed to allocate buf memory, %ld\n", PTR_ERR(buf));
        return;
    }

    for (i = 0; i < len; i++)
        buf[i] = 0xFF;

    if (ilitek_tddi_fw_iram_read(buf, start, end) < 0)
        ipio_err("Read IRAM data failed\n");

    ipio_debug_level = 1;
    ilitek_dump_data(buf, 8, len, 0, "IRAM");
    ipio_debug_level = tmp;
    ipio_vfree((void **)&buf);
}

int ilitek_tddi_fw_dump_iram_data(u32 start, u32 end)
{
    struct file *f = NULL;
    u8 *buf = NULL;
    mm_segment_t old_fs;
    loff_t pos = 0;
    int ret, wdt, i;
    int len;

    f = filp_open(DUMP_IRAM_PATH, O_WRONLY | O_CREAT | O_TRUNC, 644);
    if (ERR_ALLOC_MEM(f)) {
        ipio_err("Failed to open the file at %ld.\n", PTR_ERR(f));
        return -1;
    }

    ret = ilitek_ice_mode_ctrl(ENABLE, OFF);
    if (ret < 0) {
        filp_close(f, NULL);
        return ret;
    }

    wdt = ilitek_tddi_ic_watch_dog_ctrl(ILI_READ, DISABLE);
    if (wdt)
        ilitek_tddi_ic_watch_dog_ctrl(ILI_WRITE, DISABLE);

    len = end - start + 1;

    buf = vmalloc(len * sizeof(u8));
    if (ERR_ALLOC_MEM(buf)) {
        ipio_err("Failed to allocate buf memory, %ld\n", PTR_ERR(buf));
        filp_close(f, NULL);
        ret = ENOMEM;
        goto out;
    }

    for (i = 0; i < len; i++)
        buf[i] = 0xFF;

    if (ilitek_tddi_fw_iram_read(buf, start, end) < 0)
        ipio_err("Read IRAM data failed\n");

    old_fs = get_fs();
    set_fs(get_ds());
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_write(f, buf, len, &pos);
    set_fs(old_fs);

out:
    if (wdt)
        ilitek_tddi_ic_watch_dog_ctrl(ILI_WRITE, ENABLE);

    ilitek_ice_mode_ctrl(DISABLE, OFF);
    filp_close(f, NULL);
    ipio_vfree((void **)&buf);
    TPD_INFO("dump iram data success\n");
    return 0;
}

static int ilitek_tddi_fw_iram_program(u32 start, u32 size, u8 *w_buf)
{
    int ret = 0;
    u32 array[1];

    if (idev->fw_buf_dma == NULL) {
        TPD_INFO("The dma fw buf is null!");
        return -ENOMEM;
    }

    idev->fw_buf_dma[0] = SPI_WRITE;
    idev->fw_buf_dma[1] = 0x25;
    idev->fw_buf_dma[4] = (char)((start & 0x00FF0000) >> 16);
    idev->fw_buf_dma[3] = (char)((start & 0x0000FF00) >> 8);
    idev->fw_buf_dma[2] = (char)((start & 0x000000FF));

    array[0] = size + 5;

    memcpy(&idev->fw_buf_dma[5], w_buf, size);

    ret = spi_write_firmware(idev->spi, idev->fw_buf_dma, array, 1);

    if (ret < 0) {
        return ret;
    }
    /* holding the status until finish this upgrade. */
    idev->fw_update_stat = 90;

    return 0;
}

static int ilitek_tddi_fw_iram_upgrade(u8 *pfw)
{
    int i, ret = UPDATE_PASS;
    u32 mode, crc, dma;
    u8 *fw_ptr = NULL;

    if (idev->already_reset == false) {
        if (idev->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
            ilitek_tddi_reset_ctrl(ILITEK_RESET_METHOD);
            idev->ignore_first_irq = true;
        } else {
            idev->ignore_first_irq = false;
        }
        ret = ilitek_ice_mode_ctrl(ENABLE, OFF);
        if (ret < 0)
            return ret;
    } else {
        idev->already_reset = false;
    }
    ret = ilitek_tddi_ic_watch_dog_ctrl(ILI_WRITE, DISABLE);
    if (ret < 0)
        return ret;

    fw_ptr = pfw;
    if (idev->actual_tp_mode == P5_X_FW_TEST_MODE) {
        mode = MP;
    } else if (idev->actual_tp_mode == P5_X_FW_GESTURE_MODE) {
        mode = GESTURE;
        fw_ptr = gestrue_fw;
    } else {
        mode = AP;
    }

    /* Program data to iram acorrding to each block */
    for (i = 0; i < ARRAY_SIZE(fbi); i++) {
        if (fbi[i].mode == mode && fbi[i].len != 0) {
            TPD_INFO("Download %s code from hex 0x%x to IRAM 0x%x, len = 0x%x\n",
                     fbi[i].name, fbi[i].start, fbi[i].mem_start, fbi[i].len);


            ilitek_tddi_fw_iram_program(fbi[i].mem_start, fbi[i].len, (fw_ptr + fbi[i].start));

            crc = CalculateCRC32(fbi[i].start, fbi[i].len - 4, fw_ptr);
            dma = 0;
            ret = host_download_dma_check(fbi[i].mem_start, fbi[i].len - 4, &dma);

            TPD_INFO("%s CRC is %s (%x) : (%x)\n",
                     fbi[i].name, (crc != dma ? "Invalid !" : "Correct !"), crc, dma);

            if (ret < 0 || crc != dma) {
                ilitek_tddi_fw_print_iram_data(fbi[i].mem_start, fbi[i].len);
                return UPDATE_FAIL;
            }
        }
    }

    if (idev->actual_tp_mode != P5_X_FW_GESTURE_MODE)
        ilitek_tddi_reset_ctrl(TP_IC_CODE_RST);

    ilitek_ice_mode_ctrl(DISABLE, OFF);
    mdelay(10);
    return ret;
}

static void ilitek_tddi_fw_update_block_info(u8 *pfw)
{
    u32 ges_area_section, ges_info_addr, ges_fw_start, ges_fw_end;

    TPD_INFO("Upgarde = IRAM, Tag = %x\n", tfd.hex_tag);


    if (tfd.hex_tag == BLOCK_TAG_AF) {
        fbi[AP].mem_start = (fbi[AP].fix_mem_start != INT_MAX) ? fbi[AP].fix_mem_start : 0;
        fbi[DATA].mem_start = (fbi[DATA].fix_mem_start != INT_MAX) ? fbi[DATA].fix_mem_start : DLM_START_ADDRESS;
        fbi[TUNING].mem_start = (fbi[TUNING].fix_mem_start != INT_MAX) ? fbi[TUNING].fix_mem_start :  fbi[DATA].mem_start + fbi[DATA].len;
        fbi[MP].mem_start = (fbi[MP].fix_mem_start != INT_MAX) ? fbi[MP].fix_mem_start :  0;
        //fbi[GESTURE].mem_start = (fbi[GESTURE].fix_mem_start != INT_MAX) ? fbi[GESTURE].fix_mem_start :     0;

        /* Parsing gesture info form AP code */
        ges_info_addr = (fbi[AP].end + 1 - 60);
        ges_area_section = (pfw[ges_info_addr + 3] << 24) + (pfw[ges_info_addr + 2] << 16) + (pfw[ges_info_addr + 1] << 8) + pfw[ges_info_addr];
        fbi[GESTURE].mem_start = (pfw[ges_info_addr + 7] << 24) + (pfw[ges_info_addr + 6] << 16) + (pfw[ges_info_addr + 5] << 8) + pfw[ges_info_addr + 4];

        ges_fw_start = (pfw[ges_info_addr + 15] << 24) + (pfw[ges_info_addr + 14] << 16) + (pfw[ges_info_addr + 13] << 8) + pfw[ges_info_addr + 12];
        ges_fw_end = (pfw[ges_info_addr + 19] << 24) + (pfw[ges_info_addr + 18] << 16) + (pfw[ges_info_addr + 17] << 8) + pfw[ges_info_addr + 16];
        fbi[GESTURE].len = ges_fw_end - ges_fw_start + 1;
        if (fbi[GESTURE].len > 8 * K) {
            TPD_INFO("WARRING!!!The gesture len is too long!!!The size is %d.\n", fbi[GESTURE].len);
            fbi[GESTURE].len = 8 * K;
        }
        fbi[GESTURE].start = 0;
    } else {
        fbi[AP].start = 0;
        fbi[AP].mem_start = 0;
        fbi[AP].len = MAX_AP_FIRMWARE_SIZE;

        fbi[DATA].start = DLM_HEX_ADDRESS;
        fbi[DATA].mem_start = DLM_START_ADDRESS;
        fbi[DATA].len = MAX_DLM_FIRMWARE_SIZE;

        fbi[MP].start = MP_HEX_ADDRESS;
        fbi[MP].mem_start = 0;
        fbi[MP].len = MAX_MP_FIRMWARE_SIZE;

        /* Parsing gesture info form AP code */
        ges_info_addr = (MAX_AP_FIRMWARE_SIZE - 60);
        ges_area_section = (pfw[ges_info_addr + 3] << 24) + (pfw[ges_info_addr + 2] << 16) + (pfw[ges_info_addr + 1] << 8) + pfw[ges_info_addr];
        fbi[GESTURE].mem_start = (pfw[ges_info_addr + 7] << 24) + (pfw[ges_info_addr + 6] << 16) + (pfw[ges_info_addr + 5] << 8) + pfw[ges_info_addr + 4];

        ges_fw_start = (pfw[ges_info_addr + 15] << 24) + (pfw[ges_info_addr + 14] << 16) + (pfw[ges_info_addr + 13] << 8) + pfw[ges_info_addr + 12];
        ges_fw_end = (pfw[ges_info_addr + 19] << 24) + (pfw[ges_info_addr + 18] << 16) + (pfw[ges_info_addr + 17] << 8) + pfw[ges_info_addr + 16];
        fbi[GESTURE].len = ges_fw_end - ges_fw_start + 1;
        if (fbi[GESTURE].len > 8 * K) {
            TPD_INFO("WARRING!!!The gesture len is too long!!!The size is %d.\n", fbi[GESTURE].len);
            fbi[GESTURE].len = 8 * K;
        }
        fbi[GESTURE].start = 0;
    }

    memset(gestrue_fw, 0xff, sizeof(gestrue_fw));

    /* Copy gesture data */
    if (fbi[GESTURE].mem_start != 0xffffffff && ges_fw_start != 0xffffffff && fbi[GESTURE].mem_start != 0 && ges_fw_start != 0)
        ipio_memcpy(gestrue_fw, (pfw + ges_fw_start), fbi[GESTURE].len, sizeof(gestrue_fw));
    else
        ipio_err("There is no gesture data inside fw\n");

    TPD_INFO("gesture memory start = 0x%x, upgrade lenth = 0x%x, hex area = %d, ges_start_addr = 0x%x, ges_end_addr = 0x%x",
             fbi[GESTURE].mem_start, MAX_GESTURE_FIRMWARE_SIZE, ges_area_section, ges_fw_start, ges_fw_end);

    fbi[AP].name = "AP";
    fbi[DATA].name = "DATA";
    fbi[TUNING].name = "TUNING";
    fbi[MP].name = "MP";
    fbi[GESTURE].name = "GESTURE";

    /* upgrade mode define */
    fbi[DATA].mode = fbi[AP].mode = fbi[TUNING].mode = AP;
    fbi[MP].mode = MP;
    fbi[GESTURE].mode = GESTURE;


    /* Get hex fw vers */
    tfd.new_fw_cb = (pfw[FW_VER_ADDR] << 24) | (pfw[FW_VER_ADDR + 1] << 16) |
                    (pfw[FW_VER_ADDR + 2] << 8) | (pfw[FW_VER_ADDR + 3]);

    /* Calculate update adress    */
    TPD_INFO("New FW ver = 0x%x\n", tfd.new_fw_cb);
    TPD_INFO("star_addr = 0x%06X, end_addr = 0x%06X, Block Num = %d\n", tfd.start_addr, tfd.end_addr, tfd.block_number);
}

static int ilitek_tddi_fw_ili_convert(u8 *pfw)
{
    int i = 0, block_enable = 0, num = 0;
    u8 block;
    u32 Addr;
    int ret = 0;
    const unsigned char *CTPM_FW = NULL;

    if (!idev->p_firmware_headfile) {
        ipio_err("get idev->p_firmware_headfile is NULL\n");
        return -1;
    }
    if(!idev->p_firmware_headfile->firmware_data || idev->p_firmware_headfile->firmware_size <= ILI_FILE_HEADER
       || idev->p_firmware_headfile->firmware_size >= MAX_HEX_FILE_SIZE) {
        ipio_err("get idev->p_firmware_headfile error\n");
        return -1;
    }
    TPD_INFO("p_firmware_headfile->firmware_size = 0x%X\n", (int)idev->p_firmware_headfile->firmware_size);
    CTPM_FW = idev->p_firmware_headfile->firmware_data;
    TPD_INFO("Start to parse ILI file, type = %d, block_count = %d\n", CTPM_FW[32], CTPM_FW[33]);

    memset(fbi, 0x0, sizeof(fbi));

    tfd.start_addr = 0;
    tfd.end_addr = 0;
    tfd.hex_tag = 0;

    block_enable = CTPM_FW[32];

    if (block_enable == 0) {
        tfd.hex_tag = BLOCK_TAG_AE;
        goto out;
    }

    tfd.hex_tag = BLOCK_TAG_AF;
    for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
        if (((block_enable >> i) & 0x01) == 0x01) {
            num = i + 1;
            if ((num) == 6) {
                fbi[num].start = (CTPM_FW[0] << 16) + (CTPM_FW[1] << 8) + (CTPM_FW[2]);
                fbi[num].end = (CTPM_FW[3] << 16) + (CTPM_FW[4] << 8) + (CTPM_FW[5]);
                fbi[num].fix_mem_start = INT_MAX;
            } else {
                fbi[num].start = (CTPM_FW[34 + i * 6] << 16) + (CTPM_FW[35 + i * 6] << 8) + (CTPM_FW[36 + i * 6]);
                fbi[num].end = (CTPM_FW[37 + i * 6] << 16) + (CTPM_FW[38 + i * 6] << 8) + (CTPM_FW[39 + i * 6]);
                fbi[num].fix_mem_start = INT_MAX;
            }
            fbi[num].len = fbi[num].end - fbi[num].start + 1;
            TPD_INFO("Block[%d]: start_addr = %x, end = %x\n", num, fbi[num].start, fbi[num].end);
        }
    }

    if ((block_enable & 0x80) == 0x80) {
        for (i = 0; i < 3; i++) {
            Addr = (CTPM_FW[6 + i * 4] << 16) + (CTPM_FW[7 + i * 4] << 8) + (CTPM_FW[8 + i * 4]);
            block = CTPM_FW[9 + i * 4];

            if ((block != 0) && (Addr != 0x000000)) {
                fbi[block].fix_mem_start = Addr;
                TPD_INFO("Tag 0xB0: change Block[%d] to addr = 0x%x\n", block, fbi[block].fix_mem_start);
            }
        }
    }
    ret = 0;
out:
    tfd.block_number = CTPM_FW[33];
    //memcpy(pfw, CTPM_FW + ILI_FILE_HEADER, (idev->p_firmware_headfile->firmware_size - ILI_FILE_HEADER));
    ipio_memcpy(pfw, CTPM_FW + ILI_FILE_HEADER, (idev->p_firmware_headfile->firmware_size - ILI_FILE_HEADER), MAX_HEX_FILE_SIZE);
    tfd.end_addr = (idev->p_firmware_headfile->firmware_size - ILI_FILE_HEADER);
    return ret;
}

static int ilitek_tddi_fw_hex_convert(u8 *pfw)
{
    u8 *phex = (u8 *)idev->tp_firmware.data;
    int size = (int)idev->tp_firmware.size;
    int block = 0;
    u32 i = 0, j = 0, k = 0, num = 0;
    u32 len = 0, addr = 0, type = 0;
    u32 start_addr = 0x0, end_addr = 0x0, ex_addr = 0;
    u32 offset, hex_crc, data_crc;

    memset(fbi, 0x0, sizeof(fbi));

    /* Parsing HEX file */
    for (; i < size;) {
        len = HexToDec(&phex[i + 1], 2);
        addr = HexToDec(&phex[i + 3], 4);
        type = HexToDec(&phex[i + 7], 2);

        if (type == 0x04) {
            ex_addr = HexToDec(&phex[i + 9], 4);
        } else if (type == 0x02) {
            ex_addr = HexToDec(&phex[i + 9], 4);
            ex_addr = ex_addr >> 12;
        } else if (type == BLOCK_TAG_AE || type == BLOCK_TAG_AF) {
            /* insert block info extracted from hex */
            tfd.hex_tag = type;
            if (tfd.hex_tag == BLOCK_TAG_AF)
                num = HexToDec(&phex[i + 9 + 6 + 6], 2);
            else {
                num = block + 1;
                if (num == 3) {
                    num = 5;
                }
            }
            fbi[num].start = HexToDec(&phex[i + 9], 6);
            fbi[num].end = HexToDec(&phex[i + 9 + 6], 6);
            fbi[num].fix_mem_start = INT_MAX;
            fbi[num].len = fbi[num].end - fbi[num].start + 1;
            TPD_INFO("Block[%d]: start_addr = %x, end = %x", num, fbi[num].start, fbi[num].end);

            block++;
        } else if (type == BLOCK_TAG_B0 && tfd.hex_tag == BLOCK_TAG_AF) {
            num = HexToDec(&phex[i + 9 + 6], 2);
            fbi[num].fix_mem_start = HexToDec(&phex[i + 9], 6);
            TPD_INFO("Tag 0xB0: change Block[%d] to addr = 0x%x\n", num, fbi[num].fix_mem_start);
        }

        addr = addr + (ex_addr << 16);

        if (phex[i + 1 + 2 + 4 + 2 + (len * 2) + 2] == 0x0D)
            offset = 2;
        else
            offset = 1;

        if (addr > MAX_HEX_FILE_SIZE) {
            ipio_err("Invalid hex format %d\n", addr);
            return -1;
        }

        if (type == 0x00) {
            end_addr = addr + len;
            if (addr < start_addr)
                start_addr = addr;
            /* fill data */
            for (j = 0, k = 0; j < (len * 2); j += 2, k++)
                pfw[addr + k] = HexToDec(&phex[i + 9 + j], 2);
        }
        i += 1 + 2 + 4 + 2 + (len * 2) + 2 + offset;
    }

    /* Check the content of hex file by comparsing parsed data to the crc at last 4 bytes */
    for (i = 0; i < ARRAY_SIZE(fbi); i++) {
        if (fbi[i].end == 0)
            continue;
        ex_addr = fbi[i].end;
        data_crc = CalculateCRC32(fbi[i].start, fbi[i].len - 4, pfw);
        hex_crc = pfw[ex_addr - 3] << 24 | pfw[ex_addr - 2] << 16 | pfw[ex_addr - 1] << 8 | pfw[ex_addr];
        TPD_DEBUG("data crc = %x, hex crc = %x\n", data_crc, hex_crc);
        if (data_crc != hex_crc) {
            ipio_err("Content of hex file is broken. (%d, %x, %x)\n",
                     i, data_crc, hex_crc);
            return -1;
        }
    }

    TPD_INFO("Contect of hex file is correct\n");
    tfd.start_addr = start_addr;
    tfd.end_addr = end_addr;
    tfd.block_number = block;
    return 0;
}

int ilitek_tddi_fw_upgrade(void)
{
    int ret = -1;
    int retry = 3;
    u8 *pfw = NULL;

    atomic_set(&idev->fw_stat, START);

    idev->esd_check_enabled = false;

    idev->fw_update_stat = 0;

    pfw = vmalloc(MAX_HEX_FILE_SIZE * sizeof(u8));
    if (ERR_ALLOC_MEM(pfw)) {
        ipio_err("Failed to allocate pfw memory, %ld\n", PTR_ERR(pfw));
        ret = -ENOMEM;
        goto out;
    }

    memset(pfw, 0xFF, MAX_HEX_FILE_SIZE * sizeof(u8));

    if (idev->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
        if ((ERR_ALLOC_MEM(idev->tp_firmware.data))
            || (0 == idev->tp_firmware.size)
            || ilitek_tddi_fw_hex_convert(pfw) < 0) {
            ipio_err("Open file fail, try upgrade from hex file\n");
            ret = ilitek_tddi_fw_ili_convert(pfw);
            if (ret < 0)
                goto out;
        }
        ilitek_tddi_fw_update_block_info(pfw);
    }

    do {
        ret = ilitek_tddi_fw_iram_upgrade(pfw);

        if (ret == UPDATE_PASS) {
            break;
        }

        ipio_err("Upgrade failed, do retry!\n");
    } while (--retry > 0);

    if (ret != UPDATE_PASS) {
        ipio_err("Upgrade firmware failed after retry 3 times\n");
        ilitek_tddi_reset_ctrl(ILITEK_RESET_METHOD);
        ret = UPDATE_FAIL;
    }
    idev->already_upgrade = true;
    idev->sleep_type = NOT_SLEEP_MODE;
    if (ret == UPDATE_PASS) {
        ilitek_tddi_ic_get_protocl_ver();
        ilitek_tddi_ic_get_fw_ver();
        ilitek_tddi_ic_get_core_ver();
        ilitek_tddi_ic_get_tp_info();
        ilitek_tddi_ic_get_panel_info();

#ifdef CONFIG_OPLUS_TP_APK
        if(idev->debug_mode_sta && idev->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
            if(idev->ts->apk_op && idev->ts->apk_op->apk_debug_set) {
                idev->ts->apk_op->apk_debug_set((void *)idev, true);
            }
        }
#endif // end of CONFIG_OPLUS_TP_APK
    }
out:
    ipio_vfree((void **)&pfw);
    if (ret != 0)
        idev->fw_update_stat = -1;
    else
        idev->fw_update_stat = 100;

    idev->esd_check_enabled = true;

    atomic_set(&idev->fw_stat, END);
    return ret;
}
