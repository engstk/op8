// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/soc/qcom/smem.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/err.h>

#include "oplus_pmic_info.h"

#define SMEM_PMIC_INFO	134

static void *format = NULL;

void init_pmic_history_smem(void)
{
	size_t smem_size=0;

	format = qcom_smem_get(QCOM_SMEM_HOST_ANY,
			SMEM_PMIC_INFO,
			&smem_size);

    if (IS_ERR(format) || !smem_size) {
        format = NULL;
    }
}

void *get_pmic_history(void)
{
        if (format == NULL) {
            init_pmic_history_smem();
        }
        return format;
}

MODULE_DESCRIPTION("oplus ocp status");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lee <lijiang>");