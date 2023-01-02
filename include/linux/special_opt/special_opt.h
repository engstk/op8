/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _SPECIAL_OPT_H
#define _SPECIAL_OPT_H
#define HEAVY_LOAD_RUNTIME (1024000000)
#define HEAVY_LOAD_SCALE (80)
extern bool is_heavy_load_task(struct task_struct *p);
extern bool specopt_skip_balance(void);
#endif /* _SPECIAL_OPT_H */
