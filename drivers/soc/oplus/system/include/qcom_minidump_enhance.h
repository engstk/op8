/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef _QCOM_MINIDUMP_CUSTOMIZED_H_
#define _QCOM_MINIDUMP_CUSTOMIZED_H_

#include <linux/reboot.h>

bool is_fulldump_enable(void);
void oplus_switch_fulldump(int on);
void do_restart_early(enum reboot_mode reboot_mode, const char *cmd);
void do_poweroff_early(void);
void dumpcpuregs(struct pt_regs *pt_regs);
void register_cpu_contex(void);

#endif /* _QCOM_MINIDUMP_CUSTOMIZED_H_ */

