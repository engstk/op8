// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "touchpanel_exception.h"

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <asm/current.h>
#include <linux/version.h>

#include "touchpanel_common.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_LOG_CORE)

#include <soc/oplus/system/olc.h>

#define TP_MODULE_NAME         "touchpanel"
#define TP_LOG_PATH            "kernel/tp_debug_info"
#define TP_MODULE_ID           1
#define TP_RESERVED_ID         256

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "touchpanel"
#else
#define TPD_DEVICE "touchpanel"
#endif

#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)

static inline void *tp_kzalloc(size_t size, gfp_t flags)
{
	void *p;

	p = kzalloc(size, flags);

	if (!p) {
		TPD_INFO("%s: Failed to allocate memory\n", __func__);
		/*add for health monitor*/
	}

	return p;
}

static inline int tp_memcpy(void *dest, unsigned int dest_size,
			    void *src, unsigned int src_size,
			    unsigned int count)
{
	if (dest == NULL || src == NULL) {
		return -EINVAL;
	}

	if (count > dest_size || count > src_size) {
		TPD_INFO("%s: src_size = %d, dest_size = %d, count = %d\n",
			 __func__, src_size, dest_size, count);
		return -EINVAL;
	}

	memcpy((void *)dest, (void *)src, count);

	return 0;
}

static inline void tp_kfree(void **mem)
{
	if (*mem != NULL) {
		kfree(*mem);
		*mem = NULL;
	}
}

static int tp_olc_raise_exception(tp_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	struct exception_info *exp_info = NULL;
	int ret = -1;

	TPD_INFO("%s:enter,type:%d\n", __func__ , excep_tpye);

	exp_info = tp_kzalloc(sizeof(struct exception_info), GFP_KERNEL);

	if (!exp_info) {
		return -ENOMEM;
	}

	if (excep_tpye > 0xfff) {
		TPD_INFO("%s: excep_tpye:%d is beyond 0xfff\n", __func__ , excep_tpye);
		goto free_exp;
	}
	exp_info->time = 0;
	exp_info->id = (TP_RESERVED_ID << 20) | (TP_MODULE_ID << 12) | excep_tpye;
	exp_info->pid = 0;
	exp_info->exceptionType = EXCEPTION_KERNEL;
	exp_info->faultLevel = 0;
	exp_info->logOption = LOG_KERNEL | LOG_MAIN;
	tp_memcpy(exp_info->module, sizeof(exp_info->module),
		TP_MODULE_NAME, sizeof(TP_MODULE_NAME), sizeof(TP_MODULE_NAME));
	tp_memcpy(exp_info->logPath, sizeof(exp_info->logPath),
		TP_LOG_PATH, sizeof(TP_LOG_PATH), sizeof(TP_LOG_PATH));

	tp_memcpy(exp_info->summary, sizeof(exp_info->summary),
		summary, summary_size, summary_size);

	ret = olc_raise_exception(exp_info);
	if (ret) {
		TPD_INFO("%s: raise fail, ret:%d\n", __func__ , ret);
	}

free_exp:
	tp_kfree((void **)&exp_info);
	return ret;
}
#else
static  int tp_olc_raise_exception(tp_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	return 0;
}
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD_DELETE */


int tp_exception_report(void *tp_exception_data, tp_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	int ret = -1;

	struct exception_data *exception_data = (struct exception_data *)tp_exception_data;

	if (!exception_data || !exception_data->exception_upload_support) {
		return 0;
	}
	exception_data->exception_upload_count++;
	switch (excep_tpye) {
	case EXCEP_BUS:
		/*bus error upload tow times*/
		exception_data->bus_error_upload_count++;
		if (exception_data->bus_error_count > MAX_BUS_ERROR_COUNT
				&& exception_data->bus_error_upload_count < 3) {
			exception_data->bus_error_count = 0;
			ret = tp_olc_raise_exception(excep_tpye, summary, summary_size);
		}
		break;
	default:
		ret = tp_olc_raise_exception(excep_tpye, summary, summary_size);
		break;
	}

	return ret;
}
EXPORT_SYMBOL(tp_exception_report);

