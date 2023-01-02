/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
###############################################################################
## File: phoenix_base.h
## Description : add for project phoenix(hang), PHOENIX_PROJECT
##
##
##
## Version:  1.0
##    Date:  2018/11/15
##  Author:  hukun
## ----------------- Revision History: ----------------------
## <author>       <data>           <desc>
## Kun Hu         2018/11/15       create this file
################################################################################
*/

#ifndef PHOENIX_BASE_H

//stage kernel
#define KERNEL_MM_INIT_DONE                               "KERNEL_MM_INIT_DONE"
#define KERNEL_LOCAL_IRQ_ENABLE                           "KERNEL_LOCAL_IRQ_ENABLE"
#define KERNEL_DELAYACCT_INIT_DONE                        "KERNEL_DELAYACCT_INIT_DONE"
#define KERNEL_DRIVER_INIT_DONE                           "KERNEL_DRIVER_INIT_DONE"
#define KERNEL_DO_INITCALLS_DONE                          "KERNEL_DO_INITCALLS_DONE"
#define KERNEL_DO_BASIC_SETUP_DONE                        "KERNEL_DO_BASIC_SETUP_DONE"
#define KERNEL_INIT_DONE                                  "KERNEL_INIT_DONE"

//stage native
#define NATIVE_INIT_START                                 "NATIVE_INIT_START"
#define NATIVE_INIT_DO_MOUNTALL_START                     "NATIVE_INIT_DO_MOUNTALL_START"
#define NATIVE_INIT_DO_MOUNTALL_END                       "NATIVE_INIT_DO_MOUNTALL_END"
#define NATIVE_INIT_COMMAND_RESTORECON_DATA_START         "NATIVE_INIT_COMMAND_RESTORECON_DATA_START"
#define NATIVE_INIT_COMMAND_RESTORECON_DATA_END           "NATIVE_INIT_COMMAND_RESTORECON_DATA_END"
#define NATIVE_INIT_POST_FS                               "NATIVE_INIT_POST_FS"
#define NATIVE_INIT_TRIGGER_POST_FS_DATA                  "NATIVE_INIT_TRIGGER_POST_FS_DATA"
#define NATIVE_INIT_TRGGIER_RESTART_FRAMEWORK             "NATIVE_INIT_TRGGIER_RESTART_FRAMEWORK"
#define NATIVE_FSMGR_CHECKFS_START                        "NATIVE_FSMGR_CHECKFS_START"
#define NATIVE_FSMGR_CHECKFS_END                          "NATIVE_FSMGR_CHECKFS_END"
#define NATIVE_SURFACEFLINGER_START                       "NATIVE_SURFACEFLINGER_START"
#define NATIVE_SF_BOOTANIMATION_START                     "NATIVE_SF_BOOTANIMATION_START"
#define NATIVE_SF_BOOTANIMATION_END                       "NATIVE_SF_BOOTANIMATION_END"
#define NATIVE_VOLD_START                                 "NATIVE_VOLD_START"
#define NATIVE_VOLD_DECRYPT_MASTER_KEY_START              "NATIVE_VOLD_DECRYPT_MASTER_KEY_START"
#define NATIVE_VOLD_DECRYPT_MASTER_KEY_END                "NATIVE_VOLD_DECRYPT_MASTER_KEY_END"
#define NATIVE_VOLD_SCRYPT_KEYMASTER_START                "NATIVE_VOLD_SCRYPT_KEYMASTER_START"
#define NATIVE_VOLD_SCRYPT_KEYMASTER_END                  "NATIVE_VOLD_SCRYPT_KEYMASTER_END"
#define NATIVE_VOLD_CRYPTFS_RESTART_INTERNAL_START        "NATIVE_VOLD_CRYPTFS_RESTART_INTERNAL_START"
#define NATIVE_VOLD_CRYPTFS_RESTART_INTERNAL_END          "NATIVE_VOLD_CRYPTFS_RESTART_INTERNAL_END"
#define NATIVE_BOOT_PROGRESS_START                        "NATIVE_BOOT_PROGRESS_START"

//stage android framework
#define ANDROID_ZYGOTE_PRELOAD_START                      "ANDROID_ZYGOTE_PRELOAD_START"
#define ANDROID_ZYGOTE_PRELOAD_END                        "ANDROID_ZYGOTE_PRELOAD_END"
#define ANDROID_SYSTEMSERVER_INIT_START                   "ANDROID_SYSTEMSERVER_INIT_START"
#define ANDROID_PMS_INIT_START                            "ANDROID_PMS_INIT_START"
#define ANDROID_PMS_SCAN_START                            "ANDROID_PMS_SCAN_START"
#define ANDROID_PMS_SCAN_END                              "ANDROID_PMS_SCAN_END"
#define ANDROID_PMS_DEXOPT_PERSISTPKGS_START              "ANDROID_PMS_DEXOPT_PERSISTPKGS_START"
#define ANDROID_PMS_DEXOPT_PERSISTPKGS_END                "ANDROID_PMS_DEXOPT_PERSISTPKGS_END"
#define ANDROID_PMS_DEXOPT_START                          "ANDROID_PMS_DEXOPT_START"
#define ANDROID_PMS_DEXOPT_END                            "ANDROID_PMS_DEXOPT_END"
#define ANDROID_PMS_READY                                 "ANDROID_PMS_READY"
#define ANDROID_AMS_READY                                 "ANDROID_AMS_READY"
#define ANDROID_SYSTEMSERVER_READY                        "ANDROID_SYSTEMSERVER_READY"
#define ANDROID_AMS_ENABLE_SCREEN                         "ANDROID_AMS_ENABLE_SCREEN"
#define ANDROID_BOOT_COMPLETED                            "ANDROID_BOOT_COMPLETED"
#define PHOENIX_BOOT_COMPLETED                            "PHOENIX_BOOT_COMPLETED"

//error
#define ERROR_HANG_OPLUS                                  "ERROR_HANG_OPLUS"
#define ERROR_CRITICAL_SERVICE_CRASHED_4_TIMES            "ERROR_CRITICAL_SERVICE_CRASHED_4_TIMES"
#define ERROR_KERNEL_PANIC                                "ERROR_KERNEL_PANIC"
#define ERROR_REBOOT_FROM_PANIC_SUCCESS                   "ERROR_REBOOT_FROM_PANIC_SUCCESS"
#define ERROR_NATIVE_REBOOT_INTO_RECOVERY                 "ERROR_NATIVE_REBOOT_INTO_RECOVERY"
#define ERROR_SYSTEM_SERVER_WATCHDOG                      "ERROR_SYSTEM_SERVER_WATCHDOG"
#define ERROR_HWT                                         "ERROR_HWT"
#define ERROR_HW_REBOOT                                   "ERROR_HW_REBOOT"

//action
#define ACTION_SET_BOOTSTAGE                              "SET_BOOTSTAGE"
#define ACTION_SET_BOOTERROR                              "SET_BOOTERROR"
#define CMD_STR_MAX_SIZE                                   256

//signal
#define SIGPHX_HANG                                       (SIGRTMIN + 0x11)

#define DO_NOT_RECOVERY                                   0
#define ONLY_RESTART                                      1
#define RESTART_AND_RECOVERY                              2

#define PHX_KLOG_ERROR(fmt, args...)                \
    do {                        \
        printk(KERN_ERR "[PHOENIX] %s: " fmt,        \
               __func__, ##args);    \
    } while (0)
#define PHX_KLOG_INFO(fmt, args...)                \
    do {                        \
        printk(KERN_INFO "[PHOENIX] %s: " fmt,        \
               __func__, ##args);    \
    } while (0)

typedef struct _phx_baseinfo {
    char stage[96];
    char error[96];
    char happen_time[64];
} phx_baseinfo;

typedef struct _phx_action_mapping {
    char action[CMD_STR_MAX_SIZE];
    void (*map_func)(const char *);
} phx_action_mapping;

// ATTENTION:
// if phoenix project swithoff these function would evaluate to false
// so make a if-else condition when use these function
extern int __weak phx_is_system_boot_completed(void);
extern int __weak phx_is_phoenix_boot_completed(void);
extern int __weak phx_is_system_server_init_start(void);
extern void __weak phx_set_boot_error(const char *error);
extern void __weak phx_set_boot_stage(const char *stage);

//for native command deleiver to kernel
void phx_monit(const char *monitoring_command);

#endif // !PHOENIX_BASE_H
