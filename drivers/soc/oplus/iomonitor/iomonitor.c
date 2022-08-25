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
#include <linux/swap.h>
#include <linux/magic.h>
#include "../../../../fs/mount.h"
#ifdef CONFIG_IOMONITOR_WITH_F2FS
#include "../../../../fs/of2fs/f2fs.h"
#endif

#define TMP_BUF_LEN 64
#define ABNORMAL_LOG_NAME "/data/debugging/io_abnormal.log"
#define ENTRY_END_TAG  "-------STR end-----\n"

#define DATE_STR_LEN 128
#define IOM_REASON_LENGTH 32

#ifdef CONFIG_IOMONITOR_WITH_F2FS
extern block_t of2fs_seg_freefrag(struct f2fs_sb_info *sbi,
				  unsigned int segno, block_t *blocks,
				  unsigned int n);
#endif

extern unsigned dump_log(char *buf, unsigned len);

static LIST_HEAD(daily_list);
static LIST_HEAD(abnormal_list);
static LIST_HEAD(flow_list);

#define MAX_IOMEVENT_PARAM 5
#define MAX_RTEVENT_PARAM 2
static struct kobject *iomonitor_kobj;
static char *io_monitor_env[MAX_IOMEVENT_PARAM] = { NULL };
static char *rt_record_env[MAX_RTEVENT_PARAM] = { NULL };
char iom_reason[IOM_REASON_LENGTH];
char iom_pid[IOM_REASON_LENGTH];
char iom_delay[IOM_REASON_LENGTH];
char rt_reason[IOM_REASON_LENGTH];

unsigned int io_abl_interval_s = 10 * 60 * 1000; /* secs */
unsigned int rt_record_interval_s = 30 * 1000; /* secs */
unsigned int log_file_size = 10 * 1024 * 1024; /* LOG_FILE_SIZE */

static unsigned long last_abnormal_jiffies;
static unsigned long last_rt_record_jiffies;
static atomic_t abnormal_handle_ref;
static int rt_record_status;
struct proc_dir_entry *IoMonitor_dir;
struct fs_status_info fs_status;
struct iowait_info iowait_status;
struct abnormal_info io_abnormal_info = { 0, 0, 0 };

static void io_abnormal(struct work_struct *work);
static void realtime_record(struct work_struct *work);
void rt_record_handle(void);
static DECLARE_WAIT_QUEUE_HEAD(io_abnormal_wait);
static struct workqueue_struct *io_abnormal_queue;
static struct workqueue_struct *rt_record_queue;
static DECLARE_DELAYED_WORK(io_abnormal_work, io_abnormal);
static DECLARE_DELAYED_WORK(rt_record_work, realtime_record);

static DEFINE_PER_CPU(struct cal_data_info, rw_data_info) = {
0, 0, 0, 0, 0};

static DEFINE_PER_CPU(struct cal_page_info, pgpg_info) = {
0, 0};

#ifdef CONFIG_IOMONITOR_WITH_F2FS
static struct disk_info disk_status;
#endif
static int task_status_init(void);

static struct task_io_info taskio[PID_HASH_LEGNTH];
char *task_buffer;
static int block_delay_range[][10] = {
	/* unit of measurement is 'ms' */
	[IO_MONITOR_DELAY_SYNC] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256},
	[IO_MONITOR_DELAY_NSYNC] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256},
};

static int device_delay_range[][5] = {
	/* unit of measurement is 'ms' */
	[IO_MONITOR_DELAY_4K] = {0, 1, 10, 50, 100},
	[IO_MONITOR_DELAY_512K] = {1, 10, 50, 100, 500},
};

struct req_delay_data req_para;

/*
 * descripte one item
 * @name: string for this entry
 * @type: see above
 * @get_value: call back used to get this entry's value
 * @list: list to abnormal_list or daily_list
 *
 * get_value in case of type == NUM_ENTRY_TYPE
 * return  entry's value;  -1 means failed,
 * @data is ignored
 *
 * get_value in case of type == STR_ENTRY_TYPE
 * return value represent length of @data ; -1 means get_value failed
 * @data point to entry buf
 *
 * get_value in case of type == STR_FREE_ENTRY_TYPE
 * return value represent length of @data ; -1  means get_value failed
 * @data point to entry buf
 * need free @data after exec get_value
 */
struct io_info_entry {
	char *name;
	int type;
	int (*get_value) (char **data);
	struct list_head list;
};

static int abnormal_interval_seq_show(struct seq_file *seq, void *p)
{
	seq_printf(seq, "%d\n", (io_abl_interval_s / 1000));
	return 0;
}

static ssize_t abnormal_interval_write(struct file *filp,
				       const char __user *ubuf, size_t cnt,
				       loff_t *ppos)
{
	char buf[8] = { 0 };
	ssize_t buf_size;
	unsigned int v;
	int ret;

	if (!cnt)
		return 0;

	buf_size = min(cnt, (size_t) (sizeof(buf) - 1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;
	buf[buf_size - 1] = '\0';

	ret = kstrtouint(buf, 10, &v);
	if (ret)
		return ret;

	io_abl_interval_s = v * 1000;

	printk("my debug %s %d:int buf=%d\n", __func__, __LINE__,
			io_abl_interval_s);

	return cnt;
}

static int abnormal_interval_open(struct inode *inode, struct file *file)
{
	return single_open(file, abnormal_interval_seq_show, PDE_DATA(inode));
}

static const struct file_operations seq_abnormal_interval_fops = {
	.open = abnormal_interval_open,
	.read = seq_read,
	.write = abnormal_interval_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int abnormal_size_seq_show(struct seq_file *seq, void *p)
{
	seq_printf(seq, "%d\n", (log_file_size/(1024*1024)));
	return 0;
}

static ssize_t abnormal_size_write(struct file *filp, const char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char buf[8] = { 0 };
	ssize_t buf_size;
	unsigned int v;
	int ret;

	if (!cnt)
		return 0;

	buf_size = min(cnt, (size_t)(sizeof(buf)-1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;
	buf[buf_size-1] = '\0';

	ret = kstrtouint(buf, 10, &v);
	if (ret)
		return ret;

	log_file_size = v*1024*1024;

	printk("my debug %s %d:size buf=%d\n", __func__, __LINE__,
			log_file_size);

	return cnt;
}

static int abnormal_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, abnormal_size_seq_show, PDE_DATA(inode));
}

static const struct file_operations seq_abnormal_size_fops = {
	.open = abnormal_size_open,
	.read = seq_read,
	.write = abnormal_size_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static inline bool is_data_fs(struct file *file)
{
	struct super_block *sb;

	sb = file->f_inode->i_sb;
	if (!sb)
		return false;
	if ((!strncmp(sb->s_type->name, "f2fs", 4))
	    || (!strncmp(sb->s_type->name, "ext4", 4)))
		return true;
	return false;
}

static int rt_record_count;
static unsigned long rt_record_last_time;
#define RT_RECORD_COUNT 3
bool iomonitor_rt_need_record(void)
{
	/*some iowait trigger during 1 sec,than record count add 1*/
	if (time_before(jiffies, rt_record_last_time + msecs_to_jiffies(1000)))
		rt_record_count++;
	else
		rt_record_count = 0;
	rt_record_last_time = jiffies;
	if (rt_record_count == RT_RECORD_COUNT)
		return true;
	return false;
}

void iomonitor_iowait_segment_statistics(u64 delta_ms)
{
	iowait_status.iowait_total_ms += delta_ms;
	iowait_status.iowait_total_count++;
	if (delta_ms <= 1)
		iowait_status.iowait_1ms++;
	else if (delta_ms <= 10)
		iowait_status.iowait_10ms++;
	else if (delta_ms <= 20)
		iowait_status.iowait_20ms++;
	else if (delta_ms <= 60)
		iowait_status.iowait_60ms++;
	else if (delta_ms <= 100)
		iowait_status.iowait_100ms++;
	else if (delta_ms <= 200)
		iowait_status.iowait_200ms++;
	else if (delta_ms <= 300)
		iowait_status.iowait_300ms++;
	else if (delta_ms <= 400)
		iowait_status.iowait_400ms++;
	else if (delta_ms <= 500)
		iowait_status.iowait_500ms++;
	else if (delta_ms <= 600)
		iowait_status.iowait_600ms++;
	else if (delta_ms <= 800)
		iowait_status.iowait_800ms++;
	else if (delta_ms <= 1000)
		iowait_status.iowait_1000ms++;
	else if (delta_ms <= 1500)
		iowait_status.iowait_1500ms++;
	else if (delta_ms <= 2000)
		iowait_status.iowait_2000ms++;
	else
		iowait_status.iowait_above2000ms++;
}

void iomonitor_update_rw_stats(enum daily_data_type type, struct file *file,
			       ssize_t ret)
{
	switch (type) {
	case USER_READ:
		if (is_data_fs(file))
			this_cpu_add(rw_data_info.user_read_data, ret);
		break;
	case USER_WRITE:
		if (is_data_fs(file))
			this_cpu_add(rw_data_info.user_write_data, ret);
		break;
	case KERNEL_READ:
		if (is_data_fs(file))
			this_cpu_add(rw_data_info.kernel_read_data, ret);
		break;
	case KERNEL_WRITE:
		if (is_data_fs(file))
			this_cpu_add(rw_data_info.kernel_write_data, ret);
		break;
	case DIO_WRITE:
		this_cpu_add(rw_data_info.dio_write_data, ret);
		break;
	}
}
EXPORT_SYMBOL(iomonitor_update_rw_stats);

void iomonitor_update_vm_stats(enum vm_event_item type, u64 delta)
{
	switch (type) {
	case PGPGIN:
		this_cpu_add(pgpg_info.page_in, delta);
		break;
	case PGPGOUT:
		this_cpu_add(pgpg_info.page_out, delta);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(iomonitor_update_vm_stats);

void iomonitor_update_fs_stats(enum fs_event_type type, long delta)
{
	switch (type) {
	case FS_GC_OPT:
		fs_status.gc += delta;
		break;
	case FS_DISCARD_OPT:
		fs_status.discard += delta;
		break;
	case FS_FSYNC:
		atomic64_add(delta, &fs_status.nfsync);
		break;
	case FS_MAJOR_FAULT:
		atomic64_add(delta, &fs_status.major_fpage);
		break;
	case FS_DIRTY_PAGES:
		atomic64_add(delta, &fs_status.dirty_page);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(iomonitor_update_fs_stats);

#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif
void iomonitor_record_iowait(struct task_struct *tsk, u64 delta_ms)
{
	if (delta_ms > IO_WAIT_HIGH)
		fs_status.high_iowait++;
	else if (delta_ms > IO_WAIT_LOW) {
		fs_status.low_iowait++;
		if (iomonitor_rt_need_record())
			rt_record_handle();
	}
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	/* first decide ux iowait */
	if (test_task_ux(tsk) && delta_ms > UX_IO_WAIT_TIMEOUT)
		abnormal_handle(UX_IO_WAIT, tsk->pid, delta_ms);
	else if (delta_ms > IO_WAIT_TIMEOUT)
		abnormal_handle(IO_WAIT, tsk->pid, delta_ms);
#else
	if (delta_ms > IO_WAIT_TIMEOUT)
		abnormal_handle(IO_WAIT, tsk->pid, delta_ms);
#endif

	iomonitor_iowait_segment_statistics(delta_ms);
}

static void get_rw_bytes(u64 *uread, u64 *uwrite, u64 *kread, u64 *kwrite,
			 u64 *dwrite)
{
	int i;

	for_each_possible_cpu(i) {
		struct cal_data_info *s_data_info =
		    per_cpu_ptr(&rw_data_info, i);
		*uread += s_data_info->user_read_data;
		*uwrite += s_data_info->user_write_data;
		*kread += s_data_info->kernel_read_data;
		*kwrite += s_data_info->kernel_write_data;
		*dwrite += s_data_info->dio_write_data;

		s_data_info->user_read_data = 0;
		s_data_info->user_write_data = 0;
		s_data_info->kernel_read_data = 0;
		s_data_info->kernel_write_data = 0;
		s_data_info->dio_write_data = 0;
	}
}

static int fs_status_get_value(char **data)
{
	char *tmp = kzalloc(128, GFP_KERNEL);
	ssize_t len;
	u64 major_fpage = atomic64_read(&fs_status.major_fpage);
	u64 nfsync = atomic64_read(&fs_status.nfsync);
	u64 dirty_page = atomic64_read(&fs_status.dirty_page);

	len = snprintf(tmp, 128, "%llu %llu %llu %llu %llu %llu %llu %llu\n",
		       major_fpage, nfsync, fs_status.discard, fs_status.gc,
		       fs_status.task_rw_data, fs_status.high_iowait,
		       fs_status.low_iowait, dirty_page);
	*data = tmp;
	return len;
}

struct io_info_entry fs_status_entry = {
	.name = "fs_status",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = fs_status_get_value,
	.list = LIST_HEAD_INIT(fs_status_entry.list),
};

extern int write_log_kernel(char **data);
struct io_info_entry io_trace_entry = {
	.name = "io_trace",
	.type = PRIVATE_ENTRY_TYPE,
	.get_value = write_log_kernel,
};

static int pgpg_status_get_value(char **data)
{
	char *tmp = kzalloc(128, GFP_KERNEL);
	ssize_t len;
	int i;
	unsigned long pgpgin = 0, pgpgout = 0;
	u64 uread = 0, uwrite = 0, kread = 0, kwrite = 0, dwrite = 0;

	for_each_possible_cpu(i) {
		struct cal_page_info *s_page_info = per_cpu_ptr(&pgpg_info, i);
		pgpgin += s_page_info->page_in;
		pgpgout += s_page_info->page_out;
		s_page_info->page_in = 0;
		s_page_info->page_out = 0;
	}

	pgpgin /= 2;
	pgpgout /= 2;
	get_rw_bytes(&uread, &uwrite, &kread, &kwrite, &dwrite);
	len =
	    snprintf(tmp, 128, "%llu %llu %llu %llu %llu %lu %lu\n", uread,
		     uwrite, kread, kwrite, dwrite, pgpgin, pgpgout);
	*data = tmp;
	return len;
}

struct io_info_entry pgpg_status_entry = {
	.name = "pgpg_status",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = pgpg_status_get_value,
	.list = LIST_HEAD_INIT(pgpg_status_entry.list),
};

static int get_free_mem(char **data)
{
	char *tmp = kzalloc(64, GFP_KERNEL);
	ssize_t len = -1;
	unsigned long freeram, bufferram;
	long cached, availableram;
	/* Free memory = xxx MB. */
	freeram = global_zone_page_state(NR_FREE_PAGES);
	bufferram = nr_blockdev_pages();
	cached = global_node_page_state(NR_FILE_PAGES) -
				total_swapcache_pages() - bufferram;
	availableram = si_mem_available();
	len = snprintf(tmp, 64, "%lu %lu %ld %ld\n",
				freeram, bufferram, cached, availableram);
	*data = tmp;
	return len;
}


struct io_info_entry free_mem_entry = {
	.name = "free_mem",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_free_mem
};

#ifdef CONFIG_IOMONITOR_WITH_F2FS
static int f2fs_get_free_disk(struct f2fs_sb_info *sbi)
{
	u64 free_size = 0;

	if (!sbi->ckpt || !sbi->sm_info)
		return 0;
	if (!sbi->sm_info->dcc_info)
		return 0;

	free_size = (sbi->user_block_count - sbi->total_valid_block_count);
	/* Free disk = xxx GB. */
	free_size >>= (20 - (int)sbi->log_blocksize);
	return ((int)free_size < 0 ? 0 : (int)free_size);
}

static void __dump_disk_info(struct inter_disk_data *disk_data)
{
	int ret = 0;
	char *disk_buf;
	int i;
	char *p;

	disk_data->len = -1;

	disk_buf = kzalloc(512, GFP_KERNEL);
	if (!disk_buf)
		return;

	p = disk_buf;
	ret = sprintf(p, "%d ", disk_status.score);
	p = p + ret;
	disk_data->len += ret;

	ret = sprintf(p, "%d ", disk_status.free);
	p = p + ret;
	disk_data->len += ret;

	for (i = 0; i < ARRAY_SIZE(disk_status.blocks); i++) {
		if (!disk_status.blocks[i])
			continue;
		if (i == ARRAY_SIZE(disk_status.blocks) - 1) {
			ret = sprintf(p, "%d\n", disk_status.blocks[i]);
			p = p + ret;
			disk_data->len += ret;
		} else {
			ret = sprintf(p, "%d ", disk_status.blocks[i]);
			p = p + ret;
			disk_data->len += ret;
		}
	}

	disk_data->buf = disk_buf;

	return;
}

static void __get_disk_info(struct super_block *sb, void *arg)
{
	unsigned int i;
	bool valid_patition = false;
	struct mount *mnt;
	struct f2fs_sb_info *sbi;
	unsigned int total_segs;
	block_t total_blocks = 0;
	struct inter_disk_data *disk_data = (struct inter_disk_data *)arg;

	if (disk_data->len != -1)
		return;

	lock_mount_hash();
	list_for_each_entry(mnt, &sb->s_mounts, mnt_instance) {
		if (strstr(mnt->mnt_devname, "userdata")
		    && strstr(mnt->mnt_mp->m_dentry->d_name.name, "data")) {
			if (sb->s_magic == F2FS_SUPER_MAGIC) {
				valid_patition = true;
				break;
			}
		}
	}
	unlock_mount_hash();

	if (valid_patition) {
		sbi = F2FS_SB(sb);
		total_segs = le32_to_cpu(sbi->raw_super->segment_count_main);
		memset(&disk_status, 0, sizeof(struct disk_info));
		for (i = 0; i < total_segs; i++) {
			total_blocks += of2fs_seg_freefrag(sbi, i,
					disk_status.blocks,
					ARRAY_SIZE(disk_status.blocks));

			cond_resched();
		}
		disk_status.score =
		    total_blocks ? (disk_status.blocks[0] +
			disk_status.blocks[1]) * 100ULL / total_blocks : 0;
		disk_status.free = f2fs_get_free_disk(sbi);

		__dump_disk_info(disk_data);

	}

	return;
}

static int get_disk_info(char **data)
{
	struct inter_disk_data disk_data;

	disk_data.len = -1;
	disk_data.buf = NULL;

	iterate_supers(__get_disk_info, &disk_data);

	*data = disk_data.buf;
	return disk_data.len;
}

struct io_info_entry disk_info_entry = {
	.name = "disk",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_disk_info,
};
#endif

static int get_iowait_info(char **data)
{
	ssize_t len = -1;
	char *tmp = kzalloc(512, GFP_KERNEL);

	if (!tmp)
		return -1;

	len = snprintf(tmp, 512,
		"%lld %lld %lld %lld %lld %lld %lld %lld "
		"%lld %lld %lld %lld %lld %lld %lld %lld %lld\n",
		iowait_status.iowait_total_ms,
		iowait_status.iowait_total_count,
		iowait_status.iowait_1ms,
		iowait_status.iowait_10ms,
		iowait_status.iowait_20ms,
		iowait_status.iowait_60ms,
		iowait_status.iowait_100ms,
		iowait_status.iowait_200ms,
		iowait_status.iowait_300ms,
		iowait_status.iowait_400ms,
		iowait_status.iowait_500ms,
		iowait_status.iowait_600ms,
		iowait_status.iowait_800ms,
		iowait_status.iowait_1000ms,
		iowait_status.iowait_1500ms,
		iowait_status.iowait_2000ms,
		iowait_status.iowait_above2000ms
		);

	*data = tmp;
	return len;

}

struct io_info_entry iowait_info_entry = {
	 .name = "iowait",
	 .type = STR_FREE_ENTRY_TYPE,
	 .get_value = get_iowait_info,
};

static struct io_history io_queue_history[IO_HISTORY_DEPTH];
static unsigned char io_queue_index;
void iomonitor_record_io_history(const struct request *rq)
{
	if (rq && rq->rq_disk && !strcmp(rq->rq_disk->disk_name, "sda")) {
		io_queue_history[io_queue_index].cmd_flags = rq->cmd_flags;
		io_queue_history[io_queue_index].jiffies = jiffies;

		io_queue_index++;
		if (io_queue_index == 32)
			io_queue_index = 0;
	}
}

#ifdef OPLUS_FEATURE_SCHED_ASSIST
static inline bool rq_is_ux(struct request *rq)
{
	return (rq->cmd_flags & REQ_UX);
}
#endif

void reqstats_init(void)
{
	memset(&req_para, 0, sizeof(struct req_delay_data));
	return;
}

void reqstats_record(struct request *req, unsigned int nr_bytes)
{
	u64 tg2d = 0, td2c = 0;
	int index, rw, sync;
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	int isux;
#endif
	if (!req)
		return;

	/* BLOCK */
	tg2d = ktime_us_delta(req->req_td, req->req_tg) / 1000;
	sync = rq_is_sync(req) ? 0 : 1;
	index = sync;
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	isux = rq_is_ux(req);
	/* UX req */
	if (!sync && isux) {
		req_para.uxreq_block_para.cnt++;
		req_para.uxreq_block_para.total_delay += tg2d;
		if (tg2d >= block_delay_range[index][9])
			req_para.uxreq_block_para.stage_ten++;
		else if (tg2d >= block_delay_range[index][8])
			req_para.uxreq_block_para.stage_nin++;
		else if (tg2d >= block_delay_range[index][7])
			req_para.uxreq_block_para.stage_eig++;
		else if (tg2d >= block_delay_range[index][6])
			req_para.uxreq_block_para.stage_sev++;
		else if (tg2d >= block_delay_range[index][5])
			req_para.uxreq_block_para.stage_six++;
		else if (tg2d >= block_delay_range[index][4])
			req_para.uxreq_block_para.stage_fiv++;
		else if (tg2d >= block_delay_range[index][3])
			req_para.uxreq_block_para.stage_fou++;
		else if (tg2d >= block_delay_range[index][2])
			req_para.uxreq_block_para.stage_thr++;
		else if (tg2d >= block_delay_range[index][1])
			req_para.uxreq_block_para.stage_two++;
		else if (tg2d >= block_delay_range[index][0])
			req_para.uxreq_block_para.stage_one++;
		req_para.uxreq_block_para.max_delay =
		    (req_para.uxreq_block_para.max_delay >=
		     tg2d) ? req_para.uxreq_block_para.max_delay : tg2d;
	}
#endif
	/* ALL req */
	req_para.req_block_para.cnt[sync]++;
	req_para.req_block_para.total_delay[sync] += tg2d;
	if (tg2d >= block_delay_range[index][9])
		req_para.req_block_para.stage_ten[sync]++;
	else if (tg2d >= block_delay_range[index][8])
		req_para.req_block_para.stage_nin[sync]++;
	else if (tg2d >= block_delay_range[index][7])
		req_para.req_block_para.stage_eig[sync]++;
	else if (tg2d >= block_delay_range[index][6])
		req_para.req_block_para.stage_sev[sync]++;
	else if (tg2d >= block_delay_range[index][5])
		req_para.req_block_para.stage_six[sync]++;
	else if (tg2d >= block_delay_range[index][4])
		req_para.req_block_para.stage_fiv[sync]++;
	else if (tg2d >= block_delay_range[index][3])
		req_para.req_block_para.stage_fou[sync]++;
	else if (tg2d >= block_delay_range[index][2])
		req_para.req_block_para.stage_thr[sync]++;
	else if (tg2d >= block_delay_range[index][1])
		req_para.req_block_para.stage_two[sync]++;
	else if (tg2d >= block_delay_range[index][0])
		req_para.req_block_para.stage_one[sync]++;
	req_para.req_block_para.max_delay[sync] =
	    (req_para.req_block_para.max_delay[sync] >=
	     tg2d) ? req_para.req_block_para.max_delay[sync] : tg2d;

	/* DEVICE */
	td2c = ktime_us_delta(req->req_tc, req->req_td) / 1000;
	if (nr_bytes == 4 * 1024)
		index = IO_MONITOR_DELAY_4K;
	else if (nr_bytes == 512 * 1024)
		index = IO_MONITOR_DELAY_512K;
	else {
		index = IO_MONITOR_DELAY_DEVICE_NUM;
		return;
	}
	/* size = 512K && time < 1ms, do not record */
	if (td2c < 1 && index == IO_MONITOR_DELAY_512K)
		return;
	rw = rq_data_dir(req);
	req_para.req_device_para[index].cnt[rw]++;
	req_para.req_device_para[index].total_delay[rw] += td2c;
	if (td2c >= device_delay_range[index][4])
		req_para.req_device_para[index].stage_fiv[rw]++;
	else if (td2c >= device_delay_range[index][3])
		req_para.req_device_para[index].stage_fou[rw]++;
	else if (td2c >= device_delay_range[index][2])
		req_para.req_device_para[index].stage_thr[rw]++;
	else if (td2c >= device_delay_range[index][1])
		req_para.req_device_para[index].stage_two[rw]++;
	else if (td2c >= device_delay_range[index][0])
		req_para.req_device_para[index].stage_one[rw]++;
	req_para.req_device_para[index].max_delay[rw] =
	    (req_para.req_device_para[index].max_delay[rw] >=
	     td2c) ? req_para.req_device_para[index].max_delay[rw] : td2c;

	return;
}

static int get_blk_ux_io_distribution(char **data)
{
	int ret = 0;
	char *uxio_buff;

	uxio_buff = kzalloc(1024, GFP_KERNEL);
	if (!uxio_buff)
		return ret;
	ret = snprintf(uxio_buff, 1024,
		      "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld\n",
		      req_para.uxreq_block_para.cnt,
		      req_para.uxreq_block_para.total_delay,
		      req_para.uxreq_block_para.max_delay,
		      req_para.uxreq_block_para.stage_one,
		      req_para.uxreq_block_para.stage_two,
		      req_para.uxreq_block_para.stage_thr,
		      req_para.uxreq_block_para.stage_fou,
		      req_para.uxreq_block_para.stage_fiv,
		      req_para.uxreq_block_para.stage_six,
		      req_para.uxreq_block_para.stage_sev,
		      req_para.uxreq_block_para.stage_eig,
		      req_para.uxreq_block_para.stage_nin,
		      req_para.uxreq_block_para.stage_ten);

	*data = uxio_buff;
	return ret;
}

struct io_info_entry block_ux_io_entry = {
	.name = "block_ux_io",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_blk_ux_io_distribution
};

static int get_blk_sync_io_distribution(char **data)
{
	int ret = 0;
	char *syncio_buff;

	syncio_buff = kzalloc(1024, GFP_KERNEL);
	if (!syncio_buff)
		return ret;
	ret = snprintf(syncio_buff, 1024,
		      "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld\n",
		      req_para.req_block_para.cnt[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.total_delay[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.max_delay[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_one[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_two[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_thr[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_fou[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_fiv[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_six[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_sev[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_eig[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_nin[IO_MONITOR_DELAY_SYNC],
		      req_para.req_block_para.stage_ten[IO_MONITOR_DELAY_SYNC]);

	*data = syncio_buff;
	return ret;
}

struct io_info_entry block_sync_io_entry = {
	.name = "block_sync_io",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_blk_sync_io_distribution
};

static int get_blk_nsync_io_distribution(char **data)
{
	int ret = 0;
	char *nsyncio_buff;

	nsyncio_buff = kzalloc(1024, GFP_KERNEL);
	if (!nsyncio_buff)
		return ret;
	ret = snprintf(nsyncio_buff, 1024,
		      "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld\n",
		      req_para.req_block_para.cnt[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.total_delay[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.max_delay[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_one[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_two[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_thr[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_fou[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_fiv[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_six[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_sev[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_eig[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_nin[IO_MONITOR_DELAY_NSYNC],
		      req_para.req_block_para.stage_ten[IO_MONITOR_DELAY_NSYNC]);

	*data = nsyncio_buff;
	return ret;
}

struct io_info_entry block_nsync_io_entry = {
	.name = "block_nsync_io",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_blk_nsync_io_distribution
};

static int get_dev_4k_rw_distribution(char **data)
{
	int ret = 0;
	char *rw4k_buff;

	rw4k_buff = kzalloc(1024, GFP_KERNEL);
	if (!rw4k_buff)
		return ret;
	ret = snprintf(rw4k_buff, 1024,
		"%lld %lld %lld %lld %lld %lld %lld %lld "
		"%lld %lld %lld %lld %lld %lld %lld %lld\n",
		req_para.req_device_para[IO_MONITOR_DELAY_4K].cnt[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].total_delay[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].max_delay[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_one[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_two[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_thr[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_fou[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_fiv[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].cnt[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].total_delay[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].max_delay[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_one[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_two[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_thr[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_fou[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_4K].stage_fiv[IO_MONITOR_DELAY_WRITE]);

	*data = rw4k_buff;
	return ret;
}

struct io_info_entry device_4k_rw_entry = {
	.name = "device_4k_rw",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_dev_4k_rw_distribution
};

static int get_dev_512k_rw_distribution(char **data)
{
	int ret = 0;
	char *rw512k_buff;

	rw512k_buff = kzalloc(1024, GFP_KERNEL);
	if (!rw512k_buff)
		return ret;
	ret = snprintf(rw512k_buff, 1024,
		"%lld %lld %lld %lld %lld %lld %lld %lld "
		"%lld %lld %lld %lld %lld %lld %lld %lld\n",
		req_para.req_device_para[IO_MONITOR_DELAY_512K].cnt[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].total_delay[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].max_delay[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_one[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_two[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_thr[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_fou[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_fiv[IO_MONITOR_DELAY_READ],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].cnt[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].total_delay[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].max_delay[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_one[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_two[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_thr[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_fou[IO_MONITOR_DELAY_WRITE],
		req_para.req_device_para[IO_MONITOR_DELAY_512K].stage_fiv[IO_MONITOR_DELAY_WRITE]);

	*data = rw512k_buff;
	return ret;
}

struct io_info_entry device_512k_rw_entry = {
	.name = "device_512k_rw",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_dev_512k_rw_distribution
};

static int get_dump_log(char **data)
{
	int ret = 0;
	char *dmesg_buff;

	dmesg_buff = kzalloc(4096, GFP_KERNEL);
	if (!dmesg_buff)
		return ret;
	ret = dump_log(dmesg_buff, 4096);
	*data = dmesg_buff;
	return ret;
}

struct io_info_entry ligntweight_dmesg_entry = {
	.name = "dmesg",
	.type = STR_FREE_ENTRY_TYPE,
	.get_value = get_dump_log
};

static int exit_task_notifier(struct notifier_block *self,
			      unsigned long cmd, void *v)
{
	struct task_struct *task = v;

	if (!task->ioac.read_bytes && !task->ioac.write_bytes)
		return NOTIFY_OK;

	exit_uid_find_or_register(task);
	return NOTIFY_OK;
}

static struct notifier_block exit_task_notifier_block = {
	.notifier_call = exit_task_notifier,
};

void add_pid_to_list(struct task_struct *task, size_t bytes, bool opt)
{
	int i = 0;
	int slot = task->pid%PID_HASH_LEGNTH;
	struct task_io_info *ti = NULL;
	struct task_io_info *old_ti = NULL;
	unsigned long min_time = jiffies;

	for (i = 0; i < PID_HASH_LEGNTH; i++) {
		ti = &taskio[(slot + i) % PID_HASH_LEGNTH];
		if (ti->used) {
			if (ti->pid == task->pid) {
				ti->time = jiffies;
				if (opt)
					ti->write_bytes += bytes;
				else
					ti->read_bytes += bytes;
				return;
			} else if (time_before(ti->time, min_time)) {
				min_time = ti->time;
				old_ti = ti;
			}
		} else {
			ti->pid = task->pid;
			ti->tgid = task->tgid;
			ti->uid = from_kuid_munged(current_user_ns(), task_uid(task));
			ti->time = jiffies;
			memcpy(ti->comm, task->comm, TASK_COMM_LEN);
			ti->used = true;
			if (opt)
				ti->write_bytes = bytes;
			else
				ti->read_bytes = bytes;
			return;
		}
	}
	if (!old_ti)
		return;
	old_ti->pid = task->pid;
	old_ti->tgid = task->tgid;
	old_ti->uid = from_kuid_munged(current_user_ns(), task_uid(task));
	old_ti->time = jiffies;
	memcpy(old_ti->comm, task->comm, TASK_COMM_LEN);
	old_ti->used = true;
	if (opt)
		old_ti->write_bytes = bytes;
	else
		old_ti->read_bytes = bytes;
}

static int task_status_get_value(struct seq_file *m, void *v)
{
	int i;
	struct task_io_info *ti = NULL;

	for (i = 0; i < PID_HASH_LEGNTH; i++) {
		ti = taskio + i;
		if (ti->pid)
			seq_printf(m, "%d %d %d %llu %llu %s\n",
				ti->pid, ti->tgid, ti->uid, ti->read_bytes,
				ti->write_bytes, ti->comm);
	}
	return 0;
}

static int get_task_status(char **data)
{
	int len = 0, i;
	struct task_io_info *ti = NULL;

	memset(task_buffer, 0, sizeof(struct task_io_info)*PID_HASH_LEGNTH);
	for (i = 0; i < PID_HASH_LEGNTH; i++) {
		ti = taskio + i;
		if (ti->pid)
			len += snprintf(task_buffer + len,
					sizeof(struct task_io_info) * PID_HASH_LEGNTH - len,
					"%d %d %d %llu %llu %s\n", ti->pid, ti->tgid,
					ti->uid, ti->read_bytes, ti->write_bytes, ti->comm);
	}
	*data = task_buffer;
	return len;
}

static inline void add_daily_entry(struct io_info_entry *entry)
{
	list_add_tail(&entry->list, &daily_list);
}

static inline void add_monitor_entry(struct io_info_entry *entry)
{
	list_add_tail(&entry->list, &abnormal_list);
}

static inline void add_flow_entry(struct io_info_entry *entry)
{
	list_add_tail(&entry->list, &flow_list);
}

static int io_monitor_show(struct seq_file *m, void *v)
{
	struct list_head *p;
	struct io_info_entry *entry;
	int ret;
	char *pbuf;

	p = daily_list.next;
	while (p != &daily_list) {
		entry = list_entry(p, struct io_info_entry, list);
		p = p->next;

		if (entry->type == NUM_ENTRY_TYPE) {
			ret = entry->get_value(NULL);
			if (-1 != ret)
				seq_printf(m, "%s %d\n", entry->name, ret);
		} else if (entry->type == STR_ENTRY_TYPE
			   || entry->type == STR_FREE_ENTRY_TYPE) {
			ret = entry->get_value(&pbuf);
			if (-1 != ret) {
				seq_printf(m, "%s %s", entry->name, pbuf);
				if (entry->type == STR_FREE_ENTRY_TYPE)
					kfree(pbuf);
			}
		}

	}
	memset(&fs_status, 0, sizeof(struct fs_status_info));
	/* memset(&disk_status, 0, sizeof(struct disk_info)); */
	memset(&req_para, 0, sizeof(struct req_delay_data));
	memset(&iowait_status, 0, sizeof(struct iowait_info));
	return 0;
}

static int iomonitor_open(struct inode *inode, struct file *file)
{
	return single_open(file, io_monitor_show, inode->i_private);
}

static const struct file_operations proc_dailyio_operations = {
	.open = iomonitor_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int iomonitor_dataflow_show(struct seq_file *m, void *v)
{
	task_status_get_value(m, v);
	return 0;
}

static int iomonitor_dataflow_open(struct inode *inode, struct file *file)
{
	return single_open(file, iomonitor_dataflow_show, inode->i_private);
}

static ssize_t iomonitor_dataflow_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[8] = { 0 };
	ssize_t buf_size;
	unsigned int v;
	int ret;

	if (!cnt)
		return 0;

	buf_size = min(cnt, (size_t)(sizeof(buf)-1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;
	buf[buf_size-1] = '\0';

	ret = kstrtouint(buf, 10, &v);
	if (ret)
		return ret;

	if (v > RECORD_SIZE)
		return -EINVAL;

	rt_record_status = v;

	printk("my debug %s %d:int dataflow=%d\n", __func__, __LINE__, rt_record_status);

	return cnt;
}

static int iomonitor_dataflow_release(struct inode *inode, struct file *file)
{
	memset(taskio, 0, sizeof(struct task_io_info)*PID_HASH_LEGNTH);
	return single_release(inode, file);
}

static ssize_t trigger_abnormal_event(struct file *filp,
				      const char __user *ubuf, size_t cnt,
				      loff_t *ppos)
{
	char buf[8] = { 0 };
	ssize_t buf_size;
	unsigned int v;
	int ret;

	if (!cnt)
		return 0;

	buf_size = min(cnt, (size_t)(sizeof(buf)-1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;
	buf[buf_size-1] = '\0';

	ret = kstrtouint(buf, 10, &v);
	if (ret)
		return ret;

	switch (v) {
	case 1:
		abnormal_handle(USER_TRIGGER, current->pid, 0);
		break;
	case 2:
		rt_record_handle();
		break;
	default:
		abnormal_handle(USER_TRIGGER, current->pid, 0);
		break;
	}

	return cnt;
}

static const struct file_operations proc_dataflow_operations = {
	.open = iomonitor_dataflow_open,
	.read = seq_read,
	.write = iomonitor_dataflow_write,
	.llseek = seq_lseek,
	.release = iomonitor_dataflow_release,
};

static int trigger_abnormal_open(struct inode *inode, struct file *file)
{
	return single_open(file, abnormal_interval_seq_show, inode->i_private);
}

static const struct file_operations trigger_abnormal_open_operations = {
	.open = trigger_abnormal_open,
	.read = seq_read,
	.write = trigger_abnormal_event,
	.llseek = seq_lseek,
	.release = single_release,
};

static int get_date(char *date)
{
	struct timespec64 ts;
	struct rtc_time tm;
	int len = 0;

	getnstimeofday64(&ts);
	ts.tv_sec -= sys_tz.tz_minuteswest * 60;
	rtc_time_to_tm(ts.tv_sec, &tm);

	len += snprintf(date + len, DATE_STR_LEN - len,
			"%04d-%02d-%02d %02d:%02d:%02d %d %d %d\n", tm.tm_year + 1900,
			tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
			tm.tm_sec, io_abnormal_info.reason, io_abnormal_info.pid,
			io_abnormal_info.delay);

	return len;
}

int write_abnormal_file(struct file *filp, struct io_info_entry *entry)
{
	char name_buf[TMP_BUF_LEN];
	int ret;
	loff_t file_pos = 0;
	char *pbuf = NULL;

	switch (entry->type) {
	case NUM_ENTRY_TYPE:
		ret = entry->get_value(NULL);
		if (-1 != ret) {
			memset(name_buf, 0, TMP_BUF_LEN);
			snprintf(name_buf, TMP_BUF_LEN, "%d\n", ret);
			kernel_write(filp, name_buf, strlen(name_buf), &file_pos);
		}
		break;
	case STR_ENTRY_TYPE:
	case STR_FREE_ENTRY_TYPE:
		ret = entry->get_value(&pbuf);
		if (-1 != ret) {
			memset(name_buf, 0, TMP_BUF_LEN);
			snprintf(name_buf, TMP_BUF_LEN, "%s %d\n", entry->name, ret);
			kernel_write(filp, name_buf, strlen(name_buf), &file_pos);
			kernel_write(filp, pbuf, ret, &file_pos);
			kernel_write(filp, ENTRY_END_TAG, strlen(ENTRY_END_TAG), &file_pos);

			if (entry->type == STR_FREE_ENTRY_TYPE) {
				if (pbuf)
					kfree(pbuf);
			}
		}
		break;
	case PRIVATE_ENTRY_TYPE:
		entry->get_value((char **)filp);
		break;
	}
	return 0;
}

static void realtime_record(struct work_struct  *work)
{
	printk("[iomonitor] RT record status %d\n", rt_record_status);
	if (time_before(jiffies, last_rt_record_jiffies +
					msecs_to_jiffies(rt_record_interval_s)))
		return;
	memset(rt_record_env[0], 0, IOM_REASON_LENGTH);
	snprintf(rt_record_env[0], IOM_REASON_LENGTH,
			"RT_TRIGGER=%d", rt_record_status);
	if (iomonitor_kobj) {
		printk("[iomonitor] send RT_TRIGGER uevent\n");
		kobject_uevent_env(iomonitor_kobj, KOBJ_CHANGE, rt_record_env);
	}
	last_rt_record_jiffies = jiffies;
}

static void io_abnormal(struct work_struct *work)
{
	struct list_head *p;
	struct file *filp_log;
	loff_t file_pos = 0;
	struct io_info_entry *io_entry;
	char *buffer = NULL;
	char date[DATE_STR_LEN];
	int flags = O_RDWR | O_CREAT | O_APPEND;

	if (time_before(jiffies, last_abnormal_jiffies +
					msecs_to_jiffies(io_abl_interval_s))) {
		atomic_dec(&abnormal_handle_ref);
		return;
	}

	memset(date, 0, DATE_STR_LEN);
	get_date(date);
	memset(io_monitor_env[0], 0, IOM_REASON_LENGTH);
	memset(io_monitor_env[1], 0, IOM_REASON_LENGTH);
	memset(io_monitor_env[2], 0, IOM_REASON_LENGTH);
	memset(io_monitor_env[3], 0, DATE_STR_LEN);
	snprintf(io_monitor_env[0], IOM_REASON_LENGTH,
				"REASON=%d", io_abnormal_info.reason);
	snprintf(io_monitor_env[1], IOM_REASON_LENGTH,
				"PID=%d", io_abnormal_info.pid);
	snprintf(io_monitor_env[2], IOM_REASON_LENGTH,
				"DELAY=%d", io_abnormal_info.delay);
	snprintf(io_monitor_env[3], DATE_STR_LEN, "DATE=%s", date);
	if (iomonitor_kobj)
		kobject_uevent_env(iomonitor_kobj, KOBJ_CHANGE, io_monitor_env);
	memset(&io_abnormal_info, 0, sizeof(struct abnormal_info));
	filp_log = filp_open(ABNORMAL_LOG_NAME, flags, 0666);
	if (IS_ERR(filp_log)) {
		atomic_dec(&abnormal_handle_ref);
		last_abnormal_jiffies = jiffies;
		return;
	}

	if (filp_log->f_inode->i_size > log_file_size) {
		atomic_dec(&abnormal_handle_ref);
		filp_close(filp_log, NULL);
		last_abnormal_jiffies = jiffies;
		return;
	}

	/* write log file header */
	kernel_write(filp_log, date, strlen(date), &file_pos);

	get_task_status(&buffer);
	kernel_write(filp_log, buffer, strlen(buffer), &file_pos);

	/* write log file data */
	p = abnormal_list.next;
	while (p != &abnormal_list) {
		io_entry = list_entry(p, struct io_info_entry, list);
		p = p->next;

		write_abnormal_file(filp_log, io_entry);
	}

	filp_close(filp_log, NULL);
	last_abnormal_jiffies = jiffies;

	atomic_dec(&abnormal_handle_ref);

	return;
}

/*
 * should be called at io abnormal point
 * might sleep,can't used in atomic-context
 */
int abnormal_handle(enum iomointor_io_type reason, pid_t pid, u64 delta_ms)
{
	if (atomic_inc_return(&abnormal_handle_ref) > 1) {
		atomic_dec(&abnormal_handle_ref);
		return 0;
	}

	io_abnormal_info.reason = reason;
	io_abnormal_info.pid = pid;
	io_abnormal_info.delay = (int)delta_ms;
	queue_delayed_work(io_abnormal_queue, &io_abnormal_work, msecs_to_jiffies(1));

	return 0;
}

/*
 * should be called at realtime record point
 * might sleep,can't used in atomic-context
 */
void rt_record_handle(void)
{
	queue_delayed_work(rt_record_queue, &rt_record_work, msecs_to_jiffies(1));
}

static void fs_status_init(void)
{
	memset(&fs_status, 0, sizeof(struct fs_status_info));
}

static int task_status_init(void)
{
	task_buffer = kzalloc(sizeof(struct task_io_info) * PID_HASH_LEGNTH, GFP_KERNEL);
	if (!task_buffer)
		return -1;
	profile_event_register(PROFILE_TASK_EXIT, &exit_task_notifier_block);
	return 0;
}

static int task_status_exit(void)
{
	if (task_buffer)
		kfree(task_buffer);
	profile_event_unregister(PROFILE_TASK_EXIT, &exit_task_notifier_block);
	return 0;
}

/*
 * alloc meme for IoMonitor
 * return 0 success,otherwith fail
 */
static int io_monitor_resource_init(void)
{
	fs_status_init();
	task_status_init();
	return 0;
}

static inline void remove_uevent_resourse(void)
{
	int i;

	for (i = 3; i < MAX_IOMEVENT_PARAM - 1; i++) {
		if (io_monitor_env[i])
			kfree(io_monitor_env[i]);
	}

	return;
}

/*
 * free meme for IoMonitor
 * return 0 success,otherwith fail
 */
static int io_monitor_resource_exit(void)
{
	task_status_exit();
	free_all_uid_entry();
	return 0;
}

static void add_monitor_items(void)
{
	add_monitor_entry(&free_mem_entry);
	/* add_monitor_entry(&io_queue_history_entry); */
	add_monitor_entry(&io_trace_entry);
	add_monitor_entry(&ligntweight_dmesg_entry);
}

static void add_daily_items(void)
{
	add_daily_entry(&fs_status_entry);
	add_daily_entry(&block_ux_io_entry);
	add_daily_entry(&block_sync_io_entry);
	add_daily_entry(&block_nsync_io_entry);
	add_daily_entry(&device_4k_rw_entry);
	add_daily_entry(&device_512k_rw_entry);
	add_daily_entry(&pgpg_status_entry);
#ifdef CONFIG_IOMONITOR_WITH_F2FS
	add_daily_entry(&disk_info_entry);
#endif
	add_daily_entry(&iowait_info_entry);
}

static int iomonitor_uevent_init(void)
{
	int i;

	io_monitor_env[0] = &iom_reason[0];
	io_monitor_env[1] = &iom_pid[0];
	io_monitor_env[2] = &iom_delay[0];
	io_monitor_env[3] = kzalloc(DATE_STR_LEN, GFP_KERNEL);
	if (!io_monitor_env[3]) {
		printk("io_monitor:kzalloc io monitor uevent param failed\n");
		goto err;
	}

	rt_record_env[0] = &rt_reason[0];
	iomonitor_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!iomonitor_kobj) {
		printk("io_monitor:find kobj %s failed.\n", KBUILD_MODNAME);
		goto err;
	}

	printk("io_monitor:find kobj %s success.\n", KBUILD_MODNAME);

	return 0;
err:
	for (i = 3; i < MAX_IOMEVENT_PARAM - 1; i++) {
		if (io_monitor_env[i])
			kfree(io_monitor_env[i]);
	}

	return -1;
}

static int __init io_monitor_init(void)
{
	int ret = -1;
	struct proc_dir_entry *daily_info_entry = NULL;
	struct proc_dir_entry *data_flow_entry = NULL;
	struct proc_dir_entry *uid_status_entry = NULL;
	struct proc_dir_entry *trigger_event_entry = NULL;
	struct proc_dir_entry *interval_entry = NULL;
	struct proc_dir_entry *size_entry = NULL;
	struct proc_dir_entry *iotrace_debug = NULL;

	ret = io_monitor_resource_init();
	if (ret != 0) {
		printk("io_monitor:resource init failed.\n");
		return -1;
	}

	ret = iomonitor_uevent_init();
	if (ret)
		goto err;

	IoMonitor_dir = proc_mkdir("IoMonitor", NULL);
	if (IoMonitor_dir) {
		daily_info_entry =
			proc_create("daily", S_IRUGO | S_IWUGO, IoMonitor_dir,
				&proc_dailyio_operations);
		if (!daily_info_entry) {
			printk("io_monitor:create daily_info failed.\n");
			ret = -1;
			goto err;
		}
		data_flow_entry =
			proc_create("flow", S_IRUGO | S_IWUGO, IoMonitor_dir,
				&proc_dataflow_operations);
		if (!data_flow_entry) {
			printk("io_monitor:create data_flow failed.\n");
			ret = -1;
			goto err;
		}

		trigger_event_entry =
			proc_create("event", S_IRUGO | S_IWUGO, IoMonitor_dir,
				&trigger_abnormal_open_operations);
		if (!trigger_event_entry) {
			printk("io_monitor:create trigger_event failed.\n");
			ret = -1;
			goto err;
		}

		uid_status_entry = create_uid_proc(IoMonitor_dir);
		if (!uid_status_entry) {
			printk("io_monitor:create uid_proc failed.\n");
			ret = -1;
			goto err;
		}

		interval_entry =
			proc_create("interval", S_IRUGO | S_IWUGO, IoMonitor_dir,
				&seq_abnormal_interval_fops);
		if (!interval_entry) {
			printk("io_monitor:create interval failed.\n");
			ret = -1;
			goto err;
		}

		size_entry = proc_create("log_size", S_IRUGO | S_IWUGO, IoMonitor_dir,
			&seq_abnormal_size_fops);
		if (!size_entry) {
			printk("io_monitor:create log_size failed.\n");
			ret = -1;
			goto err;
		}

		iotrace_debug = create_iotrace_proc(IoMonitor_dir);
		if (!iotrace_debug) {
			ret = -1;
			goto err;
		}
	}

	reqstats_init();
	add_monitor_items();
	add_daily_items();

	io_abnormal_queue = create_singlethread_workqueue("iomonitor-queue");
	if (io_abnormal_queue == NULL) {
		printk("%s: failed to create work queue iomonitor-queue\n", __func__);
		goto err;
	}

	rt_record_queue = create_singlethread_workqueue("iomonitor-rt-queue");
	if (rt_record_queue == NULL) {
		printk("%s: failed to create work queue iomonitor-rt-queue\n", __func__);
		goto err;
	}

	printk("io_monitor:init module success\n");

	return 0;

 err:
	if (IoMonitor_dir) {
		remove_proc_subtree("IoMonitor", NULL);
		IoMonitor_dir = NULL;
	}
	io_monitor_resource_exit();
	return ret;
}

static void __exit io_monitor_exit(void)
{
	if (io_abnormal_queue) {
		destroy_workqueue(io_abnormal_queue);
		io_abnormal_queue = NULL;
	}
	if (rt_record_queue) {
		destroy_workqueue(rt_record_queue);
		rt_record_queue = NULL;
	}
	remove_proc_subtree("IoMonitor", NULL);
	IoMonitor_dir = NULL;

	remove_uevent_resourse();

	io_monitor_resource_exit();

	printk("io_monitor: module exit\n");
}

module_init(io_monitor_init);
module_exit(io_monitor_exit);

MODULE_DESCRIPTION("iomonitor");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
