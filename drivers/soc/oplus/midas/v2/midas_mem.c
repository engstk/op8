/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/memory_monitor.h>
#include "midas_mem.h"
#include "midas_cpu.h"
#include "midas_dev.h"

static void update_procmem_entry_locked(int index, struct process_mem val, struct mem_mmap_data *buf)
{
	buf->entrys[index].id[MEM_TGID] = val.pid;
	buf->entrys[index].id[MEM_UID] = val.uid;
	buf->entrys[index].rss = val.rss;
	buf->entrys[index].oom_score_adj = val.oom_score_adj;
	buf->entrys[index].gl_dev = val.gl_dev;
	strncpy(buf->entrys[index].comm, val.comm, TASK_COMM_LEN);
}

static struct process_mem pmem[ENTRY_MAX] = {0};
void midas_update_procmem(struct midas_priv_info *info)
{
	unsigned long flags;
	int i, cnt;
	struct mem_mmap_data *buf = info->mmap_addr;

	mm_get_all_pmem(&cnt, pmem);

	spin_lock_irqsave(&info->lock, flags);

	for (i = 0; i < cnt; i++)
		update_procmem_entry_locked(i, pmem[i], buf);

	buf->cnt = cnt;

	spin_unlock_irqrestore(&info->lock, flags);
}

static void update_meminfo_locked(int index, unsigned long value, struct mem_mmap_data* buf)
{
	buf->meminfo_entrys[index].value = value;
}

/*
 * meminfo_list:
 * 0, "MemFree",
 * 1, "MemAvailable",
 * 2 - 9, reserved.
 */
enum {
	MI_MEMFREE = 0,
	MI_MEMAVAIL,
	MI_MAX,
};

static void midas_update_meminfo(struct midas_priv_info *info)
{
	struct sysinfo i;
	unsigned long available;
	unsigned long flags;

	si_meminfo(&i);
	available = (unsigned long)si_mem_available();

	spin_lock_irqsave(&info->lock, flags);

	update_meminfo_locked(MI_MEMFREE, (unsigned long)(i.freeram << (PAGE_SHIFT - 10)), info->mmap_addr);
	update_meminfo_locked(MI_MEMAVAIL, available << (PAGE_SHIFT - 10), info->mmap_addr);

	spin_unlock_irqrestore(&info->lock, flags);
}

int midas_ioctl_get_meminfo(void *kdata, void *priv_info)
{
	unsigned long flags;
	struct midas_priv_info *info = priv_info;

	if (IS_ERR_OR_NULL(info) ||
		IS_ERR_OR_NULL(info->mmap_addr))
		return -EINVAL;

	spin_lock_irqsave(&info->lock, flags);
	memset(info->mmap_addr, 0, sizeof(struct mem_mmap_data));
	spin_unlock_irqrestore(&info->lock, flags);

	midas_update_meminfo(info);
	midas_update_procmem(info);

	return 0;
}

int midas_ioctl_get_totalmem(void *kdata, void *priv_info)
{
	unsigned long *tmp = (unsigned long *)kdata;
	struct sysinfo i;

	si_meminfo(&i);
	*tmp = (unsigned long)(i.totalram << (PAGE_SHIFT - 10));
	return 0;
}
