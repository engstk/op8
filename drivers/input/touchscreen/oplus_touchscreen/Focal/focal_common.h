/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FOCAL_COMMON_H__
#define __FOCAL_COMMON_H__

/*********PART1:Head files**********************/
#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>

#include "../touchpanel_common.h"
#include "../touchpanel_healthinfo.h"
#include "../touchpanel_exception.h"
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/device_info.h>

/*********PART2:Define Area**********************/
/*create apk debug channel*/
#define PROC_UPGRADE                            0
#define PROC_READ_REGISTER                      1
#define PROC_WRITE_REGISTER                     2
#define PROC_AUTOCLB                            4
#define PROC_UPGRADE_INFO                       5
#define PROC_WRITE_DATA                         6
#define PROC_READ_DATA                          7
#define PROC_SET_TEST_FLAG                      8
#define PROC_SET_SLAVE_ADDR                     10
#define PROC_HW_RESET                           11

#define WRITE_BUF_SIZE                          512
#define READ_BUF_SIZE                           512
#define FILE_NAME_LENGTH                        128

#define Limit_ItemMagic     0x4F50504F
#define Limit_ItemMagic_V2  0x4F504C53

struct focal_testdata{
    int TX_NUM;
    int RX_NUM;
    int fd;
    int irq_gpio;
    int key_TX;
    int key_RX;
    uint64_t  TP_FW;
    const struct firmware *fw;
    bool fd_support;
    bool fingerprint_underscreen_support;
    uint64_t test_item;
};

struct auto_test_header {
    uint32_t magic1;
    uint32_t magic2;
    uint64_t test_item;
};

struct auto_test_item_header {
    uint32_t    item_magic;
    uint32_t    item_size;
    uint16_t    item_bit;
    uint16_t    item_limit_type;
    uint32_t    top_limit_offset;
    uint32_t    floor_limit_offset;
    uint32_t    para_num;
};

struct test_item_info {
    uint32_t    item_magic;
    uint32_t    item_size;
    uint16_t    item_bit;
    uint16_t    item_limit_type;
    uint32_t    top_limit_offset;
    uint32_t    floor_limit_offset;
    uint32_t    para_num;               /*number of parameter*/
    int32_t     *p_buffer;              /*pointer to item parameter buffer*/
    uint32_t    item_offset;            /*item offset*/
};

//test item
enum {
    TYPE_ERROR                                  = 0x00,
    TYPE_NOISE_DATA                             = 0x01,
    TYPE_RAW_DATA                               = 0x02,
    TYPE_UNIFORMITY_DATA                        = 0x03,
    TYPE_SCAP_CB_DATA                           = 0x04,
    TYPE_SCAP_RAW_DATA                          = 0x05,
    TYPE_SCAP_CB_WATERPROOF_DATA                = 0x06,
    TYPE_PANEL_DIFFER_DATA                      = 0x07,
    TYPE_SCAP_RAW_WATERPROOF_DATA               = 0x08,
    TYPE_SHORT_DATA                             = 0x09,
    TYPE_OPEN_DATA                              = 0x0A,
    TYPE_CB_DATA                                = 0x0B,

    TYPE_BLACK_CB_DATA                          = 0x10,
    TYPE_BLACK_RAW_DATA                         = 0x11,
    TYPE_BLACK_NOISE_DATA                       = 0x12,

    TYPE_FACTORY_NOISE_DATA                     = 0x15,            /*limit from panel factory*/
    TYPE_FACTORY_RAW_DATA                       = 0x16,
    TYPE_FACTORY_UNIFORMITY_DATA                = 0x17,
    TYPE_FACTORY_SCAP_CB_DATA                   = 0x18,
    TYPE_FACTORY_SCAP_RAW_DATA                  = 0x19,
    TYPE_FACTORY_SCAP_CB_WATERPROOF_DATA        = 0x1A,
    TYPE_FACTORY_PANEL_DIFFER_DATA              = 0x1B,
    TYPE_FACTORY_SCAP_RAW_WATERPROOF_DATA       = 0x1C,

    TYPE_MAX                                    = 0xFF,
};

enum limit_type {
    LIMIT_TYPE_NO_DATA                 = 0x00,            //means no limit data
    LIMIT_TYPE_CERTAIN_DATA            = 0x01,            //means all nodes limit data is a certain data
    LIMIT_TYPE_EACH_NODE_DATA          = 0x02,            //means all nodes have it's own limit
    LIMIT_TYPE_TX_RX_DATA              = 0x03,            //means all nodes have it's own limit,format is : tx * rx,horizontal for rx data,vertical for tx data
    LIMIT_TYPE_SLEF_TX_RX_DATA         = 0x04,            //means all nodes have it's own limit,format is : tx + rx
    LIMIT_TYPE_SLEF_TX_RX_DATA_DOUBLE  = 0x05,            //means all nodes have it's own limit,format is : tx + tx + rx + rx
/***************************** Novatek *********************************/
    LIMIT_TYPE_TOP_FLOOR_DATA          = 0x06,     //means all nodes limit data is a certain data
    LIMIT_TYPE_DOZE_FDM_DATA           = 0x07,     //means all nodes limit data is a certain data
    LIMIT_TYPE_TOP_FLOOR_RX_TX_DATA    = 0x08,     //means all nodes limit data is a certain data
    LIMIT_TYPE_INVALID_DATA            = 0xFF,            //means wrong limit data type
};


struct fts_proc_operations {
    void (*auto_test)    (struct seq_file *s, void *chip_data, struct focal_testdata *focal_testdata);
    void (*set_touchfilter_state)  (void *chip_data, uint8_t range_size);
    uint8_t (*get_touchfilter_state)  (void *chip_data);
};

int fts_create_proc(struct touchpanel_data *ts, struct fts_proc_operations *syna_ops);




/*********PART3:Struct Area**********************/
struct focal_debug_func
{
    void (*esd_check_enable)(void *chip_data, bool enable);
    bool (*get_esd_check_flag)(void *chip_data);
    void (*reset)(void *chip_data, int msecond);
    int  (*get_fw_version)(void *chip_data);
    int  (*dump_reg_sate)(void *chip_data, char *buf);
    void (*set_grip_handle)  (void *chip_data, int para_num, char *buf);
};

/*********PART4:function declare*****************/
int focal_create_sysfs_spi(struct spi_device *spi);
int focal_create_sysfs(struct i2c_client *client);
int focal_create_apk_debug_channel(struct touchpanel_data *ts);
void ft_limit_read_std(struct seq_file *s, struct touchpanel_data *ts);
int focal_auto_test(struct seq_file *s,  struct touchpanel_data *ts);

#endif /*__FOCAL_COMMON_H__*/
