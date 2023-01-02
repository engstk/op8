// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/init.h>
#include <linux/page-isolation.h>
#include <linux/memory_isolate.h>
#include <linux/version.h>


#define SZ_1G_PAGES (SZ_1G >> PAGE_SHIFT)

#define TOTALRAM_2GB (2*SZ_1G_PAGES)
#define TOTALRAM_3GB (3*SZ_1G_PAGES)
#define TOTALRAM_4GB (4*SZ_1G_PAGES)
#define TOTALRAM_6GB (6*SZ_1G_PAGES)
#define TOTALRAM_8GB (6*SZ_1G_PAGES)

#define OPLUS2_ORDER 2

static const int oplus2_reserve[] = {
	12, /*2GB<===>48MB**/
	13, /*3GB<===>52MB*/
	19, /*4GB<===>76MB*/
	21, /*6GB<===>84MB*/
	22, /*8GB<===>88MB*/
};

enum totalram_index {
 TOTALRAM_2GB_INDEX = 0,
 TOTALRAM_3GB_INDEX,
 TOTALRAM_4GB_INDEX,
 TOTALRAM_6GB_INDEX,
 TOTALRAM_8GB_INDEX,
};

static int pageblock_is_reserved(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		if (!pfn_valid_within(pfn) || PageReserved(pfn_to_page(pfn)))
			return 1;
	}
	return 0;
}

 static unsigned long config_migrate_oplus(int reserve_migratetype)
{
	int index;

	if (totalram_pages <= TOTALRAM_2GB)
		index = TOTALRAM_2GB_INDEX;
	else if (totalram_pages <= TOTALRAM_3GB)
		index = TOTALRAM_3GB_INDEX;
	else if (totalram_pages <= TOTALRAM_4GB)
		index = TOTALRAM_4GB_INDEX;
	else if (totalram_pages <= TOTALRAM_6GB)
		index = TOTALRAM_6GB_INDEX;
	else
		index = TOTALRAM_8GB_INDEX;

	if (reserve_migratetype == MIGRATE_OPLUS2)
		return oplus2_reserve[index];
	else
		return 0;
}

/*
 * Mark a number of pageblocks as MIGRATE_OPLUS.
 * The memory withinthe reserve will tend to store contiguous free pages.
 */
void setup_zone_migrate_oplus(struct zone *zone, int reserve_migratetype)
{
	unsigned long start_pfn, pfn, end_pfn, block_end_pfn;
	struct page *page;
	unsigned long block_migratetype;
	unsigned long reserve;
	unsigned long old_reserve;
	int pages_moved = 0;
	enum zone_stat_item item;

	/*
	 * Get the start pfn, end pfn and the number of blocks to reserve
	 * We have to be careful to be aligned to pageblock_nr_pages to
	 * make sure that we always check pfn_valid for the first page in
	 * the block.
	 */
	start_pfn = zone->zone_start_pfn;
	end_pfn = zone_end_pfn(zone);
	start_pfn = roundup(start_pfn, pageblock_nr_pages);

	/* fix me. reserve should be limited based on wmark.
	reserve = roundup(min_wmark_pages(zone), pageblock_nr_pages) >>
							pageblock_order;
	reserve = min((unsigned long)MIGRATE_OPLUS_PAGE_BLOCKS, reserve);
	*/
	if (reserve_migratetype == MIGRATE_OPLUS2) {
		reserve = config_migrate_oplus(MIGRATE_OPLUS2);
		old_reserve = zone->nr_migrate_oplus2_block;
		item = NR_FREE_OPLUS2_PAGES;
	}
	else {
		reserve = 0;
		old_reserve = 0;
	}

#ifdef CONFIG_ZONE_DMA
	/* only reserve for DMA zone */
	if (strncmp(zone->name, "DMA", 3))
		reserve = 0;
#else
	/*reserve for normal zone if there is no DMA zone*/
	if (strncmp(zone->name, "Normal", 6))
		reserve = 0;
#endif /*CONFIG_ZONE_DMA*/
	/* When memory hot-add, we almost always need to do nothing */
	if (reserve == old_reserve)
		return;

	if (reserve_migratetype == MIGRATE_OPLUS2)
		zone->nr_migrate_oplus2_block = reserve;

	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);

		/* Watch out for overlapping nodes */
		if (page_to_nid(page) != zone_to_nid(zone))
			continue;

		block_migratetype = get_pageblock_migratetype(page);

		/* Only test what is necessary when the reserves are not met */
		if (reserve > 0) {
			/*
			 * Blocks with reserved pages will never free, skip
			 * them.
			 */
			block_end_pfn = min(pfn + pageblock_nr_pages, end_pfn);
			if (pageblock_is_reserved(pfn, block_end_pfn))
				continue;

			/* If this block is reserved, account for it */
			if (block_migratetype == reserve_migratetype) {
				reserve--;
				continue;
			}

			/* Suitable for reserving if this block is movable */
			if (block_migratetype == MIGRATE_MOVABLE) {
				set_pageblock_migratetype(page,
							reserve_migratetype);
#if(LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0))
				pages_moved = move_freepages_block(zone, page,
							reserve_migratetype,NULL);
#else
				pages_moved = move_freepages_block(zone, page,
							reserve_migratetype);
#endif
				__mod_zone_page_state(zone, item,
						pages_moved);
				reserve--;
				continue;
			}
		} else if (!old_reserve) {
			/*
			 * At boot time we don't need to scan the whole zone
			 * for turning off MIGRATE_RESERVE.
			 */
			break;
		}

		/*
		 * If the reserve is met and this is a previous reserved block,
		 * take it back
		 */
		if (block_migratetype == reserve_migratetype) {
			set_pageblock_migratetype(page, MIGRATE_MOVABLE);
#if(LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0))
			pages_moved = move_freepages_block(zone, page, MIGRATE_MOVABLE,NULL);
#else
			pages_moved = move_freepages_block(zone, page, MIGRATE_MOVABLE);
#endif
			__mod_zone_page_state(zone, item,
						-pages_moved);
		}
	}
}
