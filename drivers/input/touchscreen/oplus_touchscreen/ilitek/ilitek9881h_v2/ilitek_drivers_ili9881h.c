// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek_common.h"
#include "firmware.h"
#include "protocol.h"
#include "mp_test.h"
#include <linux/pm_wakeup.h>

/*******Part0: TAG Declear********************/
#define DTS_OF_NAME        "tchip,ilitek"
#define DEVICE_ID        "ILITEK_TDDI"

/* Debug level */
uint32_t ipio_debug_level = DEBUG_NONE;
EXPORT_SYMBOL(ipio_debug_level);
static struct wakeup_source *gesture_process_ws = NULL;

extern int tp_register_times;
struct ilitek_chip_data_9881h *ipd = NULL;
static struct oplus_touchpanel_operations ilitek_ops;

#ifndef CONFIG_TOUCHPANEL_MTK_PLATFORM
extern void disable_esd_thread(void);
#endif
static fw_update_state ilitek_fw_update(void *chip_data, const struct firmware *fw, bool force);
static int ilitek_reset_for_esd(void *chip_data);

/*******Part1:Common Function implement*******/
void ilitek_platform_disable_irq(void)
{
    unsigned long nIrqFlag = 0;

    TPD_DEBUG("IRQ = %d\n", ipd->isEnableIRQ);

    spin_lock_irqsave(&ipd->plat_spinlock, nIrqFlag);

    if (ipd->isEnableIRQ) {
        if (ipd->isr_gpio) {
            disable_irq_nosync(ipd->isr_gpio);
            ipd->isEnableIRQ = false;
            TPD_DEBUG("Disable IRQ: %d\n", ipd->isEnableIRQ);
        } else
            TPD_INFO("The number of gpio to irq is incorrect\n");
    } else
        TPD_DEBUG("IRQ was already disabled\n");

    spin_unlock_irqrestore(&ipd->plat_spinlock, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_disable_irq);

void ilitek_platform_enable_irq(void)
{
    unsigned long nIrqFlag = 0;

    TPD_DEBUG("IRQ = %d\n", ipd->isEnableIRQ);

    spin_lock_irqsave(&ipd->plat_spinlock, nIrqFlag);

    if (!ipd->isEnableIRQ) {
        if (ipd->isr_gpio) {
            enable_irq(ipd->isr_gpio);
            ipd->isEnableIRQ = true;
            TPD_DEBUG("Enable IRQ: %d\n", ipd->isEnableIRQ);
        } else
            TPD_INFO("The number of gpio to irq is incorrect\n");
    } else
        TPD_DEBUG("IRQ was already enabled\n");

    spin_unlock_irqrestore(&ipd->plat_spinlock, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_enable_irq);

int ilitek_platform_tp_hw_reset(bool isEnable)
{
    int ret = 0;
    int i = 0;
    mutex_lock(&ipd->plat_mutex);
    core_fr->isEnableFR = false;

    if (ipd->hw_res->reset_gpio) {
        if (isEnable) {
            core_config->ili_sleep_type = NOT_SLEEP_MODE;
            ret = core_firmware_get_hostdownload_data();
            if (ret < 0) {
                TPD_INFO("get host download data fail use default data\n");
                //goto out;
            }
            for (i = 0; i < 3; i++) {

                TPD_DEBUG("HW Reset: HIGH\n");
                gpio_direction_output(ipd->hw_res->reset_gpio, 1);
                mdelay(ipd->delay_time_high);
                TPD_DEBUG("HW Reset: LOW\n");
                gpio_set_value(ipd->hw_res->reset_gpio, 0);
                mdelay(ipd->delay_time_low);
                TPD_INFO("HW Reset: HIGH\n");
                gpio_set_value(ipd->hw_res->reset_gpio, 1);
                mdelay(ipd->edge_delay);

#ifdef HOST_DOWNLOAD
                //core_config_ice_mode_enable();
                ret = core_firmware_upgrade(ipd, true);
                if (ret >= 0) {
                    break;
                }
                else {
                    TPD_INFO("upgrade fail retry = %d\n", i);
                }
#endif
            }
        } else {
            TPD_INFO("HW Reset: LOW\n");
            gpio_set_value(ipd->hw_res->reset_gpio, 0);
        }
    }
    else {
        TPD_INFO("reset gpio is Invalid\n");
    }
    core_fr->handleint = false;
    core_fr->isEnableFR = true;
    mutex_unlock(&ipd->plat_mutex);
    return ret;
}
EXPORT_SYMBOL(ilitek_platform_tp_hw_reset);

/**
 * Calculate the check sum of each packet reported by firmware
 *
 * @pMsg: packet come from firmware
 * @nLength : the length of its packet
 */
uint8_t cal_fr_checksum(uint8_t *pMsg, uint32_t nLength)
{
    int i = 0;
    int32_t nCheckSum = 0;

    for (i = 0; i < nLength; i++) {
        nCheckSum += pMsg[i];
    }

    return (uint8_t) ((-nCheckSum) & 0xFF);
}
EXPORT_SYMBOL(cal_fr_checksum);


/*******Part2:SPI Interface Function implement*******/

static int ilitek_core_spi_write_then_read(struct spi_device *spi, uint8_t * txbuf,
    int w_len, uint8_t * rxbuf, int r_len)
{
    int res = 0;
    int retry = 2;

    if(!spi){
        TPD_INFO("spi is Null\n");
        return -1;
    }
    while(retry--) {
        if (spi_write_then_read(spi, txbuf, w_len, rxbuf, r_len) < 0) {
            TPD_INFO("spi Write Error, retry = %d\n", retry);
            msleep(20);
        }
        else {
            res = 0;
            break;
        }
    }
    if (retry < 0) {
        res = -1;
    }
    return res;
}

static int core_Rx_check(struct spi_device *spi, uint16_t check)
{
    int size = 0;
    int i = 0;
    int count = 100;
    uint8_t txbuf[5] = { 0 };
    uint8_t rxbuf[4] = {0};
    uint16_t status = 0;

    for(i = 0; i < count; i++)
    {
        txbuf[0] = SPI_WRITE;
        txbuf[1] = 0x25;
        txbuf[2] = 0x94;
        txbuf[3] = 0x0;
        txbuf[4] = 0x2;
        if (ilitek_core_spi_write_then_read(spi, txbuf, 5, txbuf, 0) < 0) {
            size = -EIO;
            TPD_INFO("spi Write Error, res = %d\n", size);
            return size;
        }
        txbuf[0] = SPI_READ;
        if (ilitek_core_spi_write_then_read(spi, txbuf, 1, rxbuf, 4) < 0) {
            size = -EIO;
            TPD_INFO("spi Write Error, res = %d\n", size);
            return size;
        }
        status = (rxbuf[2] << 8) + rxbuf[3];
        size = (rxbuf[0] << 8) + rxbuf[1];
        //TPD_INFO("count:%d,status =0x%x size: = %d\n", i, status, size);
        if(status == check)
            return size;
        mdelay(1);
    }
    if (ipio_debug_level & DEBUG_CONFIG) {
        core_config_get_reg_data(0x44008);
        core_config_get_reg_data(0x40054);
        core_config_get_reg_data(0x5101C);
        core_config_get_reg_data(0x51020);
        core_config_get_reg_data(0x4004C);
    }
    size = -EIO;
    return size;
}
static int core_Tx_unlock_check(struct spi_device *spi)
{
    int res = 0;
    int i = 0;
    int count = 100;
    uint8_t txbuf[5] = { 0 };
    uint8_t rxbuf[4] = {0};
    uint16_t unlock = 0;

    for(i = 0; i < count; i++)
    {
        txbuf[0] = SPI_WRITE;
        txbuf[1] = 0x25;
        txbuf[2] = 0x0;
        txbuf[3] = 0x0;
        txbuf[4] = 0x2;
        if (ilitek_core_spi_write_then_read(spi, txbuf, 5, txbuf, 0) < 0) {
            res = -EIO;
            TPD_INFO("spi Write Error, res = %d\n", res);
            return res;
        }
        txbuf[0] = SPI_READ;
        if (ilitek_core_spi_write_then_read(spi, txbuf, 1, rxbuf, 4) < 0) {
            res = -EIO;
            TPD_INFO("spi Write Error, res = %d\n", res);
            return res;
        }
        unlock = (rxbuf[2] << 8) + rxbuf[3];
        //TPD_INFO("count:%d,unlock =0x%x\n", i, unlock);
        if(unlock == 0x9881)
            return res;
        mdelay(1);
    }
    return res;
}
static int core_ice_mode_read_9881H11(struct spi_device *spi, uint8_t *data, uint32_t size)
{
    int res = 0;
    uint8_t txbuf[64] = { 0 };
    //set read address
    txbuf[0] = SPI_WRITE;
    txbuf[1] = 0x25;//default
    txbuf[2] = 0x98;//addr
    txbuf[3] = 0x0;//addr
    txbuf[4] = 0x2;//addr

    if (ilitek_core_spi_write_then_read(spi, txbuf, 5, txbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        return res;
    }
    //read data
    txbuf[0] = SPI_READ;
    if (ilitek_core_spi_write_then_read(spi, txbuf, 1, data, size) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        return res;
    }
    //write data lock
    txbuf[0] = SPI_WRITE;
    txbuf[1] = 0x25;
    txbuf[2] = 0x94;
    txbuf[3] = 0x0;
    txbuf[4] = 0x2;
    txbuf[5] = (size & 0xFF00) >> 8;
    txbuf[6] = size & 0xFF;
    txbuf[7] = (char)0x98;
    txbuf[8] = (char)0x81;
    if (ilitek_core_spi_write_then_read(spi, txbuf, 9, txbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
    }
    return res;
}

static int core_ice_mode_write_9881H11(struct spi_device *spi, uint8_t *data, uint32_t size)
{
    int res = 0;
    uint8_t check_sum = 0;
    uint8_t wsize = 0;
    uint8_t *txbuf;

    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*size+9, GFP_KERNEL);
    if (ERR_ALLOC_MEM(txbuf)) {
        TPD_INFO("Failed to allocate mem\n");
        res = -ENOMEM;
        goto out;
    }
    //write data
    txbuf[0] = SPI_WRITE;
    txbuf[1] = 0x25;
    txbuf[2] = 0x4;
    txbuf[3] = 0x0;
    txbuf[4] = 0x2;
    check_sum = cal_fr_checksum(data, size);
    memcpy(txbuf + 5, data, size);
    txbuf[5 + size] = check_sum;
    //size + checksum
    size++;
    wsize = size;
    if(wsize%4 != 0)
    {
        wsize += 4 - (wsize % 4);
    }
    if (ilitek_core_spi_write_then_read(spi, txbuf, wsize + 5, txbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        goto out;
    }
    //write data lock
    txbuf[0] = SPI_WRITE;
    txbuf[1] = 0x25;
    txbuf[2] = 0x0;
    txbuf[3] = 0x0;
    txbuf[4] = 0x2;
    txbuf[5] = (size & 0xFF00) >> 8;
    txbuf[6] = size & 0xFF;
    txbuf[7] = (char)0x5A;
    txbuf[8] = (char)0xA5;
    if (ilitek_core_spi_write_then_read(spi, txbuf, 9, txbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
    }
out:
    kfree(txbuf);
    txbuf=NULL;
    return res;
}

static int core_ice_mode_disable_9881H11(struct spi_device *spi)
{
    int res = 0;
    uint8_t txbuf[5] = {0};
    txbuf[0] = 0x82;
    txbuf[1] = 0x1B;
    txbuf[2] = 0x62;
    txbuf[3] = 0x10;
    txbuf[4] = 0x18;

    //TPD_INFO("FW ICE Mode disable\n");
    if (ilitek_core_spi_write_then_read(spi, txbuf, 5, txbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
    }
    return res;
}

static int core_ice_mode_enable_9881H11(struct spi_device *spi)
{
    int res = 0;
    uint8_t txbuf[5] = {0};
    uint8_t rxbuf[2]= {0};
    txbuf[0] = 0x82;
    txbuf[1] = 0x1F;
    txbuf[2] = 0x62;
    txbuf[3] = 0x10;
    txbuf[4] = 0x18;

    if (ilitek_core_spi_write_then_read(spi, txbuf, 1, rxbuf, 1) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        return res;
    }
    //check recover data
    if(rxbuf[0] == 0x82)
    {
        TPD_INFO("recover data rxbuf:0x%x\n", rxbuf[0]);
        return CHECK_RECOVER;
    }
    if (ilitek_core_spi_write_then_read(spi, txbuf, 5, rxbuf, 0) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
    }
    return res;
}

/*spi interface for finger report*/
static int core_spi_check_read_size(void)
{
    int res = 0;
    int size = 0;
    struct spi_device *temp_spi = NULL;

    temp_spi = ipd->spi;
    res = core_ice_mode_enable_9881H11(temp_spi);
    if (res < 0) {
        goto out;
    }
    size = core_Rx_check(temp_spi, 0x5AA5);
    if (size < 0) {
        res = -EIO;
        TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
        goto out;
    }
    return size;
out:
    return res;
}

static int core_spi_read_data_after_checksize(uint8_t *pBuf, uint16_t nSize)
{
    int res = 0;
    struct spi_device *temp_spi = NULL;

    temp_spi = ipd->spi;
    if (core_ice_mode_read_9881H11(temp_spi, pBuf, nSize) < 0) {
        res = -EIO;
        TPD_INFO("spi read Error, res = %d\n", res);
        goto out;
    }
    if (core_ice_mode_disable_9881H11(temp_spi) < 0) {
        res = -EIO;
        TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
        goto out;
    }
out:
    return res;
}

static int core_spi_read_9881H11(struct spi_device *spi, uint8_t *pBuf, uint16_t nSize)
{
    int res = 0;
    int size = 0;

    res = core_ice_mode_enable_9881H11(spi);
    if (res < 0) {
        goto out;
    }
    size = core_Rx_check(spi, 0x5AA5);
    if (size < 0) {
        res = -EIO;
        TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
        goto out;
    }
    if (size > nSize) {
        TPD_INFO("check read size > nSize  size = %d, nSize = %d\n", size, nSize);
        size = nSize;
    }
    if (core_ice_mode_read_9881H11(spi, pBuf, size) < 0) {
        res = -EIO;
        TPD_INFO("spi read Error, res = %d\n", res);
        goto out;
    }
    if (core_ice_mode_disable_9881H11(spi) < 0) {
        res = -EIO;
        TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
        goto out;
    }
    out:
    return res;
}

int core_spi_rx_check_test(void)
{
    int res = 0;
    int size = 0;
    struct spi_device *temp_spi = NULL;

    temp_spi = ipd->spi;

    res = core_ice_mode_enable_9881H11(temp_spi);
    if (res < 0) {
        TPD_INFO("ice mode enable error\n");
    }
    size = core_Rx_check(temp_spi, 0x5AA5);
    if (size < 0) {
        res = -EIO;
        TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
    }
    if (core_ice_mode_disable_9881H11(temp_spi) < 0) {
        res = -EIO;
        TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
    }
    return res;
}


static int core_spi_check_header(struct spi_device *spi, uint8_t *data, uint32_t size)
{
    int res = 0;
    uint8_t txbuf[5] = {0};
    uint8_t rxbuf[2]= {0};
    txbuf[0] = 0x82;

    if (ilitek_core_spi_write_then_read(spi, txbuf, 1, rxbuf, 1) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        return res;
    }
    data[0] = rxbuf[0];
    TPD_DEBUG("spi header data rxbuf:0x%x\n", rxbuf[0]);
    //check recover data
    if(rxbuf[0] == 0x82)
    {
        TPD_INFO("recover data rxbuf:0x%x\n", rxbuf[0]);
        return CHECK_RECOVER;
    }
    return 0;
}

/*for spi read or write function*/
static int core_spi_write_9881H11(struct spi_device *spi, uint8_t *pBuf, uint16_t nSize)
{
    int res = 0;
    uint8_t *txbuf;

    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*nSize+5, GFP_KERNEL);
    if (ERR_ALLOC_MEM(txbuf)) {
        TPD_INFO("Failed to allocate mem\n");
        res = -ENOMEM;
        goto out;
    }
    res = core_ice_mode_enable_9881H11(spi);
    if (res < 0) {
        goto out;
    }
    if (core_ice_mode_write_9881H11(spi, pBuf, nSize) < 0) {
        res = -EIO;
        TPD_INFO("spi Write Error, res = %d\n", res);
        goto out;
    }
    if(core_Tx_unlock_check(spi) < 0)
    {
        res = -ETXTBSY;
        TPD_INFO("check TX unlock Fail, res = %d\n", res);
    }
out:
    kfree(txbuf);
    txbuf = NULL;
    return res;
}

/*core spi write function for other moudule */

int core_spi_write(uint8_t *pBuf, uint16_t nSize)
{
    int res = 0;
    uint8_t *txbuf;
    struct spi_device *temp_spi = NULL;

    temp_spi = ipd->spi;

    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*nSize+1, GFP_KERNEL);
    if (ERR_ALLOC_MEM(txbuf)) {
        TPD_INFO("Failed to allocate mem\n");
        res = -ENOMEM;
        goto out;
    }
    if(core_config->icemodeenable == false)
    {
        res = core_spi_write_9881H11(temp_spi, pBuf, nSize);
        core_ice_mode_disable_9881H11(temp_spi);
        kfree(txbuf);
        return res;
    }

    txbuf[0] = SPI_WRITE;
    memcpy(txbuf+1, pBuf, nSize);
    if (ilitek_core_spi_write_then_read(temp_spi, txbuf, nSize+1, txbuf, 0) < 0) {
        if (core_config->do_ic_reset) {
            /* ignore spi error if doing ic reset */
            res = 0;
        } else {
            res = -EIO;
            TPD_INFO("spi Write Error, res = %d\n", res);
            goto out;
        }
    }

out:
    kfree(txbuf);
    txbuf = NULL;
    return res;
}
EXPORT_SYMBOL(core_spi_write);

/*core spi read function for other moudule */
int core_spi_read(uint8_t *pBuf, uint16_t nSize)
{
    int res = 0;
    uint8_t txbuf[1];
    struct spi_device *temp_spi = NULL;

    temp_spi = ipd->spi;
    txbuf[0] = SPI_READ;
    if(core_config->icemodeenable == false)
    {
        return core_spi_read_9881H11(temp_spi, pBuf, nSize);
    }
    if (ilitek_core_spi_write_then_read(temp_spi, txbuf, 1, pBuf, nSize) < 0) {
        if (core_config->do_ic_reset) {
            /* ignore spi error if doing ic reset */
            res = 0;
        } else {
            res = -EIO;
            TPD_INFO("spi Read Error, res = %d\n", res);
            goto out;
        }
    }
out:
    return res;
}
EXPORT_SYMBOL(core_spi_read);

static int core_spi_init(struct spi_device *spi)
{
    int ret = 0;

    if(!spi){
        TPD_INFO("core_spi_init:spi is Null\n");
        return -EINVAL;
    }
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;

    ret = spi_setup(spi);
    if (ret < 0){
        TPD_INFO("ERR: fail to setup spi\n");
        return -ENODEV;
    }
    TPD_INFO("%s:name=%s,bus_num=%d,cs=%d,mode=%d,speed=%d\n",__func__,spi->modalias,
     spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);
    return 0;
}

/*******Part3:Finger Report Function implement*******/

/* the total length of finger report packet */
static int g_total_len = 0;
/* the touch information of finger report packet */
static struct mutual_touch_info g_mutual_data;
/* the touch buffer of finger report packet */
static struct fr_data_node *g_fr_node = NULL;

/*finger report*/
struct core_fr_data *core_fr = NULL;

/*
 * It mainly parses the packet assembled by protocol v5.0
 */
static int parse_touch_package_v5_0(uint8_t pid)
{
    int i = 0;
    int res = 0;
    int index = 0;
    uint8_t check_sum = 0;
    uint32_t nX = 0;
    uint32_t nY = 0;
    uint32_t count = (core_config->tp_info->nXChannelNum * core_config->tp_info->nYChannelNum) * 2;

    for (i = 0; i < 9; i++)
        TPD_DEBUG("data[%d] = %x\n", i, g_fr_node->data[i]);

    check_sum = cal_fr_checksum(&g_fr_node->data[0], (g_fr_node->len - 1));
    TPD_DEBUG("data = %x  ;  check_sum : %x\n", g_fr_node->data[g_fr_node->len - 1], check_sum);

    if (g_fr_node->data[g_fr_node->len - 1] != check_sum) {
        TPD_INFO("Wrong checksum set pointid info = -1\n");
        TPD_INFO("check sum error data is:");
        for (i = 0; i < g_fr_node->len; i++) {
            TPD_DEBUG_NTAG("0x%02X, ", g_fr_node->data[i]);
        }
        TPD_DEBUG_NTAG("\n");
        g_mutual_data.pointid_info = -1;
        res = -1;
        goto out;
    }

    /* start to parsing the packet of finger report */
    if (pid == protocol->demo_pid) {
        TPD_DEBUG(" **** Parsing DEMO packets : 0x%x ****\n", pid);

        for (i = 0; i < MAX_TOUCH_NUM; i++) {
            if ((g_fr_node->data[(4 * i) + 1] == 0xFF) && (g_fr_node->data[(4 * i) + 2] == 0xFF)
                && (g_fr_node->data[(4 * i) + 3] == 0xFF)) {
                continue;
            }
            nX = (((g_fr_node->data[(4 * i) + 1] & 0xF0) << 4) | (g_fr_node->data[(4 * i) + 2]));
            nY = (((g_fr_node->data[(4 * i) + 1] & 0x0F) << 8) | (g_fr_node->data[(4 * i) + 3]));
            if (!core_fr->isSetResolution) {
                g_mutual_data.points[i].x = nX * (ipd->resolution_x) / TPD_WIDTH;
                g_mutual_data.points[i].y = nY * (ipd->resolution_y) / TPD_HEIGHT;
            } else {
                g_mutual_data.points[i].x = nX;
                g_mutual_data.points[i].y = nY;
            }
            g_mutual_data.points[i].z = g_fr_node->data[(4 * i) + 4];
            if (g_mutual_data.points[i].z == 0) {
                TPD_INFO("pressure = 0 force set 1\n");
                g_mutual_data.points[i].z = 1;
            }
            g_mutual_data.pointid_info = g_mutual_data.pointid_info | (1 << i);
            g_mutual_data.points[i].width_major = g_mutual_data.points[i].z;
            g_mutual_data.points[i].touch_major = g_mutual_data.points[i].z;
            g_mutual_data.points[i].status = 1;

            TPD_DEBUG("[x,y]=[%d,%d]\n", nX, nY);
            TPD_DEBUG("point[0x%x] : (%d,%d) = %d\n",
            g_mutual_data.pointid_info,
            g_mutual_data.points[i].x,
            g_mutual_data.points[i].y,
            g_mutual_data.points[i].z);

            g_mutual_data.touch_num++;

        }
    } else if (pid == protocol->debug_pid) {
        TPD_DEBUG(" **** Parsing DEBUG packets : 0x%x ****\n", pid);
        TPD_DEBUG("Length = %d\n", (g_fr_node->data[1] << 8 | g_fr_node->data[2]));

        for (i = 0; i < MAX_TOUCH_NUM; i++) {
            if ((g_fr_node->data[(3 * i) + 5] == 0xFF) && (g_fr_node->data[(3 * i) + 6] == 0xFF)
                && (g_fr_node->data[(3 * i) + 7] == 0xFF)) {
                continue;
            }

            nX = (((g_fr_node->data[(3 * i) + 5] & 0xF0) << 4) | (g_fr_node->data[(3 * i) + 6]));
            nY = (((g_fr_node->data[(3 * i) + 5] & 0x0F) << 8) | (g_fr_node->data[(3 * i) + 7]));
            if (!core_fr->isSetResolution) {
                g_mutual_data.points[i].x = nX * (ipd->resolution_x) / TPD_WIDTH;
                g_mutual_data.points[i].y = nY * (ipd->resolution_y) / TPD_HEIGHT;
            } else {
                g_mutual_data.points[i].x = nX;
                g_mutual_data.points[i].y = nY;
            }
            g_mutual_data.points[i].z = g_fr_node->data[(4 * i) + 4];
            if (g_mutual_data.points[i].z == 0) {
                TPD_INFO("pressure = 0 force set 1\n");
                g_mutual_data.points[i].z = 1;
            }
            g_mutual_data.pointid_info = g_mutual_data.pointid_info | (1 << i);
            g_mutual_data.points[i].width_major = g_mutual_data.points[i].z;
            g_mutual_data.points[i].touch_major = g_mutual_data.points[i].z;
            g_mutual_data.points[i].status = 1;

            TPD_DEBUG("[x,y]=[%d,%d]\n", nX, nY);
            TPD_DEBUG("point[0x%x] : (%d,%d) = %d\n",
            g_mutual_data.pointid_info,
            g_mutual_data.points[i].x,
            g_mutual_data.points[i].y,
            g_mutual_data.points[i].z);

            g_mutual_data.touch_num++;
        }
        if (ipd->oplus_read_debug_data && (!ERR_ALLOC_MEM(ipd->oplus_debug_buf))) {
            for (index = 0, i = 35; i < count + 35; i+=2, index++) {
                if((uint8_t)(g_fr_node->data[i] & 0x80) == (uint8_t)0x80)
                {
                    TPD_DEBUG("%d, ", (((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]) - 0x10000));
                    ipd->oplus_debug_buf[index] = (((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]) - 0x10000);
                }
                else
                {
                    TPD_DEBUG("%d, ", (g_fr_node->data[i] << 8) + g_fr_node->data[i+1]);
                    ipd->oplus_debug_buf[index] = ((g_fr_node->data[i] << 8) + g_fr_node->data[i+1]);
                }
            }
            ipd->oplus_read_debug_data = false;
        }
    } else {
        if (pid != 0) {
            /* ignore the pid with 0x0 after enable irq at once */
            TPD_INFO(" **** Unknown PID : 0x%x ****\n", pid);
            res = -1;
        }
    }

out:
    return res;
}

/*
 * The function is called by an interrupt and used to handle packet of finger
 * touch from firmware. A differnece in the process of the data is acorrding to the protocol
 */
static int finger_report_ver_5_0(void)
{
    int res = 0;
    uint8_t pid = 0x0;

    res = core_spi_read_data_after_checksize(g_fr_node->data, g_fr_node->len);

    if (res < 0) {
        TPD_INFO("Failed to read finger report packet\n");
        g_mutual_data.pointid_info = -1;
        goto out;
    }

    pid = g_fr_node->data[0];
    TPD_DEBUG("PID = 0x%x\n", pid);

    if (pid == protocol->i2cuart_pid) {
        TPD_DEBUG("I2CUART(0x%x): set pointid info = -1;\n", pid);
        //memcpy(&g_mutual_data, &pre_touch_info, sizeof(struct mutual_touch_info));
        g_mutual_data.pointid_info = -1;
        goto out;
    }

    if (pid == protocol->ges_pid && core_config->isEnableGesture) {
        TPD_DEBUG("pid = 0x%x, code = %x\n", pid, g_fr_node->data[1]);
        memcpy(g_mutual_data.gesture_data, g_fr_node->data, (g_total_len > GESTURE_INFO_LENGTH ? GESTURE_INFO_LENGTH : g_total_len));
        goto out;
    }

    res = parse_touch_package_v5_0(pid);
    if (res < 0) {
        TPD_INFO("Failed to parse packet of finger touch\n");
        goto out;
    }

    TPD_DEBUG("Touch Num = %d oplus id info 0x%X\n", g_mutual_data.touch_num, g_mutual_data.pointid_info);
out:
    return res;
}


/**
 * Calculate the length with different modes according to the format of protocol 5.0
 *
 * We compute the length before receiving its packet. If the length is differnet between
 * firmware and the number we calculated, in this case I just print an error to inform users
 * and still send up to users.
 */
static int calc_packet_length(void)
{
    int rlen = 0;
    /*spi interface*/
    rlen = core_spi_check_read_size();
    TPD_DEBUG("rlen = %d\n", rlen);
    return rlen;
}

/**
 * The table is used to handle calling functions that deal with packets of finger report.
 * The callback function might be different of what a protocol is used on a chip.
 *
 * It's possible to have the different protocol according to customer's requirement on the same
 * touch ic with customised firmware, so I don't have to identify which of the ic has been used; instead,
 * the version of protocol should match its parsing pattern.
 */
typedef struct {
    uint8_t protocol_marjor_ver;
    uint8_t protocol_minor_ver;
    int (*finger_report)(void);
} fr_hashtable;

static fr_hashtable fr_t[] = {
    //{0x5, 0x0, finger_report_ver_5_0},
    {0x5, 0x1, finger_report_ver_5_0},
};

/**
 * The function is an entry for the work queue registered by ISR activates.
 *
 * Here will allocate the size of packet depending on what the current protocol
 * is used on its firmware.
 */
static void core_fr_handler(void)
{
    int i = 0;
    int res = 0;
    uint8_t *tdata = NULL;

    mutex_lock(&ipd->plat_mutex);
    if (core_fr->isEnableFR && core_fr->handleint) {
        g_total_len = calc_packet_length();
        if (g_total_len > 0) {
            g_fr_node = kmalloc(sizeof(*g_fr_node), GFP_ATOMIC);
            if (ERR_ALLOC_MEM(g_fr_node)) {
                TPD_INFO("Failed to allocate g_fr_node memory %ld\n", PTR_ERR(g_fr_node));
                goto out;
            }

            g_fr_node->data = kcalloc(g_total_len, sizeof(uint8_t), GFP_ATOMIC);
            if (ERR_ALLOC_MEM(g_fr_node->data)) {
                TPD_INFO("Failed to allocate g_fr_node memory %ld\n", PTR_ERR(g_fr_node->data));
                goto out;
            }

            g_fr_node->len = g_total_len;
            memset(g_fr_node->data, 0xFF, (uint8_t) sizeof(uint8_t) * g_total_len);

            while (i < ARRAY_SIZE(fr_t)) {
                if (protocol->major == fr_t[i].protocol_marjor_ver) {
                    res = fr_t[i].finger_report();
                    if (res < 0) {
                        TPD_INFO("set pointid_info = -1\n");
                        g_mutual_data.pointid_info = -1;
                    }
                    /* 2048 is referred to the defination by user */
                    if (g_total_len < MAX_INT_DATA_LEN) {
                        //tdata = kmalloc(g_total_len, GFP_ATOMIC);
                        tdata = kmalloc(MAX_INT_DATA_LEN, GFP_ATOMIC);
                        if (ERR_ALLOC_MEM(tdata)) {
                            TPD_INFO("Failed to allocate g_fr_node memory %ld\n",
                                PTR_ERR(tdata));
                            goto out;
                        }

                        memcpy(tdata, g_fr_node->data, g_fr_node->len);
                    } else {
                        TPD_INFO("total length (%d) is too long than user can handle\n",
                            g_total_len);
                        goto out;
                    }

                    if (core_fr->isEnableNetlink) {
                        //netlink_reply_msg(tdata, g_total_len);
                        netlink_reply_msg(tdata, MAX_INT_DATA_LEN);
                    }

                    if (ipd->debug_node_open) {
                        mutex_lock(&ipd->ilitek_debug_mutex);
                        if (!ERR_ALLOC_MEM(ipd->debug_buf) && !ERR_ALLOC_MEM(ipd->debug_buf[ipd->debug_data_frame])) {
                            memset(ipd->debug_buf[ipd->debug_data_frame], 0x00,
                                   (uint8_t) sizeof(uint8_t) * MAX_INT_DATA_LEN);
                            memcpy(ipd->debug_buf[ipd->debug_data_frame], tdata, g_total_len);
                        }
                        else {
                            TPD_INFO("Failed to malloc debug_buf\n");
                        }
                        ipd->debug_data_frame++;
                        if (ipd->debug_data_frame > 1) {
                            TPD_INFO("ipd->debug_data_frame = %d\n", ipd->debug_data_frame);
                        }
                        if (ipd->debug_data_frame >= MAX_DEBUG_DATA_FRAME) {
                            TPD_INFO("ipd->debug_data_frame = %d > 1023\n",
                                ipd->debug_data_frame);
                            ipd->debug_data_frame = MAX_DEBUG_DATA_FRAME-1;
                        }
                        mutex_unlock(&ipd->ilitek_debug_mutex);
                        wake_up(&(ipd->inq));
                    }
                    break;
                }
                i++;
            }

            if (i >= ARRAY_SIZE(fr_t))
                TPD_INFO("Can't find any callback functions to handle INT event\n");
        }
        else {
            g_mutual_data.pointid_info = -1;
            TPD_INFO("Wrong the length of packet\n");
        }
    } else {
        TPD_INFO("The figner report was disabled\n");
        core_fr->handleint = true;
    }

out:
    mutex_unlock(&ipd->plat_mutex);
    if (CHECK_RECOVER == g_total_len) {
            TPD_INFO("==================Recover=================\n");
            ilitek_reset_for_esd((void *)ipd);
    }
    ipio_kfree((void **)&tdata);

    if(!ERR_ALLOC_MEM(g_fr_node)) {
        ipio_kfree((void **)&g_fr_node->data);
        ipio_kfree((void **)&g_fr_node);
    }
    g_total_len = 0;
    TPD_DEBUG("handle INT done\n");
}

static void core_fr_remove(void)
{
    TPD_INFO("Remove core-FingerReport members\n");
    ipio_kfree((void **)&core_fr);
}

static int core_fr_init(void)
{
    core_fr = kzalloc(sizeof(*core_fr), GFP_KERNEL);
    if (ERR_ALLOC_MEM(core_fr)) {
        TPD_INFO("Failed to allocate core_fr mem, %ld\n", PTR_ERR(core_fr));
        core_fr_remove();
        return -ENOMEM;
    }
    /*chip is 0x9881*/
    core_fr->isEnableFR = true;
    core_fr->handleint = true;
    core_fr->isEnableNetlink = false;
    core_fr->isEnablePressure = false;
    core_fr->isSetResolution = false;
    core_fr->actual_fw_mode = protocol->demo_mode;


    return 0;
}

/*******Part4:Call Back Function implement*******/
static int ilitek_ftm_process(void *chip_data)
{
    int ret = -1;

    TPD_INFO("\n");
    ret = core_firmware_boot_host_download(ipd);
    if (ret < 0) {
        TPD_INFO("Failed to upgrade firmware, ret = %d\n", ret);
    }

    TPD_INFO("FTM tp enter sleep\n");
    /*ftm sleep in */
    core_config->ili_sleep_type = SLEEP_IN_BEGIN_FTM;
    core_config_sleep_ctrl(false);

    return ret;
}

static void copy_fw_to_buffer(struct ilitek_chip_data_9881h *chip_info, const struct firmware *fw)
{
    if (fw) {
        //free already exist fw data buffer
        if (chip_info->tp_firmware.data) {
            kfree((void *)(chip_info->tp_firmware.data));
            chip_info->tp_firmware.data = NULL;
        }

        //new fw data buffer
        chip_info->tp_firmware.data = kmalloc(fw->size, GFP_KERNEL);
        if (chip_info->tp_firmware.data == NULL) {
            TPD_INFO("kmalloc tp firmware data error\n");

            chip_info->tp_firmware.data = kmalloc(fw->size, GFP_KERNEL);
            if (chip_info->tp_firmware.data == NULL) {
                TPD_INFO("retry kmalloc tp firmware data error\n");
                return;
            }
        }

        //copy bin fw to data buffer
        memcpy((u8 *)chip_info->tp_firmware.data, (u8 *)(fw->data), fw->size);
        if (0 == memcmp((u8 *)chip_info->tp_firmware.data, (u8 *)(fw->data), fw->size)) {
            TPD_INFO("copy_fw_to_buffer fw->size=%zu\n", fw->size);
            chip_info->tp_firmware.size = fw->size;
        } else {
            TPD_INFO("copy_fw_to_buffer fw error\n");
            chip_info->tp_firmware.size = 0;
        }
    }

    return;
}

int ilitek_reset(void *chip_data)
{
    int ret = -1;
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    const struct firmware *fw = NULL;

    TPD_INFO("chip_info->fw_name=%s, chip_info->tp_firmware.size=%zu\n",
              chip_info->fw_name, chip_info->tp_firmware.size);
    core_gesture->entry = false;

    //check fw exist and fw checksum ok
    if (chip_info->tp_firmware.size && chip_info->tp_firmware.data) {
        fw = &(chip_info->tp_firmware);
    }

    ret = ilitek_fw_update(chip_info, fw, 0);
    if(ret < 0) {
        TPD_INFO("fw update failed!\n");
    }
    return 0;
}

static int ilitek_reset_for_esd(void *chip_data)
{
    int ret = -1;
    int retry = 100;
    uint32_t reg_data = 0;
    uint8_t temp[64] = {0};
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    const struct firmware *fw = NULL;

    TPD_INFO("chip_info->fw_name=%s\n", chip_info->fw_name);

    core_gesture->entry = false;
    if (chip_info->tp_firmware.size && chip_info->tp_firmware.data) {
        fw = &(chip_info->tp_firmware);
    }
    if (P5_0_FIRMWARE_GESTURE_MODE != core_firmware->enter_mode) {
        ret = ilitek_fw_update(chip_info, fw, 0);
        if(ret < 0) {
            TPD_INFO("fw update failed!\n");
        }
    }
    else {
        core_firmware->esd_fail_enter_gesture = 1;
        ret = ilitek_fw_update(chip_info, fw, 0);
        if(ret < 0) {
            TPD_INFO("fw update failed!\n");
        }
        else {
            core_fr->isEnableFR = false;
            mdelay(150);
            core_config_ice_mode_enable();
            while(retry--) {
                reg_data = core_config_ice_mode_read(0x25FF8);
                if (reg_data == 0x5B92E7F4) {
                    TPD_DEBUG("check ok 0x25FF8 read 0x%X\n", reg_data);
                    break;
                }
                mdelay(10);
            }
            if (retry <= 0) {
                TPD_INFO("check  error 0x25FF8 read 0x%X\n", reg_data);
            }
            core_config_ice_mode_disable();
            core_gesture->entry = true;
            host_download(ipd, true);
            temp[0] = 0x01;
            temp[1] = 0x0A;
            temp[2] = 0x06;
            if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
                TPD_INFO("write command error\n");
            }
            core_fr->isEnableFR = true;
        }
        core_firmware->esd_fail_enter_gesture = 0;
    }
    return 0;
}

static int ilitek_power_control(void *chip_data, bool enable)
{

    TPD_INFO("set reset pin low\n");
    if (gpio_is_valid(ipd->hw_res->reset_gpio)) {
        gpio_direction_output(ipd->hw_res->reset_gpio, 0);
    }
    return 0;

}

static int ilitek_get_chip_info(void *chip_data)
{
    int ret = 0;
    TPD_INFO("\n");
    ret = 0;//core_config_get_chip_id();
    return ret;
}

static u8 ilitek_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    memset(&g_mutual_data, 0x0, sizeof(struct mutual_touch_info));
    TPD_DEBUG("gesture_enable = %d, is_suspended = %d\n", gesture_enable, is_suspended);

    chip_info->irq_timer = jiffies;    //reset esd check trigger base time
    if ((!ERR_ALLOC_MEM(core_mp) && (core_mp->run == true))) {
        chip_info->Mp_test_data_ready = true;
        TPD_INFO("Mp test data ready ok, return IRQ_IGNORE\n");
        return IRQ_IGNORE;
    }
    if ((gesture_enable == 1) && is_suspended) {
        if (gesture_process_ws) {
            TPD_INFO("black gesture process wake lock\n");
            __pm_stay_awake(gesture_process_ws);
        }
        mdelay(40);//wait for spi ok
        core_fr_handler();
        if (gesture_process_ws) {
            TPD_INFO("black gesture process wake unlock\n");
            __pm_relax(gesture_process_ws);
        }
        return IRQ_GESTURE;
    } else if (is_suspended) {
        return IRQ_IGNORE;
    }

    core_fr_handler();
    if (g_mutual_data.pointid_info == -1) {
        TPD_INFO("get point info error ignore\n");
        return IRQ_IGNORE;
    }
    return IRQ_TOUCH;
}


static int ilitek_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    memcpy(points, g_mutual_data.points, sizeof(struct point_info) * (chip_info->ts->max_num));
    return g_mutual_data.pointid_info;
}


static int ilitek_get_gesture_info(void *chip_data, struct gesture_info * gesture)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    uint8_t gesture_id = 0;
    uint8_t score = 0;
    int lu_x = 0;
    int lu_y = 0;
    int rd_x = 0;
    int rd_y = 0;
    uint8_t point_data[GESTURE_INFO_LENGTH + 1] = {0};

    memset(point_data, 0, sizeof(point_data));
    memcpy(point_data, g_mutual_data.gesture_data, GESTURE_INFO_LENGTH);

    if (point_data[0] != P5_0_GESTURE_PACKET_ID) {
        TPD_INFO("%s: read gesture data failed\n", __func__);
        return -1;
    }

    gesture_id = (uint8_t)(point_data[1]);
    score = point_data[36];

    gesture->Point_start.x = (((point_data[4] & 0xF0) << 4) | (point_data[5]));
    gesture->Point_start.y = (((point_data[4] & 0x0F) << 8) | (point_data[6]));
    gesture->Point_end.x   = (((point_data[7] & 0xF0) << 4) | (point_data[8]));
    gesture->Point_end.y   = (((point_data[7] & 0x0F) << 8) | (point_data[9]));

    gesture->Point_1st.x   = (((point_data[16] & 0xF0) << 4) | (point_data[17]));
    gesture->Point_1st.y   = (((point_data[16] & 0x0F) << 8) | (point_data[18]));
    gesture->Point_2nd.x   = (((point_data[19] & 0xF0) << 4) | (point_data[20]));
    gesture->Point_2nd.y   = (((point_data[19] & 0x0F) << 8) | (point_data[21]));
    gesture->Point_3rd.x   = (((point_data[22] & 0xF0) << 4) | (point_data[23]));
    gesture->Point_3rd.y   = (((point_data[22] & 0x0F) << 8) | (point_data[24]));

    switch (gesture_id)     //judge gesture type
    {
        case GESTURE_RIGHT :
            gesture->gesture_type  = Left2RightSwip;
            break;

        case GESTURE_LEFT :
            gesture->gesture_type  = Right2LeftSwip;
            break;

        case GESTURE_DOWN  :
            gesture->gesture_type  = Up2DownSwip;
            break;

        case GESTURE_UP :
            gesture->gesture_type  = Down2UpSwip;
            break;

        case GESTURE_DOUBLECLICK:
            gesture->gesture_type  = DouTap;
            gesture->Point_end     = gesture->Point_start;
            break;

        case GESTURE_V :
            gesture->gesture_type  = UpVee;
            break;

        case GESTURE_V_DOWN :
            gesture->gesture_type  = DownVee;
            break;

        case GESTURE_V_LEFT :
            gesture->gesture_type  = LeftVee;
            break;

        case GESTURE_V_RIGHT :
            gesture->gesture_type  = RightVee;
            break;

        case GESTURE_O  :
            gesture->gesture_type = Circle;
            gesture->clockwise = (point_data[34] > 1) ? 0 : point_data[34];

            lu_x = (((point_data[28] & 0xF0) << 4) | (point_data[29]));
            lu_y = (((point_data[28] & 0x0F) << 8) | (point_data[30]));
            rd_x = (((point_data[31] & 0xF0) << 4) | (point_data[32]));
            rd_y = (((point_data[31] & 0x0F) << 8) | (point_data[33]));

            gesture->Point_1st.x   = ((rd_x + lu_x) / 2);  //ymain
            gesture->Point_1st.y   = lu_y;
            gesture->Point_2nd.x   = lu_x;  //xmin
            gesture->Point_2nd.y   = ((rd_y + lu_y) / 2);
            gesture->Point_3rd.x   = ((rd_x + lu_x) / 2);  //ymax
            gesture->Point_3rd.y   = rd_y;
            gesture->Point_4th.x   = rd_x;  //xmax
            gesture->Point_4th.y   = ((rd_y + lu_y) / 2);
            break;

        case GESTURE_M  :
            gesture->gesture_type  = Mgestrue;
            break;

        case GESTURE_W :
            gesture->gesture_type  = Wgestrue;
            break;

        case GESTURE_TWOLINE_DOWN :
            gesture->gesture_type  = DouSwip;
            gesture->Point_1st.x   = (((point_data[10] & 0xF0) << 4) | (point_data[11]));
            gesture->Point_1st.y   = (((point_data[10] & 0x0F) << 8) | (point_data[12]));
            gesture->Point_2nd.x   = (((point_data[13] & 0xF0) << 4) | (point_data[14]));
            gesture->Point_2nd.y   = (((point_data[13] & 0x0F) << 8) | (point_data[15]));
            break;

        default:
            gesture->gesture_type = UnkownGesture;
            break;
    }
     TPD_DEBUG("gesture data 0-17 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X "
        "0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", \
        point_data[0], point_data[1], point_data[2], point_data[3], point_data[4], point_data[5], \
        point_data[6], point_data[7], point_data[8], point_data[9], point_data[10], point_data[11], \
        point_data[12], point_data[13], point_data[14], point_data[15], point_data[16], point_data[17]);

    TPD_DEBUG("gesture data 18-35 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X "
        "0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", \
        point_data[18], point_data[19], point_data[20], point_data[21], point_data[22], point_data[23], \
        point_data[24], point_data[25], point_data[26], point_data[27], point_data[28], point_data[29], \
        point_data[30], point_data[31], point_data[32], point_data[33], point_data[34], point_data[35]);

    TPD_INFO("gesture debug data 160-168 0x%02X 0x%02X "
        "0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", \
        point_data[160], point_data[161], point_data[162], point_data[163], \
        point_data[164], point_data[165], point_data[166], point_data[167], point_data[168]);

    TPD_DEBUG("before scale gesture_id: 0x%x, score: %d, gesture_type: %d, clockwise: %d,"
        "points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
                gesture_id, score, gesture->gesture_type, gesture->clockwise, \
                gesture->Point_start.x, gesture->Point_start.y, \
                gesture->Point_end.x, gesture->Point_end.y, \
                gesture->Point_1st.x, gesture->Point_1st.y, \
                gesture->Point_2nd.x, gesture->Point_2nd.y, \
                gesture->Point_3rd.x, gesture->Point_3rd.y, \
                gesture->Point_4th.x, gesture->Point_4th.y);

    if (!core_fr->isSetResolution) {
        gesture->Point_start.x = gesture->Point_start.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_start.y = gesture->Point_start.y * (chip_info->resolution_y) / TPD_HEIGHT;
        gesture->Point_end.x = gesture->Point_end.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_end.y = gesture->Point_end.y * (chip_info->resolution_y) / TPD_HEIGHT;
        gesture->Point_1st.x = gesture->Point_1st.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_1st.y = gesture->Point_1st.y * (chip_info->resolution_y) / TPD_HEIGHT;

        gesture->Point_2nd.x = gesture->Point_2nd.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_2nd.y = gesture->Point_2nd.y * (chip_info->resolution_y) / TPD_HEIGHT;

        gesture->Point_3rd.x = gesture->Point_3rd.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_3rd.y = gesture->Point_3rd.y * (chip_info->resolution_y) / TPD_HEIGHT;

        gesture->Point_4th.x = gesture->Point_4th.x * (chip_info->resolution_x) / TPD_WIDTH;
        gesture->Point_4th.y = gesture->Point_4th.y * (chip_info->resolution_y) / TPD_HEIGHT;
    }
    TPD_INFO("gesture_id: 0x%x, score: %d, gesture_type: %d, clockwise: %d, points:"
        "(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
                gesture_id, score, gesture->gesture_type, gesture->clockwise, \
                gesture->Point_start.x, gesture->Point_start.y, \
                gesture->Point_end.x, gesture->Point_end.y, \
                gesture->Point_1st.x, gesture->Point_1st.y, \
                gesture->Point_2nd.x, gesture->Point_2nd.y, \
                gesture->Point_3rd.x, gesture->Point_3rd.y, \
                gesture->Point_4th.x, gesture->Point_4th.y);

    return 0;
}


static int ilitek_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = 0;
    uint8_t temp[64] = {0};
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    mutex_lock(&chip_info->plat_mutex);
    if ((ERR_ALLOC_MEM(core_mp) || (!ERR_ALLOC_MEM(core_mp) && (core_mp->run == false))) \
        && (((!ERR_ALLOC_MEM(core_firmware))) && core_firmware->enter_mode != -1)) {
        switch(mode) {
            case MODE_NORMAL:
                TPD_DEBUG("MODE_NORMAL flag = %d\n", flag);
                ret = 0;
            break;

            case MODE_SLEEP:
                TPD_INFO("MODE_SLEEP flag = %d\n", flag);

                //lcd will goto sleep when tp suspend, close lcd esd check
                #ifndef CONFIG_TOUCHPANEL_MTK_PLATFORM
                    TPD_INFO("disable_esd_thread by tp driver\n");
                    disable_esd_thread();
                #endif

                if (P5_0_FIRMWARE_GESTURE_MODE != core_firmware->enter_mode) {
                    core_config_ic_suspend();
                }
                else {
                    core_config->ili_sleep_type = SLEEP_IN_GESTURE_PS;
                    /* sleep in */
                    core_config_sleep_ctrl(false);
                }
                //ilitek_platform_tp_hw_reset(false);
            break;

            case MODE_GESTURE:
                TPD_DEBUG("MODE_GESTURE flag = %d\n", flag);
                if (core_config->ili_sleep_type == SLEEP_IN_DEEP) {
                    TPD_INFO("TP in deep sleep mode is not support gesture mode flag = %d\n", flag);
                    break;
                }
                core_config->isEnableGesture = flag;
                if (flag) {
                    //lcd will goto sleep when tp suspend, close lcd esd check
                    #ifndef CONFIG_TOUCHPANEL_MTK_PLATFORM
                    TPD_INFO("disable_esd_thread by tp driver\n");
                    disable_esd_thread();
                    #endif

                    if (P5_0_FIRMWARE_GESTURE_MODE != core_firmware->enter_mode) {
                        core_config_ic_suspend();
                    }
                    else {
                        temp[0] = 0xF6;
                        temp[1] = 0x0A;
                         TPD_INFO("write prepare gesture command 0xF6 0x0A \n");
                        if ((core_write(core_config->slave_i2c_addr, temp, 2)) < 0) {
                            TPD_INFO("write prepare gesture command error\n");
                        }
                        temp[0] = 0x01;
                        temp[1] = 0x0A;
                        temp[2] = core_gesture->mode + 1;
                        TPD_INFO("write gesture command 0x01 0x0A, 0x%02X\n", core_gesture->mode + 1);
                        if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
                            TPD_INFO("write gesture command error\n");
                        }
                    }
                    core_fr->handleint = true;
                }
                break;

            case MODE_EDGE:
                TPD_DEBUG("MODE_EDGE flag = %d\n", flag);
                core_config_edge_limit_ctrl(chip_info, flag);
                chip_info->edge_limit_status = flag;
                break;

            case MODE_HEADSET:
                TPD_INFO("MODE_HEADSET flag = %d\n", flag);
                core_config_headset_ctrl(flag);
                chip_info->headset_status = flag;
                break;

            case MODE_CHARGE:
                TPD_INFO("MODE_CHARGE flag = %d\n", flag);
                if (chip_info->plug_status != flag) {
                    core_config_plug_ctrl(!flag);
                }
                else {
                    TPD_INFO("%s: already set plug status.\n", __func__);
                }
                chip_info->plug_status = flag;
                break;

            case MODE_GAME:
                TPD_INFO("MODE_GAME flag = %d\n", flag);
                if (chip_info->lock_point_status != flag) {
                    core_config_lock_point_ctrl(!flag);
                }
                else {
                    TPD_INFO("%s: already set game status.\n", __func__);
                }
                chip_info->lock_point_status = flag;
                break;

            default:
                TPD_INFO("%s: Wrong mode.\n", __func__);
        }
    }else {
        TPD_INFO("not ready switch mode work_mode mode = %d flag = %d\n", mode, flag);
    }
    mutex_unlock(&chip_info->plat_mutex);
    return ret;
}

static fw_check_state ilitek_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    uint8_t ver_len = 0;
    int ret = 0;
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    TPD_INFO("%s: call\n", __func__);
    ret = core_config_get_fw_ver(chip_info);
    if (ret < 0) {
        TPD_INFO("%s: get fw info failed\n", __func__);
    } else {
        panel_data->TP_FW = core_config->firmware_ver[3];
        sprintf(dev_version, "%02X", core_config->firmware_ver[3]);
        TPD_INFO("core_config->firmware_ver = %02X\n",
                   core_config->firmware_ver[3]);

        if (panel_data->manufacture_info.version) {
            ver_len = strlen(panel_data->manufacture_info.version);
            strlcpy(&(panel_data->manufacture_info.version[12]), dev_version, 3);
        }
        TPD_INFO("manufacture_info.version: %s\n", panel_data->manufacture_info.version);
    }
    chip_info->fw_version = panel_data->manufacture_info.version;
    return FW_NORMAL;
}

static fw_update_state ilitek_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;

    TPD_INFO("%s start\n", __func__);
    //request firmware failed, get from headfile
    if(fw == NULL) {
        TPD_INFO("request firmware failed\n");
    }
    ipd->common_reset = 1;
    core_firmware->fw = fw;
    ret = ilitek_platform_tp_hw_reset(true);
    if (ret < 0) {
        TPD_INFO("Failed to upgrade firmware, ret = %d\n", ret);
        return -1;
    }
    core_firmware->fw = NULL;
    ipd->common_reset = 0;
    return FW_UPDATE_SUCCESS;
}

static fw_update_state ilitek_fw_update_common(void *chip_data, const struct firmware *fw, bool force)
{
    int ret = 0;
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    TPD_INFO("%s start\n", __func__);

    //request firmware failed, get from headfile
    if(fw == NULL) {
        TPD_INFO("request firmware failed\n");
    }
    ipd->common_reset = 1;
    core_firmware->fw = fw;
    copy_fw_to_buffer(chip_info, fw);

    ret = ilitek_platform_tp_hw_reset(true);
    if (ret < 0) {
        TPD_INFO("Failed to upgrade firmware, ret = %d\n", ret);
        return -1;
    }
    core_firmware->fw = NULL;
    ipd->common_reset = 0;
    return FW_UPDATE_SUCCESS;
}

static int ilitek_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len-3] == 'i') && \
        (panel_data->fw_name[len-2] == 'm') && (panel_data->fw_name[len-1] == 'g')) {
        panel_data->fw_name[len-3] = 'b';
        panel_data->fw_name[len-2] = 'i';
        panel_data->fw_name[len-1] = 'n';
    }
    len = strlen(panel_data->test_limit_name);
    if ((len > 3)) {
        panel_data->test_limit_name[len-3] = 'i';
        panel_data->test_limit_name[len-2] = 'n';
        panel_data->test_limit_name[len-1] = 'i';
    }
    chip_info->tp_type = panel_data->tp_type;
    /*get ftm firmware ini from touch.h*/
    chip_info->p_firmware_headfile = &panel_data->firmware_headfile;
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s panel_data->test_limit_name = %s\n", \
        chip_info->tp_type, panel_data->fw_name, panel_data->test_limit_name);

    return 0;
}

static void ilitek_black_screen_test(void *chip_data, char *message)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    TPD_INFO("enter %s\n", __func__);
    ilitek_mp_black_screen_test(chip_info, message);
}

static int ilitek_esd_handle(void *chip_data)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    unsigned int timer = jiffies_to_msecs(jiffies - chip_info->irq_timer);
    int ret = 0;
    uint8_t buf[8] = {0};

    mutex_lock(&chip_info->plat_mutex);
    if (!(chip_info->esd_check_enabled)) {
        TPD_INFO("esd_check_enabled =  %d\n",chip_info->esd_check_enabled);
        goto out;
    }
    if ((timer > ILITEK_TOUCH_ESD_CHECK_PERIOD) && chip_info->esd_check_enabled) {
        TPD_DEBUG("do ESD check, timer = %d\n", timer);
        ret = core_spi_check_header(chip_info->spi, buf, 1);
        /* update interrupt timer */
        chip_info->irq_timer = jiffies;
    }
out:
    mutex_unlock(&chip_info->plat_mutex);
    if(ret == CHECK_RECOVER)
    {
        tp_touch_btnkey_release();
        chip_info->esd_retry++;
        TPD_INFO("Recover esd_retry = %d\n", chip_info->esd_retry);
        ilitek_reset_for_esd((void *)chip_info);
    }
    return 0;
}

static void ilitek_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

        chip_info->touch_direction = dir;
}

static uint8_t ilitek_get_touch_direction(void *chip_data)
{
        struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

        return chip_info->touch_direction;
}

static struct oplus_touchpanel_operations ilitek_ops = {
    .ftm_process                = ilitek_ftm_process,
    .reset                      = ilitek_reset,
    .power_control              = ilitek_power_control,
    .get_chip_info              = ilitek_get_chip_info,
    .trigger_reason             = ilitek_trigger_reason,
    .get_touch_points           = ilitek_get_touch_points,
    .get_gesture_info           = ilitek_get_gesture_info,
    .mode_switch                = ilitek_mode_switch,
    .fw_check                   = ilitek_fw_check,
    .fw_update                  = ilitek_fw_update_common,
    .get_vendor                 = ilitek_get_vendor,
    //.get_usb_state                    = ilitek_get_usb_state,
    .black_screen_test          = ilitek_black_screen_test,
    .esd_handle                 = ilitek_esd_handle,
    .set_touch_direction        = ilitek_set_touch_direction,
    .get_touch_direction        = ilitek_get_touch_direction,
};

static int ilitek_read_debug_data(struct seq_file *s, struct ilitek_chip_data_9881h *chip_info, DEBUG_READ_TYPE read_type)
{
    uint8_t test_cmd[4] = { 0 };
    int i = 0;
    int j = 0;
    int xch = core_config->tp_info->nXChannelNum;
    int ych = core_config->tp_info->nYChannelNum;

    chip_info->oplus_debug_buf = (int *)kzalloc((xch * ych) * sizeof(int), GFP_KERNEL);
    if (ERR_ALLOC_MEM(chip_info->oplus_debug_buf)) {
        TPD_INFO("Failed to allocate oplus_debug_buf memory, %ld\n", PTR_ERR(chip_info->oplus_debug_buf));
        return -ENOMEM;
    }

    test_cmd[0] = protocol->debug_mode;
    core_config_mode_control(test_cmd);
    ilitek_platform_disable_irq();
    test_cmd[0] = 0xFA;
    test_cmd[1] = 0x08;
    switch (read_type) {
    case ILI_RAWDATA:
        test_cmd[1] = 0x08;
        break;
    case ILI_DIFFDATA:
        test_cmd[1] = 0x03;
        break;
    case ILI_BASEDATA:
        test_cmd[1] = 0x08;
        break;
    default:

    break;
    }
    TPD_INFO("debug cmd 0x%X, 0x%X", test_cmd[0], test_cmd[1]);
    core_write(core_config->slave_i2c_addr, test_cmd, 2);
    ilitek_platform_enable_irq();
    mutex_unlock(&ipd->ts->mutex);
    enable_irq(ipd->isr_gpio);//because oplus disable
    chip_info->oplus_read_debug_data = true;
    for (i = 0; i < 1000; i++) {
        msleep(5);
        if (!chip_info->oplus_read_debug_data) {
            TPD_INFO("already read debug data\n");
            break;
        }
    }
    disable_irq_nosync(ipd->isr_gpio);
    msleep(15);
    switch (read_type) {
    case ILI_RAWDATA:
        seq_printf(s, "raw_data:\n");
        break;
    case ILI_DIFFDATA:
        seq_printf(s, "diff_data:\n");
        break;
    case ILI_BASEDATA:
        seq_printf(s, "basline_data:\n");
        break;
    default:
        seq_printf(s, "read type not support\n");
    break;
    }
    for (i = 0; i < ych; i++) {
        seq_printf(s, "[%2d]", i);
        for (j = 0; j < xch; j++) {
            seq_printf(s, "%5d, ", chip_info->oplus_debug_buf[i * xch + j]);
        }
        seq_printf(s, "\n");
    }

    mutex_lock(&ipd->ts->mutex);
    test_cmd[0] = protocol->demo_mode;
    core_config_mode_control(test_cmd);
    mutex_lock(&ipd->plat_mutex);
    ipio_kfree((void **)&chip_info->oplus_debug_buf);
    mutex_unlock(&ipd->plat_mutex);
    return 0;
}

static void ilitek_delta_read(struct seq_file *s, void *chip_data)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    if (s->size <= (MAX_DEBUG_NODE_LEN * 2)) {
        s->count = s->size;
        return;
    }
    ilitek_read_debug_data(s, chip_info, ILI_DIFFDATA);
}

static void ilitek_baseline_read(struct seq_file *s, void *chip_data)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
    TPD_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
    if (s->size <= (MAX_DEBUG_NODE_LEN * 2)) {
        s->count = s->size;
        return;
    }
    ilitek_read_debug_data(s, chip_info, ILI_BASEDATA);
}

static void ilitek_main_register_read(struct seq_file *s, void *chip_data)
{
    TPD_INFO("\n");
}

static struct debug_info_proc_operations debug_info_proc_ops = {
    //.limit_read    = nvt_limit_read,
    .baseline_read = ilitek_baseline_read,
    .delta_read = ilitek_delta_read,
    .main_register_read = ilitek_main_register_read,
};

/*******Part5:Platform Bus Registor*******/

/**
 * The function is to initialise all necessary structurs in those core APIs,
 * they must be called before the i2c dev probes up successfully.
 */
static int ilitek_platform_core_init(void)
{
    TPD_INFO("Initialise core's components\n");

    if (core_config_init() < 0 || core_protocol_init() < 0 ||
        core_firmware_init() < 0 || core_fr_init() < 0) {
        TPD_INFO("Failed to initialise core components\n");
        return -EINVAL;
    }
    if(core_spi_init(ipd->spi) < 0)
    {
        TPD_INFO("Failed to initialise core components\n");
        return -EINVAL;
    }
    return 0;
}

/**
 * Remove Core APIs memeory being allocated.
 */
static void ilitek_platform_core_remove(void)
{
    TPD_INFO("Remove all core's compoenets\n");
    ilitek_proc_remove();
    core_flash_remove();
    core_firmware_remove();
    core_fr_remove();
    core_config_remove();
    core_protocol_remove();
}

/**
 * The probe func would be called after an i2c device was detected by kernel.
 *
 * It will still return zero even if it couldn't get a touch ic info.
 * The reason for why we allow it passing the process is because users/developers
 * might want to have access to ICE mode to upgrade a firwmare forcelly.
 */
static int ilitek_platform_probe(struct spi_device *spi)
{
    int ret;
    struct touchpanel_data *ts = NULL;

    TPD_INFO("Probe Enter\n");
    if (tp_register_times > 0)

    {
        TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
        return -1;
    }

    /*step1:Alloc chip_info*/
    ipd = kzalloc(sizeof(*ipd), GFP_KERNEL);
    if (ERR_ALLOC_MEM(ipd)) {
        TPD_INFO("Failed to allocate ipd memory, %ld\n", PTR_ERR(ipd));
        return -ENOMEM;
    }

    /*step2:Alloc common ts*/
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto err_ts_malloc;
    }
    memset(ts, 0, sizeof(*ts));

    ipd->ts = ts;

    if(!spi) {
        goto err_spi_init;
    }
    spi->chip_select = 0; //modify reg=0 for more tp vendor share same spi interface
    ipd->spi = spi;

    ipd->chip_id = CHIP_TYPE_ILI9881;
    ipd->isEnableIRQ = true;

    TPD_INFO("Driver Version : %s\n", DRIVER_VERSION);
    TPD_INFO("Driver for Touch IC :  %x\n", CHIP_TYPE_ILI9881);

    /* 3. bind client and dev for easy operate */
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->s_client = spi;
    ts->irq = spi->irq;
    spi_set_drvdata(spi, ts);
    ts->dev = &spi->dev;
    ts->chip_data = ipd;
    ipd->hw_res = &ts->hw_res;
    //---prepare for spi parameter---
    if (ts->s_client->master->flags & SPI_MASTER_HALF_DUPLEX) {
        printk("Full duplex not supported by master\n");
        ret = -EIO;
        goto err_spi_setup;
    }
    /*
     * Different ICs may require different delay time for the reset.
     * They may also depend on what your platform need to.
     */
    if (ipd->chip_id == CHIP_TYPE_ILI9881) {
        ipd->delay_time_high = 10;
        ipd->delay_time_low = 5;
         /*spi interface*/
        ipd->edge_delay = 1;
    } else {
        ipd->delay_time_high = 10;
        ipd->delay_time_low = 10;
        ipd->edge_delay = 10;
    }

    mutex_init(&ipd->plat_mutex);
    spin_lock_init(&ipd->plat_spinlock);

    /* Init members for debug */
    mutex_init(&ipd->ilitek_debug_mutex);
    mutex_init(&ipd->ilitek_debug_read_mutex);
    init_waitqueue_head(&(ipd->inq));
    ipd->debug_data_frame = 0;
    ipd->debug_node_open = false;

    /* If kernel failes to allocate memory to the core components, driver will be unloaded. */
    if (ilitek_platform_core_init() < 0) {
        TPD_INFO("Failed to allocate cores' mem\n");
        goto err_spi_setup;
    }

     /* file_operations callbacks binding */
    ts->ts_ops = &ilitek_ops;

    /*register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    /*for firmware ini*/
    ipd->p_firmware_headfile = &ts->panel_data.firmware_headfile;
    ts->tp_suspend_order = TP_LCD_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
    ipd->isr_gpio = ts->irq;
    ipd->fw_name = ts->panel_data.fw_name;
    ipd->test_limit_name = ts->panel_data.test_limit_name;
    ipd->resolution_x = ts->resolution_info.max_x;
    ipd->resolution_y = ts->resolution_info.max_y;
    ipd->fw_edge_limit_support = ts->fw_edge_limit_support;

    TPD_INFO("reset_gpio = %d irq_gpio = %d irq = %d ipd->fw_name = %s\n", \
        ipd->hw_res->reset_gpio, ipd->hw_res->irq_gpio, ipd->isr_gpio, ipd->fw_name);
    TPD_INFO("resolution_x = %d resolution_y = %d\n", ipd->resolution_x, ipd->resolution_y);

    if (core_firmware_get_h_file_data() < 0)
        TPD_INFO("Failed to get h file data\n");

    /* Create nodes for ipd debug*/
    ilitek_proc_init(ts);

    /* Create nodes for oplus debug */
    ilitek_create_proc_for_oplus(ts);

    if (ts->esd_handle_support) {
        ts->esd_info.esd_work_time = msecs_to_jiffies(ILITEK_TOUCH_ESD_CHECK_PERIOD); // change esd check interval to 1.5s
        TPD_INFO("%s:change esd handle time to %d ms\n", __func__, ts->esd_info.esd_work_time/HZ);
    }
    /* Register wake_lock for gesture */
    gesture_process_ws = wakeup_source_register("gesture_wake_lock");
    if (!gesture_process_ws) {
        TPD_INFO("gesture_process_ws request failed\n");
        goto err_spi_setup;
        return -1;
    }
    TPD_INFO("end\n");
    return 0;

err_register_driver:
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY
        || ts->boot_mode == MSM_BOOT_MODE__RF
        || ts->boot_mode == MSM_BOOT_MODE__WLAN)) {
        gesture_process_ws = wakeup_source_register("gesture_wake_lock");
        TPD_INFO("ftm mode probe end ok\n");
        return 0;
    }

err_spi_setup:
    spi_set_drvdata(spi, NULL);

err_spi_init:

    common_touch_data_free(ts);
    ts = NULL;

err_ts_malloc:
    kfree(ipd);
    ipd = NULL;

    return -1;
}

static int ilitek_platform_remove(struct spi_device *spi)
{
    struct touchpanel_data *ts = spi_get_drvdata(spi);

    TPD_INFO("Remove platform components\n");

    wakeup_source_unregister(gesture_process_ws);

    ipio_kfree((void **)&ipd);
    ilitek_platform_core_remove();

    spi_set_drvdata(spi, NULL);
    kfree(ts);
    return 0;
}

/*
 * The name in the table must match the definiation
 * in a dts file.
 *
 */
static struct of_device_id tp_match_table[] = {
    {.compatible = DTS_OF_NAME},
    {},
};

static int ilitek_spi_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called\n", __func__);

    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY
        || ts->boot_mode == MSM_BOOT_MODE__RF
        || ts->boot_mode == MSM_BOOT_MODE__WLAN)) {

        TPD_INFO("ilitek_spi_suspend do nothing in ftm\n");
        return 0;
    }

    tp_i2c_suspend(ts);

    return 0;
}

/* lcd : add for ilitek tp deep sleep in ftm mode*/
void tp_goto_sleep_ftm(void)
{
    int ret = 0;

    if(ipd != NULL && ipd->ts != NULL) {
        TPD_INFO("ipd->ts->boot_mode = %d\n", ipd->ts->boot_mode);

        if ((ipd->ts->boot_mode == MSM_BOOT_MODE__FACTORY
            || ipd->ts->boot_mode == MSM_BOOT_MODE__RF
            || ipd->ts->boot_mode == MSM_BOOT_MODE__WLAN)) {
            ret = core_firmware_boot_host_download(ipd);

            //lcd will goto sleep when tp suspend, close lcd esd check
            #ifndef CONFIG_TOUCHPANEL_MTK_PLATFORM
            TPD_INFO("disable_esd_thread by tp driver\n");
            disable_esd_thread();
            #endif

            core_config_ic_suspend_ftm();

            mdelay(60);
            TPD_INFO("mdelay 60 ms test for ftm wait sleep\n");
        }
    }
}

static int ilitek_spi_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);

    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY
        || ts->boot_mode == MSM_BOOT_MODE__RF
        || ts->boot_mode == MSM_BOOT_MODE__WLAN)) {

        TPD_INFO("ilitek_spi_resume do nothing in ftm\n");
        return 0;
    }
    tp_i2c_resume(ts);

    return 0;
}

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = ilitek_spi_suspend,
    .resume = ilitek_spi_resume,
#endif
};

static struct spi_driver tp_spi_driver = {
    .driver = {
        .name    = DEVICE_ID,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
    .probe = ilitek_platform_probe,
    .remove = ilitek_platform_remove,
};

static int __init ilitek_platform_init(void)
{
    int res = 0;

    TPD_INFO("%s is called\n", __func__);
    if (!tp_judge_ic_match(TPD_DEVICE)) {
        TPD_INFO("TP driver is already register\n");
        return -1;
    }
    res = spi_register_driver(&tp_spi_driver);
    if (res < 0) {
        TPD_INFO("Failed to add spi driver\n");
        return -ENODEV;
    }

    TPD_INFO("Succeed to add driver\n");
    return res;
}

static void __exit ilitek_platform_exit(void)
{
    spi_unregister_driver(&tp_spi_driver);
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
