// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "../touchpanel_common.h"
#include "himax_common.h"

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "himax_common"
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


/*******Part1:Call Back Function implement*******/
void himax_parse_header(struct image_header_data *header, const unsigned char *fw_image)
{
    return;
}

void himax_limit_read(struct seq_file *s, struct touchpanel_data *ts)
{
    struct hx_limit_data *limit_data = NULL;
    struct himax_proc_operations *syna_ops;
    int i;
    int j;

    TPD_INFO("%s: Entering\n", __func__);
    syna_ops = (struct himax_proc_operations *)ts->private_data;
    limit_data = kzalloc(sizeof(struct hx_limit_data), GFP_KERNEL);
    if (!limit_data) {
        TPD_INFO("limit_data allocat fail!\n");
        goto FAIL_ALLOC_MEM;
    }
    if (!syna_ops->fp_hx_limit_get){
        TPD_INFO("%s:Doesn't support!\n", __func__);
        goto FAIL_FUNC;
    } else {
        syna_ops->fp_hx_limit_get(ts, limit_data);
    }
    if (limit_data != NULL) {
        for (i = 0 ; i < limit_data->item_size; i++) {
            seq_printf(s, "%s:\n",limit_data->item_name[i]);
            TPD_INFO("%s: [%d]Size of =%d\n", __func__, i, (int)sizeof(limit_data->item_name[i]));

                for (j = 0 ; j < limit_data->rawdata_size; j++) {
                    if (j % ( ts->hw_res.RX_NUM) == 0)
                        seq_printf(s, "\n[%2d] ", (j / ts->hw_res.RX_NUM));
                    seq_printf(s, "%4d, ", limit_data->crtra_val[i][j]);
                }
            seq_printf(s, "\n");
        }
    }
    seq_printf(s, "\n");
    TPD_INFO("%s: END PRINT!\n", __func__);
FAIL_FUNC:
    if (limit_data != NULL) {
        for (i = 0 ; i < limit_data->item_size; i++) {
            kfree(limit_data->item_name[i]);
            kfree(limit_data->crtra_val[i]);
        }
        kfree(limit_data);
    }
FAIL_ALLOC_MEM:
    TPD_INFO("%s: End\n", __func__);
}

//proc/touchpanel/baseline_test
static int tp_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct himax_proc_operations *syna_ops;
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    mm_segment_t old_fs;
    uint8_t data_buf[64];
//    int ret = 0;
    int fd = -1;

    struct syna_testdata syna_testdata =
    {
        .TX_NUM = 0,
        .RX_NUM = 0,
        .fd = -1,
        .irq_gpio = -1,
        .key_TX = 0,
        .key_RX = 0,
        .TP_FW = 0,
        .fw = NULL,
    };

    if (!ts)
        return 0;
    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;
    if (!syna_ops->auto_test) {
        seq_printf(s, "Not support auto-test proc node\n");
        return 0;
    }

    //step1:disable_irq && get mutex locked
    if (!ts->is_noflash_ic) {
        disable_irq_nosync(ts->client->irq);
    } else {
        disable_irq_nosync(ts->s_client->irq);
    }
    mutex_lock(&ts->mutex);

    //step2: create a file to store test data in /sdcard/Tp_Test
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
	sprintf(data_buf, "/sdcard/TpTestReport/tp_testlimit_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
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
    fd = ksys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
    sys_mkdir("/sdcard/TpTestReport", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);

    sys_mkdir("/sdcard/TpTestReport/screenOff", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/NG", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOff/OK", 0666);
    fd = sys_open(data_buf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif
    if (fd < 0) {
        TPD_INFO("Open log file '%s' failed.\n", data_buf);
        set_fs(old_fs);
    }

    //step4:init syna_testdata
    syna_testdata.fd = fd;
    syna_testdata.TX_NUM = ts->hw_res.TX_NUM;
    syna_testdata.RX_NUM = ts->hw_res.RX_NUM;
    syna_testdata.irq_gpio = ts->hw_res.irq_gpio;
    syna_testdata.key_TX = ts->hw_res.key_TX;
    syna_testdata.key_RX = ts->hw_res.key_RX;
    syna_testdata.TP_FW = ts->panel_data.TP_FW;

    syna_ops->auto_test(s, ts->chip_data, &syna_testdata);

    if (ts->auto_test_force_pass_support) {
        seq_printf(s, "0");
    }

    //step5: close file && release test limit firmware
    if (fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_close(fd);
#else
        sys_close(fd);
#endif
        set_fs(old_fs);
    }

    //step6: return to normal mode
    //ts->ts_ops->reset(ts->chip_data);
    //operate_mode_switch(ts);

    //step7: unlock the mutex && enable irq trigger
    mutex_unlock(&ts->mutex);
    if (!ts->is_noflash_ic) {
        enable_irq(ts->client->irq);
    } else {
        enable_irq(ts->s_client->irq);
    }

    return 0;
}

static int baseline_autotest_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_auto_test_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_auto_test_proc_fops = {
    .owner = THIS_MODULE,
    .open  = baseline_autotest_open,
    .read  = seq_read,
    .release = single_release,
};

static ssize_t himax_proc_register_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_register_read) {
        if(copy_to_user(buff, "Not support auto-test proc node\n", strlen("Not support auto-test proc node\n")))
                TPD_INFO("%s,here:%d\n",__func__,__LINE__);
        return 0;
    }

    mutex_lock(&ts->mutex);
    ret = syna_ops->himax_proc_register_read(file, buff, len, pos);
    mutex_unlock(&ts->mutex);
    return ret;
}

static ssize_t himax_proc_register_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_register_write) {
            TPD_INFO("Not support auto-test proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    mutex_lock(&ts->mutex);
    ret = syna_ops->himax_proc_register_write(file, buff, len, pos);
    mutex_unlock(&ts->mutex);
    return ret;
}

static struct file_operations himax_proc_register_ops =
{
    .owner = THIS_MODULE,
    .read = himax_proc_register_read,
    .write = himax_proc_register_write,
};

static ssize_t himax_proc_diag_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_diag_read) {
        if(copy_to_user(buff, "Not support auto-test proc node\n", strlen("Not support auto-test proc node\n")))
                TPD_INFO("%s,here:%d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_diag_read(file, buff, len, pos);
    return ret;
}

static ssize_t himax_proc_diag_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_diag_write) {
            TPD_INFO("Not support auto-test proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_diag_write(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_diag_ops =
{
    .owner = THIS_MODULE,
    .read = himax_proc_diag_read,
    .write = himax_proc_diag_write,
};

static ssize_t himax_proc_DD_debug_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_DD_debug_read) {
        if(copy_to_user(buff, "Not support auto-test proc node\n", strlen("Not support auto-test proc node\n")))
                TPD_INFO("%s,here:%d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_DD_debug_read(file, buff, len, pos);
    return ret;
}

static ssize_t himax_proc_DD_debug_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_DD_debug_write) {
            TPD_INFO("Not support dd proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_DD_debug_write(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_dd_debug_ops =
{
	.owner = THIS_MODULE,
	.read = himax_proc_DD_debug_read,
	.write = himax_proc_DD_debug_write,
};

static ssize_t himax_proc_FW_debug_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_FW_debug_read) {
            TPD_INFO("Not support fw proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_FW_debug_read(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_fw_debug_ops =
{
	.owner = THIS_MODULE,
	.read = himax_proc_FW_debug_read,
};

static ssize_t himax_proc_reset_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_reset_write) {
            TPD_INFO("Not support reset proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_reset_write(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_reset_ops =
{
	.owner = THIS_MODULE,
	.write = himax_proc_reset_write,
};

static ssize_t himax_proc_sense_on_off_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_sense_on_off_write) {
            TPD_INFO("Not support senseonoff proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_sense_on_off_write(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_sense_on_off_ops =
{
	.owner = THIS_MODULE,
	.write = himax_proc_sense_on_off_write,
};

static ssize_t himax_proc_vendor_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct himax_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct himax_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->himax_proc_vendor_read) {
            TPD_INFO("Not support fw proc node %s %d\n",__func__,__LINE__);
        return 0;
    }

    ret = syna_ops->himax_proc_vendor_read(file, buff, len, pos);
    return ret;
}

static struct file_operations himax_proc_vendor_ops =
{
    .owner = THIS_MODULE,
    .read = himax_proc_vendor_read,
};

int himax_create_proc(struct touchpanel_data *ts, struct himax_proc_operations *syna_ops)
{
    int ret = 0;

    // touchpanel_auto_test interface
    struct proc_dir_entry *prEntry_himax = NULL;
    struct proc_dir_entry *prEntry_tmp = NULL;
    ts->private_data = syna_ops;
    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp, &tp_auto_test_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files:/proc/touchpanel/debug_info/himax
    prEntry_himax = proc_mkdir("himax", ts->prEntry_debug_tp);
    if (prEntry_himax == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create /proc/touchpanel/debug_info/himax proc entry\n", __func__);
    }

   //proc files: /proc/touchpanel/debug_info/himax/register
    prEntry_tmp = proc_create_data("register", 0666, prEntry_himax, &himax_proc_register_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/diag
    prEntry_tmp = proc_create_data("diag", 0666, prEntry_himax, &himax_proc_diag_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/dd
    prEntry_tmp = proc_create_data("dd", 0666, prEntry_himax, &himax_proc_dd_debug_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/fw
    prEntry_tmp = proc_create_data("fw", 0666, prEntry_himax, &himax_proc_fw_debug_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/reset
    prEntry_tmp = proc_create_data("reset", 0666, prEntry_himax, &himax_proc_reset_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/senseonoff
    prEntry_tmp = proc_create_data("senseonoff", 0666, prEntry_himax, &himax_proc_sense_on_off_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files: /proc/touchpanel/debug_info/himax/vendor
    prEntry_tmp = proc_create_data("vendor", 0666, prEntry_himax, &himax_proc_vendor_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    return ret;
}
