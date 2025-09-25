// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/sched/signal.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/reserve_area.h>
#include <linux/version.h>
#define STACK_RLIMIT_OVERFFLOW (32<<20)
#define THRIDPART_APP_UID_LOW_LIMIT 10000UL
#define NS_PER_SEC (1000000000LLU)
#define TRIGGER_TIME_NS (300*NS_PER_SEC)

int svm_oom_pid = -1;
unsigned long svm_oom_jiffies = 0;
static int va_feature = RESERVE_FEATURES;

static inline bool check_parent_is_zygote(struct task_struct *tsk)
{
	struct task_struct *t;
	bool ret = false;

	rcu_read_lock();
	t = rcu_dereference(tsk->real_parent);
	if (t) {
		const struct cred *tcred = __task_cred(t);

		if (!strcmp(t->comm, "main") && (tcred->uid.val == 0) &&
				(t->parent != NULL) && !strcmp(t->parent->comm,"init"))
			ret = true;
	}
	rcu_read_unlock();
	return ret;
}

int get_va_feature_value(unsigned int feature)
{
	return va_feature & (feature & ~RESERVE_LOGGING);
}

unsigned long get_unmmaped_area_from_anti_fragment(struct mm_struct *mm,
		struct vm_unmapped_area_info *info)
{
	struct vm_unmapped_area_info info_b;
	unsigned long addr = -ENOMEM;

	switch (info->length) {
		case 4096: case 8192: case 16384: case 32768:
		case 65536: case 131072: case 262144:
			info_b = *info;
			info_b.high_limit = current->mm->va_feature_rnd - (SIZE_10M * (ilog2(info->length) - 12));
			info_b.low_limit = current->mm->va_feature_rnd - (SIZE_10M * 7);
			addr = unmapped_area_topdown(&info_b);
			if (!offset_in_page(addr))
				return addr;
		default:
			break;
	}

	return addr;
}

unsigned long get_unmmaped_area_from_reserved(struct mm_struct *mm,
		struct vm_unmapped_area_info *info)
{
	info->flags = VM_UNMAPPED_AREA_TOPDOWN;
	info->low_limit = max(PAGE_SIZE, mmap_min_addr);
	info->high_limit = mm->mmap_base;

	return vm_unmapped_area(info);
}

static ssize_t va_feature_write(struct file *file,
		const char __user *buff, size_t len, loff_t *ppos)
{
	char kbuf[12] = {'0'};
	long val;
	int ret;

	if (len > 11)
		len = 11;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 16, &val);
	if (ret)
		return -EINVAL;

	va_feature = val & RESERVE_FEATURES;
	return len;
}

static ssize_t va_feature_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[12] = {'0'};
	int len;

	len = snprintf(kbuf, 12, "%#x\n", va_feature);
	if (kbuf[len] != '\n')
		kbuf[len] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static const struct file_operations proc_va_feature_ops = {
	.write          = va_feature_write,
	.read		= va_feature_read,
};

int create_reserved_area_enable_proc(struct proc_dir_entry *parent)
{
	if (parent && proc_create("va_feature", S_IRUSR|S_IWUSR,
				parent, &proc_va_feature_ops)) {
		printk("Register va_feature interface passed.\n");
		return 0;
	}
	pr_err("Register va_feature interface failed.\n");
	return -ENOMEM;
}
EXPORT_SYMBOL(create_reserved_area_enable_proc);

#ifdef CONFIG_DUMP_MM_INFO
static int fetch_vma_name(struct vm_area_struct *vma, char *kbuf, int klen)
{
	const char __user *name = vma_get_anon_name(vma);
	struct mm_struct *mm = vma->vm_mm;

	unsigned long page_start_vaddr;
	unsigned long page_offset;
	unsigned long num_pages;
	unsigned long max_len = klen - 16;
	int i;
	int cnt = 0;

	page_start_vaddr = (unsigned long)name & PAGE_MASK;
	page_offset = (unsigned long)name - page_start_vaddr;
	num_pages = DIV_ROUND_UP(page_offset + max_len, PAGE_SIZE);

	cnt += snprintf(kbuf, klen, "[anon:");

	for (i = 0; i < num_pages; i++) {
		int len;
		int write_len;
		const char *kaddr;
		long pages_pinned;
		struct page *page;

		pages_pinned = get_user_pages_remote(current, mm,
				page_start_vaddr, 1, 0, &page, NULL, NULL);
		if (pages_pinned < 1) {
			cnt += snprintf(kbuf + cnt, klen - cnt, "<fault>]");
			return cnt;
		}

		kaddr = (const char *)kmap(page);
		len = min(max_len, PAGE_SIZE - page_offset);
		write_len = strnlen(kaddr + page_offset, len);
		memcpy(kbuf + cnt, kaddr + page_offset, write_len);
		kunmap(page);
		put_page(page);
		cnt += write_len;

		/* if strnlen hit a null terminator then we're done */
		if (write_len != len)
			break;

		max_len -= len;
		if ((int)max_len <= 0)
			break;
		page_offset = 0;
		page_start_vaddr += PAGE_SIZE;
	}

	cnt += snprintf(kbuf + cnt, klen - cnt, "]");
	return cnt;
}

static void dump_one_vma(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	vm_flags_t flags = vma->vm_flags;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	unsigned long start, end;
	dev_t dev = 0;
	char vma_name[256] = {'\0'};
	const char *name = NULL;
	int len;

	start = vma->vm_start;
	end = vma->vm_end;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);

		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
		name = d_path(&file->f_path, vma_name, 255);

		if (IS_ERR(name))
			name = NULL;
		goto done;
	}

	if (vma->vm_ops && vma->vm_ops->name) {
		name = vma->vm_ops->name(vma);
		if (name)
			goto done;
	}

	if (!mm) {
		name = "[vdso]";
		goto done;
	}

	if (start <= mm->brk && end >= mm->start_brk) {
		name = "[heap]";
		goto done;
	}

	/* whether the vma is stack */
	if (start <= mm->start_stack && end >= mm->start_stack) {
		name = "[stack]";
		goto done;
	}

	if (vma_get_anon_name(vma)) {
		len = fetch_vma_name(vma, vma_name, 256);
		if (len) {
			name = vma_name;
			if ((len == 256) && (vma_name[len-1] != '\0'))
				vma_name[len-1] = '\0';
		} else if (len == 0)
			name = "";
	}
done:
	pr_err("dbgmm %08lx-%08lx %c%c%c%c %08llx %02u:%02u %ld %s",
			start, end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			pgoff, MAJOR(dev), MINOR(dev), ino,
			name ?: "");
}

void dump_mm_info(unsigned long len, unsigned long flags, int dump_vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long swap, anon, file, shmem;
	unsigned long total_vm, total_rss;
	struct vm_area_struct *vma;
	unsigned long chunk, chunk_start;
	unsigned long used_size, lastend;
	int dump_num = 0;

	anon = get_mm_counter(mm, MM_ANONPAGES);
	file = get_mm_counter(mm, MM_FILEPAGES);
	shmem = get_mm_counter(mm, MM_SHMEMPAGES);
	swap = get_mm_counter(mm, MM_SWAPENTS);
	total_vm = mm->total_vm;
	total_rss = anon + file + shmem;

	lastend = 0;
	chunk = 0;
	chunk_start = 0;
	used_size = 0;

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if ((vma->vm_start - lastend) >	chunk) {
			chunk = vma->vm_start - lastend;
			chunk_start = lastend;
		}
		lastend = vma->vm_end;
		used_size += vma->vm_end - vma->vm_start;
		if (dump_vma) {
			dump_one_vma(vma);
			dump_num++;
		}
	}

	if ((TASK_SIZE - lastend) > chunk) {
		chunk = TASK_SIZE - lastend;
		chunk_start = lastend;
	}

	pr_err("dbgmm task %s %d parent %s %d VmSize %luKB VmRSS %luKB "\
			"RssAnon %luKB RssFile %luKB RssShmem %luKB chunk %lu "\
			"chunk_start 0x%lx used_size %lu map_count %d len %lu "\
			"dump_num %d va_feature 0x%x mm->va_feature 0x%lx "\
			"flags %#lx\n",
			current->comm, current->pid,
			current->group_leader->comm,
			current->group_leader->pid,
			total_vm << (PAGE_SHIFT-10),
			total_rss << (PAGE_SHIFT-10),
			anon << (PAGE_SHIFT-10),
			file << (PAGE_SHIFT-10),
			shmem << (PAGE_SHIFT-10),
			chunk, chunk_start,
			used_size >> 10, mm->map_count, len, dump_num,
			va_feature, mm->va_feature, flags);
}
#endif/* CONFIG_DUMP_MM_INFO */
