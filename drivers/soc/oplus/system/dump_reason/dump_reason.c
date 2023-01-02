// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** File : dump_reason.c
** Description : dump reason feature
** Version : 1.0
******************************************************************/

#include <linux/string.h>
#include <linux/kallsyms.h>
#include <linux/soc/qcom/smem.h>
#include <soc/oplus/system/dump_reason.h>
#include <soc/oplus/system/device_info.h>

static char caller_function_name[KSYM_SYMBOL_LEN];
static struct dump_info *dp_info;

char *parse_function_builtin_return_address(unsigned long function_address)
{
	char *cur = caller_function_name;

	if (!function_address)
		return NULL;

	sprint_symbol(caller_function_name, function_address);
	strsep(&cur, "+");
	return caller_function_name;
}
EXPORT_SYMBOL(parse_function_builtin_return_address);

void save_dump_reason_to_smem(char *info, char *function_name)
{
	int strlinfo = 0, strlfun = 0;
	size_t size;
	static int flag = 0;

	/* Make sure save_dump_reason_to_smem() is not
	called infinite times by nested panic caller fns etc*/
	if (flag > 1)
		return;

	dp_info = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_DUMP_INFO,&size);

	if (IS_ERR_OR_NULL(dp_info)) {
		pr_debug("%s: get dp_info failure\n", __func__);
		return;
	}
	else {
		pr_debug("%s: info : %s\n",__func__, info);

		strlinfo = strlen(info)+1;
		strlfun  = strlen(function_name)+1;
		strlinfo = strlinfo  <  DUMP_REASON_SIZE ? strlinfo : DUMP_REASON_SIZE;
		strlfun  = strlfun <  DUMP_REASON_SIZE ? strlfun: DUMP_REASON_SIZE;
		if ((strlen(dp_info->dump_reason) + strlinfo) < DUMP_REASON_SIZE)
			strncat(dp_info->dump_reason, info, strlinfo);

		if (function_name != NULL &&
			((strlen(dp_info->dump_reason) + strlfun + sizeof("\r\n")+1) < DUMP_REASON_SIZE)) {
			strncat(dp_info->dump_reason, "\r\n", sizeof("\r\n"));
			strncat(dp_info->dump_reason, function_name, strlfun);
		}

		pr_debug("\r%s: dump_reason : %s strl=%d function caused panic :%s strl1=%d \n", __func__,
				dp_info->dump_reason, strlinfo, function_name, strlfun);
		save_dump_reason_to_device_info(dp_info->dump_reason);
		flag++;
	}
}

EXPORT_SYMBOL(save_dump_reason_to_smem);

void dump_reason_init_smem(void)
{
    int ret;

    ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY,SMEM_DUMP_INFO,
                                  sizeof(struct dump_info));

    if (ret < 0 && ret != -EEXIST) {
          pr_err("%s:unable to allocate dp_info \n", __func__);
          return;
    }
}

static int __init dump_reason_init(void)
{
	dump_reason_init_smem();
	return 0;
}

module_init(dump_reason_init);
MODULE_LICENSE("GPL v2");
