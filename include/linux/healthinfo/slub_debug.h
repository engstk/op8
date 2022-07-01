#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
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

#include "internal.h"

extern unsigned long slabs_node(struct kmem_cache *s, int node);
extern unsigned long node_nr_slabs(struct kmem_cache_node *n);
extern void inc_slabs_node(struct kmem_cache *s, int node, int objects);
extern void dec_slabs_node(struct kmem_cache *s, int node, int objects);

extern int count_free(struct page *page);
extern unsigned long node_nr_objs(struct kmem_cache_node *n);

