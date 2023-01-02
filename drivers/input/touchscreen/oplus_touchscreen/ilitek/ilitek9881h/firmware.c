// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/version.h>
#include <linux/uaccess.h>

#include "ilitek_ili9881h.h"
#include "firmware.h"
#include "data_transfer.h"
#include "protocol.h"
#include "finger_report.h"

#ifdef BOOT_FW_UPGRADE
#include "ilitek_fw.h"
#endif
#ifdef HOST_DOWNLOAD
#include "ilitek_fw.h"
#endif
extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;
extern struct core_fr_data *core_fr;
/*
 * the size of two arrays is different depending on
 * which of methods to upgrade firmware you choose for.
 */
uint8_t *flash_fw = NULL;

#ifdef HOST_DOWNLOAD
uint8_t ap_fw[MAX_AP_FIRMWARE_SIZE] = { 0 };
uint8_t dlm_fw[MAX_DLM_FIRMWARE_SIZE] = { 0 };
uint8_t mp_fw[MAX_MP_FIRMWARE_SIZE] = { 0 };
uint8_t gesture_fw[MAX_GESTURE_FIRMWARE_SIZE] = { 0 };
#else
uint8_t iram_fw[MAX_IRAM_FIRMWARE_SIZE] = { 0 };
#endif
/* the length of array in each sector */
int g_section_len = 0;
int g_total_sector = 0;

#ifdef BOOT_FW_UPGRADE
/* The addr of block reserved for customers */
int g_start_resrv = 0x1C000;
int g_end_resrv = 0x1CFFF;
#endif

struct flash_sector {
	uint32_t ss_addr;
	uint32_t se_addr;
	uint32_t checksum;
	uint32_t crc32;
	uint32_t dlength;
	bool data_flag;
	bool inside_block;
};

struct flash_block_info {
	uint32_t start_addr;
	uint32_t end_addr;
	uint32_t hex_crc;
	uint32_t block_crc;
};

__attribute__((weak)) void set_tp_fw_done(void) {printk("set_tp_fw_done() in tp\n");}

struct flash_sector *g_flash_sector = NULL;
struct flash_block_info g_flash_block_info[4];
struct core_firmware_data *core_firmware = NULL;

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
	uint32_t nRetVal = 0;
	uint32_t nTemp = 0;
	uint32_t i = 0;
	int32_t nShift = (nLength - 1) * 4;

	for (i = 0; i < nLength; nShift -= 4, i++) {
		if ((pHex[i] >= '0') && (pHex[i] <= '9')) {
			nTemp = pHex[i] - '0';
		} else if ((pHex[i] >= 'a') && (pHex[i] <= 'f')) {
			nTemp = (pHex[i] - 'a') + 10;
		} else if ((pHex[i] >= 'A') && (pHex[i] <= 'F')) {
			nTemp = (pHex[i] - 'A') + 10;
		} else {
			return -1;
		}

		nRetVal |= (nTemp << nShift);
	}

	return nRetVal;
}

static uint32_t calc_crc32(uint32_t start_addr, uint32_t end_addr, uint8_t *data)
{
	int i = 0;
	int j = 0;
	uint32_t CRC_POLY = 0x04C11DB7;
	uint32_t ReturnCRC = 0xFFFFFFFF;
	uint32_t len = start_addr + end_addr;

	for (i = start_addr; i < len; i++) {
		ReturnCRC ^= (data[i] << 24);

		for (j = 0; j < 8; j++) {
			if ((ReturnCRC & 0x80000000) != 0) {
				ReturnCRC = ReturnCRC << 1 ^ CRC_POLY;
			} else {
				ReturnCRC = ReturnCRC << 1;
			}
		}
	}

	return ReturnCRC;
}

static uint32_t tddi_check_data(uint32_t start_addr, uint32_t end_addr)
{
	int timer = 500;
	uint32_t busy = 0;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;
	uint32_t id = core_config->chip_id;
	uint32_t type = core_config->chip_type;

	write_len = end_addr;

	TPD_DEBUG("start = 0x%x , write_len = 0x%x, max_count = %x\n",
	    start_addr, end_addr, core_firmware->max_count);

	if (write_len > core_firmware->max_count) {
		TPD_INFO("The length (%x) written to firmware is greater than max count (%x)\n",
			write_len, core_firmware->max_count);
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x3b, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	core_config_ice_mode_write(0x041003, 0x01, 1);	/* Enable Dio_Rx_dual */
	core_config_ice_mode_write(0x041008, 0xFF, 1);	/* Dummy */

	/* Set Receive count */
	if (core_firmware->max_count == 0xFFFF)
		core_config_ice_mode_write(0x04100C, write_len, 2);
	else if (core_firmware->max_count == 0x1FFFF)
		core_config_ice_mode_write(0x04100C, write_len, 3);

	if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_F) {
		/* Checksum_En */
		core_config_ice_mode_write(0x041014, 0x10000, 3);
	} else if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_H) {
		/* Clear Int Flag */
		core_config_ice_mode_write(0x048007, 0x02, 1);

		/* Checksum_En */
		core_config_ice_mode_write(0x041016, 0x00, 1);
		core_config_ice_mode_write(0x041016, 0x01, 1);
	}

	/* Start to receive */
	core_config_ice_mode_write(0x041010, 0xFF, 1);

	while (timer > 0) {

		mdelay(1);

		if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_F)
			busy = core_config_read_write_onebyte(0x041014);
		else if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_H) {
			busy = core_config_read_write_onebyte(0x048007);
			busy = busy >> 1;
		} else {
			TPD_INFO("Unknow chip type\n");
			break;
		}

		if ((busy & 0x01) == 0x01)
			break;

		timer--;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	if (timer >= 0) {
		/* Disable dio_Rx_dual */
		core_config_ice_mode_write(0x041003, 0x0, 1);
		iram_check =  core_firmware->isCRC ? core_config_ice_mode_read(0x4101C) : core_config_ice_mode_read(0x041018);
	} else {
		TPD_INFO("TIME OUT\n");
		goto out;
	}

	return iram_check;

out:
	TPD_INFO("Failed to read Checksum/CRC from IC\n");
	return -1;

}

static void calc_verify_data(uint32_t sa, uint32_t se, uint32_t *check)
{
	uint32_t i = 0;
	uint32_t tmp_ck = 0;
	uint32_t tmp_crc = 0;

	if (core_firmware->isCRC) {
		tmp_crc = calc_crc32(sa, se, flash_fw);
		*check = tmp_crc;
	} else {
		for (i = sa; i < (sa + se); i++)
			tmp_ck = tmp_ck + flash_fw[i];

		*check = tmp_ck;
	}
}

static int do_check(uint32_t start, uint32_t len)
{
	int res = 0;
	uint32_t vd = 0;
	uint32_t lc = 0;

	calc_verify_data(start, len, &lc);
	vd = tddi_check_data(start, len);
	res = CHECK_EQUAL(vd, lc);

	TPD_INFO("%s (%x) : (%x)\n", (res < 0 ? "Invalid !" : "Correct !"), vd, lc);

	return res;
}

static int verify_flash_data(void)
{
	int i = 0;
	int res = 0;
	int len = 0;
	int fps = flashtab->sector;
	uint32_t ss = 0x0;

	/* check chip type with its max count */
	if (core_config->chip_id == CHIP_TYPE_ILI7807 && core_config->chip_type == ILI7807_TYPE_H) {
		core_firmware->max_count = 0x1FFFF;
		core_firmware->isCRC = true;
	}

	for (i = 0; i < g_section_len + 1; i++) {
		if (g_flash_sector[i].data_flag) {
			if (ss > g_flash_sector[i].ss_addr || len == 0)
				ss = g_flash_sector[i].ss_addr;

			len = len + g_flash_sector[i].dlength;

			/* if larger than max count, then committing data to check */
			if (len >= (core_firmware->max_count - fps)) {
				res = do_check(ss, len);
				if (res < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		} else {
			/* split flash sector and commit the last data to fw */
			if (len != 0) {
				res = do_check(ss, len);
				if (res < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		}
	}

	/* it might be lower than the size of sector if calc the last array. */
	if (len != 0 && res != -1)
		res = do_check(ss, core_firmware->end_addr - ss);

out:
	return res;
}

static int do_program_flash(uint32_t start_addr)
{
	int res = 0;
	uint32_t k = 0;
	uint8_t buf[512] = { 0 };

	res = core_flash_write_enable();
	if (res < 0)
		goto out;

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x02, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	buf[0] = 0x25;
	buf[3] = 0x04;
	buf[2] = 0x10;
	buf[1] = 0x08;

	for (k = 0; k < flashtab->program_page; k++) {
		if (start_addr + k <= core_firmware->end_addr)
			buf[4 + k] = flash_fw[start_addr + k];
		else
			buf[4 + k] = 0xFF;
	}

	if (core_write(core_config->slave_i2c_addr, buf, flashtab->program_page + 4) < 0) {
		TPD_INFO("Failed to write data at start_addr = 0x%X, k = 0x%X, addr = 0x%x\n",
			start_addr, k, start_addr + k);
		res = -EIO;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	res = core_flash_poll_busy();
	if (res < 0)
		goto out;

	core_firmware->update_status = (start_addr * 101) / core_firmware->end_addr;

	/* holding the status until finish this upgrade. */
	if (core_firmware->update_status > 90)
		core_firmware->update_status = 90;

	/* Don't use TPD_INFO to print log because it needs to be kpet in the same line */
	printk("%cUpgrading firmware ... start_addr = 0x%x, %02d%c", 0x0D, start_addr, core_firmware->update_status,
	       '%');

out:
	return res;
}

static int flash_program_sector(void)
{
	int i = 0;
	int j = 0;
	int res = 0;

	for (i = 0; i < g_section_len + 1; i++) {
		/*
		 * If running the boot stage, fw will only be upgrade data with the flag of block,
		 * otherwise data with the flag itself will be programed.
		 */
		if (core_firmware->isboot) {
			if (!g_flash_sector[i].inside_block)
				continue;
		} else {
			if (!g_flash_sector[i].data_flag)
				continue;
		}

		/* programming flash by its page size */
		for (j = g_flash_sector[i].ss_addr; j < g_flash_sector[i].se_addr; j += flashtab->program_page) {
			if (j > core_firmware->end_addr)
				goto out;

			res = do_program_flash(j);
			if (res < 0)
				goto out;
		}
	}

out:
	return res;
}

static int do_erase_flash(uint32_t start_addr)
{
	int res = 0;
	uint32_t temp_buf = 0;

	res = core_flash_write_enable();
	if (res < 0) {
		TPD_INFO("Failed to config write enable\n");
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x20, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	mdelay(1);

	res = core_flash_poll_busy();
	if (res < 0)
		goto out;

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x3, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);
	core_config_ice_mode_write(0x041008, 0xFF, 1);

	temp_buf = core_config_read_write_onebyte(0x041010);
	if (temp_buf != 0xFF) {
		TPD_INFO("Failed to erase data(0x%x) at 0x%x\n", temp_buf, start_addr);
		res = -EINVAL;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	TPD_DEBUG("Earsing data at start addr: %x\n", start_addr);

out:
	return res;
}

static int flash_erase_sector(void)
{
	int i = 0;
	int res = 0;

	for (i = 0; i < g_total_sector; i++) {
		if (core_firmware->isboot) {
			if (!g_flash_sector[i].inside_block)
				continue;
		} else {
			if (!g_flash_sector[i].data_flag && !g_flash_sector[i].inside_block)
				continue;
		}

		res = do_erase_flash(g_flash_sector[i].ss_addr);
		if (res < 0)
			goto out;
	}

out:
	return res;
}

#ifndef HOST_DOWNLOAD
static int iram_upgrade(void)
{
	int i = 0;
	int j = 0;
	int res = 0;
	uint8_t buf[512];
	int upl = flashtab->program_page;

	/* doing reset for erasing iram data before upgrade it. */
	ilitek_platform_tp_hw_reset(true);

	mdelay(1);

	TPD_INFO("Upgrade firmware written data into IRAM directly\n");

	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		return res;
	}

	mdelay(20);

	core_config_set_watch_dog(false);

	TPD_DEBUG("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X\n",
	    core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);

	/* write hex to the addr of iram */
	TPD_INFO("Writing data into IRAM ...\n");
	for (i = core_firmware->start_addr; i < core_firmware->end_addr; i += upl) {
		if ((i + 256) > core_firmware->end_addr) {
			upl = core_firmware->end_addr % upl;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		for (j = 0; j < upl; j++)
			buf[4 + j] = iram_fw[i + j];

		if (core_write(core_config->slave_i2c_addr, buf, upl + 4)) {
			TPD_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
				(int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
			return res;
		}

		core_firmware->update_status = (i * 101) / core_firmware->end_addr;
		printk("%cupgrade firmware(ap code), %02d%c", 0x0D, core_firmware->update_status, '%');

		mdelay(3);
	}

	/* ice mode code reset */
	TPD_INFO("Doing code reset ...\n");
	core_config_ice_mode_write(0x40040, 0xAE, 1);
	core_config_ice_mode_write(0x40040, 0x00, 1);

	mdelay(10);
	core_config_set_watch_dog(true);
	core_config_ice_mode_disable();

	/*TODO: check iram status */

	return res;
}
#endif

#ifdef HOST_DOWNLOAD

int check_hex_crc(void)
{
	int ap_crc = 0;
	int dlm_crc = 0;
	int mp_crc = 0;
	int gesture_crc = 0;

	int hex_ap_crc = 0;
	int hex_dlm_crc = 0;
	int hex_mp_crc = 0;
	int hex_gesture_crc = 0;
	bool check_fail = false;

	int addr_len = 0;
	addr_len = g_flash_block_info[0].end_addr - g_flash_block_info[0].start_addr;
	if ((addr_len >= 4) && (addr_len < MAX_AP_FIRMWARE_SIZE)) {
		ap_crc = calc_crc32(0, addr_len - 3, ap_fw);
		hex_ap_crc = ((ap_fw[addr_len - 3] << 24) + (ap_fw[addr_len - 2] << 16) + \
			(ap_fw[addr_len - 1] << 8) + ap_fw[addr_len]);
	}
	else {
		check_fail = true;
		TPD_INFO("get ap addr len error addr_len = 0x%X\n", addr_len);
	}
	TPD_DEBUG("ap block, driver crc = 0x%X, hex ap crc = 0x%X\n", ap_crc, hex_ap_crc);

	addr_len = g_flash_block_info[1].end_addr - g_flash_block_info[1].start_addr;
	if ((addr_len >= 4) && (addr_len < MAX_DLM_FIRMWARE_SIZE)) {
		dlm_crc = calc_crc32(0, addr_len -3, dlm_fw);
		hex_dlm_crc = ((dlm_fw[addr_len - 3] << 24) + (dlm_fw[addr_len - 2] << 16) + \
			(dlm_fw[addr_len - 1] << 8) + dlm_fw[addr_len]);
	}
	else {
		check_fail = true;
		TPD_INFO("get dlm addr len error addr_len = 0x%X\n", addr_len);
	}
	TPD_DEBUG("dlm block, driver crc = 0x%X, hex dlm crc = 0x%X\n", dlm_crc, hex_dlm_crc);

	addr_len = g_flash_block_info[2].end_addr - g_flash_block_info[2].start_addr;
	if ((addr_len >= 4) && (addr_len < MAX_MP_FIRMWARE_SIZE)) {
		mp_crc = calc_crc32(0, addr_len -3, mp_fw);
		hex_mp_crc = ((mp_fw[addr_len - 3] << 24) + (mp_fw[addr_len - 2] << 16) + \
			(mp_fw[addr_len - 1] << 8) + mp_fw[addr_len]);
	}
	else {
		check_fail = true;
		TPD_INFO("get mp addr len error addr_len = 0x%X\n", addr_len);
	}
	TPD_DEBUG("mp block, driver crc = 0x%x, hex mp crc = 0x%x\n", mp_crc, hex_mp_crc);

	addr_len = g_flash_block_info[3].end_addr - g_flash_block_info[3].start_addr;
	if ((addr_len >= 4) && (addr_len < MAX_GESTURE_FIRMWARE_SIZE)) {
		gesture_crc = calc_crc32(0, addr_len -3, gesture_fw);
		hex_gesture_crc = ((gesture_fw[addr_len - 3] << 24) + (gesture_fw[addr_len - 2] << 16) + \
			(gesture_fw[addr_len - 1] << 8) + gesture_fw[addr_len]);
	}
	else {
		check_fail = true;
		TPD_INFO("get gesture addr len error addr_len = 0x%X\n", addr_len);
	}
	TPD_DEBUG("gesture block, driver crc = 0x%x, hex gesture crc = 0x%x\n", gesture_crc, hex_gesture_crc);

	if ((check_fail) || (ap_crc != hex_ap_crc) || (mp_crc != hex_mp_crc)\
		|| (dlm_crc != hex_dlm_crc) || (gesture_crc != hex_gesture_crc)) {
		TPD_INFO("crc erro use header file data check_fail = %d\n", check_fail);
		TPD_INFO("ap block, driver crc = 0x%X, hex ap crc = 0x%X\n", ap_crc, hex_ap_crc);
		TPD_INFO("dlm block, driver crc = 0x%X, hex dlm crc = 0x%X\n", dlm_crc, hex_dlm_crc);
		TPD_INFO("mp block, driver crc = 0x%x, hex mp crc = 0x%x\n", mp_crc, hex_mp_crc);
		TPD_INFO("gesture block, driver crc = 0x%x, hex gesture crc = 0x%x\n", gesture_crc, hex_gesture_crc);
		return -1;
	}
	else {
		TPD_DEBUG("check file crc ok\n");
		return 0;
	}
}
int read_download(uint32_t start, uint32_t size, uint8_t *r_buf, uint32_t r_len)
{
	int res = 0;
	int addr = 0;
	int i = 0;
	uint32_t end = start + size;
	uint8_t *buf = NULL;
    buf = (uint8_t*)kmalloc(sizeof(uint8_t) * r_len + 4, GFP_KERNEL);
	if (ERR_ALLOC_MEM(buf)) {
		TPD_INFO("malloc read_ap_buf error\n");
		return -1;
	}
	memset(buf, 0xFF, (int)sizeof(uint8_t) * r_len + 4);
	for (addr = start, i = 0; addr < end; i += r_len, addr += r_len) {
		if ((addr + r_len) > end) {
			r_len = end % r_len;
		}
		buf[0] = 0x25;
		buf[3] = (char)((addr & 0x00FF0000) >> 16);
		buf[2] = (char)((addr & 0x0000FF00) >> 8);
		buf[1] = (char)((addr & 0x000000FF));
		if (core_write(core_config->slave_i2c_addr, buf, 4)) {
			TPD_INFO("Failed to write data via SPI\n");
			res = -EIO;
			goto error;
		}
		res = core_read(core_config->slave_i2c_addr, buf, r_len);
		if (res < 0) {
			TPD_INFO("Failed to read data via SPI\n");
			res = -EIO;
			goto error;
		}
		memcpy(r_buf + i, buf, r_len);
	}
error:
	ipio_kfree((void **)&buf);
	return res;
}


int write_download(uint32_t start, uint32_t size, uint8_t *w_buf, uint32_t w_len)
{
	int res = 0;
	int addr = 0;
	int i = 0;
	int update_status = 0;
	int end = 0;
	int j = 0;
	uint8_t *buf = NULL;
	end = start + size;
    buf = (uint8_t*)kmalloc(sizeof(uint8_t) * w_len + 4, GFP_KERNEL);
	if (ERR_ALLOC_MEM(buf)) {
		TPD_INFO("malloc read_ap_buf error\n");
		return -1;
	}
	memset(buf, 0xFF, (int)sizeof(uint8_t) * w_len + 4);
	for (addr = start, i = 0; addr < end; addr += w_len, i += w_len) {
		if ((addr + w_len) > end) {
			w_len = end % w_len;
		}
		buf[0] = 0x25;
		buf[3] = (char)((addr & 0x00FF0000) >> 16);
		buf[2] = (char)((addr & 0x0000FF00) >> 8);
		buf[1] = (char)((addr & 0x000000FF));
		for (j = 0; j < w_len; j++)
			buf[4 + j] = w_buf[i + j];

		if (core_write(core_config->slave_i2c_addr, buf, w_len + 4)) {
			TPD_INFO("Failed to write data via SPI, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
				(int)addr, 0, end);
			res = -EIO;
			goto write_error;
		}

		update_status = (i * 101) / end;
		//printk("%cupgrade firmware(mp code), %02d%c", 0x0D, update_status, '%');
	}
write_error:
	ipio_kfree((void **)&buf);
	return res;
}


static int host_download_dma_check(int block)
{
	int count = 50;
	uint8_t ap_block = 0;
	uint8_t dlm_block = 1;
	uint32_t start_addr = 0;
	uint32_t block_size = 0;
	uint32_t busy = 0;
	uint32_t reg_data = 0;
	if (block == ap_block) {
		start_addr = 0;
		block_size = MAX_AP_FIRMWARE_SIZE - 0x4;
	} else if (block == dlm_block) {
		start_addr = DLM_START_ADDRESS;
		block_size = MAX_DLM_FIRMWARE_SIZE;
	}
	/* dma_ch1_start_clear */
	core_config_ice_mode_write(0x072103, 0x2, 1);
	/* dma1 src1 adress */
	core_config_ice_mode_write(0x072104, start_addr, 4);
	/* dma1 src1 format */
	core_config_ice_mode_write(0x072108, 0x80000001, 4);
	/* dma1 dest address */
	core_config_ice_mode_write(0x072114, 0x00030000, 4);
	/* dma1 dest format */
	core_config_ice_mode_write(0x072118, 0x80000000, 4);
	/* Block size*/
	core_config_ice_mode_write(0x07211C, block_size, 4);
	/* crc off */
	core_config_ice_mode_write(0x041014, 0x00000000, 4);
	/* dma crc */
	core_config_ice_mode_write(0x041048, 0x00000001, 4);
	/* crc on */
	core_config_ice_mode_write(0x041014, 0x00010000, 4);
	/* Dma1 stop */
	core_config_ice_mode_write(0x072100, 0x00000000, 4);
	/* clr int */
	core_config_ice_mode_write(0x048006, 0x1, 1);
	/* Dma1 start */
	core_config_ice_mode_write(0x072100, 0x01000000, 4);

	/* Polling BIT0 */
	while (count > 0) {
		mdelay(1);
		busy = core_config_read_write_onebyte(0x048006);
		reg_data = core_config_ice_mode_read(0x072100);
		TPD_DEBUG("0x072100 reg_data = 0x%X busy = 0x%X\n", reg_data, busy);
		if ((busy & 0x01) == 1)
			break;

		count--;
	}

	if (count <= 0) {
		TPD_INFO("BIT0 is busy\n");
		reg_data = core_config_ice_mode_read(0x072100);
		TPD_INFO("0x072100 reg_data = 0x%X\n", reg_data);
		//return -1;
	}

	return core_config_ice_mode_read(0x04101C);
}

int host_download(bool mode)
{
	int res = 0;
	int ap_crc = 0;
	int dlm_crc = 0;

	int ap_dma = 0;
	int dlm_dma = 0;
	int method = 0;
	uint8_t *buf = NULL;
	uint8_t *read_ap_buf = NULL;
	uint8_t *read_dlm_buf = NULL;
	uint8_t *read_mp_buf = NULL;
	uint8_t *read_gesture_buf = NULL;
	uint8_t *gesture_ap_buf = NULL;
	uint32_t reg_data = 0;
	int retry = 100;
    read_ap_buf = (uint8_t*)vmalloc(MAX_AP_FIRMWARE_SIZE);
	if (ERR_ALLOC_MEM(read_ap_buf)) {
		TPD_INFO("malloc read_ap_buf error\n");
		goto out;
	}
	memset(read_ap_buf, 0xFF, MAX_AP_FIRMWARE_SIZE);
	//create ap buf
    read_dlm_buf = (uint8_t*)vmalloc(MAX_DLM_FIRMWARE_SIZE);
	if (ERR_ALLOC_MEM(read_dlm_buf)) {
		TPD_INFO("malloc read_dlm_buf error\n");
		goto out;
	}
	memset(read_dlm_buf, 0xFF, MAX_DLM_FIRMWARE_SIZE);
	//create mp buf
    read_mp_buf = (uint8_t*)vmalloc(MAX_MP_FIRMWARE_SIZE);
	if (ERR_ALLOC_MEM(read_mp_buf)) {
		TPD_INFO("malloc read_mp_buf error\n");
		goto out;
	}
	memset(read_mp_buf, 0xFF, MAX_MP_FIRMWARE_SIZE);
	//create buf
    buf = (uint8_t*)vmalloc(sizeof(uint8_t)*0x10000+4);
	if (ERR_ALLOC_MEM(buf)) {
		TPD_INFO("malloc buf error\n");
		goto out;
	}
	memset(buf, 0xFF, (int)sizeof(uint8_t) * 0x10000+4);
	//create gesture buf
    read_gesture_buf = (uint8_t*)vmalloc(core_gesture->ap_length);
	if (ERR_ALLOC_MEM(read_gesture_buf)) {
		TPD_INFO("malloc read_gesture_buf error\n");
		goto out;
	}
    gesture_ap_buf = (uint8_t*)vmalloc(core_gesture->ap_length);
	if (ERR_ALLOC_MEM(gesture_ap_buf)) {
		TPD_INFO("malloc gesture_ap_buf error\n");
		goto out;
	}
	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		goto out;
	}

	res = check_hex_crc();
	if (res < 0) {
		TPD_INFO("crc erro use header file data\n");
		if (core_firmware_get_h_file_data() < 0) {
			TPD_INFO("Failed to get h file data\n");
		}
        res = 0;
	}

	method = core_config_ice_mode_read(core_config->pid_addr);
	method = method & 0xff;
	TPD_DEBUG("method of calculation for crc = %x\n", method);
	core_firmware->enter_mode = -1;
	ipd->esd_check_enabled = false;
	ipd->irq_timer = jiffies;    //reset esd check trigger base time
	memset(gesture_ap_buf, 0xFF, core_gesture->ap_length);
	TPD_INFO("core_gesture->entry = %d\n", core_gesture->entry);
	memset(read_gesture_buf, 0xFF, core_gesture->ap_length);
	//TPD_INFO("Upgrade firmware written data into AP code directly\n");
	core_config_ice_mode_write(0x5100C, 0x81, 1);
	core_config_ice_mode_write(0x5100C, 0x98, 1);
	while(retry--) {
		reg_data = core_config_read_write_onebyte(0x51018);
		if (reg_data == 0x5A) {
			TPD_DEBUG("check wdt close ok 0x51018 read 0x%X\n", reg_data);
			break;
		}
		mdelay(10);
	}
	if (retry <= 0) {
		TPD_INFO("check wdt close error 0x51018 read 0x%X\n", reg_data);
	}
	core_config_ice_mode_write(0x5100C, 0x00, 1);

	if(core_fr->actual_fw_mode == P5_0_FIRMWARE_TEST_MODE)
	{
		core_firmware->enter_mode = P5_0_FIRMWARE_TEST_MODE;
		/* write hex to the addr of MP code */
		TPD_DEBUG("Writing data into MP code ...\n");
		if(write_download(0, MAX_MP_FIRMWARE_SIZE, mp_fw, SPI_UPGRADE_LEN) < 0)
		{
			TPD_INFO("SPI Write MP code data error\n");
		}
		if(read_download(0, MAX_MP_FIRMWARE_SIZE, read_mp_buf, SPI_UPGRADE_LEN))
		{
			TPD_INFO("SPI Read MP code data error\n");
		}
		if(memcmp(mp_fw, read_mp_buf, MAX_MP_FIRMWARE_SIZE) == 0)
		{
			TPD_INFO("Check MP Mode upgrade: PASS\n");
		}
		else
		{
			TPD_INFO("Check MP Mode upgrade: FAIL\n");
			res = UPDATE_FAIL;
			goto out;
		}
	}
	else if(core_gesture->entry)
	{
		//int i;
		if(mode)
		{
			core_firmware->enter_mode = P5_0_FIRMWARE_GESTURE_MODE;
			/* write hex to the addr of Gesture code */
			//TPD_INFO("Writing data into Gesture code ...\n");
			if(write_download(core_gesture->ap_start_addr, core_gesture->length, gesture_fw, core_gesture->length) < 0)
			{
				TPD_INFO("SPI Write Gesture code data error\n");
			}
			if(read_download(core_gesture->ap_start_addr, core_gesture->length, read_gesture_buf, core_gesture->length))
			{
				TPD_INFO("SPI Read Gesture code data error\n");
			}
			if(memcmp(gesture_fw, read_gesture_buf, core_gesture->length) == 0)
			{
				TPD_INFO("Check Gesture Mode upgrade: PASS\n");
			}
			else
			{
				TPD_INFO("Check Gesture Mode upgrade: FAIL\n");
				res = UPDATE_FAIL;
				goto out;
			}
		}
		else{
			core_firmware->enter_mode = P5_0_FIRMWARE_DEMO_MODE;
			/* write hex to the addr of AP code */
			memcpy(gesture_ap_buf, ap_fw + core_gesture->ap_start_addr, core_gesture->ap_length);
			//TPD_INFO("Writing data into AP code ...\n");
			if(write_download(core_gesture->ap_start_addr, core_gesture->ap_length, gesture_ap_buf, core_gesture->ap_length) < 0)
			{
				TPD_INFO("SPI Write AP code data error\n");
			}
			if(read_download(core_gesture->ap_start_addr, core_gesture->ap_length, read_ap_buf, core_gesture->ap_length))
			{
				TPD_INFO("SPI Read AP code data error\n");
			}
			if(memcmp(gesture_ap_buf, read_ap_buf, core_gesture->ap_length) == 0)
			{
				TPD_INFO("Check AP Mode upgrade: PASS\n");
			}
			else
			{
				TPD_INFO("Check AP Mode upgrade: FAIL\n");
				res = UPDATE_FAIL;
				goto out;
			}
		}
	}
	else
	{
		core_firmware->enter_mode = P5_0_FIRMWARE_DEMO_MODE;
		/* write hex to the addr of AP code */
		//TPD_INFO("Writing data into AP code ...\n");
		if(write_download(0, MAX_AP_FIRMWARE_SIZE, ap_fw, SPI_UPGRADE_LEN) < 0)
		{
			TPD_INFO("SPI Write AP code data error\n");
		}
		/* write hex to the addr of DLM code */
		//TPD_INFO("Writing data into DLM code ...\n");
		if(write_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, dlm_fw, SPI_UPGRADE_LEN) < 0)
		{
			TPD_INFO("SPI Write DLM code data error\n");
		}
		/* Check AP/DLM mode Buffer data */
		if (method >= CORE_TYPE_E) {
			ap_crc = calc_crc32(0, MAX_AP_FIRMWARE_SIZE - 4, ap_fw);
			ap_dma = host_download_dma_check(0);

			dlm_crc = calc_crc32(0, MAX_DLM_FIRMWARE_SIZE, dlm_fw);
			dlm_dma = host_download_dma_check(1);

			TPD_INFO("AP CRC %s (%x) : (%x)\n",
				(ap_crc != ap_dma ? "Invalid !" : "Correct !"), ap_crc, ap_dma);

			TPD_INFO("DLM CRC %s (%x) : (%x)\n",
				(dlm_crc != dlm_dma ? "Invalid !" : "Correct !"), dlm_crc, dlm_dma);

			if (CHECK_EQUAL(ap_crc, ap_dma) == UPDATE_FAIL ||
					CHECK_EQUAL(dlm_crc, dlm_dma) == UPDATE_FAIL ) {
				TPD_INFO("Check AP/DLM Mode upgrade: FAIL read data check\n");
				res = UPDATE_FAIL;
				read_download(0, MAX_AP_FIRMWARE_SIZE, read_ap_buf, SPI_UPGRADE_LEN);
				read_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, read_dlm_buf, SPI_UPGRADE_LEN);

				if (memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) != 0 ||
						memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) != 0) {
					TPD_INFO("Check AP/DLM Mode upgrade: FAIL\n");
					res = UPDATE_FAIL;
					goto out;
				} else {
					TPD_INFO("Check AP/DLM Mode upgrade: SUCCESS\n");
					res = 0;
				}
				//goto out;
			}
		} else {
			read_download(0, MAX_AP_FIRMWARE_SIZE, read_ap_buf, SPI_UPGRADE_LEN);
			read_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, read_dlm_buf, SPI_UPGRADE_LEN);

			if (memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) != 0 ||
					memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) != 0) {
				TPD_INFO("Check AP/DLM Mode upgrade: FAIL\n");
				res = UPDATE_FAIL;
				goto out;
			} else {
				TPD_INFO("Check AP/DLM Mode upgrade: SUCCESS\n");
			}
		}
		if (1 == core_firmware->esd_fail_enter_gesture) {
			TPD_INFO("set 0x25FF8 = 0xF38A94EF for gesture\n");
			core_config_ice_mode_write(0x25FF8, 0xF38A94EF, 4);
		}
	}

	core_config_ice_mode_write(0x5100C, 0x01, 1);
	while(retry--) {
		reg_data = core_config_read_write_onebyte(0x51018);
		if (reg_data == 0xA5) {
			TPD_DEBUG("check wdt open ok 0x51018 read 0x%X\n", reg_data);
			break;
		}
		mdelay(10);
	}
	if (retry <= 0) {
		TPD_INFO("check wdt open error 0x51018 read 0x%X retry set\n", reg_data);
		core_config_ice_mode_write(0x5100C, 0x01, 1);
	}
	if(core_gesture->entry != true)
	{
		/* ice mode code reset */
		TPD_DEBUG("Doing code reset ...\n");
		core_config_ice_mode_write(0x40040, 0xAE, 1);
	}
#ifdef CHECK_REG
	core_get_tp_register();
	#ifdef CHECK_DDI_REG
	core_get_ddi_register();
	#endif
#endif
	core_config_ice_mode_disable();
#ifdef CHECK_REG
		res = core_config_ice_mode_enable();
		if (res < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		}
		//mdelay(20);

		core_get_tp_register();
	#ifdef CHECK_DDI_REG
		core_get_ddi_register();
	#endif
		core_config_ice_mode_disable();
#endif
	if(core_fr->actual_fw_mode == P5_0_FIRMWARE_TEST_MODE)
		mdelay(200);
	else
	    mdelay(40);
out:
	ipd->esd_check_enabled = true;
	ipio_vfree((void **)&buf);
	ipio_vfree((void **)&read_ap_buf);
	ipio_vfree((void **)&read_dlm_buf);
	ipio_vfree((void **)&read_mp_buf);
	ipio_vfree((void **)&read_gesture_buf);
	ipio_vfree((void **)&gesture_ap_buf);
	if (res == UPDATE_FAIL) {
		core_config_ice_mode_disable();
	}
	return res;
}
EXPORT_SYMBOL(host_download);

int core_load_gesture_code(void)
{
	int res = 0;
	int i = 0;
	uint8_t temp[64] = {0};
	core_gesture->entry = true;
	core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
	TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
	if (core_firmware->core_version >= 0x01000600) {
		core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
		core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
		core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
	}
	else {
		core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
		core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
		core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
	}
	core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
	core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
    core_fr->isEnableFR = false;
	ilitek_platform_disable_irq();
    TPD_DEBUG("gesture_start_addr = 0x%x, core_gesture->length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
	TPD_DEBUG("area = %d, ap_start_addr = 0x%x, core_gesture->ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
	//write load gesture flag
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x03;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
	//enter gesture cmd lpwg start
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = core_gesture->mode + 1;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
	for(i = 0; i < 5; i++)
	{
        temp[0] = 0xF6;
        temp[1] = 0x0A;
        TPD_DEBUG("write prepare gesture command 0xF6 0x0A \n");
        if ((core_write(core_config->slave_i2c_addr, temp, 2)) < 0) {
            TPD_INFO("write prepare gesture command error\n");
        }
		mdelay(i*50);
		temp[0] = 0x01;
		temp[1] = 0x0A;
		temp[2] = 0x05;
		if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
			TPD_INFO("write command error\n");
		}
		if ((core_read(core_config->slave_i2c_addr, temp, 2)) < 0) {
			TPD_INFO("Read command error\n");
		}
		if(temp[0] == 0x91)
		{
			TPD_DEBUG("check fw ready\n");
			break;
		}
	}
	if(temp[0] != 0x91)
			TPD_INFO("FW is busy, error\n");

	//load gesture code
	if (core_config_ice_mode_enable() < 0) {
		TPD_INFO("Failed to enter ICE mode\n");
        res = -1;
		goto out;
	}
	host_download(true);

	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x06;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
out:
    core_fr->isEnableFR = true;
	ilitek_platform_enable_irq();
	return res;
}
EXPORT_SYMBOL(core_load_gesture_code);

int core_load_ap_code(void)
{
	int res = 0;
	int i = 0;
	uint8_t temp[64] = {0};
	uint32_t gesture_end_addr = 0;
	uint32_t gesture_start_addr = 0;
	uint32_t ap_start_addr = 0;
	uint32_t ap_end_addr = 0;
	uint32_t area = 0;
	core_gesture->entry = true;
    core_fr->isEnableFR = false;
	ilitek_platform_disable_irq();
	//Write Load AP Flag
	temp[0] = 0x01;
	temp[1] = 0x01;
	temp[2] = 0x00;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
	if ((core_read(core_config->slave_i2c_addr, temp, 20)) < 0) {
		TPD_INFO("Read command error\n");
	}
	area = (temp[0] << 24) + (temp[1] << 16) + (temp[2] << 8) + temp[3];
	ap_start_addr = (temp[4] << 24) + (temp[5] << 16) + (temp[6] << 8) + temp[7];
	ap_end_addr = (temp[8] << 24) + (temp[9] << 16) + (temp[10] << 8) + temp[11];
	gesture_start_addr = (temp[12] << 24) + (temp[13] << 16) + (temp[14] << 8) + temp[15];
	gesture_end_addr = (temp[16] << 24) + (temp[17] << 16) + (temp[18] << 8) + temp[19];
	TPD_INFO("gesture_start_addr = 0x%x, gesture_end_addr = 0x%x\n", gesture_start_addr, gesture_end_addr);
	TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_end_addr = 0x%x\n", area, ap_start_addr, ap_end_addr);
	//Leave Gesture Cmd LPWG Stop
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x00;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
	for(i = 0; i < 20; i++)
	{
		mdelay(i*100+100);
		temp[0] = 0x01;
		temp[1] = 0x0A;
		temp[2] = 0x05;
		if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
			TPD_INFO("write command error\n");
		}
		if ((core_read(core_config->slave_i2c_addr, temp, 1)) < 0) {
			TPD_INFO("Read command error\n");
		}
		if(temp[0] == 0x91)
		{
			TPD_INFO("check fw ready\n");
			break;
		}
	}
	if(i == 3 && temp[0] != 0x01)
			TPD_INFO("FW is busy, error\n");

	//load AP code
	if (core_config_ice_mode_enable() < 0) {
		TPD_INFO("Failed to enter ICE mode\n");
        res = -1;
		goto out;
	}
	res = host_download(false);

	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x06;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		TPD_INFO("write command error\n");
	}
out:
    core_fr->isEnableFR = true;
	ilitek_platform_enable_irq();
	core_gesture->entry = false;
	return res;
}
EXPORT_SYMBOL(core_load_ap_code);

#endif

static int tddi_fw_upgrade(bool isIRAM)
{
	int res = 0;
#ifndef HOST_DOWNLOAD
	if (isIRAM) {
		res = iram_upgrade();
		return res;
	}
#endif

	ilitek_platform_tp_hw_reset(true);

	TPD_INFO("Enter to ICE Mode\n");

	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(5);

	/*
	 * This command is used to fix the bug of spi clk in 7807F-AB
	 * while operating with flash.
	 */
	if (core_config->chip_id == CHIP_TYPE_ILI7807 && core_config->chip_type == ILI7807_TYPE_F_AB) {
		res = core_config_ice_mode_write(0x4100C, 0x01, 1);
		if (res < 0)
			goto out;
	}

	mdelay(25);

	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
		res = -EINVAL;
		goto out;
	}

	/* Disable flash protection from being written */
	core_flash_enable_protect(false);

	res = flash_erase_sector();
	if (res < 0) {
		TPD_INFO("Failed to erase flash\n");
		goto out;
	}

	mdelay(1);

	res = flash_program_sector();
	if (res < 0) {
		TPD_INFO("Failed to program flash\n");
		goto out;
	}

	/* We do have to reset chip in order to move new code from flash to iram. */
	TPD_INFO("Doing Soft Reset ..\n");
	core_config_ic_reset();

	/* the delay time moving code depends on what the touch IC you're using. */
	mdelay(core_firmware->delay_after_upgrade);

	/* ensure that the chip has been updated */
	TPD_INFO("Enter to ICE Mode again\n");
	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(20);

	/* check the data that we've just written into the iram. */
	res = verify_flash_data();
	if (res == 0)
		TPD_INFO("Data Correct !\n");

out:
	if (core_config_set_watch_dog(true) < 0) {
		TPD_INFO("Failed to enable watch dog\n");
		res = -EINVAL;
	}
	core_config_ice_mode_disable();
	return res;
}

#ifdef BOOT_FW_UPGRADE  
static int convert_hex_array(void)
{
	int i = 0;
	int j = 0;
	int index = 0;
	int crc_byte_len = 4;
	int block = 0;
	int blen = 0;
	int bindex = 0;
	uint32_t tmp_addr = 0x0;
	uint32_t start_addr = 0x0;
	uint32_t end_addr = 0;
	bool boot_update_flag = false;
	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
	core_firmware->hasBlockInfo = false;

	TPD_INFO("CTPM_FW = %d\n", (int)ARRAY_SIZE(CTPM_FW));

	if (ARRAY_SIZE(CTPM_FW) <= 0) {
		TPD_INFO("The size of CTPM_FW is invaild (%d)\n", (int)ARRAY_SIZE(CTPM_FW));
		goto out;
	}

	/* Get new version from ILI array */
	if(protocol->mid >= 0x3)
	{
		core_firmware->new_fw_ver[0] = CTPM_FW[18];
		core_firmware->new_fw_ver[1] = CTPM_FW[19];
		core_firmware->new_fw_ver[2] = CTPM_FW[20];
		core_firmware->new_fw_ver[3] = CTPM_FW[21];
	}
	else{
		core_firmware->new_fw_ver[0] = CTPM_FW[19];
		core_firmware->new_fw_ver[1] = CTPM_FW[20];
		core_firmware->new_fw_ver[2] = CTPM_FW[21];
	}
	/* The process will be executed if the comparison is different with origin ver */
	for (i = 0; i < ARRAY_SIZE(core_firmware->old_fw_ver); i++) {
		if (core_firmware->old_fw_ver[i] != core_firmware->new_fw_ver[i]) {
			TPD_INFO("FW version is different, preparing to upgrade FW\n");
			break;
		}
	}
	TPD_INFO("hw fw: %d.%d.%d.%d", core_firmware->old_fw_ver[0], core_firmware->old_fw_ver[1], core_firmware->old_fw_ver[2], core_firmware->old_fw_ver[3]);
	TPD_INFO("hex fw: %d.%d.%d.%d", core_firmware->new_fw_ver[0], core_firmware->new_fw_ver[1], core_firmware->new_fw_ver[2], core_firmware->new_fw_ver[3]);
	if (i == ARRAY_SIZE(core_firmware->old_fw_ver)) {
		TPD_INFO("FW version is the same as previous version\n");
		//goto out;
	}

	/* Extract block info */
	block = CTPM_FW[33];

	if (block > 0) {
		core_firmware->hasBlockInfo = true;

		/* Initialize block's index and length */
		blen = 6;
		bindex = 34;
		ilitek_platform_disable_irq();

		core_config_ice_mode_enable();
		mdelay(30);

		if (core_config_set_watch_dog(false) < 0) {
			TPD_INFO("Failed to disable watch dog\n");
		}
		for (i = 0; i < block; i++) {
			for (j = 0; j < blen; j++) {
				if (j < 3)
					g_flash_block_info[i].start_addr =
					    (g_flash_block_info[i].start_addr << 8) | CTPM_FW[bindex + j];
				else
					g_flash_block_info[i].end_addr =
					    (g_flash_block_info[i].end_addr << 8) | CTPM_FW[bindex + j];
			}
			start_addr = g_flash_block_info[i].start_addr;
			end_addr = g_flash_block_info[i].end_addr;
			g_flash_block_info[i].hex_crc = (CTPM_FW[64 + end_addr - 3] << 24) + (CTPM_FW[64 + end_addr - 2] << 16)
			+ (CTPM_FW[64 + end_addr - 1] << 8) + CTPM_FW[64 +(end_addr)];
			g_flash_block_info[i].block_crc = tddi_check_data(start_addr, end_addr - start_addr - crc_byte_len + 1);
			TPD_INFO("block=%d, start_addr=0x%06x, end_addr=0x%06x, H_CRC=0x%06x, B_CRC=0x%06x\n", i, start_addr, end_addr, g_flash_block_info[i].hex_crc, g_flash_block_info[i].block_crc);
			bindex += blen;
			if(g_flash_block_info[i].hex_crc != g_flash_block_info[i].block_crc)
			{
				boot_update_flag = true;
				break;
			}
		}
	}
	core_config_ice_mode_disable();
	if(boot_update_flag == false)
	{
		TPD_INFO("No need upgrade\n");
		goto out;
	}
	/* Fill data into buffer */
	for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
		flash_fw[i] = CTPM_FW[i + 64];
		index = i / flashtab->sector;
		if (!g_flash_sector[index].data_flag) {
			g_flash_sector[index].ss_addr = index * flashtab->sector;
			g_flash_sector[index].se_addr = (index + 1) * flashtab->sector - 1;
			g_flash_sector[index].dlength =
			    (g_flash_sector[index].se_addr - g_flash_sector[index].ss_addr) + 1;
			g_flash_sector[index].data_flag = true;
		}
	}

	g_section_len = index;

	if (g_flash_sector[g_section_len].se_addr > flashtab->mem_size) {
		TPD_INFO("The size written to flash is larger than it required (%x) (%x)\n",
			g_flash_sector[g_section_len].se_addr, flashtab->mem_size);
		goto out;
	}

	for (i = 0; i < g_total_sector; i++) {
		/* fill meaing address in an array where is empty */
		if (g_flash_sector[i].ss_addr == 0x0 && g_flash_sector[i].se_addr == 0x0) {
			g_flash_sector[i].ss_addr = tmp_addr;
			g_flash_sector[i].se_addr = (i + 1) * flashtab->sector - 1;
		}

		tmp_addr += flashtab->sector;

		/* set erase flag in the block if the addr of sectors is between them. */
		if (core_firmware->hasBlockInfo) {
			for (j = 0; j < ARRAY_SIZE(g_flash_block_info); j++) {
				if (g_flash_sector[i].ss_addr >= g_flash_block_info[j].start_addr
				    && g_flash_sector[i].se_addr <= g_flash_block_info[j].end_addr) {
					g_flash_sector[i].inside_block = true;
					break;
				}
			}
		}

		/*
		 * protects the reserved address been written and erased.
		 * This feature only applies on the boot upgrade. The addr is progrmmable in normal case.
		 */
		if (g_flash_sector[i].ss_addr == g_start_resrv && g_flash_sector[i].se_addr == g_end_resrv) {
			g_flash_sector[i].inside_block = false;
		}
	}

	/* DEBUG: for showing data with address that will write into fw or be erased */
	for (i = 0; i < g_total_sector; i++) {
		TPD_INFO
		    ("g_flash_sector[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d, inside_block = %d\n",
		     i, g_flash_sector[i].ss_addr, g_flash_sector[i].se_addr, g_flash_sector[index].dlength,
		     g_flash_sector[i].data_flag, g_flash_sector[i].inside_block);
	}

	core_firmware->start_addr = 0x0;
	core_firmware->end_addr = g_flash_sector[g_section_len].se_addr;
	TPD_INFO("start_addr = 0x%06X, end_addr = 0x%06X\n", core_firmware->start_addr, core_firmware->end_addr);
	return 0;

out:
	TPD_INFO("Failed to convert ILI FW array\n");
	return -1;
}

int core_firmware_boot_upgrade(void)
{
	int res = 0;
	bool power = false;

	TPD_INFO("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}

	/* store old version before upgrade fw */
	if(protocol->mid >= 0x3)
	{
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
		core_firmware->old_fw_ver[3] = core_config->firmware_ver[4];
	}
	else
	{
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
	}
	if (flashtab == NULL) {
		TPD_INFO("Flash table isn't created\n");
		res = -ENOMEM;
		goto out;
	}

	//flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	flash_fw = (uint8_t*)vmalloc(flashtab->mem_size);
	if (ERR_ALLOC_MEM(flash_fw)) {
		TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		TPD_INFO("Flash configure is wrong\n");
		res = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(struct flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		TPD_INFO("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		res = -ENOMEM;
		goto out;
	}

	res = convert_hex_array();
	if (res < 0) {
		TPD_INFO("Failed to covert firmware data, res = %d\n", res);
		goto out;
	}
	/* calling that function defined at init depends on chips. */
	res = core_firmware->upgrade_func(false);
	if (res < 0) {
		core_firmware->update_status = res;
		TPD_INFO("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}

	core_firmware->update_status = 100;
	TPD_INFO("Update firmware information...\n");
	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	core_config_get_key_info();

out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}

	ipio_vfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	return res;
}
#endif /* BOOT_FW_UPGRADE */

#ifdef HOST_DOWNLOAD
void core_firmware_get_project_h_file_data(void)
{
	static int do_once = 0;

	if (do_once == 0) {
	    do_once = 1;
	    static unsigned char CTPM_FW_18031[] = {
	            #include "FW_NF_ILI9881H_INNOLUX.ili"
	    };
	    TPD_INFO("sizeof(CTPM_FW_18031) = 0x%X\n", (int)sizeof(CTPM_FW_18031));
	    memcpy(CTPM_FW, CTPM_FW_18031, sizeof(CTPM_FW_18031));
	}
}
int core_firmware_get_h_file_data(void)
{
	int res = 0;
	int i = 0;
	int j = 0;
	int block = 0;
	int blen = 0;
	int bindex = 0;
	flash_fw = (uint8_t*)vmalloc(256 * 1024);
	if (ERR_ALLOC_MEM(flash_fw)) {
		TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * 256 * 1024);
	core_firmware_get_project_h_file_data();
	/* Fill data into buffer */
	for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
		flash_fw[i] = CTPM_FW[i + 64];
	}
	memcpy(ap_fw, flash_fw, MAX_AP_FIRMWARE_SIZE);
	memcpy(dlm_fw, flash_fw + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);

	core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
	TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
	if (core_firmware->core_version >= 0x01000600) {
		core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
		core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
		core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
	}
	else {
		core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
		core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
		core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
	}
	core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
	core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;

	TPD_INFO("gesture_start_addr = 0x%x, length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
	TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
	TPD_INFO("MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE);
    memcpy(mp_fw, flash_fw + MP_HEX_ADDRESS, MAX_MP_FIRMWARE_SIZE);
	TPD_INFO("core_gesture->start_addr + core_gesture->length = 0x%X\n", core_gesture->start_addr + MAX_MP_FIRMWARE_SIZE);
	memcpy(gesture_fw, flash_fw + core_gesture->start_addr, core_gesture->length);

	/* Extract block info */
	block = CTPM_FW[33];
    for (i = 0; i < 4; i++) {
        g_flash_block_info[i].start_addr = 0;
        g_flash_block_info[i].end_addr = 0;
    }
	if (block > 0) {
		core_firmware->hasBlockInfo = true;

		/* Initialize block's index and length */
		blen = 6;
		bindex = 34;

		for (i = 0; i < block; i++) {
			for (j = 0; j < blen; j++) {
				if (j < 3)
					g_flash_block_info[i].start_addr =
					    (g_flash_block_info[i].start_addr << 8) | CTPM_FW[bindex + j];
				else
					g_flash_block_info[i].end_addr =
					    (g_flash_block_info[i].end_addr << 8) | CTPM_FW[bindex + j];
			}

			bindex += blen;
		}
	}
	check_hex_crc();
	if (!ERR_ALLOC_MEM(flash_fw)) {
		vfree(flash_fw);
		flash_fw = NULL;
	}
out:
	return res;
}
int core_firmware_boot_host_download(void)
{
	int res = 0;
	int i = 0;
	bool power = false;
	mutex_lock(&ipd->plat_mutex);
	TPD_INFO("MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE);
	//flash_fw = kcalloc(MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
	flash_fw = (uint8_t*)vmalloc(256 * 1024);
	if (ERR_ALLOC_MEM(flash_fw)) {
		TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * 256 * 1024);

	TPD_INFO("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}
	core_firmware_get_project_h_file_data();
	/* Fill data into buffer */
	for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
		flash_fw[i] = CTPM_FW[i + 64];
	}
	memcpy(ap_fw, flash_fw, MAX_AP_FIRMWARE_SIZE);
	memcpy(dlm_fw, flash_fw + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);

	core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
	TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
	if (core_firmware->core_version >= 0x01000600) {
		core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
		core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
		core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
	}
	else {
		core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
		core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
		core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
	}

	core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
	core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
	TPD_INFO("gesture_start_addr = 0x%x, length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
	TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
	TPD_INFO("MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE);
    memcpy(mp_fw, flash_fw + MP_HEX_ADDRESS, MAX_MP_FIRMWARE_SIZE);
	TPD_INFO("core_gesture->start_addr + core_gesture->length = 0x%X\n", core_gesture->start_addr + MAX_MP_FIRMWARE_SIZE);
	memcpy(gesture_fw, flash_fw + core_gesture->start_addr, core_gesture->length);
	ilitek_platform_disable_irq();
	if (ipd->hw_res->reset_gpio) {
		TPD_INFO("HW Reset: HIGH\n");
		gpio_direction_output(ipd->hw_res->reset_gpio, 1);
		mdelay(ipd->delay_time_high);
		TPD_INFO("HW Reset: LOW\n");
		gpio_set_value(ipd->hw_res->reset_gpio, 0);
		mdelay(ipd->delay_time_low);
		TPD_INFO("HW Reset: HIGH\n");
		gpio_set_value(ipd->hw_res->reset_gpio, 1);
		mdelay(ipd->edge_delay);
	}
	else {
		TPD_INFO("reset gpio is Invalid\n");
	}
	res = core_firmware->upgrade_func(true);
	if (res < 0) {
		core_firmware->update_status = res;
		TPD_INFO("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}
	mdelay(20);
	TPD_INFO("mdelay 20 ms test for ftm\n");

	core_firmware->update_status = 100;
	TPD_INFO("Update firmware information...\n");
	core_config_get_chip_id();
	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	if (core_config->tp_info->nKeyCount > 0) {
	    core_config_get_key_info();
    }
	if (ipd->edge_limit_status) {
		core_config_edge_limit_ctrl(true);
	}
	if (ipd->plug_status) {
		core_config_plug_ctrl(false);
	}
out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}

	//ipio_kfree((void **)&flash_fw);
	if (!ERR_ALLOC_MEM(flash_fw)) {
		vfree(flash_fw);
		flash_fw = NULL;
	}
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	ilitek_platform_enable_irq();
	mutex_unlock(&ipd->plat_mutex);
	return res;
}
EXPORT_SYMBOL(core_firmware_boot_host_download);
#endif

static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t k = 0;
	uint32_t nLength = 0;
	uint32_t nAddr = 0;
	uint32_t nType = 0;
	uint32_t nStartAddr = 0x0;
	uint32_t nEndAddr = 0x0;
	uint32_t nChecksum = 0x0;
	uint32_t nExAddr = 0;
#ifndef HOST_DOWNLOAD
	uint32_t tmp_addr = 0x0;
#endif
	int index = 0, block = 0;
	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
	core_firmware->hasBlockInfo = false;
	memset(g_flash_block_info, 0x0, sizeof(g_flash_block_info));
#ifdef HOST_DOWNLOAD
	memset(ap_fw, 0xFF, sizeof(ap_fw));
	memset(dlm_fw, 0xFF, sizeof(dlm_fw));
	memset(mp_fw, 0xFF, sizeof(mp_fw));
	memset(gesture_fw, 0xFF, sizeof(gesture_fw));
#endif
	/* Parsing HEX file */
	for (; i < nSize;) {
		int32_t nOffset;

		nLength = HexToDec(&pBuf[i + 1], 2);
		nAddr = HexToDec(&pBuf[i + 3], 4);
		nType = HexToDec(&pBuf[i + 7], 2);

		/* calculate checksum */
		for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2) {
			if (nType == 0x00) {
				/* for ice mode write method */
				nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);
			}
		}

		if (nType == 0x04) {
			nExAddr = HexToDec(&pBuf[i + 9], 4);
		}

		if (nType == 0x02) {
			nExAddr = HexToDec(&pBuf[i + 9], 4);
			nExAddr = nExAddr >> 12;
		}

		if (nType == 0xAE) {
			core_firmware->hasBlockInfo = true;
			/* insert block info extracted from hex */
			if (block < 4) {
				g_flash_block_info[block].start_addr = HexToDec(&pBuf[i + 9], 6);
				g_flash_block_info[block].end_addr = HexToDec(&pBuf[i + 9 + 6], 6);
				TPD_DEBUG("Block[%d]: start_addr = %x, end = %x\n",
				    block, g_flash_block_info[block].start_addr, g_flash_block_info[block].end_addr);
			}
			block++;
		}

		nAddr = nAddr + (nExAddr << 16);
		if (pBuf[i + 1 + j + 2] == 0x0D) {
			nOffset = 2;
		} else {
			nOffset = 1;
		}

		if (nType == 0x00) {
			if (nAddr > MAX_HEX_FILE_SIZE) {
				TPD_INFO("Invalid hex format\n");
				goto out;
			}

			if (nAddr < nStartAddr) {
				nStartAddr = nAddr;
			}
			if ((nAddr + nLength) > nEndAddr) {
				nEndAddr = nAddr + nLength;
			}
			/* fill data */
			for (j = 0, k = 0; j < (nLength * 2); j += 2, k++) {
				if (isIRAM)
				{
					#ifdef HOST_DOWNLOAD
					if((nAddr + k) < 0x10000)
					{
						ap_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					else if((nAddr + k) >= DLM_HEX_ADDRESS && (nAddr + k) < MP_HEX_ADDRESS )
					{
						if((nAddr + k) < (DLM_HEX_ADDRESS + MAX_DLM_FIRMWARE_SIZE))
							dlm_fw[nAddr - DLM_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					else if((nAddr + k) >= MP_HEX_ADDRESS && ((nAddr + k) < MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE))
					{
						mp_fw[nAddr - MP_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					if ((nAddr + k) == (0xFFF7)) {
						core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
						TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
						if (core_firmware->core_version >= 0x01000600) {
							core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
							core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
							core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
						}
						else {
							core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
							core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
							core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
						}
						core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
						core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
					}
					if((nAddr + k) >= core_gesture->start_addr && (nAddr + k) < (core_gesture->start_addr + MAX_GESTURE_FIRMWARE_SIZE))
					{
						gesture_fw[nAddr - core_gesture->start_addr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					#else
					iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					#endif
				}
				else {
					flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);

					if ((nAddr + k) != 0) {
						index = ((nAddr + k) / flashtab->sector);
						if (!g_flash_sector[index].data_flag) {
							g_flash_sector[index].ss_addr = index * flashtab->sector;
							g_flash_sector[index].se_addr =
							    (index + 1) * flashtab->sector - 1;
							g_flash_sector[index].dlength =
							    (g_flash_sector[index].se_addr -
							     g_flash_sector[index].ss_addr) + 1;
							g_flash_sector[index].data_flag = true;
						}
					}
				}
			}
		}
		i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;
	}
	#ifdef HOST_DOWNLOAD
		return 0;
	#else
	/* Update the length of section */
	g_section_len = index;

	if (g_flash_sector[g_section_len - 1].se_addr > flashtab->mem_size) {
		TPD_INFO("The size written to flash is larger than it required (%x) (%x)\n",
			g_flash_sector[g_section_len - 1].se_addr, flashtab->mem_size);
		goto out;
	}

	for (i = 0; i < g_total_sector; i++) {
		/* fill meaing address in an array where is empty */
		if (g_flash_sector[i].ss_addr == 0x0 && g_flash_sector[i].se_addr == 0x0) {
			g_flash_sector[i].ss_addr = tmp_addr;
			g_flash_sector[i].se_addr = (i + 1) * flashtab->sector - 1;
		}

		tmp_addr += flashtab->sector;

		/* set erase flag in the block if the addr of sectors is between them. */
		if (core_firmware->hasBlockInfo) {
			for (j = 0; j < ARRAY_SIZE(g_flash_block_info); j++) {
				if (g_flash_sector[i].ss_addr >= g_flash_block_info[j].start_addr
				    && g_flash_sector[i].se_addr <= g_flash_block_info[j].end_addr) {
					g_flash_sector[i].inside_block = true;
					break;
				}
			}
		}
	}

	/* DEBUG: for showing data with address that will write into fw or be erased */
	for (i = 0; i < g_total_sector; i++) {
		TPD_DEBUG("g_flash_sector[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d, inside_block = %d\n", i,
		    g_flash_sector[i].ss_addr, g_flash_sector[i].se_addr, g_flash_sector[index].dlength,
		    g_flash_sector[i].data_flag, g_flash_sector[i].inside_block);
	}

	core_firmware->start_addr = nStartAddr;
	core_firmware->end_addr = nEndAddr;
	TPD_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X\n", nStartAddr, nEndAddr);
	return 0;
	#endif

out:
	TPD_INFO("Failed to convert HEX data\n");
	return -1;
}

#ifdef HOST_DOWNLOAD
int core_firmware_get_hostdownload_data(const char *pFilePath)
{
	int res = 0;
	int fsize = 0;
	uint8_t *hex_buffer = NULL;

	struct file *pfile = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;

	if (!ERR_ALLOC_MEM(core_firmware->fw) && (!ERR_ALLOC_MEM(core_firmware->fw->data)) && (core_firmware->fw->size != 0) && (ipd->common_reset == 1)) {
		TPD_INFO("fw from image file\n");
		goto convert_hex;
	}
	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (ERR_ALLOC_MEM(pfile)) {
		TPD_INFO("Failed to open the file at %s.\n", pFilePath);
		res = -EINVAL;
		goto out;
	}

	fsize = pfile->f_inode->i_size;

	TPD_INFO("fsize = %d\n", fsize);

	if (fsize <= 0) {
		TPD_INFO("The size of file is zero\n");
		res = -EINVAL;
		goto out;
	}
	hex_buffer = vmalloc(fsize);//kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		TPD_INFO("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		res = -ENOMEM;
		goto out;
	}
	/* store current userspace mem segment. */
	old_fs = get_fs();

	/* set userspace mem segment equal to kernel's one. */
	set_fs(get_ds());

	/* read firmware data from userspace mem segment */
	vfs_read(pfile, hex_buffer, fsize, &pos);

	/* restore userspace mem segment after read. */
	set_fs(old_fs);
convert_hex:
	if (!ERR_ALLOC_MEM(core_firmware->fw) && (!ERR_ALLOC_MEM(core_firmware->fw->data)) && (core_firmware->fw->size != 0) && (ipd->common_reset == 1)) {
		res = convert_hex_file((uint8_t *)core_firmware->fw->data, core_firmware->fw->size, true);
	}
	else {
		res = convert_hex_file(hex_buffer, fsize, true);
	}
	if (res < 0) {
		TPD_INFO("Failed to covert firmware data, res = %d\n", res);
		goto out;
	}
out:
	if (!ERR_ALLOC_MEM(hex_buffer)) {
		vfree(hex_buffer);
		hex_buffer = NULL;
	}
	return res;
}
#endif

/*
 * It would basically be called by ioctl when users want to upgrade firmware.
 *
 * @pFilePath: pass a path where locates user's firmware file.
 *
 */
int core_firmware_upgrade(const char *pFilePath, bool isIRAM)
{
	int res = 0;
	bool power = false;
	int i = 0;
	uint8_t cmd[4] = { 0 };
#ifndef HOST_DOWNLOAD
	int fsize = 0;
	uint8_t *hex_buffer = NULL;

	struct file *pfile = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
#endif
	if (core_firmware->isUpgrading) {
		TPD_INFO("isupgrading so return\n");
		return 0;
	}
	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}
#ifndef HOST_DOWNLOAD
	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (ERR_ALLOC_MEM(pfile)) {
		TPD_INFO("Failed to open the file at %s.\n", pFilePath);
		res = -ENOENT;
		return res;
	}

	fsize = pfile->f_inode->i_size;

	TPD_INFO("fsize = %d\n", fsize);

	if (fsize <= 0) {
		TPD_INFO("The size of file is zero\n");
		res = -EINVAL;
		goto out;
	}
	TPD_INFO("\n");
	if (flashtab == NULL) {
		TPD_INFO("Flash table isn't created\n");
		res = -ENOMEM;
		goto out;
	}

	//flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	flash_fw = (uint8_t*)vmalloc(flashtab->mem_size);
	if (ERR_ALLOC_MEM(flash_fw)) {
		TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		TPD_INFO("Flash configure is wrong\n");
		res = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(*g_flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		TPD_INFO("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		res = -ENOMEM;
		goto out;
	}
	TPD_INFO("\n");
	hex_buffer = kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		TPD_INFO("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		res = -ENOMEM;
		goto out;
	}
	TPD_INFO("\n");
	/* store current userspace mem segment. */
	old_fs = get_fs();

	/* set userspace mem segment equal to kernel's one. */
	set_fs(get_ds());

	/* read firmware data from userspace mem segment */
	vfs_read(pfile, hex_buffer, fsize, &pos);

	/* restore userspace mem segment after read. */
	set_fs(old_fs);
	TPD_INFO("\n");
	res = convert_hex_file(hex_buffer, fsize, isIRAM);
	if (res < 0) {
		TPD_INFO("Failed to covert firmware data, res = %d\n", res);
		goto out;
	}
#endif
	/* calling that function defined at init depends on chips. */
	res = core_firmware->upgrade_func(isIRAM);
	if (res < 0) {
		TPD_INFO("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}
	
	//check tp set trim code status 
	if (core_firmware->core_version < 0x01000600) {
		for (i = 0; i < 14; i++) {
			cmd[0] = 0x04;
			res = core_write(core_config->slave_i2c_addr, cmd, 1);
			if (res < 0) {
				TPD_INFO("Failed to write data, %d\n", res);
			}

			res = core_read(core_config->slave_i2c_addr, cmd, 3);
			TPD_INFO("read value 0x%X 0x%X 0x%X\n", cmd[0], cmd[1], cmd[2]);
			if (res < 0) {
				TPD_INFO("Failed to read tp set ddi trim code %d\n", res);
			}
			if (cmd[0] == 0x55) {
				TPD_INFO("TP set ddi trim code ok read value 0x%X i = %d\n", cmd[0], i);
				break;
			}
			else if (cmd[0] == 0x35) {
				TPD_INFO("TP set ddi trim code bypass read value 0x%X\n", cmd[0]);
				break;
			}
			mdelay(5);
		}
		if (i >= 14) {
			TPD_INFO("check TP set ddi trim code error\n");
		}
	}
    set_tp_fw_done();

	core_config_get_chip_id();
    core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	if (core_config->tp_info->nKeyCount > 0) {
	    core_config_get_key_info();
    }

	//if (ipd->edge_limit_status) {
		core_config_edge_limit_ctrl(ipd->edge_limit_status);
	//}
	if (ipd->plug_status) {
		core_config_plug_ctrl(false);
	}
	if (ipd->lock_point_status) {
		core_config_lock_point_ctrl(false);
	}
	if (ipd->headset_status) {
		core_config_headset_ctrl(true);
	}
out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}
#ifndef HOST_DOWNLOAD
    if (!ERR_ALLOC_MEM(pfile)) {
	    filp_close(pfile, NULL);
    }
	ipio_kfree((void **)&hex_buffer);
#endif
	//ipio_kfree((void **)&flash_fw);
	if (!ERR_ALLOC_MEM(flash_fw)) {
		vfree(flash_fw);
		flash_fw = NULL;
	}
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	return res;
}

int core_firmware_init(void)
{
	int i = 0;
	int j = 0;

	core_firmware = kzalloc(sizeof(*core_firmware), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_firmware)) {
		TPD_INFO("Failed to allocate core_firmware mem, %ld\n", PTR_ERR(core_firmware));
		core_firmware_remove();
		return -ENOMEM;
	}

	core_firmware->hasBlockInfo = false;
	core_firmware->isboot = false;
	core_firmware->enter_mode = -1;

	for (; i < ARRAY_SIZE(ipio_chip_list); i++) {
		if (ipio_chip_list[i] == TP_TOUCH_IC) {
			for (j = 0; j < 4; j++) {
				core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
				core_firmware->new_fw_ver[i] = 0x0;
			}

			if (ipio_chip_list[i] == CHIP_TYPE_ILI7807) {
				core_firmware->max_count = 0xFFFF;
				core_firmware->isCRC = false;
				core_firmware->upgrade_func = tddi_fw_upgrade;
				core_firmware->delay_after_upgrade = 100;
			} else if (ipio_chip_list[i] == CHIP_TYPE_ILI9881) {
				core_firmware->max_count = 0x1FFFF;
				core_firmware->isCRC = true;	
			#ifdef HOST_DOWNLOAD
				core_firmware->upgrade_func = host_download;
			#else
				core_firmware->upgrade_func = tddi_fw_upgrade;
			#endif
				core_firmware->delay_after_upgrade = 200;
			}
			return 0;
		}
	}

	TPD_INFO("Can't find this chip in support list\n");
	return 0;
}

void core_firmware_remove(void)
{
	TPD_INFO("Remove core-firmware members\n");
	ipio_kfree((void **)&core_firmware);
}
