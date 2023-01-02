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

#include <linux/rtc.h>
#include <linux/syscalls.h>

#include "../../touchpanel_common.h"
#include <soc/oplus/oplus_project.h>

/*
 * Relative Driver with Touch IC
 */

/* An Touch IC currently supported by driver */
#define CHIP_TYPE_ILI9881      0x9881
#define TP_TOUCH_IC            CHIP_TYPE_ILI9881

#define CHIP_ID_ERR            (-100)

/* Driver version */
#define DRIVER_VERSION    "1.0.4.2_for_oplus_test8"

/* Driver core type */
#define CORE_TYPE_B            0x00
#define CORE_TYPE_E            0x03

/* Protocol version */
#define PROTOCOL_MAJOR         0x5
#define PROTOCOL_MID           0x4
#define PROTOCOL_MINOR         0x0

/*  Debug messages */
#ifdef BIT
#undef BIT
#endif
#define BIT(x)                 (1 << (x))

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
/*this debug value is just for ili debug*/
#define ipio_info(fmt, arg...)    \
    pr_info("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);

#define ipio_err(fmt, arg...)    \
    pr_err("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);

#define ipio_debug(level, fmt, arg...)                                    \
    do {                                                                \
        if (level & ipio_debug_level)                                    \
        pr_info("ILITEK: (%s, %d): " fmt, __func__, __LINE__, ##arg);    \
    } while (0)

/*this debug value is just for oplus debug*/
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

/* Macros */
#define CHECK_EQUAL(X, Y)              ((X == Y) ? 0 : -1)
#define ERR_ALLOC_MEM(X)               ((IS_ERR(X) || X == NULL) ? 1 : 0)
#define USEC                           1
#define MSEC                           (USEC * 1000)

#define ILITEK_TOUCH_ESD_CHECK_PERIOD  1000

/* The size of firmware upgrade */
#define MAX_HEX_FILE_SIZE              (160*1024)
#define MAX_FLASH_FIRMWARE_SIZE        (256*1024)
#define MAX_IRAM_FIRMWARE_SIZE         (60*1024)
/*spi interface*/
/*for no flash ic*/
#define HOST_DOWNLOAD

#ifdef HOST_DOWNLOAD
    #define MAX_AP_FIRMWARE_SIZE         (64*1024)
    #define MAX_DLM_FIRMWARE_SIZE        (8*1024)
    #define MAX_MP_FIRMWARE_SIZE         (64*1024)
    #define MAX_GESTURE_FIRMWARE_SIZE    (8*1024)
    #define DLM_START_ADDRESS             0x20610
    #define DLM_HEX_ADDRESS               0x10000
    #define MP_HEX_ADDRESS                0x13000
    #define SPI_UPGRADE_LEN               2048
    /*max len for array CTPM_FW*/
    #define MAX_CTPM_FW_LEN              (160 * 1024)
    #define MAX_FLASH_FW_LEN             (256 * 1024)
    extern int core_firmware_boot_host_download(void *chip_data);
#endif

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

#define ILI9881_SLAVE_ADDR        0x41
#define ILI9881_ICE_MODE_ADDR     0x181062
#define ILI9881_PID_ADDR          0x4009C
#define ILI9881_WDT_ADDR          0x5100C


#define SUCCESS                   0
#define UPDATE_FAIL               -1
#define CHECK_RECOVER             -2

/*
 * Other settings
 */
#define CSV_PATH               "/sdcard/ILITEK"
#define INI_NAME_PATH          "tp/18031/LIMIT_NF_ILI9881H_INNOLUX.ini"
#define UPDATE_FW_PATH         "/mnt/sdcard/ILITEK_FW"
#define POWER_STATUS_PATH      "/sys/class/power_supply/battery/status"
#define CHECK_BATTERY_TIME     2000
#define VDD_VOLTAGE            1800000
#define VDD_I2C_VOLTAGE        1800000

 /* define the width and heigth of a screen. */
#define TOUCH_SCREEN_X_MIN     0
#define TOUCH_SCREEN_Y_MIN     0
#define TOUCH_SCREEN_X_MAX     720
#define TOUCH_SCREEN_Y_MAX     1520

/* define the range on panel */
#define TPD_HEIGHT             2048
#define TPD_WIDTH              2048

/* How many numbers of touch are supported by IC. */
#define MAX_TOUCH_NUM          10

/* Linux multiple touch protocol, either B type or A type. */
#define MT_B_TYPE

/* Gesture buffer length*/
#define GESTURE_INFO_LENGTH    170

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

struct ilitek_chip_data_9881h {
    bool isEnableIRQ;
    bool Mp_test_data_ready;//add for mp test flag

    uint32_t chip_id;
    int isr_gpio;//irq number

    int delay_time_high;
    int delay_time_low;
    int edge_delay;

    struct mutex plat_mutex;
    spinlock_t plat_spinlock;

    /* Sending report data to users for the debug */
    bool debug_node_open;
    int debug_data_frame;
    unsigned char ** debug_buf;
    wait_queue_head_t inq;
    struct mutex ilitek_debug_mutex;
    struct mutex ilitek_debug_read_mutex;

    struct spi_device *spi;

    /*support oplus struce*/
    bool fw_edge_limit_support;
    bool edge_limit_status;
    bool headset_status;
    bool plug_status;
    bool lock_point_status;
    bool oplus_read_debug_data;

    bool esd_check_enabled;
    unsigned long irq_timer;

    int esd_retry;
    int apk_upgrade;
    int common_reset;

    int resolution_x;
    int resolution_y;
    int touch_direction;

    char *fw_name;
    char *test_limit_name;
    char *fw_version;
    int *oplus_debug_buf;
    tp_dev tp_type;

    struct hw_resource *hw_res;
    struct touchpanel_data *ts;
    struct firmware tp_firmware;
	struct firmware_headfile *p_firmware_headfile;/*for ini firmware*/
};

extern struct ilitek_chip_data_9881h *ipd;

/* exported from platform.c */
extern void ilitek_platform_disable_irq(void);
extern void ilitek_platform_enable_irq(void);
extern int ilitek_platform_tp_hw_reset(bool isEnable);

/* exported from userspsace.c */
extern void netlink_reply_msg(void *raw, int size);
extern int ilitek_proc_init(struct touchpanel_data *ts);
extern int ilitek_create_proc_for_oplus(struct touchpanel_data *ts);
extern void ilitek_proc_remove(void);
extern int ilitek_reset(void *chip_data);

/* spi interface */
#define SPI_WRITE              0X82
#define SPI_READ               0X83
extern int core_spi_write(uint8_t *pBuf, uint16_t nSize);
extern int core_spi_read(uint8_t *pBuf, uint16_t nSize);

/*max interrupt data length*/
#define MAX_INT_DATA_LEN       2048

/*debug data frame buffer*/
#define MAX_DEBUG_DATA_FRAME   1024

/*debug proc node length*/
#define MAX_DEBUG_NODE_LEN     4096

/*finger report*/
struct core_fr_data {
    /* the default of finger report is enabled */
    bool isEnableFR;
    bool handleint;
    /* used to send finger report packet to user psace */
    bool isEnableNetlink;
    /* allow input dev to report the value of physical touch */
    bool isEnablePressure;
    /* get screen resloution from fw if it's true */
    bool isSetResolution;
    /* used to change I2C Uart Mode when fw mode is in this mode */
    uint8_t i2cuart_mode;
    /* current firmware mode in driver */
    uint16_t actual_fw_mode;
};

/* Keys and code with each fingers */
struct mutual_touch_info {
    uint8_t touch_num;
    //for oplus
    int pointid_info;
    struct point_info points[MAX_TOUCH_NUM];
    uint8_t gesture_data[GESTURE_INFO_LENGTH];
    //end for oplus
};

/* Store the packet of finger report */
struct fr_data_node {
    uint8_t *data;
    uint16_t len;
};

extern struct core_fr_data *core_fr;

#endif /* __ilitek_common.h */
