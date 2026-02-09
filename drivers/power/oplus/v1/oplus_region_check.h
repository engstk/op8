// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */
#ifndef _OPLUS_REGION_CHECK_
#define _OPLUS_REGION_CHECK_
#include "oplus_charger.h"

bool third_pps_supported_from_nvid(void);
bool third_pps_priority_than_svooc(void);
void oplus_chg_region_check_init(struct oplus_chg_chip *chip);

#endif /*_OPLUS_REGION_CHECK_*/
