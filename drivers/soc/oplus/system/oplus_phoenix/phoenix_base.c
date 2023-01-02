// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
###############################################################################
## File: boologhelper.te
## Description : add for project phoenix(hang)
##               PHOENIX_PROJECT
## Version:  1.0
################################################################################
*/
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include "oplus_phoenix.h"
#include "op_bootprof.h"

#define PHX_KE_MARK                                   "PHX_KE_HAPPEND"
#define TIME_FORMAT                                   "yyyy-mm-dd HH:mm:ss"

extern int is_phoenix_enable(void);
extern void phx_log_dump(const char * phx_error);

static void phx_handle_hang_oplus(const char *);
static void phx_handle_critical_service_crash_4_times(const char *);
static void phx_handle_kernel_panic(const char *);
static void phx_handle_hwt(const char *);
static void phx_handle_reboot_from_panic_success_log(const char *);
static void phx_handle_system_server_watchdog(const char *);
static void phx_handle_native_reboot_into_recovery(const char *);


static void phx_choose_action_handler(const char *, const phx_action_mapping *, size_t);


static phx_action_mapping monitor_action_mapping[] = {
    {ACTION_SET_BOOTSTAGE, phx_set_boot_stage},
    {ACTION_SET_BOOTERROR, phx_set_boot_error},
};

static phx_action_mapping errno_handle_action_mapping[] = {
    {ERROR_CRITICAL_SERVICE_CRASHED_4_TIMES, phx_handle_critical_service_crash_4_times},
    {ERROR_KERNEL_PANIC, phx_handle_kernel_panic},
    {ERROR_HWT, phx_handle_hwt},
    {ERROR_REBOOT_FROM_PANIC_SUCCESS, phx_handle_reboot_from_panic_success_log},
    {ERROR_HANG_OPLUS, phx_handle_hang_oplus},
    {ERROR_SYSTEM_SERVER_WATCHDOG, phx_handle_system_server_watchdog},
    {ERROR_NATIVE_REBOOT_INTO_RECOVERY, phx_handle_native_reboot_into_recovery},
};

static int is_system_boot_completed = 0;
static int is_phoenix_boot_completed = 0;
static int is_system_server_init_start = 0;
static int is_filesystem_prepared = 0;
static phx_baseinfo *phx_curr_info = 0;

static void phx_get_local_time(char* strtime, size_t strtime_length)
{
    struct timespec ts;
    struct rtc_time tm;
    getnstimeofday(&ts);
    rtc_time_to_tm(ts.tv_sec, &tm);
    snprintf(strtime, strtime_length, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
        tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void phx_update_current_stage(const char *stage)
{
    if(!phx_curr_info)
        return;
    snprintf(phx_curr_info->stage, sizeof(phx_curr_info->stage), "%s", stage);
}

static void phx_update_current_error(const char *error)
{
    if(!phx_curr_info)
        return;
    snprintf(phx_curr_info->error, sizeof(phx_curr_info->error), "%s", error);
}

static void phx_update_happen_time(const char *happen_time)
{
    if(!phx_curr_info)
        return;
    snprintf(phx_curr_info->happen_time, sizeof(phx_curr_info->happen_time), "%s", happen_time);
}

static void phx_klog_bootstage(const char *stage)
{
    char stage_pnt[CMD_STR_MAX_SIZE] = {0};
    snprintf(stage_pnt, CMD_STR_MAX_SIZE, "STAGE_%s", stage);
    op_log_boot(stage_pnt);
    phx_update_current_stage(stage);
    PHX_KLOG_INFO("%s\n", stage);
}

static void phx_klog_booterror(const char * errno, const char *time)
{
    char error_pnt[CMD_STR_MAX_SIZE] = {0};
    snprintf(error_pnt, CMD_STR_MAX_SIZE, "ERROR_%s", errno);
    // panic will diable local interrupts, msleep behaviour is not permissive
    if(strcmp(errno, ERROR_KERNEL_PANIC))
    {
        op_log_boot(error_pnt);
    }
    phx_update_current_error(errno);
    phx_update_happen_time(time);
    PHX_KLOG_INFO("%s, happen_time: %s\n", errno, time);
}

static void phx_handle_critical_service_crash_4_times(const char *happen_time)
{
    phx_klog_booterror(ERROR_CRITICAL_SERVICE_CRASHED_4_TIMES, happen_time);
}

static void phx_handle_kernel_panic(const char *happen_time)
{
    phx_klog_booterror(ERROR_KERNEL_PANIC, happen_time);
    PHX_KLOG_INFO("%s\n", PHX_KE_MARK);
}

static void phx_handle_hwt(const char *happen_time)
{
    phx_klog_booterror(ERROR_HWT, happen_time);
    PHX_KLOG_INFO("%s\n", PHX_KE_MARK);
}

static void phx_handle_reboot_from_panic_success_log(const char *happen_time)
{
    phx_klog_booterror(ERROR_REBOOT_FROM_PANIC_SUCCESS, happen_time);
}

static void phx_handle_system_server_watchdog(const char *happen_time)
{
    phx_klog_booterror(ERROR_SYSTEM_SERVER_WATCHDOG, happen_time);
}

static void phx_handle_native_reboot_into_recovery(const char *happen_time)
{
    phx_klog_booterror(ERROR_NATIVE_REBOOT_INTO_RECOVERY, happen_time);
}

static void phx_handle_hang_oplus(const char *happen_time)
{
    phx_klog_booterror(ERROR_HANG_OPLUS, happen_time);
    phx_log_dump(ERROR_HANG_OPLUS);
}

static void phx_choose_action_handler(const char * cmd, const phx_action_mapping * action_map_array,
    size_t action_map_array_size)
{
    char action[CMD_STR_MAX_SIZE] = {0};
    char *p = strchr(cmd, '@');
    size_t i = 0;
    if(NULL == p)
    {
        PHX_KLOG_INFO("invalid command\n");
        return;
    }
    strncpy(action, cmd, p - cmd);
    for(; i< action_map_array_size; i++)
    {
        if(!strcmp(action_map_array[i].action, action))
        {
            action_map_array[i].map_func(p + 1);
            return;
        }
    }
    PHX_KLOG_INFO("undefined command\n");
    return;
}

static void phx_strip_line_break(const char * line, char *result)
{
    size_t line_length = strlen(line);
    size_t copy_length = strchr(line, '\n') != NULL ? line_length - 1 : line_length;
    strncpy(result, line, copy_length);
}

void phx_get_base_info(phx_baseinfo ** baseinfo)
{
    *baseinfo = phx_curr_info;
}

EXPORT_SYMBOL(phx_get_base_info);

int phx_is_system_server_init_start(void)
{
	return is_system_server_init_start;
}

EXPORT_SYMBOL(phx_is_system_server_init_start);

int phx_is_system_boot_completed(void)
{
    return is_system_boot_completed;
}

EXPORT_SYMBOL(phx_is_system_boot_completed);

int phx_is_phoenix_boot_completed(void)
{
    return is_phoenix_boot_completed;
}

EXPORT_SYMBOL(phx_is_phoenix_boot_completed);

int phx_is_filesystem_ready(void)
{
    return is_filesystem_prepared;
}

EXPORT_SYMBOL(phx_is_filesystem_ready);

void phx_set_boot_stage(const char *stage)
{
    if(!is_phoenix_enable())
    {
        //phoenix off
        return;
    }
    if(!is_phoenix_boot_completed)
        phx_klog_bootstage(stage);

	if (!strcmp(stage, ANDROID_SYSTEMSERVER_INIT_START)) {
		is_system_server_init_start = 1;
	}

    if(!strcmp(stage, ANDROID_BOOT_COMPLETED))
        is_system_boot_completed = 1;

    if(!is_filesystem_prepared && !strcmp(stage, NATIVE_INIT_POST_FS))
    {
        is_filesystem_prepared = 1;
    }

    if(!is_phoenix_boot_completed && !strcmp(stage, PHOENIX_BOOT_COMPLETED))
    {
        is_phoenix_boot_completed = 1;
    }
}

EXPORT_SYMBOL(phx_set_boot_stage);

void phx_set_boot_error(const char *errno)
{
    int i = 0;
    char time_pattern[ARRAY_SIZE(TIME_FORMAT) + 1] = {0};
    if(!is_phoenix_enable())
    {
        //phoenix off
        return;
    }
    phx_get_local_time(time_pattern, ARRAY_SIZE(time_pattern));
    for(; i < ARRAY_SIZE(errno_handle_action_mapping); i++)
    {
        if(!strcmp(errno_handle_action_mapping[i].action, errno))
        {
            errno_handle_action_mapping[i].map_func(time_pattern);
            return;
        }
    }
}

EXPORT_SYMBOL(phx_set_boot_error);

void phx_monit(const char *monitoring_command)
{
    char stripped_command[CMD_STR_MAX_SIZE] = {0};
    if(!is_phoenix_enable())
    {
        //phoenix off
        return;
    }
    if(strlen(monitoring_command) > CMD_STR_MAX_SIZE - 1)
    {
        PHX_KLOG_INFO("monitor command too long\n");
        return;
    }
    phx_strip_line_break(monitoring_command, stripped_command);
    phx_choose_action_handler(stripped_command, monitor_action_mapping, ARRAY_SIZE(monitor_action_mapping));
}

EXPORT_SYMBOL(phx_monit);

static int __init phoenix_base_init(void)
{
    phx_curr_info = (phx_baseinfo *)kmalloc(sizeof(phx_baseinfo), GFP_KERNEL);
    if(!phx_curr_info)
    {
        PHX_KLOG_ERROR("kmalloc to phx_curr_info failed\n");
        return -1;
    }
    return 0;
}

postcore_initcall(phoenix_base_init);

MODULE_DESCRIPTION("phoenix base");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kun.Hu <hukun>");
