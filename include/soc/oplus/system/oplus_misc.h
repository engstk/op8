/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
 * oem_force_dump.h
 *
 * header file supporting debug functions.
 *
 */

#ifndef OPLUS_MISC_H
#define OPLUS_MISC_H

extern void oplus_switch_fulldump(int on);
extern int dsi_panel_backlight_get(void);
extern void send_sig_to_get_trace(char *name);

#endif
