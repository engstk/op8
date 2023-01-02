/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef ILITEK_COMMON_H
#define ILITEK_COMMON_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <net/sock.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>

#include <linux/namei.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <linux/gpio.h>
#include <linux/spi/spi.h>

#ifdef CONFIG_OF
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#else
#include <linux/earlysuspend.h>
#endif

#include <linux/rtc.h>
#include <linux/syscalls.h>

#include "../../touchpanel_common.h"
#include <soc/oplus/system/oplus_project.h>

/*
 * Relative Driver with Touch IC
 */

/* An Touch IC currently supported by driver */
#define CHIP_TYPE_ILI7807	0x7807
#define CHIP_TYPE_ILI9881	0x9881
#define TP_TOUCH_IC		CHIP_TYPE_ILI9881

#define CHIP_ID_ERR	(-100)

/* A platform currently supported by driver */
#define PT_QCOM	1
#define PT_MTK	2
#define PT_SPRD	3
#define TP_PLATFORM PT_QCOM

/* A interface currently supported by driver */
#define I2C_INTERFACE 1
#define SPI_INTERFACE 2
#define INTERFACE SPI_INTERFACE
/* Driver version */
#define DRIVER_VERSION	"1.0.4.2_for_oplus_test8"

//#define CHECK_REG
//#define CHECK_DDI_REG
/* Driver core type */
#define CORE_TYPE_B		0x00
#define CORE_TYPE_E		0x03

/* Protocol version */
#define PROTOCOL_MAJOR		0x5
#define PROTOCOL_MID		0x4
#define PROTOCOL_MINOR		0x0

/*  Debug messages */
#ifdef BIT
#undef BIT
#endif
#define BIT(x)	(1 << (x))

enum {
	DEBUG_NONE = 0,
	DEBUG_IRQ = BIT(0),
	DEBUG_FINGER_REPORT = BIT(1),
	DEBUG_FIRMWARE = BIT(2),
	DEBUG_CONFIG = BIT(3),
	DEBUG_I2C = BIT(4),
	DEBUG_BATTERY = BIT(5),
	DEBUG_MP_TEST = BIT(6),
	DEBUG_IOCTL = BIT(7),
	DEBUG_NETLINK = BIT(8),
	DEBUG_PARSER = BIT(9),
	DEBUG_GESTURE = BIT(10),
	DEBUG_ALL = ~0,
};

#define ipio_info(fmt, arg...)	\
	pr_info("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);

#define ipio_err(fmt, arg...)	\
	pr_err("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);

#define ipio_debug(level, fmt, arg...)									\
	do {																\
		if (level & ipio_debug_level)									\
		pr_info("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);	\
	} while (0)

#define TPD_DEVICE "ilitek,ili9881h"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE "(%s, %d): " a, __func__, __LINE__, ##arg)
#define TPD_DEBUG(a, arg...)\
			do{\
				if (LEVEL_DEBUG == tp_debug)\
					pr_err("[TP]"TPD_DEVICE "(%s, %d): " a, __func__, __LINE__, ##arg);\
			}while(0)
		
#define TPD_DETAIL(a, arg...)\
			do{\
				if (LEVEL_BASIC != tp_debug)\
					pr_err("[TP]"TPD_DEVICE "(%s, %d): " a, __func__, __LINE__, ##arg);\
			}while(0)
		
#define TPD_DEBUG_NTAG(a, arg...)\
			do{\
				if (tp_debug)\
					printk(a, ##arg);\
			}while(0)


/* Distributed to all core functions */
extern uint32_t ipio_debug_level;
extern uint32_t ipio_chip_list[2];

/* Macros */
#define CHECK_EQUAL(X, Y) ((X == Y) ? 0 : -1)
#define ERR_ALLOC_MEM(X)	((IS_ERR(X) || X == NULL) ? 1 : 0)
#define USEC	1
#define MSEC	(USEC * 1000)


#define ILITEK_TOUCH_ESD_CHECK_PERIOD  1000

/* The size of firmware upgrade */
#define MAX_HEX_FILE_SIZE			(160*1024)
#define MAX_FLASH_FIRMWARE_SIZE		(256*1024)
#define MAX_IRAM_FIRMWARE_SIZE		(60*1024)
#if (INTERFACE == SPI_INTERFACE)
#define HOST_DOWNLOAD
#endif
#ifdef HOST_DOWNLOAD
	#define MAX_AP_FIRMWARE_SIZE		(64*1024)
	#define MAX_DLM_FIRMWARE_SIZE		(8*1024)
	#define MAX_MP_FIRMWARE_SIZE		(64*1024)
	#define MAX_GESTURE_FIRMWARE_SIZE	(8*1024)
	#define DLM_START_ADDRESS 			0x20610
	#define DLM_HEX_ADDRESS 			0x10000
	#define MP_HEX_ADDRESS	 			0x13000
	#define SPI_UPGRADE_LEN	 			2048
	extern int core_firmware_boot_host_download(void);
#endif


/* ILI7807 Series */
enum ili7881_types
{
	ILI7807_TYPE_F_AA = 0x0000,
	ILI7807_TYPE_F_AB = 0x0001,
	ILI7807_TYPE_H = 0x1100
};

#define ILI7807_SLAVE_ADDR		0x41
#define ILI7807_ICE_MODE_ADDR	0x181062
#define ILI7807_PID_ADDR		0x4009C
#define ILI7808_WDT_ADDR		0x5100C

/* ILI9881 Series */
enum ili9881_types
{
	ILI9881_TYPE_F = 0x0F,
	ILI9881_TYPE_H = 0x11
};


typedef enum {
    ILI_RAWDATA,    //raw data
    ILI_DIFFDATA,   //diff data
    ILI_BASEDATA,   //baseline data
}DEBUG_READ_TYPE;

#define ILI9881_SLAVE_ADDR		0x41
#define ILI9881_ICE_MODE_ADDR	0x181062
#define ILI9881_PID_ADDR		0x4009C
#define ILI9881_WDT_ADDR		0x5100C


#define SUCCESS 				0
#define UPDATE_FAIL 			-1
#define CHECK_RECOVER 			-2

/*
 * Other settings
 */
#define CSV_PATH			"/sdcard/ILITEK"
#define INI_NAME_PATH		"tp/18031/LIMIT_NF_ILI9881H_INNOLUX.ini"
#define UPDATE_FW_PATH		"/mnt/sdcard/ILITEK_FW"
#define POWER_STATUS_PATH 	"/sys/class/power_supply/battery/status"
#define CHECK_BATTERY_TIME  2000
#define VDD_VOLTAGE			1800000
#define VDD_I2C_VOLTAGE		1800000

 /* define the width and heigth of a screen. */
#define TOUCH_SCREEN_X_MIN 0
#define TOUCH_SCREEN_Y_MIN 0
#define TOUCH_SCREEN_X_MAX 720
#define TOUCH_SCREEN_Y_MAX 1520

/* define the range on panel */
#define TPD_HEIGHT 2048
#define TPD_WIDTH 2048

/* How many numbers of touch are supported by IC. */
#define MAX_TOUCH_NUM	10

/* Linux multiple touch protocol, either B type or A type. */
#define MT_B_TYPE

/* Enable the support of regulator power. */
//#define REGULATOR_POWER_ON

/* Either an interrupt event handled by kthread or work queue. */
#define USE_KTHREAD

/* Enable DMA with I2C. */
/* #define I2C_DMA */

/* Split the length written to or read from IC via I2C. */
/* #define I2C_SEGMENT */

/* Be able to upgrade fw at boot stage */
//#define BOOT_FW_UPGRADE 

/* Check battery's status in order to avoid some effects from charge. */
/* #define BATTERY_CHECK */

static inline void ipio_kfree(void **mem)
{
	if(*mem != NULL) {
		kfree(*mem);
		*mem = NULL;
	}
}

static inline void ipio_vfree(void **mem)
{
	if(*mem != NULL) {
		vfree(*mem);
		*mem = NULL;
	}
}

extern int katoi(char *string);
extern int str2hex(char *str);

#endif /* __ilitek_common.h */
