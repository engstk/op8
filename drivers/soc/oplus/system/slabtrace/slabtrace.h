/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kasan.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/debugobjects.h>
#include <linux/kallsyms.h>
#include <linux/memory.h>
#include <linux/math64.h>
#include <linux/fault-inject.h>
#include <linux/stacktrace.h>
#include <linux/prefetch.h>
#include <linux/memcontrol.h>
#include <linux/random.h>
#include <trace/events/kmem.h>
#include <linux/version.h>

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#include "../../../../../mm/internal.h"
#include "../../../../../mm/slab.h"
#endif

#if defined(CONFIG_OPLUS_FEATURE_SLABTRACE_DEBUG)
#ifndef CONFIG_RANDOMIZE_BASE
#define COMPACT_OPLUS_SLUB_TRACK
#endif
#endif
/*
 * Tracking user of a slab.
 */
#define TRACK_ADDRS_COUNT 16
struct track {
	unsigned long addr;	/* Called from address */
#ifdef CONFIG_STACKTRACE
#if defined(COMPACT_OPLUS_SLUB_TRACK)
/* Store the offset after MODULES_VADDR for
 * kernel module and kernel text address
 */
	u32 addrs[TRACK_ADDRS_COUNT];
#else
	unsigned long addrs[TRACK_ADDRS_COUNT];	/* Called from address */
#endif
#endif
	int cpu;		/* Was running on cpu */
	int pid;		/* Pid context */
	unsigned long when;	/* When did the operation occur */
};

enum track_item { TRACK_ALLOC, TRACK_FREE };



#if defined(CONFIG_OPLUS_FEATURE_SLABTRACE_DEBUG)
#define OPLUS_MEMCFG_SLABTRACE_CNT 4
#if (OPLUS_MEMCFG_SLABTRACE_CNT > TRACK_ADDRS_COUNT)
#error (OPLUS_MEMCFG_SLABTRACE_CNT > TRACK_ADDRS_COUNT)
#endif
#endif
struct location {
	unsigned long count;
	unsigned long addr;
#if defined(CONFIG_OPLUS_FEATURE_SLABTRACE_DEBUG) 
#ifdef CONFIG_STACKTRACE
	unsigned long addrs[OPLUS_MEMCFG_SLABTRACE_CNT]; /* caller address */
#endif
#endif
	long long sum_time;
	long min_time;
	long max_time;
	long min_pid;
	long max_pid;
	DECLARE_BITMAP(cpus, NR_CPUS);
	nodemask_t nodes;
};

struct loc_track {
	unsigned long max;
	unsigned long count;
	struct location *loc;
};

static bool isbypid;
static bool isbystacks;
static bool isprintSyms = true;
static char sort_cache_name[13];
static struct mutex sort_mutex;

struct pid_cnt_struct {
	struct list_head list;
	unsigned cnt;           /* slab pid count */
	int pid;				/* Pid context */
};
static struct list_head  pid_head;
static struct pid_cnt_struct top_pid[5]; /* record the top 5 pid */
static unsigned long sort_addrs[OPLUS_MEMCFG_SLABTRACE_CNT]; /*sort by caller address */
static struct kmem_cache *pid_cnt_cachep;
extern alloc_loc_track(struct loc_track *t, unsigned long max, gfp_t flags);
extern void get_map(struct kmem_cache *s, struct page *page, unsigned long *map);
extern void flush_all(struct kmem_cache *s);

#define for_each_object(__p, __s, __addr, __objects) \
	for (__p = fixup_red_left(__s, __addr); \
		__p < (__addr) + (__objects) * (__s)->size; \
		__p += (__s)->size)
#define OO_SHIFT	16
#define OO_MASK		((1 << OO_SHIFT) - 1)

