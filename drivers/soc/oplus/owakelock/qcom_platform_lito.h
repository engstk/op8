/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __QCOM_PLATFORM_LITO_H__
#define __QCOM_PLATFORM_LITO_H__

#include "oplus_wakelock_profiler.h"

static struct wakeup_count_desc_t wc_powerkey_lito = {
	.module_name = "powerkey",
	.module_mask = WS_CNT_POWERKEY,
	.ws_number = 1,
	.ws_desc[0] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_POWERKEY,
	},
};

static struct wakeup_count_desc_t wc_rtc_alarm_lito = {
	.module_name = "rtc_alarm",
	.module_mask = WS_CNT_ALARM,
	.ws_number = 1,
	.ws_desc[0] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_ALARM,
	},
};

static struct wakeup_count_desc_t wc_modem_lito = {
	.module_name = "modem",
	.module_mask = WS_CNT_MODEM,
	.ws_number = 4,
	.ws_desc[0] = {
		.prop = IRQ_PROP_EXCHANGE,
		.name = IRQ_NAME_MODEM_QSI,
	},
	.ws_desc[1] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_MODEM_GLINK,
	},
	.ws_desc[2] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_MODEM_IPA,
	},
	.ws_desc[3] = {
		.prop = IRQ_PROP_EXCHANGE,
		.name = IRQ_NAME_MODEM_QMI,
	},
};

static struct wakeup_count_desc_t wc_wlan_lito = {
	.module_name = "wlan",
	.module_mask = WS_CNT_WLAN,
	.ws_number = 2,
	.ws_desc[0] = {
		.prop = IRQ_PROP_EXCHANGE,
		.name = IRQ_NAME_WLAN_MSI,
	},
	.ws_desc[1] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_WLAN_IPCC_DATA,
	},
};

static struct wakeup_count_desc_t wc_adsp_lito = {
	.module_name = "adsp",
	.module_mask = WS_CNT_ADSP,
	.ws_number = 2,
	.ws_desc[0] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_ADSP,
	},
	.ws_desc[1] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_ADSP_GLINK,
	},
};

static struct wakeup_count_desc_t wc_cdsp_lito = {
	.module_name = "cdsp",
	.module_mask = WS_CNT_CDSP,
	.ws_number = 2,
	.ws_desc[0] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_CDSP,
	},
	.ws_desc[1] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_CDSP_GLINK,
	},
};

static struct wakeup_count_desc_t wc_slpi_lito = {
	.module_name = "slpi",
	.module_mask = WS_CNT_SLPI,
	.ws_number = 2,
	.ws_desc[0] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_SLPI,
	},
	.ws_desc[1] = {
		.prop = IRQ_PROP_REAL,
		.name = IRQ_NAME_SLPI_GLINK,
	},
};


static struct wakeup_count_desc_t wc_glink_lito = {
	.module_name = "glink", /* all platform should be the same */
	.module_mask = WS_CNT_GLINK,
	.ws_number = 1,
	.ws_desc[0] = {
		.prop = IRQ_PROP_DUMMY_STATICS,
		.name = IRQ_NAME_GLINK,
	},
};

static struct wakeup_count_desc_t wc_abort_lito = {
	.module_name = "abort", /* all platform should be the same */
	.module_mask = WS_CNT_ABORT,
	.ws_number = 1,
	.ws_desc[0] = {
		.prop = IRQ_PROP_DUMMY_STATICS,
		.name = IRQ_NAME_ABORT,
	},
};

static struct wakeup_count_desc_t wc_sum_lito = {
	.module_name = "wakeup_sum", /* all platform should be the same */
	.module_mask = WS_CNT_SUM,
	.ws_number = 1,
	.ws_desc[0] = {
		.prop = IRQ_PROP_DUMMY_STATICS,
		.name = IRQ_NAME_WAKE_SUM,
	},
};

/*
 * Abstract each statistical item as a wake-up source
 * interrupt, including sum statistics and abort statistics.
 * The prop property of ws_desc_t determines the statistical
 * behavior of this "wake source interrupt".
 * Each wakeup_count_desc_t represents a statistical module.
 */
static struct wakeup_count_desc_t * const all_modules_lito[] = {
	&wc_powerkey_lito,
	&wc_rtc_alarm_lito,
	/*&wc_alarm_lito,*/
	&wc_modem_lito,
	&wc_wlan_lito,
	&wc_adsp_lito,
	&wc_cdsp_lito,
	&wc_slpi_lito,
	&wc_glink_lito,
	&wc_abort_lito,
	&wc_sum_lito,
	NULL,
};

/*
 * The upper layer requires report IPA_WS/QMI_WS,
 * so a conversion must be done.
 * [x][0] --> IPA_WS, [x][1] --> QMI_WS
 */
static const char* const modem_report_exchange_lito[][MODEM_REPORT_NUMBER] = {
	{IRQ_NAME_MODEM_IPA, IRQ_NAME_MODEM_QMI},
	{NULL, NULL},
};

#endif /*__QCOM_PLATFORM_LITO_H__*/