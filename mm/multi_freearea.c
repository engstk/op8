// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/kasan.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/oom.h>
#include <linux/topology.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/memory_hotplug.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/mempolicy.h>
#include <linux/memremap.h>
#include <linux/stop_machine.h>
#include <linux/sort.h>
#include <linux/pfn.h>
#include <linux/backing-dev.h>
#include <linux/fault-inject.h>
#include <linux/page-isolation.h>
#include <linux/page_ext.h>
#include <linux/debugobjects.h>
#include <linux/kmemleak.h>
#include <linux/compaction.h>
#include <trace/events/kmem.h>
#include <trace/events/oom.h>
#include <linux/prefetch.h>
#include <linux/mm_inline.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/sched/rt.h>
#include <linux/sched/mm.h>
#include <linux/page_owner.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/ftrace.h>
#include <linux/lockdep.h>
#include <linux/nmi.h>
#include <linux/psi.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/vmstat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/math64.h>
#include <linux/writeback.h>
#include "internal.h"
#include "multi_freearea.h"
#include "internal.h"

static unsigned int show_order = 0;
#define SHOW_ALL (11)

static char * const zone_names[MAX_NR_ZONES] = {
#ifdef CONFIG_ZONE_DMA
	 "DMA",
#endif
#ifdef CONFIG_ZONE_DMA32
	 "DMA32",
#endif
	 "Normal",
#ifdef CONFIG_HIGHMEM
	 "HighMem",
#endif
	 "Movable",
#ifdef CONFIG_ZONE_DEVICE
	 "Device",
#endif
};

static int proc_free_area_show(struct seq_file *m, void *p)
{
	unsigned int order, t, flc;
	pg_data_t *pgdat = NODE_DATA(0);
    struct page *page;
	int zone_type;


    for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++) {
        struct zone *zone = &pgdat->node_zones[zone_type];
    
        if (!managed_zone(zone)) {
            continue;
        }
        seq_printf(m, "---------------------------------------------------------------------------------------------------------------\n");
        seq_printf(m, "zone_name = %s, show_order = %u\n", zone_names[zone_type], show_order);
        for (flc = 0; flc < FREE_AREA_COUNTS; flc++)
            seq_printf(m, "[%d]: label = %lu, segment = %lu\n", flc, zone->zone_label[flc].label, zone->zone_label[flc].segment);
        seq_printf(m, "\n---------------------------------------------------------------------------------------------------------------\n");
        for (flc = 0; flc < FREE_AREA_COUNTS; flc++) {
            seq_printf(m, "flc = %u\n", flc);
            for_each_migratetype_order(order, t) {
                if (order == show_order || show_order == SHOW_ALL) {
                    seq_printf(m, "order = %u, mt = %u\n", order, t);
                    list_for_each_entry(page, &(zone->free_area[flc][order].free_list[t]), lru) {
                        seq_printf(m, "%lu\t", page_to_pfn(page));
                    }
                    seq_printf(m, "\n");
                }
            }
        }
    }
    
   return 0; 
}

static ssize_t proc_free_area_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
    char write_data[16] = {0};
    int ret = 0;

    if (copy_from_user(&write_data, buff, len)) {
        return -EFAULT;
    }
    ret = kstrtouint(write_data, 10, &show_order);

    return len;
}

static int proc_free_area_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_free_area_show, NULL);
}

const struct file_operations proc_free_area_fops = {
    .open       = proc_free_area_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
    .write      = proc_free_area_write,
};


void list_sort_add(struct page *page, struct zone *zone, unsigned int order, int mt)
{
    struct list_head *list = &(zone->free_area[0][order].free_list[mt]);
    unsigned long pfn = 0, segment = 0;
    int i = 0;
    
    pfn = page_to_pfn(page);

    if (unlikely(pfn > zone->zone_label[FREE_AREA_COUNTS - 1].label)) {
        list = &(zone->free_area[FREE_AREA_COUNTS - 1][order].free_list[mt]);
        segment = zone->zone_label[FREE_AREA_COUNTS - 1].segment;
        goto add_page;
    }

    for (i = 0; i < FREE_AREA_COUNTS; i++) { 
        if (pfn <= zone->zone_label[i].label) {
            list = &(zone->free_area[i][order].free_list[mt]);
            segment = zone->zone_label[i].segment;
            break;
        }
    }

add_page:
    if (pfn >= segment)
        list_add_tail(&page->lru, list);
    else
        list_add(&page->lru, list);
}

int page_to_flc(struct page *page)
{
    struct zone *zone = page_zone(page);
    unsigned long pfn = page_to_pfn(page);
    int flc = 0;
 
    if (unlikely(pfn > zone->zone_label[FREE_AREA_COUNTS - 1].label))
        return FREE_AREA_COUNTS - 1;

    for (flc = 0; flc < FREE_AREA_COUNTS; flc++) {
        if (pfn <= zone->zone_label[flc].label)
            return flc;
    }

    return flc;
}

void ajust_zone_label(struct zone *zone)
{
    int i;
    unsigned long prev_base;

    for (i = 0; i < FREE_AREA_COUNTS; i++) {
        zone->zone_label[i].label = zone->zone_start_pfn + zone->spanned_pages * (i + 1) / FREE_AREA_COUNTS;
	}

    for (i = 0; i < FREE_AREA_COUNTS; i++) {
        if (i == 0)
            prev_base = zone->zone_start_pfn;
        else
            prev_base = zone->zone_label[i - 1].label;

        zone->zone_label[i].segment = prev_base +
            ((zone->zone_label[i].label - prev_base) >> 1);
    }
}

unsigned int ajust_flc(unsigned int current_flc, unsigned int order)
{
    /* when alloc_order >= HIGH_ORDER_TO_FLC, 
     * we like to alloc in free_area: 4->3->2->1
     */
    if (order >= HIGH_ORDER_TO_FLC)
        return (FREE_AREA_COUNTS - 1 - current_flc);

    return current_flc;
}


