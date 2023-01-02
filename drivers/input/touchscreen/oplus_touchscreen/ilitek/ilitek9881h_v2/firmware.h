/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FIRMWARE_H
#define __FIRMWARE_H

struct core_firmware_data {
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

    int (*upgrade_func)(void *chip_data, bool isIRAM);

    const struct firmware *fw;
    int enter_mode;
    int esd_fail_enter_gesture;
};

extern struct core_firmware_data *core_firmware;

extern int core_firmware_upgrade(void *chip_data, bool isIRAM);
extern int core_firmware_init(void);
extern void core_firmware_remove(void);
#ifdef HOST_DOWNLOAD
extern int core_firmware_get_hostdownload_data(void);
extern int core_load_gesture_code(void);
#if 0
extern int core_load_ap_code(void);
#endif
extern int core_firmware_get_h_file_data(void);
extern int host_download(void *chip_data, bool mode);
#endif
#endif /* __FIRMWARE_H */
