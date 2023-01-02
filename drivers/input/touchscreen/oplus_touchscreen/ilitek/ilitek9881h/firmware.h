/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FIRMWARE_H
#define __FIRMWARE_H

struct core_firmware_data {
	uint8_t new_fw_ver[4];
	uint8_t old_fw_ver[4];

	uint32_t start_addr;
	uint32_t end_addr;
	uint32_t checksum;
	uint32_t crc32;
	uint32_t core_version;

	uint32_t update_status;
	uint32_t max_count;

	int delay_after_upgrade;

	bool isUpgrading;
	bool isCRC;
	bool isboot;
	bool hasBlockInfo;

	int (*upgrade_func)(bool isIRAM);

	const struct firmware *fw;
	int enter_mode;
	int esd_fail_enter_gesture;
};

extern struct core_firmware_data *core_firmware;

#ifdef BOOT_FW_UPGRADE
extern int core_firmware_boot_upgrade(void);
#endif
/* extern int core_firmware_iram_upgrade(const char* fpath); */
extern int core_firmware_upgrade(const char *, bool isIRAM);
extern int core_firmware_init(void);
extern void core_firmware_remove(void);
#ifdef HOST_DOWNLOAD
extern int core_firmware_get_hostdownload_data(const char *pFilePath);
extern int core_load_gesture_code(void);
extern int core_load_ap_code(void);
extern int core_firmware_get_h_file_data(void);
extern int host_download(bool mode);

#endif
#endif /* __FIRMWARE_H */
