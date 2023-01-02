// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
###############################################################################
## File: phoenix_watchdog.c
## Description : add for project phenix(hang )
##
## Version:  1.0
################################################################################
*/
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/syscalls.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/of_fdt.h>
#include "oplus_phoenix.h"

#define DELAY_TIME                      30
#define MAX_CMD_LENGTH                  32
#define DEFAULT_PHX_WD_PET_TIME         (60 * 4)

static int hang_oplus_main_on = 1;   //default on
int hang_oplus_recovery_method = RESTART_AND_RECOVERY;
static int phx_hlos_wd_pet_time = DEFAULT_PHX_WD_PET_TIME;
static char buildvariant[20];
static char hardwaretype[16];

static int __init phoenix_get_hardwaretype(char *line)
{
    strlcpy(hardwaretype, line, sizeof(hardwaretype));
    return 1;
}

__setup("androidboot.hardware=", phoenix_get_hardwaretype);

static int __init phoenix_get_buildvariant(char *line)
{
    strlcpy(buildvariant, line, sizeof(buildvariant));
    return 1;
}

__setup("buildvariant=", phoenix_get_buildvariant);

static inline bool is_userdebug(void)
{
    static const char typeuserdebug[]  = "userdebug";

    return !strncmp(buildvariant, typeuserdebug, sizeof(typeuserdebug));
}

static void reinitialze_pet_time_for_debug_build(void)
{
    if (is_userdebug())
    {
        phx_hlos_wd_pet_time = 60 * 5;
    }
    if (AGING == get_eng_version())
    {
        phx_hlos_wd_pet_time = 60 * 10; //aging test version
    }

#ifdef CONFIG_MEMLEAK_DETECT_THREAD
    if (AGING != get_eng_version())
	    phx_hlos_wd_pet_time += 60 * 5;
#endif
}

static int __init hang_oplus_main_on_init(char *str)
{
    get_option(&str,&hang_oplus_main_on);

    pr_info("hang_oplus_main_on %d\n", hang_oplus_main_on);

    return 1;
}
__setup("phx_rus_conf.main_on=", hang_oplus_main_on_init);

static int __init hang_oplus_recovery_method_init(char *str)
{
    get_option(&str,&hang_oplus_recovery_method);

    pr_info("hang_oplus_recovery_method %d\n", hang_oplus_recovery_method);

    return 1;
}
__setup("phx_rus_conf.recovery_method=", hang_oplus_recovery_method_init);

static int __init phx_hlos_wd_pet_time_init(char *str)
{
    get_option(&str,&phx_hlos_wd_pet_time);

    pr_info("phx_hlos_wd_pet_time %d\n", phx_hlos_wd_pet_time);

    return 1;
}
__setup("phx_rus_conf.kernel_time=", phx_hlos_wd_pet_time_init);

int is_phoenix_enable(void)
{
    return hang_oplus_main_on;
}

EXPORT_SYMBOL(is_phoenix_enable);

int phx_is_long_time(void)
{
    struct file *opfile;
    ssize_t size;
    loff_t offsize;
    char data_info[16] = {'\0'};
    mm_segment_t old_fs;

    opfile = filp_open("/proc/opbootfrom", O_RDONLY, 0444);
    if (IS_ERR(opfile)) {
        PHX_KLOG_ERROR("open /proc/opbootfrom error:\n");
        return -1;
    }

    offsize = 0;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    size = vfs_read(opfile,data_info,sizeof(data_info),&offsize);
    if (size < 0) {
        PHX_KLOG_ERROR("data_info %s size %ld", data_info, size);
        set_fs(old_fs);
        return -1;
    }
    set_fs(old_fs);
    filp_close(opfile,NULL);

    if (strncmp(data_info, "normal", 6) == 0) {
        return 0;
    }
    return 1;
}

static int phx_is_boot_into_native(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
    return !ksys_access("/proc/opbootfrom", 0);
#else
    return !sys_access("/proc/opbootfrom", 0);
#endif
}

static int phx_is_gsi_test(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	return ksys_access("/system/system_ext/etc/init/init.oplus.rootdir.rc", 0);
#else
	return sys_access("/system/system_ext/etc/init/init.oplus.rootdir.rc", 0);
#endif
}

static int phx_pet(void)
{
    if(phx_is_long_time())
    {
        schedule_timeout_interruptible(phx_hlos_wd_pet_time * HZ);
    }

    PHX_KLOG_INFO("phoenix watchdog pet!\n");

    if(!phx_is_system_boot_completed())
    {
        // check if this version is google gsi version
        if(phx_is_boot_into_native() && phx_is_gsi_test())
        {
            return 0;
        }

        if (is_userdebug() || (get_eng_version() == AGING))
        {
            PHX_KLOG_INFO("will not set ERROR_HANG_OPLUS with userdebug/aging version\n");
            return 0;
        }
        phx_set_boot_error(ERROR_HANG_OPLUS);
    }
    return 0;

}

//start phoenix high level os watchdog
static int phoenix_watchdog_kthread(void *dummy)
{
    schedule_timeout_interruptible(phx_hlos_wd_pet_time * HZ);
    phx_pet();
    return 0;
}

static int phx_is_normal_mode_qcom(void)
{
    int i;
    char *substr;
    char boot_mode[MAX_CMD_LENGTH + 1];
    const char * const normal_boot_mode_list[] = {"normal", "reboot", "kernel"};
    substr =  strstr(boot_command_line, "androidboot.mode=");
    if (substr)
    {
        substr += strlen("androidboot.mode=");
        for (i=0; substr[i] != ' ' && i < MAX_CMD_LENGTH && substr[i] != '\0'; i++)
        {
            boot_mode[i] = substr[i];
        }
        boot_mode[i] = '\0';

        if( MSM_BOOT_MODE__NORMAL != get_boot_mode())
        {
            return 0;
        }

        for( i = 0; i < ARRAY_SIZE(normal_boot_mode_list); i++)
        {
            if(!strcmp(boot_mode, normal_boot_mode_list[i]))
            {
                return 1;
            }
        }
    }
    return 0;
}

//copy mtk_boot_common.h
#define NORMAL_BOOT 0
#define ALARM_BOOT 7
static int phx_is_normal_mode_mtk(void)
{
    int mtk_boot_mode = 0;

    mtk_boot_mode = get_boot_mode();
    PHX_KLOG_INFO("mtk_boot_mode: %d\n", mtk_boot_mode);

    if((mtk_boot_mode == NORMAL_BOOT) || (mtk_boot_mode == ALARM_BOOT)    ) {
        return 1;
    } else {
        return 0;
    }
}

static int phx_is_qcom_platform(void)
{
    const char *platform_name;
    char *substr;
    int i;
    const char * const qcom_platform_keywords[] = {"Qualcomm", "SDM"};
    static const char qcomhardware[]  = "qcom";

    platform_name = of_flat_dt_get_machine_name();
    for( i = 0; i < ARRAY_SIZE(qcom_platform_keywords); i++)
    {
        substr =  strstr(platform_name, qcom_platform_keywords[i]);
        if(substr)
        {
            PHX_KLOG_INFO("Qcom platform");
            return 1;
        }
    }

    if (!strncmp(hardwaretype, qcomhardware, sizeof(qcomhardware))) {
        PHX_KLOG_INFO("Qcom platform hardware");
        return 1;
    }

    PHX_KLOG_INFO("MTK platform");
    return 0;
}

static int __init phx_is_normal_mode(void)
{
    return phx_is_qcom_platform()?phx_is_normal_mode_qcom():phx_is_normal_mode_mtk();
}

static int __init phoenix_hlos_watchdog_init(void)
{
    int ret = 0;
    PHX_KLOG_INFO("phoenix hlos watchdog: %s\n", hang_oplus_main_on ? "on" : "off");
    if(hang_oplus_main_on && phx_is_normal_mode())
    {
        reinitialze_pet_time_for_debug_build();
        PHX_KLOG_INFO("phoenix hlos watchdog pet time: %d\n", phx_hlos_wd_pet_time);
        kthread_run(phoenix_watchdog_kthread, NULL, "phoenix_hlos_watchdog");
    }

    return ret;
}
arch_initcall_sync(phoenix_hlos_watchdog_init);

MODULE_DESCRIPTION("PHOENIX HLOS WATCHDOG");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bright.Zhang <bright.zhang>");
