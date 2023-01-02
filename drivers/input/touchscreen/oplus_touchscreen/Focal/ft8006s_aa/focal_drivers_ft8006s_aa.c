// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "ft8006s_aa_core.h"

struct fts_ts_data *fts_data = NULL;
extern int tp_register_times;

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "focaltech,fts"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
        do {\
                if (LEVEL_DEBUG == tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DETAIL(a, arg...)\
        do {\
                if (LEVEL_BASIC != tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
        do {\
                if (tp_debug) {\
                        printk(a, ##arg);\
                }\
        } while(0)


#define FTS_REG_UPGRADE                             0xFC
#define FTS_UPGRADE_AA                              0xAA
#define FTS_UPGRADE_55                              0x55
#define FTS_DELAY_UPGRADE_AA                        10
#define FTS_DELAY_UPGRADE_RESET                     80
#define FTS_UPGRADE_LOOP                            10

#define FTS_CMD_RESET                               0x07
#define FTS_CMD_START                               0x55
#define FTS_CMD_START_DELAY                         12
#define FTS_CMD_READ_ID                             0x90

#define FTS_CMD_SET_PRAM_ADDR                       0xAD
#define FTS_CMD_WRITE                               0xAE

#define FTS_CMD_ECC                                 0xCC
#define FTS_CMD_ECC_LEN                             7
#define FTS_ECC_FINISH_TIMEOUT                      100
#define FTS_CMD_ECC_FINISH                          0xCE
#define FTS_CMD_ECC_FINISH_OK_A5                    0xA5
#define FTS_CMD_ECC_FINISH_OK_00                    0x00
#define FTS_CMD_ECC_READ                            0xCD

#define FTS_CMD_START_APP                           0x08

#define FTS_APP_INFO_OFFSET                         0x100

#define AL2_FCS_COEF                ((1 << 15) + (1 << 10) + (1 << 3))


/****************proc/ftxxxx-debug**********************/
#define PROC_READ_REGISTER                      1
#define PROC_WRITE_REGISTER                     2
#define PROC_WRITE_DATA                         6
#define PROC_READ_DATA                          7
#define PROC_SET_TEST_FLAG                      8
#define PROC_HW_RESET                           11
#define PROC_NAME                               "ftxxxx-debug"
#define PROC_BUF_SIZE                           256



enum GESTURE_ID {
    GESTURE_RIGHT2LEFT_SWIP = 0x20,
    GESTURE_LEFT2RIGHT_SWIP = 0x21,
    GESTURE_DOWN2UP_SWIP = 0x22,
    GESTURE_UP2DOWN_SWIP = 0x23,
    GESTURE_DOUBLE_TAP = 0x24,
    GESTURE_DOUBLE_SWIP = 0x25,
    GESTURE_RIGHT_VEE = 0x51,
    GESTURE_LEFT_VEE = 0x52,
    GESTURE_DOWN_VEE = 0x53,
    GESTURE_UP_VEE = 0x54,
    GESTURE_O_CLOCKWISE = 0x57,
    GESTURE_O_ANTICLOCK = 0x30,
    GESTURE_W = 0x31,
    GESTURE_M = 0x32,
    GESTURE_FINGER_PRINT = 0x26,
    GESTURE_SINGLE_TAP = 0x27,
};


static void focal_esd_check_enable(void *chip_data, bool enable);
static fw_update_state fts_fw_update(void *chip_data, const struct firmware *fw, bool force);
static fw_update_state fts_fw_update_ftm(void *chip_data,  bool force);




u8 fw_file[] = {
//#include "include/RIO_HLT_B6_FT_02_factorymode_app.i"
};

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
static const struct mtk_chip_config spi_ctrdata = {
    .rx_mlsb = 1,
    .tx_mlsb = 1,
    /*.cs_pol = 0,*/
    .cs_setuptime = 25,
};
#else
static const struct mt_chip_conf spi_ctrdata = {
    .setuptime = 25,
    .holdtime = 25,
    .high_time = 3, /* 16.6MHz */
    .low_time = 3,
    .cs_idletime = 2,
    .ulthgh_thrsh = 0,

    .cpol = 0,
    .cpha = 0,

    .rx_mlsb = 1,
    .tx_mlsb = 1,

    .tx_endian = 0,
    .rx_endian = 0,

    .com_mod = DMA_TRANSFER,

    .pause = 0,
    .finish_intr = 1,
    .deassert = 0,
    .ulthigh = 0,
    .tckdly = 0,
};
#endif //CONFIG_SPI_MT65XX
#endif // end of  CONFIG_TOUCHPANEL_MTK_PLATFORM



/**********************************************************************
 *                    SPI protocols                                   *
 **********************************************************************/
#define SPI_RETRY_NUMBER            3
#define CS_HIGH_DELAY               150 /* unit: us */
#define SPI_BUF_LENGTH              4096

#define DATA_CRC_EN                 0x20
#define WRITE_CMD                   0x00
#define READ_CMD                    (0x80 | DATA_CRC_EN)

#define SPI_DUMMY_BYTE              3
#define SPI_HEADER_LENGTH           6   /*CRC*/

static struct mutex bus_lock;
static u8 *bus_tx_buf;
static u8 *bus_rx_buf;

/* spi interface */
static int fts_spi_transfer(struct spi_device *spi, u8 *tx_buf, u8 *rx_buf, u32 len)
{
    int ret = 0;
    struct spi_message msg;
    struct spi_transfer xfer = {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len    = len,
    };

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);

    ret = spi_sync(spi, &msg);
    if (ret) {
        TPD_INFO("spi_sync fail,ret:%d", ret);
        return ret;
    }

    return ret;
}

static void crckermit(u8 *data, u32 len, u16 *crc_out)
{
    u32 i = 0;
    u32 j = 0;
    u16 crc = 0xFFFF;

    for ( i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x01)
                crc = (crc >> 1) ^ 0x8408;
            else
                crc = (crc >> 1);
        }
    }

    *crc_out = crc;
}

static int rdata_check(u8 *rdata, u32 rlen)
{
    u16 crc_calc = 0;
    u16 crc_read = 0;

    crckermit(rdata, rlen - 2, &crc_calc);
    crc_read = (u16)(rdata[rlen - 1] << 8) + rdata[rlen - 2];
    if (crc_calc != crc_read) {
        return -EIO;
    }

    return 0;
}

int fts_write(struct spi_device *spi, u8 *writebuf, u32 writelen)
{
    int ret = 0;
    int i = 0;
    u8 *txbuf = NULL;
    u8 *rxbuf = NULL;
    u32 txlen = 0;
    u32 txlen_need = writelen + SPI_HEADER_LENGTH + SPI_DUMMY_BYTE;
    u32 datalen = writelen - 1;

    if (!writebuf || !writelen) {
        TPD_INFO("writebuf/len is invalid");
        return -EINVAL;
    }

    mutex_lock(&bus_lock);
    if (txlen_need > SPI_BUF_LENGTH) {
        txbuf = kzalloc(txlen_need, GFP_KERNEL | GFP_DMA);
        if (NULL == txbuf) {
            TPD_INFO("txbuf malloc fail");
            ret = -ENOMEM;
            goto err_write;
        }

        rxbuf = kzalloc(txlen_need, GFP_KERNEL | GFP_DMA);
        if (NULL == rxbuf) {
            TPD_INFO("rxbuf malloc fail");
            ret = -ENOMEM;
            goto err_write;
        }
    } else {
        txbuf = bus_tx_buf;
        rxbuf = bus_rx_buf;
        memset(txbuf, 0x0, SPI_BUF_LENGTH);
        memset(rxbuf, 0x0, SPI_BUF_LENGTH);
    }

    txbuf[txlen++] = writebuf[0];
    txbuf[txlen++] = WRITE_CMD;
    txbuf[txlen++] = (datalen >> 8) & 0xFF;
    txbuf[txlen++] = datalen & 0xFF;
    if (datalen > 0) {
        txlen = txlen + SPI_DUMMY_BYTE;
        memcpy(&txbuf[txlen], &writebuf[1], datalen);
        txlen = txlen + datalen;
    }

    for (i = 0; i < SPI_RETRY_NUMBER; i++) {
        ret = fts_spi_transfer(spi, txbuf, rxbuf, txlen);
        if ((0 == ret) && ((rxbuf[3] & 0xA0) == 0)) {
            break;
        } else {
            TPD_INFO("data write(addr:%x),status:%x,retry:%d,ret:%d",
                      writebuf[0], rxbuf[3], i, ret);
            ret = -EIO;
            udelay(CS_HIGH_DELAY);
        }
    }
    if (ret < 0) {
        TPD_INFO("data write(addr:%x) fail,status:%x,ret:%d",
                  writebuf[0], rxbuf[3], ret);
    }

err_write:
    if (txlen_need > SPI_BUF_LENGTH) {
        if (txbuf) {
            kfree(txbuf);
            txbuf = NULL;
        }

        if (rxbuf) {
            kfree(rxbuf);
            rxbuf = NULL;
        }
    }

    udelay(CS_HIGH_DELAY);
    mutex_unlock(&bus_lock);
    return ret;
}

int fts_write_reg(struct spi_device *spi, u8 addr, u8 value)
{
    u8 writebuf[2] = { 0 };

    writebuf[0] = addr;
    writebuf[1] = value;
    return fts_write(spi, writebuf, 2);
}

int fts_read(struct spi_device *spi, u8 *cmd, u32 cmdlen, u8 *data, u32 datalen)
{
    int ret = 0;
    int i = 0;
    u8 *txbuf = NULL;
    u8 *rxbuf = NULL;
    u32 txlen = 0;
    u32 txlen_need = datalen + SPI_HEADER_LENGTH + SPI_DUMMY_BYTE;
    u8 ctrl = READ_CMD;
    u32 dp = 0;

    if (!cmd || !cmdlen || !data || !datalen) {
        TPD_INFO("cmd/cmdlen/data/datalen is invalid");
        return -EINVAL;
    }

    mutex_lock(&bus_lock);
    if (txlen_need > SPI_BUF_LENGTH) {
        txbuf = kzalloc(txlen_need, GFP_KERNEL | GFP_DMA);
        if (NULL == txbuf) {
            TPD_INFO("txbuf malloc fail");
            ret = -ENOMEM;
            goto err_read;
        }

        rxbuf = kzalloc(txlen_need, GFP_KERNEL | GFP_DMA);
        if (NULL == rxbuf) {
            TPD_INFO("rxbuf malloc fail");
            ret = -ENOMEM;
            goto err_read;
        }
    } else {
        txbuf = bus_tx_buf;
        rxbuf = bus_rx_buf;
        memset(txbuf, 0x0, SPI_BUF_LENGTH);
        memset(rxbuf, 0x0, SPI_BUF_LENGTH);
    }

    txbuf[txlen++] = cmd[0];
    txbuf[txlen++] = ctrl;
    txbuf[txlen++] = (datalen >> 8) & 0xFF;
    txbuf[txlen++] = datalen & 0xFF;
    dp = txlen + SPI_DUMMY_BYTE;
    txlen = dp + datalen;
    if (ctrl & DATA_CRC_EN) {
        txlen = txlen + 2;
    }

    for (i = 0; i < SPI_RETRY_NUMBER; i++) {
        ret = fts_spi_transfer(spi, txbuf, rxbuf, txlen);
        if ((0 == ret) && ((rxbuf[3] & 0xA0) == 0)) {
            memcpy(data, &rxbuf[dp], datalen);
            /* crc check */
            if (ctrl & DATA_CRC_EN) {
                ret = rdata_check(&rxbuf[dp], txlen - dp);
                if (ret < 0) {
                    TPD_DEBUG("data read(addr:%x) crc abnormal,retry:%d",
                              cmd[0], i);
                    udelay(CS_HIGH_DELAY);
                    continue;
                }
            }
            break;
        } else {
            TPD_INFO("data read(addr:%x) status:%x,retry:%d,ret:%d",
                      cmd[0], rxbuf[3], i, ret);
            ret = -EIO;
            udelay(CS_HIGH_DELAY);
        }
    }

    if (ret < 0) {
        TPD_INFO("data read(addr:%x) %s,status:%x,ret:%d", cmd[0],
                  (i >= SPI_RETRY_NUMBER) ? "crc abnormal" : "fail",
                  rxbuf[3], ret);
    }

err_read:
    if (txlen_need > SPI_BUF_LENGTH) {
        if (txbuf) {
            kfree(txbuf);
            txbuf = NULL;
        }

        if (rxbuf) {
            kfree(rxbuf);
            rxbuf = NULL;
        }
    }

    udelay(CS_HIGH_DELAY);
    mutex_unlock(&bus_lock);
    return ret;
}

int fts_read_reg(struct spi_device *spi, u8 addr, u8 *value)
{
    return fts_read(spi, &addr, 1, value, 1);
}

static int fts_bus_init(void)
{
    
    bus_tx_buf = kzalloc(SPI_BUF_LENGTH, GFP_KERNEL | GFP_DMA);
    if (NULL == bus_tx_buf) {
        TPD_INFO("failed to allocate memory for bus_tx_buf");
        return -ENOMEM;
    }

    bus_rx_buf = kzalloc(SPI_BUF_LENGTH, GFP_KERNEL | GFP_DMA);
    if (NULL == bus_rx_buf) {
        TPD_INFO("failed to allocate memory for bus_rx_buf");
        kfree(bus_tx_buf);
        bus_tx_buf = NULL;
        return -ENOMEM;
    }

    mutex_init(&bus_lock);
    return 0;
}

static int fts_bus_exit(void)
{
    if (bus_tx_buf) {
        kfree(bus_tx_buf);
        bus_tx_buf = NULL;
    }

    if (bus_rx_buf) {
        kfree(bus_rx_buf);
        bus_rx_buf = NULL;
    }
    return 0;
}







/*******Part1:Call Back Function implement*******/

static int fts_rstgpio_set(struct hw_resource *hw_res, bool on)
{
    if (gpio_is_valid(hw_res->reset_gpio)) {
        TPD_INFO("Set the reset_gpio \n");
        gpio_direction_output(hw_res->reset_gpio, on);
    }
    else{
         TPD_INFO("reset is invalid!!\n");
    }
    return 0;
}

/*
 * return success: 0; fail : negative
 */
static int fts_hw_reset(struct fts_ts_data *ts_data, u32 delayms)
{
    TPD_INFO("%s.\n", __func__);
    fts_rstgpio_set(ts_data->hw_res, false); /* reset gpio*/
    msleep(5);
    fts_rstgpio_set(ts_data->hw_res, true); /* reset gpio*/
    if (delayms) {
        msleep(delayms);
    }

    return 0;
}




/*********************************************************
 *              proc/ftxxxx-debug                        *
 *********************************************************/
static ssize_t fts_debug_write(struct file *filp, const char __user *buff, size_t count, loff_t *ppos)
{
    u8 *writebuf = NULL;
    u8 tmpbuf[PROC_BUF_SIZE] = { 0 };
    int buflen = count;
    int writelen = 0;
    int ret = 0;
    char tmp[PROC_BUF_SIZE];
    struct fts_ts_data *ts_data = PDE_DATA(file_inode(filp));
    struct ftxxxx_proc *proc;

    if (!ts_data) {
        TPD_INFO("ts_data is null");
        return 0;
    }
    proc = &ts_data->proc;

    if (buflen <= 1) {
        TPD_INFO("apk proc wirte count(%d) fail", buflen);
        return -EINVAL;
    }

    if (buflen > PROC_BUF_SIZE) {
        writebuf = (u8 *)kzalloc(buflen * sizeof(u8), GFP_KERNEL);
        if (NULL == writebuf) {
            TPD_INFO("apk proc wirte buf zalloc fail");
            return -ENOMEM;
        }
    } else {
        writebuf = tmpbuf;
    }

    if (copy_from_user(writebuf, buff, buflen)) {
        TPD_INFO("[APK]: copy from user error!!");
        ret = -EFAULT;
        goto proc_write_err;
    }

    proc->opmode = writebuf[0];
    switch (proc->opmode) {
    case PROC_SET_TEST_FLAG:
        TPD_DEBUG("[APK]: PROC_SET_TEST_FLAG = %x", writebuf[1]);
        if (writebuf[1] == 0) {
            focal_esd_check_enable(ts_data, true);
        } else {
            focal_esd_check_enable(ts_data, false);
        }
        break;

    case PROC_READ_REGISTER:
        proc->cmd[0] = writebuf[1];
        break;

    case PROC_WRITE_REGISTER:
        ret = fts_write_reg(ts_data->spi, writebuf[1], writebuf[2]);
        if (ret < 0) {
            TPD_INFO("PROC_WRITE_REGISTER write error");
            goto proc_write_err;
        }
        break;

    case PROC_READ_DATA:
        writelen = buflen - 1;
        if (writelen >= FTX_MAX_COMMMAND_LENGTH) {
            TPD_INFO("cmd(PROC_READ_DATA) length(%d) fail", writelen);
            goto proc_write_err;
        }
        memcpy(proc->cmd, writebuf + 1, writelen);
        proc->cmd_len = writelen;
        break;

    case PROC_WRITE_DATA:
        writelen = buflen - 1;
        ret = fts_write(ts_data->spi, writebuf + 1, writelen);
        if (ret < 0) {
            TPD_INFO("PROC_WRITE_DATA write error");
            goto proc_write_err;
        }
        break;

    case PROC_HW_RESET:
        if (buflen < PROC_BUF_SIZE) {
            snprintf(tmp, PROC_BUF_SIZE, "%s", writebuf + 1);
            tmp[buflen - 1] = '\0';
            if (strncmp(tmp, "focal_driver", 12) == 0) {
                TPD_INFO("APK execute HW Reset");
                fts_hw_reset(ts_data, 0);
            }
        }
        break;

    default:
        break;
    }

    ret = buflen;
proc_write_err:
    if ((buflen > PROC_BUF_SIZE) && writebuf) {
        kfree(writebuf);
        writebuf = NULL;
    }

    return ret;
}

static ssize_t fts_debug_read(struct file *filp, char __user *buff, size_t count, loff_t *ppos)
{
    int ret = 0;
    int num_read_chars = 0;
    int buflen = count;
    u8 *readbuf = NULL;
    u8 tmpbuf[PROC_BUF_SIZE] = { 0 };
    struct fts_ts_data *ts_data = PDE_DATA(file_inode(filp));
    struct ftxxxx_proc *proc;

    if (!ts_data) {
        TPD_INFO("ts_data is null");
        return 0;
    }
    proc = &ts_data->proc;

    if (buflen <= 0) {
        TPD_INFO("apk proc read count(%d) fail", buflen);
        return -EINVAL;
    }

    if (buflen > PROC_BUF_SIZE) {
        readbuf = (u8 *)kzalloc(buflen * sizeof(u8), GFP_KERNEL);
        if (NULL == readbuf) {
            TPD_INFO("apk proc wirte buf zalloc fail");
            return -ENOMEM;
        }
    } else {
        readbuf = tmpbuf;
    }

    switch (proc->opmode) {
    case PROC_READ_REGISTER:
        num_read_chars = 1;
        ret = fts_read_reg(ts_data->spi, proc->cmd[0], &readbuf[0]);
        if (ret < 0) {
            TPD_INFO("PROC_READ_REGISTER read error");
            goto proc_read_err;
        }
        break;
    case PROC_WRITE_REGISTER:
        break;

    case PROC_READ_DATA:
        num_read_chars = buflen;
        ret = fts_read(ts_data->spi, proc->cmd, proc->cmd_len, readbuf, num_read_chars);
        if (ret < 0) {
            TPD_INFO("PROC_READ_DATA read error");
            goto proc_read_err;
        }
        break;

    case PROC_WRITE_DATA:
        break;

    default:
        break;
    }

    ret = num_read_chars;
proc_read_err:
    if (copy_to_user(buff, readbuf, num_read_chars)) {
        TPD_INFO("copy to user error");
        ret = -EFAULT;
    }

    if ((buflen > PROC_BUF_SIZE) && readbuf) {
        kfree(readbuf);
        readbuf = NULL;
    }

    return ret;
}

static const struct file_operations fts_proc_fops = {
    .owner  = THIS_MODULE,
    .read   = fts_debug_read,
    .write  = fts_debug_write,
};

static int fts_create_apk_debug_channel(struct fts_ts_data *ts_data)
{
    struct ftxxxx_proc *proc = &ts_data->proc;

    proc->proc_entry = proc_create_data(PROC_NAME, 0777, NULL, &fts_proc_fops, ts_data);
    if (NULL == proc->proc_entry) {
        TPD_INFO("create proc entry fail");
        return -ENOMEM;
    }
    TPD_INFO("Create proc entry success!");
    return 0;
}

static void fts_release_apk_debug_channel(struct fts_ts_data *ts_data)
{
    struct ftxxxx_proc *proc = &ts_data->proc;

    if (proc->proc_entry) {
        proc_remove(proc->proc_entry);
    }
}









static int focal_dump_reg_state(void *chip_data, char *buf)
{
    int count = 0;
    u8 regvalue = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    //power mode 0:active 1:monitor 3:sleep
    fts_read_reg(ts_data->spi, FTS_REG_POWER_MODE, &regvalue);
    count += sprintf(buf + count, "Power Mode:0x%02x\n", regvalue);

    //FW version
    fts_read_reg(ts_data->spi, FTS_REG_FW_VER, &regvalue);
    count += sprintf(buf + count, "FW Ver:0x%02x\n",regvalue);

    //Vendor ID
    fts_read_reg(ts_data->spi, FTS_REG_VENDOR_ID, &regvalue);
    count += sprintf(buf + count, "Vendor ID:0x%02x\n", regvalue);

    // 1 Gesture mode,0 Normal mode
    fts_read_reg(ts_data->spi, FTS_REG_GESTURE_EN, &regvalue);
    count += sprintf(buf + count, "Gesture Mode:0x%02x\n", regvalue);

    // 3 charge in
    fts_read_reg(ts_data->spi, FTS_REG_CHARGER_MODE_EN, &regvalue);
    count += sprintf(buf + count, "charge stat:0x%02x\n", regvalue);

    //Interrupt counter
    fts_read_reg(ts_data->spi, FTS_REG_INT_CNT, &regvalue);
    count += sprintf(buf + count, "INT count:0x%02x\n", regvalue);

    //Flow work counter
    fts_read_reg(ts_data->spi, FTS_REG_FLOW_WORK_CNT, &regvalue);
    count += sprintf(buf + count, "ESD count:0x%02x\n", regvalue);

    return count;
}

static int focal_get_fw_version(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 fw_ver = 0;

    fts_read_reg(ts_data->spi, FTS_REG_FW_VER, &fw_ver);
    return (int)fw_ver;
}

static void focal_esd_check_enable(void *chip_data, bool enable)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    ts_data->esd_check_enabled = enable;
}

static bool focal_get_esd_check_flag(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return ts_data->esd_check_enabled;
}

static int fts_esd_handle(void *chip_data)
{
    int ret = -1;
    int i = 0;
    static int flow_work_cnt_last = 0;
    static int err_cnt = 0;
    static int i2c_err = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 val = 0;

    if (!ts_data->esd_check_enabled) {
        goto NORMAL_END;
    }

    ret = fts_read_reg(ts_data->spi, 0x00, &val);
    if ((ret >= 0) && ((val & 0x70) == 0x40)) { //work in factory mode
        goto NORMAL_END;
    }

    for (i = 0; i < 3; i++) {
        ret = fts_read_reg(ts_data->spi, FTS_REG_CHIP_ID, &val);
        if ((val != 0x86) && (val != 0x80)) {
            TPD_INFO("%s: read chip_id(%x) failed!(ret:%d)\n", __func__, val, ret);
            msleep(10);
            i2c_err++;
        } else {
            i2c_err = 0;
            break;
        }
    }
    ret = fts_read_reg(ts_data->spi, FTS_REG_FLOW_WORK_CNT, &val);
    if (ret < 0) {
        TPD_INFO("%s: read FTS_REG_FLOW_WORK_CNT failed!\n", __func__);
        i2c_err++;
    }

    if (flow_work_cnt_last == val) {
        err_cnt++;
    } else {
        err_cnt = 0;
    }
    flow_work_cnt_last = val;

    if ((err_cnt >= 5) || (i2c_err >= 3)) {
        TPD_INFO("esd check failed, start reset!\n");
        disable_irq_nosync(ts_data->ts->irq);
        tp_touch_btnkey_release();
        fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
        enable_irq(ts_data->ts->irq);
        flow_work_cnt_last = 0;
        err_cnt = 0;
        i2c_err = 0;
    }

NORMAL_END:
    return 0;
}



static int fts_enter_into_boot(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int i = 0;
    u8 cmd = 0;
    u8 id[2] = { 0 };

    TPD_INFO("enter into boot environment");
    for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
        /* hardware tp reset to boot */
        fts_hw_reset(ts_data, 0);
        mdelay(FTS_CMD_START_DELAY);

        /* check boot id*/
        cmd = FTS_CMD_START;
        ret = fts_write(ts_data->spi, &cmd, 1);
        mdelay(FTS_CMD_START_DELAY);
        cmd = FTS_CMD_READ_ID;
        ret = fts_read(ts_data->spi, &cmd, 1, id, 2);
        TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);
        if (id[0] == FTS_VAL_BOOT_ID) {
            return 0;
        }
    }

    return -EIO;
}

static int fts_dpram_write(struct fts_ts_data *ts_data, u32 saddr, const u8 *buf, u32 len)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    u8 *cmd = NULL;
    u32 addr = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u32 packet_number = 0;
    u32 packet_len = 0;
    u32 packet_size = FTS_FLASH_PACKET_LENGTH_SPI;

    TPD_INFO("dpram write");
    cmd = vmalloc(packet_size + 4);
    if (NULL == cmd) {
        TPD_INFO("malloc memory for pram write buffer fail");
        return -ENOMEM;
    }
    memset(cmd, 0, packet_size + 4);

    packet_number = len / packet_size;
    remainder = len % packet_size;
    if (remainder > 0)
        packet_number++;
    packet_len = packet_size;
    TPD_INFO("write data, num:%d remainder:%d", packet_number, remainder);

    for (i = 0; i < packet_number; i++) {
        offset = i * packet_size;
        addr = saddr + offset;
        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;

        /* set pram address */
        cmd[0] = FTS_CMD_SET_PRAM_ADDR;
        cmd[1] = (addr >> 16);
        cmd[2] = (addr >> 8);
        cmd[3] = (addr);
        ret = fts_write(ts_data->spi, &cmd[0], 4);
        if (ret < 0) {
            TPD_INFO("set pram(%d) addr(%d) fail", i, addr);
            goto write_pram_err;
        }

        /* write pram data */
        cmd[0] = FTS_CMD_WRITE;
        for (j = 0; j < packet_len; j++) {
            cmd[1 + j] = buf[offset + j];
        }
        ret = fts_write(ts_data->spi, &cmd[0], 1 + packet_len);
        if (ret < 0) {
            TPD_INFO("write fw to pram(%d) fail", i);
            goto write_pram_err;
        }
    }

write_pram_err:
    if (cmd) {
        vfree(cmd);
        cmd = NULL;
    }
    return ret;
}


static int fts_ecc_cal_tp(struct fts_ts_data *ts_data, u32 ecc_saddr, u32 ecc_len, u16 *ecc_value)
{
    int ret = 0;
    int i = 0;
    u8 cmd[FTS_CMD_ECC_LEN] = { 0 };
    u8 value[2] = { 0 };

    TPD_INFO("ecc calc in tp");
    cmd[0] = FTS_CMD_ECC;
    cmd[1] = (ecc_saddr >> 16);
    cmd[2] = (ecc_saddr >> 8);
    cmd[3] = (ecc_saddr);
    cmd[4] = (ecc_len >> 16);
    cmd[5] = (ecc_len >> 8);
    cmd[6] = (ecc_len);

    /* make boot to calculate ecc in pram */
    ret = fts_write(ts_data->spi, cmd, FTS_CMD_ECC_LEN);
    if (ret < 0) {
        TPD_INFO("ecc calc cmd fail");
        return ret;
    }
    mdelay(2);

    /* wait boot calculate ecc finish */
    cmd[0] = FTS_CMD_ECC_FINISH;
    for (i = 0; i < FTS_ECC_FINISH_TIMEOUT; i++) {
        ret = fts_read(ts_data->spi, cmd, 1, value, 1);
        if (ret < 0) {
            TPD_INFO("ecc finish cmd fail");
            return ret;
        }
        if (FTS_CMD_ECC_FINISH_OK_A5 == value[0])
            break;
        mdelay(1);
    }
    if (i >= FTS_ECC_FINISH_TIMEOUT) {
        TPD_INFO("wait ecc finish timeout,ecc_finish=%x", value[0]);
        return -EIO;
    }

    /* get ecc value calculate in boot */
    cmd[0] = FTS_CMD_ECC_READ;
    ret = fts_read(ts_data->spi, cmd, 1, value, 2);
    if (ret < 0) {
        TPD_INFO("ecc read cmd fail");
        return ret;
    }

    *ecc_value = ((u16)(value[0] << 8) + value[1]) & 0x0000FFFF;
    return 0;
}

static int fts_ecc_cal_host(u8 *data, u32 data_len, u16 *ecc_value)
{
    u16 ecc = 0;
    u32 i = 0;
    u32 j = 0;
    u16 al2_fcs_coef = AL2_FCS_COEF;

    for (i = 0; i < data_len; i += 2 ) {
        ecc ^= ((data[i] << 8) | (data[i + 1]));
        for (j = 0; j < 16; j ++) {
            if (ecc & 0x01)
                ecc = (u16)((ecc >> 1) ^ al2_fcs_coef);
            else
                ecc >>= 1;
        }
    }

    *ecc_value = ecc & 0x0000FFFF;
    return 0;
}

static int fts_pram_write_ecc(struct fts_ts_data *ts_data, u8 *buf, u32 len)
{
    int ret = 0;
    u32 pram_start_addr = 0;
    u16 code_len = 0;
    u16 code_len_n = 0;
    u32 pram_app_size = 0;
    u16 ecc_in_host = 0;
    u16 ecc_in_tp = 0;

    TPD_INFO("begin to write pram app(bin len:%d)", len);
    /* get pram app length */
    code_len = ((u16)buf[FTS_APP_INFO_OFFSET + 0] << 8)
               + buf[FTS_APP_INFO_OFFSET + 1];
    code_len_n = ((u16)buf[FTS_APP_INFO_OFFSET + 2] << 8)
                 + buf[FTS_APP_INFO_OFFSET + 3];
    if ((code_len + code_len_n) != 0xFFFF) {
        TPD_INFO("pram code len(%x %x) fail", code_len, code_len_n);
        return -EINVAL;
    }
    pram_app_size = (u32)code_len;
    TPD_INFO("pram app length in fact:%d", pram_app_size);
    
    /* write pram */
    ret = fts_dpram_write(ts_data, pram_start_addr, buf, pram_app_size);
    if (ret < 0) {
        TPD_INFO("write pram fail");
        return ret;
    }

    /* check ecc */
    TPD_INFO("ecc check");
    ret = fts_ecc_cal_host(buf, pram_app_size, &ecc_in_host);
    if (ret < 0) {
        TPD_INFO("ecc in host calc fail");
        return ret;
    }

    ret = fts_ecc_cal_tp(ts_data, pram_start_addr, pram_app_size, &ecc_in_tp);
    if (ret < 0) {
        TPD_INFO("ecc in tp calc fail");
        return ret;
    }

    TPD_INFO("ecc in tp:%04x,host:%04x", ecc_in_tp, ecc_in_host);
    if (ecc_in_tp != ecc_in_host) {
        TPD_INFO("ecc_in_tp(%x) != ecc_in_host(%x), ecc check fail",
                  ecc_in_tp, ecc_in_host);
        return -EIO;
    }

    TPD_INFO("pram app write successfully");
    return 0;
}

static int fts_pram_start(struct fts_ts_data *ts_data)
{
    int ret = 0;
    u8 cmd = FTS_CMD_START_APP;

    TPD_INFO("remap to start pram");
    ret = fts_write(ts_data->spi, &cmd, 1);
    if (ret < 0) {
        TPD_INFO("write start pram cmd fail");
        return ret;
    }

    return 0;
}

static int fts_fw_write_start(struct fts_ts_data *ts_data, u8 *buf, u32 len, bool need_reset)
{
    int ret = 0;

    TPD_INFO("begin to write and start fw(bin len:%d)", len);

    if (need_reset) {
        /* enter into boot environment */
        ret = fts_enter_into_boot(ts_data);
        if (ret < 0) {
            TPD_INFO("enter into boot environment fail");
            return ret;
        }
    }

    /* write pram */
    ret = fts_pram_write_ecc(ts_data, buf, len);
    if (ret < 0) {
        TPD_INFO("write pram fail");
        return ret;
    }

    /* remap pram and run fw */
    ret = fts_pram_start(ts_data);
    if (ret < 0) {
        TPD_INFO("pram start fail");
        return ret;
    }

    TPD_INFO("fw download successfully");
    return 0;
}

static int fts_fw_download(struct fts_ts_data *ts_data, u8 *buf, u32 len, bool need_reset)
{
    int ret = 0;
    int i = 0;

    TPD_INFO("fw upgrade download function");
    for (i = 0; i < 3; i++) {
        TPD_INFO("fw download times:%d", i + 1);
        ret = fts_fw_write_start(ts_data, buf, len, need_reset);
        if (0 == ret)
            break;
    }
    if (i >= 3) {
        TPD_INFO("fw download fail");
        return -EIO;
    }

    return ret;
}



void fts_auto_test(struct seq_file *s, void *chip_data, struct focal_testdata *focal_testdata)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    ts_data->s = s;
    ts_data->csv_fd = focal_testdata->fd;

    focal_esd_check_enable(ts_data, false);
    fts_test_entry(ts_data, 0);
    focal_esd_check_enable(ts_data, true);
}

#if 0
static void fts_limit_read(struct seq_file *s, struct touchpanel_data *ts)
{
}
#endif

static int fts_enter_factory_work_mode(struct fts_ts_data *ts_data, u8 mode_val)
{
    int ret = 0;
    int retry = 20;
    u8 regval = 0;

    TPD_INFO("%s:enter %s mode", __func__, (mode_val == 0x40) ? "factory" : "work");
    ret = fts_write_reg(ts_data->spi, DEVIDE_MODE_ADDR, mode_val);
    if (ret < 0) {
        TPD_INFO("%s:write mode(val:0x%x) fail", __func__, mode_val);
        return ret;
    }

    while (--retry) {
        ret = fts_read_reg(ts_data->spi, DEVIDE_MODE_ADDR, &regval);
        if (regval == mode_val)
            break;
        msleep(20);
    }

    if (!retry) {
        TPD_INFO("%s:enter mode(val:0x%x) timeout", __func__, mode_val);
        return -EIO;
    }

    msleep(FACTORY_TEST_DELAY);
    return 0;
}

static int fts_start_scan(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int retry = 50;
    u8 regval = 0;
    u8 scanval = FTS_FACTORY_MODE_VALUE | (1 << 7);

    TPD_INFO("%s: start to scan a frame", __func__);
    ret = fts_write_reg(ts_data->spi, DEVIDE_MODE_ADDR, scanval);
    if (ret < 0) {
        TPD_INFO("%s:start to scan a frame fail", __func__);
        return ret;
    }

    while (--retry) {
        ret = fts_read_reg(ts_data->spi, DEVIDE_MODE_ADDR, &regval);
        if (regval == FTS_FACTORY_MODE_VALUE)
            break;
        msleep(20);
    }
    msleep(50);

    if (!retry) {
        TPD_INFO("%s:scan a frame timeout", __func__);
        return -EIO;
    }

    return 0;
}

static int fts_get_rawdata(struct fts_ts_data *ts_data, int *raw, bool is_diff)
{
    int ret = 0;
    int i = 0;
    int byte_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM * 2;
    u8 raw_addr = 0;
    u8 regval = 0;
    u8 *buf = NULL;

    TPD_INFO("%s:call", __func__);

    /*kzalloc buffer*/
    buf = kzalloc(byte_num, GFP_KERNEL);
    if (!buf) {
        TPD_INFO("%s:kzalloc for raw byte buf fail", __func__);
        return -ENOMEM;
    }

    ret = fts_enter_factory_work_mode(ts_data, FTS_FACTORY_MODE_VALUE);
    if (ret < 0) {
        TPD_INFO("%s:enter factory mode fail", __func__);
        goto raw_err;
    }

    if (is_diff) {
        ret = fts_read_reg(ts_data->spi, FACTORY_REG_DATA_SELECT, &regval);
        ret = fts_write_reg(ts_data->spi, FACTORY_REG_DATA_SELECT, 0x01);
        if (ret < 0) {
            TPD_INFO("%s:write 0x01 to reg0x06 fail", __func__);
            goto reg_restore;
        }
    }

    ret = fts_start_scan(ts_data);
    if (ret < 0) {
        TPD_INFO("%s:scan a frame fail", __func__);
        goto reg_restore;
    }

    ret = fts_write_reg(ts_data->spi, FACTORY_REG_LINE_ADDR, 0xAD);
    if (ret < 0) {
        TPD_INFO("%s:write [data_type] to reg0x01 fail", __func__);
        goto reg_restore;
    }

    raw_addr = 0x6A;
    ret = fts_read(ts_data->spi, &raw_addr, 1, buf, byte_num);
    for (i = 0; i < byte_num; i = i + 2) {
        raw[i >> 1] = (int)(short)((buf[i] << 8) + buf[i + 1]);
        if (i%16 == 0)
            TPD_DEBUG("[%d] \n", i);
        TPD_DEBUG("%5x %5x", buf[i], buf[i+1]);
    }

reg_restore:
    if (is_diff) {
        ret = fts_write_reg(ts_data->spi, FACTORY_REG_DATA_SELECT, regval);
        if (ret < 0) {
            TPD_INFO("%s:restore reg0x06 fail", __func__);
        }
    }

raw_err:
    kfree(buf);
    ret = fts_enter_factory_work_mode(ts_data, FTS_WORK_MODE_VALUE);
    if (ret < 0) {
        TPD_INFO("%s:enter work mode fail", __func__);
    }

    return ret;
}

static void fts_delta_read(struct seq_file *s, void *chip_data)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int *raw = NULL;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;

    TPD_INFO("%s:start to read diff data", __func__);
    focal_esd_check_enable(ts_data, false);   //no allowed esd check

    raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);
    if (!raw) {
        seq_printf(s, "kzalloc for raw fail\n");
        goto raw_fail;
    }

    ret = fts_write_reg(ts_data->spi, FTS_REG_AUTOCLB_ADDR, 0x01);
    if (ret < 0){
        TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
    }

    ret = fts_get_rawdata(ts_data, raw, true);
    if (ret < 0) {
        seq_printf(s, "get diff data fail\n");
        goto raw_fail;
    }

    for (i = 0; i < tx_num; i++) {
        seq_printf(s, "\n[%2d]", i + 1);
        for (j = 0; j < rx_num; j++) {
            seq_printf(s, " %5d,", raw[i * rx_num + j]);
        }
    }
    seq_printf(s, "\n");

raw_fail:
    focal_esd_check_enable(ts_data, true);
    kfree(raw);
}

static void fts_baseline_read(struct seq_file *s, void *chip_data)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int *raw = NULL;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;

    TPD_INFO("%s:start to read raw data", __func__);
    focal_esd_check_enable(ts_data, false);

    raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);
    if (!raw) {
        seq_printf(s, "kzalloc for raw fail\n");
        goto raw_fail;
    }
    
    ret = fts_write_reg(ts_data->spi, FTS_REG_AUTOCLB_ADDR, 0x01);
    if (ret < 0){
        TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
    }
    
    ret = fts_get_rawdata(ts_data, raw, false);
    if (ret < 0) {
        seq_printf(s, "get raw data fail\n");
        goto raw_fail;
    }

    for (i = 0; i < tx_num; i++) {
        seq_printf(s, "\n[%2d]", i + 1);
        for (j = 0; j < rx_num; j++) {
            seq_printf(s, " %5d,", raw[i * rx_num + j]);
        }
    }
    seq_printf(s, "\n");

raw_fail:
    focal_esd_check_enable(ts_data, true);
    kfree(raw);
}

static void fts_main_register_read(struct seq_file *s, void *chip_data)
{
    u8 regvalue = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    /*TP FW version*/
    fts_read_reg(ts_data->spi, FTS_REG_FW_VER, &regvalue);
    seq_printf(s, "TP FW Ver:0x%02x\n", regvalue);

    /*Vendor ID*/
    fts_read_reg(ts_data->spi, FTS_REG_VENDOR_ID, &regvalue);
    seq_printf(s, "Vendor ID:0x%02x\n", regvalue);

    /*Gesture enable*/
    fts_read_reg(ts_data->spi, FTS_REG_GESTURE_EN, &regvalue);
    seq_printf(s, "Gesture Mode:0x%02x\n", regvalue);

    /*charge in*/
    fts_read_reg(ts_data->spi, FTS_REG_CHARGER_MODE_EN, &regvalue);
    seq_printf(s, "charge state:0x%02x\n", regvalue);

    /*edge limit*/
    fts_read_reg(ts_data->spi, FTS_REG_EDGE_LIMIT, &regvalue);
    seq_printf(s, "edge Mode:0x%02x\n", regvalue);

    /*game mode*/
    fts_read_reg(ts_data->spi, FTS_REG_GAME_MODE_EN, &regvalue);
    seq_printf(s, "Game Mode:0x%02x\n", regvalue);

    /*FOD mode*/
    fts_read_reg(ts_data->spi, FTS_REG_FOD_EN, &regvalue);
    seq_printf(s, "FOD Mode:0x%02x\n", regvalue);

    /*Interrupt counter*/
    fts_read_reg(ts_data->spi, FTS_REG_INT_CNT, &regvalue);
    seq_printf(s, "INT count:0x%02x\n", regvalue);

    /*Flow work counter*/
    fts_read_reg(ts_data->spi, FTS_REG_FLOW_WORK_CNT, &regvalue);
    seq_printf(s, "ESD count:0x%02x\n", regvalue);

    /*Panel ID*/
    fts_read_reg(ts_data->spi, FTS_REG_MODULE_ID, &regvalue);
    seq_printf(s, "PANEL ID:0x%02x\n", regvalue);

    return;
}

#define LEN_DOZE_FDM_ROW_DATA 2
#define NUM_MODE 2
#define LEN_TEST_ITEM_FIELD 16
#define LIMIT_HEADER_MAGIC_1 0x494D494C
#define LIMIT_HEADER_MAGIC_2 0x474D4954
static void fts_limit_read_std(struct seq_file *s, struct touchpanel_data *ts)
{
    int ret = 0, m = 0, i = 0, j = 0, item_cnt = 0;
    const struct firmware *fw = NULL;
    struct auto_test_header *ph = NULL;
    struct auto_test_item_header *item_head = NULL;
    uint32_t *p_item_offset = NULL;
    int32_t *p_data32 = NULL;
    int tx = ts->hw_res.TX_NUM;
    int rx = ts->hw_res.RX_NUM;
    int num_panel_node = rx  * tx ;


    ret = request_firmware(&fw, ts->panel_data.test_limit_name, ts->dev);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        seq_printf(s, "Request failed, Check the path\n");
        return;
    }

    ph = (struct auto_test_header *)(fw->data);
    p_item_offset = (uint32_t *)(fw->data + LEN_TEST_ITEM_FIELD);
    if ((ph->magic1 != LIMIT_HEADER_MAGIC_1) || (ph->magic2 != LIMIT_HEADER_MAGIC_2)) {
        TPD_INFO("limit image is not generated by oplus\n");
        seq_printf(s, "limit image is not generated by oplus\n");
        release_firmware(fw);
        return;
    }

    for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
        if ((ph->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }
    TPD_INFO("%s: total test item = %d \n", __func__, item_cnt);
    if (!item_cnt) {
        TPD_INFO("limit image has no test item\n");
        seq_printf(s, "limit image has no test item\n");
    }

    for (m = 0; m < item_cnt; m++) {
        TPD_INFO("common debug d: p_item_offset[%d] = 0x%x \n", m, p_item_offset[m]);
        item_head = (struct auto_test_item_header *)(fw->data + p_item_offset[m]);
        if (item_head->item_magic != 0x4F50504F) {
            TPD_INFO("item: %d limit data has some problem\n", item_head->item_bit);
            seq_printf(s, "item: %d limit data has some problem\n", item_head->item_bit);
            continue;
        }
        TPD_INFO("item %d[size %d, limit type %d, para num %d] :\n", item_head->item_bit, item_head->item_size, item_head->item_limit_type, item_head->para_num);
        seq_printf(s, "\n\nitem %d[size %d, limit type %d, para num %d] :", item_head->item_bit, item_head->item_size, item_head->item_limit_type, item_head->para_num);
        if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
            seq_printf(s, "no limit data\n");
        } else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
            if(item_head->item_bit == TYPE_SHORT_DATA) {
                seq_printf(s, "TYPE_SHORT_DATA: \n");
            } else if(item_head->item_bit == TYPE_OPEN_DATA) {
                seq_printf(s, "TYPE_OPEN_DATA: \n");
            } else if(item_head->item_bit == TYPE_CB_DATA) {
                seq_printf(s, "TYPE_CB_DATA: \n");
            } else if(item_head->item_bit == TYPE_RAW_DATA){
                seq_printf(s, "TYPE_FW_RAWDATA: \n");
            } else if(item_head->item_bit == TYPE_NOISE_DATA) {
                seq_printf(s, "TYPE_NOISE_DATA: \n");
            } else if(item_head->item_bit == TYPE_BLACK_CB_DATA) {
                seq_printf(s, "TYPE_BLACK_CB_DATA: \n");
            } else if(item_head->item_bit == TYPE_BLACK_RAW_DATA) {
                seq_printf(s, "TYPE_BLACK_RAW_DATA: \n");
            } else if(item_head->item_bit == TYPE_BLACK_NOISE_DATA) {
                seq_printf(s, "TYPE_BLACK_NOISE_DATA: \n");
            }
            
            TPD_INFO("top data [%d]: \n", m);
            seq_printf(s, "top data: ");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            if (p_data32) {
                for (i = 0 ; i < num_panel_node; i++) {
                    if (i % rx == 0)
                        seq_printf(s, "\n[%2d] ", (i / rx));
                    seq_printf(s, "%4d, ", p_data32[i]);
                    TPD_DEBUG("%d, ", p_data32[i]);
                }
                seq_printf(s, "\nfloor data: ");
                p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
                for (i = 0 ; i < num_panel_node; i++) {
                    if (i % rx == 0) {
                        seq_printf(s, "\n[%2d] ", (i / rx));
                    }
                    seq_printf(s, "%4d, ", p_data32[i]);
                    TPD_DEBUG("%d, ", p_data32[i]);
                }
            } else {
                TPD_INFO("%s: screen on, p_data32 is NULL \n", __func__);
            }
        }
        p_data32 = (int32_t *)(fw->data + p_item_offset[m] + sizeof(struct auto_test_item_header));
        if (item_head->para_num) {
            seq_printf(s, "parameter:");
            for (j = 0; j < item_head->para_num; j++) {
                seq_printf(s, "%d, ", p_data32[j]);
            }
            seq_printf(s, "\n");
        }
        seq_printf(s, "\n");
    }

    release_firmware(fw);
}


static int fts_enable_black_gesture(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_GESTURE, write 0xD0=%d", enable);
    return fts_write_reg(ts_data->spi, FTS_REG_GESTURE_EN, enable);
}

static int fts_enable_edge_limit(struct fts_ts_data *ts_data, bool enable)
{
    u8 edge_mode = 0;

    /*0:Horizontal, 1:Vertical*/
    if ((enable == 1) || (VERTICAL_SCREEN == ts_data->touch_direction)) {
        edge_mode = 0;
    } else if (enable == 0) {
        if (LANDSCAPE_SCREEN_90 == ts_data->touch_direction)
            edge_mode = 1;
        else if (LANDSCAPE_SCREEN_270 == ts_data->touch_direction)
            edge_mode = 2;
    }
    TPD_INFO("MODE_EDGE, write 0x8C=%d", edge_mode);
    return fts_write_reg(ts_data->spi, FTS_REG_EDGE_LIMIT, edge_mode);
}

static int fts_enable_charge_mode(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_CHARGE, write 0x8B=%d", enable);
    return fts_write_reg(ts_data->spi, FTS_REG_CHARGER_MODE_EN, enable);
}

static int fts_enable_game_mode(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_GAME, write 0xB8=%d", enable);
    return fts_write_reg(ts_data->spi, FTS_REG_GAME_MODE_EN, enable);;
}

static int fts_enable_headset_mode(struct fts_ts_data *ts_data, bool enable)
{
    TPD_INFO("MODE_HEADSET, write 0xC3=%d \n", enable);
    return fts_write_reg(ts_data->spi, FTS_REG_HEADSET_MODE_EN, enable);
}

static int fts_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;

    switch (mode) {
    case MODE_NORMAL:
        TPD_INFO("MODE_NORMAL");
        break;

    case MODE_SLEEP:
        TPD_INFO("MODE_SLEEP, write 0xA5=3");
        ret = fts_write_reg(ts_data->spi, FTS_REG_POWER_MODE, 0x03);
        if (ret < 0) {
            TPD_INFO("%s: enter into sleep failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GESTURE:
        TPD_INFO("MODE_GESTURE, Melo, ts->is_suspended = %d \n", ts_data->ts->is_suspended);
        if (ts_data->ts->is_suspended){                              // do not pull up reset when doing resume
            if (ts_data->last_mode == MODE_SLEEP) {
                fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
            }
        }
        ret = fts_enable_black_gesture(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable gesture failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GLOVE:
        break;

    case MODE_EDGE:
        ret = fts_enable_edge_limit(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable edg limit failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_FACE_DETECT:
        break;

    case MODE_CHARGE:
        ret = fts_enable_charge_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable charge mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_GAME:
        ret = fts_enable_game_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable game mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    case MODE_HEADSET:
        ret = fts_enable_headset_mode(ts_data, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable headset mode failed.\n", __func__);
            goto mode_err;
        }
        break;

    default:
        TPD_INFO("%s: Wrong mode.\n", __func__);
        goto mode_err;
    }

    ts_data->last_mode = mode;
    return 0;
mode_err:
    return ret;
}

static int fts_power_control(void *chip_data, bool enable)
{
    /*For IDC, power on sequences are done in LCD driver*/
    return 0;
}

/*
 * return success: 0; fail : negative
 */
static int fts_reset(void *chip_data)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s:call\n", __func__);
    fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);

    return 0;
}

static int  fts_reset_gpio_control(void *chip_data, bool enable)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    return fts_rstgpio_set(ts_data->hw_res, enable);
}

static int fts_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    //char manu_temp[MAX_DEVICE_MANU_LENGTH] = FOCAL_PREFIX;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len-3] == 'i') && \
        (panel_data->fw_name[len-2] == 'm') && (panel_data->fw_name[len-1] == 'g')) {
        //panel_data->fw_name[len-3] = 'b';
        //panel_data->fw_name[len-2] = 'i';
        //panel_data->fw_name[len-1] = 'n';
        TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type, panel_data->fw_name);
    }
    //strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
    //strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
    TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type, panel_data->fw_name);

    return 0;
}

static int fts_get_chip_info(void *chip_data)
{
    u8 cmd = 0x90;
    u8 id[2] = { 0 };
    int cnt = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    for (cnt = 0; cnt < 3; cnt++) {
        /* hardware tp reset to boot */
        fts_hw_reset(ts_data, 0);
        mdelay(FTS_CMD_START_DELAY);

        /* check boot id*/
        cmd = FTS_CMD_START;
        fts_write(ts_data->spi, &cmd, 1);
        mdelay(FTS_CMD_START_DELAY);
        cmd = FTS_CMD_READ_ID;
        fts_read(ts_data->spi, &cmd, 1, id, 2);
        TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);
        if (id[0] == FTS_VAL_BOOT_ID) {
//            tpd_load_status = 1;
            return 0;
        }
    }

    return -1;
}

static int fts_ftm_process(void *chip_data)
{
    int ret = -1;
	struct fts_ts_data *chip_info = (struct fts_ts_data *)chip_data;

	TPD_INFO("%s is called!\n", __func__);

	 ret = fts_get_chip_info(chip_info);
	 if (!ret) {
		 ret = fts_fw_update_ftm(chip_info, 1);
		 if(ret > 0) {
			 TPD_INFO("%s fw update failed!\n", __func__);
		 }  
	}

    return ret;
}

static fw_check_state fts_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    u8 cmd = 0x90;
    u8 id[2] = { 0 };
	uint8_t ver_len = 0;
    int i = 0;
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    TPD_INFO("%s:called", __func__);

    for(i = 0; i < 10; i++) {
        msleep(10);
        fts_read_reg(ts_data->spi, FTS_REG_CHIP_ID, &id[0]);
        if ((id[0] == 0x86) || (id[0] == 0x80)) {
            break;
        }
    }
    if (i >= 10) {
        fts_read(ts_data->spi, &cmd, 1, id, 2);
        TPD_INFO("%s:boot id:0x%02x%02x, fw abnormal", __func__, id[0], id[1]);
        return FW_ABNORMAL;
    }

    /*fw check normal need update TP_FW  && device info*/
    fts_read_reg(ts_data->spi, FTS_REG_FW_VER, &ts_data->fwver);
    panel_data->TP_FW = ts_data->fwver;
    TPD_INFO("FW VER:%d", panel_data->TP_FW);
    if (panel_data->manufacture_info.version) {
        sprintf(dev_version, "%02x", panel_data->TP_FW);
        ver_len = strlen(panel_data->manufacture_info.version);
        if (ver_len <= 11) {
            //strlcat(panel_data->manufacture_info.version, dev_version, MAX_DEVICE_VERSION_LENGTH);
            strlcpy(&panel_data->manufacture_info.version[7], dev_version, 3);
        } else {
            strlcpy(&panel_data->manufacture_info.version[12], dev_version, 3);
        }

    }
    return FW_NORMAL;
}
static fw_update_state fts_fw_update_ftm(void *chip_data,  bool force)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 *buf;
    u32 len = 0;

    TPD_INFO("%s: called", __func__);


    /*request_firmware fail*/
    TPD_INFO("no fw from request_firmware()");
    buf = ts_data->h_fw_file;
    len = ts_data->h_fw_size;



    if ((len < 0x120) || (len > (256 * 1024))) {
        TPD_INFO("fw_len(%d) is invalid", len);
        return FW_UPDATE_ERROR;
    }

    focal_esd_check_enable(ts_data, false);
    ret = fts_fw_download(ts_data, buf, len, true);
    focal_esd_check_enable(ts_data, true);
    if (ret < 0) {
        TPD_INFO("fw update fail");
        return FW_UPDATE_ERROR;
    }

    return FW_UPDATE_SUCCESS;
}

static fw_update_state fts_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 *buf;
    u32 len = 0;

    TPD_INFO("%s: called", __func__);
    if (!fw && (!ts_data->h_fw_file || !ts_data->h_fw_size)) {
        TPD_INFO("fw is null");
        return FW_UPDATE_ERROR;
    }

    if (fw) {
        buf = (u8 *)fw->data;
        len = (int)fw->size;
    } else {
        /*request_firmware fail*/
        TPD_INFO("no fw from request_firmware()");
        buf = ts_data->h_fw_file;
        len = ts_data->h_fw_size;
    }


    if ((len < 0x120) || (len > (256 * 1024))) {
        TPD_INFO("fw_len(%d) is invalid", len);
        return FW_UPDATE_ERROR;
    }

    focal_esd_check_enable(ts_data, false);
    ret = fts_fw_download(ts_data, buf, len, true);
    focal_esd_check_enable(ts_data, true);
    if (ret < 0) {
        TPD_INFO("fw update fail");
        return FW_UPDATE_ERROR;
    }

    return FW_UPDATE_SUCCESS;
}


static int fts_fw_recovery(struct fts_ts_data *ts_data)
{
    int ret = 0;
    u8 boot_state = 0;
    u8 chip_id = 0;
    u8 cmd = FTS_CMD_READ_ID;
    u8 id[2] = { 0 };
    const struct firmware *fw = NULL;
    struct touchpanel_data *ts = ts_data->ts;

    TPD_INFO("check if boot recovery");

    if (ts->loading_fw) {
        TPD_INFO("fw is loading, not download again");
        return -EINVAL;
    }

    ret = fts_read(ts_data->spi, &cmd, 1, id, 2);
    TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);
    if (id[0] != FTS_VAL_BOOT_ID) {
        TPD_INFO("check boot id fail");
        return ret;
    }

    ret = fts_read_reg(ts_data->spi, 0xD0, &boot_state);
    if (ret < 0) {
        TPD_INFO("read boot state failed, ret=%d", ret);
        return ret;
    }

    if (boot_state != 0x01) {
        TPD_INFO("not in boot mode(0x%x),exit", boot_state);
        return -EIO;
    }

    TPD_INFO("abnormal situation,need download fw");
    /*ret = request_firmware(&fw, ts->panel_data.fw_name, ts->dev);
    if (ret) {
        TPD_INFO("request_firmware(%s) fail", ts->panel_data.fw_name);
    }*/
   ts_data->h_fw_file = (u8 *)ts->panel_data.firmware_headfile.firmware_data;
   ts_data->h_fw_size = ts->panel_data.firmware_headfile.firmware_size;

   TPD_INFO("ts_data->h_fw_size == %d\n", ts_data->h_fw_size);

    ts->loading_fw = true;
    if (ts->ts_ops && ts->ts_ops->fw_update)
        ret = ts->ts_ops->fw_update(ts->chip_data, fw, 1);
    ts->loading_fw = false;
    if (fw) {
        release_firmware(fw);
        fw = NULL;
    }

    msleep(10);
    ret = fts_read_reg(ts_data->spi, FTS_REG_CHIP_ID, &chip_id);
    TPD_INFO("read chip id:0x%02x", chip_id);

    TPD_INFO("boot recovery pass");
    return ret;
}


static u8 fts_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;
    u8 cmd = FTS_REG_POINTS;
    u8 result_event = 0;
    u8 *buf = ts_data->rbuf;
    u8 *gesture = &ts_data->rbuf[FTS_MAX_POINTS_LENGTH];

    memset(buf, 0xFF, FTS_MAX_POINTS_LENGTH);
    

    ret = fts_read(ts_data->spi, &cmd, 1, &buf[0], FTS_REPORT_BUFFER_SIZE);
    if (ret < 0) {
        TPD_INFO("read touch point one fail");
        return IRQ_IGNORE;
    }

    if ((0xEF == buf[1]) && (0xEF == buf[2]) && (0xEF == buf[3])) {
        /*recovery fw*/
        fts_fw_recovery(ts_data);
        return IRQ_FW_AUTO_RESET;
    }

    /*gesture*/
    if (gesture_enable && is_suspended) {
        if (gesture[0] == 0x01) {
            return IRQ_GESTURE;
        }
    }
    

    if ((buf[1] == 0xFF) && (buf[2] == 0xFF) && (buf[3] == 0xFF)) {
        TPD_INFO("Need recovery TP state");
        return IRQ_FW_AUTO_RESET;
    }

    /*TODO:confirm need print debug info*/
    if (ts_data->rbuf[0] != ts_data->irq_type) {
        SET_BIT(result_event, IRQ_FW_HEALTH);
    }
    ts_data->irq_type = ts_data->rbuf[0];

    /*normal touch*/
    SET_BIT(result_event, IRQ_TOUCH);

    return result_event;
}

static u32 fts_u32_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int ret = 0;
    u8 cmd = FTS_REG_POINTS;
    u32 result_event = 0;
    u8 *buf = ts_data->rbuf;
    u8 *gesture = &ts_data->rbuf[FTS_MAX_POINTS_LENGTH];

    memset(buf, 0xFF, FTS_MAX_POINTS_LENGTH);


    ret = fts_read(ts_data->spi, &cmd, 1, &buf[0], FTS_REPORT_BUFFER_SIZE);
    if (ret < 0) {
        TPD_INFO("read touch point one fail");
        return IRQ_IGNORE;
    }

    if ((0xEF == buf[1]) && (0xEF == buf[2]) && (0xEF == buf[3])) {
        /*recovery fw*/
        fts_fw_recovery(ts_data);
        return IRQ_IGNORE;
    }

    /*gesture*/
    if (gesture_enable && is_suspended) {
        if (gesture[0] == 0x01) {
            return IRQ_GESTURE;
        }
    }
    

    if ((buf[1] == 0xFF) && (buf[2] == 0xFF) && (buf[3] == 0xFF)) {
        TPD_INFO("Need recovery TP state");
        return IRQ_FW_AUTO_RESET;
    }

    /*TODO:confirm need print debug info*/
    if (ts_data->rbuf[0] != ts_data->irq_type) {
        SET_BIT(result_event, IRQ_FW_HEALTH);
    }
    ts_data->irq_type = ts_data->rbuf[0];

    /*normal touch*/
    SET_BIT(result_event, IRQ_TOUCH);

    return result_event;
}

static void fts_show_touch_buffer(u8 *data, int datalen)
{
    int i = 0;
    int count = 0;
    char *tmpbuf = NULL;

    tmpbuf = kzalloc(1024, GFP_KERNEL);
    if (!tmpbuf) {
        TPD_DEBUG("tmpbuf zalloc fail");
        return;
    }

    for (i = 0; i < datalen; i++) {
        count += snprintf(tmpbuf + count, 1024 - count, "%02X,", data[i]);
        if (count >= 1024)
            break;
    }
    TPD_DEBUG("point buffer:%s", tmpbuf);

    if (tmpbuf) {
        kfree(tmpbuf);
        tmpbuf = NULL;
    }
}

static int fts_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    int i = 0;
    int obj_attention= 0;
    int base = 0;
    int touch_point = 0;
    u8 point_num = 0;
    u8 pointid = 0;
    u8 event_flag = 0;
    u8 *buf = ts_data->rbuf;

    fts_show_touch_buffer(buf, FTS_REPORT_BUFFER_SIZE);

    point_num = buf[1] & 0xFF;
    if (point_num > max_num) {
        TPD_INFO("invalid point_num(%d),max_num(%d)", point_num, max_num);
        return -EIO;
    }

    for (i = 0; i < max_num; i++) {
        base = 6 * i;
        pointid = (buf[4 + base]) >> 4;
        if (pointid >= FTS_MAX_ID)
            break;
        else if (pointid >= max_num) {
            TPD_INFO("ID(%d) beyond max_num(%d)", pointid, max_num);
            return -EINVAL;
        }

        touch_point++;
        points[pointid].x = ((buf[2 + base] & 0x0F) << 8) + (buf[3 + base] & 0xFF);
        points[pointid].y = ((buf[4 + base] & 0x0F) << 8) + (buf[5 + base] & 0xFF);
        points[pointid].touch_major = buf[7 + base];
        points[pointid].width_major = buf[7 + base];
        points[pointid].z =  buf[6 + base];
        event_flag = (buf[2 + base] >> 6);

        points[pointid].status = 0;
        if ((event_flag == 0) || (event_flag == 2)) {
            points[pointid].status = 1;
            obj_attention |= (1 << pointid);
            if (point_num == 0) {
                TPD_INFO("abnormal touch data from fw");
                return -EIO;
            }
        }
    }

    if (touch_point == 0) {
        TPD_INFO("no touch point information");
        return -EIO;
    }

    return obj_attention;
}

/*TODO:*/
static void fts_health_report(void *chip_data, struct monitor_data *mon_data)
{
    int ret = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;

    ret = fts_read_reg(ts_data->spi, 0x01, &val);
    TPD_INFO("Health register(0x01):0x%x", ret);
    ret = fts_read_reg(ts_data->spi, FTS_REG_HEALTH_1, &val);
    TPD_INFO("Health register(0xFD):0x%x", ret);
    ret = fts_read_reg(ts_data->spi, FTS_REG_HEALTH_2, &val);
    TPD_INFO("Health register(0xFE):0x%x", ret);
}

static int fts_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 gesture_id = 0;
    u8 point_num = 0;
    u8 *gesture_buf = &ts_data->rbuf[FTS_MAX_POINTS_LENGTH];

    gesture_id = gesture_buf[2];
    point_num = gesture_buf[3];
    TPD_INFO("gesture_id=%d, point_num=%d", gesture_id, point_num);
    switch (gesture_id) {
    case GESTURE_DOUBLE_TAP:
        gesture->gesture_type = DouTap;
        break;
    case GESTURE_UP_VEE:
        gesture->gesture_type = UpVee;
        break;
    case GESTURE_DOWN_VEE:
        gesture->gesture_type = DownVee;
        break;
    case GESTURE_LEFT_VEE:
        gesture->gesture_type = LeftVee;
        break;
    case GESTURE_RIGHT_VEE:
        gesture->gesture_type = RightVee;
        break;
    case GESTURE_O_CLOCKWISE:
        gesture->clockwise = 1;
        gesture->gesture_type = Circle;
        break;
    case GESTURE_O_ANTICLOCK:
        gesture->clockwise = 0;
        gesture->gesture_type = Circle;
        break;
    case GESTURE_DOUBLE_SWIP:
        gesture->gesture_type = DouSwip;
        break;
    case GESTURE_LEFT2RIGHT_SWIP:
        gesture->gesture_type = Left2RightSwip;
        break;
    case GESTURE_RIGHT2LEFT_SWIP:
        gesture->gesture_type = Right2LeftSwip;
        break;
    case GESTURE_UP2DOWN_SWIP:
        gesture->gesture_type = Up2DownSwip;
        break;
    case GESTURE_DOWN2UP_SWIP:
        gesture->gesture_type = Down2UpSwip;
        break;
    case GESTURE_M:
        gesture->gesture_type = Mgestrue;
        break;
    case GESTURE_W:
        gesture->gesture_type = Wgestrue;
        break;
    case GESTURE_FINGER_PRINT:
        break;
    case GESTURE_SINGLE_TAP:
        gesture->gesture_type = SingleTap;
        break;
    default:
        gesture->gesture_type = UnkownGesture;
    }

    if ((gesture->gesture_type != FingerprintDown)
        && (gesture->gesture_type != FingerprintUp)
        && (gesture->gesture_type != UnkownGesture)) {
        gesture->Point_start.x = (u16)((gesture_buf[4] << 8) + gesture_buf[5]);
        gesture->Point_start.y = (u16)((gesture_buf[6] << 8) + gesture_buf[7]);
        gesture->Point_end.x = (u16)((gesture_buf[8] << 8) + gesture_buf[9]);
        gesture->Point_end.y = (u16)((gesture_buf[10] << 8) + gesture_buf[11]);
        gesture->Point_1st.x = (u16)((gesture_buf[12] << 8) + gesture_buf[13]);
        gesture->Point_1st.y = (u16)((gesture_buf[14] << 8) + gesture_buf[15]);
        gesture->Point_2nd.x = (u16)((gesture_buf[16] << 8) + gesture_buf[17]);
        gesture->Point_2nd.y = (u16)((gesture_buf[18] << 8) + gesture_buf[19]);
        gesture->Point_3rd.x = (u16)((gesture_buf[20] << 8) + gesture_buf[21]);
        gesture->Point_3rd.y = (u16)((gesture_buf[22] << 8) + gesture_buf[23]);
        gesture->Point_4th.x = (u16)((gesture_buf[24] << 8) + gesture_buf[25]);
        gesture->Point_4th.y = (u16)((gesture_buf[26] << 8) + gesture_buf[27]);
    }

    return 0;
}

static void fts_register_info_read(void *chip_data, uint16_t register_addr, uint8_t *result, uint8_t length)
{
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    u8 addr = (u8)register_addr;

    fts_read(ts_data->spi, &addr, 1, result, length);
}

static void fts_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
        ts_data->touch_direction = dir;
}

static uint8_t fts_get_touch_direction(void *chip_data)
{
        struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
        return ts_data->touch_direction;
}

void fts_black_screen_test(void *chip_data, char *msg)
{
    int ret = 0;
    struct fts_ts_data *ts_data = (struct fts_ts_data *)chip_data;
    struct touchpanel_data *ts = ts_data->ts;

    ts_data->s = NULL;
    ts_data->csv_fd = -1;
    
    focal_esd_check_enable(ts_data, false);
    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    
    ret = fts_test_entry(ts_data, 1);
    snprintf(msg, 256, "%d error(s). %s\n", ret, ret ? "" : "All test passed.");

    ts->ts_ops->reset(ts->chip_data);
    operate_mode_switch(ts);

    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    focal_esd_check_enable(ts_data, true);
}

static struct oplus_touchpanel_operations fts_ops = {
    .power_control              = fts_power_control,
    .get_vendor                 = fts_get_vendor,
    .get_chip_info              = fts_get_chip_info,
    .fw_check                   = fts_fw_check,
    .mode_switch                = fts_mode_switch,
    .reset                      = fts_reset,
    .reset_gpio_control         = fts_reset_gpio_control,
    .fw_update                  = fts_fw_update,
    .trigger_reason             = fts_trigger_reason,
    .u32_trigger_reason         = fts_u32_trigger_reason,
    .get_touch_points           = fts_get_touch_points,
    .health_report              = fts_health_report,
    .get_gesture_info           = fts_get_gesture_info,
    .ftm_process                = fts_ftm_process,
	.register_info_read         = fts_register_info_read,
	.set_touch_direction        = fts_set_touch_direction,
    .get_touch_direction        = fts_get_touch_direction,
    .esd_handle                 = fts_esd_handle,
    .black_screen_test          = fts_black_screen_test,
};

static struct fts_proc_operations fts_proc_ops = {
    .auto_test              = fts_auto_test,
};

static struct debug_info_proc_operations fts_debug_info_proc_ops = {
    .limit_read        = fts_limit_read_std,
    .delta_read        = fts_delta_read,
    .baseline_read     = fts_baseline_read,
    .baseline_blackscreen_read = fts_baseline_read,
    .main_register_read = fts_main_register_read,
};

struct focal_debug_func focal_debug_ops =
{
    .esd_check_enable       = focal_esd_check_enable,
    .get_esd_check_flag     = focal_get_esd_check_flag,
    .get_fw_version         = focal_get_fw_version,
    .dump_reg_sate          = focal_dump_reg_state,
};

static int fts_tp_probe(struct spi_device *spi)
{
        struct fts_ts_data *ts_data;
        struct touchpanel_data *ts = NULL;
        int ret = -1;

        TPD_INFO("%s  is called\n", __func__);

        if (tp_register_times > 0) {
                TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
                return -1;
        }

        /*step0:spi setup*/
        spi->mode = SPI_MODE_0;
        spi->bits_per_word = 8;
        spi->max_speed_hz = 8000000;

/*
        ret = spi_setup(spi);
        if (ret) {
            TPD_INFO("spi setup fail");
            return ret;
        }
*/
        /*step1:Alloc chip_info*/
        ts_data = kzalloc(sizeof(struct fts_ts_data), GFP_KERNEL);
        if (ts_data == NULL) {
                TPD_INFO("ts_data kzalloc error\n");
                ret = -ENOMEM;
                return ret;
        }
        memset(ts_data, 0, sizeof(*ts_data));
        fts_data = ts_data;

        /*step2:Alloc common ts*/
        ts = common_touch_data_alloc();
        if (ts == NULL) {
                TPD_INFO("ts kzalloc error\n");
                goto ts_malloc_failed;
        }
        memset(ts, 0, sizeof(*ts));

        /*step3:binding client && dev for easy operate*/
        ts->dev = &spi->dev;
        ts->s_client = spi;
        ts->irq = spi->irq;
        ts->chip_data = ts_data;
        
        ts_data->dev = ts->dev;
        ts_data->spi = spi;
        ts_data->hw_res = &ts->hw_res;
        ts_data->irq_num = ts->irq;
        ts_data->ts = ts;
        ts_data->syna_ops = &fts_proc_ops;
        ts_data->h_fw_file = fw_file;
        ts_data->h_fw_size = sizeof(fw_file);
        ts->debug_info_ops = &fts_debug_info_proc_ops;

        spi_set_drvdata(spi, ts);
	
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifdef CONFIG_SPI_MT65XX
		/* new usage of MTK spi API */
		memcpy(&ts_data->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
		ts->s_client->controller_data = (void *)&ts_data->spi_ctrl;
#else
		/* old usage of MTK spi API */
		ret = spi_setup(spi);
		if (ret) {
			 TPD_INFO("spi setup fail");
			 return ret;
		}
#endif	//CONFIG_SPI_MT65XX
#else // else of CONFIG_TOUCHPANEL_MTK_PLATFORM
		ret = spi_setup(spi);
		if (ret) {
			 TPD_INFO("spi setup fail");
			 return ret;
		}
		
#endif // end of CONFIG_TOUCHPANEL_MTK_PLATFORM

        /*step4:file_operations callback binding*/
        ts->ts_ops = &fts_ops;
        ts->private_data = &focal_debug_ops;

        /* Init communication interface */
        ret = fts_bus_init();
        if (ret) {
            TPD_INFO("bus initialize fail");
            goto err_bus_init;
        }

        /*step5:register common touch*/
        ret = register_common_touch_device(ts);
        if (ret < 0) {
                goto err_register_driver;
        }

	 ts_data->p_firmware_headfile = &ts->panel_data.firmware_headfile;
	 ts_data->h_fw_file = (u8 *)ts->panel_data.firmware_headfile.firmware_data;
        ts_data->h_fw_size = ts->panel_data.firmware_headfile.firmware_size;

        ts->tp_suspend_order = TP_LCD_SUSPEND;
        ts->tp_resume_order = LCD_TP_RESUME;

        /*step6:create proc/ftxxxx-debug files*/
        fts_create_apk_debug_channel(ts_data);

        /*step7:create focaltech related proc files*/
        fts_create_proc(ts, ts_data->syna_ops);

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
        if (ts->boot_mode == RECOVERY_BOOT || is_oem_unlocked() || ts->fw_update_in_probe_with_headfile)
#else
        if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY || is_oem_unlocked() || ts->fw_update_in_probe_with_headfile)
#endif
        {
            TPD_INFO("In Recovery mode,or oem unlocked,or fw_update_in_probe_with_headfile is enable,no-flash download fw by headfile\n");
            disable_irq_nosync(ts_data->ts->irq);
			ret = fts_fw_update(ts_data, NULL, 0);
            if(ret > 0) {
                TPD_INFO("fw update failed!\n");
            }
            enable_irq(ts_data->ts->irq);
        }

        ts_data->probe_done = 1;
        TPD_INFO("%s, probe normal end\n", __func__);

        return 0;

err_register_driver:
        common_touch_data_free(ts);
        ts = NULL;
        
err_bus_init:
        fts_bus_exit();
    
ts_malloc_failed:
        kfree(ts_data);
        ts_data = NULL;
        ret = -1;

        TPD_INFO("%s, probe error\n", __func__);

        return ret;
}

static int fts_tp_remove(struct spi_device *spi)
{
        struct touchpanel_data *ts = spi_get_drvdata(spi);
        struct fts_ts_data *ts_data = (struct fts_ts_data *)ts->chip_data;

        TPD_INFO("%s is called\n", __func__);
        fts_release_apk_debug_channel(ts_data);
        fts_bus_exit();
        kfree(ts_data);
        ts_data = NULL;
        kfree(ts);

        return 0;
}

static int fts_spi_suspend(struct device *dev)
{
        struct touchpanel_data *ts = dev_get_drvdata(dev);

        TPD_INFO("%s: is called\n", __func__);
        tp_i2c_suspend(ts);

        return 0;
}

static int fts_spi_resume(struct device *dev)
{
        struct touchpanel_data *ts = dev_get_drvdata(dev);

        TPD_INFO("%s is called\n", __func__);
        tp_i2c_resume(ts);

        return 0;
}

static const struct spi_device_id tp_id[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { "oplus,tp_noflash", 0 },
#else
    {TPD_DEVICE, 0},
#endif
    {},
};

static struct of_device_id tp_match_table[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { .compatible = "oplus,tp_noflash",},
#else
    { .compatible =  TPD_DEVICE,},
#endif
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
        .suspend = fts_spi_suspend,
        .resume = fts_spi_resume,
#endif
};

static struct spi_driver tp_spi_driver = {
        .probe          = fts_tp_probe,
        .remove         = fts_tp_remove,
        .id_table   = tp_id,
        .driver         = {
                .name   = TPD_DEVICE,
                .of_match_table =  tp_match_table,
                .pm = &tp_pm_ops,
        },
};

static int __init tp_driver_init(void)
{
        TPD_INFO("%s is called\n", __func__);

        if (!tp_judge_ic_match(TPD_DEVICE))
            return -1;

        if (spi_register_driver(&tp_spi_driver)!= 0) {
                TPD_INFO("unable to add spi driver.\n");
                return -1;
        }
        return 0;
}

/* should never be called */
static void __exit tp_driver_exit(void)
{
        spi_unregister_driver(&tp_spi_driver);
        return;
}
#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init);
#else
module_init(tp_driver_init);
#endif
module_exit(tp_driver_exit);

MODULE_DESCRIPTION("Touchscreen Driver");
MODULE_LICENSE("GPL");
