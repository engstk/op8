// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/trace_clock.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/namei.h>
#include <linux/f2fs_fs.h>
#include <linux/blktrace_api.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/iomonitor/iomonitor.h>
#include <linux/iomonitor/iotrace.h>

/* data type for block offset of block group */
typedef int ext4_grpblk_t;
/* data type for filesystem-wide blocks number */
typedef unsigned long long ext4_fsblk_t;
/* data type for file logical block number */
typedef __u32 ext4_lblk_t;
/* data type for block group number */
typedef unsigned int ext4_group_t;
#include <trace/events/ext4.h>
#include <trace/events/block.h>
#include <trace/events/mmc.h>
#include <trace/events/scsi.h>
#include <trace/events/sched.h>

/********************************************************************/
#define do_posix_clock_monotonic_gettime(ts) ktime_get_ts(ts)
#define io_trace_print(fmt, arg...) \
	printk("[[IO_TRACE] %s:%d]"fmt, __func__, __LINE__, ##arg)
#define io_trace_assert(cond) \
{ \
	if (!(cond)) \
		io_trace_print("[IO_TRACE] %s\n", #cond); \
}
#define IO_TRACE_PARENT_END     (10)

enum IO_TRACE_ACTION {
	SYS_READ_TIMEOUT_TAG = 0x1,
	TRACE_BEGIN_TAG = SYS_READ_TIMEOUT_TAG,
	SYS_WRITE_TIMEOUT_TAG,
	SYS_SYNC_TIMEOUT_TAG,

	BLOCK_BEGIN_TAG = SYS_SYNC_TIMEOUT_TAG,

	BLOCK_GETRQ_TAG,
	BLOCK_RQ_INSERT_TAG,
	BLOCK_RQ_ISSUE_TAG,
	BLOCK_RQ_COMPLETE_TAG,
	BLOCK_RQ_REQUEUE_TAG,
	BLOCK_RQ_SLEEP_TAG,
	BLOCK_END_TAG = BLOCK_RQ_SLEEP_TAG,

	SCSI_BLK_CMD_RW_ERROR_TAG,
	SCSI_BLK_CMD_RW_TIMEOUT_TAG,

	TRACE_END_TAG = SCSI_BLK_CMD_RW_TIMEOUT_TAG,

	SCHED_STAT_IOWAIT_TAG,

};

struct io_trace_action {
	unsigned int action;
	unsigned char *name;
};

/*lint -save -e64*/
enum IO_TRACE_TYPE {
	__IO_TRACE_WRITE,

	__IO_TRACE_SYNC = 4,
	__IO_TRACE_META,	/* metadata io request */
	__IO_TRACE_PRIO,	/* boost priority in cfq */
	__IO_TRACE_DISCARD,	/* request to discard sectors */
	__IO_TRACE_SECURE_ERASE,
	__IO_TRACE_WRITE_SAME,
	__IO_TRACE_FLUSH,
	__IO_TRACE_FUA,
	__IO_TRACE_FG,
};

enum TRACE_TYPE {
	TRACE_SYSCALL,
	TRACE_BLK,
	TRACE_DEV,
	TRACE_IOWAIT,
	TRACE_END,
};

#define IO_TRACE_READ   (0)
#define IO_TRACE_WRITE  (1 << __IO_TRACE_WRITE)
#define IO_TRACE_SYNC   (1 << __IO_TRACE_SYNC)
#define IO_TRACE_META   (1 << __IO_TRACE_META)
#define IO_TRACE_DISCARD (1 << __IO_TRACE_DISCARD)
#define IO_TRACE_SECURE_ERASE (1 << __IO_TRACE_SECURE_ERASE)
#define IO_TRACE_WRITE_SAME  (1 <<  __IO_TRACE_WRITE_SAME)
#define IO_TRACE_FLUSH (1 << __IO_TRACE_FLUSH)
#define IO_TRACE_FUA (1 << __IO_TRACE_FUA)
#define IO_TRACE_FG (1 << __IO_TRACE_FG)

#define io_ns_to_ms(ns) ((ns) / 1000000)
#define IO_MAX_GLOBAL_ENTRY (1024 * 1024)

#define IO_MAX_LOG_PER_GLOBAL \
	(((IO_MAX_GLOBAL_ENTRY) / sizeof(struct io_trace_log_entry)) - 1)
#define IO_F_NAME_LEN  (32)
#define IO_P_NAME_LEN  (16)
#define IO_TRACE_SYSCALL_TIMEOUT  (1000)

struct io_trace_entry_head {
	char type;
	char len;
	char val[0];
};

struct io_trace_syscall_entry {
	unsigned char action;
	u64 delay;
	unsigned char f_name[IO_F_NAME_LEN];
	unsigned char comm[IO_P_NAME_LEN];
	pid_t pid;
	unsigned long time;
};

struct io_trace_blk_entry {
	unsigned char action;
	unsigned int rw;
	unsigned long sector;
	unsigned int nr_bytes;
	unsigned char dev_major;
	unsigned char dev_minor;

	union {
		unsigned short nr_rqs_sync;
		unsigned short in_flight_sync;
	};
	union {
		unsigned short nr_rqs_async;
		unsigned short in_flight_async;
	};
	unsigned long time;
};

struct io_trace_dev_entry {
	unsigned char action;
	unsigned int rw;
	unsigned long sector;
	int result;
	unsigned long time;
};
struct io_trace_iowait_entry {
	unsigned char action;
	unsigned char comm[IO_P_NAME_LEN];
	unsigned char wchan[IO_P_NAME_LEN];
	pid_t pid;
	u64 delay;
	unsigned char ux_flag;
	unsigned long time;
};

struct io_trace_ctrl {
	struct platform_device *pdev;
	unsigned int enable;

	unsigned int size;	/* size of buffer */
	unsigned int first;	/* start pos of first record */
	unsigned int last;	/* end pos of last record */

	unsigned char *all_trace_buff;
};

struct iotrace_debug_info {
	unsigned int pos_err_cnt;
	int before_loop_pos;
	unsigned int before_loop_last;
	ktime_t enter_loop_time;
	int in_loop_pos;
	unsigned int in_loop_first;
	unsigned int in_loop_last;
	ktime_t err_time;
};

static struct iotrace_debug_info *otrace_dbg_info;
static struct io_trace_ctrl *io_trace_this;
static struct io_trace_iowait_entry iowait_entry;
static struct io_trace_syscall_entry syscall_entry;
static struct io_trace_blk_entry blk_entry;
static struct io_trace_dev_entry dev_entry;

static struct ufs_hba *fsr_hba;
static spinlock_t global_spinlock;
static struct mutex output_mutex;
static DEFINE_SPINLOCK(global_spinlock);
static DEFINE_MUTEX(output_mutex);

static int trace_mm_init(struct io_trace_ctrl *trace_ctrl);
static void io_trace_setup(struct io_trace_ctrl *trace_ctrl);

static ssize_t sysfs_io_trace_attr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);
static ssize_t sysfs_io_trace_attr_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);

#define IO_TRACE_DEVICE_ATTR(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR, \
	sysfs_io_trace_attr_show, \
	sysfs_io_trace_attr_store)

static IO_TRACE_DEVICE_ATTR(enable);

static struct attribute *io_trace_attrs[] = {
	&dev_attr_enable.attr,

	/*&io_log_data_attr.attr, */
	NULL,
};

struct attribute_group io_trace_attr_group = {
	.name = "iotrace",
	.attrs = io_trace_attrs,
};

static ssize_t sysfs_io_trace_attr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t ret = 0;

	if (attr == &dev_attr_enable)
		ret = sprintf(buf, "%d\n", io_trace_this->enable);

	return ret;
}

static ssize_t sysfs_io_trace_attr_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	unsigned int value;

	if (attr == &dev_attr_enable) {
		mutex_lock(&output_mutex);
		sscanf(buf, "%u", &value);
		if (value == 1)
			io_trace_this->enable = value;
		if (value == 0)
			io_trace_this->enable = value;
		mutex_unlock(&output_mutex);
	}

	return count;
}

static struct kobject *kobject_ts;
static int io_trace_probe(struct platform_device *pdev)
{
	int ret;

	kobject_ts = kobject_create_and_add("io_log_data", NULL);
	if (!kobject_ts) {
		io_trace_print("create object failed!\n");
		return -1;
	}

	ret = sysfs_create_group(kobject_ts, &io_trace_attr_group);
	if (ret) {
		kobject_put(kobject_ts);
		io_trace_print("create group failed!\n");
		return -1;
	}
	return ret;
}

static struct platform_driver io_trace_driver = {
	.probe = io_trace_probe,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "io_trace",
		   },
};

static inline int is_syscall_action(unsigned int action)
{
	/*only the syscall log */
	if (action >= TRACE_BEGIN_TAG && action <= BLOCK_BEGIN_TAG)
		return 1;

	return 0;
}

static inline int is_block_action(unsigned int action)
{
	/*only the block log */
	if (action > BLOCK_BEGIN_TAG && action <= BLOCK_END_TAG)
		return 1;

	return 0;
}

static inline int is_dev_action(unsigned int action)
{
	/*only the device log */
	if (action > BLOCK_END_TAG && action <= TRACE_END_TAG)
		return 1;

	return 0;
}

static inline struct io_trace_entry_head *get_entry_head(int pos)
{

	if (unlikely(pos >= io_trace_this->size))
		pos %= io_trace_this->size;

	return (struct io_trace_entry_head *)(io_trace_this->all_trace_buff +
					      pos);
}

static inline void io_trace_record_log(char type, char entry_size, void *entry)
{
	struct io_trace_entry_head *entry_head;
	int len, first, remain, pos;
	unsigned long flags;

	len = sizeof(struct io_trace_entry_head) + entry_size;
	spin_lock_irqsave(&global_spinlock, flags);

	pos = io_trace_this->last % io_trace_this->size;
	remain = io_trace_this->size - pos;

	/* mark the end of the record */
	if (unlikely(remain < len)) {
		entry_head = get_entry_head(pos);
		entry_head->type = TRACE_END;
		io_trace_this->last = io_trace_this->size + len;
		first = 1;
		pos = 0;
	} else {
		if (io_trace_this->first == 0
		    && io_trace_this->last == io_trace_this->size) {
			first = 1;

		} else {
			first = io_trace_this->first;

		}
		io_trace_this->last += len;
	}
	/* mark the start of record */
	if (first && (first < pos + len)) {
		if (unlikely(first == 1))
			first = 0;
		while (first < pos + len) {

			entry_head = get_entry_head(first);
			if (entry_head->type == TRACE_END) {
				first = 0;
				break;
			} else {
				first += entry_head->len;
			}
		}
		if (!first && io_trace_this->last >= io_trace_this->size)
			io_trace_this->last -= io_trace_this->size;
		if (first == io_trace_this->size &&
			io_trace_this->last >= io_trace_this->size) {
			first -= io_trace_this->size;
			io_trace_this->last -= io_trace_this->size;
		}
		io_trace_this->first = first;
	}

	spin_unlock_irqrestore(&global_spinlock, flags);
	entry_head = get_entry_head(pos);
	entry_head->type = type;
	entry_head->len = len;

	memcpy(entry_head->val, entry, entry_size);

	return;
}

static void io_trace_global_log(unsigned int action, unsigned long sector,
				unsigned int nr_bytes, struct file *filp,
				unsigned int rw_flags, struct request_queue *rq,
				unsigned int dev_major, unsigned int dev_minor,
				int result, struct task_struct *tsk, u64 delay)
{
	struct timespec realts;
	unsigned long log_time;

	do_posix_clock_monotonic_gettime(&realts);
	log_time = (unsigned long)realts.tv_sec * 1000000000 + realts.tv_nsec;

	if (sector == ((unsigned long)-1))
		sector = (unsigned long)0;

	if (is_block_action(action)) {
		blk_entry.action = action;
		blk_entry.rw = rw_flags;
		blk_entry.sector = sector;
		blk_entry.nr_bytes = nr_bytes;
		blk_entry.dev_major = (unsigned char)dev_major;
		blk_entry.dev_minor = (unsigned char)dev_minor;

		if (action == BLOCK_GETRQ_TAG) {
			blk_entry.nr_rqs_sync = (unsigned short)rq->nr_rqs[1];
			blk_entry.nr_rqs_async = (unsigned short)rq->nr_rqs[0];
		} else if (action == BLOCK_RQ_ISSUE_TAG) {
			blk_entry.in_flight_sync =
			    (unsigned short)rq->in_flight[1];
			blk_entry.in_flight_async =
			    (unsigned short)rq->in_flight[0];

		}
		blk_entry.time = log_time;

		io_trace_record_log(TRACE_BLK,
				    sizeof(struct io_trace_blk_entry),
				    &blk_entry);
	} else if (is_dev_action(action)) {
		dev_entry.action = action;
		dev_entry.rw = rw_flags;
		dev_entry.sector = sector;
		dev_entry.result = result;
		dev_entry.time = log_time;

		io_trace_record_log(TRACE_DEV,
				    sizeof(struct io_trace_dev_entry),
				    &dev_entry);
	} else if (action == SCHED_STAT_IOWAIT_TAG) {
		iowait_entry.action = action;
		iowait_entry.pid = tsk->pid;
		iowait_entry.delay = io_ns_to_ms(delay);
		iowait_entry.time = log_time;
		iowait_entry.ux_flag = (unsigned char)result;
		memcpy(iowait_entry.comm, tsk->comm, IO_P_NAME_LEN);
		snprintf(iowait_entry.wchan, sizeof(iowait_entry.wchan), "%ps",
			 (void *)get_wchan(tsk));

		io_trace_record_log(TRACE_IOWAIT,
				    sizeof(struct io_trace_iowait_entry),
				    &iowait_entry);
	} else if (is_syscall_action(action)) {
		syscall_entry.action = action;
		syscall_entry.delay = delay;
		syscall_entry.pid = current->pid;
		memcpy(syscall_entry.comm, current->comm, IO_P_NAME_LEN);

		memset(syscall_entry.f_name, '\0', IO_F_NAME_LEN);
		memcpy(syscall_entry.f_name, filp->f_path.dentry->d_iname,
		       IO_F_NAME_LEN);

		syscall_entry.time = log_time;

		io_trace_record_log(TRACE_SYSCALL,
				    sizeof(struct io_trace_syscall_entry),
				    &syscall_entry);
	}
	return;
}

int write_log_kernel(char **data)
{

	int ret = -1, pos, before_loop_pos;
	loff_t file_pos = 0;
	unsigned char buf[512];
	unsigned len;
	unsigned int before_loop_last;
	ktime_t enter_loop_time;

	struct io_trace_iowait_entry *iowait_entry = NULL;
	struct io_trace_blk_entry *blk_entry = NULL;
	struct io_trace_dev_entry *dev_entry = NULL;
	struct io_trace_entry_head *entry_head = NULL;
	struct io_trace_syscall_entry *syscall_entry = NULL;

	struct file *filp = (struct file *)data;

	if (io_trace_this == NULL) {
		io_trace_print("io_trace_this is NULL!\n");
		return ret;
	}
	mutex_lock(&output_mutex);
	if (!io_trace_this->enable) {
		io_trace_print("io trace enable failed!\n");
		mutex_unlock(&output_mutex);
		return ret;
	}

	io_trace_this->enable = 0;

	if (filp == NULL) {
		io_trace_print("filp is null\n");
		goto end;
	}
	if (IS_ERR(filp)) {
		io_trace_print("file descriptor to is invalid: %ld\n",
			       PTR_ERR(filp));
		goto end;
	}

	pos = io_trace_this->first;
	before_loop_pos = pos;
	before_loop_last = io_trace_this->last;
	enter_loop_time = ktime_get();
	while (pos >= io_trace_this->first && pos < io_trace_this->last) {

		entry_head = get_entry_head(pos);

		/* Check end flag first, avoid illegal memory access */
		if (entry_head->type == TRACE_END) {
			pos = io_trace_this->size;
			continue;
		}

		/* Avoid while dead loop problem, but can cause invalid log */
		if ((int)entry_head->len <= 0) {
			if (otrace_dbg_info) {
				otrace_dbg_info->pos_err_cnt += 1;
				otrace_dbg_info->before_loop_pos = before_loop_pos;
				otrace_dbg_info->before_loop_last = before_loop_last;
				otrace_dbg_info->enter_loop_time = enter_loop_time;
				otrace_dbg_info->in_loop_pos = pos;
				otrace_dbg_info->in_loop_first = io_trace_this->first;
				otrace_dbg_info->in_loop_last = io_trace_this->last;
				otrace_dbg_info->err_time = ktime_get();
			}
			pr_err("IOMonitor: dead loop will happen, but it is circumvented. "
				"first=%d, last=%d, pos=%d.\n", io_trace_this->first,
				io_trace_this->last, pos);
			break;
		}

		pos += entry_head->len;

		switch (entry_head->type) {
		case TRACE_SYSCALL:
			syscall_entry =
			    (struct io_trace_syscall_entry *)entry_head->val;
			len =
			    snprintf(buf, sizeof(buf), "%lu %d %d %s %s %llu\n",
				     syscall_entry->time, syscall_entry->action,
				     syscall_entry->pid, syscall_entry->comm,
				     syscall_entry->f_name,
				     syscall_entry->delay);
			ret = kernel_write(filp, buf, len, &file_pos);
			break;
		case TRACE_IOWAIT:
			iowait_entry =
			    (struct io_trace_iowait_entry *)entry_head->val;
			len =
			    snprintf(buf, sizeof(buf),
				     "%lu %d %d %s %s %llu %d\n",
				     iowait_entry->time, iowait_entry->action,
				     iowait_entry->pid, iowait_entry->comm,
				     iowait_entry->wchan, iowait_entry->delay,
				     iowait_entry->ux_flag);
			ret = kernel_write(filp, buf, len, &file_pos);
			break;
		case TRACE_BLK:
			blk_entry =
			    (struct io_trace_blk_entry *)entry_head->val;
			if (blk_entry->action == BLOCK_GETRQ_TAG)
				len =
				    snprintf(buf, sizeof(buf),
					     "%lu %d %d %d %d %lu %d %u %u\n",
					     blk_entry->time, blk_entry->action,
					     blk_entry->dev_major,
					     blk_entry->dev_minor,
					     (unsigned int)(blk_entry->rw),
					     blk_entry->sector,
					     blk_entry->nr_bytes,
					     blk_entry->nr_rqs_sync,
					     blk_entry->nr_rqs_async);
			else if (blk_entry->action == BLOCK_RQ_ISSUE_TAG)
				len =
				    snprintf(buf, sizeof(buf),
					     "%lu %d %d %d %d %lu %d %u %u\n",
					     blk_entry->time, blk_entry->action,
					     blk_entry->dev_major,
					     blk_entry->dev_minor,
					     (unsigned int)(blk_entry->rw),
					     blk_entry->sector,
					     blk_entry->nr_bytes,
					     blk_entry->in_flight_sync,
					     blk_entry->in_flight_async);
			else
				len =
				    snprintf(buf, sizeof(buf),
					     "%lu %d %d %d %d %lu %d\n",
					     blk_entry->time, blk_entry->action,
					     blk_entry->dev_major,
					     blk_entry->dev_minor,
					     (unsigned int)(blk_entry->rw),
					     blk_entry->sector,
					     blk_entry->nr_bytes);
			ret = kernel_write(filp, buf, len, &file_pos);
			break;
		case TRACE_DEV:
			dev_entry =
			    (struct io_trace_dev_entry *)entry_head->val;
			len =
			    snprintf(buf, sizeof(buf), "%lu %d %d %lu %d\n",
				     dev_entry->time, dev_entry->action,
				     (unsigned int)(dev_entry->rw),
				     dev_entry->sector, dev_entry->result);
			ret = kernel_write(filp, buf, len, &file_pos);
			break;
		case TRACE_END:
			break;
		}

	}

 end:
	io_trace_this->enable = 1;
	mutex_unlock(&output_mutex);

	return ret;

}
EXPORT_SYMBOL(write_log_kernel);

static void io_syscall_read_timeout(void *ignore, struct file *file, u64 delay)
{
	if (delay < IO_TRACE_SYSCALL_TIMEOUT || file == NULL)
		return;

	if (io_trace_this->enable) {
		io_trace_global_log(SYS_READ_TIMEOUT_TAG, 0, 0, file, 0, NULL,
				    0, 0, -1000, NULL, delay);
	}
}

static void io_syscall_write_timeout(void *ignore, struct file *file, u64 delay)
{
	if (delay < IO_TRACE_SYSCALL_TIMEOUT || file == NULL)
		return;

	if (io_trace_this->enable) {
		io_trace_global_log(SYS_WRITE_TIMEOUT_TAG, 0, 0, file, 0, NULL,
				    0, 0, -1000, NULL, delay);
	}
}

static void io_syscall_sync_timeout(void *ignore, struct file *file, u64 delay)
{
	if (delay < IO_TRACE_SYSCALL_TIMEOUT || file == NULL)
		return;

	if (io_trace_this->enable) {
		io_trace_global_log(SYS_SYNC_TIMEOUT_TAG, 0, 0, file, 0, NULL,
				    0, 0, -1000, NULL, delay);
	}
}

static void blk_add_trace_rq_get(void *ignore, struct request_queue *q,
				 struct bio *bio, int rw)
{
	if (io_trace_this->enable && bio) {
		unsigned int rw_type = bio->bi_opf;
		unsigned int dev = bio_dev(bio);

		io_trace_global_log(BLOCK_GETRQ_TAG, bio->bi_iter.bi_sector,
				    bio->bi_iter.bi_size, 0, rw_type, q,
				    MAJOR(dev), MINOR(dev), -1000, NULL, 0);
	}

	return;
}

static void blk_add_trace_rq_insert(void *ignore, struct request_queue *q,
				    struct request *rq)
{
	if (io_trace_this->enable) {
		unsigned int rw_type = rq->cmd_flags;
		unsigned int dev = rq->rq_disk ? disk_devt(rq->rq_disk) : 0;

		io_trace_global_log(BLOCK_RQ_INSERT_TAG,
				    blk_rq_trace_sector(rq), blk_rq_bytes(rq),
				    0, rw_type, q, MAJOR(dev), MINOR(dev),
				    -1000, NULL, 0);
	}

	return;
}

static void blk_add_trace_rq_issue(void *ignore, struct request_queue *q,
				   struct request *rq)
{

	if (io_trace_this->enable) {
		unsigned int rw_type = rq->cmd_flags;
		unsigned int dev = rq->rq_disk ? disk_devt(rq->rq_disk) : 0;

		io_trace_global_log(BLOCK_RQ_ISSUE_TAG, blk_rq_trace_sector(rq),
				    blk_rq_bytes(rq), 0, rw_type, q, MAJOR(dev),
				    MINOR(dev), -1000, NULL, 0);
	}

	return;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
static void blk_add_trace_rq_complete(void *ignore, struct request *rq,
				      int error, unsigned int nr_bytes)
{
	if (io_trace_this->enable) {
		unsigned int rw_type = rq->cmd_flags;
		unsigned int dev = rq->rq_disk ? disk_devt(rq->rq_disk) : 0;

		io_trace_global_log(BLOCK_RQ_COMPLETE_TAG,
				    blk_rq_trace_sector(rq), blk_rq_bytes(rq),
				    0, rw_type, NULL, MAJOR(dev), MINOR(dev),
				    -1000, NULL, 0);
	}
	return;
}

#else
static void blk_add_trace_rq_complete(void *ignore, struct request_queue *q,
				      struct request *rq, unsigned int nr_bytes)
{
	if (io_trace_this->enable) {
		unsigned int rw_type = rq->cmd_flags;
		unsigned int dev = rq->rq_disk ? disk_devt(rq->rq_disk) : 0;

		io_trace_global_log(BLOCK_RQ_COMPLETE_TAG,
				    blk_rq_trace_sector(rq), blk_rq_bytes(rq),
				    0, rw_type, NULL, MAJOR(dev), MINOR(dev),
				    -1000, NULL, 0);

	}
	return;
}
#endif

static void blk_add_trace_rq_requeue(void *ignore, struct request_queue *q,
				     struct request *rq)
{

	if (io_trace_this->enable) {
		unsigned int rw_type = rq->cmd_flags;
		unsigned int dev = rq->rq_disk ? disk_devt(rq->rq_disk) : 0;

		io_trace_global_log(BLOCK_RQ_REQUEUE_TAG,
				    blk_rq_trace_sector(rq), blk_rq_bytes(rq),
				    0, rw_type, q, MAJOR(dev), MINOR(dev),
				    -1000, NULL, 0);
	}

	return;
}

static void blk_add_trace_sleeprq(void *ignore, struct request_queue *q,
				  struct bio *bio, int rw)
{

	if (io_trace_this->enable && bio) {
		unsigned int rw_type = bio->bi_opf;
		unsigned int dev = bio_dev(bio);

		io_trace_global_log(BLOCK_RQ_SLEEP_TAG, bio->bi_iter.bi_sector,
				    bio->bi_iter.bi_size, 0, rw_type, q,
				    MAJOR(dev), MINOR(dev), -1000, NULL, 0);
	}

	return;
}

static void scsi_dispatch_cmd_timeout(void *ignore, struct scsi_cmnd *cmd)
{
	unsigned int rw_flags = 0;
	unsigned char c[4];
	unsigned char len[2];
	unsigned int *addr;
	unsigned short *size;
	int result;
	struct scsi_cmnd *fsr_scsi = NULL;
	struct Scsi_Host *host = NULL;

	fsr_scsi = (struct scsi_cmnd *)cmd;

	if (fsr_scsi->device->host == NULL)
		return;

	host = fsr_scsi->device->host;
	fsr_hba = shost_priv(host);

	if (!fsr_hba) {
		io_trace_print("%s shost_priv host failed\n", __func__);
		return;
	}

	c[0] = cmd->cmnd[5];
	c[1] = cmd->cmnd[4];
	c[2] = cmd->cmnd[3];
	c[3] = cmd->cmnd[2];

	len[0] = cmd->cmnd[7];
	len[1] = cmd->cmnd[8];

	addr = (int *)c;
	size = (short *)len;
	result = cmd->result;

	if (0x2A == cmd->cmnd[0])
		rw_flags |= IO_TRACE_WRITE;
	else if (0x35 == cmd->cmnd[0])
		rw_flags |= (IO_TRACE_SYNC | IO_TRACE_WRITE);
	else if (0x28 == cmd->cmnd[0])
		rw_flags |= IO_TRACE_READ;
	if (io_trace_this->enable) {
		io_trace_global_log(SCSI_BLK_CMD_RW_TIMEOUT_TAG, (*addr) * 8,
				    (*size) * 16, 0, rw_flags, NULL, 0, 0,
				    result, NULL, 0);
	}
	return;
}

static void scsi_dispatch_cmd_error(void *ignore, struct scsi_cmnd *cmd,
				    int rtn)
{
	unsigned int rw_flags = 0;
	unsigned char c[4];
	unsigned char len[2];
	unsigned int *addr;
	unsigned short *size;
	int result;
	struct scsi_cmnd *fsr_scsi = NULL;
	struct Scsi_Host *host = NULL;

	fsr_scsi = (struct scsi_cmnd *)cmd;

	if (fsr_scsi->device->host == NULL)
		return;

	host = fsr_scsi->device->host;
	fsr_hba = shost_priv(host);

	if (!fsr_hba) {
		io_trace_print("%s shost_priv host failed\n", __func__);
		return;
	}

	c[0] = cmd->cmnd[5];
	c[1] = cmd->cmnd[4];
	c[2] = cmd->cmnd[3];
	c[3] = cmd->cmnd[2];

	len[0] = cmd->cmnd[7];
	len[1] = cmd->cmnd[8];

	addr = (int *)c;
	size = (short *)len;
	result = rtn;

	if (0x2A == cmd->cmnd[0])
		rw_flags |= IO_TRACE_WRITE;
	else if (0x35 == cmd->cmnd[0])
		rw_flags |= (IO_TRACE_SYNC | IO_TRACE_WRITE);
	else if (0x28 == cmd->cmnd[0])
		rw_flags |= IO_TRACE_READ;
	if (io_trace_this->enable) {
		io_trace_global_log(SCSI_BLK_CMD_RW_ERROR_TAG, (*addr) * 8,
				    (*size) * 16, 0, rw_flags, NULL, 0, 0,
				    result, NULL, 0);
	}
	return;
}

#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif
static void sched_stat_iowait(void *ignore, struct task_struct *tsk, u64 delay)
{
	if (tsk == NULL)
		return;

	if (io_trace_this->enable) {
#ifdef OPLUS_FEATURE_SCHED_ASSIST
		if (test_task_ux(tsk)
		    && io_ns_to_ms(delay) >= UX_IO_WAIT_TIMEOUT)
			io_trace_global_log(SCHED_STAT_IOWAIT_TAG, 0, 0, 0, 0,
					    NULL, 0, 0, 1, tsk, delay);
		else if (io_ns_to_ms(delay) >= IO_WAIT_TIMEOUT)
			io_trace_global_log(SCHED_STAT_IOWAIT_TAG, 0, 0, 0, 0,
					    NULL, 0, 0, 0, tsk, delay);
#else
		if (io_ns_to_ms(delay) >= IO_WAIT_TIMEOUT)
			io_trace_global_log(SCHED_STAT_IOWAIT_TAG, 0, 0, 0, 0,
					    NULL, 0, 0, 0, tsk, delay);
#endif
	}

	return;
}

static int iotrace_debug_seq_show(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "pos_err_cnt: %u\n"
			"before_loop_pos: %d\n"
			"before_loop_last: %u\n"
			"enter_loop_time: %lld\n"
			"in_loop_pos: %d\n"
			"in_loop_first: %u\n"
			"in_loop_last: %u\n"
			"err_time: %lld\n",
			otrace_dbg_info->pos_err_cnt,
			otrace_dbg_info->before_loop_pos,
			otrace_dbg_info->before_loop_last,
			otrace_dbg_info->enter_loop_time,
			otrace_dbg_info->in_loop_pos,
			otrace_dbg_info->in_loop_first,
			otrace_dbg_info->in_loop_last,
			otrace_dbg_info->err_time);
	return 0;
}

static int iotrace_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, iotrace_debug_seq_show, NULL);
}

static const struct file_operations proc_iotrace_debug_operations = {
	.open = iotrace_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

struct proc_dir_entry *create_iotrace_proc(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *child = NULL;
	child = proc_create("iotrace_debug", S_IRUGO | S_IWUGO, parent,
			&proc_iotrace_debug_operations);
	return child;
}

static void iotrace_register_tracepoints(void)
{
	int ret;

	/* syscall */
	ret =
	    register_trace_syscall_read_timeout(io_syscall_read_timeout, NULL);
	WARN_ON(ret);
	ret =
	    register_trace_syscall_write_timeout(io_syscall_write_timeout,
						 NULL);
	WARN_ON(ret);
	ret =
	    register_trace_syscall_sync_timeout(io_syscall_sync_timeout, NULL);
	WARN_ON(ret);

	/* block layer */
	ret = register_trace_block_getrq(blk_add_trace_rq_get, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_insert(blk_add_trace_rq_insert, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_issue(blk_add_trace_rq_issue, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_complete(blk_add_trace_rq_complete, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_requeue(blk_add_trace_rq_requeue, NULL);
	WARN_ON(ret);
	ret = register_trace_block_sleeprq(blk_add_trace_sleeprq, NULL);
	WARN_ON(ret);

	ret =
	    register_trace_scsi_dispatch_cmd_error(scsi_dispatch_cmd_error,
						   NULL);
	WARN_ON(ret);
	ret =
	    register_trace_scsi_dispatch_cmd_timeout(scsi_dispatch_cmd_timeout,
						     NULL);
	WARN_ON(ret);
	ret = register_trace_sched_stat_iowait(sched_stat_iowait, NULL);
	WARN_ON(ret);

	return;
}

static void io_trace_setup(struct io_trace_ctrl *trace_ctrl)
{
	io_trace_print("io trace setup!\n");
	iotrace_register_tracepoints();

	return;
}

static int trace_mm_init(struct io_trace_ctrl *trace_ctrl)
{
	io_trace_print("trace mm init start!\n");

	trace_ctrl->all_trace_buff = kvmalloc(IO_MAX_GLOBAL_ENTRY, GFP_KERNEL);
	if (trace_ctrl->all_trace_buff == NULL) {
		io_trace_print("mem mngt kvmalloc failed!\n");
		goto failed;
	}

	return 0;

 failed:
	return -1;
}

static int trace_ctrl_init(struct io_trace_ctrl *trace_ctrl,
			   unsigned int *total_mem)
{
	trace_ctrl->enable = 0;
	trace_ctrl->pdev = NULL;
	trace_ctrl->size = IO_MAX_GLOBAL_ENTRY;
	trace_ctrl->first = 0;
	trace_ctrl->last = 0;
	return 0;
}

static int __init io_trace_init(void)
{
	int ret = -1;
	int total_mem = 0;

	if (otrace_dbg_info == NULL)
		otrace_dbg_info = (struct iotrace_debug_info *)
					kzalloc(sizeof(struct iotrace_debug_info), GFP_KERNEL);
	if (otrace_dbg_info == NULL)
		return -1;

	io_trace_print("Enter %s\n", __func__);
	if (io_trace_this == NULL) {
		io_trace_this =
		    (struct io_trace_ctrl *)
		    kzalloc(sizeof(struct io_trace_ctrl), GFP_KERNEL);
		total_mem += sizeof(struct io_trace_ctrl);
		if (NULL == io_trace_this) {
			io_trace_print("(%s)kmalloc failed!", __func__);
			return -1;
		}
		io_trace_print("io_trace_this is init!\n");
		if (trace_ctrl_init(io_trace_this, &total_mem) < 0)
			return -1;

		ret = trace_mm_init(io_trace_this);
		if (ret < 0) {
			io_trace_print("trace mm init failed!\n");
			return ret;
		}
		io_trace_setup(io_trace_this);
		io_trace_this->enable = 1;
	}

	io_trace_print("total mem : %d\n", total_mem);
	io_trace_print("Enter %s:%d\n", __func__, __LINE__);

	io_trace_this->pdev =
	    platform_device_register_simple("io_trace", -1, NULL, 0);
	if (!io_trace_this->pdev) {
		io_trace_print
		    ("(%s)register platform_device_register_simple null!",
		     __func__);
		return -1;
	}

	ret = platform_driver_register(&io_trace_driver);
	if (ret < 0) {
		io_trace_print("(%s)register platform_driver_register failed!",
			       __func__);
		return ret;
	}

	return 0;
}

static void __exit io_trace_exit(void)
{
	if (io_trace_this->pdev)
		platform_device_unregister(io_trace_this->pdev);

	platform_driver_unregister(&io_trace_driver);

	if (otrace_dbg_info)
		kfree(otrace_dbg_info);
}

late_initcall(io_trace_init);
module_exit(io_trace_exit);
