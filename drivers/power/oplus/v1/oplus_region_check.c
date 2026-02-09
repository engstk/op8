// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */

#include <linux/of.h>
#include <linux/module.h>
#include "oplus_region_check.h"

static int dbg_nvid = 0x00;
module_param(dbg_nvid, int, 0644);
MODULE_PARM_DESC(dbg_nvid, "debug nvid for autotest");

#define PPS_REGION_COUNT_MAX 16

struct region_list {
	int elem_count;
	u32 pps_region_list[PPS_REGION_COUNT_MAX];
};

enum region_list_index {
	PPS_SUPPORT_REGION,
	PPS_PRIORITY_REGION,
	REGION_INDEX_MAX,
};

static u8 region_id = 0xFF;

static const char *const dts_region_id_list[REGION_INDEX_MAX] = {
	/* support third pps, EU/JP... */
	[PPS_SUPPORT_REGION] = "oplus,pps_region_list",
	/* pd_svooc adapter works preferentially in pps mode, EU */
	[PPS_PRIORITY_REGION] = "oplus,pps_priority_list",
};

static struct region_list region_list_arrry[REGION_INDEX_MAX] = {
	{ 0, { 0x00 } },
	{ 0, { 0x00 } },
};

static bool get_regionid_from_cmdline(void)
{
	struct device_node *np;
	const char *bootparams = NULL;
	char *str;
	int temp_region = 0;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (np) {
		ret = of_property_read_string(np, "bootargs", &bootparams);
		if (!bootparams || ret < 0) {
			chg_err("failed to get bootargs property\n");
			return false;
		}

		str = strstr(bootparams, "oplus_region=");
		if (str) {
			str += strlen("oplus_region=");
			ret = get_option(&str, &temp_region);
			if (ret == 1)
				region_id = temp_region & 0xFF;
			else
				region_id = 0x00;
			chg_err("oplus_region=0x%02x\n", region_id);
			return true;
		}
	}
	return false;
}

static void oplus_parse_pps_region_list(struct oplus_chg_chip *chip)
{
	int len = 0;
	int rc = 0;
	int i = 0;
	struct device_node *node;

	node = chip->dev->of_node;

	for (i = 0; i < REGION_INDEX_MAX; i++) {
		rc = of_property_count_elems_of_size(node, dts_region_id_list[i], sizeof(u32));
		if (rc >= 0) {
			len = rc <= PPS_REGION_COUNT_MAX ? rc : PPS_REGION_COUNT_MAX;
			rc = of_property_read_u32_array(node, dts_region_id_list[i],
							region_list_arrry[i].pps_region_list, len);
			if (rc < 0) {
				len = 0;
				chg_err("parse %s failed, rc=%d", dts_region_id_list[i], rc);
			}
		} else {
			len = 0;
			chg_err("parse %s_length failed, rc=%d", dts_region_id_list[i], rc);
		}
		region_list_arrry[i].elem_count = len;
		for (len = 0; len < region_list_arrry[i].elem_count; len++)
			chg_err("%s[%d]=0x%02x", dts_region_id_list[i], len, region_list_arrry[i].pps_region_list[len]);
	}
}

static bool find_id_in_region_list(u8 id, enum region_list_index list_index)
{
	int index = 0;
	u8 region_id_tmp = 0;
	u32 *id_list = NULL;
	int list_length = 0;

	if (id == 0xFF)
		return false;

	if (list_index >= PPS_SUPPORT_REGION && list_index < REGION_INDEX_MAX) {
		id_list = region_list_arrry[list_index].pps_region_list;
		list_length = region_list_arrry[list_index].elem_count;
	}
	if (list_length == 0 || id_list == NULL)
		return false;

	while (index < list_length) {
		region_id_tmp = id_list[index] & 0xFF;
		chg_err("pps_list[%d]=0x%x", index, id_list[index]);
		if (id == region_id_tmp) {
			return true;
		}
		index++;
	}

	return false;
}

bool third_pps_supported_from_nvid(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	return find_id_in_region_list(region_temp, PPS_SUPPORT_REGION);
}

bool third_pps_priority_than_svooc(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	return find_id_in_region_list(region_temp, PPS_PRIORITY_REGION);
}

void oplus_chg_region_check_init(struct oplus_chg_chip *chip)
{
	if (chip == NULL)
		return;

	if (get_regionid_from_cmdline())
		oplus_parse_pps_region_list(chip);
}
