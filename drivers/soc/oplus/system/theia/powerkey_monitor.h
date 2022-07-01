/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/
#ifndef __POWERKEY_MONITOR_H_
#define __POWERKEY_MONITOR_H_

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <soc/oplus/system/oplus_bscheck.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_ARM
#include <linux/sched.h>
#else
#include <linux/wait.h>
#endif
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched/debug.h>
#include <linux/nmi.h>
#include <soc/oplus/system/boot_mode.h>
#if IS_MODULE(CONFIG_OPLUS_FEATURE_THEIA)
#include <linux/sysrq.h>
#endif

#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <soc/oplus/system/oplus_bscheck.h>
#include <soc/oplus/system/oplus_brightscreen_check.h>

#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_ARM
#include <linux/sched.h>
#else
#include <linux/wait.h>
#endif
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched/debug.h>
#include <linux/nmi.h>
#include <soc/oplus/system/boot_mode.h>
#if IS_MODULE(CONFIG_OPLUS_FEATURE_THEIA)
#include <linux/sysrq.h>
#endif

#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif

#include <linux/sched/signal.h>
#include <soc/oplus/system/oplus_signal.h>

#define PWKKEY_DCS_TAG                      "CriticalLog"
#define PWKKEY_DCS_EVENTID                  "Theia"
#define PWKKEY_BLACK_SCREEN_DCS_LOGTYPE     "black_screen_monitor"
#define PWKKEY_BRIGHT_SCREEN_DCS_LOGTYPE    "bright_screen_monitor"

#define BRIGHT_STATUS_INIT                1
#define BRIGHT_STATUS_INIT_FAIL         2
#define BRIGHT_STATUS_INIT_SUCCEES        3
#define BRIGHT_STATUS_CHECK_ENABLE         4
#define BRIGHT_STATUS_CHECK_DISABLE     5
#define BRIGHT_STATUS_CHECK_DEBUG         6

struct bright_data{
    int is_panic;
    int status;
    int blank;
    int get_log;
    unsigned int timeout_ms;
    unsigned int error_count;
    struct notifier_block fb_notif;
    struct timer_list timer;
    struct work_struct error_happen_work;
    char error_id[64]; /*format: systemserver_pid:time_sec:time_usec*/
};

#define BLACK_STATUS_INIT                 1
#define BLACK_STATUS_INIT_FAIL             2
#define BLACK_STATUS_INIT_SUCCEES        3
#define BLACK_STATUS_CHECK_ENABLE         4
#define BLACK_STATUS_CHECK_DISABLE         5
#define BLACK_STATUS_CHECK_DEBUG         6

struct black_data{
    int is_panic;
    int status;
    int blank;
    int get_log;
    unsigned int timeout_ms;
    unsigned int error_count;
    struct notifier_block fb_notif;
    struct timer_list timer;
    struct work_struct error_happen_work;
    char error_id[64]; /*format: systemserver_pid:time_sec:time_usec*/
};

extern struct bright_data g_bright_data;
extern struct black_data g_black_data;

int bright_screen_check_init(void);
void bright_screen_exit(void);
int black_screen_check_init(void);
void black_screen_exit(void);
void theia_pwk_stage_start(char* reason);
void theia_pwk_stage_end(char* reason);
void send_black_screen_dcs_msg(void);
void send_bright_screen_dcs_msg(void);
bool is_powerkey_check_valid(void);
ssize_t get_last_pwkey_stage(char *buf);
ssize_t get_pwkey_stages(char *buf);
void record_stage(const char *buf);
bool is_valid_systemserver_pid(int pid);
int get_systemserver_pid(void);
void doPanic(void);
#endif