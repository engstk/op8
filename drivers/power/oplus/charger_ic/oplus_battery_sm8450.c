// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt)	"BATTERY_CHG: %s: " fmt, __func__

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include "oplus_battery_sm8450.h"
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_short.h"
#include "../oplus_wireless.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../oplus_adapter.h"
#include "../oplus_configfs.h"
//#include "../gauge_ic/oplus_bq27541.h"
#include "oplus_da9313.h"
#include "op_charge.h"
#include "../wireless_ic/oplus_nu1619.h"
#include "../oplus_debug_info.h"
#include "oplus_mp2650.h"
#include "../oplus_chg_ops_manager.h"
#include "../oplus_chg_module.h"
#include "../oplus_chg_comm.h"
#include "../voocphy/oplus_adsp_voocphy.h"

#define OPLUS_HVDCP_DISABLE_INTERVAL round_jiffies_relative(msecs_to_jiffies(15000))
#define OPLUS_HVDCP_DETECT_TO_DETACH_TIME 100
#define OEM_MISC_CTL_DATA_PAIR(cmd, enable) ((enable ? 0x3 : 0x1) << cmd)
#define FLASH_SCREEN_CTRL_OTA		0X01
#define FLASH_SCREEN_CTRL_DTSI	0X02

struct oplus_chg_chip *g_oplus_chip = NULL;
static struct task_struct *oplus_usbtemp_kthread;
struct wakeup_source *usbtemp_wakelock;
static bool probe_done;
static bool usb_online;
int qpnp_get_prop_charger_voltage_now(void);
bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_adsp_voocphy_enable(bool enable);
int oplus_adsp_voocphy_reset_again(void);
static void oplus_adsp_voocphy_status_func(struct work_struct *work);
int oplus_get_otg_online_status(void);
int oplus_set_otg_switch_status(bool enable);
static int oplus_otg_ap_enable(bool enable);
int oplus_chg_get_charger_subtype(void);
static int oplus_get_vchg_trig_status(void);
static bool oplus_vchg_trig_is_support(void);
void oplus_wake_up_usbtemp_thread(void);
bool oplus_chg_is_usb_present(void);
static int oplus_usbtemp_adc_gpio_dt(struct oplus_chg_chip *chip);
int oplus_get_usb_status(void);
int oplus_adsp_voocphy_get_enable(void);
static void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);
static int smbchg_usb_suspend_enable(void);
static void oplus_set_otg_boost_en_val(int value);
static void oplus_set_otg_ovp_en_val(int value);
extern void oplus_dwc3_config_usbphy_pfunc(bool (*pfunc)(void));
extern void oplus_usb_set_none_role(void);

//extern void oplus_usb_set_none_role(void);
#if defined(OPLUS_FEATURE_POWERINFO_FTM) && defined(CONFIG_OPLUS_POWERINFO_FTM)
extern bool ext_boot_with_console(void);
#endif
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
/*for p922x compile*/
void __attribute__((weak)) oplus_set_wrx_otg_value(void)
{
	return;
}
int __attribute__((weak)) oplus_get_idt_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_otg_val(void)
{
	return 0;
}
void __attribute__((weak)) oplus_wireless_set_otg_en_val(void)
{
	return;
}
void __attribute__((weak)) oplus_dcin_irq_enable(void)
{
	return;
}

__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->wls_ocm)
		chip->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!chip->wls_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->comm_ocm)
		chip->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!chip->comm_ocm;
}

#endif /*OPLUS_FEATURE_CHG_BASIC*/

static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
	[USB_TEMP]		= POWER_SUPPLY_PROP_TEMP,
};

static const int wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
};

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PD_PPS", "BrickID"
};

/* Custom usb_type definitions */
static const char * const qc_power_supply_usb_type_text[] = {
	"HVDCP", "HVDCP_3", "HVDCP_3P5"
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static bool is_ext_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "plat-pmic", 64));
}

static int oem_battery_chg_write(struct battery_chg_dev *bcdev, void *data,
	int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_err("glink state is down\n");
		return -ENOTCONN;
	}

	mutex_lock(&bcdev->read_buffer_lock);
	reinit_completion(&bcdev->oem_read_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->oem_read_ack,
			msecs_to_jiffies(OEM_READ_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->read_buffer_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	/*pr_err("oem_battery_chg_write end\n");*/
	mutex_unlock(&bcdev->read_buffer_lock);

	return rc;
}

static int oem_read_buffer(struct battery_chg_dev *bcdev)
{
	struct oem_read_buffer_req_msg req_msg = { { 0 } };

	req_msg.data_size = sizeof(bcdev->read_buffer_dump.data_buffer);
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_READ_BUFFER;

	/*pr_err("oem_read_buffer\n");*/

	return oem_battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

void oplus_get_props_from_adsp_by_buffer(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, oplus_get_batt_argv_buffer\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	oem_read_buffer(bcdev);
}

static void handle_oem_read_buffer(struct battery_chg_dev *bcdev,
	struct oem_read_buffer_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;

	/*pr_err("correct length received: %zu expected: %u\n", len,
		sizeof(bcdev->read_buffer_dump));*/

	if (len > sizeof(bcdev->read_buffer_dump)) {
		pr_err("Incorrect length received: %zu expected: %u\n", len,
		sizeof(bcdev->read_buffer_dump));
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->read_buffer_dump.data_buffer)) {
		pr_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	/*pr_err("buf length: %u\n", buf_len);*/
	if (buf_len == 0) {
		pr_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);

	printk(KERN_ERR "%s : ----temp[%d], current[%d], vol[%d], soc[%d], rm[%d], chg_cyc[%d], fcc[%d], cc[%d], soh[%d], \
		suspend[%d], oplus_UsbCommCapable[%d], oplus_pd_svooc[%d]", __func__,
		bcdev->read_buffer_dump.data_buffer[0], bcdev->read_buffer_dump.data_buffer[1], bcdev->read_buffer_dump.data_buffer[2],
		bcdev->read_buffer_dump.data_buffer[3], bcdev->read_buffer_dump.data_buffer[4], bcdev->read_buffer_dump.data_buffer[5],
		bcdev->read_buffer_dump.data_buffer[6], bcdev->read_buffer_dump.data_buffer[7], bcdev->read_buffer_dump.data_buffer[8],
		bcdev->read_buffer_dump.data_buffer[9], bcdev->read_buffer_dump.data_buffer[10], bcdev->read_buffer_dump.data_buffer[11]);
	complete(&bcdev->oem_read_ack);
}
#endif

static int battery_chg_fw_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return -ENOTCONN;
	}

	reinit_completion(&bcdev->fw_buf_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->fw_buf_ack,
					msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	return rc;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);

	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
		req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int get_property_id(struct psy_state *pst,
			enum power_supply_property prop)
{
	u32 i;

	for (i = 0; i < pst->prop_count; i++)
		if (pst->map[i] == prop)
			return i;

	pr_err("No property id for property %d in psy %s\n", prop,
		pst->psy->desc->name);

	return -ENOENT;
}

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_NOTIFY;
	req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		pr_err("Failed to enable notification rc=%d\n", rc);
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static void oplus_ccdetect_happened_to_adsp(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_CCDETECT_HAPPENED, 1);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: write ccdetect plugout fail, rc[%d]\n", __func__, rc);
		return;
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]:write ccdetect plugout success, rc[%d]\n", __func__, rc);
	}
}

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
static int get_otg_scheme(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = chip->pmic_spmi.bcdev_chip;
	int otg_scheme = bcdev->otg_scheme;

	otg_scheme = OTG_SCHEME_CID;
	chg_err("otg_scheme: %d.\n", otg_scheme);
	return otg_scheme;
}

void oplus_ccdetect_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: 111 Couldn't read 0x2b44 rc=%d\n", __func__, rc);
		return;
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]:111 reg0x2b44[0x%x], bit[2:0]=0(DRP)\n", __func__, pst->prop[USB_TYPEC_MODE]);
	}

	/* set DRP mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, TYPEC_PORT_ROLE_DRP);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: Couldn't clear 0x2b44[0] rc=%d\n", __func__, rc);
	}

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: 111 Couldn't read 0x2b44 rc=%d\n", __func__, rc);
		return;
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]:111 reg0x2b44[0x%x], bit[2:0]=0(DRP)\n", __func__, pst->prop[USB_TYPEC_MODE]);
	}
}

void oplus_typec_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	/* set disable typec mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, TYPEC_PORT_ROLE_DISABLE);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: Couldn't write 0x2b44[3] rc=%d\n", __func__, rc);
	}
}

void oplus_ccdetect_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set UFP mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, TYPEC_PORT_ROLE_SNK);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: Couldn't clear 0x2b44[0] rc=%d\n", __func__, rc);
	}

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: 111 Couldn't read 0x2b44 rc=%d\n", __func__, rc);
		return;
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]:111 reg0x2b44[0x%x], bit[2:0]=0(UFP)\n", __func__, pst->prop[USB_TYPEC_MODE]);
	}
}

#define CCDETECT_DELAY_MS	50
irqreturn_t oplus_ccdetect_change_handler(int irq, void *data)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	cancel_delayed_work_sync(&bcdev->ccdetect_work);

	printk(KERN_ERR "[OPLUS_CHG][%s]: !!!!handle!\n", __func__);
	schedule_delayed_work(&bcdev->ccdetect_work,
			msecs_to_jiffies(CCDETECT_DELAY_MS));
	return IRQ_HANDLED;
}

int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return -EINVAL;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	bcdev->oplus_custom_gpio.ccdetect_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.ccdetect_pinctrl)) {
		chg_err("get ccdetect ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.ccdetect_active = pinctrl_lookup_state(
		bcdev->oplus_custom_gpio.ccdetect_pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.ccdetect_sleep = pinctrl_lookup_state(
		bcdev->oplus_custom_gpio.ccdetect_pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}

	if (bcdev->oplus_custom_gpio.ccdetect_gpio > 0) {
		gpio_direction_input(bcdev->oplus_custom_gpio.ccdetect_gpio);
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.ccdetect_pinctrl,
		bcdev->oplus_custom_gpio.ccdetect_active);

	return 0;
}

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = NULL;
	int boot_mode = get_boot_mode();

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip ready!\n", __func__);
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	/* HW engineer requirement */
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY)
		return false;

	if (gpio_is_valid(bcdev->oplus_custom_gpio.ccdetect_gpio))
		return true;

	return false;
}

void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	bcdev->ccdetect_irq = gpio_to_irq(bcdev->oplus_custom_gpio.ccdetect_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev->ccdetect_irq[%d]!\n", __func__, bcdev->ccdetect_irq);
}
static void oplus_ccdetect_irq_register(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	ret = devm_request_threaded_irq(chip->dev, bcdev->ccdetect_irq,
			NULL, oplus_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
	printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(bcdev->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}

static void oplus_hvdcp_disable_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, hvdcp_disable_work.work);

	if (oplus_chg_is_usb_present() == false)
		bcdev->hvdcp_disable = false;
}

static void oplus_adsp_voocphy_status_func(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct psy_state *pst_batt = NULL;
	int rc = 0;
	int intval = 0;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, oplus_adsp_voocphy_status_func\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	pst_batt = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = read_property_id(bcdev, pst, USB_VOOCPHY_STATUS);
	if (rc < 0) {
		printk(KERN_ERR "!!![adsp_voocphy] read adsp voocphy status fail\n");
		return;
	}
	intval = pst->prop[USB_VOOCPHY_STATUS];

	oplus_adsp_voocphy_handle_status(pst_batt->psy, intval);
}

static void oplus_otg_init_status_func(struct work_struct *work)
{
	int count = 20;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip)
		return;

	while (count--) {
		if (is_wls_ocm_available(chip))
			break;

		msleep(500);
	}

	printk(KERN_ERR "!!!!oplus_otg_init_status_func, count[%d]\n", count);
	oplus_otg_ap_enable(true);
}

static void otg_notification_handler(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, otg_vbus_enable_work.work);

	if (bcdev == NULL) {
		pr_err("bcdev is null, return\n");
		return;
	}

	if (bcdev->otg_online) {
		if (bcdev->usb_ocm) {
			oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OTG_ENABLE);
			msleep(50);
		}
		oplus_set_otg_ovp_en_val(1);
		oplus_set_otg_boost_en_val(1);
	} else {
		if (bcdev->usb_ocm) {
			oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OTG_DISABLE);
			msleep(50);
		}
		oplus_set_otg_ovp_en_val(0);
		oplus_set_otg_boost_en_val(0);
	}
}

static void oplus_vbus_enable_adc_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;


	if (!chip || !chip->chg_ops) {
		pr_err("chip or chip->chg_ops NULL\n");
		return;
	}

	oplus_chg_disable_charge();
	oplus_chg_suspend_charger();
}
void oplus_turn_off_power_when_adsp_crash(void)
{
#if 0
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	bcdev->is_chargepd_ready = false;
	bcdev->pd_svooc = false;

	if (bcdev->otg_online == true) {
		bcdev->otg_online = false;
		oplus_wpc_set_booster_en_val(0);
		oplus_wpc_set_ext1_wired_otg_en_val(0);
		oplus_wpc_set_wrx_en_value(0);
	}
#endif
}
EXPORT_SYMBOL(oplus_turn_off_power_when_adsp_crash);

bool oplus_is_pd_svooc(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return false;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (!bcdev) {
		printk(KERN_ERR "!!!bcdev null\n");
		return false;
	}

	printk(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
	return bcdev->pd_svooc;
}
EXPORT_SYMBOL(oplus_is_pd_svooc);

void oplus_adsp_crash_recover_work(void)
{
#if 0
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	schedule_delayed_work(&bcdev->adsp_crash_recover_work, round_jiffies_relative(msecs_to_jiffies(1500)));
#endif
}
EXPORT_SYMBOL(oplus_adsp_crash_recover_work);

static void oplus_adsp_crash_recover_func(struct work_struct *work)
{
#if 0
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	oplus_adsp_voocphy_reset_status_when_crash_recover();
	chip->charger_type  = POWER_SUPPLY_TYPE_UNKNOWN;
	oplus_adsp_voocphy_enable(true);
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	oplus_chg_wake_update_work();
	schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(0)));
	schedule_delayed_work(&bcdev->check_charger_out_work, round_jiffies_relative(msecs_to_jiffies(3000)));
#endif
}

static void oplus_check_charger_out_func(struct work_struct *work)
{
	int chg_vol = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst_batt = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst_batt = &bcdev->psy_list[PSY_TYPE_BATTERY];

	chg_vol = oplus_chg_get_charger_voltage();
	if (chg_vol >= 0 && chg_vol < 2000) {
		oplus_adsp_voocphy_clear_status();
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
		printk(KERN_ERR, "charger out, chg_vol:%d\n", chg_vol);
	}
}

static void oplus_adsp_voocphy_enable_check_func(struct work_struct *work)
{
	int rc = 0;
	int voocphy_enable = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (chip->mmi_chg == 0 || chip->charger_exist == false
		|| chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		/*chg_err("is_mmi_chg no_charger_exist no_dcp_type\n");*/
		schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	voocphy_enable = oplus_adsp_voocphy_get_enable();
	if (voocphy_enable == 0) {
		chg_err("!!!need enable voocphy again\n");
		rc = oplus_adsp_voocphy_enable(true);
		schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(500)));
	} else {
		/*chg_err("!!!enable voocphy ok\n");*/
		schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
	}
}

#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
static void oplus_wait_wired_charge_on_work(struct work_struct *work)
{
	printk(KERN_ERR "[OPLUS_CHG][%s]<~WPC~> wait_wired_charge_on\n", __func__);
#if 0
	oplus_wpc_set_wrx_en_value(0);
	oplus_wpc_set_wls_pg_value(1);
	msleep(100);
	oplus_wpc_set_booster_en_val(1);
	oplus_wpc_set_ext2_wireless_otg_en_val(1);
	msleep(100);
	oplus_wpc_set_tx_start();
	return;
#endif
}

#if 0
static void oplus_switch_to_wired_charge(struct battery_chg_dev *bcdev)
{
	oplus_wpc_dis_wireless_chg(1);
	if (oplus_wpc_get_wireless_charge_start() == true) {
		/*oplus_wpc_dis_wireless_chg(1);*/
		oplus_wpc_set_vbat_en_val(0);
		msleep(100);
		oplus_wpc_set_wrx_en_value(0);
		oplus_wpc_set_wls_pg_value(1);
	}

	if (oplus_wpc_get_otg_charging()) {
		/*oplus_wpc_dis_wireless_chg(1);*/
		mp2650_wireless_set_mps_otg_en_val(0);
		oplus_wpc_set_wrx_otg_en_value(0);

		cancel_delayed_work_sync(&bcdev->wait_wired_charge_on);
		schedule_delayed_work(&bcdev->wait_wired_charge_on, msecs_to_jiffies(100));
	}
}
#endif

static void oplus_wait_wired_charge_off_work(struct work_struct *work)
{
#if 0
	printk(KERN_ERR "[OPLUS_CHG][%s]<~WPC~> wait_wired_charge_off\n", __func__);
	oplus_wpc_dis_wireless_chg(0);
	oplus_wpc_set_rtx_function_prepare();
	oplus_wpc_set_rtx_function(true);
	return;
#endif
}

#if 0
static void oplus_switch_from_wired_charge(struct battery_chg_dev *bcdev)
{
	if (oplus_wpc_get_otg_charging()) {
		oplus_wpc_set_booster_en_val(0);
		oplus_wpc_set_ext2_wireless_otg_en_val(0);
		oplus_wpc_set_wls_pg_value(0);
		cancel_delayed_work_sync(&bcdev->wait_wired_charge_off);
		schedule_delayed_work(&bcdev->wait_wired_charge_off, msecs_to_jiffies(100));
	} else {
		if (oplus_wpc_get_fw_updating() == false)
			oplus_wpc_dis_wireless_chg(0);
	}
}
#endif

bool oplus_get_wired_otg_online(void)
{
#if 0
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev) {
		if (bcdev->wls_fw_update == true)
			return false;
		return bcdev->otg_online;
	}
#endif
	return false;
}

bool oplus_get_wired_chg_present(void)
{
	if (oplus_get_wired_otg_online() == true)
		return false;
	if (oplus_vchg_trig_is_support() == true && oplus_get_vchg_trig_status() == 0)
		return true;
	return usb_online;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	atomic_set(&bcdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct power_supply *psy;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst;
	int rc = 0;

	if (prop_id >= BATTERY_CHARGER_PROP_MAX)
		return -EINVAL;

	if (strcmp(name, "battery") && strcmp(name, "usb") &&
	    strcmp(name, "wireless"))
		return -EINVAL;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return -ENODEV;

	bcdev = power_supply_get_drvdata(psy);
	if (!bcdev)
		return -ENODEV;

	power_supply_put(psy);

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static bool validate_message(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		pr_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
	struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
	case BC_SHUTDOWN_NOTIFY:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
			else
				pr_err("Wireless FW update not done %d\n",
					(int)fw_update_msg->fw_update_done);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static struct power_supply_desc usb_psy_desc;

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		pr_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		return;
	}

	/* Reset usb_icl_ua whenever USB adapter type changes */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD)
		bcdev->usb_icl_ua = 0;

	pr_debug("usb_adap_type: %u\n", pst->prop[USB_ADAP_TYPE]);

	switch (pst->prop[USB_ADAP_TYPE]) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
#ifndef OPLUS_FEATURE_CHG_BASIC
		rc = read_property_id(bcdev, pst, USB_ONLINE);
		if (rc < 0) {
			pr_err("Failed to read USB_ONLINE rc=%d\n", rc);
			return;
		}
		if (pst->prop[USB_ONLINE] == 0) {
			/*pr_err("lizhijie USB_ONLINE 00000\n");*/
			usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		}
#else
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
#endif
		break;
	}
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct psy_state *pst = NULL;

	if (len != sizeof(*notify_msg)) {
		pr_err("Incorrect response length %zu\n", len);
		return;
	}

	pr_debug("notification: %#x\n", notify_msg->notification);

	switch (notify_msg->notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
#ifdef OPLUS_FEATURE_CHG_BASIC
	case BC_PD_SVOOC:
		/*if ((g_oplus_chip && g_oplus_chip->wireless_support == false)
			|| oplus_get_wired_chg_present() == true) {*/
			printk(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
			//oplus_usb_set_none_role();
			bcdev->pd_svooc = true;
		/*}*/
		printk(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
		break;
	case BC_VOOC_STATUS_GET:
		schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
		break;
	case BC_OTG_ENABLE:
		printk(KERN_ERR "!!!!!enable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_OTG_DISABLE:
		printk(KERN_ERR "!!!!!disable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		printk(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
		bcdev->adsp_voocphy_err_check = true;
		cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
		schedule_delayed_work(&bcdev->adsp_voocphy_err_work, msecs_to_jiffies(8500));
		if (is_ext_chg_ops()) {
			oplus_chg_disable_charge();
			oplus_chg_suspend_charger();/*excute in glink loop for real time*/
		} else {
			schedule_delayed_work(&bcdev->vbus_adc_enable_work, 0);/*excute in work to avoid glink dead loop*/
		}
		break;
	case BC_CID_DETECT:
		printk(KERN_ERR "!!!!!cid detect || no detect\n");
		schedule_delayed_work(&bcdev->cid_status_change_work, 0);
		break;
	case BC_QC_DETECT:
		bcdev->hvdcp_detect_ok = true;
		bcdev->hvdcp_detect_time = cpu_clock(smp_processor_id()) / 1000000;
		printk(KERN_ERR " HVDCP2 detect: %d, the detect time: %lu\n",
			bcdev->hvdcp_detect_ok, bcdev->hvdcp_detect_time);
		break;
	case BC_TYPEC_STATE_CHANGE:
		schedule_delayed_work(&bcdev->typec_state_change_work, 0);
		break;
#endif
	default:
		break;
	}

	if (pst && pst->psy) {
		/*
		 * For charger mode, keep the device awake at least for 50 ms
		 * so that device won't enter suspend when a non-SDP charger
		 * is removed. This would allow the userspace process like
		 * "charger" to be able to read power supply uevents to take
		 * appropriate actions (e.g. shutting down when the charger is
		 * unplugged).
		 */
		power_supply_changed(pst->psy);
		pm_wakeup_dev_event(bcdev->dev, 50, true);
	}
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	if (!bcdev->initialized) {
		pr_debug("Driver initialization failed: Dropping glink callback message: state %d\n",
			 bcdev->state);
		return 0;
	}

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
#ifdef OPLUS_FEATURE_CHG_BASIC
	else if (hdr->opcode == OEM_OPCODE_READ_BUFFER)
		handle_oem_read_buffer(bcdev, data, len);
#endif
	else
		handle_message(bcdev, data, len);

	return 0;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
#ifndef OPLUS_FEATURE_CHG_BASIC
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];
#else /*OPLUS_FEATURE_CHG_BASIC*/
	struct oplus_chg_chip *chip = g_oplus_chip;
	union oplus_chg_mod_propval temp_val = {0};
	int rc = 0;

	if (!chip || !is_wls_ocm_available(chip)) {
		pr_err("wireless mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_ONLINE_KEEP, &temp_val);
		if (rc < 0)
			temp_val.intval = 0;
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		if (!rc && !!temp_val.intval)
			pval->intval = 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		break;
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
#ifdef OPLUS_FEATURE_CHG_BASIC
	POWER_SUPPLY_PROP_PRESENT,
#endif
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

static const char *get_usb_type_name(u32 usb_type)
{
	u32 i;

	if (usb_type >= QTI_POWER_SUPPLY_USB_TYPE_HVDCP &&
	    usb_type <= QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5) {
		for (i = 0; i < ARRAY_SIZE(qc_power_supply_usb_type_text);
		     i++) {
			if (i == (usb_type - QTI_POWER_SUPPLY_USB_TYPE_HVDCP))
				return qc_power_supply_usb_type_text[i];
		}
		return "Unknown";
	}

	for (i = 0; i < ARRAY_SIZE(power_supply_usb_type_text); i++) {
		if (i == usb_type)
			return power_supply_usb_type_text[i];
	}

	return "Unknown";
}

#ifndef OPLUS_FEATURE_CHG_BASIC
static int usb_psy_set_icl(struct battery_chg_dev *bcdev, u32 prop_id, int val)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	u32 temp;
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		pr_err("Failed to read prop USB_ADAP_TYPE, rc=%d\n", rc);
		return rc;
	}

	/* Allow this only for SDP or USB_PD and not for other charger types */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD)
		return -EINVAL;

	/*
	 * Input current limit (ICL) can be set by different clients. E.g. USB
	 * driver can request for a current of 500/900 mA depending on the
	 * port type. Also, clients like EUD driver can pass 0 or -22 to
	 * suspend or unsuspend the input for its use case.
	 */

	temp = val;
	if (val < 0)
		temp = UINT_MAX;

	rc = write_property_id(bcdev, pst, prop_id, temp);
	if (rc < 0) {
		pr_err("Failed to set ICL (%u uA) rc=%d\n", temp, rc);
	} else {
		pr_debug("Set ICL to %u\n", temp);
		bcdev->usb_icl_ua = temp;
	}

	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_adsp_voocphy_set_current_level(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int cool_down = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	cool_down = oplus_chg_get_cool_down_status();
	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, cool_down);
	if (rc) {
		chg_err("set current level fail, rc=%d\n", rc);
		return;
	}

	chg_debug("ap set current level[%d] to adsp voocphy\n", cool_down);
}

void oplus_adsp_voocphy_set_match_temp(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int match_temp = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	match_temp = oplus_chg_match_temp_for_chging();
	rc = write_property_id(bcdev, pst, BATT_SET_MATCH_TEMP, match_temp);
	if (rc) {
		chg_err("set match temp fail, rc=%d\n", rc);
		return;
	}

	/*chg_debug("ap set match temp[%d] to voocphy\n", match_temp);*/
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;
#ifdef OPLUS_FEATURE_CHG_BASIC
	static int online = 0;
	static int adap_type = 0;
#endif

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];
	if (prop == POWER_SUPPLY_PROP_TEMP)
		pval->intval = DIV_ROUND_CLOSEST((int)pval->intval, 10);

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (prop_id == USB_ONLINE) {
		if (pval->intval == 2) {
			pval->intval = 1;
		}
		if (online ^ pval->intval) {
			online = pval->intval;
			usb_online = online;
			printk(KERN_ERR "!!!!! usb online: [%d]\n", online);
			oplus_chg_wake_update_work();
			if (online == 1) {
				if (g_oplus_chip && g_oplus_chip->usbtemp_wq_init_finished) {
					g_oplus_chip->usbtemp_check = true;
					oplus_wake_up_usbtemp_thread();
				}
				if (bcdev->usb_ocm)
					oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_ONLINE);
			} else {
				if (g_oplus_chip) {
					g_oplus_chip->usbtemp_check = false;
				}
				if (bcdev->usb_ocm)
					oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OFFLINE);
				bcdev->pd_svooc = false;
				bcdev->hvdcp_detach_time = cpu_clock(smp_processor_id()) / 1000000;
				printk(KERN_ERR "!!! %s: the hvdcp_detach_time:%lu\n", __func__, bcdev->hvdcp_detach_time);
				if (bcdev->hvdcp_detect_ok && (bcdev->hvdcp_detach_time - bcdev->hvdcp_detect_time
					<= OPLUS_HVDCP_DETECT_TO_DETACH_TIME)) {
					bcdev->hvdcp_disable = true;
					schedule_delayed_work(&bcdev->hvdcp_disable_work, OPLUS_HVDCP_DISABLE_INTERVAL);
				} else {
					bcdev->hvdcp_detect_ok = false;
					bcdev->hvdcp_detect_time = 0;
					bcdev->hvdcp_disable = false;
				}
				bcdev->adsp_voocphy_err_check = false;
				cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
			}
			printk(KERN_ERR "!!!pd_svooc[%d]\n", bcdev->pd_svooc);
		}
	}
	if (prop_id == USB_ADAP_TYPE) {
		if (adap_type ^ pval->intval) {
			adap_type = pval->intval;
			printk(KERN_ERR "!!! usb adap type: [%d]\n", adap_type);
			oplus_chg_wake_update_work();
		}
	}
#endif

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_get_typec_cc_orientation(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;
	int rc = 0;
	int typec_cc_orientation = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_TYPEC_CC_ORIENTATION);
	if (rc < 0) {
		printk(KERN_ERR "!!![OPLUS_CHG] read typec_cc_orientation fail\n");
		return 0;
	}
	typec_cc_orientation = pst->prop[USB_TYPEC_CC_ORIENTATION];
	printk(KERN_ERR "!!![OPLUS_CHG] typec_cc_orientation=%d\n", typec_cc_orientation);

	return typec_cc_orientation;
}

int oplus_get_otg_switch_status(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;
	int rc = 0;
	int otg_switch_status = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (get_otg_scheme(chip) != OTG_SCHEME_CCDETECT_GPIO) {
		rc = read_property_id(bcdev, pst, USB_OTG_SWITCH);
		if (rc < 0) {
			printk(KERN_ERR "!!![OPLUS_CHG] read otg_switch_status fail\n");
			return 0;
		}
		otg_switch_status = pst->prop[USB_OTG_SWITCH];
		chip->otg_switch = otg_switch_status;
	}

	printk(KERN_ERR "!!![OPLUS_CHG] otg_switch_status=%d\n", chip->otg_switch);
	return chip->otg_switch;
}

int oplus_get_fast_chg_type(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	union oplus_chg_mod_propval pval;
	int fast_chg_type = 0;
	int rc;

	fast_chg_type = oplus_vooc_get_fast_chg_type();
	if (fast_chg_type == 0) {
		fast_chg_type = oplus_chg_get_charger_subtype();
	}

	if (!chip) {
		chg_err("chip is NULL!\n");
		return fast_chg_type;
	}
	if (is_wls_ocm_available(chip) && (fast_chg_type == 0)) {
		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
		if (rc == 0) {
			if ((pval.intval == OPLUS_CHG_WLS_VOOC) ||
			    (pval.intval == OPLUS_CHG_WLS_SVOOC)) {
				rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_ADAPTER_TYPE, &pval);
				if (rc == 0)
					fast_chg_type = pval.intval;
			}
		}
	}

	return fast_chg_type;
}
#endif

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#ifndef OPLUS_FEATURE_CHG_BASIC
		rc = usb_psy_set_icl(bcdev, prop_id, pval->intval);
#endif
		break;
	default:
		break;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

static int __battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					u32 fcc_ua)
{
	int rc;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (rc < 0)
		pr_err("Failed to set FCC %u, rc=%d\n", fcc_ua, rc);
	else
		pr_debug("Set FCC to %u uA\n", fcc_ua);

	return rc;
}

static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		pr_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];
	prev_fcc_ua = bcdev->thermal_fcc_ua;
	bcdev->thermal_fcc_ua = fcc_ua;

	rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
	if (!rc)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
#ifndef OPLUS_FEATURE_CHG_BASIC
	int prop_id, rc;
#else
	union oplus_chg_mod_propval temp_val = {0, };
	int batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	bool wls_online = false;
	int rc = 0;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *chip = g_oplus_chip;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
#endif

	pval->intval = -ENODATA;

#ifndef OPLUS_FEATURE_CHG_BASIC
	/*
	 * The prop id of TIME_TO_FULL_NOW and TIME_TO_FULL_AVG is same.
	 * So, map the prop id of TIME_TO_FULL_AVG for TIME_TO_FULL_NOW.
	 */
	if (prop == POWER_SUPPLY_PROP_TIME_TO_FULL_NOW)
		prop = POWER_SUPPLY_PROP_TIME_TO_FULL_AVG;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);
		if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) &&
		   (bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
			pval->intval = bcdev->fake_soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	default:
		pval->intval = pst->prop[prop_id];
		break;
	}
#else
	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		if (is_wls_ocm_available(chip)) {
			rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (rc == 0)
				wls_online = !!temp_val.intval;
			rc = 0;
		}
		if (wls_online && is_comm_ocm_available(chip)) {
			pval->intval = oplus_chg_comm_get_batt_status(chip->comm_ocm);
		} else {
			if (oplus_chg_show_vooc_logo_ornot() == 1) {
				if (chip->new_ui_warning_support
					&& (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && chip->batt_full))
					pval->intval = chip->prop_status;
				else
					pval->intval = POWER_SUPPLY_STATUS_CHARGING;
			} else if (!chip->authenticate) {
				pval->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			} else {
				pval->intval = chip->prop_status;
			}
		}
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (is_wls_ocm_available(chip)) {
			rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (rc == 0)
				wls_online = !!temp_val.intval;
			else
				rc = 0;
		}
		if (is_comm_ocm_available(chip) && wls_online)
			batt_health = oplus_chg_comm_get_batt_health(chip->comm_ocm);
		if ((batt_health == POWER_SUPPLY_HEALTH_UNKNOWN) ||
		    (batt_health == POWER_SUPPLY_HEALTH_GOOD)) {
			pval->intval = oplus_chg_get_prop_batt_health(chip);
		} else {
			pval->intval = batt_health;
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		pval->intval = chip->batt_exist;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		pval->intval = chip->charger_type;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if(chip->vooc_show_ui_soc_decimal == true && chip->decimal_control) {
			pval->intval = (chip->ui_soc_integer + chip->ui_soc_decimal)/1000;
		} else {
			pval->intval = chip->ui_soc;
		}
		if(pval->intval > 100) {
			pval->intval = 100;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		if (chip && (chip->ui_soc == 0)) {
			pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
			chg_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		pval->intval = chip->limits.charger_hv_thr;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		pval->intval = chip->batt_volt * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pval->intval = chip->limits.temp_normal_vfloat_mv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		pval->intval = chip->icharging;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = chip->tbatt_temp - chip->offset_temp;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		pval->intval = chip->vooc_project;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		pval->intval = chip->batt_rm * 1000;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		pval->intval = chip->charger_cycle;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		pval->intval = chip->batt_capacity_mah * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		pval->intval = 1800;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		pval->intval = 3600;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
	case POWER_SUPPLY_PROP_POWER_AVG:
		pval->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		pval->intval = chip->charger_volt;
		if (oplus_vooc_get_fastchg_started() == true && (chip->vbatt_num == 2)
				&& oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC) {
			pval->intval = 10000;
		}
		if (is_wls_ocm_available(chip)) {
			oplus_chg_mod_get_property(chip->wls_ocm,
				OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);
			if (!!temp_val.intval) {
				rc = oplus_chg_mod_get_property(chip->wls_ocm,
					OPLUS_CHG_PROP_TRX_VOLTAGE_NOW, &temp_val);
				pval->intval = temp_val.intval / 1000;
			}
		}
		rc = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		pval->intval = chip->batt_volt_min * 1000;
		rc = 0;
		break;
	default:
		pval->intval = 0;
		rc = 0;
		break;
	}
#endif

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
#ifdef OPLUS_FEATURE_CHG_BASIC
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (g_oplus_chip && g_oplus_chip->smart_charging_screenoff) {
			oplus_smart_charge_by_shell_temp(g_oplus_chip, pval->intval);
			break;
		} else {
			return  -EINVAL;
		}
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
#ifdef OPLUS_FEATURE_CHG_BASIC
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (g_oplus_chip && g_oplus_chip->smart_charging_screenoff) {
			return 1;
		} else {
			return 0;
		}
#endif
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

static int battery_chg_init_psy(struct battery_chg_dev *bcdev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = bcdev;
	psy_cfg.of_node = bcdev->dev->of_node;
	bcdev->psy_list[PSY_TYPE_BATTERY].psy =
		devm_power_supply_register(bcdev->dev, &batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy);
		pr_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_USB].psy =
		devm_power_supply_register(bcdev->dev, &usb_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_USB].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_USB].psy);
		pr_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_WLS].psy =
		devm_power_supply_register(bcdev->dev, &wls_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy);
		pr_err("Failed to register wireless power supply, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);
	int rc;

	battery_chg_notify_enable(bcdev);

	/*
	 * Give some time after enabling notification so that USB adapter type
	 * information can be obtained properly which is essential for setting
	 * USB ICL.
	 */
	msleep(200);

	if (bcdev->last_fcc_ua) {
		rc = __battery_psy_set_charge_current(bcdev,
				bcdev->last_fcc_ua);
		if (rc < 0)
			pr_err("Failed to set FCC (%u uA), rc=%d\n",
				bcdev->last_fcc_ua, rc);
	}

#ifndef OPLUS_FEATURE_CHG_BASIC
	if (bcdev->usb_icl_ua) {
		rc = usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT,
				bcdev->usb_icl_ua);
		if (rc < 0)
			pr_err("Failed to set ICL(%u uA), rc=%d\n",
				bcdev->usb_icl_ua, rc);
	}
#endif
}

static int wireless_fw_send_firmware(struct battery_chg_dev *bcdev,
					const struct firmware *fw)
{
	struct wireless_fw_push_buf_req msg = {};
	const u8 *ptr;
	u32 i, num_chunks, partial_chunk_size;
	int rc;

	num_chunks = fw->size / WLS_FW_BUF_SIZE;
	partial_chunk_size = fw->size % WLS_FW_BUF_SIZE;

	if (!num_chunks)
		return -EINVAL;

	pr_debug("Updating FW...\n");

	ptr = fw->data;
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_WLS_FW_PUSH_BUF_REQ;

	for (i = 0; i < num_chunks; i++, ptr += WLS_FW_BUF_SIZE) {
		msg.fw_chunk_id = i + 1;
		memcpy(msg.buf, ptr, WLS_FW_BUF_SIZE);

		pr_debug("sending FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	if (partial_chunk_size) {
		msg.fw_chunk_id = i + 1;
		memset(msg.buf, 0, WLS_FW_BUF_SIZE);
		memcpy(msg.buf, ptr, partial_chunk_size);

		pr_debug("sending partial FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wireless_fw_check_for_update(struct battery_chg_dev *bcdev,
					u32 version, size_t size)
{
	struct wireless_fw_check_req req_msg = {};

	bcdev->wls_fw_update_reqd = false;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_CHECK_UPDATE;
	req_msg.fw_version = version;
	req_msg.fw_size = size;
	req_msg.fw_crc = bcdev->wls_fw_crc;

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#define IDT_FW_MAJOR_VER_OFFSET		0x94
#define IDT_FW_MINOR_VER_OFFSET		0x96
static int wireless_fw_update(struct battery_chg_dev *bcdev, bool force)
{
	const struct firmware *fw;
	struct psy_state *pst;
	u32 version;
	u16 maj_ver, min_ver;
	int rc;

	pm_stay_awake(bcdev->dev);

	/*
	 * Check for USB presence. If nothing is connected, check whether
	 * battery SOC is at least 50% before allowing FW update.
	 */
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_ONLINE);
	if (rc < 0)
		goto out;

	if (!pst->prop[USB_ONLINE]) {
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_CAPACITY);
		if (rc < 0)
			goto out;

		if ((pst->prop[BATT_CAPACITY] / 100) < 50) {
			pr_err("Battery SOC should be at least 50%% or connect charger\n");
			rc = -EINVAL;
			goto out;
		}
	}

	rc = firmware_request_nowarn(&fw, bcdev->wls_fw_name, bcdev->dev);
	if (rc) {
		pr_err("Couldn't get firmware rc=%d\n", rc);
		goto out;
	}

	if (!fw || !fw->data || !fw->size) {
		pr_err("Invalid firmware\n");
		rc = -EINVAL;
		goto release_fw;
	}

	if (fw->size < SZ_16K) {
		pr_err("Invalid firmware size %zu\n", fw->size);
		rc = -EINVAL;
		goto release_fw;
	}

	maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MAJOR_VER_OFFSET));
	min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MINOR_VER_OFFSET));
	version = maj_ver << 16 | min_ver;

	if (force)
		version = UINT_MAX;

	pr_debug("FW size: %zu version: %#x\n", fw->size, version);

	rc = wireless_fw_check_for_update(bcdev, version, fw->size);
	if (rc < 0) {
		pr_err("Wireless FW update not needed, rc=%d\n", rc);
		goto release_fw;
	}

	if (!bcdev->wls_fw_update_reqd) {
		pr_warn("Wireless FW update not required\n");
		goto release_fw;
	}

	/* Wait for IDT to be setup by charger firmware */
	msleep(WLS_FW_PREPARE_TIME_MS);

	reinit_completion(&bcdev->fw_update_ack);
	rc = wireless_fw_send_firmware(bcdev, fw);
	if (rc < 0) {
		pr_err("Failed to send FW chunk, rc=%d\n", rc);
		goto release_fw;
	}

	rc = wait_for_completion_timeout(&bcdev->fw_update_ack,
				msecs_to_jiffies(WLS_FW_UPDATE_TIME_MS));
	if (!rc) {
		pr_err("Error, timed out updating firmware\n");
		rc = -ETIMEDOUT;
		goto release_fw;
	} else {
		rc = 0;
	}

	pr_info("Wireless FW update done\n");

release_fw:
	bcdev->wls_fw_crc = 0;
	release_firmware(fw);
out:
	pm_relax(bcdev->dev);

	return rc;
}

static ssize_t wireless_fw_crc_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	u16 val;

	if (kstrtou16(buf, 0, &val) || !val)
		return -EINVAL;

	bcdev->wls_fw_crc = val;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_crc);

static ssize_t wireless_fw_version_show(struct class *c,
					struct class_attribute *attr,
					char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct wireless_fw_get_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_GET_VERSION;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%#x\n", bcdev->wls_fw_version);
}
static CLASS_ATTR_RO(wireless_fw_version);

static ssize_t wireless_fw_force_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, true);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_force_update);

static ssize_t wireless_fw_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, false);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_update);

static ssize_t usb_typec_compliant_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_TYPEC_COMPLIANT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)pst->prop[USB_TYPEC_COMPLIANT]);
}
static CLASS_ATTR_RO(usb_typec_compliant);

static ssize_t usb_real_type_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_REAL_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			get_usb_type_name(pst->prop[USB_REAL_TYPE]));
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t restrict_cur_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (kstrtou32(buf, 0, &fcc_ua) || fcc_ua > bcdev->thermal_fcc_ua)
		return -EINVAL;

	prev_fcc_ua = bcdev->restrict_fcc_ua;
	bcdev->restrict_fcc_ua = fcc_ua;
	if (bcdev->restrict_chg_en) {
		rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
		if (rc < 0) {
			bcdev->restrict_fcc_ua = prev_fcc_ua;
			return rc;
		}
	}

	return count;
}

static ssize_t restrict_cur_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->restrict_fcc_ua);
}
static CLASS_ATTR_RW(restrict_cur);

static ssize_t restrict_chg_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	bcdev->restrict_chg_en = val;
	rc = __battery_psy_set_charge_current(bcdev, bcdev->restrict_chg_en ?
			bcdev->restrict_fcc_ua : bcdev->thermal_fcc_ua);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t restrict_chg_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->restrict_chg_en);
}
static CLASS_ATTR_RW(restrict_chg);

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	bcdev->fake_soc = val;
	pr_debug("Set fake soc to %d\n", val);

	if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) && pst->psy)
		power_supply_changed(pst->psy);

	return count;
}

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->fake_soc);
}
static CLASS_ATTR_RW(fake_soc);

static ssize_t wireless_boost_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
				WLS_BOOST_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_boost_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_BOOST_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_BOOST_EN]);
}
static CLASS_ATTR_RW(wireless_boost_en);

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static ssize_t resistance_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[BATT_RESISTANCE]);
}
static CLASS_ATTR_RO(resistance);

static ssize_t soh_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SOH]);
}
static CLASS_ATTR_RO(soh);

static ssize_t ship_mode_en_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->ship_mode_en))
		return -EINVAL;

	return count;
}

static ssize_t ship_mode_en_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ship_mode_en);
}
static CLASS_ATTR_RW(ship_mode_en);

static struct attribute *battery_class_attrs[] = {
	&class_attr_soh.attr,
	&class_attr_resistance.attr,
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	&class_attr_wireless_boost_en.attr,
	&class_attr_fake_soc.attr,
	&class_attr_wireless_fw_update.attr,
	&class_attr_wireless_fw_force_update.attr,
	&class_attr_wireless_fw_version.attr,
	&class_attr_wireless_fw_crc.attr,
	&class_attr_ship_mode_en.attr,
	&class_attr_restrict_chg.attr,
	&class_attr_restrict_cur.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_usb_typec_compliant.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir, *file;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		pr_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	file = debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		pr_err("Failed to create block_tx debugfs file, rc=%d\n",
			rc);
		goto error;
	}

	bcdev->debugfs_dir = dir;

	return;
error:
	debugfs_remove_recursive(dir);
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_otg_boost_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.otg_boost_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl)) {
		chg_err("get otg_boost_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_boost_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl, "otg_booster_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_active)) {
		chg_err("get otg_boost_en_active\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_boost_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl, "otg_booster_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_sleep)) {
		chg_err("get otg_booster_en_sleep\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
		bcdev->oplus_custom_gpio.otg_boost_en_sleep);

	return 0;
}

static int oplus_otg_ovp_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl)) {
		chg_err("get otg_ovp_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_ovp_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl, "otg_ovp_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_active)) {
		chg_err("get otg_ovp_en_active\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_ovp_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl, "otg_ovp_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_sleep)) {
		chg_err("get otg_ovp_en_sleep\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
		bcdev->oplus_custom_gpio.otg_ovp_en_sleep);

	return 0;
}

static void oplus_set_otg_boost_en_val(int value)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev->oplus_custom_gpio.otg_boost_en_gpio <= 0) {
		chg_err("otg_boost_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_sleep)) {
		chg_err("otg_boost_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_sleep);
	}

	chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.otg_boost_en_gpio));
}

static void oplus_set_otg_ovp_en_val(int value)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev->oplus_custom_gpio.otg_ovp_en_gpio <= 0) {
		chg_err("otg_ovp_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_sleep)) {
		chg_err("otg_ovp_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_sleep);
	}

	chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.otg_ovp_en_gpio));
}

static bool is_support_tx_boost(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(bcdev->oplus_custom_gpio.tx_boost_en_gpio))
		return true;
	return false;
}

static int oplus_tx_boost_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.tx_boost_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl)) {
		chg_err("get tx_boost_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.tx_boost_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl, "tx_boost_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_active)) {
		chg_err("get tx_boost_en_active fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.tx_boost_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl, "tx_boost_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_sleep)) {
		chg_err("get tx_boost_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(bcdev->oplus_custom_gpio.tx_boost_en_gpio, 0);
	pinctrl_select_state(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl,
		bcdev->oplus_custom_gpio.tx_boost_en_sleep);

	return 0;
}

static void oplus_set_tx_boost_en_val(int value)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev->oplus_custom_gpio.tx_boost_en_gpio <= 0) {
		chg_err("tx_boost_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_boost_en_sleep)) {
		chg_err("otg_boost_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.tx_boost_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.tx_boost_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.tx_boost_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.tx_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.tx_boost_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.tx_boost_en_gpio));
}

static int oplus_tx_ovp_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl)) {
		chg_err("get tx_ovp_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.tx_ovp_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl, "tx_ovp_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_active)) {
		chg_err("get tx_ovp_en_active fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.tx_ovp_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl, "tx_ovp_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_sleep)) {
		chg_err("get tx_ovp_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(bcdev->oplus_custom_gpio.tx_ovp_en_gpio, 0);
	pinctrl_select_state(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl,
		bcdev->oplus_custom_gpio.tx_ovp_en_sleep);

	return 0;
}

static void oplus_set_tx_ovp_en_val(int value)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev->oplus_custom_gpio.tx_ovp_en_gpio <= 0) {
		chg_err("tx_ovp_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.tx_ovp_en_sleep)) {
		chg_err("otg_ovp_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.tx_ovp_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.tx_ovp_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.tx_ovp_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.tx_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.tx_ovp_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.tx_ovp_en_gpio));
}

static int oplus_chg_2uart_pinctrl_init(struct oplus_chg_chip *chip)
{
	struct pinctrl			*chg_2uart_pinctrl;
	struct pinctrl_state	*chg_2uart_active;
	struct pinctrl_state	*chg_2uart_sleep;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}

	chg_2uart_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg_2uart_pinctrl)) {
		chg_err("get 2uart chg_2uart_pinctrl fail\n");
		return -EINVAL;
	}

	chg_2uart_active = pinctrl_lookup_state(chg_2uart_pinctrl, "chg_qupv3_se3_2uart_active");
	if (IS_ERR_OR_NULL(chg_2uart_active)) {
		chg_err("get chg_qupv3_se3_2uart_active fail\n");
		return -EINVAL;
	}

	chg_2uart_sleep = pinctrl_lookup_state(chg_2uart_pinctrl, "chg_qupv3_se3_2uart_sleep");
	if (IS_ERR_OR_NULL(chg_2uart_sleep)) {
		chg_err("get chg_qupv3_se3_2uart_sleep fail\n");
		return -EINVAL;
	}

#if defined(OPLUS_FEATURE_POWERINFO_FTM) && defined(CONFIG_OPLUS_POWERINFO_FTM)
	if (!ext_boot_with_console()) {
		chg_err("set chg_qupv3_se3_2uart_sleep\n");
		pinctrl_select_state(chg_2uart_pinctrl, chg_2uart_sleep);
	}
#endif

	return 0;
}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static bool oplus_vchg_trig_is_support(void)
{
#if 0
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0)
		return false;

	if (get_PCB_Version() >= EVT1)
		return true;
#endif
	return false;
}

static int oplus_vchg_trig_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.vchg_trig_pinctrl = devm_pinctrl_get(bcdev->dev);

	bcdev->oplus_custom_gpio.vchg_trig_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl, "vchg_trig_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("get vchg_trig_default\n");
		return -EINVAL;
	}

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio > 0) {
		gpio_direction_input(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}
	pinctrl_select_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl,
		bcdev->oplus_custom_gpio.vchg_trig_default);

	chg_err("get vchg_trig_default level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio));
	return 0;
}

static int oplus_get_vchg_trig_gpio_val(void)
{
	int level = 1;
	static int pre_level = 1;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("vchg_trig_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_pinctrl)
			|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	level = gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	if (pre_level ^ level) {
		pre_level = level;
		chg_err("!!!!! vchg_trig gpio level[%d], wired[%d]\n", level, !level);
	}
	return level;
}

static int vchg_trig_status = -1;
static int oplus_get_vchg_trig_status(void)
{
	if (vchg_trig_status == -1) {
		vchg_trig_status = !!oplus_get_vchg_trig_gpio_val();
	}
	return vchg_trig_status;
}

static void oplus_vchg_trig_work(struct work_struct *work)
{
#if 0
	int level;
	static bool pre_otg = false;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	level = oplus_get_vchg_trig_gpio_val();
	vchg_trig_status = !!level;
	if (level == 0) {
		if (bcdev->otg_online == true) {
			pre_otg = true;
			return;
		}
		if (chip->wireless_support)
			oplus_switch_to_wired_charge(bcdev);
	} else {
		if (pre_otg == true) {
			pre_otg = false;
			return;
		}
		if (chip->wireless_support
			&& oplus_voocphy_get_fastchg_to_warm() == false
			&& oplus_voocphy_get_fastchg_to_normal() == false)
			oplus_switch_from_wired_charge(bcdev);
	}

	if (oplus_voocphy_get_fastchg_to_warm() == false
		&& oplus_voocphy_get_fastchg_to_normal() == false) {
		oplus_chg_wake_update_work();
	}
#endif
}

static void oplus_vchg_trig_irq_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return;
	}

	bcdev->vchg_trig_irq = gpio_to_irq(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: vchg_trig_irq[%d]!\n", __func__, bcdev->vchg_trig_irq);
}

#define VCHG_TRIG_DELAY_MS	50
irqreturn_t oplus_vchg_trig_change_handler(int irq, void *data)
{
	struct battery_chg_dev *bcdev = data;

	cancel_delayed_work_sync(&bcdev->vchg_trig_work);
	printk(KERN_ERR "[OPLUS_CHG][%s]: scheduling vchg_trig work!\n", __func__);
	schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(VCHG_TRIG_DELAY_MS));

	return IRQ_HANDLED;
}

static void oplus_vchg_trig_irq_register(struct battery_chg_dev *bcdev)
{
	int ret = 0;

	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return;
	}

	ret = devm_request_threaded_irq(bcdev->dev, bcdev->vchg_trig_irq,
			NULL, oplus_vchg_trig_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "vchg_trig_change", bcdev);
	if (ret < 0) {
		chg_err("Unable to request vchg_trig_change irq: %d\n", ret);
	}
	printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(bcdev->vchg_trig_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: vchg_trig_irq failed %d\n", ret);
	}
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
static void smbchg_enter_shipmode_pmic(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = write_property_id(bcdev, pst, BATT_SET_SHIP_MODE, 1);
	if (rc) {
		chg_err("set ship mode fail, rc=%d\n", rc);
		return;
	}
	chg_debug("power off after 15s\n");
}

static int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: smb2_chg not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	chip->normalchg_gpio.ship_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ship_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);

	return 0;
}

static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: smb2_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

#define PWM_COUNT	5
static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int i = 0;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_debug("select gpio control\n");

		mutex_lock(&chip->pmic_spmi.bcdev_chip->oplus_custom_gpio.pinctrl_mutex);
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
		}
		for (i = 0; i < PWM_COUNT; i++) {
			/*gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);*/
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
			mdelay(3);
			/*gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);*/
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
			mdelay(3);
		}

		mutex_unlock(&chip->pmic_spmi.bcdev_chip->oplus_custom_gpio.pinctrl_mutex);
		chg_debug("power off after 15s\n");
	} else {
		smbchg_enter_shipmode_pmic(chip);
	}
}

static int oplus_chg_parse_custom_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	node = bcdev->dev->of_node;

	chip->normalchg_gpio.ship_gpio =
			of_get_named_gpio(node, "qcom,ship-gpio", 0);
	if (chip->normalchg_gpio.ship_gpio <= 0) {
		chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
				rc, chip->normalchg_gpio.ship_gpio);
	} else {
		if (oplus_ship_check_is_gpio(chip) == true) {
			rc = gpio_request(chip->normalchg_gpio.ship_gpio, "ship-gpio");
			if (rc) {
				chg_err("unable to request ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			} else {
				oplus_ship_gpio_init(chip);
				if (rc)
					chg_err("unable to init ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
				else
					chg_err("init ship-gpio level[%d]\n", gpio_get_value(chip->normalchg_gpio.ship_gpio));
			}
		}
		chg_err("ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
	}

	bcdev->oplus_custom_gpio.vchg_trig_gpio =
		of_get_named_gpio(node, "qcom,vchg_trig-gpio", 0);
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("Couldn't read qcom,vchg_trig-gpio rc = %d, vchg_trig-gpio:%d\n",
					rc, bcdev->oplus_custom_gpio.vchg_trig_gpio);
	} else {
		if (oplus_vchg_trig_is_support() == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.vchg_trig_gpio, "vchg_trig-gpio");
			if (rc) {
				chg_err("unable to vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
			} else {
				rc = oplus_vchg_trig_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
				else
					oplus_vchg_trig_irq_init(bcdev);
			}
		}
		chg_err("vchg_trig-gpio:%d\n", bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}

	bcdev->oplus_custom_gpio.ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
       if (bcdev->oplus_custom_gpio.ccdetect_gpio <= 0) {
		chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
		rc, bcdev->oplus_custom_gpio.ccdetect_gpio);
       } else {
		if (oplus_ccdetect_check_is_gpio(chip) == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.ccdetect_gpio, "ccdetect-gpio");
			if (rc) {
				chg_err("unable to request ccdetect-gpio:%d\n", bcdev->oplus_custom_gpio.ccdetect_gpio);
			} else {
				rc = oplus_ccdetect_gpio_init(chip);
				if (rc)
					chg_err("unable to init ccdetect-gpio:%d\n", bcdev->oplus_custom_gpio.ccdetect_gpio);
				else
					oplus_ccdetect_irq_init(chip);
			}
		}
		chg_err("ccdetect-gpio:%d\n", bcdev->oplus_custom_gpio.ccdetect_gpio);
	}

	bcdev->oplus_custom_gpio.otg_boost_en_gpio =
			of_get_named_gpio(node, "qcom,otg-booster-en-gpio", 0);
	if (bcdev->oplus_custom_gpio.otg_boost_en_gpio <= 0) {
		chg_err("Couldn't read qcom,otg_booster-en-gpio rc = %d, qcom,otg-booster-en-gpio:%d\n",
				rc, bcdev->oplus_custom_gpio.otg_boost_en_gpio);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.otg_boost_en_gpio) == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.otg_boost_en_gpio, "otg-boost-en-gpio");
			if (rc) {
				chg_err("unable to request otg-boost-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_boost_en_gpio);
			} else {
				rc = oplus_otg_boost_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init otg-boost-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_boost_en_gpio);
				else
					chg_err("init otg-boost-en-gpio level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.otg_boost_en_gpio));
			}
		}
		chg_err("otg-boost-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_boost_en_gpio);
	}

	bcdev->oplus_custom_gpio.otg_ovp_en_gpio =
			of_get_named_gpio(node, "qcom,otg-ovp-en-gpio", 0);
	if (bcdev->oplus_custom_gpio.otg_ovp_en_gpio <= 0) {
		chg_err("Couldn't read qcom,otg-ovp-en-gpio rc = %d, qcom,otg-ovp-en-gpio:%d\n",
				rc, bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.otg_ovp_en_gpio) == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.otg_ovp_en_gpio, "otg-ovp-en-gpio");
			if (rc) {
				chg_err("unable to request otg-ovp-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
			} else {
				rc = oplus_otg_ovp_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init otg-ovp-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
				else
					chg_err("init otg-ovp-en-gpio level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.otg_ovp_en_gpio));
			}
		}
		chg_err("otg-ovp-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
	}

	bcdev->oplus_custom_gpio.tx_boost_en_gpio = of_get_named_gpio(node, "qcom,tx_boost_en-gpio", 0);
	if (bcdev->oplus_custom_gpio.tx_boost_en_gpio <= 0) {
		chg_err("Couldn't read qcom,tx_boost_en-gpio, rc = %d\n", rc);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.tx_boost_en_gpio)) {
			rc = gpio_request(bcdev->oplus_custom_gpio.tx_boost_en_gpio, "tx_boost_en-gpio");
			if (rc) {
				chg_err("unable to tx_boost_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_boost_en_gpio);
			} else {
				rc = oplus_tx_boost_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init tx_boost_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_boost_en_gpio);
				else
					chg_err("init tx_boost_en_gpio level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.tx_boost_en_gpio));
			}
		}
		chg_err("tx_boost_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_boost_en_gpio);
	}

	bcdev->oplus_custom_gpio.tx_ovp_en_gpio = of_get_named_gpio(node, "qcom,tx_ovp_en-gpio", 0);
	if (bcdev->oplus_custom_gpio.tx_ovp_en_gpio <= 0) {
		chg_err("Couldn't read qcom,tx_ovp_en-gpio, rc = %d\n", rc);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.tx_ovp_en_gpio)) {
			rc = gpio_request(bcdev->oplus_custom_gpio.tx_ovp_en_gpio, "tx_ovp_en-gpio");
			if (rc) {
				chg_err("unable to tx_ovp_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_ovp_en_gpio);
			} else {
				rc = oplus_tx_ovp_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init tx_ovp_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_ovp_en_gpio);
				else
					chg_err("init tx_ovp_en_gpio level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.tx_ovp_en_gpio));
			}
		}
		chg_err("tx_ovp_en-gpio:%d\n", bcdev->oplus_custom_gpio.tx_ovp_en_gpio);
	}

	rc = of_property_read_u32(node, "qcom,otg_scheme",
			&bcdev->otg_scheme);
	if (rc) {
		bcdev->otg_scheme = OTG_SCHEME_UNDEFINE;
	}

	return 0;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int i, rc, len;
	u32 prev, val;

#ifdef OPLUS_FEATURE_CHG_BASIC
	bcdev->otg_online = false;
	bcdev->pd_svooc = false;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_usbtemp_adc_gpio_dt(g_oplus_chip);
#endif
	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);

	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0)
		return 0;

	len = rc;

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0) {
		pr_err("Failed to read prop BATT_CHG_CTRL_LIM_MAX, rc=%d\n",
			rc);
		return rc;
	}

	prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	for (i = 0; i < len; i++) {
		rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
						i, &val);
		if (rc < 0)
			return rc;

		if (val > prev) {
			pr_err("Thermal levels should be in descending order\n");
			bcdev->num_thermal_levels = -EINVAL;
			return 0;
		}

		prev = val;
	}

	bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
					sizeof(*bcdev->thermal_levels),
					GFP_KERNEL);
	if (!bcdev->thermal_levels)
		return -ENOMEM;

	/*
	 * Element 0 is for normal charging current. Elements from index 1
	 * onwards is for thermal mitigation charging currents.
	 */

	bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
	if (rc < 0) {
		pr_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
		return rc;
	}

	bcdev->num_thermal_levels = len;
	bcdev->thermal_fcc_ua = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_notify_msg msg_notify = { { 0 } };
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	msg_notify.hdr.owner = MSG_OWNER_BC;
	msg_notify.hdr.type = MSG_TYPE_NOTIFY;
	msg_notify.hdr.opcode = BC_SHUTDOWN_NOTIFY;

	rc = battery_chg_write(bcdev, &msg_notify, sizeof(msg_notify));
	if (rc < 0)
		pr_err("Failed to send shutdown notification rc=%d\n", rc);

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

/**********************************************************************
 * battery charge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_usbtemp_iio_init(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names", "usb_temp_adc");
	if (rc >= 0) {
		bcdev->iio.usbtemp_v_chan = iio_channel_get(bcdev->dev,
					"usb_temp_adc");
		if (IS_ERR(bcdev->iio.usbtemp_v_chan)) {
			rc = PTR_ERR(bcdev->iio.usbtemp_v_chan);
			if (rc != -EPROBE_DEFER)
				dev_err(bcdev->dev, "usb_temp_adc  get  error, %ld\n",	rc);
				bcdev->iio.usbtemp_v_chan = NULL;
				return rc;
		}
		pr_err("[OPLUS_CHG] test bcdev->iio.usb_temp_adc \n");
	}
	pr_err("[OPLUS_CHG] test bcdev->iio.usb_temp_adc out here\n");

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names", "usb_supplementary_temp_adc");
	if (rc >= 0) {
		bcdev->iio.usbtemp_sup_v_chan = iio_channel_get(bcdev->dev,
					"usb_supplementary_temp_adc");
		if (IS_ERR(bcdev->iio.usbtemp_sup_v_chan)) {
			rc = PTR_ERR(bcdev->iio.usbtemp_sup_v_chan);
			if (rc != -EPROBE_DEFER)
				dev_err(bcdev->dev, "usb_supplementary_temp_adc  get error, %ld\n", rc);
				bcdev->iio.usbtemp_sup_v_chan = NULL;
				return rc;
		}
		pr_err("[OPLUS_CHG] test bcdev->iio.usb_supplementary_temp_adc\n");
	}
	pr_err("[OPLUS_CHG] test bcdev->iio.usb_supplementary_temp_adc out here\n");

	return rc;
}

static bool oplus_usbtemp_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

static bool oplus_usbtemp_check_is_support(void)
{
	if (get_eng_version() == AGING) {
		chg_err("AGING mode, disable usbtemp\n");
		return false;
	}

	if(oplus_usbtemp_check_is_gpio(g_oplus_chip) == true)
		return true;

	chg_err("dischg return false\n");

	return false;
}

#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950
void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	int usbtemp_volt = 0;
	struct battery_chg_dev *bcdev = NULL;
	static int usbtemp_volt_l_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	static int usbtemp_volt_r_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (IS_ERR_OR_NULL(bcdev->iio.usbtemp_v_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev->iio.usbtemp_v_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

	rc = iio_read_channel_processed(bcdev->iio.usbtemp_v_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

	usbtemp_volt = 18 * usbtemp_volt / 10000;
	if (usbtemp_volt > USBTEMP_DEFAULT_VOLT_VALUE_MV) {
		usbtemp_volt = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	}

	chip->usbtemp_volt_l = usbtemp_volt;
	usbtemp_volt_l_pre = usbtemp_volt;
usbtemp_next:
	usbtemp_volt = 0;
	if (IS_ERR_OR_NULL(bcdev->iio.usbtemp_sup_v_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chg->iio.usbtemp_sup_v_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

	rc = iio_read_channel_processed(bcdev->iio.usbtemp_sup_v_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

	usbtemp_volt = 18 * usbtemp_volt / 10000;
	if (usbtemp_volt > USBTEMP_DEFAULT_VOLT_VALUE_MV) {
		usbtemp_volt = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	}

	chip->usbtemp_volt_r = usbtemp_volt;
	usbtemp_volt_r_pre = usbtemp_volt;

	/*chg_err("usbtemp_volt_l:%d, usbtemp_volt_r:%d\n",chip->usbtemp_volt_l, chip->usbtemp_volt_r);*/
}

int oplus_get_usbtemp_volt_l(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip == NULL) {
		return 0;
	}

	return chip->usbtemp_volt_l;
}

int oplus_get_usbtemp_volt_r(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip == NULL) {
		return 0;
	}

	return chip->usbtemp_volt_r;
}

static int oplus_dischg_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("chip NULL\n");
		return EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

void oplus_set_usb_status(int status)
{
	if (g_oplus_chip)
		g_oplus_chip->usb_status = g_oplus_chip->usb_status | status;
}

void oplus_clear_usb_status(int status)
{
	if (g_oplus_chip)
		g_oplus_chip->usb_status = g_oplus_chip->usb_status & (~status);
}

int oplus_get_usb_status(void)
{
	if (g_oplus_chip)
		return g_oplus_chip->usb_status;
	return 0;
}

#define USB_20C 20
#define USB_40C	40
#define USB_30C 30
#define USB_50C	50
#define USB_55C	55
#define USB_57C	57
#define USB_100C 100

#define USB_50C_VOLT 467
#define USB_55C_VOLT 400
#define USB_57C_VOLT 376
#define USB_100C_VOLT 100
#define VBUS_VOLT_THRESHOLD	400

#define VBUS_MONITOR_INTERVAL	3000

#define MIN_MONITOR_INTERVAL	50
#define MAX_MONITOR_INTERVAL	50
#define RETRY_CNT_DELAY         5
#define HIGH_TEMP_SHORT_CHECK_TIMEOUT 1000
static void get_usb_temp(struct oplus_chg_chip *chg)
{
	int i = 0;

	for (i = ARRAY_SIZE(con_volt_855) - 1; i >= 0; i--) {
		if (con_volt_855[i] >= chg->usbtemp_volt_l)
			break;
		else if (i == 0)
			break;
	}

	chg->usb_temp_l = con_temp_855[i];

	for (i = ARRAY_SIZE(con_volt_855) - 1; i >= 0; i--) {
		if (con_volt_855[i] >= chg->usbtemp_volt_r)
			break;
		else if (i == 0)
			break;
	}

	chg->usb_temp_r = con_temp_855[i];
	/*chg_err("usb_temp_l:%d, usb_temp_r:%d\n",chg->usb_temp_l, chg->usb_temp_r);*/
}

static void oplus_set_usbtemp_wakelock(bool value)
{
	if(value) {
		__pm_stay_awake(usbtemp_wakelock);
	} else {
		__pm_relax(usbtemp_wakelock);
	}
}

static void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip)
{
	int level;
	int count_time = 0;
	struct battery_chg_dev *bcdev = NULL;
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio)){
		level = gpio_get_value(chip->normalchg_gpio.dischg_gpio);
	} else {
		return;
	}

	if (level == 1) {
		oplus_set_usbtemp_wakelock(true);
		do {
			oplus_get_usbtemp_volt(chip);
			get_usb_temp(chip);
			msleep(2000);
			count_time++;
		} while (!(((chip->usb_temp_r < USB_55C || chip->usb_temp_r == USB_100C)
			&& (chip->usb_temp_l < USB_55C ||  chip->usb_temp_l == USB_100C)) || count_time == 30));
		oplus_set_usbtemp_wakelock(false);
		if (count_time == 30) {
			pr_err("[OPLUS_USBTEMP] temp still high");
		} else {
			chip->dischg_flag = false;
			chg_err("dischg disable...[%d]\n", chip->usbtemp_volt);
			oplus_clear_usb_status(USB_TEMP_HIGH);

			mutex_lock(&(bcdev->oplus_custom_gpio.pinctrl_mutex));
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
			mutex_unlock(&(bcdev->oplus_custom_gpio.pinctrl_mutex));

			pr_err("[OPLUS_USBTEMP] usbtemp recover");
		}
	}
	return;
}

static void oplus_usbtemp_recover_work(struct work_struct *work)
{
	oplus_usbtemp_recover_func(g_oplus_chip);
}

static void oplus_typec_state_change_work(struct work_struct *work)
{
	int level = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (chip == NULL) {
		pr_err("[OPLUS_CCDETECT] chip null, return\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	level = gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio);

	printk(KERN_ERR "%s: !!! level[%d]\n", __func__, level);

	if(oplus_ccdetect_check_is_gpio(chip) == true) {
		if (level == 1 && oplus_get_otg_switch_status() == false)
			oplus_ccdetect_disable();
	}
}

static void oplus_ccdetect_work(struct work_struct *work)
{
	int level = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (chip == NULL) {
		pr_err("[OPLUS_CCDETECT] chip null, return\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	level = gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio);

	printk(KERN_ERR "%s: !!!level[%d]\n", __func__, level);
	if (level != 1) {
		oplus_ccdetect_enable();
		if (g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&bcdev->usbtemp_recover_work, 0);
		}
	} else {
		chip->usbtemp_check = false;
		if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&bcdev->usbtemp_recover_work, 0);
		}
		if (oplus_get_otg_switch_status() == false) {
			oplus_ccdetect_disable();
		}
	}
	oplus_ccdetect_happened_to_adsp();
}

static void oplus_cid_status_change_work(struct work_struct *work)
{
	int rc = 0;
	int cid_status = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;

	if (chip == NULL) {
		pr_err("[OPLUS_CCDETECT] chip null, return\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		printk(KERN_ERR "!!!%s, read cid_status fail\n", __func__);
		return;
	}

	cid_status = pst->prop[USB_CID_STATUS];
	printk(KERN_ERR "%s: !!!cid_status[%d]\n", __func__, cid_status);
	if (cid_status == 0) {
		chip->usbtemp_check = false;
	}

	if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		schedule_delayed_work(&bcdev->usbtemp_recover_work, 0);
	}
}

static int oplus_usbtemp_dischg_action(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;

	if (chip == NULL) {
		pr_err("[OPLUS_CCDETECT] chip null, return\n");
		return rc;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (get_eng_version() != HIGH_TEMP_AGING) {
		oplus_set_usb_status(USB_TEMP_HIGH);

		rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, false);
		if (rc < 0) {
			printk(KERN_ERR "!!![OPLUS_USBTEMP] write utemp high action fail\n");
			return rc;
		}

		usleep_range(10000, 10000);
		chip->chg_ops->charger_suspend();
		usleep_range(10000, 10000);

		rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, TYPEC_PORT_ROLE_SNK);
		if (rc < 0) {
			printk(KERN_ERR "!!![OPLUS_USBTEMP] write usb typec sinkonly fail\n");
			return rc;
		}
	}

	mutex_lock(&(bcdev->oplus_custom_gpio.pinctrl_mutex));
	if (get_eng_version() == HIGH_TEMP_AGING) {
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,do not set vbus down \n");
		rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
	} else {
		pr_err("[oplus_usbtemp_dischg_action]: set vbus down");
		rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);
	}
	mutex_unlock(&(bcdev->oplus_custom_gpio.pinctrl_mutex));

	return 0;
}

#define RETRY_COUNT		3
static void oplus_update_usbtemp_current_status(struct oplus_chg_chip *chip)
{
	static int limit_cur_cnt_r = 0;
	static int limit_cur_cnt_l = 0;
	static int recover_cur_cnt_r = 0;
	static int recover_cur_cnt_l = 0;

	if (!chip) {
		return;
	}

	if((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
		&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		return;
	}

	if ((chip->usb_temp_r  - chip->temperature/10) >= 12) {
		limit_cur_cnt_r++;
		if (limit_cur_cnt_r >= RETRY_COUNT) {
			limit_cur_cnt_r = RETRY_COUNT;
		}
		recover_cur_cnt_r = 0;
	} else if ((chip->usb_temp_r  - chip->temperature/10) <= 6)  {
		recover_cur_cnt_r++;
		if (recover_cur_cnt_r >= RETRY_COUNT) {
			recover_cur_cnt_r = RETRY_COUNT;
		}
		limit_cur_cnt_r = 0;
	}

	if ((chip->usb_temp_l  - chip->temperature/10) >= 12) {
		limit_cur_cnt_l++;
		if (limit_cur_cnt_l >= RETRY_COUNT) {
			limit_cur_cnt_l = RETRY_COUNT;
		}
		recover_cur_cnt_l = 0;
	} else if ((chip->usb_temp_l  - chip->temperature/10) <= 6)  {
		recover_cur_cnt_l++;
		if (recover_cur_cnt_l >= RETRY_COUNT) {
			recover_cur_cnt_l = RETRY_COUNT;
		}
		limit_cur_cnt_l = 0;
	}

	if ((RETRY_COUNT <= limit_cur_cnt_r || RETRY_COUNT <= limit_cur_cnt_l)
			&& (chip->smart_charge_user == SMART_CHARGE_USER_OTHER)) {
		chip->smart_charge_user = SMART_CHARGE_USER_USBTEMP;
		chip->cool_down_done = true;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	} else if ((RETRY_COUNT <= recover_cur_cnt_r &&  RETRY_COUNT <= recover_cur_cnt_l)
			&& (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	}

	return;
}

static int get_battery_mvolts_for_usbtemp_monitor(struct oplus_chg_chip *chip){

	if(!is_ext_chg_ops())
		return 5000;

	if(chip->chg_ops && chip->chg_ops->get_charger_volt)
		return chip->chg_ops->get_charger_volt();

	return 5000;
}

static int oplus_usbtemp_monitor_main(void *data)
{
	int delay = 0;
	int vbus_volt = 0;
	static int count = 0;
	static int total_count = 0;
	static int last_usb_temp_l = 25;
	static int current_temp_l = 25;
	static int last_usb_temp_r = 25;
	static int current_temp_r = 25;
	int retry_cnt = 3, i = 0;
	int count_r = 1, count_l = 1;
	bool condition1 = false;
	bool condition2 = false;
	struct oplus_chg_chip *chip = g_oplus_chip;
	static int log_count = 0;

	pr_err("[oplus_usbtemp_monitor_main]:run first!");

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->oplus_usbtemp_wq, chip->usbtemp_check == true);
		if(chip->dischg_flag == true) {
			goto dischg;
		}
		oplus_get_usbtemp_volt(chip);
		get_usb_temp(chip);
		if ((chip->usb_temp_l < USB_50C) && (chip->usb_temp_r < USB_50C)) {/*get vbus when usbtemp < 50C*/
			vbus_volt = get_battery_mvolts_for_usbtemp_monitor(chip);
		} else {
			vbus_volt = 0;
		}
		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = 30;
		}

		oplus_update_usbtemp_current_status(chip);

		if ((chip->usbtemp_volt_l < USB_50C) && (chip->usbtemp_volt_r < USB_50C) && (vbus_volt < VBUS_VOLT_THRESHOLD))
			delay = VBUS_MONITOR_INTERVAL;

		/*condition1  :the temp is higher than 57*/
		if (chip->tbatt_temp/10 <= USB_50C &&(((chip->usb_temp_l >= USB_57C) && (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= USB_57C) && (chip->usb_temp_r < USB_100C)))) {
			pr_err("in loop 1");
			for (i = 1; i < retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_get_usbtemp_volt(chip);
				get_usb_temp(chip);
				if (chip->usb_temp_r >= USB_57C && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= USB_57C && chip->usb_temp_l < USB_100C)
					count_l++;
				pr_err("countl : %d", count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
					chip->dischg_flag = true;
					condition1 = true;
					chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
				}
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
		if (chip->tbatt_temp/10 > USB_50C && (((chip->usb_temp_l >= chip->tbatt_temp/10 + 7) && (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->tbatt_temp/10 + 7) && (chip->usb_temp_r < USB_100C)))) {
			pr_err("in loop 1");
			for (i = 1; i <= retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_get_usbtemp_volt(chip);
				get_usb_temp(chip);
				if ((chip->usb_temp_r >= chip->tbatt_temp/10 + 7) && chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >= chip->tbatt_temp/10 + 7) && chip->usb_temp_l < USB_100C)
					count_l++;
				pr_err("countl : %d", count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
					chip->dischg_flag = true;
					condition1 = true;
					chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
				}
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
		if(condition1 == true) {
			pr_err("jump_to_dischg");
			goto dischg;
		}

		/*condition2  :the temp uprising to fast*/
		if ((((chip->usb_temp_l - chip->tbatt_temp/10) > chip->usbtemp_batttemp_gap) && (chip->usb_temp_l < USB_100C))
				|| (((chip->usb_temp_r - chip->tbatt_temp/10) > chip->usbtemp_batttemp_gap) && (chip->usb_temp_r < USB_100C))) {
			if (count == 0) {
				last_usb_temp_r = chip->usb_temp_r;
				last_usb_temp_l = chip->usb_temp_l;
			} else {
				current_temp_r = chip->usb_temp_r;
				current_temp_l = chip->usb_temp_l;
			}
			if (((current_temp_l - last_usb_temp_l) >= 3) || (current_temp_r - last_usb_temp_r) >= 3) {
				for (i = 1; i <= retry_cnt; i++) {
					mdelay(RETRY_CNT_DELAY);
					oplus_get_usbtemp_volt(chip);
					get_usb_temp(chip);
					if ((chip->usb_temp_r - last_usb_temp_r) >= 3 && chip->usb_temp_r < USB_100C)
						count_r++;
					if ((chip->usb_temp_l - last_usb_temp_l) >= 3 && chip->usb_temp_l < USB_100C)
						count_l++;
					pr_err("countl : %d,countr : %d", count_l, count_r);
				}
				current_temp_l = chip->usb_temp_l;
				current_temp_r = chip->usb_temp_r;
				if ((count_l >= retry_cnt &&  chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C)
						|| (count_r >= retry_cnt &&  chip->usb_temp_r > USB_30C  && chip->usb_temp_r < USB_100C))  {
					if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
						chip->dischg_flag = true;
						chg_err("dischg enable3...,current_temp_l=%d,last_usb_temp_l=%d,current_temp_r=%d,last_usb_temp_r =%d\n",
								current_temp_l, last_usb_temp_l, current_temp_r, last_usb_temp_r);
						condition2 = true;
					}
				}
				count_r = 1;
				count_l = 1;
			}
			count++;
			if (count > total_count)
				count = 0;
		} else {
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
	/*judge whether to go the action*/
	dischg:
		if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
				&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
			condition1 = false;
			condition2 = false;
			chip->dischg_flag = false;
		}
		if((condition1== true || condition2 == true) && chip->dischg_flag == true) {
			oplus_usbtemp_dischg_action(chip);
			condition1 = false;
			condition2 = false;
		}
		msleep(delay);
		log_count++;
		if (log_count == 40) {
			chg_err("==================usbtemp_volt_l[%d], usb_temp_l[%d], usbtemp_volt_r[%d], usb_temp_r[%d]\n",
					chip->usbtemp_volt_l, chip->usb_temp_l, chip->usbtemp_volt_r, chip->usb_temp_r);
			log_count = 0;
		}
	}

	return 0;
}

static void oplus_usbtemp_thread_init(void)
{
	oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_main, 0, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (g_oplus_chip == NULL) {
		chg_err("%s g_oplus_chip is null\n", __func__);
		return;
	}

	if (oplus_usbtemp_check_is_support() == true) {
		wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq);
	}
}

static int oplus_usbtemp_l_gpio_init(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return -EINVAL;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.usbtemp_l_gpio_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.usbtemp_l_gpio_pinctrl)) {
		chg_err("get usbtemp_l_gpio_pinctrl fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.usbtemp_l_gpio_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.usbtemp_l_gpio_pinctrl, "usbtemp_l_gpio_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.usbtemp_l_gpio_default)) {
		chg_err("set usbtemp_l_gpio_default error\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.usbtemp_l_gpio_pinctrl,
		bcdev->oplus_custom_gpio.usbtemp_l_gpio_default);

	return 0;
}

static int oplus_usbtemp_r_gpio_init(struct oplus_chg_chip *chip)
{
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip not ready!\n", __func__);
		return -EINVAL;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;

	if (!bcdev) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.usbtemp_r_gpio_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.usbtemp_r_gpio_pinctrl)) {
		chg_err("get usbtemp_r_gpio_pinctrl fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.usbtemp_r_gpio_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.usbtemp_r_gpio_pinctrl, "usbtemp_r_gpio_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.usbtemp_r_gpio_default)) {
		chg_err("set usbtemp_r_gpio_default error\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.usbtemp_r_gpio_pinctrl,
		bcdev->oplus_custom_gpio.usbtemp_r_gpio_default);

	return 0;
}

static int oplus_usbtemp_adc_gpio_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (!chip) {
		pr_err("chip is null\n");
		return -EINVAL;
	}

	node = chip->dev->of_node;
	if (!node) {
		pr_err("device tree node missing\n");
			return -EINVAL;
	}

	chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
	if (chip->normalchg_gpio.dischg_gpio <= 0) {
		chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, chip->normalchg_gpio.dischg_gpio);
	} else {
		if (oplus_usbtemp_check_is_support() == true) {
			if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio)) {
				rc = gpio_request(chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
				if (rc) {
					chg_err("unable to request dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
				} else {
					oplus_dischg_gpio_init(chip);
				}
			}
		}
		chg_err("dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
	}

	oplus_usbtemp_l_gpio_init(chip);
	oplus_usbtemp_r_gpio_init(chip);

	return rc;
}

static void dump_regs(void)
{
	return;
}

static int smbchg_kick_wdt(void)
{
	return 0;
}

static int smbchg_set_fastchg_current_raw(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set fcc to %d mA\n", current_ma);

	return rc;
}

static int smbchg_set_wls_boost_en(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_WLS];

	if (enable) {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 1);
	} else {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 0);
	}
	if (rc) {
		chg_err("set fcc to %d mA fail, rc=%d\n", enable, rc);
	} else {
		chg_err("set fcc to %d mA\n", enable);
	}
	return rc;
}

bool qpnp_get_prop_vbus_collapse_status(void)
{
	int rc = 0;
	bool collapse_status = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return false;
	}
	collapse_status = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_err("read usb vbus_collapse_status[%d]\n",
			collapse_status);
	return collapse_status;
}

static int oplus_chg_set_input_current_with_no_aicl(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set icl to %d mA\n", current_ma);

	return rc;
}

static int oplus_chg_set_wls_input_current(int current_ma)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(bcdev, pst, WLS_INPUT_CURR_LIMIT, current_ma * 1000);
	if (rc)
		chg_err("set wls input current to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set wls input current to %d mA\n", current_ma);

	return rc;
}

static void smbchg_set_aicl_point(int vol)
{
	/*do nothing*/
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static int oplus_chg_set_input_current(int current_ma)
{
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (chip->mmi_chg == 0) {
		chg_debug("mmi_chg, return\n");
		return rc;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);

	chg_debug("usb input max current limit=%d setting %02x\n", current_ma, i);
	if (chip->batt_volt > 4100) {
		aicl_point = 4550;
	} else {
		aicl_point = 4500;
	}

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		chg_debug("use 500 here\n");
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(130000, 131000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 6; /* 1750 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i =  i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_end:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_boost_back:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_return:
	return rc;
}

static int smbchg_float_voltage_set(int vfloat_mv)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_MAX);
	rc = write_property_id(bcdev, pst, prop_id, vfloat_mv);
	if (rc)
		chg_err("set fv to %d mV fail, rc=%d\n", vfloat_mv, rc);
	else
		chg_err("set fv to %d mV\n", vfloat_mv);

	return rc;
}

static int smbchg_term_current_set(int term_current)
{
	int rc = 0;
#if 0
	u8 val_raw = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(&chip->pmic_spmi.smb5_chip->chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
#endif
	return rc;
}

static int smbchg_charging_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 1);
	if (rc)
		chg_err("set enable charging fail, rc=%d\n", rc);
	else
		chg_err("set enable charging sucess.");

	return rc;
}

int oplus_get_charger_cycle(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 cycle_count = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		cycle_count =bcdev->read_buffer_dump.data_buffer[5];
		return cycle_count;
	}

	rc = read_property_id(bcdev, pst, BATT_CYCLE_COUNT);
	if (rc) {
		chg_err("set charger_cycle fail, rc=%d\n", rc);
		return rc;
	}

	chg_err("get charger_cycle[%d]\n", pst->prop[BATT_CYCLE_COUNT]);
	cycle_count = pst->prop[BATT_CYCLE_COUNT];

	return cycle_count;
}

int oplus_adsp_voocphy_get_enable()
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VOOCPHY_ENABLE);
	if (rc) {
		chg_err("get adsp voocphy enable fail, rc=%d\n", rc);
		return 0;
	} else {
		chg_err("get adsp voocphy enable success, rc=%d\n", rc);
	}

	return pst->prop[USB_VOOCPHY_ENABLE];
}

int oplus_adsp_voocphy_enable(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, enable);
	if (rc) {
		chg_err("set enable adsp voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("set enable adsp voocphy success, rc=%d\n", rc);
	}

	return rc;
}

static void oplus_suspend_check_work(struct work_struct *work)
{
#if 0
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	oplus_chg_unsuspend_plat_pmic(chip);
	rc = write_property_id(bcdev, pst, USB_SUSPEND_PMIC, true);
	if (rc) {
		chg_err("set usb_suspend_pmic fail, rc=%d\n", rc);
	} else {
		chg_err("set usb_suspend_pmic success, rc=%d\n", rc);
	}
#endif
}

void oplus_adsp_voocphy_cancle_err_check(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, oplus_adsp_voocphy_cancle_err_check\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (bcdev->adsp_voocphy_err_check == true) {
		cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
	}
	bcdev->adsp_voocphy_err_check = false;
}

static void oplus_adsp_voocphy_err_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, oplus_adsp_voocphy_err_work\n");
		return;
	}

	chg_err("!!!call\n");
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_voocphy_get_fastchg_ing() == false && bcdev->adsp_voocphy_err_check) {
		chg_err("!!!happend\n");
		bcdev->adsp_voocphy_err_check = false;
		oplus_chg_suspend_charger();
		usleep_range(1000000, 1000010);
		if (chip->mmi_chg) {
			oplus_chg_unsuspend_charger();
			chip->chg_ops->charging_enable();
			oplus_adsp_voocphy_reset_again();
		}
	}
}

int oplus_adsp_voocphy_reset_again(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_RESET_AGAIN, true);
	if (rc) {
		chg_err("set adsp voocphy reset again fail, rc=%d\n", rc);
	} else {
		chg_err("set adsp voocphy reset again success, rc=%d\n", rc);
	}

	return rc;
}


int oplus_set_otg_switch_status_default(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_SWITCH, enable);
	if (rc) {
		chg_err("oplus_set_otg_switch_status fail, rc=%d\n", rc);
	} else {
		chg_err("oplus_set_otg_switch_status, rc=%d\n", rc);
	}

	return rc;
}

int oplus_set_otg_switch_status(bool enable)
{
	int rc = 0;
	int level = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	/*boot-up with newman OTG connected, android will set persist.sys.oplus.otg_support, so...*/
	if (get_otg_scheme(chip) == OTG_SCHEME_CCDETECT_GPIO) {
		level = gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio);
		if (level != 1) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: gpio[%s], should set, return\n", __func__, level ? "H" : "L");
			return rc;
		}
	} else {
		rc = oplus_set_otg_switch_status_default(enable);
		return rc;
	}

	chip->otg_switch = !!enable;
	if (enable) {
		oplus_ccdetect_enable();
	} else {
		oplus_ccdetect_disable();
	}
	printk(KERN_ERR "[OPLUS_CHG][%s]: otg_switch=%d, otg_online=%d\n",
		__func__, chip->otg_switch, chip->otg_online);

	return rc;
}

#define DISCONNECT						0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT				BIT(1)
int oplus_get_otg_online_status_with_cid_scheme(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;
	int rc = 0;
	int cid_status = 0;
	int online = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		printk(KERN_ERR "!!!read cid_status fail\n");
		return 0;
	}
	cid_status = pst->prop[USB_CID_STATUS];

	online = (cid_status == 1) ? STANDARD_TYPEC_DEV_CONNECT : DISCONNECT;

	return online;
}

static int oplus_get_otg_online_with_switch_scheme(void)
{
	int online = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		printk(KERN_ERR "!!!%s: read typec_mode fail\n", __func__);
		return 0;
	}
	online = (pst->prop[USB_TYPEC_MODE] == 1) ? 1 : 0;

	return online;
}

int oplus_get_otg_online_status(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct psy_state *pst = NULL;
	int rc = 0;
	int cid_status = 0;
	int online = 0;
	int typec_otg = 0;
	static int pre_cid_status = 1;
	static int pre_level = 1;
	static int pre_typec_otg = 0;
	int level = 0, otg_scheme;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	otg_scheme = get_otg_scheme(chip);

	if (otg_scheme == OTG_SCHEME_CCDETECT_GPIO) {
		level = gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio);
		if (level != gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(5000, 5100);
			level = gpio_get_value(bcdev->oplus_custom_gpio.ccdetect_gpio);
		}
		online = (level == 1) ? DISCONNECT : STANDARD_TYPEC_DEV_CONNECT;
	} else if (otg_scheme == OTG_SCHEME_CID) {
		online = oplus_get_otg_online_status_with_cid_scheme();
	} else {
		online = oplus_get_otg_online_with_switch_scheme();
	}

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		printk(KERN_ERR "!!!%s: read typec_mode fail\n", __func__);
		return 0;
	}
	typec_otg = pst->prop[USB_TYPEC_MODE];

	online = online | ((typec_otg == 1) ? OTG_DEV_CONNECT : DISCONNECT);

	if ((pre_cid_status ^ cid_status) || (pre_typec_otg ^ typec_otg) || (pre_level ^ level)) {
		pre_cid_status = cid_status;
		pre_typec_otg = typec_otg;
		pre_level = level;
		printk(KERN_ERR "[OPLUS_CHG][%s]: level[%d], cid_status[%d], typec_otg[%d], otg_online[%d]\n",
				__func__, level, cid_status, typec_otg, online);
	}
	chip->otg_online = typec_otg;

	return online;
}

static int oplus_otg_ap_enable(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_AP_ENABLE, enable);
	if (rc) {
		chg_err("oplus_otg_ap_enable fail, rc=%d\n", rc);
	} else {
		chg_err("oplus_otg_ap_enable, rc=%d\n", rc);
	}

	return rc;
}

static int smbchg_charging_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 0);
	if (rc)
		chg_err("set disable charging fail, rc=%d\n", rc);
	else
		chg_err("set disable charging sucess.\n");

	return rc;
}

static int smbchg_get_charge_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_CHG_EN);
	if (rc) {
		chg_err("set disable charging fail, rc=%d\n", rc);
		return rc;
	}
	chg_err("get charge enable[%d]\n", pst->prop[BATT_CHG_EN]);

	return pst->prop[BATT_CHG_EN];
}

static int smbchg_usb_suspend_enable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, 0);
	if (rc)
		chg_err("set suspend fail, rc=%d\n", rc);
	else
		chg_err("set chg suspend\n");

	return rc;
}

static int smbchg_usb_suspend_disable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, 0xFFFFFFFF);
	if (rc)
		chg_err("set unsuspend to fail, rc=%d\n", rc);
	else
		chg_err("set chg unsuspend\n");

	return rc;
}

static int oplus_chg_hw_init(void)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
		smbchg_usb_suspend_disable();
	} else {
		smbchg_usb_suspend_enable();
	}

	oplus_chg_set_input_current_with_no_aicl(500);
	smbchg_charging_enable();

	return 0;
}

static int smbchg_set_rechg_vol(int rechg_vol)
{
	return 0;
}

static int smbchg_reset_charger(void)
{
	return 0;
}

static int smbchg_read_full(void)
{
#if 0
	int rc = 0;
	u8 stat = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!oplus_chg_is_usb_present())
		return 0;

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
		return 0;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE)
		return 1;
#endif
	return 0;
}

static int smbchg_otg_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	chg_err("smbchg_otg_enable start!\n");
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_OTG_VBUS_REGULATOR_ENABLE, 1);
	if (rc) {
		chg_err("smbchg_otg_enable fail, rc=%d\n", rc);
		return rc;
	}
	schedule_delayed_work(&bcdev->otg_status_check_work, 0);
	chg_err("smbchg_otg_enable sucess!\n");
	return rc;
}

static int smbchg_otg_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	chg_err("smbchg_otg_disable start!\n");
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_OTG_VBUS_REGULATOR_ENABLE, 0);
	if (rc) {
		chg_err("smbchg_otg_disable fail, rc=%d\n", rc);
		return rc;
	}
	chg_err("smbchg_otg_disable sucess!\n");
	return rc;
}

static int oplus_set_chging_term_disable(void)
{
	return 0;
}

static bool qcom_check_charger_resume(void)
{
	return true;
}

int smbchg_get_chargerid_volt(void)
{
	return 0;
}

void smbchg_set_chargerid_switch_val(int value)
{
	return;
}

int smbchg_get_chargerid_switch_val(void)
{
	return -1;
}

static bool smbchg_need_to_check_ibatt(void)
{
	return false;
}

static int smbchg_get_chg_current_step(void)
{
	return 25;
}

int opchg_get_charger_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		goto get_type_done;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_UNKNOWN:
			charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		case POWER_SUPPLY_USB_TYPE_SDP:
			charger_type = POWER_SUPPLY_TYPE_USB;
			break;
		case POWER_SUPPLY_USB_TYPE_CDP:
			charger_type = POWER_SUPPLY_TYPE_USB_CDP;
			break;
		case POWER_SUPPLY_USB_TYPE_DCP:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
	}

get_type_done:
	if (chip && chip->wireless_support &&
			(oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present()))
		charger_type = POWER_SUPPLY_TYPE_WIRELESS;
	return charger_type;
}

int qpnp_get_prop_charger_voltage_now(void)
{
	int rc = 0;
	int prop_id = 0;
	static int vbus_volt = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	union oplus_chg_mod_propval pval = {0};

	if (!chip) {
		return 0;
	}

	if (oplus_chg_is_wls_present()) {
		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW, &pval);
		if (rc >= 0) {
			return pval.intval;
		}
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return vbus_volt;
	}
	vbus_volt = pst->prop[prop_id] / 1000;
	vbus_volt += 300;

	return vbus_volt;
}

static int oplus_get_ibus_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int ibus = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return ibus;
	}
	ibus = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return ibus;
}

int oplus_adsp_voocphy_get_fast_chg_type(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int fast_chg_type = 0;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VOOC_FAST_CHG_TYPE);
	if (rc < 0) {
		chg_err("read vooc_fast_chg_type fail, rc=%d\n", rc);
		return 0;
	}
	fast_chg_type = (pst->prop[USB_VOOC_FAST_CHG_TYPE]) & 0x7F;

	return fast_chg_type;
}

static int oplus_get_usb_icl(void)
{
	int rc = 0;
	int prop_id = 0;
	static int usb_icl = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb icl fail, rc=%d\n", rc);
		return usb_icl;
	}
	usb_icl = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	if(usb_icl <= 0)
		return 500;

	return usb_icl;
}

bool oplus_chg_is_usb_present(void)
{
	int rc = 0;
	int prop_id = 0;
	bool vbus_rising = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_ONLINE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_rising fail, rc=%d\n", rc);
		return false;
	}
	vbus_rising = pst->prop[prop_id];

	if (oplus_vchg_trig_is_support() == true
			&& oplus_get_vchg_trig_status() == 1 && vbus_rising == true) {
		/*chg_err("vchg_trig is high, vbus: 0\n");*/
		vbus_rising = false;
	}

	if (vbus_rising == false && (oplus_wpc_get_wireless_charge_start() || oplus_chg_is_wls_present())) {
		chg_err("USBIN_PLUGIN_RT_STS_BIT low but wpc has started\n");
		vbus_rising = true;
	}

	if (vbus_rising == false && pst->prop[prop_id] == 2) {
		chg_err("USBIN low but svooc/vooc started\n");
		vbus_rising = true;
	}

	return vbus_rising;
}

int qpnp_get_battery_voltage(void)
{
	return 3800;//Not use anymore
}

#if 0
static int get_boot_mode(void)
{
	return 0;
}
#endif

int smbchg_get_boot_reason(void)
{
	return 0;
}

int oplus_chg_get_shutdown_soc(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_RTC_SOC);
	if (rc < 0) {
		chg_err("read battery rtc soc fail, rc=%d\n", rc);
		return 0;
	}
	chg_err("read battery rtc soc success, rtc_soc=%d\n", pst->prop[BATT_RTC_SOC]);

	return pst->prop[BATT_RTC_SOC];
}

int oplus_chg_backup_soc(int backup_soc)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_RTC_SOC, backup_soc);
	if (rc) {
		chg_err("set battery rtc soc fail, rc=%d\n", rc);
		return 0;
	}
	chg_err("write battery rtc soc success, rtc_soc=%d\n", backup_soc);

	return 0;
}

static int smbchg_get_aicl_level_ma(void)
{
	return 0;
}

static void smbchg_rerun_aicl(void)
{
	//smbchg_aicl_enable(false);
	/* Add a delay so that AICL successfully clears */
	//msleep(50);
	//smbchg_aicl_enable(true);
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oplus_chg_get_dyna_aicl_result(void)
{
	struct power_supply *usb_psy = NULL;
	union power_supply_propval pval = {0, };

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
				&pval);
		return pval.intval / 1000;
	}

	return 1000;
}
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */

int oplus_chg_get_charger_subtype(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charg_subtype = CHARGER_SUBTYPE_DEFAULT;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return CHARGER_SUBTYPE_DEFAULT;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read charger type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
		return charg_subtype;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			charg_subtype = CHARGER_SUBTYPE_PD;
			break;
		default:
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			break;
	}

	if (charg_subtype == CHARGER_SUBTYPE_DEFAULT) {
		rc = read_property_id(bcdev, pst, USB_ADAP_SUBTYPE);
		if (rc < 0) {
			chg_err("read charger subtype fail, rc=%d\n", rc);
			if (!chip->charger_exist)
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			return charg_subtype;
		}
		switch (pst->prop[USB_ADAP_SUBTYPE]) {
			case CHARGER_SUBTYPE_FASTCHG_VOOC:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_VOOC;
				break;
			case CHARGER_SUBTYPE_FASTCHG_SVOOC:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_SVOOC;
				break;
			case CHARGER_SUBTYPE_QC:
				charg_subtype = CHARGER_SUBTYPE_QC;
				break;
			default:
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
				break;
		}
	}

	return charg_subtype;
}

int oplus_sm8150_get_pd_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static bool is_pd_type = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb pd_type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			is_pd_type = false;
		return is_pd_type;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			is_pd_type = true;
			break;
		default:
			is_pd_type = false;
			break;
	}

	return is_pd_type;
}

extern  int oplus_get_vbatt_pdqc_to_9v_thr(void);
int oplus_chg_set_pd_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int vbatt_pdqc_to_9v_thr_dt = oplus_get_vbatt_pdqc_to_9v_thr();

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (!is_ext_chg_ops() && (vbatt_pdqc_to_9v_thr_dt > 4100)) {
		chip->limits.vbatt_pdqc_to_9v_thr = vbatt_pdqc_to_9v_thr_dt;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		chip->chg_ops->input_current_write(500);
		if(is_ext_chg_ops())
			oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x03);/*set Vsys Skip threshold 101%*/
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, 5000);
		if (rc)
			chg_err("set PDO 5V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 5V OK\n");
		msleep(300);
		if(is_ext_chg_ops())
			oplus_chg_unsuspend_charger();
	} else if (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr) {
		if(is_ext_chg_ops())
			oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x02);/*set Vsys Skip threshold 104%*/
		oplus_chg_enable_burst_mode(false);
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, 9000);
		if (rc)
			chg_err("set PDO 9V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 9V OK\n");
		msleep(300);
		if(is_ext_chg_ops())
			oplus_chg_unsuspend_charger();
	}

	return rc;
}

int oplus_chg_set_qc_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int vbatt_pdqc_to_9v_thr_dt = oplus_get_vbatt_pdqc_to_9v_thr();

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (!is_ext_chg_ops() && (vbatt_pdqc_to_9v_thr_dt > 4100)) {
		chip->limits.vbatt_pdqc_to_9v_thr = vbatt_pdqc_to_9v_thr_dt;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		chip->chg_ops->input_current_write(500);
		if(is_ext_chg_ops())
			oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x03);/*set Vsys Skip threshold 101%*/
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 5000);
		if (rc)
			chg_err("set QC 5V fail, rc=%d\n", rc);
		else
			chg_err("set QC 5V OK\n");
		msleep(400);
		if(is_ext_chg_ops())
			oplus_chg_unsuspend_charger();
	} else if (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr) {
		if(is_ext_chg_ops())
			oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x02);/*set Vsys Skip threshold 104%*/
		oplus_chg_enable_burst_mode(false);
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 9000);
		if (rc)
			chg_err("set QC 9V fail, rc=%d\n", rc);
		else
			chg_err("set QC 9V OK\n");
		msleep(300);
		if(is_ext_chg_ops())
			oplus_chg_unsuspend_charger();
	}

	return rc;
}

int oplus_chg_enable_qc_detect(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->hvdcp_disable == true) {
		chg_err("hvdcp_disable!\n");
		return -1;
	}

	rc = write_property_id(bcdev, pst, BATT_SET_QC, 0);

	return rc;
}

#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_input_current_limit_ctrl_by_vooc_write(int current_ma)
{
	int rc = 0;
	int cur_usb_icl = 0;
	int temp_curr = 0;

	cur_usb_icl = oplus_get_usb_icl();
	chg_err(" get cur_usb_icl = %d\n", cur_usb_icl);

	if (current_ma > cur_usb_icl) {
		for (temp_curr = cur_usb_icl; temp_curr < current_ma; temp_curr += 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = cur_usb_icl; temp_curr > current_ma; temp_curr -= 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = oplus_chg_set_input_current_with_no_aicl(current_ma);
	return rc;
}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
struct oplus_chg_operations  battery_chg_ops = {
	.get_charger_cycle = oplus_get_charger_cycle,
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oplus_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_wls_boost_en = smbchg_set_wls_boost_en,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oplus_chg_set_input_current,
	.input_current_write_without_aicl = oplus_chg_set_input_current_with_no_aicl,
	.wls_input_current_write = oplus_chg_set_wls_input_current,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enable,
	.charging_disable = smbchg_charging_disable,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.set_charging_term_disable = oplus_set_chging_term_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.get_charger_current = oplus_get_ibus_current,
	.check_chrdet_status = oplus_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oplus_chg_get_shutdown_soc,
	.set_rtc_soc = oplus_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oplus_chg_get_dyna_aicl_result,
#endif
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.oplus_chg_get_pd_type = oplus_sm8150_get_pd_type,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.set_qc_config = oplus_chg_set_qc_config,
	.enable_qc_detect = oplus_chg_enable_qc_detect,
	.adsp_voocphy_set_match_temp = oplus_adsp_voocphy_set_match_temp,
	.input_current_ctrl_by_vooc_write = oplus_input_current_limit_ctrl_by_vooc_write,
	.get_props_from_adsp_by_buffer = oplus_get_props_from_adsp_by_buffer,
	//.input_current_write_without_aicl = mp2650_input_current_limit_without_aicl,
	//.oplus_chg_wdt_enable = mp2650_wdt_enable,
};
#endif /* OPLUS_FEATURE_CHG_BASIC */


/**********************************************************************
 * battery gauge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC
static int fg_bq27541_get_battery_mvolts(void)
{
	int rc = 0;
	int prop_id = 0;
	static int volt = 4000;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		volt = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[2], 1000);
		return volt;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery volt fail, rc=%d\n", rc);
		return volt;
	}
	volt = DIV_ROUND_CLOSEST(pst->prop[prop_id], 1000);

	return volt;
}

static int fg_bq27541_get_battery_temperature(void)
{
	int rc = 0;
	int prop_id = 0;
	static int temp = 250;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		temp = bcdev->read_buffer_dump.data_buffer[0];
		goto HIGH_TEMP;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_TEMP);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery temp fail, rc=%d\n", rc);
		return temp;
	}
	temp = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);

HIGH_TEMP:
	if (get_eng_version() == HIGH_TEMP_AGING) {
		printk(KERN_ERR "[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION enable here, \
				disable high tbat shutdown \n");
		if (temp > 690)
			temp = 690;
	}

	return temp;
}

static int fg_bq27541_get_batt_remaining_capacity(void)
{
	int rc = 0;
	static int batt_rm = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return batt_rm;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		batt_rm = bcdev->read_buffer_dump.data_buffer[4];
		chip->batt_rm = batt_rm * chip->vbatt_num;
		return batt_rm;
	}

	rc = read_property_id(bcdev, pst, BATT_CHG_COUNTER);
	if (rc < 0) {
		chg_err("read battery chg counter fail, rc=%d\n", rc);
		return batt_rm;
	}
	batt_rm = DIV_ROUND_CLOSEST(pst->prop[BATT_CHG_COUNTER], 1000);

	chip->batt_rm = batt_rm * chip->vbatt_num;
	return batt_rm;
}

static int fg_bq27541_get_battery_soc(void)
{
	int rc = 0;
	int prop_id = 0;
	static int soc = 50;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		soc = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[3], 100);
		return soc;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return soc;
	}
	soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return soc;
}

static int fg_bq27541_get_average_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int curr = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		curr = DIV_ROUND_CLOSEST((int)bcdev->read_buffer_dump.data_buffer[1], 1000);
		return curr;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return curr;
	}
	curr = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return curr;
}

static int fg_bq27541_get_battery_fcc(void)
{
	static int fcc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		fcc = bcdev->read_buffer_dump.data_buffer[6];
		return fcc;
	}

	return fcc;
}

static int fg_bq27541_get_battery_cc(void)
{
	static int cc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		cc = bcdev->read_buffer_dump.data_buffer[7];
		return cc;
	}

	return cc;
}

static int fg_bq27541_get_battery_soh(void)
{
	static int soh = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		soh = bcdev->read_buffer_dump.data_buffer[8];
		return soh;
	}

	return soh;
}

static bool fg_bq27541_get_battery_authenticate(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_BATTERY_AUTH);
	if (rc < 0) {
		chg_err("read battery auth fail, rc=%d\n", rc);
		return false;
	}
	chg_err("read battery auth success, auth=%d\n", pst->prop[BATT_BATTERY_AUTH]);

	return pst->prop[BATT_BATTERY_AUTH];
}

static void fg_bq27541_set_battery_full(bool full)
{
	/*Do nothing*/
}
/*
static int fg_bq27541_get_prev_battery_mvolts(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}

static int fg_bq27541_get_prev_battery_temperature(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->temperature;
}

static int fg_bq27541_get_prev_battery_soc(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->soc;
}

static int fg_bq27541_get_prev_average_current(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->icharging;
}

static int fg_bq27541_get_prev_batt_remaining_capacity(void)
{
	return 0;
}
*/
static int fg_bq27541_get_battery_mvolts_2cell_max(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}

static int fg_bq27541_get_battery_mvolts_2cell_min(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}
/*
static int fg_bq27541_get_prev_battery_mvolts_2cell_max(void)
{
	return 4000;
}

static int fg_bq27541_get_prev_battery_mvolts_2cell_min(void)
{
	return 4000;
}
*/
static int fg_bq28z610_modify_dod0(void)
{
	return 0;
}

static int fg_bq28z610_update_soc_smooth_parameter(void)
{
	return 0;
}

static int fg_bq28z610_get_battery_balancing_status(void)
{
	return 0;
}

static struct oplus_gauge_operations battery_gauge_ops = {
	.get_battery_mvolts = fg_bq27541_get_battery_mvolts,
	.get_battery_temperature = fg_bq27541_get_battery_temperature,
	.get_batt_remaining_capacity = fg_bq27541_get_batt_remaining_capacity,
	.get_battery_soc = fg_bq27541_get_battery_soc,
	.get_average_current = fg_bq27541_get_average_current,
	.get_battery_fcc = fg_bq27541_get_battery_fcc,
	.get_prev_batt_fcc = fg_bq27541_get_battery_fcc,
	.get_battery_cc = fg_bq27541_get_battery_cc,
	.get_battery_soh = fg_bq27541_get_battery_soh,
	.get_battery_authenticate = fg_bq27541_get_battery_authenticate,
	.get_battery_hmac = fg_bq27541_get_battery_authenticate,
	.set_battery_full = fg_bq27541_set_battery_full,
	.get_prev_battery_mvolts = fg_bq27541_get_battery_mvolts,
	.get_prev_battery_temperature = fg_bq27541_get_battery_temperature,
	.get_prev_battery_soc = fg_bq27541_get_battery_soc,
	.get_prev_average_current = fg_bq27541_get_average_current,
	.get_prev_batt_remaining_capacity = fg_bq27541_get_batt_remaining_capacity,
	.get_battery_mvolts_2cell_max = fg_bq27541_get_battery_mvolts_2cell_max,
	.get_battery_mvolts_2cell_min = fg_bq27541_get_battery_mvolts_2cell_min,
	.get_prev_battery_mvolts_2cell_max = fg_bq27541_get_battery_mvolts_2cell_max,
	.get_prev_battery_mvolts_2cell_min = fg_bq27541_get_battery_mvolts_2cell_min,
	.update_battery_dod0 = fg_bq28z610_modify_dod0,
	.update_soc_smooth_parameter = fg_bq28z610_update_soc_smooth_parameter,
	.get_battery_cb_status = fg_bq28z610_get_battery_balancing_status,
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static int smbchg_wls_input_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	if (!probe_done)
		return 0;

	if (en)
		smbchg_usb_suspend_disable();
	else
		smbchg_usb_suspend_enable();

	return 0;
}

static int smbchg_wls_output_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;

	if (!probe_done)
		return 0;

	if (en)
		rc = smbchg_charging_enable();
	else
		rc = smbchg_charging_disable();
	return rc;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, en ? 1 : 0);
	if (rc)
		pr_err("set %s charging fail, rc=%d\n", en ? "enable": "disable", rc);

	return rc;
}

static int smbchg_wls_set_icl(struct oplus_chg_ic_dev *ic_dev, int icl_ma)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_INPUT_CURR_LIMIT, icl_ma * 1000);
	if (rc < 0)
		pr_err("set wls icl to %u error, rc=%d\n", icl_ma, rc);
	else
		pr_info("set icl to %d mA\n", icl_ma);

	return rc;
}

static int smbchg_wls_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = BATT_CHG_CTRL_LIM;
	rc = write_property_id(bcdev, pst, prop_id, fcc_ma * 1000);
	if (rc)
		pr_err("set fcc to %d mA fail, rc=%d\n", fcc_ma, rc);
	else
		pr_info("set fcc to %d mA\n", fcc_ma);

	return rc;
}

static int smbchg_wls_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	rc = smbchg_float_voltage_set(fv_mv);
	if (rc < 0)
		pr_err("set wls fv to %u error, rc=%d\n", fv_mv, rc);

	return rc;
}

static int smbchg_wls_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	return rc;
}

static int smbchg_wls_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	rc = read_property_id(chip, pst, WLS_CURR_NOW);
	if (rc < 0) {
		pr_err("get wls input curr error, rc=%d\n", rc);
		return rc;
	}
	*curr = pst->prop[WLS_CURR_NOW] / 1000;

	return rc;
}

static int smbchg_wls_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	rc = read_property_id(chip, pst, WLS_VOLT_NOW);
	if (rc < 0) {
		pr_err("set wls input vol error, rc=%d\n", rc);
		return rc;
	}
	*vol = pst->prop[WLS_VOLT_NOW] / 1000;

	return rc;
}

static int smbchg_wls_set_boost_en(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	if (is_support_tx_boost(chip)) {
		if (en) {
			oplus_set_tx_ovp_en_val(1);
			oplus_set_tx_boost_en_val(1);
		} else {
			oplus_set_tx_boost_en_val(0);
			oplus_set_tx_ovp_en_val(0);
		}
		return 0;
	}

	if (en && chip->wls_boost_soft_start && chip->wls_set_boost_vol != PM8350B_BOOST_VOL_MIN_MV) {
		rc = write_property_id(chip, pst, WLS_BOOST_VOLT, PM8350B_BOOST_VOL_MIN_MV);
		if (rc < 0) {
			pr_err("set boost vol to PM8350B_BOOST_VOL_MIN_MV error, rc=%d\n", rc);
			return rc;
		}
	}

	rc = write_property_id(chip, pst, WLS_BOOST_EN, en ? 1 : 0);
	if (rc < 0) {
		pr_err("set boost %s error, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}

	if (en && chip->wls_boost_soft_start && chip->wls_set_boost_vol != PM8350B_BOOST_VOL_MIN_MV) {
		msleep(2);
		rc = write_property_id(chip, pst, WLS_BOOST_VOLT, chip->wls_set_boost_vol);
		if (rc < 0)
			pr_err("set boost vol to %d mV error, rc=%d\n", chip->wls_set_boost_vol, rc);
	}

	return rc;
}

static int smbchg_wls_set_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	if (is_support_tx_boost(chip))
		return 0;

	rc = write_property_id(chip, pst, WLS_BOOST_VOLT, vol_mv);
	if (rc < 0)
		pr_err("set boost vol to %d mV error, rc=%d\n", vol_mv, rc);
	else
		chip->wls_set_boost_vol = vol_mv;

	return rc;
}

static int smbchg_wls_set_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_BOOST_AICL_ENABLE, en);
	if (rc < 0)
		pr_err("can't %s aicl, rc=%d\n", en ? "enable" : " disable", rc);

	return rc;
}

static int smbchg_wls_rerun_aicl(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *chip;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_BOOST_AICL_RERUN, 1);
	if (rc < 0)
		pr_err("can't rerun aicl, rc=%d\n", rc);

	return rc;
}

static struct oplus_chg_ic_buck_ops pm8350b_dev_ops = {
	.chg_set_input_enable = smbchg_wls_input_enable,
	.chg_set_output_enable = smbchg_wls_output_enable,
	.chg_set_icl = smbchg_wls_set_icl,
	.chg_set_fcc = smbchg_wls_set_fcc,
	.chg_set_fv = smbchg_wls_set_fv,
	.chg_set_rechg_vol = smbchg_wls_rechg_vol,
	.chg_get_icl = NULL,
	.chg_get_input_curr = smbchg_wls_get_input_curr,
	.chg_get_input_vol = smbchg_wls_get_input_vol,
	.chg_set_boost_en = smbchg_wls_set_boost_en,
	.chg_set_boost_vol = smbchg_wls_set_boost_vol,
	.chg_set_boost_curr_limit =  NULL,
	.chg_set_aicl_enable = smbchg_wls_set_aicl_enable,
	.chg_set_aicl_rerun = smbchg_wls_rerun_aicl,
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_flash_screen_ctrl_by_pcb_version(struct oplus_chg_chip *chip)
{
	if (!chip)
		return;

	//if (get_PCB_Version() >= MP1) {
	chip->flash_screen_ctrl_status &= ~FLASH_SCREEN_CTRL_DTSI;
	//}
	chg_err("flash_screen_ctrl_dtsi=%d\n", chip->flash_screen_ctrl_status);
}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC

static enum oplus_chg_mod_property oplus_chg_usb_props[] = {
	OPLUS_CHG_PROP_ONLINE,
};

static enum oplus_chg_mod_property oplus_chg_usb_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
};

static int oplus_chg_usb_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	return -EINVAL;
}

static int oplus_chg_usb_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	return -EINVAL;
}

static int oplus_chg_usb_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_usb_mod_desc = {
	.name = "usb",
	.type = OPLUS_CHG_MOD_USB,
	.properties = oplus_chg_usb_props,
	.num_properties = ARRAY_SIZE(oplus_chg_usb_props),
	.uevent_properties = oplus_chg_usb_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_usb_uevent_props),
	.exten_properties = NULL,
	.num_exten_properties = 0,
	.get_property = oplus_chg_usb_get_prop,
	.set_property = oplus_chg_usb_set_prop,
	.property_is_writeable	= oplus_chg_usb_prop_is_writeable,
};

static int oplus_chg_usb_init_mod(struct battery_chg_dev *bcdev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = bcdev;
	ocm_cfg.of_node = bcdev->dev->of_node;

	bcdev->usb_ocm = oplus_chg_mod_register(bcdev->dev,
					   &oplus_chg_usb_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(bcdev->usb_ocm)) {
		pr_err("Couldn't register usb ocm\n");
		rc = PTR_ERR(bcdev->usb_ocm);
		return rc;
	}

	return 0;
}

static ssize_t proc_debug_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int rc = 0;
	int reg_data = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_DEBUG_REG);
	if (rc) {
		chg_err("read debug reg fail, rc=%d\n", rc);
	} else {
		chg_err("read debug reg success, rc=%d\n", rc);
	}

	reg_data = pst->prop[USB_DEBUG_REG];

	sprintf(page, "0x%x\n", reg_data);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_debug_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
	int rc = 0;
	char buffer[10] = {0};
	int add_data = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (count > 10) {
		chg_err("%s: count so len.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: read proc input error.\n", __func__);
		return -EFAULT;
	}

	if (1 != sscanf(buffer, "0x%x", &add_data)) {
		chg_err("invalid content: '%s', length = %zd\n", buf, count);
		return -EFAULT;
	}
	chg_err("%s: add:0x%x, data:0x%x\n", __func__, (add_data >> 8) & 0xffff, (add_data & 0xff));

	rc = write_property_id(bcdev, pst, USB_DEBUG_REG, add_data);
	if (rc) {
		chg_err("set usb_debug_reg fail, rc=%d\n", rc);
	} else {
		chg_err("set usb_debug_reg success, rc=%d\n", rc);
	}

	return count;
}

static const struct proc_ops proc_debug_reg_ops =
{
	.proc_read = proc_debug_reg_read,
	.proc_write  = proc_debug_reg_write,
	.proc_open  = simple_open,
};

static int init_debug_reg_proc(struct oplus_chg_chip *da)
{
	int ret = 0;
	struct proc_dir_entry *pr_entry_da = NULL;
	struct proc_dir_entry *pr_entry_tmp = NULL;

	pr_entry_da = proc_mkdir("8350_reg", NULL);
	if (pr_entry_da == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create debug_reg proc entry\n", __func__);
	}

	pr_entry_tmp = proc_create_data("reg", 0644, pr_entry_da, &proc_debug_reg_ops, da);
	if (pr_entry_tmp == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	return 0;
}

static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static unsigned long suspend_tm_sec = 0;
static int battery_chg_pm_resume(struct device *dev)
{
	int rc = 0;
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;

	if (!g_oplus_chip)
		return 0;

	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0) {
		sleep_time = 0;
	}

	if (is_ext_chg_ops())
		return 0;

	oplus_chg_soc_update_when_resume(sleep_time);

	return 0;
}

static int battery_chg_pm_suspend(struct device *dev)
{
	if (!g_oplus_chip)
		return 0;

	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}

	return 0;
}

static const struct dev_pm_ops battery_chg_pm_ops = {
	.resume		= battery_chg_pm_resume,
	.suspend	= battery_chg_pm_suspend,
};
#endif

static int battery_chg_probe(struct platform_device *pdev)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_gauge_chip *gauge_chip;
	struct oplus_chg_chip *oplus_chip;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
#endif
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	int rc, i;

#ifdef OPLUS_FEATURE_CHG_BASIC
	pr_info("battery_chg_probe start...\n");
	gauge_chip = devm_kzalloc(&pdev->dev, sizeof(*gauge_chip), GFP_KERNEL);
	if (!gauge_chip) {
		pr_err("oplus_gauge_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}
	gauge_chip->gauge_ops = &battery_gauge_ops;
	oplus_gauge_init(gauge_chip);

	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip) {
		pr_err("oplus_chg_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	oplus_chip->dev = &pdev->dev;
	rc = oplus_chg_parse_svooc_dt(oplus_chip);

	if (oplus_gauge_check_chip_is_null()) {
		chg_err("gauge chip null, will do after bettery init.\n");
		return -EPROBE_DEFER;
	}

	oplus_chip->chg_ops = oplus_chg_ops_get();
	if (!oplus_chip->chg_ops) {
		chg_err("oplus_chg_ops_get null, fatal error!!!\n");
		return -EPROBE_DEFER;
	}

	g_oplus_chip = oplus_chip;
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip->pmic_spmi.bcdev_chip = bcdev;
	oplus_chip->usbtemp_wq_init_finished = false;
	bcdev->hvdcp_detect_time = 0;
	bcdev->hvdcp_detach_time = 0;
	bcdev->hvdcp_detect_ok = false;
	bcdev->hvdcp_disable = false;
	bcdev->adsp_voocphy_err_check = false;
#endif

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_WLS].map = wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
	mutex_init(&bcdev->oplus_custom_gpio.pinctrl_mutex);
	mutex_init(&bcdev->read_buffer_lock);
	init_completion(&bcdev->oem_read_ack);
#endif
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);
	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->adsp_voocphy_status_work, oplus_adsp_voocphy_status_func);
	INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func);
	INIT_DELAYED_WORK(&bcdev->ccdetect_work, oplus_ccdetect_work);
	INIT_DELAYED_WORK(&bcdev->usbtemp_recover_work, oplus_usbtemp_recover_work);
	INIT_DELAYED_WORK(&bcdev->cid_status_change_work, oplus_cid_status_change_work);
	INIT_DELAYED_WORK(&bcdev->adsp_crash_recover_work, oplus_adsp_crash_recover_func);
	INIT_DELAYED_WORK(&bcdev->check_charger_out_work, oplus_check_charger_out_func);
	INIT_DELAYED_WORK(&bcdev->adsp_voocphy_enable_check_work, oplus_adsp_voocphy_enable_check_func);
	INIT_DELAYED_WORK(&bcdev->otg_vbus_enable_work, otg_notification_handler);
	INIT_DELAYED_WORK(&bcdev->hvdcp_disable_work, oplus_hvdcp_disable_work);
	INIT_DELAYED_WORK(&bcdev->typec_state_change_work, oplus_typec_state_change_work);
	INIT_DELAYED_WORK(&bcdev->suspend_check_work, oplus_suspend_check_work);
	INIT_DELAYED_WORK(&bcdev->vbus_adc_enable_work, oplus_vbus_enable_adc_work);
	INIT_DELAYED_WORK(&bcdev->adsp_voocphy_err_work, oplus_adsp_voocphy_err_work);
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->vchg_trig_work, oplus_vchg_trig_work);
	INIT_DELAYED_WORK(&bcdev->wait_wired_charge_on, oplus_wait_wired_charge_on_work);
	INIT_DELAYED_WORK(&bcdev->wait_wired_charge_off, oplus_wait_wired_charge_off_work);
#endif

	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;

	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	bcdev->initialized = true;
	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	register_reboot_notifier(&bcdev->reboot_notifier);

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0) {
		dev_err(dev, "Failed to parse dt rc=%d\n", rc);
		goto error;
	}

	bcdev->restrict_fcc_ua = DEFAULT_RESTRICT_FCC_UA;
	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;
	rc = battery_chg_init_psy(bcdev);
	if (rc < 0)
		goto error;

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		dev_err(dev, "Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_usbtemp_iio_init(oplus_chip);
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_set_flash_screen_ctrl_by_pcb_version(oplus_chip);
	oplus_chg_2uart_pinctrl_init(oplus_chip);
	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
		oplus_get_props_from_adsp_by_buffer();
	oplus_chg_init(oplus_chip);
	oplus_chg_configfs_init(oplus_chip);
	oplus_chg_wake_update_work();
	oplus_chip->temperature = oplus_chg_match_temp_for_chging();
	if (oplus_usbtemp_check_is_support() == true) {
		oplus_usbtemp_thread_init();
	}

#if 0
	if (qpnp_is_power_off_charging() == false) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}
#endif

	if (oplus_vchg_trig_is_support() == true) {
		schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(3000));
		oplus_vchg_trig_irq_register(bcdev);
	}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	battery_chg_add_debugfs(bcdev);
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);
	schedule_work(&bcdev->usb_type_work);
#ifdef OPLUS_FEATURE_CHG_BASIC
	if (oplus_ccdetect_check_is_gpio(oplus_chip) == true) {
		oplus_ccdetect_irq_register(oplus_chip);
	}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_adsp_voocphy_set_match_temp();
	oplus_dwc3_config_usbphy_pfunc(&oplus_is_pd_svooc);
	oplus_adsp_voocphy_enable(true);
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work,
		round_jiffies_relative(msecs_to_jiffies(5000)));
	init_debug_reg_proc(oplus_chip);
	pr_info("battery_chg_probe end...\n");
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	rc = oplus_chg_usb_init_mod(bcdev);
	if (rc < 0)
		goto error;
	rc = of_property_read_u32(bcdev->dev->of_node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		pr_err("can't get ic type, rc=%d\n", rc);
		goto ic_reg_err;
	}
	rc = of_property_read_u32(bcdev->dev->of_node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		pr_err("can't get ic index, rc=%d\n", rc);
		goto ic_reg_err;
	}
	bcdev->ic_dev = devm_oplus_chg_ic_register(dev, bcdev->dev->of_node->name, ic_index);
	if (!bcdev->ic_dev) {
		rc = -ENODEV;
		pr_err("register %s error\n", bcdev->dev->of_node->name);
		goto ic_reg_err;
	}
	bcdev->ic_dev->dev_ops = &pm8350b_dev_ops;
	bcdev->ic_dev->type = ic_type;
	probe_done = true;
	if (bcdev->usb_ocm)
		oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_ADSP_STARTED);
	else
		pr_err("usb ocm not fount\n");
#endif
	return 0;

#ifdef OPLUS_FEATURE_CHG_BASIC
ic_reg_err:
	oplus_chg_mod_unregister(bcdev->usb_ocm);
#endif
error:
	bcdev->initialized = false;
	complete(&bcdev->ack);
	pmic_glink_unregister_client(bcdev->client);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);
	int rc;

#ifdef OPLUS_FEATURE_CHG_BASIC
	probe_done = true;
	devm_oplus_chg_ic_unregister(bcdev->dev, bcdev->ic_dev);
	oplus_chg_mod_unregister(bcdev->usb_ocm);
#endif
	device_init_wakeup(bcdev->dev, false);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_exit();
#endif
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	rc = pmic_glink_unregister_client(bcdev->client);
	if (rc < 0) {
		pr_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}
	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC

static void battery_chg_shutdown(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = NULL;

	if (g_oplus_chip) {
		chg_err("disable adsp voocphy");
		bcdev = g_oplus_chip->pmic_spmi.bcdev_chip;
		cancel_delayed_work_sync(&bcdev->adsp_voocphy_enable_check_work);
		oplus_typec_disable();
		oplus_adsp_voocphy_enable(false);
	}

	if (g_oplus_chip
		&& g_oplus_chip->chg_ops->charger_suspend
		&& g_oplus_chip->chg_ops->charger_unsuspend) {
		g_oplus_chip->chg_ops->charger_suspend();
		msleep(1000);
		g_oplus_chip->chg_ops->charger_unsuspend();
	}

	if (g_oplus_chip && g_oplus_chip->enable_shipmode) {
		smbchg_enter_shipmode(g_oplus_chip);
		msleep(1000);
	}
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "qcom,battery-charger" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "qti_battery_charger",
		.of_match_table = battery_chg_match_table,
#ifdef OPLUS_FEATURE_CHG_BASIC
		.pm	= &battery_chg_pm_ops,
#endif
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
#ifdef OPLUS_FEATURE_CHG_BASIC
	.shutdown = battery_chg_shutdown,
#endif
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static int __init sm8350_chg_init(void)
{
	int ret;

	oplus_chg_ops_register("plat-pmic", &battery_chg_ops);

	adsp_voocphy_init();
	ret = platform_driver_register(&battery_chg_driver);
	return ret;
}

static void __exit sm8350_chg_exit(void)
{
	platform_driver_unregister(&battery_chg_driver);
	/*mp2650_driver_exit();*/
	adsp_voocphy_exit();
	oplus_chg_ops_deinit();
}

oplus_chg_module_register(sm8350_chg);
#else
module_platform_driver(battery_chg_driver);
#endif

MODULE_DESCRIPTION("QTI Glink battery charger driver");
MODULE_LICENSE("GPL v2");
