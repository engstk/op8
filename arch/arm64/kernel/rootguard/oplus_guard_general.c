// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved..
* VENDOR_EDIT
* File       : oplus_guard_general.c
* Description: some common function for root guard
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#ifdef CONFIG_OPLUS_SECURE_GUARD
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>


#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
enum{
        MSM_BOOT_MODE__NORMAL,
        MSM_BOOT_MODE__FASTBOOT,
        MSM_BOOT_MODE__RECOVERY,
        MSM_BOOT_MODE__FACTORY,
        MSM_BOOT_MODE__RF,
        MSM_BOOT_MODE__WLAN,
        MSM_BOOT_MODE__MOS,
        MSM_BOOT_MODE__CHARGE,
        MSM_BOOT_MODE__SILENCE,
        MSM_BOOT_MODE__SAU,

        MSM_BOOT_MODE__AGING = 998,
        MSM_BOOT_MODE__SAFE = 999,
};

extern int get_boot_mode(void);
bool is_normal_boot_mode(void)
{
	return MSM_BOOT_MODE__NORMAL == get_boot_mode();
}
#else
#define MTK_NORMAL_BOOT	0
extern unsigned int get_boot_mode(void);
bool is_normal_boot_mode(void)
{
	return MTK_NORMAL_BOOT == get_boot_mode();
}
#endif


enum{
        BOOT_STATE__GREEN,
        BOOT_STATE__ORANGE,
        BOOT_STATE__YELLOW,
        BOOT_STATE__RED,
};


static int __ro_after_init g_boot_state  = BOOT_STATE__GREEN;


bool is_unlocked(void)
{
	return  BOOT_STATE__ORANGE== g_boot_state;
}

static int __init boot_state_init(void)
{
	char * substr = strstr(boot_command_line, "androidboot.verifiedbootstate=");
	if (substr) {
   		substr += strlen("androidboot.verifiedbootstate=");
        if (strncmp(substr, "green", 5) == 0) {
        	g_boot_state = BOOT_STATE__GREEN;
        } else if (strncmp(substr, "orange", 6) == 0) {
       		g_boot_state = BOOT_STATE__ORANGE;
        } else if (strncmp(substr, "yellow", 6) == 0) {
        	g_boot_state = BOOT_STATE__YELLOW;
        } else if (strncmp(substr, "red", 3) == 0) {
        	g_boot_state = BOOT_STATE__RED;
       	}
	}

	return 0;
}

static void __exit boot_state_exit()
{
	return ;
}

module_init(boot_state_init);
module_exit(boot_state_exit);
MODULE_LICENSE("GPL");
#endif/*OPLUS_GUARD_GENERAL_H_*/
