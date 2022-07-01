/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _IOMONITOR_H_
#define _IOMONITOR_H_

/* Add for calculate free memory. */
#include <linux/vmstat.h>
#include <asm/page.h>

/* Add for calculate free disk with f2fs. */
#include <linux/f2fs_fs.h>
#include <linux/types.h>

/* Add for statistical IO time-consuming distribution. */
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/ktime.h>

/*
 * io monitor entry type
 * NUM_ENTRY_TYPE: int value
 * STR_ENTRY_TYPE: str value
 * STR_FREE_ENTRY_TYPE: str value, need free string mem
 */
#define NUM_ENTRY_TYPE 0
#define STR_ENTRY_TYPE 1
#define STR_FREE_ENTRY_TYPE 2
#define PRIVATE_ENTRY_TYPE 3
#define IO_WAIT_TIMEOUT 500
#define UX_IO_WAIT_TIMEOUT 300
#define IO_WAIT_LOW 200
#define IO_WAIT_HIGH 500
enum {
	IO_MONITOR_DELAY_SYNC = 0,
	IO_MONITOR_DELAY_NSYNC,
	IO_MONITOR_DELAY_BLOCK_NUM,
};

struct block_delay_data {
	/* sync = 0, nsync = 1 */
	u64 stage_one[2];
	u64 stage_two[2];
	u64 stage_thr[2];
	u64 stage_fou[2];
	u64 stage_fiv[2];
	u64 stage_six[2];
	u64 stage_sev[2];
	u64 stage_eig[2];
	u64 stage_nin[2];
	u64 stage_ten[2];
	u64 max_delay[2];
	u64 cnt[2];
	u64 total_delay[2];
};

enum {
	IO_MONITOR_DELAY_READ = 0,
	IO_MONITOR_DELAY_WRITE,
	IO_MONITOR_DELAY_OPTION_NUM,
};

enum {
	IO_MONITOR_DELAY_4K = 0,
	IO_MONITOR_DELAY_512K,
	IO_MONITOR_DELAY_DEVICE_NUM,
};

struct device_delay_data {
	/* read = 0, write = 1 */
	u64 stage_one[2];
	u64 stage_two[2];
	u64 stage_thr[2];
	u64 stage_fou[2];
	u64 stage_fiv[2];
	u64 max_delay[2];
	u64 cnt[2];
	u64 total_delay[2];
};

struct block_delay_data_ux {
	u64 stage_one;
	u64 stage_two;
	u64 stage_thr;
	u64 stage_fou;
	u64 stage_fiv;
	u64 stage_six;
	u64 stage_sev;
	u64 stage_eig;
	u64 stage_nin;
	u64 stage_ten;
	u64 max_delay;
	u64 cnt;
	u64 total_delay;
};

struct req_delay_data {
	struct block_delay_data_ux uxreq_block_para;
	struct block_delay_data req_block_para;
	/* 4K = 0, 512K = 1 */
	struct device_delay_data req_device_para[2];
};

struct cal_data_info {
	u64 user_read_data;
	u64 user_write_data;
	u64 kernel_read_data;
	u64 kernel_write_data;
	u64 dio_write_data;
};

struct cal_page_info {
	u64 page_in;
	u64 page_out;
};

struct fs_status_info {
	atomic64_t major_fpage;
	atomic64_t nfsync;
	u64 discard;
	u64 gc;
	u64 task_rw_data;
	u64 low_iowait;
	u64 high_iowait;
	atomic64_t dirty_page;
};

struct iowait_info {
	u64 iowait_total_ms;
	u64 iowait_total_count;
	u64 iowait_1ms;
	u64 iowait_10ms;
	u64 iowait_20ms;
	u64 iowait_60ms;
	u64 iowait_100ms;
	u64 iowait_200ms;
	u64 iowait_300ms;
	u64 iowait_400ms;
	u64 iowait_500ms;
	u64 iowait_600ms;
	u64 iowait_800ms;
	u64 iowait_1000ms;
	u64 iowait_1500ms;
	u64 iowait_2000ms;
	u64 iowait_above2000ms;
};

struct abnormal_info {
	int reason;
	pid_t pid;
	int delay;
};

enum iomointor_io_type {
	IO_WAIT,
	UX_IO_WAIT,
	FRAME_DROP,
	USER_TRIGGER
};

enum daily_data_type {
	USER_READ,
	USER_WRITE,
	KERNEL_READ,
	KERNEL_WRITE,
	DIO_WRITE
};

enum rt_record_type {
	RECORD_INIT,
	RECORD_STOP,
	RECORD_START,
	RECORD_SIZE
};

/* monitor fs event like gc, discard, fsync*/
enum fs_event_type {
	FS_GC_OPT,
	FS_DISCARD_OPT,
	FS_FSYNC,
	FS_MAJOR_FAULT,
	FS_DIRTY_PAGES,
};

extern void iomonitor_update_rw_stats(enum daily_data_type type,
				      struct file *file, ssize_t ret);
extern void iomonitor_update_vm_stats(enum vm_event_item type, u64 delta);
extern void iomonitor_update_fs_stats(enum fs_event_type type, long delta);
extern void iomonitor_record_iowait(struct task_struct *tsk, u64 delta_ms);
#define PID_LENGTH 32
#define PID_HASH_LEGNTH 199
#define TASK_STATUS_LENGTH 4096
#define UID_SHOW_DAILY_LIMIT  (300*1024*1024)	/* 300M */
#define TASK_SHOW_DAILY_LIMIT (100*1024*1024)	/* 100M */
struct task_io_info {
	pid_t pid;
	pid_t tgid;
	uid_t uid;
	u64 read_bytes;
	u64 write_bytes;
	bool used;
	char comm[TASK_COMM_LEN];
	unsigned long time;
};

struct disk_info {
	unsigned int score;
	unsigned int free;	/* blocks */
	/*
	 * 4..8K |8..16K | 16..32K |32..64K | 64..128K |
	 * 128..256K | 256..512K | 512K..1M | 1M+
	 */
	unsigned blocks[9];
};

struct inter_disk_data {
	unsigned int len;
	char *buf;
};

#define IO_HISTORY_DEPTH 32
struct io_history {
	unsigned int cmd_flags;
	unsigned long jiffies;
};

extern void iomonitor_record_io_history(const struct request *req);
extern int exit_uid_find_or_register(struct task_struct *task);
extern void free_all_uid_entry(void);

int abnormal_handle(enum iomointor_io_type reason, pid_t pid, u64 delta_ms);

struct proc_dir_entry *create_uid_proc(struct proc_dir_entry *parent);
struct proc_dir_entry *create_iotrace_proc(struct proc_dir_entry *parent);

/* extern void iomonitor_get_disk_info(struct super_block *sb, void *arg); */

void add_pid_to_list(struct task_struct *task, size_t bytes, bool opt);
static inline void iomonitor_record_task_io(struct task_struct *task,
					    size_t bytes, bool rw)
{
	add_pid_to_list(task, bytes, rw);
}

static inline void iomonitor_init_reqstats(struct request *rq)
{
	rq->req_tg = ktime_get();
	rq->req_ti = ktime_set(0, 0);
	rq->req_td = ktime_set(0, 0);
	rq->req_tc = ktime_set(0, 0);
}

void reqstats_record(struct request *req, unsigned nr_bytes);
static inline void iomonitor_record_reqstats(struct request *req,
					     unsigned int nr_bytes)
{
	req->req_tc = ktime_get();
	if (nr_bytes && req->req_tg && req->req_td && req->req_tc)
		reqstats_record(req, nr_bytes);
}

#endif /* _IOMONITOR_H_ */
