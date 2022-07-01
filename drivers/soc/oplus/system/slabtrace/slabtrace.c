// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** File : slabtrace.c
** Description : slabtrace
** Version : 1.0
******************************************************************/
#include "slabtrace.h"
/* Enalbe slabtrace:
 * CONFIG_OPLUS_FEATURE_SLABTRACE_DEBUG=y
 * Get more info disable Randomize_base
 * CONFIG_RANDOMIZE_BASE=n
 * after add the config, cat /proc/slabtrace to get slab userinfo
 */
static inline unsigned int oo_objects(struct kmem_cache_order_objects x)
{
	return x.x & OO_MASK;
}

static void free_loc_track(struct loc_track *t)
{
	if (t->max)
		free_pages((unsigned long)t->loc,
			get_order(sizeof(struct location) * t->max));
}

static inline unsigned int slab_index(void *p, struct kmem_cache *s, void *addr)
{
	return (p - addr) / s->size;
}

static struct track *get_track(struct kmem_cache *s, void *object,
	enum track_item alloc)
{
	struct track *p;

	if (s->offset)
		p = object + s->offset + sizeof(void *);
	else
		p = object + s->inuse;

	return p + alloc;
}

#if defined(CONFIG_OPLUS_FEATURE_SLABTRACE_DEBUG)

int oplus_cnt_pid(int pid){
	struct pid_cnt_struct *tmp;
	int ret = 0;
	bool find = false;
	list_for_each_entry(tmp, &pid_head, list){
		if(tmp->pid == pid) {
			tmp->cnt ++;
			find = true;
			break;
		}
	}
	if (!find && pid_cnt_cachep) {
		tmp = kmem_cache_zalloc(pid_cnt_cachep,GFP_ATOMIC);
		tmp->pid = pid;
		tmp->cnt = 1;
		list_add_tail(&(tmp->list), &pid_head);
	}
	return ret;
}

static int oplus_memcfg_add_location(struct loc_track *t, struct kmem_cache *s,
				const struct track *track)
{
	long start, end, pos;
	struct location *l;
	/* Caller from addresses */
	unsigned long (*caddrs)[OPLUS_MEMCFG_SLABTRACE_CNT];
	/* Called from addresses of track */
	unsigned long taddrs[OPLUS_MEMCFG_SLABTRACE_CNT]
		= { [0 ... OPLUS_MEMCFG_SLABTRACE_CNT - 1] = 0,};
	unsigned long age = jiffies - track->when;
	int i, cnt;

	start = -1;
	end = t->count;
	/* find the index of track->addr */
	for (i = 0; i < TRACK_ADDRS_COUNT; i++) {
#ifdef COMPACT_OPLUS_SLUB_TRACK
		/* we store the offset after MODULES_VADDR for
		 * kernel module and kernel text address
		 */
		unsigned long addr = (MODULES_VADDR + track->addrs[i]);

		if (track->addr == addr ||
			((track->addr - 4) == addr))
#else
		if ((track->addr == track->addrs[i]) ||
			(track->addr - 4 == track->addrs[i]))
#endif
			break;
	}
	/* copy all addrs if we cannot match track->addr */
	if (i == TRACK_ADDRS_COUNT)
		i = 0;
	cnt = min(OPLUS_MEMCFG_SLABTRACE_CNT, TRACK_ADDRS_COUNT - i);
#ifdef COMPACT_OPLUS_SLUB_TRACK
	{
		int j = 0;
		unsigned long addrs[TRACK_ADDRS_COUNT];

		for (j = 0; j < TRACK_ADDRS_COUNT; j++) {
			/* we store the offset after MODULES_VADDR for
			 * kernel module and kernel text address
			 */
			if (track->addrs[j])
				addrs[j] = MODULES_VADDR + track->addrs[j];
			else
				addrs[j] = 0;
		}
		memcpy(taddrs, addrs + i, (cnt * sizeof(unsigned long)));
	}
#else
	memcpy(taddrs, track->addrs + i, (cnt * sizeof(unsigned long)));
#endif

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		/*
		 * There is nothing at "end". If we end up there
		 * we need to add something to before end.
		 */
		if (pos == end)
			break;

		caddrs = &(t->loc[pos].addrs);
		if (!memcmp(caddrs, taddrs,
			OPLUS_MEMCFG_SLABTRACE_CNT * sizeof(unsigned long))) {

			l = &t->loc[pos];
			l->count++;
//sort by pid
			if ((isbypid && !isbystacks) && !strcmp(s->name, sort_cache_name))
				oplus_cnt_pid(track->pid);
			if (isbystacks && !strcmp(s->name, sort_cache_name))
			{
				if (!memcmp(l->addrs, sort_addrs, OPLUS_MEMCFG_SLABTRACE_CNT * sizeof(unsigned long)))
				{
				    oplus_cnt_pid(track->pid);
				}
			}

			if (track->when) {
				l->sum_time += age;
				if (age < l->min_time)
					l->min_time = age;
				if (age > l->max_time)
					l->max_time = age;

				if (track->pid < l->min_pid)
					l->min_pid = track->pid;
				if (track->pid > l->max_pid)
					l->max_pid = track->pid;

				cpumask_set_cpu(track->cpu,
						to_cpumask(l->cpus));
			}
			node_set(page_to_nid(virt_to_page(track)), l->nodes);
			return 1;
		}

		if (memcmp(caddrs, taddrs,
			OPLUS_MEMCFG_SLABTRACE_CNT * sizeof(unsigned long)) < 0)
			end = pos;
		else
			start = pos;
	}

	/*
	 * Not found. Insert new tracking element.
	 */
	if (t->count >= t->max &&
		!alloc_loc_track(t, 2 * t->max, __GFP_HIGH | __GFP_ATOMIC))
		return 0;

	l = t->loc + pos;
	if (pos < t->count)
		memmove(l + 1, l,
			(t->count - pos) * sizeof(struct location));
	t->count++;
	l->count = 1;
	l->addr = track->addr;
	memcpy(l->addrs, taddrs,
			OPLUS_MEMCFG_SLABTRACE_CNT * sizeof(unsigned long));

	//sort by pid
	if ((isbypid && !isbystacks) && !strcmp(s->name, sort_cache_name))
		oplus_cnt_pid(track->pid);
	if (isbystacks && !strcmp(s->name, sort_cache_name))
	{
		if (!memcmp(l->addrs, sort_addrs, OPLUS_MEMCFG_SLABTRACE_CNT * sizeof(unsigned long)))
		{
			oplus_cnt_pid(track->pid);
		}
	}

	l->sum_time = age;
	l->min_time = age;
	l->max_time = age;
	l->min_pid = track->pid;
	l->max_pid = track->pid;
	cpumask_clear(to_cpumask(l->cpus));
	cpumask_set_cpu(track->cpu, to_cpumask(l->cpus));
	nodes_clear(l->nodes);
	node_set(page_to_nid(virt_to_page(track)), l->nodes);
	return 1;
}

static void oplus_memcfg_process_slab(struct loc_track *t, struct kmem_cache *s,
		struct page *page, enum track_item alloc,
		unsigned long *map)
{
	void *addr = page_address(page);
	void *p;

	bitmap_zero(map, page->objects);
	get_map(s, page, map);

	for_each_object(p, s, addr, page->objects)
		if (!test_bit(slab_index(p, s, addr), map))
			oplus_memcfg_add_location(t, s, get_track(s, p, alloc));
}

static int oplus_memcfg_list_locations(struct kmem_cache *s, struct seq_file *m,
					enum track_item alloc)
{
	unsigned long i, j;
	struct loc_track t = { 0, 0, NULL };
	int node;
	unsigned long *map = kmalloc(BITS_TO_LONGS(oo_objects(s->max)) *
				     sizeof(unsigned long), GFP_KERNEL);
	struct kmem_cache_node *n;

//sort by pid
	struct pid_cnt_struct *tmp;
	struct list_head *pos, *q;
	if ((isbypid || isbystacks) && !strcmp(s->name, sort_cache_name))
		INIT_LIST_HEAD(&pid_head);

	if (!map || !alloc_loc_track(&t, PAGE_SIZE / sizeof(struct location),
				     GFP_KERNEL)) {
		kfree(map);
		seq_puts(m, "Out of memory\n");
		return 0;
	}
	/* Push back cpu slabs */
	flush_all(s);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long flags;
		struct page *page;

		if (!atomic_long_read(&n->nr_slabs))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(page, &n->partial, lru)
			oplus_memcfg_process_slab(&t, s, page, alloc, map);
		list_for_each_entry(page, &n->full, lru)
			oplus_memcfg_process_slab(&t, s, page, alloc, map);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	for (i = 0; i < t.count; i++) {
		struct location *l = &t.loc[i];

		seq_printf(m, "%7ld ", l->count);

		if (l->addr)
			seq_printf(m, "%pS", (void *)l->addr);
		else
			seq_puts(m, "<not-available>");

		for (j = 0; j < OPLUS_MEMCFG_SLABTRACE_CNT; j++)
		{
			if (l->addrs[j])
			{
				if (isprintSyms)
					seq_printf(m, " %pS", (void *)l->addrs[j]);
				else
					seq_printf(m, " 0x%px", (void *)l->addrs[j]);
			}
		}
		seq_puts(m, "\n");
	}

	free_loc_track(&t);
	kfree(map);
// sort by pid
	if ((isbypid || isbystacks) && !strcmp(s->name, sort_cache_name))
	{
		list_for_each_safe(pos, q, &pid_head) {
			tmp = list_entry(pos, struct pid_cnt_struct, list);
			for(i = 0; i < 5; i ++)
			{
				if(top_pid[i].cnt == 0)
				{
					top_pid[i].pid = tmp->pid;
					top_pid[i].cnt = tmp->cnt;
					break;
				}
				if(top_pid[i].cnt < tmp->cnt) {
						for (j = 4; j > i; j--) {
							top_pid[j].pid = top_pid[j-1].pid;
							top_pid[j].cnt = top_pid[j-1].cnt;
						}
					top_pid[i].pid = tmp->pid;
					top_pid[i].cnt = tmp->cnt;
					break;
				}
			}
			list_del(pos);
			kmem_cache_free(pid_cnt_cachep, tmp);
		}
		seq_printf(m, "\n\tThe top five of the number of %s alloced\n", sort_cache_name);
		for(i = 0; i < 5; i ++) {
			seq_printf(m, "\t Top%u: Pid %d, \t cnt %d \n", i, top_pid[i].pid, top_pid[i].cnt);
		}
		memset(top_pid, 0 , sizeof(top_pid));
	}


	if (!t.count)
		seq_puts(m, "No data\n");
	return 0;
}

static int oplus_memcfg_slabtrace_show(struct seq_file *m, void *p)
{
	struct kmem_cache *s;

	pid_cnt_cachep = kmem_cache_create("pid_cnt_cache", sizeof(struct pid_cnt_struct),
							  0, 0, NULL);
	if (!pid_cnt_cachep)
	{
			pr_err("slabtrace: pid_cnt_cachep create failed\n");
			return -ENOMEM;
	}

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		/* We only want to know the backtraces of kmalloc-*
		 * Backtraces of other kmem_cache can be find easily
		 */
		if (!strstr(s->name, "kmalloc-"))
			continue;
		if (strlen(sort_cache_name) > 8 && strcmp(s->name, sort_cache_name))
			continue;

		seq_printf(m, "======= kmem_cache: %s alloc_calls =======\n",
				s->name);
		if (!(s->flags & SLAB_STORE_USER))
			continue;
		else
			oplus_memcfg_list_locations(s, m, TRACK_ALLOC);
	}
	mutex_unlock(&slab_mutex);
	kmem_cache_destroy(pid_cnt_cachep);
	return 0;
}

static int slabtrace_open(struct inode *inode, struct file *file)
{
	return single_open(file, oplus_memcfg_slabtrace_show, NULL);
}

static const struct file_operations proc_slabtrace_operations = {
	.open = slabtrace_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int oplus_memcfg_slabcfg_show(struct seq_file *m, void *p)
{
	mutex_lock(&slab_mutex);
	seq_printf(m, "=== slabtrace config ===\n");
	seq_printf(m, "Reset config:echo \"reset\" > /proc/slab_memcfg/slabcfg\n");
	seq_printf(m, "Print type:echo \"print symbol/address\" > /proc/slab_memcfg/slabcfg\n");
	seq_printf(m, "Specific kmalloc:echo \"kmalloc-x pid\" > /proc/slab_memcfg/slabcfg\n");
	seq_printf(m, "Specific kmalloc sort by stacks:echo \"kmalloc-x stacks add1 add2 ....\" > /proc/slab_memcfg/slabcfg\n");
	seq_printf(m, "slabtrace config: current config\n");
	if (isbypid)
	{
		seq_printf(m, "sort by pid, sort_cache_name:%s\n", sort_cache_name);
	}
	if (isbystacks)
	{
		seq_printf(m, "sort by stacks, sort_cache_name:%s\n", sort_cache_name);
	}
	mutex_unlock(&slab_mutex);

	return 0;
}

static int slabcfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, oplus_memcfg_slabcfg_show, NULL);
}

static ssize_t slabcfg_write(struct file *filp, const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[1024] = {};
	char mchar[] = "kmalloc-";
	char argv1[10] = {};
	char argv2[10] = {};

	//sort_addrs
	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	pr_info("debug for slabconfig\n");

	mutex_lock(&sort_mutex);
	sscanf(buf,"%s %s %lx %lx %lx %lx", argv1, argv2, &sort_addrs[0], &sort_addrs[1], &sort_addrs[2], &sort_addrs[3]);
	pr_info("slabtrace:%s %s %lx %lx %lx %lx\n", argv1, argv2, sort_addrs[0], sort_addrs[1], sort_addrs[2], sort_addrs[3]);

	/* reset */
	if (!strcmp(argv1, "reset"))
	{
		isbypid = false;
		isbystacks = false;
		isprintSyms = false;
		memset(sort_addrs, 0, sizeof(sort_addrs));
		memset(sort_cache_name, 0, sizeof(sort_cache_name));
		goto out;
	}
	/* print type addr */
	if (!strcmp(argv1, "print"))
	{
		if (!strcmp(argv2, "symbol"))
			isprintSyms = true;
		if (!strcmp(argv2, "address"))
			isprintSyms = false;
		goto out;
	}

	/* pick kmalloc-x */
	if (strncmp(argv1, mchar, strlen(mchar)))
	{
		pr_info("slabtrace config args invalid\n");
		goto out;
	}
	strcpy(sort_cache_name, argv1);
	pr_info("slabtrace sort_cache_name:%s\n", sort_cache_name);

	/* only by pid */
	if (!strcmp(argv2, "pid"))
	{
		isbypid = true;
		pr_info("slabtrace sort simple by pid\n");
		goto out;
	}
	/* specific stack sort by pid */
	if (!strcmp(argv2, "stacks"))
	{
		isbystacks = true;
		pr_info("slabtrace sort by stacks\n");
	}
out:
	mutex_unlock(&sort_mutex);
	return cnt;
}

static const struct file_operations proc_slabcfg_operations = {
	.open = slabcfg_open,
	.write = slabcfg_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


static int __init oplus_memcfg_late_init(void)
{
	struct proc_dir_entry *pEntry = NULL;
	struct proc_dir_entry *oplus_memcfg_dir = NULL;

	oplus_memcfg_dir = proc_mkdir("slab_memcfg", NULL);
	if(!oplus_memcfg_dir) {
		pr_info("%s mkdir /proc/slab_memcfg failed\n", __func__);
	} else {
		pEntry = proc_create("slabtrace", 0400, oplus_memcfg_dir, &proc_slabtrace_operations);
		if(!pEntry)
			pr_info("create slabtrace proc entry failed\n");
		pEntry = proc_create("slabcfg", 0600, oplus_memcfg_dir, &proc_slabcfg_operations);
		if(!pEntry)
			pr_info("create slabcfg proc entry failed\n");
	}
	mutex_init(&sort_mutex);
	return 0;
}
late_initcall(oplus_memcfg_late_init);
#endif
