// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <linux/tick.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/list.h>
#include <linux/fcntl.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/iomonitor/iomonitor.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/rtmutex.h>
#include <linux/profile.h>
#include <linux/vmstat.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/ctype.h>

#define UID_HASH_BITS	8
DECLARE_HASHTABLE(uid_hash_table, UID_HASH_BITS);
static DEFINE_RT_MUTEX(uid_lock);

#define UID_STATE_DELTA 0
#define UID_STATE_TOTAL_CURR	1
#define UID_STATE_TOTAL_LAST	2
#define UID_STATE_DEAD_TASKS	3
#define UID_STATE_SIZE		4
#define TASK_LIMIT_COUNT	40000
#define TASK_LIMIT_DATA		(2*1024*1024)
#define TGID_TASK_LIMTI_DATA	(100*1024*1024)
#define UID_LIMIT_COUNT		2000
static int task_limit_count;
static int uid_limit_count;
struct io_stats {
	u64 read_bytes;
	u64 write_bytes;
	u64 rchar;
	u64 wchar;
	u64 fsync;
};
#define MAX_TASK_COMM_LEN 16
#define task_all_bytes(tsk)	\
	(tsk->ioac.read_bytes + compute_write_bytes(tsk))

struct task_entry {
	char comm[MAX_TASK_COMM_LEN];
	char tgid_comm[MAX_TASK_COMM_LEN];
	pid_t pid;
	bool is_kthread;
	int state;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
};

struct uid_entry {
	uid_t uid;
	int task_count;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
	 DECLARE_HASHTABLE(task_entries, UID_HASH_BITS);
};

static void trim_space(char *trim)
{
	char *s = trim;
	int cnt = 0;
	int i;

	for (i = 0; s[i]; i++) {
		if (!isspace(s[i]))
			s[cnt++] = s[i];
	}
	s[cnt] = '\0';
}

static u64 compute_write_bytes(struct task_struct *task)
{
	if (task->ioac.write_bytes <= task->ioac.cancelled_write_bytes)
		return 0;

	return task->ioac.write_bytes - task->ioac.cancelled_write_bytes;
}

static void compute_io_bucket_stats(struct io_stats *io_bucket,
				    struct io_stats *io_curr,
				    struct io_stats *io_last,
				    struct io_stats *io_dead)
{
	/* tasks could switch to another uid group, but its io_last in the
	 * previous uid group could still be positive.
	 * therefore before each update, do an overflow check first
	 */
	int64_t delta;

	delta = io_curr->read_bytes + io_dead->read_bytes - io_last->read_bytes;
	io_bucket->read_bytes = delta > 0 ? delta : 0;
	delta = io_curr->write_bytes + io_dead->write_bytes -
	    io_last->write_bytes;
	io_bucket->write_bytes = delta > 0 ? delta : 0;
	delta = io_curr->rchar + io_dead->rchar - io_last->rchar;
	io_bucket->rchar = delta > 0 ? delta : 0;
	delta = io_curr->wchar + io_dead->wchar - io_last->wchar;
	io_bucket->wchar = delta > 0 ? delta : 0;
	delta = io_curr->fsync + io_dead->fsync - io_last->fsync;
	io_bucket->fsync = delta > 0 ? delta : 0;

	io_last->read_bytes = io_curr->read_bytes;
	io_last->write_bytes = io_curr->write_bytes;
	io_last->rchar = io_curr->rchar;
	io_last->wchar = io_curr->wchar;
	io_last->fsync = io_curr->fsync;

	memset(io_dead, 0, sizeof(struct io_stats));
}

static inline bool task_is_kthread(struct task_struct *task)
{
	if (task->mm == NULL)
		return true;
	return false;
}

static struct uid_entry *find_uid_entry(uid_t uid)
{
	struct uid_entry *uid_entry;

	hash_for_each_possible(uid_hash_table, uid_entry, hash, uid) {
		if (uid_entry->uid == uid)
			return uid_entry;
	}
	return NULL;
}

static struct uid_entry *find_or_register_uid(uid_t uid)
{
	struct uid_entry *uid_entry = NULL;

	uid_entry = find_uid_entry(uid);
	if (uid_entry)
		return uid_entry;

	if (uid_limit_count > UID_LIMIT_COUNT)
		return NULL;
	uid_entry = kzalloc(sizeof(struct uid_entry), GFP_ATOMIC);
	if (!uid_entry)
		return NULL;
	uid_entry->uid = uid;
	hash_init(uid_entry->task_entries);
	hash_add(uid_hash_table, &uid_entry->hash, uid);
	uid_limit_count++;

	return uid_entry;
}

static struct task_entry *find_task_entry(struct uid_entry *uid_entry,
					  struct task_struct *task)
{
	struct task_entry *task_entry;

	hash_for_each_possible(uid_entry->task_entries, task_entry, hash,
			       task->pid) {
		if (task->pid == task_entry->pid) {
			/* if thread name changed, update the entire command */
			int len = strlen(task->comm);

			if (strncmp(task_entry->comm, task->comm, len))
				__get_task_comm(task_entry->comm, TASK_COMM_LEN, task);
			trim_space(task_entry->comm);
			return task_entry;
		}
	}
	return NULL;
}

static struct task_entry *find_or_register_task(struct uid_entry *uid_entry,
						struct task_struct *task,
						int state)
{
	struct task_entry *task_entry = NULL;
	struct task_struct *tgid_task = NULL;
	pid_t pid = task->pid;

	task_entry = find_task_entry(uid_entry, task);
	if (task_entry)
		return task_entry;
	if (task_limit_count > TASK_LIMIT_COUNT)
		return NULL;
	if (state == TASK_DEAD && (task_all_bytes(task) < TASK_LIMIT_DATA))
		return NULL;
	task_entry = kzalloc(sizeof(struct task_entry), GFP_ATOMIC);
	if (!task_entry)
		return NULL;
	__get_task_comm(task_entry->comm, TASK_COMM_LEN, task);
	trim_space(task_entry->comm);

	if (task_all_bytes(task) > TGID_TASK_LIMTI_DATA) {
		tgid_task = find_task_by_pid_ns(task->tgid, &init_pid_ns);
		if (tgid_task) {
			__get_task_comm(task_entry->tgid_comm, TASK_COMM_LEN,
					tgid_task);
			trim_space(task_entry->tgid_comm);
		}
	}
	task_entry->pid = pid;
	task_entry->is_kthread = task_is_kthread(task);
	hash_add(uid_entry->task_entries, &task_entry->hash, (unsigned int)pid);
	task_limit_count++;

	return task_entry;
}

static void add_uid_tasks_io_stats(struct uid_entry *uid_entry,
				   struct task_struct *task, int slot,
				   int state)
{
	struct io_stats *task_io_slot;
	struct task_entry *task_entry =
	    find_or_register_task(uid_entry, task, state);
	if (!task_entry)
		return;

	task_entry->state = state;
	task_io_slot = &task_entry->io[slot];

	task_io_slot->read_bytes += task->ioac.read_bytes;
	task_io_slot->write_bytes += compute_write_bytes(task);
	task_io_slot->rchar += task->ioac.rchar;
	task_io_slot->wchar += task->ioac.wchar;
	task_io_slot->fsync += task->ioac.syscfs;
}

static void add_uid_io_stats(struct uid_entry *uid_entry,
			     struct task_struct *task, int slot, int state)
{
	struct io_stats *io_slot = &uid_entry->io[slot];

	io_slot->read_bytes += task->ioac.read_bytes;
	io_slot->write_bytes += compute_write_bytes(task);
	io_slot->rchar += task->ioac.rchar;
	io_slot->wchar += task->ioac.wchar;
	io_slot->fsync += task->ioac.syscfs;

	add_uid_tasks_io_stats(uid_entry, task, slot, state);
}

static void set_io_uid_tasks_zero(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		memset(&task_entry->io[UID_STATE_TOTAL_CURR], 0,
		       sizeof(struct io_stats));
	}
}

static void compute_io_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	uid_entry->task_count = 0;
	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		uid_entry->task_count++;
		compute_io_bucket_stats(&task_entry->io[UID_STATE_DELTA],
					&task_entry->io[UID_STATE_TOTAL_CURR],
					&task_entry->io[UID_STATE_TOTAL_LAST],
					&task_entry->io[UID_STATE_DEAD_TASKS]);
	}
}

static void show_io_uid_tasks(struct seq_file *m, struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;
	u64 task_all_bytes;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		/* Separated by comma because space exists in task comm */
		task_all_bytes =
		    task_entry->io[UID_STATE_DELTA].read_bytes +
		    task_entry->io[UID_STATE_DELTA].write_bytes;
		if (task_all_bytes >= TASK_SHOW_DAILY_LIMIT)
			seq_printf(m,
				   "task:%s %lu %llu %llu %llu %llu %llu %d %s\n",
				   task_entry->comm,
				   (unsigned long)task_entry->pid,
				   task_entry->io[UID_STATE_DELTA].rchar,
				   task_entry->io[UID_STATE_DELTA].wchar,
				   task_entry->io[UID_STATE_DELTA].read_bytes,
				   task_entry->io[UID_STATE_DELTA].write_bytes,
				   task_entry->io[UID_STATE_DELTA].fsync,
				   task_entry->is_kthread,
				   task_entry->tgid_comm);

	}
}

static void remove_dead_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_entry->task_entries, bkt_task,
			   tmp_task, task_entry, hash) {
		if (task_entry->state == TASK_DEAD) {
			hash_del(&task_entry->hash);
			kfree(task_entry);
			uid_entry->task_count--;
			task_limit_count--;
		}
	}
}

static void remove_all_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_entry->task_entries, bkt_task,
			   tmp_task, task_entry, hash) {
		if (task_entry) {
			hash_del(&task_entry->hash);
			kfree(task_entry);
			task_limit_count--;
		}
	}
}

static void remove_no_task_uid_entry(void)
{
	struct uid_entry *uid_entry = NULL;
	unsigned long bkt;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_hash_table, bkt, tmp_task, uid_entry, hash) {
		if (uid_entry->task_count == 0) {
			hash_del(&uid_entry->hash);
			kfree(uid_entry);
			uid_limit_count--;
		}
	}
}

static int uid_io_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry = NULL;
	unsigned long bkt;
	u64 all_bytes;

	rt_mutex_lock(&uid_lock);
	hash_for_each(uid_hash_table, bkt, uid_entry, hash) {
		seq_printf(m, "%d %llu %llu %llu %llu %llu %d\n",
			   uid_entry->uid,
			   uid_entry->io[UID_STATE_DELTA].rchar,
			   uid_entry->io[UID_STATE_DELTA].wchar,
			   uid_entry->io[UID_STATE_DELTA].read_bytes,
			   uid_entry->io[UID_STATE_DELTA].write_bytes,
			   uid_entry->io[UID_STATE_DELTA].fsync,
			   uid_entry->task_count);

		all_bytes =
		    uid_entry->io[UID_STATE_DELTA].read_bytes +
		    uid_entry->io[UID_STATE_DELTA].write_bytes;
		if (all_bytes >= UID_SHOW_DAILY_LIMIT)
			show_io_uid_tasks(m, uid_entry);
	}
	rt_mutex_unlock(&uid_lock);
	return 0;
}

int exit_uid_find_or_register(struct task_struct *task)
{
	struct uid_entry *uid_entry;
	uid_t uid;

	if (!task)
		return NOTIFY_OK;

	rt_mutex_lock(&uid_lock);
	uid = from_kuid_munged(current_user_ns(), task_uid(task));
	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		pr_err("%s: failed to find uid %d\n", __func__, uid);
		goto exit;
	}
	add_uid_io_stats(uid_entry, task, UID_STATE_DEAD_TASKS, TASK_DEAD);

 exit:
	rt_mutex_unlock(&uid_lock);
	return NOTIFY_OK;
}

void free_all_uid_entry(void)
{
	struct uid_entry *uid_entry = NULL;
	unsigned long bkt;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_hash_table, bkt, tmp_task, uid_entry, hash) {
		if (uid_entry) {
			remove_all_uid_tasks(uid_entry);
			hash_del(&uid_entry->hash);
			kfree(uid_entry);
			uid_limit_count--;
		}
	}

}

static int uidio_show_open(struct inode *inode, struct file *file)
{
	struct uid_entry *uid_entry = NULL;
	struct task_struct *temp, *task;
	unsigned long bkt;
	uid_t uid;

	rt_mutex_lock(&uid_lock);
	hash_for_each(uid_hash_table, bkt, uid_entry, hash) {
		memset(&uid_entry->io[UID_STATE_TOTAL_CURR], 0,
		       sizeof(struct io_stats));
		set_io_uid_tasks_zero(uid_entry);
	}

	rcu_read_lock();
	do_each_thread(temp, task) {
		if (!task->ioac.read_bytes && !task->ioac.write_bytes)
			continue;
		uid = from_kuid_munged(current_user_ns(), task_uid(task));
		uid_entry = find_or_register_uid(uid);
		if (!uid_entry)
			continue;
		add_uid_io_stats(uid_entry, task, UID_STATE_TOTAL_CURR,
				 task->state);
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(uid_hash_table, bkt, uid_entry, hash) {
		compute_io_bucket_stats(&uid_entry->io[UID_STATE_DELTA],
					&uid_entry->io[UID_STATE_TOTAL_CURR],
					&uid_entry->io[UID_STATE_TOTAL_LAST],
					&uid_entry->io[UID_STATE_DEAD_TASKS]);
		compute_io_uid_tasks(uid_entry);
	}
	rt_mutex_unlock(&uid_lock);
	return single_open(file, uid_io_show, inode->i_private);
}

static int uidio_show_release(struct inode *inode, struct file *file)
{
	struct uid_entry *uid_entry = NULL;
	unsigned long bkt;

	printk("uid debug: %d %d\n", task_limit_count, uid_limit_count);
	rt_mutex_lock(&uid_lock);
	hash_for_each(uid_hash_table, bkt, uid_entry, hash) {
		remove_dead_uid_tasks(uid_entry);
	}
	remove_no_task_uid_entry();
	rt_mutex_unlock(&uid_lock);
	return single_release(inode, file);
}

static const struct file_operations proc_uidio_show_operations = {
	.open = uidio_show_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = uidio_show_release,
};

struct proc_dir_entry *create_uid_proc(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *dir_entry = NULL;

	dir_entry =
	    proc_create("uid_io_show", S_IRUGO | S_IWUGO, parent,
			&proc_uidio_show_operations);

	return dir_entry;
}
