// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "../touchpanel_common.h"
#include "focal_common.h"
#include <linux/crc32.h>
#include <linux/syscalls.h>

/*******LOG TAG Declear*****************************/

#define TPD_DEVICE "focal_common"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (LEVEL_DEBUG == tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DETAIL(a, arg...)\
    do{\
        if (LEVEL_BASIC != tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)


#define PROC_READ_REGISTER                      1
#define PROC_WRITE_REGISTER                     2
#define PROC_WRITE_DATA                         6
#define PROC_READ_DATA                          7
#define PROC_SET_TEST_FLAG                      8
#define PROC_SET_SLAVE_ADDR                     10
#define PROC_HW_RESET                           11
#define PROC_NAME                               "ftxxxx-debug"
#define PROC_BUF_SIZE                           256
#define FTX_MAX_COMMMAND_LENGTH                 16

struct ftxxxx_proc {
    struct proc_dir_entry *proc_entry;
    u8 opmode;
    u8 cmd_len;
    u8 cmd[FTX_MAX_COMMMAND_LENGTH];
};

struct ftxxxx_proc proc;

static struct fts_proc_operations *g_syna_ops;


/************ Start of other functions work for device attribute file*************************/


static int is_hex_char(const char ch)
{
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        return 1;
    }

    return 0;
}

static int hex_char_to_int(const char ch)
{
    int result = 0;
    if (ch >= '0' && ch <= '9') {
        result = (int)(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
        result = (int)(ch - 'a') + 10;
    } else if (ch >= 'A' && ch <= 'F') {
        result = (int)(ch - 'A') + 10;
    } else {
        result = -1;
    }

    return result;
}

static int hex_to_str(char *hex, int iHexLen, char *ch, int *iChLen)
{
    int high = 0;
    int low = 0;
    int tmp = 0;
    int i = 0;
    int iCharLen = 0;
    if (hex == NULL || ch == NULL) {
        return -1;
    }

    TPD_INFO("iHexLen: %d in function:%s!!\n", iHexLen, __func__);

    if (iHexLen % 2 == 1) {
        return -2;
    }

    for (i = 0; i < iHexLen; i += 2) {
        high = hex_char_to_int(hex[i]);
        if (high < 0) {
            ch[iCharLen] = '\0';
            return -3;
        }

        low = hex_char_to_int(hex[i + 1]);
        if (low < 0) {
            ch[iCharLen] = '\0';
            return -3;
        }
        tmp = (high << 4) + low;
        ch[iCharLen++] = (char)tmp;
    }
    ch[iCharLen] = '\0';
    *iChLen = iCharLen;
    TPD_INFO("iCharLen: %d, iChLen: %d in function:%s!!\n", iCharLen, *iChLen, __func__);
    return 0;
}

static void str_to_bytes(char *bufStr, int iLen, char *uBytes, int *iBytesLen)
{
    int i = 0;
    int iNumChLen = 0;
    *iBytesLen = 0;

    for (i = 0; i < iLen; i++) {
        //filter illegal chars
        if (is_hex_char(bufStr[i])) {
            bufStr[iNumChLen++] = bufStr[i];
        }
    }

    bufStr[iNumChLen] = '\0';
    hex_to_str(bufStr, iNumChLen, uBytes, iBytesLen);
}
/***************** End of other functions work for device attribute file*********************/


static struct {
    int op;         // 0: read, 1: write
    int reg;        // register
    int value;      // read: return value, write: op return
    int result;     // 0: success, otherwise: fail
} g_rwreg_result;
/************************** Start of device attribute file***********************************/
static ssize_t focal_hw_reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t count = 0;
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct focal_debug_func *focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (focal_debug_ops && focal_debug_ops->reset) {
        mutex_lock(&ts->mutex);
        focal_debug_ops->reset(ts->chip_data, 200);
        mutex_unlock(&ts->mutex);
    }

    count = snprintf(buf, PAGE_SIZE, "hw reset executed\n");
    return count;
}

static ssize_t focal_irq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    if ((strcmp(buf, "1")  == 0) || (strcmp(buf, "on") == 0)) {
        if (ts) {
            enable_irq(ts->irq);
        }
        TPD_INFO("[EX-FUN]enable irq\n");
    } else if ((strcmp(buf, "0")  == 0) || (strcmp(buf, "off") == 0)) {
        if (ts) {
            disable_irq_nosync(ts->irq);
        }
        TPD_INFO("[EX-FUN]disable irq\n");
    }
    return count;
}

static ssize_t focal_fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t num_read_chars = 0;
    int fw_version = 0;
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct focal_debug_func *focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    if (focal_debug_ops && focal_debug_ops->get_fw_version) {
        mutex_lock(&ts->mutex);
        fw_version = focal_debug_ops->get_fw_version(ts->chip_data);
        mutex_unlock(&ts->mutex);
        if (fw_version < 0) {
            num_read_chars = snprintf(buf, PAGE_SIZE, "I2c transfer error!\n");
        }

        if (fw_version == 255) {
            num_read_chars = snprintf(buf, PAGE_SIZE, "get tp fw version fail!\n");
        } else {
            num_read_chars = snprintf(buf, PAGE_SIZE, "%02X\n", (unsigned int)fw_version);
        }
    }

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }

    return num_read_chars;
}


static ssize_t focal_rw_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int count;

    if (!g_rwreg_result.op) {
        if (g_rwreg_result.result == 0) {
            count = sprintf(buf, "Read %02X: %02X\n", (unsigned int)g_rwreg_result.reg, (unsigned int)g_rwreg_result.value);
        } else {
            count = sprintf(buf, "Read %02X failed, ret: %d\n", (unsigned int)g_rwreg_result.reg,  g_rwreg_result.result);
        }
    } else {
        if (g_rwreg_result.result == 0) {
            count = sprintf(buf, "Write %02X, %02X success\n", (unsigned int)g_rwreg_result.reg,  (unsigned int)g_rwreg_result.value);
        } else {
            count = sprintf(buf, "Write %02X failed, ret: %d\n", (unsigned int)g_rwreg_result.reg,  g_rwreg_result.result);
        }
    }

    return count;
}

static ssize_t focal_rw_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct focal_debug_func *focal_debug_ops = (struct focal_debug_func *)ts->private_data;
    ssize_t num_read_chars = 0;
    int retval;
    long unsigned int wmreg = 0;
    u8 regaddr = 0xff;
    int regvalue = 0xff;
    u8 valbuf[5] = {0};

    memset(valbuf, 0, sizeof(valbuf));
    num_read_chars = count - 1;
    if ((num_read_chars != 2) && (num_read_chars != 4)) {
        TPD_INFO("please input 2 or 4 character\n");
        goto error_return;
    }

    memcpy(valbuf, buf, num_read_chars);
    retval = kstrtoul(valbuf, 16, &wmreg);
    str_to_bytes((char *)buf, num_read_chars, valbuf, &retval);

    if (1 == retval) {
        regaddr = valbuf[0];
        retval = 0;
    } else if (2 == retval) {
        regaddr = valbuf[0];
        regvalue = valbuf[1];
        retval = 0;
    } else {
        retval = -1;
    }

    if (0 != retval) {
        TPD_INFO("%s() - ERROR: Could not convert the given input to a number. The given input was: %s\n", __func__, buf);
        goto error_return;
    }

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    mutex_lock(&ts->mutex);

    if (2 == num_read_chars) {
        g_rwreg_result.op = 0;
        g_rwreg_result.reg = regaddr;
        /*read register*/
        regaddr = wmreg;
        regvalue = touch_i2c_read_byte(ts->client, regaddr);
        if (regvalue < 0)  {
            TPD_INFO("Could not read the register(0x%02x)\n", regaddr);
            g_rwreg_result.result = -1;
        } else {
            TPD_INFO("the register(0x%02x) is 0x%02x\n", regaddr, regvalue);
            g_rwreg_result.value = regvalue;
            g_rwreg_result.result = 0;
        }
    } else {
        regaddr = wmreg >> 8;
        regvalue = wmreg;

        g_rwreg_result.op = 1;
        g_rwreg_result.reg = regaddr;
        g_rwreg_result.value = regvalue;
        g_rwreg_result.result = touch_i2c_write_byte(ts->client, regaddr, regvalue);
        if (g_rwreg_result.result < 0) {
            TPD_INFO("Could not write the register(0x%02x)\n", regaddr);

        } else {
            TPD_INFO("Write 0x%02x into register(0x%02x) successful\n", regvalue, regaddr);
            g_rwreg_result.result = 0;
        }
    }

    mutex_unlock(&ts->mutex);

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }
error_return:

    return count;
}

static ssize_t focal_esdcheck_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s:esd store(%s)", __func__, buf);
    if (ts->esd_handle_support) {
        if ((memcmp(buf, "1", 1)  == 0) || (memcmp(buf, "on", 2) == 0)) {
            esd_handle_switch(&ts->esd_info, true);
        } else if ((memcmp(buf, "0", 1)  == 0) || (memcmp(buf, "off", 3) == 0)) {
            esd_handle_switch(&ts->esd_info, false);
        }
    }

    return count;
}

static ssize_t focal_esdcheck_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct focal_debug_func *focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->get_esd_check_flag) {
            count = sprintf(buf, "esd_running_flag: %d,  esd_check_need_stop: %d\n", \
                            ts->esd_info.esd_running_flag, focal_debug_ops->get_esd_check_flag(ts->chip_data));
        } else {
            count = sprintf(buf, "esd_running_flag: %d\n", ts->esd_info.esd_running_flag);
        }
    } else {
        count = sprintf(buf, "not support esd handle\n");
    }

    return count;
}

static ssize_t focal_dump_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    char tmp[256];
    int count = 0;
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    struct focal_debug_func *focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    mutex_lock(&ts->mutex);
    if (focal_debug_ops && focal_debug_ops->dump_reg_sate) {
        count = focal_debug_ops->dump_reg_sate(ts->chip_data, tmp);
    }
    mutex_unlock(&ts->mutex);

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }
    memcpy(buf, tmp, count);

    return count;
}

static DEVICE_ATTR(fts_fw_version, S_IRUGO | S_IWUSR, focal_fw_version_show, NULL);
/* read and write register
*   read example: echo 88 > rw_reg ---read register 0x88
*   write example:echo 8807 > rw_reg ---write 0x07 into register 0x88
*   note:the number of input must be 2 or 4.if it not enough,please fill in the 0.*/
static DEVICE_ATTR(fts_rw_reg, S_IRUGO | S_IWUSR, focal_rw_reg_show, focal_rw_reg_store);
static DEVICE_ATTR(fts_dump_reg, S_IRUGO | S_IWUSR, focal_dump_reg_show, NULL);
static DEVICE_ATTR(fts_hw_reset, S_IRUGO | S_IWUSR, focal_hw_reset_show, NULL);
static DEVICE_ATTR(fts_irq, S_IRUGO | S_IWUSR, NULL, focal_irq_store);
static DEVICE_ATTR(fts_esd_check, S_IRUGO | S_IWUSR, focal_esdcheck_show, focal_esdcheck_store);

static struct attribute *focal_attributes[] = {
    &dev_attr_fts_fw_version.attr,
    &dev_attr_fts_rw_reg.attr,
    &dev_attr_fts_dump_reg.attr,
    &dev_attr_fts_hw_reset.attr,
    &dev_attr_fts_irq.attr,
    &dev_attr_fts_esd_check.attr,
    NULL
};

static struct attribute_group focal_attribute_group = {
    .attrs = focal_attributes
};

int focal_create_sysfs(struct i2c_client *client)
{
    int err = -1;
    err = sysfs_create_group(&client->dev.kobj, &focal_attribute_group);
    if (0 != err) {
        TPD_INFO("[EX]: sysfs_create_group() failed!\n");
        sysfs_remove_group(&client->dev.kobj, &focal_attribute_group);
        return -EIO;
    } else {
        TPD_INFO("[EX]: sysfs_create_group() succeeded!\n");
    }
    return err;
}
EXPORT_SYMBOL(focal_create_sysfs);

int focal_create_sysfs_spi(struct spi_device *spi)
{
	int err = -1;
	err = sysfs_create_group(&spi->dev.kobj, &focal_attribute_group);

	if (0 != err) {
		TPD_INFO("[EX]: sysfs_create_group() failed!\n");
		sysfs_remove_group(&spi->dev.kobj, &focal_attribute_group);
		return -EIO;

	} else {
		TPD_INFO("[EX]: sysfs_create_group() succeeded!\n");
	}

	return err;
}
EXPORT_SYMBOL(focal_create_sysfs_spi);
/******************************* End of device attribute file******************************************/

/********************Start of apk debug file and it's operation callbacks******************************/
unsigned char proc_operate_mode = 0;

static ssize_t focal_debug_write(struct file *filp, const char __user *buff, size_t count, loff_t *ppos)
{
    unsigned char writebuf[WRITE_BUF_SIZE];
    int buflen = count;
    int writelen = 0;
    int ret = 0;
    char tmp[25];
    struct touchpanel_data *ts = PDE_DATA(file_inode(filp));
    struct focal_debug_func *focal_debug_ops = NULL;

    if (!ts) {
        return 0;
    }

    focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (copy_from_user(&writebuf, buff, buflen)) {
        TPD_INFO("[APK]: copy from user error!\n");
        return -EFAULT;
    }
    writebuf[WRITE_BUF_SIZE - 1] = '\0';

    mutex_lock(&ts->mutex);

    proc_operate_mode = writebuf[0];
    switch (proc_operate_mode) {
    case PROC_SET_TEST_FLAG:
        TPD_INFO("[APK]: PROC_SET_TEST_FLAG = %x!\n", writebuf[1]);
        if (ts->esd_handle_support && !ts->is_suspended) {
            if (writebuf[1] == 1) {
                esd_handle_switch(&ts->esd_info, false);
            } else {
                esd_handle_switch(&ts->esd_info, true);
            }
        }
        break;
    case PROC_READ_REGISTER:
        writelen = 1;
        ret = touch_i2c_write(ts->client, writebuf + 1, writelen);
        if (ret < 0) {
            TPD_INFO("[APK]: write iic error!\n");
        }
        break;
    case PROC_WRITE_REGISTER:
        writelen = 2;
        ret = touch_i2c_write(ts->client, writebuf + 1, writelen);
        if (ret < 0) {
            TPD_INFO("[APK]: write iic error!\n");
        }
        break;
    case PROC_HW_RESET:
        snprintf(tmp, sizeof(tmp), "%s", (char *)writebuf + 1);
        tmp[buflen - 1] = '\0';
        if (strncmp(tmp, "focal_driver", 12) == 0) {
            TPD_INFO("Begin HW Reset\n");
            if (focal_debug_ops && focal_debug_ops->reset) {
                focal_debug_ops->reset(ts->chip_data, 1);
            }
        }
        break;
    case PROC_READ_DATA:
    case PROC_WRITE_DATA:
        writelen = count - 1;
        if (writelen > 0) {
            ret = touch_i2c_write(ts->client, writebuf + 1, writelen);
            if (ret < 0) {
                TPD_INFO("[APK]: write iic error!\n");
            }
        }
        break;
    default:
        break;
    }

    mutex_unlock(&ts->mutex);

    if (ret < 0) {
        return ret;
    } else {
        return count;
    }
}

static ssize_t focal_debug_read(struct file *filp, char __user *buff, size_t count, loff_t *ppos)
{
    int ret = 0;
    int num_read_chars = 0;
    int readlen = 0;
    unsigned char buf[READ_BUF_SIZE];
    struct touchpanel_data *ts = PDE_DATA(file_inode(filp));
    struct focal_debug_func *focal_debug_ops = NULL;

    if (!ts) {
        return 0;
    }

    focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    mutex_lock(&ts->mutex);

    switch (proc_operate_mode) {
    case PROC_READ_REGISTER:
        readlen = 1;
        ret = touch_i2c_read(ts->client, NULL, 0, buf, readlen);
        if (ret < 0) {
            if (ts->esd_handle_support) {
                if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
                    focal_debug_ops->esd_check_enable(ts->chip_data, true);
                }
            }
            TPD_INFO("[APK]: read i2c error!\n");
            mutex_unlock(&ts->mutex);
            return ret;
        }
        num_read_chars = 1;
        break;
    case PROC_READ_DATA:
        readlen = count;
        ret = touch_i2c_read(ts->client, NULL, 0, buf, readlen);
        if (ret < 0) {
            if (ts->esd_handle_support) {
                if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
                    focal_debug_ops->esd_check_enable(ts->chip_data, true);
                }
            }
            TPD_INFO("[APK]: read iic error!\n");
            mutex_unlock(&ts->mutex);
            return ret;
        }

        num_read_chars = readlen;
        break;
    case PROC_WRITE_DATA:
        break;
    default:
        break;
    }

    mutex_unlock(&ts->mutex);

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }

    if (copy_to_user(buff, buf, num_read_chars)) {
        TPD_INFO("[APK]: copy to user error!\n");
        return -EFAULT;
    }

    return num_read_chars;
}
static const struct file_operations focal_proc_fops = {
    .owner  = THIS_MODULE,
    .open  = simple_open,
    .read   = focal_debug_read,
    .write  = focal_debug_write,
};

static ssize_t proc_grip_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int ret = 0, para_num = 0;
    char buf[PAGESIZE] = {0};
    char para_buf[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct focal_debug_func *focal_debug_ops = NULL;

    if (!ts)
        return count;

    focal_debug_ops = (struct focal_debug_func *)ts->private_data;
    if (!focal_debug_ops->set_grip_handle)
        return count;

    if (count > PAGESIZE)
        count = PAGESIZE;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d:%s", &para_num, para_buf);
    mutex_lock(&ts->mutex);
    focal_debug_ops->set_grip_handle(ts->chip_data, para_num, para_buf);
    if (ts->is_suspended == 0) {
        ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_edge);
        if (ret < 0) {
            TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
        }
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations proc_grip_control_ops = {
    .write = proc_grip_control_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

int focal_create_apk_debug_channel(struct touchpanel_data *ts)
{
    int ret = 0;
    struct proc_dir_entry *focal_proc_entry = NULL;
    focal_proc_entry = proc_create_data("ftxxxx-debug", 0777, NULL, &focal_proc_fops, ts);
    if (NULL == focal_proc_entry) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry!\n", __func__);
    }

    focal_proc_entry = proc_create_data("grip_handle", 0222, ts->prEntry_tp, &proc_grip_control_ops, ts);
    if (NULL == focal_proc_entry) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create grip handle proc entry, %d\n", __func__, __LINE__);
    }

    return ret;
}
/**********************End of apk debug file and it's operation callbacks******************************/
/*******Part1:Call Back Function implement*******/

static ssize_t fts_debug_write(struct file *filp, const char __user *buff, size_t count, loff_t *ppos)
{
    u8 *writebuf = NULL;
    u8 tmpbuf[PROC_BUF_SIZE] = { 0 };
    int buflen = count;
    int writelen = 0;
    int ret = 0;
    char tmp[PROC_BUF_SIZE];
    struct touchpanel_data *ts = PDE_DATA(file_inode(filp));
    struct focal_debug_func *focal_debug_ops = NULL;

    if (!ts)
        return 0;

    focal_debug_ops = (struct focal_debug_func *)ts->private_data;

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

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    proc.opmode = writebuf[0];
    switch (proc.opmode) {
    case PROC_SET_TEST_FLAG:
        TPD_DEBUG("[APK]: PROC_SET_TEST_FLAG = %x", writebuf[1]);
        break;

    case PROC_READ_REGISTER:
        proc.cmd[0] = writebuf[1];
        break;

    case PROC_WRITE_REGISTER:
        ret = touch_i2c_write_byte(ts->client, writebuf[1], writebuf[2]);
        if (ret < 0) {
            TPD_INFO("PROC_WRITE_REGISTER write error");
            goto proc_write_err;
        }
        break;

    case PROC_READ_DATA:
        writelen = buflen - 1;
        ret = touch_i2c_write(ts->client, writebuf + 1, writelen);
        if (ret < 0) {
            TPD_INFO("PROC_READ_DATA write error");
            goto proc_write_err;
        }

        break;

    case PROC_WRITE_DATA:
        writelen = buflen - 1;
        ret = touch_i2c_write(ts->client, writebuf + 1, writelen);
        if (ret < 0) {
            TPD_INFO("PROC_WRITE_DATA write error");
            goto proc_write_err;
        }
        break;

    case PROC_HW_RESET:
        snprintf(tmp, PROC_BUF_SIZE, "%s", (char *)writebuf + 1);
        tmp[((buflen - 1) > (PROC_BUF_SIZE - 1)) ? (PROC_BUF_SIZE - 1) : (buflen - 1)] = '\0';
        if (strncmp(tmp, "focal_driver", 12) == 0) {
            TPD_INFO("APK execute HW Reset");
            if (ts->ts_ops->reset_gpio_control) {
                ts->ts_ops->reset_gpio_control(ts->chip_data, 0);
                msleep(1);
                ts->ts_ops->reset_gpio_control(ts->chip_data, 1);
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

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
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
    struct touchpanel_data *ts = PDE_DATA(file_inode(filp));
    struct focal_debug_func *focal_debug_ops = NULL;

   if (!ts) {
	return 0;
   }

   focal_debug_ops = (struct focal_debug_func *)ts->private_data;

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

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    switch (proc.opmode) {
    case PROC_READ_REGISTER:
        num_read_chars = 1;
        ret = touch_i2c_read(ts->client, &proc.cmd[0], 1, &readbuf[0], num_read_chars);
        if (ret < 0) {
            TPD_INFO("PROC_READ_REGISTER read error");
            goto proc_read_err;
        }
        break;
    case PROC_WRITE_REGISTER:
        break;

    case PROC_READ_DATA:
        num_read_chars = buflen;
        ret = touch_i2c_read(ts->client, NULL, 0, readbuf, num_read_chars);
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

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }

    return ret;
}

static const struct file_operations ftxxxx_proc_fops = {
    .owner  = THIS_MODULE,
    .read   = fts_debug_read,
    .write  = fts_debug_write,
};


/*proc/touchpanel/baseline_test*/
static int fts_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = (struct touchpanel_data *)s->private;
    struct focal_debug_func *focal_debug_ops = NULL;
    struct fts_proc_operations *syna_ops = NULL;
    const struct firmware *fw = NULL;
    int ret = 0;
    int fd = -1;

    struct focal_testdata focal_testdata = {
        .TX_NUM = 0,
        .RX_NUM = 0,
        .fd = -1,
        .irq_gpio = -1,
        .key_TX = 0,
        .key_RX = 0,
        .TP_FW = 0,
        .fw = NULL,
        .fd_support = false,
        .fingerprint_underscreen_support = false,
    };

    if (!ts)
        return 0;
    focal_debug_ops = (struct focal_debug_func *)ts->private_data;

    syna_ops = g_syna_ops;
    if (!syna_ops)
        return 0;
    if (!syna_ops->auto_test) {
        seq_printf(s, "Not support auto-test proc node\n");
        return 0;
    }

    /*if resume not completed, do not do screen on test*/
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume state\n");
        return 0;
    }

    //step1:disable_irq && get mutex locked
    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    //step3:request test limit data from userspace
    ret = request_real_test_limit(ts,&fw, ts->panel_data.test_limit_name, ts->dev);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        seq_printf(s, "No limit IMG\n");
        mutex_unlock(&ts->mutex);
        if (ts->int_mode == BANNABLE) {
            enable_irq(ts->irq);
        }
        return 0;
    }

    ts->in_test_process = true;
    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, false);
        }
    }

    //step4:init focal_testdata
    focal_testdata.fd = fd;
    focal_testdata.TX_NUM = ts->hw_res.TX_NUM;
    focal_testdata.RX_NUM = ts->hw_res.RX_NUM;
    focal_testdata.irq_gpio = ts->hw_res.irq_gpio;
    focal_testdata.key_TX = ts->hw_res.key_TX;
    focal_testdata.key_RX = ts->hw_res.key_RX;
    focal_testdata.TP_FW = ts->panel_data.TP_FW;
    focal_testdata.fw = fw;
    focal_testdata.fd_support = ts->face_detect_support;
    focal_testdata.fingerprint_underscreen_support = ts->fingerprint_underscreen_support;

    syna_ops->auto_test(s, ts->chip_data, &focal_testdata);


    release_firmware(fw);

    //step6: return to normal mode
    ts->ts_ops->reset(ts->chip_data);
    operate_mode_switch(ts);

    //step7: unlock the mutex && enable irq trigger
    mutex_unlock(&ts->mutex);
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }

    if (ts->esd_handle_support) {
        if (focal_debug_ops && focal_debug_ops->esd_check_enable) {
            focal_debug_ops->esd_check_enable(ts->chip_data, true);
        }
    }
    ts->in_test_process = false;
    TPD_INFO("%s -\n", __func__);
    return 0;
}

static int fts_baseline_autotest_open(struct inode *inode, struct file *file)
{
    return single_open(file, fts_auto_test_read_func, PDE_DATA(inode));
}

static const struct file_operations fts_auto_test_proc_fops = {
    .owner = THIS_MODULE,
    .open  = fts_baseline_autotest_open,
    .read  = seq_read,
    .release = single_release,
};


int fts_create_proc(struct touchpanel_data *ts, struct fts_proc_operations *syna_ops)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_tmp = NULL;

    g_syna_ops = syna_ops;
    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp, &fts_auto_test_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    proc.proc_entry = proc_create_data(PROC_NAME, 0777, NULL, &ftxxxx_proc_fops, ts);
    if (NULL == proc.proc_entry) {
        TPD_INFO("create proc entry fail");
        return -ENOMEM;
    }

    return ret;
}

#define LEN_DOZE_FDM_ROW_DATA 2
#define NUM_MODE 2
#define LEN_TEST_ITEM_FIELD 16
#define LIMIT_HEADER_MAGIC_1 0x494D494C
#define LIMIT_HEADER_MAGIC_2 0x474D4954

void ft_limit_read_std(struct seq_file *s, struct touchpanel_data *ts)
{
    int ret = 0, m = 0, i = 0, j = 0, item_cnt = 0;
    const struct firmware *fw = NULL;
    struct auto_test_header *ph = NULL;
    struct auto_test_item_header *item_head = NULL;
    uint32_t *p_item_offset = NULL;
    int32_t *p_data32 = NULL;
    int tx = ts->hw_res.TX_NUM;
    int rx = ts->hw_res.RX_NUM;
    int num_channel = rx  + tx ;
    int num_panel_node = rx  * tx ;


    ret = request_real_test_limit(ts,&fw, ts->panel_data.test_limit_name, ts->dev);
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
        if (item_head->item_magic != Limit_ItemMagic && item_head->item_magic != Limit_ItemMagic_V2) {
            TPD_INFO("item: %d limit data has some problem\n", item_head->item_bit);
            seq_printf(s, "item: %d limit data has some problem\n", item_head->item_bit);
            continue;
        }
        TPD_INFO("item %d[size %d, limit type %d, para num %d] :\n", item_head->item_bit, item_head->item_size, item_head->item_limit_type, item_head->para_num);
        seq_printf(s, "item %d[size %d, limit type %d, para num %d] :\n", item_head->item_bit, item_head->item_size, item_head->item_limit_type, item_head->para_num);
        if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
            seq_printf(s, "no limit data\n");
        } else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
            if(item_head->item_bit == TYPE_RAW_DATA) {
                seq_printf(s, "TYPE_FW_RAWDATA: \n");
            } else if(item_head->item_bit == TYPE_NOISE_DATA) {
                seq_printf(s, "TYPE_NOISE_DATA: \n");
            } else if(item_head->item_bit == TYPE_UNIFORMITY_DATA) {
                seq_printf(s, "TYPE_UNIFORMITY_DATA: \n");
            } else if(item_head->item_bit == TYPE_PANEL_DIFFER_DATA) {
                seq_printf(s, "TYPE_PANEL_DIFFER_DATA: \n");
            }
            TPD_INFO("top data [%d]: \n", m);
            seq_printf(s, "top data: \n");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            if (p_data32) {
                for (i = 0 ; i < num_panel_node; i++) {
                    if (i % rx == 0)
                        seq_printf(s, "\n[%2d] ", (i / rx));
                    seq_printf(s, "%4d, ", p_data32[i]);
                    TPD_DEBUG("%d, ", p_data32[i]);
                }
                seq_printf(s, "\nfloor data: \n");
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
        if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_RX_TX_DATA) {
            if(item_head->item_bit == TYPE_SCAP_CB_DATA) {
                seq_printf(s, "TYPE_FW_RAWDATA: \n");
            } else if(item_head->item_bit == TYPE_SCAP_RAW_DATA) {
                seq_printf(s, "TYPE_OPEN_RAWDATA: \n");
            }
            TPD_INFO("top data [%d]: \n", m);
            seq_printf(s, "water proof mode: \n");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            TPD_INFO("size 1: %d * %d = %d \n", NUM_MODE, num_panel_node, (NUM_MODE * num_panel_node));
            if (p_data32) {
                for (i = 0 ; i < num_channel; i++) {
                    if (i % num_panel_node == 0)
                        seq_printf(s, "\n[%2d] ", (i / num_panel_node));
                    seq_printf(s, "%4d, ", p_data32[i]);
                    TPD_DEBUG("%d, ", p_data32[i]);
                }
                seq_printf(s, "\nNormal mode: \n");
                p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
                TPD_INFO("size 2: %d * %d = %d \n", NUM_MODE, num_panel_node, (NUM_MODE * num_panel_node));
                for (i = 0 ; i < num_channel; i++) {
                    if (i % num_panel_node == 0) {
                        seq_printf(s, "\n[%2d] ", (i / num_panel_node));
                    }
                    seq_printf(s, "%4d, ", p_data32[i]);
                    TPD_DEBUG("%d, ", p_data32[i]);
                }
            } else {
                TPD_INFO("%s: screen off, p_data32 is NULL \n", __func__);
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

