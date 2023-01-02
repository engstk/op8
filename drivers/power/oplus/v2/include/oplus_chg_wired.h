#ifndef __OPLUS_CHG_WIRED_H__
#define __OPLUS_CHG_WIRED_H__

#ifdef CONFIG_OPLUS_CHG_DYNAMIC_CONFIG
#include <oplus_chg_cfg.h>
#endif

enum oplus_wired_charge_mode {
	OPLUS_WIRED_CHG_MODE_UNKNOWN,
	OPLUS_WIRED_CHG_MODE_SDP,
	OPLUS_WIRED_CHG_MODE_CDP,
	OPLUS_WIRED_CHG_MODE_DCP,
	OPLUS_WIRED_CHG_MODE_VOOC,
	OPLUS_WIRED_CHG_MODE_QC,
	OPLUS_WIRED_CHG_MODE_PD,
	OPLUS_WIRED_CHG_MODE_MAX,
};

enum oplus_wired_action {
	OPLUS_ACTION_NULL,
	OPLUS_ACTION_BOOST,
	OPLUS_ACTION_BUCK,
};

enum oplus_wired_vbus_vol {
	OPLUS_VBUS_5V,
	OPLUS_VBUS_9V,
	OPLUS_VBUS_MAX,
};

const char *oplus_wired_get_chg_type_str(enum oplus_chg_usb_type);
const char *oplus_wired_get_chg_mode_region_str(enum oplus_wired_charge_mode);
#ifdef CONFIG_OPLUS_CHG_DYNAMIC_CONFIG
int oplus_wired_set_config(struct oplus_mms *topic, struct oplus_chg_param_head *param_head);
#endif

#endif /* __OPLUS_CHG_WIRED_H__ */
