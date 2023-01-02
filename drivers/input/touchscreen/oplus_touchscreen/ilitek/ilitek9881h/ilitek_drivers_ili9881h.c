// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek_ili9881h.h"
#include "data_transfer.h"
#include "firmware.h"
#include "finger_report.h"
#include "protocol.h"
#include "mp_test.h"
#include <linux/pm_wakeup.h>

#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"

#if (TP_PLATFORM == PT_MTK)
#define DTS_OF_NAME		"mediatek,cap_touch"
#include "tpd.h"
extern struct tpd_device *tpd;
#define MTK_RST_GPIO GTP_RST_PORT
#define MTK_INT_GPIO GTP_INT_PORT
#else
#define DTS_OF_NAME		"tchip,ilitek"
#endif /* PT_MTK */

#define DEVICE_ID	"ILITEK_TDDI"

#ifdef USE_KTHREAD
static DECLARE_WAIT_QUEUE_HEAD(waiter);
#endif

#ifndef CONFIG_TOUCHPANEL_MTK_PLATFORM
extern void disable_esd_thread(void);
#endif

/* Debug level */
uint32_t ipio_debug_level = DEBUG_NONE;
EXPORT_SYMBOL(ipio_debug_level);
struct wakeup_source *gesture_process_ws = NULL;

struct ilitek_chip_data_9881h *ipd = NULL;
static struct oplus_touchpanel_operations ilitek_ops;
static fw_update_state ilitek_fw_update(void *chip_data, const struct firmware *fw, bool force);

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

	if (ipd->reset_gpio) {
		if (isEnable) {
			core_config->ili_sleep_type = NOT_SLEEP_MODE;
			ret = core_firmware_get_hostdownload_data(UPDATE_FW_PATH);
			if (ret < 0) {
	            TPD_INFO("get host download data fail use default data\n");
				//goto out;
			}
			for (i = 0; i < 3; i++) {
#if (TP_PLATFORM == PT_MTK)
				TPD_DEBUG("HW Reset: HIGH\n");
				tpd_gpio_output(ipd->reset_gpio, 1);
				mdelay(ipd->delay_time_high);
				TPD_DEBUG("HW Reset: LOW\n");
				tpd_gpio_output(ipd->reset_gpio, 0);
				mdelay(ipd->delay_time_low);
				TPD_INFO("HW Reset: HIGH\n");
				tpd_gpio_output(ipd->reset_gpio, 1);
				mdelay(ipd->edge_delay);
#else
				TPD_DEBUG("HW Reset: HIGH\n");
				gpio_direction_output(ipd->reset_gpio, 1);
				mdelay(ipd->delay_time_high);
				TPD_DEBUG("HW Reset: LOW\n");
				gpio_set_value(ipd->reset_gpio, 0);
				mdelay(ipd->delay_time_low);
				TPD_INFO("HW Reset: HIGH\n");
				gpio_set_value(ipd->reset_gpio, 1);
				mdelay(ipd->edge_delay);
#endif /* PT_MTK */

#ifdef HOST_DOWNLOAD
				//core_config_ice_mode_enable();
				ret = core_firmware_upgrade(UPDATE_FW_PATH, true);
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
#if (TP_PLATFORM == PT_MTK)
			tpd_gpio_output(ipd->reset_gpio, 0);
#else
			gpio_set_value(ipd->reset_gpio, 0);
#endif /* PT_MTK */
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

#ifdef REGULATOR_POWER_ON
void ilitek_regulator_power_on(bool status)
{
	int res = 0;

	TPD_INFO("%s\n", status ? "POWER ON" : "POWER OFF");

	if (status) {
		if (ipd->vdd) {
			res = regulator_enable(ipd->vdd);
			if (res < 0)
				TPD_INFO("regulator_enable vdd fail\n");
		}
		if (ipd->vdd_i2c) {
			res = regulator_enable(ipd->vdd_i2c);
			if (res < 0)
				TPD_INFO("regulator_enable vdd_i2c fail\n");
		}
	} else {
		if (ipd->vdd) {
			res = regulator_disable(ipd->vdd);
			if (res < 0)
				TPD_INFO("regulator_enable vdd fail\n");
		}
		if (ipd->vdd_i2c) {
			res = regulator_disable(ipd->vdd_i2c);
			if (res < 0)
				TPD_INFO("regulator_enable vdd_i2c fail\n");
		}
	}
	core_config->icemodeenable = false;
	mdelay(5);
}
EXPORT_SYMBOL(ilitek_regulator_power_on);
#endif /* REGULATOR_POWER_ON */

int ilitek_platform_read_tp_info(void)
{
	if (core_config_get_chip_id() < 0)
		return CHIP_ID_ERR;
	if (core_config_get_protocol_ver() < 0)
		return -1;
	if (core_config_get_fw_ver() < 0)
		return -1;
	if (core_config_get_core_ver() < 0)
		return -1;
	if (core_config_get_tp_info() < 0)
		return -1;
	if (core_config->tp_info->nKeyCount > 0) {
		if (core_config_get_key_info() < 0)
			return -1;
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
	core_i2c_remove();
	core_protocol_remove();
}

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
	#if (INTERFACE == I2C_INTERFACE)
	if(core_i2c_init(ipd->client) < 0)
	#elif (INTERFACE == SPI_INTERFACE)
	if(core_spi_init(ipd->spi) < 0)
	#endif
	{
		TPD_INFO("Failed to initialise core components\n");
		return -EINVAL;
	}
	return 0;
}

#if (INTERFACE == I2C_INTERFACE)
static int ilitek_platform_remove(struct i2c_client *client)
#elif (INTERFACE == SPI_INTERFACE)
static int ilitek_platform_remove(struct spi_device *spi)
#endif
{
	TPD_INFO("Remove platform components\n");

	if (ipd->isEnableIRQ) {
		disable_irq_nosync(ipd->isr_gpio);
	}

	if (ipd->isr_gpio != 0 && ipd->int_gpio != 0 && ipd->reset_gpio != 0) {
		free_irq(ipd->isr_gpio, (void *)ipd->i2c_id);
		gpio_free(ipd->int_gpio);
		gpio_free(ipd->reset_gpio);
	}
#ifdef CONFIG_FB
	fb_unregister_client(&ipd->notifier_fb);
#else
	unregister_early_suspend(&ipd->early_suspend);
#endif /* CONFIG_FB */

#ifdef USE_KTHREAD
	if (ipd->irq_thread != NULL) {
		ipd->free_irq_thread = true;
		wake_up_interruptible(&waiter);
		kthread_stop(ipd->irq_thread);
		ipd->irq_thread = NULL;
	}
#endif /* USE_KTHREAD */

	if (ipd->input_device != NULL) {
		input_unregister_device(ipd->input_device);
		input_free_device(ipd->input_device);
	}

	if (ipd->vpower_reg_nb) {
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		destroy_workqueue(ipd->check_power_status_queue);
	}

	ipio_kfree((void **)&ipd);
	ilitek_platform_core_remove();

	return 0;
}

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
	if (s->size <= (4096 * 2)) {
		s->count = s->size;
		return;
	}
	ilitek_read_debug_data(s, chip_info, ILI_DIFFDATA);
}

static void ilitek_baseline_read(struct seq_file *s, void *chip_data)
{
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;
	TPD_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
	if (s->size <= (4096 * 2)) {
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

/**
 * The probe func would be called after an i2c device was detected by kernel.
 *
 * It will still return zero even if it couldn't get a touch ic info.
 * The reason for why we allow it passing the process is because users/developers
 * might want to have access to ICE mode to upgrade a firwmare forcelly.
 */
#if (INTERFACE == I2C_INTERFACE)
static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
#elif(INTERFACE == SPI_INTERFACE)
static int ilitek_platform_probe(struct spi_device *spi)
#endif
{
	int ret;
	struct touchpanel_data *ts = NULL;
	TPD_INFO("Probe Enter\n");
    
	/* initialise the struct of touch ic memebers. */
	ipd = kzalloc(sizeof(*ipd), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ipd)) {
		TPD_INFO("Failed to allocate ipd memory, %ld\n", PTR_ERR(ipd));
		return -ENOMEM;
	}
	/* Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        printk("ts kzalloc error\n");
        return -ENOMEM;	//???
    }
    memset(ts, 0, sizeof(*ts));
    ipd->ts = ts;
#if (INTERFACE == I2C_INTERFACE)
	if (client == NULL) {
		TPD_INFO("i2c client is NULL\n");
		return -ENODEV;
	}

	/* Set i2c slave addr if it's not configured */
	TPD_INFO("I2C Slave address = 0x%x\n", client->addr);
	if (client->addr != ILI7807_SLAVE_ADDR || client->addr != ILI9881_SLAVE_ADDR) {
		client->addr = ILI9881_SLAVE_ADDR;
		TPD_INFO("I2C Slave addr doesn't be set up, use default : 0x%x\n", client->addr);
	}
	ipd->client = client;
	ipd->i2c_id = id;
#elif(INTERFACE == SPI_INTERFACE)
	if(!spi)	
	{
		return -ENOMEM;
	}
    spi->chip_select = 0; //modify reg=0 for more tp vendor share same spi interface 
	ipd->spi = spi;
#endif
	ipd->chip_id = TP_TOUCH_IC;
	ipd->isEnableIRQ = true;
	ipd->isEnablePollCheckPower = false;
	ipd->vpower_reg_nb = false;

	TPD_INFO("Driver Version : %s\n", DRIVER_VERSION);
	TPD_INFO("Driver for Touch IC :  %x\n", TP_TOUCH_IC);
	TPD_INFO("Driver on platform :  %x\n", TP_PLATFORM);

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
	if (ipd->chip_id == CHIP_TYPE_ILI7807) {
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 5;
		ipd->edge_delay = 200;
	} else if (ipd->chip_id == CHIP_TYPE_ILI9881) {
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 5;
#if(INTERFACE == I2C_INTERFACE)
		ipd->edge_delay = 100;
#elif (INTERFACE == SPI_INTERFACE)
		ipd->edge_delay = 1;
#endif
		TPD_INFO("\n");
	} else {
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 10;
		ipd->edge_delay = 10;
		TPD_INFO("\n");
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
		return -ENOMEM;
	}

 	/* file_operations callbacks binding */
    ts->ts_ops = &ilitek_ops; 
    TPD_INFO("\n");

    

    /*register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        TPD_INFO("\n");
        goto err_register_driver;
    }
    ts->tp_suspend_order = TP_LCD_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
	ipd->reset_gpio = ts->hw_res.reset_gpio;
	ipd->int_gpio = ts->hw_res.irq_gpio;
	ipd->isr_gpio = ts->irq;
    ipd->fw_name = ts->panel_data.fw_name;
    ipd->test_limit_name = ts->panel_data.test_limit_name;
	ipd->resolution_x = ts->resolution_info.max_x;
	ipd->resolution_y = ts->resolution_info.max_y;
	ipd->fw_edge_limit_support = ts->fw_edge_limit_support;
    TPD_INFO("reset_gpio = %d int_gpio = %d irq = %d ipd->fw_name = %s\n", \
		ipd->reset_gpio, ipd->int_gpio, ipd->isr_gpio, ipd->fw_name);
    TPD_INFO("resolution_x = %d resolution_y = %d\n", ipd->resolution_x, ipd->resolution_y);

	if (core_firmware_get_h_file_data() < 0)
		TPD_INFO("Failed to get h file data\n");

	/* Create nodes for users */
	ilitek_proc_init();

#if (TP_PLATFORM == PT_MTK)
	tpd_load_status = 1;
#endif /* PT_MTK */

#ifdef BOOT_FW_UPGRADE
	ipd->update_thread = kthread_run(kthread_handler, "boot_fw", "ili_fw_boot");
	if (ipd->update_thread == (struct task_struct *)ERR_PTR) {
		ipd->update_thread = NULL;
		TPD_INFO("Failed to create fw upgrade thread\n");
	}
#endif /* BOOT_FW_UPGRADE */

    ilitek_create_proc_for_oplus(ts);

    if (ts->esd_handle_support) {
        ts->esd_info.esd_work_time = msecs_to_jiffies(ILITEK_TOUCH_ESD_CHECK_PERIOD); // change esd check interval to 1.5s
        TPD_INFO("%s:change esd handle time to %d ms\n", __func__, ts->esd_info.esd_work_time/HZ);
    }

    gesture_process_ws = wakeup_source_register("gesture_wake_lock");
    TPD_INFO("end\n");
	return 0;
err_register_driver:
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY
        || ts->boot_mode == MSM_BOOT_MODE__RF
        || ts->boot_mode == MSM_BOOT_MODE__WLAN)) {
        gesture_process_ws = wakeup_source_register("vooc_wake_lock");
        TPD_INFO("ftm mode probe end ok\n");
        return 0;
    }

    common_touch_data_free(ts);
    ts = NULL;

err_spi_setup:
    spi_set_drvdata(spi, NULL);

    kfree(ipd);
    ipd = NULL;

    TPD_INFO("err_spi_setup end\n");
    return -1;
}

static const struct i2c_device_id tp_device_id[] = {
	{DEVICE_ID, 0},
	{},			/* should not omitted */
};

MODULE_DEVICE_TABLE(i2c, tp_device_id);

/*
 * The name in the table must match the definiation
 * in a dts file.
 *
 */
static struct of_device_id tp_match_table[] = {
	{.compatible = DTS_OF_NAME},
	{},
};

#if (TP_PLATFORM == PT_MTK)
static int tpd_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	TPD_INFO("TPD detect i2c device\n");
	strcpy(info->type, TPD_DEVICE);
	return 0;
}
#endif /* PT_MTK */

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

void tp_goto_sleep_ftm(void)
{
    int ret = 0;

    if(ipd != NULL && ipd->ts != NULL) {
        TPD_INFO("ipd->ts->boot_mode = %d\n", ipd->ts->boot_mode);

        if ((ipd->ts->boot_mode == MSM_BOOT_MODE__FACTORY
            || ipd->ts->boot_mode == MSM_BOOT_MODE__RF
            || ipd->ts->boot_mode == MSM_BOOT_MODE__WLAN)) {
            ret = core_firmware_boot_host_download();

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

#if (INTERFACE == I2C_INTERFACE)
static struct i2c_driver tp_i2c_driver = {
	.driver = {
		   .name = DEVICE_ID,
		   .owner = THIS_MODULE,
		   .of_match_table = tp_match_table,
		   },
	.probe = ilitek_platform_probe,
	.remove = ilitek_platform_remove,
	.id_table = tp_device_id,
#if (TP_PLATFORM == PT_MTK)
	.detect = tpd_detect,
#endif /* PT_MTK */
};
#elif (INTERFACE == SPI_INTERFACE)
static struct spi_driver tp_spi_driver = {
	.driver = {
		.name	= DEVICE_ID,
		.owner = THIS_MODULE,
		.of_match_table = tp_match_table,
		.pm = &tp_pm_ops,
	},
	.probe = ilitek_platform_probe,
	.remove = ilitek_platform_remove,
};
#endif
#if (TP_PLATFORM == PT_MTK)
static int tpd_local_init(void)
{
	TPD_INFO("TPD init device driver\n");

	if (i2c_add_driver(&tp_i2c_driver) != 0) {
		TPD_INFO("Unable to add i2c driver\n");
		return -1;
	}
	if (tpd_load_status == 0) {
		TPD_INFO("Add error touch panel driver\n");

		i2c_del_driver(&tp_i2c_driver);
		return -1;
	}

	if (tpd_dts_data.use_tpd_button) {
		tpd_button_setting(tpd_dts_data.tpd_key_num, tpd_dts_data.tpd_key_local,
				   tpd_dts_data.tpd_key_dim_local);
	}

	tpd_type_cap = 1;

	return 0;
}

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = DEVICE_ID,
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
};
#endif /* PT_MTK */

static int ilitek_ftm_process(void *chip_data)
{
    int ret = -1;
    TPD_INFO("\n");
	ret = core_firmware_boot_host_download();
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

int ilitek_reset_for_esd(void *chip_data)
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
			host_download(true);
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
	//int ret = 0;
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

    if (g_mutual_data.points) {
        kfree(g_mutual_data.points);
        g_mutual_data.points = NULL;
    }

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
		mdelay(40);
		core_fr_handler();
        if (gesture_process_ws) {
            TPD_INFO("black gesture process wake unlock\n");
            __pm_relax(gesture_process_ws);
        }
        return IRQ_GESTURE;
    } else if (is_suspended) {
        return IRQ_IGNORE;
    }
	g_mutual_data.points = (struct point_info *)kcalloc(10, sizeof(struct point_info), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_mutual_data.points)) {
		TPD_INFO("Failed to allocate g_mutual_data.points memory, %ld\n", PTR_ERR(g_mutual_data.points));
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
				core_config_edge_limit_ctrl(flag);
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
	}
	else {
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
    ret = core_config_get_fw_ver();
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
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s panel_data->test_limit_name = %s\n", \
		chip_info->tp_type, panel_data->fw_name, panel_data->test_limit_name);

    return 0;
}

static void ilitek_black_screen_test(void *chip_data, char *message)
{
	TPD_INFO("enter %s\n", __func__);
	ilitek_mp_black_screen_test(message);
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
		ret = core_spi_check_header(buf, 1);
        /* update interrupt timer */
        chip_info->irq_timer = jiffies;
    }
out:
	mutex_unlock(&ipd->plat_mutex);
    if(ret == CHECK_RECOVER)
    {
    #ifdef CHECK_REG
		ret = core_config_ice_mode_enable();
		if (ret < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", ret);
		}
		//mdelay(20);
		
		core_get_tp_register();
		#ifdef CHECK_DDI_REG
		core_get_ddi_register();
		#endif
		core_config_ice_mode_disable();
		core_spi_rx_check_test();
	#endif
		tp_touch_btnkey_release();
        chip_info->esd_retry++;
        TPD_INFO("Recover esd_retry = %d\n", chip_info->esd_retry);
        ilitek_reset_for_esd((void *)ipd);
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
    //.get_usb_state              = ilitek_get_usb_state,
    .black_screen_test          = ilitek_black_screen_test,
    .esd_handle                 = ilitek_esd_handle,
    .set_touch_direction    	= ilitek_set_touch_direction,
    .get_touch_direction    	= ilitek_get_touch_direction,
};

static int __init ilitek_platform_init(void)
{
	int res = 0;

	TPD_INFO("TP driver init\n");

#if (TP_PLATFORM == PT_MTK)
	tpd_get_dts_info();
	res = tpd_driver_add(&tpd_device_driver);
	if (res < 0) {
		TPD_INFO("TPD add TP driver failed\n");
		tpd_driver_remove(&tpd_device_driver);
		return -ENODEV;
	}
#elif (INTERFACE == I2C_INTERFACE)
	TPD_INFO("TP driver add i2c interface\n");
	res = i2c_add_driver(&tp_i2c_driver);
	if (res < 0) {
		TPD_INFO("Failed to add i2c driver\n");
		i2c_del_driver(&tp_i2c_driver);
		return -ENODEV;
	}
#elif(INTERFACE == SPI_INTERFACE)
	TPD_INFO("TP driver add spi interface\n");
	if (!tp_judge_ic_match(TPD_DEVICE)) {
		TPD_INFO("TP driver is already register\n");
		return -1;
	}
	res = spi_register_driver(&tp_spi_driver);
	if (res < 0) {
		TPD_INFO("Failed to add spi driver\n");
		return -ENODEV;
	}
#endif /* PT_MTK */

	TPD_INFO("Succeed to add driver\n");
	return res;
}

static void __exit ilitek_platform_exit(void)
{
	TPD_INFO("I2C driver has been removed\n");

#if (TP_PLATFORM == PT_MTK)
	tpd_driver_remove(&tpd_device_driver);
#else
#if(INTERFACE == I2C_INTERFACE)
	i2c_del_driver(&tp_i2c_driver);
#elif(INTERFACE == SPI_INTERFACE)
	spi_unregister_driver(&tp_spi_driver);
#endif
#endif
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
