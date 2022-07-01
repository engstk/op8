/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __MULTI_FREEAREA_H__
#define __MULTI_FREEAREA_H__

#define HIGH_ORDER_TO_FLC 3

extern const struct file_operations proc_free_area_fops;

extern void list_sort_add(struct page *page, struct zone *zone, unsigned int order, int mt);
extern int page_to_flc(struct page *page);
extern void ajust_zone_label(struct zone *zone);
extern unsigned int ajust_flc(unsigned int current_flc, unsigned int order);

#endif //__MULTI_FREEAREA_H__
