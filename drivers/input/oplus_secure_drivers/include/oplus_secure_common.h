/************************************************************************************
** File: - vendor/oplus/secure/common/bsp$ vim drivers/include/oplus_secure_common.h
** VENDOR_EDIT
** Copyright (C), 2008-2016, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**          fingerprint compatibility configuration
**
** Version: 1.0
** Date created: 18:03:11,23/07/2016
**
** --------------------------- Revision History: --------------------------------
**  <author>                 <data>                        <desc>
**  Bin.Li       2017/11/17       create the file
************************************************************************************/

#ifndef _OPLUS_SECURE_COMMON_H_
#define _OPLUS_SECURE_COMMON_H_

#include <linux/platform_device.h>

#define ENGINEER_MENU_SELECT_MAXLENTH   20
enum {
        SECURE_OK,
        SECURE_ERROR_GPIO,
        SECURE_ERROR_GENERAL,
} ;

typedef enum {
        SECURE_BOOT_OFF,
        SECURE_BOOT_ON,
        SECURE_BOOT_ON_STAGE_1,
        SECURE_BOOT_ON_STAGE_2,
        SECURE_BOOT_UNKNOWN,
} secure_type_t;

typedef enum {
        SECURE_DEVICE_SN_BOUND_OFF,
        SECURE_DEVICE_SN_BOUND_ON,
        SECURE_DEVICE_SN_BOUND_UNKNOWN,
} secure_device_sn_bound_state_t;

struct secure_data {
        struct device *dev;
        uint32_t sec_reg_num;
        uint32_t sec_en_anti_reg;
        uint32_t sec_override1_reg;
        uint32_t override1_en_value;
};

#endif  /*_OPLUS_SECURE_COMMON_H_*/
