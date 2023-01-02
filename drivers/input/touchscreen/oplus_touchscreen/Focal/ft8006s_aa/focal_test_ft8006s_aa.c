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

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "focaltech_test"
#define TPD_INFO(a, arg...)  printk("[TP]"TPD_DEVICE ": " a, ##arg)
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
        }while(0)



#define FACTORY_REG_SHORT_TEST_EN               0x0F
#define FACTORY_REG_SHORT_TEST_STATE            0x10
#define FACTORY_REG_SHORT_ADDR                  0x89

#define FACTORY_REG_OPEN_START                  0x15
#define FACTORY_REG_OPEN_STATE                  0x16

#define FACTORY_REG_CB_ADDR_H                   0x18
#define FACTORY_REG_CB_ADDR_L                   0x19
#define FACTORY_REG_CB_ADDR                     0x6E

#define FACTORY_REG_CB_TEST_EN                  0x9F

#define FACTORY_REG_RAWDATA_TEST_EN             0x9E
#define FACTORY_REG_RAWDATA_ADDR                0x6A

#define FACTORY_REG_LCD_NOISE_START             0x11
#define FACTORY_REG_LCD_NOISE_FRAME             0x12
#define FACTORY_REG_LCD_NOISE_TEST_STATE        0x13
#define FACTORY_REG_LCD_NOISE_TTHR              0x14
#define LCD_NOISE_FRAME_NUM                     20


#define FTS_TEST_FUNC_ENTER() do { \
    TPD_INFO("[FTS_TS][TEST]%s: Enter\n", __func__); \
} while (0)

#define FTS_TEST_FUNC_EXIT()  do { \
    TPD_INFO("[FTS_TS][TEST]%s: Exit(%d)\n", __func__, __LINE__); \
} while (0)


#define FTS_TEST_SAVE_INFO(fmt, args...) do { \
    if (fts_data->s) { \
        seq_printf(fts_data->s, fmt, ##args); \
    } \
    TPD_INFO(fmt, ##args); \
} while (0)

#define FTS_TEST_SAVE_ERR(fmt, args...)  do { \
    if (fts_data->s) { \
        seq_printf(fts_data->s, fmt, ##args); \
    } \
    TPD_INFO(fmt, ##args); \
} while (0)



enum wp_type {
    WATER_PROOF_OFF = 0,
    WATER_PROOF_ON = 1,
    WATER_PROOF_ON_TX,
    WATER_PROOF_ON_RX,
    WATER_PROOF_OFF_TX,
    WATER_PROOF_OFF_RX,
};

enum byte_mode {
    DATA_ONE_BYTE,
    DATA_TWO_BYTE,
};

enum normalize_type {
    NORMALIZE_OVERALL,
    NORMALIZE_AUTO,
};


#define MAX_LENGTH_TEST_NAME            64


static void sys_delay(int ms)
{
    msleep(ms);
}

int focal_abs(int value)
{
    if (value < 0)
        value = 0 - value;

    return value;
}

void print_buffer(int *buffer, int length, int line_num)
{
    int i = 0;
    int j = 0;
    int tmpline = 0;
    char *tmpbuf = NULL;
    int tmplen = 0;
    int cnt = 0;

    if ((NULL == buffer) || (length <= 0)) {
        TPD_INFO("buffer/length(%d) fail\n", length);
        return;
    }

    tmpline = line_num ? line_num : length;
    tmplen = tmpline * 6 + 128;
    tmpbuf = kzalloc(tmplen, GFP_KERNEL);

    for (i = 0; i < length; i = i + tmpline) {
        cnt = 0;
        for (j = 0; j < tmpline; j++) {
            cnt += snprintf(tmpbuf + cnt, tmplen - cnt, "%5d ", buffer[i + j]);
            if ((cnt >= tmplen) || ((i + j + 1) >= length))
                break;
        }
        TPD_DEBUG("%s", tmpbuf);
    }

    if (tmpbuf) {
        kfree(tmpbuf);
        tmpbuf = NULL;
    }
}

/********************************************************************
 * test read/write interface
 *******************************************************************/
static int fts_test_bus_read(u8 *cmd, int cmdlen, u8 *data, int datalen)
{
    int ret = 0;

    ret = fts_read(fts_data->spi, cmd, cmdlen, data, datalen);
    if (ret < 0)
        return ret;
    else
        return 0;
}

static int fts_test_bus_write(u8 *writebuf, int writelen)
{
    int ret = 0;

    ret = fts_write(fts_data->spi, (char *)writebuf, writelen);
    if (ret < 0)
        return ret;
    else
        return 0;
}

static int fts_test_read_reg(u8 addr, u8 *val)
{
    return fts_test_bus_read(&addr, 1, val, 1);
}

static int fts_test_write_reg(u8 addr, u8 val)
{
    int ret;
    u8 cmd[2] = {0};

    cmd[0] = addr;
    cmd[1] = val;
    ret = fts_test_bus_write(cmd, 2);

    return ret;
}

static int fts_test_read(u8 addr, u8 *readbuf, int readlen)
{
    int ret = 0;
    int i = 0;
    int packet_length = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    int byte_num = readlen;

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;

    if (byte_num < BYTES_PER_TIME) {
        packet_length = byte_num;
    } else {
        packet_length = BYTES_PER_TIME;
    }
    /* FTS_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

    ret = fts_test_bus_read(&addr, 1, &readbuf[offset], packet_length);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read buffer fail\n");
        return ret;
    }
    for (i = 1; i < packet_num; i++) {
        offset += packet_length;
        if ((i == (packet_num - 1)) && packet_remainder) {
            packet_length = packet_remainder;
        }


        ret = fts_test_bus_read(&addr, 1, &readbuf[offset],
                                packet_length);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("read buffer fail\n");
            return ret;
        }
    }

    return 0;
}

/*
 * read_mass_data - read rawdata/short test data
 * addr - register addr which read data from
 * byte_num - read data length, unit:byte
 * buf - save data
 *
 * return 0 if read data succuss, otherwise return error code
 */
static int read_mass_data(u8 addr, int byte_num, int *buf)
{
    int ret = 0;
    int i = 0;
    u8 *data = NULL;

    data = (u8 *)kzalloc(byte_num * sizeof(u8), GFP_KERNEL);
    if (NULL == data) {
        FTS_TEST_SAVE_ERR("mass data buffer malloc fail\n");
        return -ENOMEM;
    }

    /* read rawdata buffer */
    TPD_INFO("mass data len:%d", byte_num);
    ret = fts_test_read(addr, data, byte_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read mass data fail\n");
        goto read_massdata_err;
    }

    for (i = 0; i < byte_num; i = i + 2) {
        buf[i >> 1] = (int)(short)((data[i] << 8) + data[i + 1]);
    }

    ret = 0;
read_massdata_err:
    kfree(data);
    return ret;
}

static void fts_test_save_data(char *name, int *data, int datacnt, int line, int fd)
{
    char *data_buf = NULL;
    u32 cnt = 0;
    u32 max_size = (datacnt * 8 + 128);
    int i = 0;

    if ((fd < 0) || !name || !data || !datacnt || !line) {
        FTS_TEST_SAVE_ERR("fd/name/data/datacnt/line is invalid\n");
        return;
    }

    data_buf = kzalloc(max_size, GFP_KERNEL);
    if (!data_buf) {
        FTS_TEST_SAVE_ERR("kzalloc for data_buf fail\n");
        return;
    }

    for (i = 0; i < datacnt; i++) {
        cnt += snprintf(data_buf + cnt, max_size - cnt, "%d,", data[i]);
        if ((i + 1) % line == 0)
            cnt += snprintf(data_buf + cnt, max_size - cnt, "\n");
    }

    if (i % line != 0)
        cnt += snprintf(data_buf + cnt, max_size - cnt, "\n");

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_write(fd, data_buf, cnt);
#else
    sys_write(fd, data_buf, cnt);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/

    kfree(data_buf);
}


/********************************************************************
 * test global function enter work/factory mode
 *******************************************************************/
static int enter_work_mode(void)
{
    int ret = 0;
    u8 mode = 0;
    int i = 0;
    int j = 0;

    TPD_INFO("%s +\n", __func__);
    ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
    if ((ret >= 0) && (0x00 == mode))
        return 0;

    for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
        ret = fts_test_write_reg(DEVIDE_MODE_ADDR, 0x00);
        if (ret >= 0) {
            sys_delay(FACTORY_TEST_DELAY);
            for (j = 0; j < 20; j++) {
                ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
                if ((ret >= 0) && (0x00 == mode)) {
                    TPD_INFO("enter work mode success");
                    return 0;
                } else
                    sys_delay(FACTORY_TEST_DELAY);
            }
        }

        sys_delay(50);
    }

    if (i >= ENTER_WORK_FACTORY_RETRIES) {
        FTS_TEST_SAVE_ERR("Enter work mode fail\n");
        return -EIO;
    }

    TPD_INFO("%s -\n", __func__);
    return 0;
}


#define FTS_FACTORY_MODE 0x40
static int enter_factory_mode(void)
{
    int ret = 0;
    u8 mode = 0;
    int i = 0;
    int j = 0;

    ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
    if ((ret >= 0) && (FTS_FACTORY_MODE == mode)) {
        return 0;
    }

    for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
        ret = fts_test_write_reg(DEVIDE_MODE_ADDR, 0x40);
        if (ret >= 0) {
            sys_delay(FACTORY_TEST_DELAY);
            for (j = 0; j < 20; j++) {
                ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
                if ((ret >= 0) && (FTS_FACTORY_MODE == mode)) {
                    TPD_INFO("enter factory mode success");
                    sys_delay(200);
                    return 0;
                } else
                    sys_delay(FACTORY_TEST_DELAY);
            }
        }

        sys_delay(50);
    }

    if (i >= ENTER_WORK_FACTORY_RETRIES) {
        FTS_TEST_SAVE_ERR("Enter factory mode fail\n");
        return -EIO;
    }

    return 0;
}

static int get_channel_num(struct fts_ts_data *ts_data)
{
    int ret = 0;
    u8 tx_num = 0;
    u8 rx_num = 0;

    ret = fts_test_read_reg(FACTORY_REG_CHX_NUM, &tx_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read tx_num register fail\n");
        return ret;
    }

    ret = fts_test_read_reg(FACTORY_REG_CHY_NUM, &rx_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rx_num register fail\n");
        return ret;
    }

    if ((tx_num != ts_data->hw_res->TX_NUM) || (rx_num != ts_data->hw_res->RX_NUM)) {
        FTS_TEST_SAVE_ERR("channel num check fail, tx_num:%d-%d, rx_num:%d-%d\n",
                          tx_num, ts_data->hw_res->TX_NUM,
                          rx_num, ts_data->hw_res->RX_NUM);
        return -EIO;
    }

    return 0;
}

static int start_scan(void)
{
    int ret = 0;
    u8 addr = 0;
    u8 val = 0;
    u8 finish_val = 0;
    int times = 0;

    addr = DEVIDE_MODE_ADDR;
    val = 0xC0;
    finish_val = 0x40;

    /* write register to start scan */
    ret = fts_test_write_reg(addr, val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write start scan mode fail\n");
        return ret;
    }

    /* Wait for the scan to complete */
    while (times++ < FACTORY_TEST_RETRY) {
        sys_delay(FACTORY_TEST_DELAY);

        ret = fts_test_read_reg(addr, &val);
        if ((ret >= 0) && (val == finish_val)) {
            break;
        } else
            TPD_INFO("reg%x=%x,retry:%d", addr, val, times);
    }

    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("scan timeout\n");
        return -EIO;
    }

    return 0;
}

static int short_get_adcdata_incell(u8 retval, u8 ch_num, int byte_num, int *adc_buf)
{
    int ret = 0;
    int times = 0;
    u8 short_state = 0;

    FTS_TEST_FUNC_ENTER();

    /* Start ADC sample */
    ret = fts_test_write_reg(FACTORY_REG_SHORT_TEST_EN, 0x01);
    if (ret) {
        FTS_TEST_SAVE_ERR("start short test fail\n");
        goto adc_err;
    }

    sys_delay(ch_num * FACTORY_TEST_DELAY);
    for (times = 0; times < FACTORY_TEST_RETRY; times++) {
        ret = fts_test_read_reg(FACTORY_REG_SHORT_TEST_STATE, &short_state);
        if ((ret >= 0) && (retval == short_state))
            break;
        else
            TPD_DEBUG("reg%x=%x,retry:%d",
                         FACTORY_REG_SHORT_TEST_STATE, short_state, times);

        sys_delay(FACTORY_TEST_RETRY_DELAY);
    }
    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
        ret = -EIO;
        goto adc_err;
    }

    ret = read_mass_data(FACTORY_REG_SHORT_ADDR, byte_num, adc_buf);
    if (ret) {
        FTS_TEST_SAVE_ERR("get short(adc) data fail\n");
    }

adc_err:
    FTS_TEST_FUNC_EXIT();
    return ret;
}


static int chip_clb(void)
{
    int ret = 0;
    u8 val = 0;
    int times = 0;

    /* start clb */
    ret = fts_test_write_reg(FACTORY_REG_CLB, 0x04);
    if (ret) {
        FTS_TEST_SAVE_ERR("write start clb fail\n");
        return ret;
    }

    while (times++ < FACTORY_TEST_RETRY) {
        sys_delay(FACTORY_TEST_RETRY_DELAY);
        ret = fts_test_read_reg(FACTORY_REG_CLB, &val);
        if ((0 == ret) && (0x02 == val)) {
            /* clb ok */
            break;
        } else
            TPD_DEBUG("reg%x=%x,retry:%d", FACTORY_REG_CLB, val, times);
    }

    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("chip clb timeout\n");
        return -EIO;
    }

    return 0;
}

static int get_cb_incell(u16 saddr, int byte_num, int *cb_buf)
{
    int ret = 0;
    int i = 0;
    u8 cb_addr = 0;
    u8 addr_h = 0;
    u8 addr_l = 0;
    int read_num = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    int addr = 0;
    u8 *data = NULL;

    data = (u8 *)kzalloc(byte_num * sizeof(u8), GFP_KERNEL);
    if (NULL == data) {
        FTS_TEST_SAVE_ERR("cb buffer malloc fail\n");
        return -ENOMEM;
    }

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;
    read_num = BYTES_PER_TIME;

    TPD_INFO("cb packet:%d,remainder:%d", packet_num, packet_remainder);
    cb_addr = FACTORY_REG_CB_ADDR;
    for (i = 0; i < packet_num; i++) {
        offset = read_num * i;
        addr = saddr + offset;
        addr_h = (addr >> 8) & 0xFF;
        addr_l = addr & 0xFF;
        if ((i == (packet_num - 1)) && packet_remainder) {
            read_num = packet_remainder;
        }

        ret = fts_test_write_reg(FACTORY_REG_CB_ADDR_H, addr_h);
        if (ret) {
            FTS_TEST_SAVE_ERR("write cb addr high fail\n");
            goto TEST_CB_ERR;
        }
        ret = fts_test_write_reg(FACTORY_REG_CB_ADDR_L, addr_l);
        if (ret) {
            FTS_TEST_SAVE_ERR("write cb addr low fail\n");
            goto TEST_CB_ERR;
        }

        ret = fts_test_read(cb_addr, data + offset, read_num);
        if (ret) {
            FTS_TEST_SAVE_ERR("read cb fail\n");
            goto TEST_CB_ERR;
        }
    }

    for (i = 0; i < byte_num; i++) {
        cb_buf[i] = data[i];
    }

TEST_CB_ERR:
    kfree(data);
    return ret;
}






#define NUM_MODE 2
static int fts_auto_preoperation(struct fts_ts_data *ts_data)
{
    int node_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM;

    ts_data->short_data = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
    if (!ts_data->short_data) {
        FTS_TEST_SAVE_ERR("kzalloc for short_data fail\n");
        goto alloc_err;
    }

    ts_data->open_data = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
    if (!ts_data->open_data) {
        FTS_TEST_SAVE_ERR("kzalloc for open_data fail\n");
        goto alloc_err;
    }


    ts_data->cb_data = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
    if (!ts_data->cb_data) {
        FTS_TEST_SAVE_ERR("kzalloc for cb_data fail\n");
        goto alloc_err;
    }

    ts_data->raw_data = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
    if (!ts_data->raw_data) {
        FTS_TEST_SAVE_ERR("kzalloc for raw_data fail\n");
        goto alloc_err;
    }

    ts_data->lcdnoise_data = (int *)kzalloc(node_num * sizeof(int), GFP_KERNEL);
    if (!ts_data->lcdnoise_data) {
        FTS_TEST_SAVE_ERR("kzalloc for lcdnoise_data fail\n");
        goto alloc_err;
    }

    return 0;

alloc_err:
    if (ts_data->short_data) {
        kfree(ts_data->short_data);
        ts_data->short_data = NULL;
    }
    if (ts_data->open_data) {
        kfree(ts_data->open_data);
        ts_data->open_data = NULL;
    }
    if (ts_data->cb_data) {
        kfree(ts_data->cb_data);
        ts_data->cb_data = NULL;
    }
    if (ts_data->raw_data) {
        kfree(ts_data->raw_data);
        ts_data->raw_data = NULL;
    }
    if (ts_data->lcdnoise_data) {
        kfree(ts_data->lcdnoise_data);
        ts_data->lcdnoise_data = NULL;
    }
    return -1;
}


static int fts_short_test(struct fts_ts_data *ts_data, bool *test_result)
{
    int ret = 0;
    int i = 0;
    bool tmp_result = false;
    int tmp_adc = 0;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;
    int byte_num = node_num * 2;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Short Circuit Test\n");
    if (!ts_data->short_data || !ts_data->fts_autotest_offset || !ts_data->fts_autotest_offset->node_valid) {
        FTS_TEST_SAVE_ERR("short_data/fts_autotest_offset is null\n");
        return -EINVAL;
    }

    if (!ts_data->fts_autotest_offset->fts_short_data_P || !ts_data->fts_autotest_offset->fts_short_data_N) {
        FTS_TEST_SAVE_ERR("fts_short_data_P || fts_short_data_N is NULL\n");
        return -EINVAL;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        goto test_err;
    }

    ret = short_get_adcdata_incell(TEST_RETVAL_AA, rx_num, byte_num, ts_data->short_data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get adc data fail\n");
        goto test_err;
    }

    /* calculate resistor */
    for (i = 0; i < node_num; i++) {
        tmp_adc = ts_data->short_data[i];
        if (tmp_adc <= 0) {
            ts_data->short_data[i] = -1;
            continue;
        } else if ((tmp_adc >= 1) && (tmp_adc <= 941)) {
            tmp_adc = 941;
        }

        ts_data->short_data[i] = (6090 * 4096 / (74 * tmp_adc - 17 * 4096)) - 20;
        if (ts_data->short_data[i] > 3000) {
            ts_data->short_data[i] = 3000;
        }
    }

    /* compare */
    tmp_result = true;
    for (i = 0; i < node_num; i++) {
        if (0 == ts_data->fts_autotest_offset->node_valid[i])
            continue;

        if ((ts_data->short_data[i] < ts_data->fts_autotest_offset->fts_short_data_N[i])
            || (ts_data->short_data[i] > ts_data->fts_autotest_offset->fts_short_data_P[i])) {
            TPD_INFO("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx_num + 1, i % rx_num + 1, ts_data->short_data[i],
                              ts_data->fts_autotest_offset->fts_short_data_N[i],
                              ts_data->fts_autotest_offset->fts_short_data_P[i]);
            tmp_result = false;
        }
    }
    

test_err:
    ret = fts_test_write_reg(FACTORY_REG_SHORT_TEST_STATE, 0x03);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write idle to short test state fail\n");
    }

    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ Short Circuit Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("------ Short Circuit Test NG\n");
    }

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int fts_open_test(struct fts_ts_data *ts_data, bool *test_result)
{
    int ret = 0;
    int i = 0;
    bool tmp_result = false;
    u8 state = 0;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;
    int byte_num = node_num;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Open Test\n");

    if (!ts_data->open_data || !ts_data->fts_autotest_offset || !ts_data->fts_autotest_offset->node_valid) {
        FTS_TEST_SAVE_ERR("open_data/fts_autotest_offset is null\n");
        return -EINVAL;
    }

    if (!ts_data->fts_autotest_offset->fts_open_data_P || !ts_data->fts_autotest_offset->fts_open_data_N) {
        FTS_TEST_SAVE_ERR("fts_open_data_P || fts_open_data_N is NULL\n");
        return -EINVAL;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        goto test_err;
    }

    ret = fts_test_write_reg(FACTORY_REG_OPEN_START, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("start open test fail\n");
        goto test_err;
    }

    /* check test status */
    for (i = 0; i < FACTORY_TEST_RETRY; i++) {
        sys_delay(FACTORY_TEST_RETRY_DELAY);
        ret = fts_test_read_reg(FACTORY_REG_OPEN_STATE, &state);
        if ((ret >= 0) && (TEST_RETVAL_AA == state)) {
            break;
        } else {
            TPD_DEBUG("reg%x=%x,retry:%d\n",
                         FACTORY_REG_OPEN_STATE, state, i);
        }
    }
    if (i >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("open test timeout\n");
        goto restore_reg;
    }

    /* get cb data */
    ret = get_cb_incell(0, byte_num, ts_data->open_data);
    if (ret) {
        FTS_TEST_SAVE_ERR("get CB fail\n");
        goto restore_reg;
    }

    /* compare */
    tmp_result = true;
    for (i = 0; i < node_num; i++) {
        if (0 == ts_data->fts_autotest_offset->node_valid[i])
            continue;

        if ((ts_data->open_data[i] < ts_data->fts_autotest_offset->fts_open_data_N[i])
            || (ts_data->open_data[i] > ts_data->fts_autotest_offset->fts_open_data_P[i])) {
            TPD_INFO("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx_num + 1, i % rx_num + 1, ts_data->open_data[i],
                              ts_data->fts_autotest_offset->fts_open_data_N[i],
                              ts_data->fts_autotest_offset->fts_open_data_P[i]);
            tmp_result = false;
        }
    }

restore_reg:
    /* auto clb */
    ret = chip_clb();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("auto clb fail\n");
    }

test_err:
    ret = fts_test_write_reg(FACTORY_REG_OPEN_STATE, 0x03);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write idle to open test state fail\n");
    }

    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ Open Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("------ Open Test NG\n");
    }

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int fts_cb_test(struct fts_ts_data *ts_data, bool *test_result)
{
    int ret = 0;
    int i = 0;
    bool tmp_result = false;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;
    int byte_num = node_num;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: CB Test\n");

    if (!ts_data->cb_data || !ts_data->fts_autotest_offset || !ts_data->fts_autotest_offset->node_valid) {
        FTS_TEST_SAVE_ERR("cb_data/fts_autotest_offset is null\n");
        return -EINVAL;
    }

    if (!ts_data->fts_autotest_offset->fts_cb_data_P || !ts_data->fts_autotest_offset->fts_cb_data_N) {
        FTS_TEST_SAVE_ERR("fts_cb_data_P || fts_cb_data_N is NULL\n");
        return -EINVAL;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        goto test_err;
    }

    /* cb test enable */
    ret = fts_test_write_reg(FACTORY_REG_CB_TEST_EN, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("cb test enable fail\n");
        goto test_err;
    }

    /* auto clb */
    ret = chip_clb();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("auto clb fail\n");
        goto test_err;
    }
    
    /* get cb data */
    ret = get_cb_incell(0, byte_num, ts_data->cb_data);
    if (ret) {
        FTS_TEST_SAVE_ERR("get CB fail\n");
        goto test_err;
    }

    /* compare */
    tmp_result = true;
    for (i = 0; i < node_num; i++) {
        if (0 == ts_data->fts_autotest_offset->node_valid[i])
            continue;

        if ((ts_data->cb_data[i] < ts_data->fts_autotest_offset->fts_cb_data_N[i])
            || (ts_data->cb_data[i] > ts_data->fts_autotest_offset->fts_cb_data_P[i])) {
            TPD_INFO("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx_num + 1, i % rx_num + 1, ts_data->cb_data[i],
                              ts_data->fts_autotest_offset->fts_cb_data_N[i],
                              ts_data->fts_autotest_offset->fts_cb_data_P[i]);
            tmp_result = false;
        }
    }

test_err:
    /* cb test disable */
    ret = fts_test_write_reg(FACTORY_REG_CB_TEST_EN, 0x00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("cb test disable fail\n");
    }

    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ CB Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("------ CB Test NG\n");
    }

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int fts_rawdata_test(struct fts_ts_data *ts_data, bool *test_result)
{
    int ret = 0;
    int i = 0;
    bool tmp_result = false;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;
    int byte_num = node_num * 2;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Rawdata Test\n");

    if (!ts_data->raw_data || !ts_data->fts_autotest_offset || !ts_data->fts_autotest_offset->node_valid) {
        FTS_TEST_SAVE_ERR("raw_data/fts_autotest_offset is null\n");
        return -EINVAL;
    }

    if (!ts_data->fts_autotest_offset->fts_raw_data_P || !ts_data->fts_autotest_offset->fts_raw_data_N) {
        FTS_TEST_SAVE_ERR("fts_raw_data_P || fts_raw_data_N is NULL\n");
        return -EINVAL;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
        goto test_err;
    }

    /* rawdata test enable */
    ret = fts_test_write_reg(FACTORY_REG_RAWDATA_TEST_EN, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("rawdata test enable fail\n");
        goto test_err;
    }


    /*********************GET RAWDATA*********************/
    /* start scanning */
    ret = start_scan();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("scan fail\n");
        goto test_err;
    }

    ret = fts_test_write_reg(FACTORY_REG_LINE_ADDR, 0xAD);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("rawdata test enable fail\n");
        goto test_err;
    }

    /* read rawdata */
    ret = read_mass_data(FACTORY_REG_RAWDATA_ADDR, byte_num, ts_data->raw_data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rawdata fail\n");
        goto test_err;
    }

    /* compare */
    tmp_result = true;
    for (i = 0; i < node_num; i++) {
        if (0 == ts_data->fts_autotest_offset->node_valid[i])
            continue;

        if ((ts_data->raw_data[i] < ts_data->fts_autotest_offset->fts_raw_data_N[i])
            || (ts_data->raw_data[i] > ts_data->fts_autotest_offset->fts_raw_data_P[i])) {
            TPD_INFO("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx_num + 1, i % rx_num + 1, ts_data->raw_data[i],
                              ts_data->fts_autotest_offset->fts_raw_data_N[i],
                              ts_data->fts_autotest_offset->fts_raw_data_P[i]);
            tmp_result = false;
        }
    }

test_err:
    /* rawdata test disble */
    ret = fts_test_write_reg(FACTORY_REG_RAWDATA_TEST_EN, 0x00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("rawdata test disable fail\n");
    }
    
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------Rawdata Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("------Rawdata Test NG\n");
    }

    FTS_TEST_FUNC_EXIT();
    return ret;
}



static int fts_lcdnoise_test(struct fts_ts_data *ts_data, bool *test_result)
{
    int ret = 0;
    int i = 0;
    bool tmp_result = false;
    u8 old_mode = 0;
    u8 status = 0;
    u8 touch_thr = 0;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;
    int byte_num = node_num * 2;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: LCD Noise Test\n");

    if (!ts_data->lcdnoise_data || !ts_data->fts_autotest_offset || !ts_data->fts_autotest_offset->node_valid) {
        FTS_TEST_SAVE_ERR("lcdnoise_data/fts_autotest_offset is null\n");
        return -EINVAL;
    }

    if (!ts_data->fts_autotest_offset->fts_lcdnoise_data_P || !ts_data->fts_autotest_offset->fts_lcdnoise_data_N) {
        FTS_TEST_SAVE_ERR("fts_lcdnoise_data_P || fts_lcdnoise_data_N is NULL\n");
        return -EINVAL;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
        goto test_err;
    }

    ret = fts_test_read_reg(FACTORY_REG_LCD_NOISE_TTHR, &touch_thr);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read reg14 fail\n");
        goto test_err;
    }

    ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &old_mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read reg06 fail\n");
        goto test_err;
    }

    ret =  fts_test_write_reg(FACTORY_REG_DATA_SELECT, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 1 to reg06 fail\n");
        goto test_err;
    }

    ret =  fts_test_write_reg(FACTORY_REG_LINE_ADDR, 0xAD);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write reg01 fail\n");
        goto test_err;
    }

    ret = fts_test_write_reg(FACTORY_REG_LCD_NOISE_FRAME, LCD_NOISE_FRAME_NUM / 4);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("write frame num fail\n");
        goto test_err;
    }

    /* start test */
    ret = fts_test_write_reg(FACTORY_REG_LCD_NOISE_START, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("start lcdnoise test fail\n");
        goto test_err;
    }

    /* check test status */
    sys_delay(LCD_NOISE_FRAME_NUM * FACTORY_TEST_DELAY / 2);
    for (i = 0; i < FACTORY_TEST_RETRY; i++) {
        status = 0xFF;
        ret = fts_test_read_reg(FACTORY_REG_LCD_NOISE_TEST_STATE, &status);
        if ((ret >= 0) && (TEST_RETVAL_AA == status)) {
            break;
        } else {
            TPD_DEBUG("reg%x=%x,retry:%d\n",
                         FACTORY_REG_LCD_NOISE_TEST_STATE, status, i);
        }
        sys_delay(FACTORY_TEST_RETRY_DELAY);
    }
    if (i >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("lcdnoise test timeout\n");
        goto test_err;
    }
    /* read lcdnoise */
    ret = read_mass_data(FACTORY_REG_RAWDATA_ADDR, byte_num, ts_data->lcdnoise_data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read lcdnoise data fail\n");
        goto test_err;
    }

    /* compare */
    //TODO
    tmp_result = true;
    for (i = 0; i < node_num; i++) {
        if (0 == ts_data->fts_autotest_offset->node_valid[i])
            continue;

        if ((ts_data->lcdnoise_data[i] < ts_data->fts_autotest_offset->fts_lcdnoise_data_N[i])
            || (ts_data->lcdnoise_data[i] > ts_data->fts_autotest_offset->fts_lcdnoise_data_P[i])) {
            TPD_INFO("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx_num + 1, i % rx_num + 1, ts_data->lcdnoise_data[i],
                              ts_data->fts_autotest_offset->fts_lcdnoise_data_N[i],
                              ts_data->fts_autotest_offset->fts_lcdnoise_data_P[i]);
            tmp_result = false;
        }
    }

test_err:
    ret = fts_test_write_reg(FACTORY_REG_LCD_NOISE_START, 0x00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 0 to reg11 fail\n");
    }

    ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, old_mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg06 fail\n");
    }

    ret = fts_test_write_reg(FACTORY_REG_LCD_NOISE_TEST_STATE, 0x03);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write idle to lcdnoise test state fail\n");
    }

    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ LCD Noise Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("------ LCD Noise Test NG\n");
    }

    FTS_TEST_FUNC_EXIT();
    return ret;
}


static void fts_auto_write_result(struct fts_ts_data *ts_data, int failed_count)
{
    //uint8_t  data_buf[64];
    uint8_t file_data_buf[128];
    uint8_t  data_buf[256];
    uint32_t buflen = 0;
    //int i;
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM;
    mm_segment_t old_fs;
    struct timespec now_time;
    struct rtc_time rtc_now_time;

    TPD_INFO("%s +\n", __func__);

    //step2: create a file to store test data in /sdcard/Tp_Test
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    //if test fail,save result to path:/sdcard/TpTestReport/screenOn/NG/
    if (ts_data->black_screen_test) {
        if(failed_count) {
            snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOff/NG/tp_testlimit_%02d%02d%02d-%02d%02d%02d-fail-utc.csv",
                     (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                     rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
        } else {
            snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOff/OK/tp_testlimit_%02d%02d%02d-%02d%02d%02d-pass-utc.csv",
                     (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                     rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

        }
    } else {
        if(failed_count) {
            snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/NG/tp_testlimit_%02d%02d%02d-%02d%02d%02d-fail-utc.csv",
                     (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                     rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
        } else {
            snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/OK/tp_testlimit_%02d%02d%02d-%02d%02d%02d-pass-utc.csv",
                     (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                     rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

        }
    }
    old_fs = get_fs();
    set_fs(KERNEL_DS);

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_mkdir("/sdcard/TpTestReport", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOff/NG", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOff/OK", 0666);
    ts_data->csv_fd = ksys_open(file_data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
    sys_mkdir("/sdcard/TpTestReport", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/NG", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/OK", 0666);
    ts_data->csv_fd = sys_open(file_data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/

    if (ts_data->csv_fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", file_data_buf);
        set_fs(old_fs);
		return;
    }

    /*header*/
    buflen = snprintf(data_buf, 256, "ECC, 85, 170, IC Name, %s, IC Code, %x\n", "FT8006S-AA", 0x9B00);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
    sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/

    if (ts_data->black_screen_test) {
        buflen = snprintf(data_buf, 256, "TestItem Num, %d, ", 3);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "CB Test", 12, tx_num, rx_num, 11, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "RawData Test", 7, tx_num, rx_num, 11 + tx_num, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "LCD Noise Test", 27, tx_num, rx_num, 11 + tx_num * 2, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/

        buflen = snprintf(data_buf, 256, "\n\n\n\n\n\n\n\n\n");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        fts_test_save_data("CB Test", ts_data->cb_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("RawData Test", ts_data->raw_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("LCD Noise Test", ts_data->lcdnoise_data, node_num, rx_num, ts_data->csv_fd);
    } else {
        buflen = snprintf(data_buf, 256, "TestItem Num, %d, ", 5);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Short Circuit Test", 15, tx_num, rx_num, 11, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "Open Test", 25, tx_num, rx_num, 11 + tx_num, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "CB Test", 12, tx_num, rx_num, 11 + tx_num * 2, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "RawData Test", 7, tx_num, rx_num, 11 + tx_num * 3, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
         ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        buflen = snprintf(data_buf, 256, "%s, %d, %d, %d, %d, %d, ", "LCD Noise Test", 27, tx_num, rx_num, 11 + tx_num * 4, 1);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/

        buflen = snprintf(data_buf, 256, "\n\n\n\n\n\n\n\n\n");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(ts_data->csv_fd, data_buf, buflen);
#else
        sys_write(ts_data->csv_fd, data_buf, buflen);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        fts_test_save_data("Short Circuit Test", ts_data->short_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("Open Test", ts_data->open_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("CB Test", ts_data->cb_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("RawData Test", ts_data->raw_data, node_num, rx_num, ts_data->csv_fd);
        fts_test_save_data("LCD Noise Test", ts_data->lcdnoise_data, node_num, rx_num, ts_data->csv_fd);
    }

    if (ts_data->csv_fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_close(ts_data->csv_fd);
#else
        sys_close(ts_data->csv_fd);
#endif /*CONFIG_ARCH_HAS_SYSCALL_WRAPPER*/
        set_fs(old_fs);
    }
    TPD_INFO("%s -\n", __func__);
    return;
}


static int fts_auto_endoperation(struct fts_ts_data *ts_data)
{
    TPD_INFO("%s +\n", __func__);
    if (ts_data->short_data) {
        kfree(ts_data->short_data);
        ts_data->short_data = NULL;
    }
    if (ts_data->open_data) {
        kfree(ts_data->open_data);
        ts_data->open_data = NULL;
    }
    if (ts_data->cb_data) {
        kfree(ts_data->cb_data);
        ts_data->cb_data = NULL;
    }
    if (ts_data->raw_data) {
        kfree(ts_data->raw_data);
        ts_data->raw_data = NULL;
    }
    if (ts_data->lcdnoise_data) {
        kfree(ts_data->lcdnoise_data);
        ts_data->lcdnoise_data = NULL;
    }
    TPD_INFO("%s -\n", __func__);

    return 0;
}


static int fts_start_test(struct fts_ts_data *ts_data)
{
    int ret = 0;
    bool temp_result = false;
    int test_result = 0;
    int failed_count = 0;

    FTS_TEST_FUNC_ENTER();
    TPD_INFO("%s +\n", __func__);
    fts_auto_preoperation(ts_data);

    if (!ts_data->black_screen_test) {
        /* short test */
        ret = fts_short_test(ts_data, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result |= (1 << 1);
            failed_count += 1;
        }

        /* open test */
        ret = fts_open_test(ts_data, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result |= (2 << 1);
            failed_count += 1;
        }
    }

    /* cb test */
    ret = fts_cb_test(ts_data, &temp_result);
    if ((ret < 0) || (false == temp_result)) {
        test_result |= (3 << 1);
        failed_count += 1;
    }

    /* rawdata test */
    ret = fts_rawdata_test(ts_data, &temp_result);
    if ((ret < 0) || (false == temp_result)) {
        test_result |= (4 << 1);
        failed_count += 1;
    }

    /* lcdnoise test */
    ret = fts_lcdnoise_test(ts_data, &temp_result);
    if ((ret < 0) || (false == temp_result)) {
        test_result |= (5 << 1);
        failed_count += 1;
    }

    fts_auto_write_result(ts_data, failed_count);
    fts_auto_endoperation(ts_data);

    TPD_INFO("%s: test_result = [0x%x] \n ", __func__, test_result);
    FTS_TEST_FUNC_EXIT();
    TPD_INFO("%s -\n", __func__);

    return failed_count;
}

static void fts_autotest_endoperation(struct fts_ts_data *ts_data, const struct firmware *limit_fw)
{
    TPD_INFO("%s +\n", __func__);
    
    if (ts_data->fts_autotest_offset) {
        kfree(ts_data->fts_autotest_offset->node_valid);
        kfree(ts_data->fts_autotest_offset);
        ts_data->fts_autotest_offset = NULL;
    }

    if (limit_fw) {
        release_firmware(limit_fw);
        limit_fw = NULL;
    }
    TPD_INFO("%s -\n", __func__);
}

#define FTS_THR_DEBUG   0
static int fts_get_threshold_from_img(struct fts_ts_data *ts_data, char *data, const struct firmware *limit_fw)
{

    int ret = 0;
    int i = 0;
    int item_cnt = 0;
    int node_num = ts_data->hw_res->TX_NUM * ts_data->hw_res->RX_NUM;
    //uint8_t * p_print = NULL;
    uint32_t *p_item_offset = NULL;
    struct auto_test_header *ph = NULL;
    struct auto_test_item_header *item_head = NULL;
    struct touchpanel_data *ts = ts_data->ts;

#if 0
    ret = touch_i2c_read_byte(ts_data->client, FTS_REG_FW_VER);
    if (ret > 0x10) {
        ts_data->use_panelfactory_limit = false;
    } else {
        ts_data->use_panelfactory_limit = true;
    }
    TPD_INFO("%s, use_panelfactory_limit = %d \n", __func__, ts_data->use_panelfactory_limit);
#endif 

    ts_data->fts_autotest_offset = kzalloc(sizeof(struct fts_autotest_offset), GFP_KERNEL);
    if (!ts_data->fts_autotest_offset) {
        TPD_INFO("allocating memory for fts_autotest_offset fails");
        return -ENOMEM;
    }

    ts_data->fts_autotest_offset->node_valid = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    if (!ts_data->fts_autotest_offset->node_valid) {
        TPD_INFO("allocating memory for node_valid fails");
        return -ENOMEM;
    }

    for (i = 0; i < node_num; i++) {
        ts_data->fts_autotest_offset->node_valid[i] = 1;
    }
if (FTS_THR_DEBUG) {
    ts_data->fts_autotest_offset->fts_short_data_N = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_short_data_P = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_open_data_N = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_open_data_P = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_cb_data_N = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_cb_data_P = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_raw_data_N = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_raw_data_P = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_lcdnoise_data_N = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    ts_data->fts_autotest_offset->fts_lcdnoise_data_P = kzalloc(node_num * sizeof(int32_t), GFP_KERNEL);
    for (i = 0; i < node_num; i++) {
        ts_data->fts_autotest_offset->fts_short_data_N[i] = 1000;
        ts_data->fts_autotest_offset->fts_short_data_P[i] = 1000000;

        ts_data->fts_autotest_offset->fts_open_data_N[i] = 50;
        ts_data->fts_autotest_offset->fts_open_data_P[i] = 1000000;

        ts_data->fts_autotest_offset->fts_cb_data_N[i] = 3;
        ts_data->fts_autotest_offset->fts_cb_data_P[i] = 50;

        ts_data->fts_autotest_offset->fts_raw_data_N[i] = 4000;
        ts_data->fts_autotest_offset->fts_raw_data_P[i] = 8000;

        ts_data->fts_autotest_offset->fts_lcdnoise_data_N[i] = 0;
        ts_data->fts_autotest_offset->fts_lcdnoise_data_P[i] = 200;
    }
    
} else {
    ret = request_firmware(&limit_fw, ts->panel_data.test_limit_name, ts_data->dev);
    TPD_INFO("limit_img path is [%s] \n", ts->panel_data.test_limit_name);
    if (ret < 0) {
        TPD_INFO("Request limit_img failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        goto RELEASE_DATA;
    }

    ph = (struct auto_test_header *)(limit_fw->data);
#if 0
    TPD_INFO("start to dump img \n");
    p_print = (uint8_t *)ph;
    for (i = 0; i < 16 * 8; i++) {
        if (i % 16 == 0){
            TPD_INFO("current line [%d]: \n", i/16);
        }
        TPD_INFO("0x%x \n", *(p_print + i * sizeof(uint8_t)));
    }
    TPD_INFO("end of dump img \n");
#endif
    p_item_offset = (uint32_t *)(limit_fw->data + 16);
    for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
        if ((ph->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }
    TPD_INFO("%s: total test item = %d \n", __func__, item_cnt);

    TPD_INFO("%s: populating nvt_test_offset \n", __func__);
    for (i = 0; i < item_cnt; i++) {
        TPD_INFO("%s: i[%d] \n", __func__, i);
        item_head = (struct auto_test_item_header *)(limit_fw->data + p_item_offset[i]);        
        if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
            TPD_INFO("[%d] incorrect item type: LIMIT_TYPE_NO_DATA\n", item_head->item_bit);
        } else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
            TPD_INFO("test item bit [%d] \n", item_head->item_bit);
            if (item_head->item_bit == TYPE_SHORT_DATA) {
                ts_data->fts_autotest_offset->fts_short_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                ts_data->fts_autotest_offset->fts_short_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
            } else if (item_head->item_bit == TYPE_OPEN_DATA) {
                ts_data->fts_autotest_offset->fts_open_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                ts_data->fts_autotest_offset->fts_open_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
            }

            if (ts_data->black_screen_test) {
                if(item_head->item_bit == TYPE_BLACK_NOISE_DATA){
                    ts_data->fts_autotest_offset->fts_lcdnoise_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_lcdnoise_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } else if(item_head->item_bit == TYPE_BLACK_RAW_DATA) {
                    ts_data->fts_autotest_offset->fts_raw_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_raw_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } else if (item_head->item_bit == TYPE_BLACK_CB_DATA) {
                    ts_data->fts_autotest_offset->fts_cb_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_cb_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } 
            } else {
                if(item_head->item_bit == TYPE_NOISE_DATA){
                    ts_data->fts_autotest_offset->fts_lcdnoise_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_lcdnoise_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } else if(item_head->item_bit == TYPE_RAW_DATA) {
                    ts_data->fts_autotest_offset->fts_raw_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_raw_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } else if (item_head->item_bit == TYPE_CB_DATA) {
                    ts_data->fts_autotest_offset->fts_cb_data_P = (int32_t *)(limit_fw->data + item_head->top_limit_offset);
                    ts_data->fts_autotest_offset->fts_cb_data_N = (int32_t *)(limit_fw->data + item_head->floor_limit_offset);
                } 
            }
            
        } else {
            TPD_INFO("[%d] unknown item type \n", item_head->item_bit);
        }        
    }
    return 0;
    
RELEASE_DATA:
    if (limit_fw) {
        release_firmware(limit_fw);
    }
}
    return ret;
}

static void fts_print_threshold(struct fts_ts_data *ts_data)
{
    int tx_num = ts_data->hw_res->TX_NUM;
    int rx_num = ts_data->hw_res->RX_NUM;
    int node_num = tx_num * rx_num;

    TPD_DEBUG("short threshold max/min:\n");
    print_buffer(ts_data->fts_autotest_offset->fts_short_data_P, node_num, rx_num);
    print_buffer(ts_data->fts_autotest_offset->fts_short_data_N, node_num, rx_num);

    TPD_DEBUG("open threshold max/min:\n");
    print_buffer(ts_data->fts_autotest_offset->fts_open_data_P, node_num, rx_num);
    print_buffer(ts_data->fts_autotest_offset->fts_open_data_N, node_num, rx_num);

    TPD_DEBUG("cb threshold max/min:\n");
    print_buffer(ts_data->fts_autotest_offset->fts_cb_data_P, node_num, rx_num);
    print_buffer(ts_data->fts_autotest_offset->fts_cb_data_N, node_num, rx_num);

    TPD_DEBUG("rawdata threshold max/min:\n");
    print_buffer(ts_data->fts_autotest_offset->fts_raw_data_P, node_num, rx_num);
    print_buffer(ts_data->fts_autotest_offset->fts_raw_data_N, node_num, rx_num);

    TPD_DEBUG("lcdnoise threshold max/min:\n");
    print_buffer(ts_data->fts_autotest_offset->fts_lcdnoise_data_P, node_num, rx_num);
    print_buffer(ts_data->fts_autotest_offset->fts_lcdnoise_data_N, node_num, rx_num);
}

static int fts_enter_test_environment(struct touchpanel_data *ts, bool test_state)
{
    int ret = 0;
    u8 detach_flag = 0;
    uint8_t copy_len = 0;
    const struct firmware *fw = NULL;
    char *fw_name_test = NULL;
    char *p_node = NULL;
    char *postfix = "_TEST.img";

    TPD_INFO("fw test download function");
    if (ts->loading_fw) {
        TPD_INFO("fw is loading, not download again");
        return -EINVAL;
    }

    if (test_state) {
        //update test firmware
        fw_name_test = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
        if (fw_name_test == NULL) {
            TPD_INFO("fw_name_test kzalloc error!\n");
            return -ENOMEM;
        }

        p_node = strstr(ts->panel_data.fw_name, ".");
        copy_len = p_node - ts->panel_data.fw_name;
        memcpy(fw_name_test, ts->panel_data.fw_name, copy_len);
        strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
        TPD_INFO("fw_name_test is %s\n", fw_name_test);

        /*write test firmware.bin*/
        ret = request_firmware(&fw, fw_name_test, ts->dev);
        if (ret) {
            TPD_INFO("request_firmware(%s) fail", fw_name_test);
            return -ENODATA;
        }
    } else {
        /*write normal firmware.bin*/
        ret = request_firmware_select(&fw, ts->panel_data.fw_name, ts->dev);
    }

    /*download firmware*/
    ts->loading_fw = true;
    if (ts->ts_ops && ts->ts_ops->fw_update)
        ret = ts->ts_ops->fw_update(ts->chip_data, fw, 1);
    ts->loading_fw = false;

    msleep(50);
    fts_test_read_reg(FTS_REG_FACTORY_MODE_DETACH_FLAG, &detach_flag);
    TPD_INFO("regb4:0x%02x\n", detach_flag);

    if (fw) {
        release_firmware(fw);
        fw = NULL;
    }
    return ret;
}

int fts_test_entry(struct fts_ts_data *ts_data, bool black_screen)
{
    int ret = 0;
    struct touchpanel_data *ts = ts_data->ts;
    const struct firmware *limit_fw = NULL;

    TPD_INFO("%s +\n", __func__);
    FTS_TEST_SAVE_ERR("FW_VER:0x%02x, TX_NUM:%d, RX_NUM:%d\n", ts_data->fwver, 
        ts_data->hw_res->TX_NUM, ts_data->hw_res->RX_NUM);

    ts_data->black_screen_test = black_screen;

    ret = fts_enter_test_environment(ts, 1);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter test mode fails\n");
        return 0xFF;
    }

    ret = fts_get_threshold_from_img(ts_data, NULL, limit_fw);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get threshold from img fail,ret=%d\n", ret);
        ret = 0xFF;
        goto err_exit_test_mode;
    }
    fts_print_threshold(ts_data);

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
        ret = 0xFF;
        goto test_err;
    }

    ret = get_channel_num(ts_data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("check channel num fail,ret=%d\n", ret);
        ret = 0xFF;
        goto test_err;
    }

    ret = fts_start_test(ts_data);
    //seq_printf(s, "%d error(s). %s\n", gts_test->error_count, gts_test->error_count ? "" : "All test passed.");
    //FTS_TEST_SAVE_INFO("\n\n %d Error(s). Factory Test Result \n", ret);
    //FTS_TEST_SAVE_INFO("\n\n%d error(s). %s\n", ret, ret ? "" : "All test passed.\n");
    if (fts_data->s) {
        seq_printf(fts_data->s, "%d error(s). %s\n", ret, ret ? "" : "All test passed.");
    }

test_err:
    enter_work_mode();

err_exit_test_mode:
    fts_autotest_endoperation(ts_data, limit_fw);
    fts_enter_test_environment(ts, 0);
    TPD_INFO("%s -\n", __func__);
    return ret;
}





