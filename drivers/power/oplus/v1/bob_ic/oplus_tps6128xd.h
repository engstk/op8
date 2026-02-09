/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_TP6S128XD_H__

#define __OPLUS_TP6S128XD_H__

#define OPLUS_TP6S128XD

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int tps6128xd_subsys_init(void);
void tps6128xd_subsys_exit(void);
#endif
int tps6128xd_get_status_reg(void);
int tps6128xd_set_high_batt_vol(int val);
int tps6128xd_get_status(void);

#endif /*__OPLUS_MP2650_H__*/
