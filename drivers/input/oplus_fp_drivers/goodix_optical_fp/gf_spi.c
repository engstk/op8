// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt)    KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>

#include "../include/wakelock.h"
#include "gf_spi.h"
#include "../include/oplus_fp_common.h"
#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif
#include <soc/oplus/system/boot_mode.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/uaccess.h>
#endif

#include "../include/fingerprint_event.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#define FB_EARLY_EVENT_BLANK    0x10
#endif

#define VER_MAJOR   1
#define VER_MINOR   2
#define PATCH_LEVEL 9

#define WAKELOCK_HOLD_TIME 500 /* in ms */
#define SENDCMD_WAKELOCK_HOLD_TIME 1000 /* in ms */

#define GF_SPIDEV_NAME       "goodix,goodix_fp"
/*device name after register in charater*/
#define GF_DEV_NAME          "goodix_fp"

#define CHRD_DRIVER_NAME     "goodix_fp_spi"
#define CLASS_NAME           "goodix_fp"
#define GF_INPUT_NAME "qwerty"  /*"goodix_fp" */
#define N_SPI_MINORS         32	/* ... up to 256 */

struct fp_underscreen_info fp_tpinfo;
static unsigned int lasttouchmode = 0;

static int SPIDEV_MAJOR;

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static struct wake_lock fp_wakelock;
static struct wake_lock gf_cmd_wakelock;
struct gf_dev gf;
struct gf_key_map maps[] = {
    {EV_KEY, GF_KEY_INPUT_HOME},
    {EV_KEY, GF_KEY_INPUT_MENU},
    {EV_KEY, GF_KEY_INPUT_BACK},
    {EV_KEY, GF_KEY_INPUT_POWER},
#if defined(SUPPORT_NAV_EVENT)
    {EV_KEY, GF_NAV_INPUT_UP},
    {EV_KEY, GF_NAV_INPUT_DOWN},
    {EV_KEY, GF_NAV_INPUT_RIGHT},
    {EV_KEY, GF_NAV_INPUT_LEFT},
    {EV_KEY, GF_KEY_INPUT_CAMERA},
    {EV_KEY, GF_NAV_INPUT_CLICK},
    {EV_KEY, GF_NAV_INPUT_DOUBLE_CLICK},
    {EV_KEY, GF_NAV_INPUT_LONG_PRESS},
    {EV_KEY, GF_NAV_INPUT_HEAVY},
#endif
};

static int gf_opticalfp_irq_handler(struct fp_underscreen_info *tp_info);

static void gf_enable_irq(struct gf_dev *gf_dev)
{
    if (gf_dev->irq_enabled) {
        pr_warn("IRQ has been enabled.\n");
    } else {
        enable_irq(gf_dev->irq);
        gf_dev->irq_enabled = 1;
    }
}

static void gf_disable_irq(struct gf_dev *gf_dev)
{
    if (gf_dev->irq_enabled) {
        gf_dev->irq_enabled = 0;
        disable_irq(gf_dev->irq);
    } else {
        pr_warn("IRQ has been disabled.\n");
    }
}

#ifdef AP_CONTROL_CLK
static long spi_clk_max_rate(struct clk *clk, unsigned long rate)
{
    long lowest_available, nearest_low, step_size, cur;
    long step_direction = -1;
    long guess = rate;
    int max_steps = 10;

    cur = clk_round_rate(clk, rate);
    if (cur == rate) {
        return rate;
    }
    /* if we got here then: cur > rate */
    lowest_available = clk_round_rate(clk, 0);
    if (lowest_available > rate) {
        return -EINVAL;
    }

    step_size = (rate - lowest_available) >> 1;
    nearest_low = lowest_available;

    while (max_steps-- && step_size) {
        guess += step_size * step_direction;
        cur = clk_round_rate(clk, guess);

        if ((cur < rate) && (cur > nearest_low)) {
            nearest_low = cur;
        }
        /*
         * if we stepped too far, then start stepping in the other
         * direction with half the step size
         */
        if (((cur > rate) && (step_direction > 0))
                || ((cur < rate) && (step_direction < 0))) {
            step_direction = -step_direction;
            step_size >>= 1;
        }
    }
    return nearest_low;
}

static void spi_clock_set(struct gf_dev *gf_dev, int speed)
{
    long rate;
    int rc;

    rate = spi_clk_max_rate(gf_dev->core_clk, speed);
    if (rate < 0) {
        pr_info("%s: no match found for requested clock frequency:%d",
                __func__, speed);
        return;
    }

    rc = clk_set_rate(gf_dev->core_clk, rate);
}

static int gfspi_ioctl_clk_init(struct gf_dev *data)
{
    pr_debug("%s: enter\n", __func__);

    data->clk_enabled = 0;
    data->core_clk = clk_get(&data->spi->dev, "core_clk");
    if (IS_ERR_OR_NULL(data->core_clk)) {
        pr_err("%s: fail to get core_clk\n", __func__);
        return -EPERM;
    }
    data->iface_clk = clk_get(&data->spi->dev, "iface_clk");
    if (IS_ERR_OR_NULL(data->iface_clk)) {
        pr_err("%s: fail to get iface_clk\n", __func__);
        clk_put(data->core_clk);
        data->core_clk = NULL;
        return -ENOENT;
    }
    return 0;
}

static int gfspi_ioctl_clk_enable(struct gf_dev *data)
{
    int err;

    pr_debug("%s: enter\n", __func__);

    if (data->clk_enabled)
        return 0;

    err = clk_prepare_enable(data->core_clk);
    if (err) {
        pr_debug("%s: fail to enable core_clk\n", __func__);
        return -EPERM;
    }

    err = clk_prepare_enable(data->iface_clk);
    if (err) {
        pr_debug("%s: fail to enable iface_clk\n", __func__);
        clk_disable_unprepare(data->core_clk);
        return -ENOENT;
    }

    data->clk_enabled = 1;

    return 0;
}

static int gfspi_ioctl_clk_disable(struct gf_dev *data)
{
    pr_debug("%s: enter\n", __func__);

    if (!data->clk_enabled) {
        return 0;
    }

    clk_disable_unprepare(data->core_clk);
    clk_disable_unprepare(data->iface_clk);
    data->clk_enabled = 0;

    return 0;
}

static int gfspi_ioctl_clk_uninit(struct gf_dev *data)
{
    pr_debug("%s: enter\n", __func__);

    if (data->clk_enabled) {
        gfspi_ioctl_clk_disable(data);
    }

    if (!IS_ERR_OR_NULL(data->core_clk)) {
        clk_put(data->core_clk);
        data->core_clk = NULL;
    }

    if (!IS_ERR_OR_NULL(data->iface_clk)) {
        clk_put(data->iface_clk);
        data->iface_clk = NULL;
    }

    return 0;
}
#endif

static void gf_kernel_key_input(struct gf_dev *gf_dev, struct gf_key *gf_key)
{
    uint32_t key_input = 0;
    if (GF_KEY_HOME == gf_key->key)
    {
        key_input = GF_KEY_INPUT_HOME;
    }
    else if (GF_KEY_POWER == gf_key->key)
    {
        key_input = GF_KEY_INPUT_POWER;
    }
    else if (GF_KEY_CAMERA == gf_key->key)
    {
        key_input = GF_KEY_INPUT_CAMERA;
    }
    else
    {
        /* add special key define */
        key_input = gf_key->key;
    }
    pr_info("%s: received key event[%d], key=%d, value=%d\n",
            __func__, key_input, gf_key->key, gf_key->value);

    if ((GF_KEY_POWER == gf_key->key || GF_KEY_CAMERA == gf_key->key) && (gf_key->value == 1))
    {
        input_report_key(gf_dev->input, key_input, 1);
        input_sync(gf_dev->input);
        input_report_key(gf_dev->input, key_input, 0);
        input_sync(gf_dev->input);
    }

    if (GF_KEY_HOME == gf_key->key)
    {
        input_report_key(gf_dev->input, key_input, gf_key->value);
        input_sync(gf_dev->input);
    }
}

static irqreturn_t gf_irq(int irq, void *handle)
{
#if defined(GF_NETLINK_ENABLE)
    char msg = GF_NET_EVENT_IRQ;
    wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
    sendnlmsg(&msg);
    send_fingerprint_message(E_FP_SENSOR, msg, NULL, 0);
#elif defined (GF_FASYNC)
    struct gf_dev *gf_dev = &gf;
    if (gf_dev->async) {
        kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
    }
#endif

    return IRQ_HANDLED;
}

static int irq_setup(struct gf_dev *gf_dev)
{
    int status;

    gf_dev->irq = gf_irq_num(gf_dev);
    status = request_threaded_irq(gf_dev->irq, NULL, gf_irq,
            IRQF_TRIGGER_RISING | IRQF_ONESHOT, "gf", gf_dev);

    if (status) {
        pr_err("failed to request IRQ:%d\n", gf_dev->irq);
        return status;
    }
    enable_irq_wake(gf_dev->irq);
    gf_dev->irq_enabled = 1;

    return status;
}

static void irq_cleanup(struct gf_dev *gf_dev)
{
    gf_dev->irq_enabled = 0;
    disable_irq(gf_dev->irq);
    disable_irq_wake(gf_dev->irq);
    free_irq(gf_dev->irq, gf_dev);//need modify
}

static void gf_auto_send_touchdown(void)
{
    struct fp_underscreen_info tp_info;
    tp_info.touch_state = 1;
    gf_opticalfp_irq_handler(&tp_info);
}

static void gf_auto_send_touchup(void)
{
    struct fp_underscreen_info tp_info;
    tp_info.touch_state = 0;
    gf_opticalfp_irq_handler(&tp_info);
}

static long gf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct gf_dev *gf_dev = &gf;
    int retval = 0;
    u8 netlink_route = NETLINK_TEST;
    struct gf_ioc_chip_info info;
    struct gf_key gf_key;

    if (_IOC_TYPE(cmd) != GF_IOC_MAGIC) {
        return -ENODEV;
    }

    if (_IOC_DIR(cmd) & _IOC_READ) {
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
        #else
        retval = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
        #endif
    } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
        #else
        retval = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
        #endif
    }
    if (retval) {
        return -EFAULT;
    }

    if (gf_dev->device_available == 0) {
        if ((cmd == GF_IOC_ENABLE_POWER) || (cmd == GF_IOC_DISABLE_POWER) || (cmd == GF_IOC_POWER_RESET)) {
            pr_info("power cmd\n");
        } else {
            pr_info("Sensor is power off currently. \n");
            return -ENODEV;
        }
    }

    switch (cmd) {
        case GF_IOC_INIT:
            pr_debug("%s GF_IOC_INIT\n", __func__);
            if (copy_to_user((void __user *)arg, (void *)&netlink_route, sizeof(u8))) {
                retval = -EFAULT;
                break;
            }
            break;
        case GF_IOC_EXIT:
            pr_debug("%s GF_IOC_EXIT\n", __func__);
            break;
        case GF_IOC_DISABLE_IRQ:
            pr_debug("%s GF_IOC_DISABEL_IRQ\n", __func__);
            gf_disable_irq(gf_dev);
            break;
        case GF_IOC_ENABLE_IRQ:
            pr_debug("%s GF_IOC_ENABLE_IRQ\n", __func__);
            gf_enable_irq(gf_dev);
            break;
        case GF_IOC_RESET:
            pr_info("%s GF_IOC_RESET. \n", __func__);
            gf_hw_reset(gf_dev, 10);
            break;
        case GF_IOC_POWER_RESET:
            pr_info("%s GF_IOC_POWER_RESET. \n", __func__);
            gf_power_reset(gf_dev);
            gf_dev->device_available = 1;
            break;
        case GF_IOC_INPUT_KEY_EVENT:
            if (copy_from_user(&gf_key, (struct gf_key *)arg, sizeof(struct gf_key))) {
                pr_info("Failed to copy input key event from user to kernel\n");
                retval = -EFAULT;
                break;
            }

            gf_kernel_key_input(gf_dev, &gf_key);
            break;
        case GF_IOC_ENABLE_SPI_CLK:
            pr_debug("%s GF_IOC_ENABLE_SPI_CLK\n",  __func__);
#ifdef AP_CONTROL_CLK
            gfspi_ioctl_clk_enable(gf_dev);
#else
            pr_debug("Doesn't support control clock.\n");
#endif
            break;
        case GF_IOC_DISABLE_SPI_CLK:
            pr_debug("%s GF_IOC_DISABLE_SPI_CLK\n", __func__);
#ifdef AP_CONTROL_CLK
            gfspi_ioctl_clk_disable(gf_dev);
#else
            pr_debug("Doesn't support control clock\n");
#endif
            break;
        case GF_IOC_ENABLE_POWER:
            pr_debug("%s GF_IOC_ENABLE_POWER\n", __func__);
            if (gf_dev->device_available == 1)
                pr_info("Sensor has already powered-on.\n");
            else
                gf_power_on(gf_dev);
            gf_dev->device_available = 1;
            break;
        case GF_IOC_DISABLE_POWER:
            pr_debug("%s GF_IOC_DISABLE_POWER\n", __func__);
            if (gf_dev->device_available == 0)
                pr_info("Sensor has already powered-off.\n");
            else
                gf_power_off(gf_dev);
            gf_dev->device_available = 0;
            break;
        case GF_IOC_ENTER_SLEEP_MODE:
            pr_debug("%s GF_IOC_ENTER_SLEEP_MODE\n", __func__);
            break;
        case GF_IOC_GET_FW_INFO:
            pr_debug("%s GF_IOC_GET_FW_INFO\n", __func__);
            break;

        case GF_IOC_REMOVE:
            irq_cleanup(gf_dev);
            gf_cleanup(gf_dev);
            pr_debug("%s GF_IOC_REMOVE\n", __func__);
            send_fingerprint_message(E_FP_HAL, 0, NULL, 0);
            break;

        case GF_IOC_CHIP_INFO:
            pr_debug("%s GF_IOC_CHIP_INFO\n", __func__);
            if (copy_from_user(&info, (struct gf_ioc_chip_info *)arg, sizeof(struct gf_ioc_chip_info))) {
                retval = -EFAULT;
                break;
            }
            pr_info("vendor_id : 0x%x\n", info.vendor_id);
            pr_info("mode : 0x%x\n", info.mode);
            pr_info("operation: 0x%x\n", info.operation);
            break;
        case GF_IOC_WAKELOCK_TIMEOUT_ENABLE:
            pr_debug("%s GF_IOC_WAKELOCK_TIMEOUT_ENABLE\n", __func__);
            wake_lock_timeout(&gf_cmd_wakelock, msecs_to_jiffies(SENDCMD_WAKELOCK_HOLD_TIME));
            break;
        case GF_IOC_WAKELOCK_TIMEOUT_DISABLE:
            pr_debug("%s GF_IOC_WAKELOCK_TIMEOUT_DISABLE\n", __func__);
            wake_unlock(&gf_cmd_wakelock);
            break;
        case GF_IOC_CLEAN_TOUCH_FLAG:
            lasttouchmode = 0;
            pr_debug("%s GF_IOC_CLEAN_TOUCH_FLAG\n", __func__);
            break;
        case GF_IOC_AUTO_SEND_TOUCHDOWN:
            pr_info("%s GF_IOC_AUTO_SEND_TOUCHDOWN\n", __func__);
            gf_auto_send_touchdown();
            break;
        case GF_IOC_AUTO_SEND_TOUCHUP:
            pr_info("%s GF_IOC_AUTO_SEND_TOUCHUP\n", __func__);
            gf_auto_send_touchup();
            break;
        case GF_IOC_STOP_WAIT_INTERRUPT_EVENT:
            pr_info("%s GF_IOC_STOP_WAIT_INTERRUPT_EVENT\n", __func__);
            send_fingerprint_message(E_FP_HAL, 0, NULL, 0);
            break;
        default:
            pr_warn("unsupport cmd:0x%x\n", cmd);
            break;
    }

    return retval;
}

#ifdef CONFIG_COMPAT
static long gf_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return gf_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif /*CONFIG_COMPAT*/


static int gf_open(struct inode *inode, struct file *filp)
{
    struct gf_dev *gf_dev = &gf;
    int status = -ENXIO;

    mutex_lock(&device_list_lock);

    list_for_each_entry(gf_dev, &device_list, device_entry) {
        if (gf_dev->devt == inode->i_rdev) {
            pr_info("Found\n");
            status = 0;
            break;
        }
    }

    if (status == 0) {
        if (status == 0) {
            gf_dev->users++;
            filp->private_data = gf_dev;
            nonseekable_open(inode, filp);
            pr_info("Succeed to open device. irq = %d\n",
                    gf_dev->irq);
            if (gf_dev->users == 1) {
                status = gf_parse_dts(gf_dev);
                if (status)
                    goto out;

                status = irq_setup(gf_dev);
                if (status) {
                    gf_cleanup(gf_dev);
                    goto out;
                }
            }
        }
    } else {
        pr_info("No device for minor %d\n", iminor(inode));
    }

out:
    mutex_unlock(&device_list_lock);
    return status;
}

#ifdef GF_FASYNC
static int gf_fasync(int fd, struct file *filp, int mode)
{
    struct gf_dev *gf_dev = filp->private_data;
    int ret;

    ret = fasync_helper(fd, filp, mode, &gf_dev->async);
    pr_info("ret = %d\n", ret);
    return ret;
}
#endif

static int gf_release(struct inode *inode, struct file *filp)
{
    struct gf_dev *gf_dev = &gf;
    int status = 0;

    mutex_lock(&device_list_lock);
    gf_dev = filp->private_data;
    filp->private_data = NULL;

    /*last close?? */
    gf_dev->users--;
    if (!gf_dev->users) {
        irq_cleanup(gf_dev);
        gf_cleanup(gf_dev);
        /*power off the sensor*/
        gf_dev->device_available = 0;
    }
    mutex_unlock(&device_list_lock);
    return status;
}

ssize_t gf_read(struct file * f, char __user *buf, size_t count, loff_t *offset)
{
    struct fingerprint_message_t *rcv_msg = NULL;
    pr_info("gf_read enter");
    if (buf == NULL || f == NULL || count != sizeof(struct fingerprint_message_t)) {
        return 0;
    }
    pr_info("begin wait for driver event");
    if (wait_fingerprint_event(NULL, 0, &rcv_msg)) {
        return -2;
    }
    if (rcv_msg == NULL) {
        return -3;
    }
    if (copy_to_user(buf, rcv_msg, count)) {
        return -EFAULT;
    }
    pr_info("end wait for driver event");
    return count;
}

static const struct file_operations gf_fops = {
    .owner = THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
     * gets more complete API coverage.  It'll simplify things
     * too, except for the locking.
     */
    .unlocked_ioctl = gf_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = gf_compat_ioctl,
#endif /*CONFIG_COMPAT*/
    .open = gf_open,
    .release = gf_release,
#ifdef GF_FASYNC
    .fasync = gf_fasync,
#endif
    .read = gf_read,
};

static int goodix_fb_state_chg_callback(struct notifier_block *nb,
        unsigned long val, void *data)
{
    struct gf_dev *gf_dev;
    struct fb_event *evdata = data;
    unsigned int blank;
    char msg = 0;
    int retval = 0;

    gf_dev = container_of(nb, struct gf_dev, notifier);

    if (val == MSM_DRM_ONSCREENFINGERPRINT_EVENT) {
        uint8_t op_mode = 0x0;
        op_mode = *(uint8_t *)evdata->data;

        switch (op_mode) {
            case 0:
                pr_info("[%s] UI disappear\n", __func__);
                msg = GF_NET_EVENT_UI_DISAPPEAR;
                sendnlmsg(&msg);
                break;
            case 1:
                pr_info("[%s] UI ready \n", __func__);
                msg = GF_NET_EVENT_UI_READY;
                sendnlmsg(&msg);
                break;
            default:
                pr_info("[%s] Unknown MSM_DRM_ONSCREENFINGERPRINT_EVENT\n", __func__);
                break;
        }
        send_fingerprint_message(E_FP_LCD, op_mode, NULL, 0);
        return retval;
    }

    if (evdata && evdata->data && val == FB_EARLY_EVENT_BLANK && gf_dev) {
        blank = *(int *)(evdata->data);
        switch (blank) {
            case FB_BLANK_POWERDOWN:
                if (gf_dev->device_available == 1) {
                    gf_dev->fb_black = 1;
#if defined(GF_NETLINK_ENABLE)
                    msg = GF_NET_EVENT_FB_BLACK;
                    sendnlmsg(&msg);
                    send_fingerprint_message(E_FP_SENSOR, msg, NULL, 0);
#elif defined (GF_FASYNC)
                    if (gf_dev->async) {
                        kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
                    }
#endif
                }
                break;
            case FB_BLANK_UNBLANK:
                if (gf_dev->device_available == 1) {
                    gf_dev->fb_black = 0;
#if defined(GF_NETLINK_ENABLE)
                    msg = GF_NET_EVENT_FB_UNBLACK;
                    sendnlmsg(&msg);
                    send_fingerprint_message(E_FP_SENSOR, msg, NULL, 0);
#elif defined (GF_FASYNC)
                    if (gf_dev->async) {
                        kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
                    }
#endif
                }
                break;
            default:
                pr_info("%s defalut\n", __func__);
                break;
        }
    }
    return NOTIFY_OK;
}

static struct notifier_block goodix_noti_block = {
    .notifier_call = goodix_fb_state_chg_callback,
};

static int gf_opticalfp_irq_handler(struct fp_underscreen_info *tp_info)
{
    char msg = 0;
    fp_tpinfo = *tp_info;
    if(tp_info->touch_state== lasttouchmode){
        return IRQ_HANDLED;
    }
    wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
    if (1 == tp_info->touch_state) {
        msg = GF_NET_EVENT_TP_TOUCHDOWN;
        sendnlmsg(&msg);
        lasttouchmode = tp_info->touch_state;
        send_fingerprint_message(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
    } else {
        msg = GF_NET_EVENT_TP_TOUCHUP;
        sendnlmsg(&msg);
        send_fingerprint_message(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
        lasttouchmode = tp_info->touch_state;
    }

    return IRQ_HANDLED;
}


static struct class *gf_class;
#if defined(USE_SPI_BUS)
static int gf_probe(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int gf_probe(struct platform_device *pdev)
#endif
{
    struct gf_dev *gf_dev = &gf;
    int status = -EINVAL;
    unsigned long minor;
    int boot_mode = 0;
    int i;
    /* Initialize the driver data */
    INIT_LIST_HEAD(&gf_dev->device_entry);
#if defined(USE_SPI_BUS)
    gf_dev->spi = spi;
#elif defined(USE_PLATFORM_BUS)
    gf_dev->spi = pdev;
#endif
    gf_dev->irq_gpio = -EINVAL;
    gf_dev->reset_gpio = -EINVAL;
    gf_dev->pwr_gpio = -EINVAL;
    gf_dev->device_available = 0;
    gf_dev->fb_black = 0;

    /* If we can allocate a minor number, hook up this device.
     * Reusing minors is fine so long as udev or mdev is working.
     */
    mutex_lock(&device_list_lock);
    minor = find_first_zero_bit(minors, N_SPI_MINORS);
    if (minor < N_SPI_MINORS) {
        struct device *dev;

        gf_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
        dev = device_create(gf_class, &gf_dev->spi->dev, gf_dev->devt,
                gf_dev, GF_DEV_NAME);
        status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
    } else {
        dev_dbg(&gf_dev->spi->dev, "no minor number available!\n");
        status = -ENODEV;
        mutex_unlock(&device_list_lock);
        goto error_hw;
    }

    if (status == 0) {
        set_bit(minor, minors);
        list_add(&gf_dev->device_entry, &device_list);
    } else {
        gf_dev->devt = 0;
    }
    mutex_unlock(&device_list_lock);

    if (status == 0)
    {
        /*input device subsystem */
        gf_dev->input = input_allocate_device();
        if (gf_dev->input == NULL)
        {
            pr_err("%s, failed to allocate input device\n", __func__);
            status = -ENOMEM;
            goto error_dev;
        }
        for (i = 0; i < ARRAY_SIZE(maps); i++)
            input_set_capability(gf_dev->input, maps[i].type, maps[i].code);

        gf_dev->input->name = GF_INPUT_NAME;
        status = input_register_device(gf_dev->input);
        if (status)
        {
            pr_err("failed to register input device\n");
            goto error_input;
        }
    }
#ifdef AP_CONTROL_CLK
    pr_info("Get the clk resource.\n");
    /* Enable spi clock */
    if (gfspi_ioctl_clk_init(gf_dev))
        goto gfspi_probe_clk_init_failed:

            if (gfspi_ioctl_clk_enable(gf_dev))
                goto gfspi_probe_clk_enable_failed;

    spi_clock_set(gf_dev, 1000000);
#endif

    gf_dev->notifier = goodix_noti_block;
#if defined(CONFIG_DRM_MSM)
    status = msm_drm_register_client(&gf_dev->notifier);
    if (status == -1) {
        return status;
    }
#elif defined(CONFIG_FB)
    status = fb_register_client(&gf_dev->notifier);
    if (status == -1) {
        return status;
    }
#endif
    wake_lock_init(&fp_wakelock, WAKE_LOCK_SUSPEND, "fp_wakelock");
    wake_lock_init(&gf_cmd_wakelock, WAKE_LOCK_SUSPEND, "gf_cmd_wakelock");
    pr_err("register goodix_fp_ok\n");
    pr_info("version V%d.%d.%02d\n", VER_MAJOR, VER_MINOR, PATCH_LEVEL);

    gf_parse_ftm_poweroff_flag(gf_dev);
    if (gf_dev->ftm_poweroff_flag) {
        boot_mode = get_boot_mode();
        if (MSM_BOOT_MODE__FACTORY == boot_mode)
        {
            pr_err("enter to fastbootmode,and power off\n");
            status = gf_parse_pwr_list(gf_dev);
            if (status) {
                pr_err("failed to parse power list, status = %d\n", status);
                gf_cleanup_pwr_list(gf_dev);
            } else {
                pr_err("enter to power off\n");
                gf_power_on(gf_dev);
                gf_power_off(gf_dev);
            }
        }
    }

    return status;

#ifdef AP_CONTROL_CLK
gfspi_probe_clk_enable_failed:
    gfspi_ioctl_clk_uninit(gf_dev);
gfspi_probe_clk_init_failed:
#endif

error_input:
    if (gf_dev->input != NULL)
        input_free_device(gf_dev->input);
error_dev:
    if (gf_dev->devt != 0)
    {
        pr_info("Err: status = %d\n", status);
        mutex_lock(&device_list_lock);
        list_del(&gf_dev->device_entry);
        device_destroy(gf_class, gf_dev->devt);
        clear_bit(MINOR(gf_dev->devt), minors);
        mutex_unlock(&device_list_lock);
    }
error_hw:
    gf_dev->device_available = 0;
    boot_mode = get_boot_mode();
    if (MSM_BOOT_MODE__FACTORY == boot_mode)
    {
        gf_power_off(gf_dev);
    }

    return status;
}

#if defined(USE_SPI_BUS)
static int gf_remove(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int gf_remove(struct platform_device *pdev)
#endif
{
    struct gf_dev *gf_dev = &gf;
    wake_lock_destroy(&fp_wakelock);
    wake_lock_destroy(&gf_cmd_wakelock);

    fb_unregister_client(&gf_dev->notifier);
    if (gf_dev->input)
        input_unregister_device(gf_dev->input);
    input_free_device(gf_dev->input);

    /* prevent new opens */
    mutex_lock(&device_list_lock);
    list_del(&gf_dev->device_entry);
    device_destroy(gf_class, gf_dev->devt);
    clear_bit(MINOR(gf_dev->devt), minors);
    mutex_unlock(&device_list_lock);

    return 0;
}

static struct of_device_id gx_match_table[] = {
    { .compatible = GF_SPIDEV_NAME },
    {},
};

#if defined(USE_SPI_BUS)
static struct spi_driver gf_driver = {
#elif defined(USE_PLATFORM_BUS)
static struct platform_driver gf_driver = {
#endif
    .driver = {
        .name = GF_DEV_NAME,
        .owner = THIS_MODULE,
        .of_match_table = gx_match_table,
    },
    .probe = gf_probe,
    .remove = gf_remove,
};

static int __init gf_init(void)
{
    int status;
    /* Claim our 256 reserved device numbers.  Then register a class
     * that will key udev/mdev to add/remove /dev nodes.  Last, register
     * the driver which manages those device numbers.
     */

    if ((FP_GOODIX_3268 != get_fpsensor_type())
            && (FP_GOODIX_3688 != get_fpsensor_type())
            && (FP_GOODIX_5288 != get_fpsensor_type())
            && (FP_GOODIX_5228 != get_fpsensor_type())
            && (FP_GOODIX_5658 != get_fpsensor_type())
            && (FP_GOODIX_OPTICAL_95 != get_fpsensor_type())
            && (FP_GOODIX_3626 != get_fpsensor_type())) {
        pr_err("%s, found not goodix sensor\n", __func__);
        status = -EINVAL;
        return status;
    }

    BUILD_BUG_ON(N_SPI_MINORS > 256);
    status = register_chrdev(SPIDEV_MAJOR, CHRD_DRIVER_NAME, &gf_fops);
    if (status < 0) {
        pr_warn("Failed to register char device!\n");
        return status;
    }
    SPIDEV_MAJOR = status;
    gf_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(gf_class)) {
        unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
        pr_warn("Failed to create class.\n");
        return PTR_ERR(gf_class);
    }
#if defined(USE_PLATFORM_BUS)
    status = platform_driver_register(&gf_driver);
#elif defined(USE_SPI_BUS)
    status = spi_register_driver(&gf_driver);
#endif
    if (status < 0) {
        class_destroy(gf_class);
        unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
        pr_warn("Failed to register SPI driver.\n");
        return status;
    }

#ifdef GF_NETLINK_ENABLE
    netlink_init();
#endif
    /*Register for receiving tp touch event.
     * Must register after get_fpsensor_type filtration as only one handler can be registered.
     */
    opticalfp_irq_handler_register(gf_opticalfp_irq_handler);
    pr_info("status = 0x%x\n", status);
    return 0;
}
late_initcall(gf_init);

static void __exit gf_exit(void)
{
#ifdef GF_NETLINK_ENABLE
    netlink_exit();
#endif
#if defined(USE_PLATFORM_BUS)
    platform_driver_unregister(&gf_driver);
#elif defined(USE_SPI_BUS)
    spi_unregister_driver(&gf_driver);
#endif
    class_destroy(gf_class);
    unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
}
module_exit(gf_exit);

MODULE_AUTHOR("Jiangtao Yi, <yijiangtao@goodix.com>");
MODULE_AUTHOR("Jandy Gou, <gouqingsong@goodix.com>");
MODULE_DESCRIPTION("goodix fingerprint sensor device driver");
MODULE_LICENSE("GPL");
