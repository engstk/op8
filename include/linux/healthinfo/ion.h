/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#ifndef __LINUX_ION_H__
#define __LINUX_ION_H__

extern atomic_long_t ion_total_size;
extern bool ion_cnt_enable;
extern unsigned long ion_total(void);

#endif /* __LINUX_ION_H__ */

