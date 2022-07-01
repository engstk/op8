// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "process_reclaim: " fmt

#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/rmap.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/mm_inline.h>
#include <linux/process_mm_reclaim.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
extern bool is_fg(int uid);
static inline int task_is_fg(struct task_struct *tsk)
{	int cur_uid;
	cur_uid = task_uid(tsk).val;
	if (is_fg(cur_uid))
		return 1;
	return 0;
}

extern int swapin_walk_pmd_entry(pmd_t *pmd, unsigned long start,
		unsigned long end, struct mm_walk *walk);

/* check current need cancel reclaim or not, please check task not NULL first.
 * If the reclaimed task has goto foreground, cancel reclaim immediately
 */
#define RECLAIM_SCAN_REGION_LEN (400ul<<20)

enum reclaim_type {
	RECLAIM_FILE,
	RECLAIM_ANON,
	RECLAIM_ALL,
	RECLAIM_RANGE,
	/*
	 * add three reclaim_type that only reclaim inactive pages
         */
	RECLAIM_INACTIVE_FILE,
	RECLAIM_INACTIVE_ANON,
	RECLAIM_INACTIVE,
	RECLAIM_SWAPIN,
};

static int mm_reclaim_pte_range(pmd_t *pmd, unsigned long addr,
		unsigned long end, struct mm_walk *walk)
{
	struct reclaim_param *rp = walk->private;
	struct vm_area_struct *vma = rp->vma;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;
	LIST_HEAD(page_list);
	int isolated;
	int reclaimed;
	int ret = 0;

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	split_huge_pmd(vma, addr, pmd);
#else
	split_huge_pmd(vma, pmd, addr);
#endif
	if (pmd_trans_unstable(pmd) || !rp->nr_to_reclaim)
		return 0;
cont:
	isolated = 0;
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		/*
		 * check whether the reclaim process should cancel
                 */
		if (rp->reclaimed_task &&
				(ret = is_reclaim_addr_over(walk, addr))) {
			ret = -ret;
			break;
		}
		ptent = *pte;
		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/*
		 * do not reclaim page in active lru list
		 */
		if (rp->inactive_lru && (PageActive(page) ||
					PageUnevictable(page)))
			continue;

		if (isolate_lru_page(page))
			continue;

		/*
		 * MADV_FREE clears pte dirty bit and then marks the page
		 * lazyfree (clear SwapBacked). Inbetween if this lazyfreed page
		 * is touched by user then it becomes dirty.  PPR in
		 * shrink_page_list in try_to_unmap finds the page dirty, marks
		 * it back as PageSwapBacked and skips reclaim. This can cause
		 * isolated count mismatch.
		 */
		if (PageAnon(page) && !PageSwapBacked(page)) {
			putback_lru_page(page);
			continue;
		}

		list_add(&page->lru, &page_list);
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		/*
		 * only qualcomm need inc isolate count; mtk do it itself.
		 */
		inc_node_page_state(page, NR_ISOLATED_ANON +
				page_is_file_cache(page));
#endif
		isolated++;
		rp->nr_scanned++;
		if ((isolated >= SWAP_CLUSTER_MAX) || !rp->nr_to_reclaim)
			break;
	}
	pte_unmap_unlock(pte - 1, ptl);

	/*
	 * check whether the reclaim process should cancel
         */
	reclaimed = reclaim_pages_from_list(&page_list, vma, walk);

	rp->nr_reclaimed += reclaimed;
	rp->nr_to_reclaim -= reclaimed;
	if (rp->nr_to_reclaim < 0)
		rp->nr_to_reclaim = 0;

	/*
	 * if want to cancel, if ret <0 means need jump out of the loop immediately
	 */
	if (ret < 0)
		return ret;
	if (!rp->nr_to_reclaim)
		return -PR_FULL;
	if (addr != end)
		goto cont;
	return 0;
}

static noinline ssize_t reclaim_task_write(struct task_struct* task, char *buffer)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	enum reclaim_type type;
	char *type_buf;
	struct mm_walk reclaim_walk = {};
	unsigned long start = 0;
	unsigned long end = 0;
	struct reclaim_param rp;
	int err = 0;

	if (task == current->group_leader)
		goto out_err;

	type_buf = strstrip(buffer);
	if (!strcmp(type_buf, "file"))
		type = RECLAIM_FILE;
	else if (!strcmp(type_buf, "anon"))
		type = RECLAIM_ANON;
	else if (!strcmp(type_buf, "all"))
		type = RECLAIM_ALL;
	else if (!strcmp(type_buf, "inactive"))
		type = RECLAIM_INACTIVE;
	else if (!strcmp(type_buf, "inactive_file"))
		type = RECLAIM_INACTIVE_FILE;
	else if (!strcmp(type_buf, "inactive_anon"))
		type = RECLAIM_INACTIVE_ANON;
	else if (!strcmp(type_buf, "swapin"))
		type = RECLAIM_SWAPIN;
	else if (isdigit(*type_buf))
		type = RECLAIM_RANGE;
	else
		goto out_err;

	if (type == RECLAIM_RANGE) {
		char *token;
		unsigned long long len, len_in, tmp;
		token = strsep(&type_buf, " ");
		if (!token)
			goto out_err;
		tmp = memparse(token, &token);
		if (tmp & ~PAGE_MASK || tmp > ULONG_MAX)
			goto out_err;
		start = tmp;

		token = strsep(&type_buf, " ");
		if (!token)
			goto out_err;
		len_in = memparse(token, &token);
		len = (len_in + ~PAGE_MASK) & PAGE_MASK;
		if (len > ULONG_MAX)
			goto out_err;
		/*
		 * Check to see whether len was rounded up from small -ve
		 * to zero.
		 */
		if (len_in && !len)
			goto out_err;

		end = start + len;
		if (end < start)
			goto out_err;
	}

	mm = get_task_mm(task);
	if (!mm)
		goto out;

	/*
	 * Flag that relcaim inactive pages only in mm_reclaim_pte_range
	 */
	if ((type == RECLAIM_INACTIVE) ||
			(type == RECLAIM_INACTIVE_FILE) ||
			(type == RECLAIM_INACTIVE_ANON))
		rp.inactive_lru = true;
	else
		rp.inactive_lru = false;

	reclaim_walk.mm = mm;
	reclaim_walk.pmd_entry = mm_reclaim_pte_range;
	reclaim_walk.private = &rp;

	current->flags |= PF_RECLAIM_SHRINK;
	rp.reclaimed_task = task;
	current->reclaim.stop_jiffies = jiffies + RECLAIM_TIMEOUT_JIFFIES;

cont:
	rp.nr_to_reclaim = RECLAIM_PAGE_NUM;
	rp.nr_reclaimed = 0;
	rp.nr_scanned = 0;

	down_read(&mm->mmap_sem);
	if (type == RECLAIM_RANGE) {
		vma = find_vma(mm, start);
		while (vma) {
			if (vma->vm_start > end)
				break;
			if (is_vm_hugetlb_page(vma))
				continue;

			rp.vma = vma;
			walk_page_range(max(vma->vm_start, start),
					min(vma->vm_end, end),
					&reclaim_walk);
			vma = vma->vm_next;
		}
	} else if (type == RECLAIM_SWAPIN) {
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			if (is_vm_hugetlb_page(vma))
				continue;

			if (!vma_is_anonymous(vma))
				continue;

			/* if mlocked, don't reclaim it */
			if (vma->vm_flags & VM_LOCKED)
				continue;

			reclaim_walk.mm = mm;
			reclaim_walk.pmd_entry = swapin_walk_pmd_entry;
			reclaim_walk.private = vma;
			walk_page_range(max(vma->vm_start, start),
					min(vma->vm_end, end),
					&reclaim_walk);
		}
	} else {
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			if (vma->vm_end <= task->reclaim.stop_scan_addr)
				continue;

			if (is_vm_hugetlb_page(vma))
				continue;

			/*
			 * Jump out of the reclaim flow immediately
			 */
			err = is_reclaim_addr_over(&reclaim_walk, vma->vm_start);
			if (err) {
				err = -err;
				break;
			}

			/*
			 * filter only reclaim anon pages
			 */
			if ((type == RECLAIM_ANON ||
				type == RECLAIM_INACTIVE_ANON) && vma->vm_file)
				continue;

			/*
			 * filter only reclaim file-backed pages
			 */
			if ((type == RECLAIM_FILE ||
				type == RECLAIM_INACTIVE_FILE) && !vma->vm_file)
					continue;

			rp.vma = vma;
			if (vma->vm_start < task->reclaim.stop_scan_addr)
				err = walk_page_range(
						task->reclaim.stop_scan_addr,
						vma->vm_end, &reclaim_walk);
			else
				err = walk_page_range(vma->vm_start,
						vma->vm_end, &reclaim_walk);

			if (err < 0)
				break;
		}

		if (err != -PR_ADDR_OVER)
			task->reclaim.stop_scan_addr = vma ? vma->vm_start : 0;
	}

	flush_tlb_mm(mm);
	up_read(&mm->mmap_sem);

	if (type != RECLAIM_SWAPIN) {
		/*
		 * If not reach the mmap end, continue
		 * If timeout, reset the stop_jiffies after release mmap_sem.
		 */
		if (((err == -PR_TIME_OUT) || (err == PR_PASS) ||
			(err == -PR_ADDR_OVER) || (err == -PR_FULL)) && vma) {
			if (err == -PR_TIME_OUT)
				current->reclaim.stop_jiffies =
					jiffies + RECLAIM_TIMEOUT_JIFFIES;
			goto cont;
		}
		task->reclaim.stop_scan_addr = 0;
	}

	current->flags &= ~PF_RECLAIM_SHRINK;
	mmput(mm);
out:
	return 0;

out_err:
	return -EINVAL;
}

/*
 * If count < 0 means write sem locked
 */
static inline int rwsem_is_wlocked(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) < 0;
}

static inline int _is_reclaim_should_cancel(struct mm_walk *walk)
{
	struct mm_struct *mm = walk->mm;
	struct task_struct *task;

	if (!mm)
		return 0;

	task = ((struct reclaim_param *)(walk->private))->reclaimed_task;
	if (!task)
		return 0;

	if (mm != task->mm)
		return PR_TASK_DIE;
	if (rwsem_is_wlocked(&mm->mmap_sem))
		return PR_SEM_OUT;
#ifdef CONFIG_FG_TASK_UID
	if (task_is_fg(task))
		return PR_TASK_FG;
#endif
	if (task->state == TASK_RUNNING)
		return PR_TASK_RUN;
	if (time_is_before_eq_jiffies(current->reclaim.stop_jiffies))
		return PR_TIME_OUT;

	return 0;
}

int is_reclaim_should_cancel(struct mm_walk *walk)
{
	struct task_struct *task;

	if (!current_is_reclaimer() || !walk->private)
		return 0;

	task = ((struct reclaim_param *)(walk->private))->reclaimed_task;
	if (!task)
		return 0;

	return _is_reclaim_should_cancel(walk);
}

int is_reclaim_addr_over(struct mm_walk *walk, unsigned long addr)
{
	struct task_struct *task;

	if (!current_is_reclaimer() || !walk->private)
		return 0;

	task = ((struct reclaim_param *)(walk->private))->reclaimed_task;
	if (!task)
		return 0;

	if (task->reclaim.stop_scan_addr + RECLAIM_SCAN_REGION_LEN <= addr) {
		task->reclaim.stop_scan_addr = addr;
		return PR_ADDR_OVER;
	}

	return _is_reclaim_should_cancel(walk);
}

/*
 * Create /proc/process_reclaim interface for process reclaim.
 * Because /proc/$pid/reclaim has deifferent permissiones of different processes,
 * and can not set to 0444 because that have security risk.
 * Use /proc/process_reclaim and setting with selinux
 */
#ifdef CONFIG_PROC_FS
#define PROCESS_RECLAIM_CMD_LEN 64
static int process_reclaim_enable = 1;
module_param_named(process_reclaim_enable, process_reclaim_enable, int, 0644);

static ssize_t proc_reclaim_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *ppos)
{
	char kbuf[PROCESS_RECLAIM_CMD_LEN];
	char *act_str;
	char *end;
	long value;
	pid_t tsk_pid;
	struct task_struct* tsk;
	ssize_t ret = 0;

	if (!process_reclaim_enable) {
		pr_warn("Process memory reclaim is disabled!\n");
		return -EFAULT;
	}

	if (count > PROCESS_RECLAIM_CMD_LEN) {
		pr_warn("count %ld is over %d\n",
				count, PROCESS_RECLAIM_CMD_LEN);
		return -EINVAL;
	}

	memset(kbuf, 0, PROCESS_RECLAIM_CMD_LEN);
	if (copy_from_user(&kbuf, buffer, count))
		return -EFAULT;
	kbuf[PROCESS_RECLAIM_CMD_LEN - 1] = '\0';

	act_str = strstrip(kbuf);
	if (*act_str <= '0' || *act_str > '9') {
		pr_err("process_reclaim write [%s] pid format is invalid.\n",
				kbuf);
		return -EINVAL;
	}

	value = simple_strtol(act_str, &end, 10);
	if (value < 0 || value > INT_MAX) {
		pr_err("process_reclaim write [%s] is invalid.\n", kbuf);
		return -EINVAL;
	}

	tsk_pid = (pid_t)value;

	if (end == (act_str + strlen(act_str))) {
		pr_err("process_reclaim write [%s] do not set reclaim type.\n", kbuf);
		return -EINVAL;
	}

	if (*end != ' ' && *end != '	') {
		pr_err("process_reclaim write [%s] format is wrong.\n", kbuf);
		return -EINVAL;
	}

	end = strstrip(end);
	rcu_read_lock();
	tsk = find_task_by_vpid(tsk_pid);
	if (!tsk) {
		rcu_read_unlock();
		pr_err("process_reclaim can not find task of pid:%d\n", tsk_pid);
		return -ESRCH;
	}

	if (tsk != tsk->group_leader)
		tsk = tsk->group_leader;
	get_task_struct(tsk);
	rcu_read_unlock();

	ret = reclaim_task_write(tsk, end);

	put_task_struct(tsk);
	if (ret < 0) {
		pr_err("process_reclaim failed, command [%s]\n", kbuf);
		return ret;
	}

	return count;
}

static ssize_t process_reclaim_enable_write(struct file *file,
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

	ret = kstrtol(kbuf, 10, &val);
	if (ret)
		return -EINVAL;

	process_reclaim_enable = val ? 1 : 0;
	return len;
}

static ssize_t process_reclaim_enable_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[12] = {'0'};
	int len;

	len = snprintf(kbuf, 12, "%d\n", process_reclaim_enable);
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

static const struct file_operations proc_process_reclaim_enable_ops = {
	.write          = process_reclaim_enable_write,
	.read		= process_reclaim_enable_read,
};

int create_process_reclaim_enable_proc(struct proc_dir_entry *parent)
{
	if (parent && proc_create("process_reclaim_enable",
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
				parent, &proc_process_reclaim_enable_ops)) {
		printk("Register process_reclaim_enable interface passed.\n");
		return 0;
	}
	pr_err("Register process_reclaim_enable interface failed.\n");
	return -ENOMEM;
}

static struct file_operations process_reclaim_w_fops = {
	.write = proc_reclaim_write,
	.llseek = noop_llseek,
};

static inline void process_mm_reclaim_init_procfs(void)
{
	if (!proc_create("process_reclaim", 0222, NULL, &process_reclaim_w_fops))
		pr_err("Failed to register proc interface\n");
}
#else /* CONFIG_PROC_FS */
static inline void process_mm_reclaim_init_procfs(void)
{
}
#endif

static int __init process_reclaim_proc_init(void)
{
	process_mm_reclaim_init_procfs();
	return 0;
}

late_initcall(process_reclaim_proc_init);
MODULE_LICENSE("GPL");
