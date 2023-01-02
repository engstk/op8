// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_IC_H__
#define __OPLUS_CHG_IC_H__

#include <linux/cdev.h>
#include <oplus_chg.h>

#define OPLUS_CHG_IC_INIT_RETRY_DELAY	100
#define OPLUS_CHG_IC_INIT_RETRY_MAX	1000
#define OPLUS_CHG_IC_MANU_NAME_MAX	32
#define OPLUS_CHG_IC_FW_ID_MAX		16

struct oplus_chg_ic_dev;

enum oplus_chg_ic_type {
	OPLUS_CHG_IC_BUCK,
	OPLUS_CHG_IC_BOOST,
	OPLUS_CHG_IC_BUCK_BOOST,
	OPLUS_CHG_IC_CP_DIV2,
	OPLUS_CHG_IC_CP_MUL2,
	OPLUS_CHG_IC_CP_TW2,
	OPLUS_CHG_IC_RX,
	OPLUS_CHG_IC_VIRTUAL_RX,
	OPLUS_CHG_IC_VIRTUAL_BUCK,
	OPLUS_CHG_IC_VIRTUAL_CP,
	OPLUS_CHG_IC_VIRTUAL_USB,
	OPLUS_CHG_IC_TYPEC,
	OPLUS_CHG_IC_GAUGE,
	OPLUS_CHG_IC_VIRTUAL_GAUGE,
	OPLUS_CHG_IC_ASIC,
	OPLUS_CHG_IC_VIRTUAL_ASIC,
};

enum oplus_chg_ic_connect_type {
	OPLUS_CHG_IC_CONNECT_PARALLEL,
	OPLUS_CHG_IC_CONNECT_SERIAL,
};

enum oplus_chg_ic_func {
	OPLUS_IC_FUNC_EXIT = 0,
	OPLUS_IC_FUNC_INIT,
	OPLUS_IC_FUNC_REG_DUMP,
	OPLUS_IC_FUNC_SMT_TEST,
	OPLUS_IC_FUNC_CHIP_ENABLE,
	OPLUS_IC_FUNC_CHIP_IS_ENABLE,

	/* wireless rx */
	OPLUS_IC_FUNC_RX_IS_CONNECTED = 100,
	OPLUS_IC_FUNC_RX_GET_VOUT,
	OPLUS_IC_FUNC_RX_SET_VOUT,
	OPLUS_IC_FUNC_RX_GET_VRECT,
	OPLUS_IC_FUNC_RX_GET_IOUT,
	OPLUS_IC_FUNC_RX_GET_TRX_VOL,
	OPLUS_IC_FUNC_RX_GET_TRX_CURR,
	OPLUS_IC_FUNC_RX_GET_CEP_COUNT,
	OPLUS_IC_FUNC_RX_GET_CEP_VAL,
	OPLUS_IC_FUNC_RX_GET_WORK_FREQ,
	OPLUS_IC_FUNC_RX_GET_RX_MODE,
	OPLUS_IC_FUNC_RX_SET_DCDC_ENABLE,
	OPLUS_IC_FUNC_RX_SET_TRX_ENABLE,
	OPLUS_IC_FUNC_RX_GET_TRX_STATUS,
	OPLUS_IC_FUNC_RX_GET_TRX_ERR,
	OPLUS_IC_FUNC_RX_GET_HEADROOM,
	OPLUS_IC_FUNC_RX_SET_HEADROOM,
	OPLUS_IC_FUNC_RX_SEND_MATCH_Q,
	OPLUS_IC_FUNC_RX_SET_FOD_PARM,
	OPLUS_IC_FUNC_RX_SEND_MSG,
	OPLUS_IC_FUNC_RX_REG_MSG_CALLBACK,
	OPLUS_IC_FUNC_RX_GET_FW_VERSION_BY_BUF,
	OPLUS_IC_FUNC_RX_GET_FW_VERSION_BY_CHIP,
	OPLUS_IC_FUNC_RX_UPGRADE_FW_BY_BUF,
	OPLUS_IC_FUNC_RX_UPGRADE_FW_BY_IMG,
	OPLUS_IC_FUNC_RX_CHECK_CONNECT,

	/* buck/boost */
	OPLUS_IC_FUNC_BUCK_INPUT_PRESENT = 200,
	OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND,
	OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND,
	OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND,
	OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND,
	OPLUS_IC_FUNC_BUCK_SET_ICL,
	OPLUS_IC_FUNC_BUCK_GET_ICL,
	OPLUS_IC_FUNC_BUCK_SET_FCC,
	OPLUS_IC_FUNC_BUCK_SET_FV,
	OPLUS_IC_FUNC_BUCK_SET_ITERM,
	OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL,
	OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR,
	OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL,
	OPLUS_IC_FUNC_BUCK_AICL_ENABLE,
	OPLUS_IC_FUNC_BUCK_AICL_RERUN,
	OPLUS_IC_FUNC_BUCK_AICL_RESET,
	OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION,
	OPLUS_IC_FUNC_BUCK_GET_HW_DETECT,
	OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE,
	OPLUS_IC_FUNC_BUCK_RERUN_BC12,
	OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE,
	OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE,
	OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG,
	OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG,
	OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS,
	OPLUS_IC_FUNC_BUCK_CURR_DROP,
	OPLUS_IC_FUNC_BUCK_WDT_ENABLE,
	OPLUS_IC_FUNC_BUCK_KICK_WDT,
	OPLUS_IC_FUNC_BUCK_BC12_COMPLETED,
	OPLUS_IC_FUNC_BUCK_SET_AICL_POINT,
	OPLUS_IC_FUNC_BUCK_SET_VINDPM,
	OPLUS_IC_FUNC_BUCK_HARDWARE_INIT,

	/* charge pump */
	OPLUS_IC_FUNC_CP_START = 300,

	/* gauge */
	OPLUS_IC_FUNC_GAUGE_UPDATE = 400,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
	OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_QM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_PD,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_PC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_QS,
	OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0,
	OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH,
	OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
	OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
	OPLUS_IC_FUNC_GAUGE_SET_LOCK,
	OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
	OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
	OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP,
	OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
	OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS,
	OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS,
	OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS,
	OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS,
	OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK,
	OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE,
	OPLUS_IC_FUNC_GAUGE_CHECK_RESET,
	OPLUS_IC_FUNC_GAUGE_SET_RESET,

	/* misc */
	OPLUS_IC_FUNC_GET_CHARGER_CYCLE = 500,
	OPLUS_IC_FUNC_OTG_BOOST_ENABLE,
	OPLUS_IC_FUNC_SET_OTG_BOOST_VOL,
	OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT,
	OPLUS_IC_FUNC_WLS_BOOST_ENABLE,
	OPLUS_IC_FUNC_SET_WLS_BOOST_VOL,
	OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT,
	OPLUS_IC_FUNC_GET_SHUTDOWN_SOC,
	OPLUS_IC_FUNC_BACKUP_SOC,
	OPLUS_IC_FUNC_GET_USB_TEMP,
	OPLUS_IC_FUNC_GET_USB_TEMP_VOLT,
	OPLUS_IC_FUNC_USB_TEMP_CHECK_IS_SUPPORT,
	OPLUS_IC_FUNC_GET_TYPEC_MODE,
	OPLUS_IC_FUNC_SET_TYPEC_MODE,
	OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE,
	OPLUS_IC_FUNC_GET_USB_DISCHG_STATUS,
	OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS,
	OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS,
	OPLUS_IC_FUNC_GET_OTG_ONLINE_STATUS,
	OPLUS_IC_FUNC_CC_DETECT_HAPPENED,
	OPLUS_IC_FUNC_GET_OTG_ENABLE,
	OPLUS_IC_FUNC_GET_CHARGER_VOL_MAX,
	OPLUS_IC_FUNC_GET_CHARGER_VOL_MIN,
	OPLUS_IC_FUNC_GET_CHARGER_CURR_MAX,
	OPLUS_IC_FUNC_DISABLE_VBUS,
	OPLUS_IC_FUNC_IS_OPLUS_SVID,
	OPLUS_IC_FUNC_GET_DATA_ROLE,

	/* voocphy */
	OPLUS_IC_FUNC_VOOCPHY_ENABLE = 600,
	OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN,
	OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL,
	OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP,

	/* vooc */
	OPLUS_IC_FUNC_VOOC_FW_UPGRADE = 700,
	OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE,
	OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER,
	OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX,
	OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
	OPLUS_IC_FUNC_VOOC_EINT_REGISTER,
	OPLUS_IC_FUNC_VOOC_EINT_UNREGISTER,
	OPLUS_IC_FUNC_VOOC_SET_DATA_ACTIVE,
	OPLUS_IC_FUNC_VOOC_SET_DATA_SLEEP,
	OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE,
	OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP,
	OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE,
	OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE,
	OPLUS_IC_FUNC_VOOC_GET_DATA_GPIO_VAL,
	OPLUS_IC_FUNC_VOOC_GET_RESET_GPIO_VAL,
	OPLUS_IC_FUNC_VOOC_GET_CLOCK_GPIO_VAL,
	OPLUS_IC_FUNC_VOOC_READ_DATA_BIT,
	OPLUS_IC_FUNC_VOOC_REPLY_DATA,
	OPLUS_IC_FUNC_VOOC_RESET_ACTIVE,
	OPLUS_IC_FUNC_VOOC_RESET_ACTIVE_FORCE,
	OPLUS_IC_FUNC_VOOC_RESET_SLEEP,
	OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO,
	OPLUS_IC_FUNC_VOOC_UPGRADING,
	OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS,
	OPLUS_IC_FUNC_VOOC_GET_IC_DEV,
};

enum oplus_chg_ic_virq_id {
	OPLUS_IC_VIRQ_ERR,
	OPLUS_IC_VIRQ_CC_DETECT,
	OPLUS_IC_VIRQ_PLUGIN,
	OPLUS_IC_VIRQ_CC_CHANGED,
	OPLUS_IC_VIRQ_VOOC_DATA,
	OPLUS_IC_VIRQ_SUSPEND_CHECK,
	OPLUS_IC_VIRQ_CHG_TYPE_CHANGE,
	OPLUS_IC_VIRQ_OFFLINE,
	OPLUS_IC_VIRQ_RESUME,
	OPLUS_IC_VIRQ_SVID,
	OPLUS_IC_VIRQ_OTG_ENABLE,
	OPLUS_IC_VIRQ_VOLTAGE_CHANGED,
	OPLUS_IC_VIRQ_CURRENT_CHANGED,
	OPLUS_IC_VIRQ_BC12_COMPLETED,
	OPLUS_IC_VIRQ_DATA_ROLE_CHANGED,
	OPLUS_IC_VIRQ_ONLINE,
};

enum oplus_chg_ic_err {
	OPLUS_IC_ERR_UNKNOWN = 0,
	OPLUS_IC_ERR_I2C,
	OPLUS_IC_ERR_GPIO,
	OPLUS_IC_ERR_PLAT_PMIC,
	OPLUS_IC_ERR_BUCK_BOOST,
	OPLUS_IC_ERR_GAUGE,
	OPLUS_IC_ERR_WLS_RX,
	OPLUS_IC_ERR_CP,
	OPLUS_IC_ERR_CC_LOGIC,
};

enum oplus_chg_ic_plat_pmic_err {
	PLAT_PMIC_ERR_UNKNOWN,
	PLAT_PMIC_ERR_VCONN_OVP,
	PLAT_PMIC_ERR_VCONN_UVP,
	PLAT_PMIC_ERR_VCONN_RVP,
	PLAT_PMIC_ERR_VCONN_OCP,
	PLAT_PMIC_ERR_VCONN_OPEN,
	PLAT_PMIC_ERR_VCONN_CLOSE,
	PLAT_PMIC_ERR_VBUS_ABNORMAL,
};

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
#define OPLUS_CHG_IC_FUNC_DATA_MAGIC 0X20302009
struct oplus_chg_ic_func_date_item {
	u32 num;
	u32 size;
	u32 str_data;
	u8 buf[0];
};

struct oplus_chg_ic_func_date {
	u32 magic;
	u32 size;
	u32 item_num;
	u8 buf[0];
};

struct oplus_chg_ic_overwrite_data {
	enum oplus_chg_ic_func func_id;
	struct list_head list;
	u32 size;
	u8 buf[0];
};

struct oplus_chg_ic_debug_data {
	enum oplus_chg_ic_func func_id;
	struct list_head overwrite_list;
	struct mutex overwrite_list_lock;
	enum oplus_chg_ic_func *overwrite_funcs;
	int func_num;

	ssize_t (*get_func_data)(struct oplus_chg_ic_dev *, enum oplus_chg_ic_func, void *);
	int (*set_func_data)(struct oplus_chg_ic_dev *, enum oplus_chg_ic_func, const void *, size_t);
};
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

typedef void (*virq_handler_t)(struct oplus_chg_ic_dev *, void *);

struct oplus_chg_ic_virq {
	enum oplus_chg_ic_virq_id virq_id;
	void *virq_data;
	virq_handler_t virq_handler;
};

struct oplus_chg_ic_cfg {
	const char *name;
	enum oplus_chg_ic_type type;
	int index;
	char manu_name[OPLUS_CHG_IC_MANU_NAME_MAX];
	char fw_id[OPLUS_CHG_IC_FW_ID_MAX];
	struct oplus_chg_ic_virq *virq_data;
	int virq_num;
	void *(*get_func)(struct oplus_chg_ic_dev *ic_dev,
			  enum oplus_chg_ic_func func_id);
};

struct oplus_chg_ic_dev {
	const char *name;
	struct device *dev;
	struct oplus_chg_ic_dev *parent;
	enum oplus_chg_ic_type type;
	struct list_head list;
	struct list_head err_list;
	spinlock_t err_list_lock;
	int index;
	char manu_name[32];
	char fw_id[16];

	bool online;
	bool ic_virtual_enable;

	struct oplus_chg_ic_virq *virq_data;
	int virq_num;

	void *(*get_func)(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id);
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	int minor;
	struct cdev cdev;
	struct device *debug_dev;
	char cdev_name[128];
	struct oplus_chg_ic_debug_data debug;
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */
};

#define IC_ERR_MSG_MAX		1024
struct oplus_chg_ic_err_msg {
	struct oplus_chg_ic_dev *ic;
	struct list_head list;
	enum oplus_chg_ic_err type;
	int sub_type;
	char msg[IC_ERR_MSG_MAX];
};

#define OPLUS_CHG_IC_FUNC_DEF(func_id) \
	typedef int (*func_id##_T)

#define oplus_chg_ic_func(ic, func_id, ...) ({				\
	func_id##_T _func = (ic && ic->get_func) ?			\
		ic->get_func(ic, func_id) : NULL;			\
	_func ? _func(ic, ##__VA_ARGS__) : -ENOTSUPP;			\
})

#define OPLUS_CHG_IC_FUNC_CHECK(func_id, func) ({		\
	__always_unused func_id##_T _func = func;		\
	(void *)func;						\
})

#define oplus_print_ic_err(msg)						\
	chg_err("[IC_ERR][%s]-[%s]-[%d]:%s\n", msg->ic->name,		\
		oplus_chg_ic_err_text(msg->type), msg->sub_type, msg->msg);

OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_INIT)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_EXIT)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_REG_DUMP)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SMT_TEST)(struct oplus_chg_ic_dev *, char buf[], int len);
/* buck/boost */
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_ICL)(struct oplus_chg_ic_dev *, bool, bool, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_ICL)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_FCC)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_FV)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_ITERM)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_AICL_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_AICL_RERUN)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_AICL_RESET)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_HW_DETECT)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_RERUN_BC12)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG)(struct oplus_chg_ic_dev *, enum oplus_chg_qc_version, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG)(struct oplus_chg_ic_dev *, u32);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_CC_DETECT_HAPPENED)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_CURR_DROP)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_WDT_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_KICK_WDT)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_BC12_COMPLETED)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_AICL_POINT)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_SET_VINDPM)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BUCK_HARDWARE_INIT)(struct oplus_chg_ic_dev *);

/* gauge */
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_UPDATE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL)(struct oplus_chg_ic_dev *, int, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_FC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_QM)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_PD)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_PC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_QS)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_SET_LOCK)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_IS_LOCKED)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_IS_SUSPEND)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS)(struct oplus_chg_ic_dev *, char *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS)(struct oplus_chg_ic_dev *, char *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS)(struct oplus_chg_ic_dev *, char *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS)(struct oplus_chg_ic_dev *, const char *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_CHECK_RESET)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GAUGE_SET_RESET)(struct oplus_chg_ic_dev *, bool *);

/* misc */
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_CHARGER_CYCLE)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_OTG_BOOST_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_OTG_BOOST_VOL)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_WLS_BOOST_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_WLS_BOOST_VOL)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_SHUTDOWN_SOC)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_BACKUP_SOC)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_USB_TEMP)(struct oplus_chg_ic_dev *, int *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_USB_TEMP_VOLT)(struct oplus_chg_ic_dev *, int *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_USB_TEMP_CHECK_IS_SUPPORT)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_TYPEC_MODE)(struct oplus_chg_ic_dev *, enum oplus_chg_typec_port_role_type *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_TYPEC_MODE)(struct oplus_chg_ic_dev *, enum oplus_chg_typec_port_role_type);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_USB_DISCHG_STATUS)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_OTG_ONLINE_STATUS)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_OTG_ENABLE)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_CHARGER_VOL_MAX)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_CHARGER_VOL_MIN)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_CHARGER_CURR_MAX)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_DISABLE_VBUS)(struct oplus_chg_ic_dev *, bool, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_IS_OPLUS_SVID)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_GET_DATA_ROLE)(struct oplus_chg_ic_dev *, int *);

/* voocphy */
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOCPHY_ENABLE)(struct oplus_chg_ic_dev *, bool);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP)(struct oplus_chg_ic_dev *, int);

/* vooc */
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_FW_UPGRADE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE)(struct oplus_chg_ic_dev *, const u8 *, u32);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_FW_VERSION)(struct oplus_chg_ic_dev *, u32 *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_EINT_REGISTER)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_EINT_UNREGISTER)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_SET_DATA_ACTIVE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_SET_DATA_SLEEP)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE)(struct oplus_chg_ic_dev *, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_DATA_GPIO_VAL)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_RESET_GPIO_VAL)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_CLOCK_GPIO_VAL)(struct oplus_chg_ic_dev *, int *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_READ_DATA_BIT)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_REPLY_DATA)(struct oplus_chg_ic_dev *, int, int);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_RESET_ACTIVE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_RESET_ACTIVE_FORCE)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_RESET_SLEEP)(struct oplus_chg_ic_dev *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_UPGRADING)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS)(struct oplus_chg_ic_dev *, bool *);
OPLUS_CHG_IC_FUNC_DEF(OPLUS_IC_FUNC_VOOC_GET_IC_DEV)(struct oplus_chg_ic_dev *, struct oplus_chg_ic_dev **);

static inline void *oplus_chg_ic_get_drvdata(const struct oplus_chg_ic_dev *ic_dev)
{
	return dev_get_drvdata(ic_dev->dev);
}

void oplus_chg_ic_list_lock(void);
void oplus_chg_ic_list_unlock(void);
struct oplus_chg_ic_dev *oplsu_chg_ic_find_by_name(const char *name);
struct oplus_chg_ic_dev *of_get_oplus_chg_ic(struct device_node *node, const char *prop_name, int index);
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
int oplus_chg_ic_reg_dump(struct oplus_chg_ic_dev *ic_dev);
int oplus_chg_ic_reg_dump_by_name(const char *name);
void oplus_chg_ic_reg_dump_all(void);
#endif
int oplus_chg_ic_virq_register(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_virq_id virq_id,
			       virq_handler_t handler,
			       void *virq_data);
int oplus_chg_ic_virq_release(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id,
			      void *virq_data);
int oplus_chg_ic_virq_trigger(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id);
int oplus_chg_ic_creat_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_err err_type, int sub_err_type,
			       const char *format, ...);
int oplus_chg_ic_move_err_msg(struct oplus_chg_ic_dev *dest,
			      struct oplus_chg_ic_dev *src);
int oplus_chg_ic_clean_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       struct oplus_chg_ic_err_msg *err_msg);
const char *oplus_chg_ic_err_text(enum oplus_chg_ic_err err_type);
struct oplus_chg_ic_dev *oplus_chg_ic_register(struct device *dev,
	struct oplus_chg_ic_cfg *cfg);
int oplus_chg_ic_unregister(struct oplus_chg_ic_dev *ic_dev);
struct oplus_chg_ic_dev *devm_oplus_chg_ic_register(struct device *dev,
	struct oplus_chg_ic_cfg *cfg);
int devm_oplus_chg_ic_unregister(struct device *dev, struct oplus_chg_ic_dev *ic_dev);

int oplus_chg_ic_func_table_sort(enum oplus_chg_ic_func *func_table, int func_num);
bool oplus_chg_ic_func_is_support(enum oplus_chg_ic_func *func_table, int func_num, enum oplus_chg_ic_func func_id);
bool oplus_chg_ic_virq_is_support(enum oplus_chg_ic_virq_id *virq_table, int virq_num, enum oplus_chg_ic_virq_id virq_id);

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
bool oplus_chg_ic_debug_data_check(const void *buf, size_t len);
int oplus_chg_ic_get_item_num(const void *buf, size_t len);
int oplus_chg_ic_get_item_data(const void *buf, int index);
void *oplus_chg_ic_get_item_data_addr(void *buf, int index);
void oplus_chg_ic_debug_data_init(void *buf, int argc);
int oplus_chg_ic_debug_str_data_init(void *buf, int len);
inline size_t oplus_chg_ic_debug_data_size(int argc);
struct oplus_chg_ic_overwrite_data *
oplus_chg_ic_get_overwrite_data(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id);
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

#endif /* __OPLUS_CHG_IC_H__ */
