// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/ratelimit.h>
#include <linux/blkdev.h>
#include <linux/cpufreq.h>
#include <linux/blkdev.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>

#ifdef CONFIG_VIRTUAL_RESERVE_MEMORY
#include <linux/resmap_account.h>
#endif
#include <linux/vm_anti_fragment.h>
#include <linux/security.h>
#define ohm_err(fmt, ...) \
        printk(KERN_ERR "[OHM_ERR][%s]"fmt, __func__, ##__VA_ARGS__)
int vm_fra_op_enabled = 1;
extern void ohm_action_trig_with_msg(int type, char *msg);
static ssize_t vm_fra_op_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;

        len = sprintf( page, "%d\n", vm_fra_op_enabled);

        if (len > *off) {
                len -= *off;
        } else {
                len = 0;
        }
        if (copy_to_user(buff, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}

static ssize_t vm_fra_op_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
        char write_data[32] = {0};
        int len_tmp = len;

        if (len > 31)
            len = 31;
		if (buff == NULL || len < 1)
			return -EFAULT;

        if (copy_from_user(&write_data, buff, len)) {
                ohm_err("write error.\n");
                return -EFAULT;
        }
        write_data[len] = '\0';
        if (write_data[len - 1] == '\n') {
                write_data[len - 1] = '\0';
        }

        if (0 == strncmp(write_data, "0", 1)) {
            vm_fra_op_enabled = 0;
        } else {
            vm_fra_op_enabled = 1;
        }
        return len_tmp;
}

const struct file_operations vm_fra_op_fops = {
        .read = vm_fra_op_read,
        .write = vm_fra_op_write,
};

int cpu_oom_event_enable = 1;
static unsigned long prev_jiffies;
static int prev_pid = -1;
static unsigned long rest_size;
void trigger_cpu_oom_event(unsigned long len)
{
	int fra = 0, rvma = 0,  pid = current->pid;
	unsigned int tmp = 0, rest = 0, rest_reserve = 0, subtree = 0, subtree_reserve = 0,
			chunk = 0, chunk_reserve = 0;
	char *svm_oom_msg = NULL;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long gap_start = 0, gap_end = 0, chunk_start = 0, chunk_start_reserve = 0;

	if (!cpu_oom_event_enable || !mm)
		return;

    if (mm->reserve_vma)
		rvma = 1;
	vma = mm->mmap;
	gap_end = max(PAGE_SIZE, mmap_min_addr);
	while (vma) {
		gap_start = vma->vm_start;

		if (gap_start < gap_end) {
			vma = vma->vm_next;
			continue;
		}

		tmp = gap_start - gap_end > 0 ?
			gap_start - gap_end : 0;
		rest += tmp;

		if (chunk < tmp) {
			chunk = tmp;
			chunk_start = gap_end;
		}
		gap_end = vma->vm_end;
		vma = vma->vm_next;
		if (tmp >= len) {
			subtree++;
		}
	}
	if (gap_end < 0xffffffff) {
		rest += 0xffffffff - gap_end;
		subtree++;
	}

	if (!mm->reserve_vma || !mm->reserve_mmap)
		goto ignore_reserve_subtree;
	gap_end =  mm->reserve_vma->vm_start;

	for (vma = mm->reserve_mmap; vma; vma = vma->vm_next) {
		gap_start = vma->vm_start;

		if (gap_start < gap_end)
			continue;

		if (gap_start >=  mm->reserve_vma->vm_start &&
			gap_start <= mm->reserve_vma->vm_end) {

			tmp = gap_start - gap_end > 0 ?
				gap_start - gap_end : 0;

			if (chunk_reserve < tmp) {
				chunk_reserve = tmp;
				chunk_start_reserve = gap_end;
			}
			rest_reserve += tmp;
			if (tmp >= len) {
				subtree_reserve++;
			}
		} else {
			break;
		}
		gap_end = vma->vm_end;
	}

	if (gap_end < mm->reserve_vma->vm_end) {
		rest_reserve += mm->reserve_vma->vm_end - gap_end;
		subtree_reserve++;
	}

ignore_reserve_subtree:
	if ((rest + rest_reserve > len * 10) &&
		(rest + rest_reserve > HUNDRED_M))
		fra = 1;

	if (pid == prev_pid && time_before(jiffies, prev_jiffies + CPU_OOM_TRIGGER_GAP)
			&& (rest + rest_reserve <= rest_size)) {
		return;
	}
	svm_oom_msg = (char*)kmalloc(128, GFP_KERNEL);
	if (!svm_oom_msg)
		return;
	len = snprintf(svm_oom_msg, 127,
				"{cpu_oom: len: %lu,pid: %d,type: %s,%u,%u,%u,%u,%d}",
				len, pid,
				(fra ? "fra" : "no_fra"),
				rest, subtree, rest_reserve, subtree_reserve, rvma);
	svm_oom_msg[len] = '\0';

#ifdef CONFIG_OPLUS_HEALTHINFO
	ohm_action_trig_with_msg(OHM_MEM_VMA_ALLOC_ERR, svm_oom_msg);
#endif

	prev_pid = current->pid;
	rest_size = rest + rest_reserve;
	prev_jiffies = jiffies;
	kfree(svm_oom_msg);
	return;
}

int vm_search_two_way = 2;
ssize_t vm_search_two_way_op_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;

		len = sprintf( page, "%d\n", vm_search_two_way);

        if (len > *off) {
                len -= *off;
        } else {
                len = 0;
        }
        if (copy_to_user(buff, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}

static ssize_t vm_search_two_way_op_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
        char write_data[32] = {0};
		struct task_struct *task = current;
		struct mm_struct *mm = task->mm;

		if (len > 31)
			len = 31;
		if (buff == NULL || len < 1)
			return -EFAULT;

		if (!mm)
			return len;

        if (copy_from_user(&write_data, buff, len)) {
                ohm_err("write error.\n");
                return -EFAULT;
        }
        write_data[len] = '\0';
        if (write_data[len - 1] == '\n') {
                write_data[len - 1] = '\0';
        }
		/*
		* 0: disable current process vm_search_two_way
		* 1: enable  current process vm_search_two_way
		* 2: disable vm_search_two_way
		* 3: enable  vm_search_two_way
		*
		*/
        if (0 == strncmp(write_data, "1", 1) && vm_search_two_way == 3 && mm) {
			mm->vm_search_two_way = true;
		} else if (0 == strncmp(write_data, "0", 1) && mm) {
			mm->vm_search_two_way = false;
		} else if (0 == strncmp(write_data, "3", 1)) {
			vm_search_two_way = 3;
        } else if (0 == strncmp(write_data, "2", 1)) {
			vm_search_two_way = 2;
        }
        return len;
}

const struct file_operations vm_search_two_way_fops = {
        .read = vm_search_two_way_op_read,
        .write = vm_search_two_way_op_write,
};
