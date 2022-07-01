// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _VMALLOC_DEBUG_
#define _VMALLOC_DEBUG_
#include <linux/sort.h>
#include <linux/stacktrace.h>
#include <linux/sched/clock.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/version.h>
#include <linux/memleak_stackdepot.h>

/* remember the vmalloc info. */
static atomic_t vmalloc_count = ATOMIC_INIT(0);

/* save vmalloc stack. */
#ifdef CONFIG_MEMLEAK_DETECT_THREAD
#define VMALLOC_STACK_DEPTH 16
#else
#define VMALLOC_STACK_DEPTH 10
#endif
#define VD_VALUE_LEN 32

static int vmalloc_debug_enable = 0;
static atomic64_t hash_cal_sum_us = ATOMIC64_INIT(0);
static atomic64_t hash_cal_times = ATOMIC64_INIT(0);
static unsigned long hash_cal_max_us;

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
#define LOGGER_PRELOAD_SIZE 4076
extern void logger_kmsg_nwrite(const char *tag, const char *msg, size_t len);

static inline int find_last_lf(char *buf, size_t len)
{
	int i = len - 1;

	for (; i >= 0; i--) {
		if (buf[i] == '\n')
			break;
	}

	return (i < 0) ? -1 : i + 1;
}

void dump_meminfo_to_logger(const char *tag, char *msg, size_t total_len)
{
	int len;
	size_t tag_len = strlen(tag);
	char *wr_msg = msg;

	while (total_len > 0) {
		if ((total_len + tag_len + 3) <= LOGGER_PRELOAD_SIZE) {
			logger_kmsg_nwrite(tag, (const char *)wr_msg, total_len);
			return;
		}

		len = LOGGER_PRELOAD_SIZE - 3 - tag_len;
		len = find_last_lf(wr_msg, len);
		if (len < 0)
			return;

		logger_kmsg_nwrite(tag, (const char *)wr_msg, len);

		wr_msg += len;
		total_len -= len;
	}
}
EXPORT_SYMBOL(dump_meminfo_to_logger);
#endif

static noinline ml_depot_stack_handle_t _save_vmalloc_stack(gfp_t flags)
{
	unsigned long entries[VMALLOC_STACK_DEPTH];
	struct stack_trace trace = {
		.nr_entries = 0,
		.entries = entries,
		.max_entries = VMALLOC_STACK_DEPTH,
#ifdef CONFIG_64BIT
		.skip = 4
#else
		.skip = 3
#endif
	};
	ml_depot_stack_handle_t handle;
	unsigned long delay;
	unsigned long start = sched_clock();

	save_stack_trace(&trace);
	if (trace.nr_entries != 0 &&
			trace.entries[trace.nr_entries-1] == ULONG_MAX)
		trace.nr_entries--;

	handle = ml_depot_save_stack(&trace, flags);
	if (handle) {
		delay = (sched_clock() - start) / 1000;
		if (delay > hash_cal_max_us)
			hash_cal_max_us = delay;
		atomic64_add(delay, &hash_cal_sum_us);
		atomic64_inc(&hash_cal_times);
	}
	return handle;
}

static unsigned int save_vmalloc_stack(unsigned long flags, struct vmap_area *va)
{
	if (flags & VM_ALLOC) {
		atomic_inc(&vmalloc_count);
		if (vmalloc_debug_enable)
			return _save_vmalloc_stack(GFP_KERNEL);
	}

	return 0;
}

static void dec_vmalloc_stat(struct vmap_area *va)
{
	if (va->vm->flags & VM_ALLOC)
		atomic_dec(&vmalloc_count);
}

static ssize_t vmalloc_debug_enable_write(struct file *file,
		const char __user *buff, size_t len, loff_t *ppos)
{
	char kbuf[VD_VALUE_LEN] = {'0'};
	long val;
	int ret;

	if (len > (VD_VALUE_LEN - 1))
		len = VD_VALUE_LEN - 1;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 10, &val);
	if (ret)
		return -EINVAL;

	if (val) {
		ret = ml_depot_init();
		if (ret)
			return  -ENOMEM;
		vmalloc_debug_enable = 1;
	} else
		vmalloc_debug_enable = 0;

	return len;
}

static ssize_t vmalloc_debug_enable_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[VD_VALUE_LEN] = {'0'};
	int len;

	len = scnprintf(kbuf, VD_VALUE_LEN - 1, "%d\n", vmalloc_debug_enable);
	if (kbuf[len - 1] != '\n')
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static ssize_t hash_cal_time_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[128] = {'0'};
	int len;
	unsigned long sum_us = atomic64_read(&hash_cal_sum_us);
	unsigned long times = atomic64_read(&hash_cal_times);

	len = scnprintf(kbuf, 127, "%lu %lu %lu %lu\n",
			sum_us, times, sum_us / times, hash_cal_max_us);
	if (kbuf[len - 1] != '\n')
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

void enable_vmalloc_debug(void)
{
	int ret;

	ret = ml_depot_init();
	if (ret) {
		pr_err("init depot failed, oom.\n");
		return;
	}
	vmalloc_debug_enable = 1;
}
EXPORT_SYMBOL(enable_vmalloc_debug);

void disable_vmalloc_debug(void)
{
	vmalloc_debug_enable = 0;
}
EXPORT_SYMBOL(disable_vmalloc_debug);

static const struct file_operations hash_cal_time_ops = {
	.read		= hash_cal_time_read,
};

static const struct file_operations proc_vmalloc_debug_enable_ops = {
	.write          = vmalloc_debug_enable_write,
	.read		= vmalloc_debug_enable_read,
};

typedef struct {
	const void *caller;
	unsigned int hash;
	unsigned int nr_pages;
} vmalloc_debug_list;

typedef struct {
	unsigned long max;
	unsigned long count;
	vmalloc_debug_list *data;
} vmalloc_record_t;

static int add_record(vmalloc_record_t *t, struct vm_struct *v)
{
	long start, end, pos;
	vmalloc_debug_list *l;
	unsigned int hash;

	start = -1;
	end = t->count;

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		/*
		 * There is nothing at "end". If we end up there
		 * we need to add something to before end.
		 */
		if (pos == end)
			break;

		hash = t->data[pos].hash;
		if (v->hash == hash) {
			l = &t->data[pos];
			l->nr_pages += v->nr_pages;
			return 1;
		}

		if (v->hash < hash)
			end = pos;
		else
			start = pos;
	}

	/*
	 * Not found. Insert new tracking element.
	 */
	if (t->count >= t->max)
		return 0;

	l = t->data + pos;
	if (pos < t->count)
		memmove(l + 1, l, (t->count - pos) * sizeof(vmalloc_record_t));
	t->count++;
	l->caller = v->caller;
	l->hash = v->hash;
	l->nr_pages = v->nr_pages;
	return 1;
}

static int record_cmp(const void *la, const void *lb)
{
	return ((vmalloc_debug_list *)lb)->nr_pages - \
		((vmalloc_debug_list *)la)->nr_pages;
}

static void record_swap(void *la, void *lb, int size)
{
	vmalloc_debug_list l_tmp;

	memcpy(&l_tmp, la, size);
	memcpy(la, lb, size);
	memcpy(lb, &l_tmp, size);
}

static inline char *dump_vmalloc_debug_info(unsigned int inlen, unsigned int *outlen)
{
	unsigned int vmalloc_entry_count = atomic_read(&vmalloc_count);
	vmalloc_record_t record;
	char *kbuf;
	struct vmap_area *va;
	struct vm_struct *v;
	unsigned int i, j;
	int ret;
	unsigned int len = 0;
	struct stack_trace trace;
	unsigned int add_record_cnt = 0;

	record.data = vmalloc((sizeof(vmalloc_debug_list) * vmalloc_entry_count));
	if (!record.data) {
		pr_err("vmalloc_debug : malloc record.data failed.\n");
		return NULL;
	}

	kbuf = kmalloc(inlen, GFP_KERNEL);
	if (!kbuf) {
		pr_err("vmalloc_debug : malloc kbuf failed.\n");
		vfree(record.data);
		return NULL;
	}

	record.max = vmalloc_entry_count;
	record.count = 0;
	spin_lock(&vmap_area_lock);
	list_for_each_entry(va, &vmap_area_list, list) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
		if (!(va->flags & VM_VM_AREA))
			continue;
#endif
		v = va->vm;
		if (!v)
			continue;

		if (!(v->flags & VM_ALLOC) || (v->hash == 0))
			continue;

		ret = add_record(&record, v);
		if (!ret)
			break;

		add_record_cnt++;
	}
	spin_unlock(&vmap_area_lock);

	sort(record.data, record.count, sizeof(vmalloc_debug_list),
			record_cmp, record_swap);

	/* reserve one byte for '\n' */
	inlen--;
	for (i = 0; i < record.count; i++) {
		len += scnprintf(kbuf+len, inlen-len, "- %u KB -\n",
				record.data[i].nr_pages << 2);

		if (record.data[i].hash) {
			memset(&trace, 0, sizeof(trace));
			ml_depot_fetch_stack(record.data[i].hash, &trace);
			for (j = 0; j < trace.nr_entries; j++) {
				len += scnprintf(kbuf+len, inlen-len,
						"%pS\n", (void *)trace.entries[j]);
				if (len == inlen)
					break;
			}
			len += scnprintf(kbuf+len, inlen-len, "\n");
		}

		if (len == inlen)
			break;
	}
	if ((len > 0) && (kbuf[len - 1] != '\n'))
		kbuf[len++] = '\n';

	vfree(record.data);
	*outlen = len;
	return kbuf;
}

#if defined(CONFIG_MEMLEAK_DETECT_THREAD) && defined(CONFIG_SVELTE)
#define VMALLOC_DEBUG_STEP 30ll
#define VMALLOC_DUMP_WATER_MIN 180ll
#define BUFLEN(total, len) (total - len - 25)
#define BUFLEN_EXT(total, len) (total - len - 1)
#define VMALLOC_LOG_TAG "vmalloc_debug"

static inline void dump_vmalloc_dmesg(unsigned long used_size, char *dump_buff,
		int len)
{
	unsigned int vmalloc_entry_count = atomic_read(&vmalloc_count);
	vmalloc_record_t record;
	struct vmap_area *va;
	struct vm_struct *v;
	unsigned int i, j;
	struct stack_trace trace;
	int dump_buff_len = 0;

	record.data = vmalloc((sizeof(vmalloc_debug_list) * vmalloc_entry_count));
	if (!record.data) {
		pr_err("[vmalloc_debug] : malloc record.data failed.\n");
		return;
	}
	record.max = vmalloc_entry_count;
	record.count = 0;

	spin_lock(&vmap_area_lock);
	list_for_each_entry(va, &vmap_area_list, list) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
		if (!(va->flags & VM_VM_AREA))
			continue;
#endif
		v = va->vm;
		if (!v)
			continue;

		if (!(v->flags & VM_ALLOC) || (v->hash == 0))
			continue;

		if (!add_record(&record, v))
			break;
	}
	spin_unlock(&vmap_area_lock);

	sort(record.data, record.count, sizeof(vmalloc_debug_list),
			record_cmp, record_swap);

	memset(dump_buff, 0, len);
	dump_buff_len = scnprintf(dump_buff + dump_buff_len,
			BUFLEN(len, dump_buff_len),
			"used %u MB depot_index %d:\n", used_size,
			ml_get_depot_index());

	for (i = 0; i < record.count; i++) {
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len),
				"%pS %u KB\n",
				record.data[i].caller,
				record.data[i].nr_pages << 2);

		if (record.data[i].hash) {
			memset(&trace, 0, sizeof(trace));
			ml_depot_fetch_stack(record.data[i].hash, &trace);
			for (j = 0; j < trace.nr_entries; j++)
				dump_buff_len += scnprintf(dump_buff + dump_buff_len,
						BUFLEN(len, dump_buff_len),
						"%pS\n",
						(void *)trace.entries[j]);
		}
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN(len, dump_buff_len),
				"-\n");
	}
	vfree(record.data);
	if (!record.count)
		dump_buff_len += scnprintf(dump_buff + dump_buff_len,
				BUFLEN_EXT(len, dump_buff_len),
				"No Data\n");

	dump_buff[dump_buff_len++] = '\n';
	dump_meminfo_to_logger(VMALLOC_LOG_TAG, dump_buff, dump_buff_len);
}

void dump_vmalloc_debug(char *dump_buff, int len)
{
	static long long debug_dump_watermark = VMALLOC_DUMP_WATER_MIN;
	long long vmalloc_total_size = vmalloc_nr_pages() >> 8;

	if (vmalloc_total_size < debug_dump_watermark) {
		pr_warn("[vmalloc_debug] vmalloc_total_size %lld do not over %lld, ignore it.\n",
				vmalloc_total_size, debug_dump_watermark);
		return;
	}

	pr_warn("[vmalloc_debug] vmalloc_size %lld is over %lld.\n",
			vmalloc_total_size, debug_dump_watermark);
	if (!vmalloc_debug_enable) {
		pr_err("[vmalloc_debug] vmalloc debug is disabled.\n");
		return;
	}

	if (!dump_buff) {
		pr_err("[vmalloc_debug] dump_buff is NULL.\n");
		return;
	}

	debug_dump_watermark += VMALLOC_DEBUG_STEP;
	dump_vmalloc_dmesg(vmalloc_total_size, dump_buff, len);
}
EXPORT_SYMBOL(dump_vmalloc_debug);
#endif

static ssize_t vmalloc_debug_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	unsigned int read_len = PAGE_SIZE;
	char *kbuf;
	unsigned int len;

	if (!vmalloc_debug_enable) {
		pr_err("[vmalloc_debug] vmalloc debug is disabled.\n");
		return 0;
	}

	kbuf = dump_vmalloc_debug_info(read_len, &len);
	if (!kbuf)
		return -ENOMEM;

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buff, kbuf + *off, (len < count ? len : count))) {
		pr_err("vmalloc_debug : copy to user failed.\n");
		kfree(kbuf);
		return -EFAULT;
	}
	kfree(kbuf);

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t vmalloc_used_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char kbuf[64];
	unsigned int len;

	len = scnprintf(kbuf, 63, "%ld\n", vmalloc_nr_pages() << 2);

	if (kbuf[len - 1] != '\n')
		kbuf[len++] = '\n';
	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buff, kbuf, (len < count ? len : count))) {
		pr_err("vmalloc_debug : copy to user failed.\n");
		return -EFAULT;
	}

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static const struct file_operations vmalloc_debug_fops = {
	.read = vmalloc_debug_read,
};

static const struct file_operations vmalloc_used_fops = {
	.read = vmalloc_used_read,
};

int __init create_vmalloc_debug(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *vpentry;
	struct proc_dir_entry *spentry;
	struct proc_dir_entry *epentry;
	struct proc_dir_entry *tpentry;

	vpentry = proc_create("vmalloc_debug", S_IRUGO, parent,
			&vmalloc_debug_fops);
	if (!vpentry) {
		pr_err("create vmalloc_debug proc failed.\n");
		return -ENOMEM;
	}

	spentry = proc_create("vmalloc_used", S_IRUGO, parent,
			&vmalloc_used_fops);
	if (!spentry) {
		pr_err("create vmalloc_used proc failed.\n");
		proc_remove(vpentry);
		return -ENOMEM;
	}

	epentry = proc_create("vmalloc_debug_enable",
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
			parent, &proc_vmalloc_debug_enable_ops);
	if (!epentry) {
		pr_err("create vmalloc_debug_enable proc failed.\n");
		proc_remove(spentry);
		proc_remove(vpentry);
		return -ENOMEM;
	}

	tpentry = proc_create("vmalloc_hash_cal", S_IRUSR, parent,
			&hash_cal_time_ops);
	if (!tpentry) {
		pr_err("create vmalloc_hash_cal proc failed.\n");
		proc_remove(epentry);
		proc_remove(spentry);
		proc_remove(vpentry);
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(create_vmalloc_debug);
#endif /* _VMALLOC_DEBUG_ */
