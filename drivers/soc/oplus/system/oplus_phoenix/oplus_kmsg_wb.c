/*
 * This program is used to reserve kernel log to block device
 */
#include "oplus_phoenix.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/kmsg_dump.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/uio.h>
#include <soc/oplus/system/boot_mode.h>

#define oprkl_info_print(fmt,arg...) \
pr_err("[op_kernel_log] "fmt, ##arg)

#define oprkl_err_print(fmt,arg...) \
pr_err("[op_kernel_log] "fmt, ##arg)


#define TARGET_PARTITION_LABEL "PARTLABEL=oplusreserve5"

#define RETRY_COUNT_FOR_GET_DEVICE 30
#define WAITING_FOR_GET_DEVICE 100
#define STOP_RECORD_MSEC 60 * 1000 //60 sec = 60*1000 msec
#define PER_LOOP_MSEC 500 //check log update once per 0.5 sec
#define STOP_CNT STOP_RECORD_MSEC/PER_LOOP_MSEC
#define BUF_SIZE_OF_LINE 2048
#define WB_BLOCK_SIZE 4096
#define PER_LOG_BUF_SIZE WB_BLOCK_SIZE
#define HEADER_BLOCK 1 //1*4096
#define PER_LOG_BLOCK  512 //per log has 512*4096 space
#define KERNEL_HEAD_OFFSET 1024*1024*4 //reserve 0~ 4Mb for uefi log thread use
#define UEFI_HEAD_OFFSET 0
#define MAX_LOG_NUM 16
#define OP_KERNEL_LOG_DEBUG 1

static struct task_struct *tsk;
static signed long long wb_start_offset = 0;
const char* LOG_THREAD_MAGIC = "PHOENIX2.0";
const char phoenix2_feedback_magic = 'x';
const struct file_operations f_op = {.fsync = blkdev_fsync};

struct kernel_log_thread_header
{
    /// magic number identify kernel log
	uint64_t bootcount;
	char magic[12]; //magic is PHOENIX2.0
	char is_monitoring; //==1,is normal mode need monitor
	char is_needfeedback; //=='x', need upload to DCS
	uint64_t last_bootcount;
	uint64_t nobootcount;
};


struct uefi_log_thread_header
{
    /// magic number identify uefi log
	uint64_t bootcount;
	char magic[12];
	char is_monitoring; // ==1,is normal mode need monitor
	char is_needfeedback; // ==1, need upload to DCS
	uint64_t no_bootcount;
};

int wb_log(struct block_device *bdev, char *buf, char *line_buf) {
	struct file dev_map_file;
	struct kmsg_dumper kmsg_dumper;
	struct kvec iov;
	struct iov_iter iter;
	struct kiocb kiocb;
	int ret = 0;
	int temp_size = 0;
	int line_count = 0;
	int loop_cnt = 0;
	int total_write_times = 0;
	int write_block_cnt = 0;
	size_t len = 0;
	bool isforcewb = false; // write once per 0.5 sec PER_LOOP_MSEC
	bool isfull = false; // buf is full

	memset(&dev_map_file, 0, sizeof(struct file));
	dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
	dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file.f_inode = bdev->bd_inode;

	init_sync_kiocb(&kiocb, &dev_map_file);
	kiocb.ki_pos = KERNEL_HEAD_OFFSET + wb_start_offset; //start wb offset
	memset(&kmsg_dumper, 0, sizeof(struct kmsg_dumper));
	kmsg_dumper.active = true;
	memset(buf, 0, PER_LOG_BUF_SIZE);
	oprkl_info_print("Start write back log\n");
	while (loop_cnt < STOP_CNT) { //STOP_CNT = 60 sec, so it'll record 60 sec kmsg.
		while (kmsg_dump_get_line(&kmsg_dumper, true, line_buf, BUF_SIZE_OF_LINE, &len)) {
			if(((temp_size + (len - 1)) >= WB_BLOCK_SIZE)) {
				isfull = true;
			}
			if (isfull || isforcewb) {
				total_write_times++; // count real write back times(debug use)
				iov.iov_base = (void *)buf;
				iov.iov_len = WB_BLOCK_SIZE;
				iov_iter_kvec(&iter, WRITE, &iov, 0, WB_BLOCK_SIZE);
				ret = generic_write_checks(&kiocb, &iter);
				if (likely(ret > 0))
					ret = generic_perform_write(&dev_map_file, &iter, kiocb.ki_pos);
				else {
					oprkl_err_print("generic_write_checks failed\n");
					return 1;
				}
				if (likely(ret > 0)) {
					dev_map_file.f_op = &f_op;
					if(isfull) {
						kiocb.ki_pos += WB_BLOCK_SIZE; //shfit offset of write back size
					}
					ret = generic_write_sync(&kiocb, ret);
					if (ret < 0) {
						oprkl_err_print("Write sync failed\n");
						return 1;
					}
				} else {
					oprkl_err_print("generic_perform_write failed\n");
					return 1;
				}
				if(isfull) {
					write_block_cnt ++;
					if (write_block_cnt >= PER_LOG_BLOCK) { //it will record 2M
						oprkl_err_print("write_block_cnt >= PER_LOG_BLOCK, This mean log full, stop record\n");
						goto FINISH;
					}
					memset(buf, 0, PER_LOG_BUF_SIZE);
					temp_size = 0;
				}
				if (phx_is_system_server_init_start()) {
					oprkl_info_print("system_server init, stop record\n");
					//it mean kernel ~ native boot complete
					goto FINISH;
				}
			}
			isfull = false;
			isforcewb = false;
			if(((temp_size + (len - 1)) >= WB_BLOCK_SIZE)) {
				oprkl_info_print("reserve_log failed wb %d line len %d ,buf offset ptr:%x, buf max offset ptr:%x\n", temp_size, len , (buf + temp_size), (buf + WB_BLOCK_SIZE));
				BUG_ON (OP_KERNEL_LOG_DEBUG);
			}
			memcpy(buf + temp_size, line_buf, (len -1));
			temp_size += (len -1);
			memset(line_buf, 0, BUF_SIZE_OF_LINE);
			line_count++;
			len = 0;
		}


		loop_cnt++;
		msleep_interruptible(PER_LOOP_MSEC); //check log update once per 0.5 sec
		isforcewb = true;
	}
FINISH:
	oprkl_info_print("Finish! Stop record line_count: %d write_block_cnt: %d, total_write_times: %d\n", line_count, write_block_cnt, total_write_times);

	filp_close(&dev_map_file, NULL);
	return 0;
}

int read_header(struct block_device *bdev, void *head, int len, int pos) {
	struct file dev_map_file;
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov;
	int read_size = 0;

	memset(&dev_map_file, 0, sizeof(struct file));

	dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
	dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file.f_inode = bdev->bd_inode;

	init_sync_kiocb(&kiocb, &dev_map_file);
	kiocb.ki_pos = pos; //start header offset
	iov.iov_base = head;
	iov.iov_len = len;
	iov_iter_kvec(&iter, READ, &iov, 1, len);

	read_size = generic_file_read_iter(&kiocb, &iter);
	oprkl_info_print("read_header read_size %d\n", read_size);
	filp_close(&dev_map_file, NULL);

	if (read_size <= 0) {
		oprkl_err_print("read header failed\n");
		return 1;
	}
	return 0;
}

int write_header(struct block_device *bdev, void *head, int len, int pos) {
	struct file dev_map_file;
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov;
	int ret = 0;

	memset(&dev_map_file, 0, sizeof(struct file));

	dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
	dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file.f_inode = bdev->bd_inode;

	init_sync_kiocb(&kiocb, &dev_map_file);
	kiocb.ki_pos = pos; //start header offset
	iov.iov_base = head;
	iov.iov_len = len;
	iov_iter_kvec(&iter, WRITE, &iov, 1, len);

	ret = generic_write_checks(&kiocb, &iter);
	if (ret > 0)
		ret = generic_perform_write(&dev_map_file, &iter, kiocb.ki_pos);
	if (ret > 0) {
		dev_map_file.f_op = &f_op;
		kiocb.ki_pos += ret;
		ret = generic_write_sync(&kiocb, ret);
		if (ret < 0) {
			oprkl_err_print("Write sync failed\n");
			return 1;
		}
	}
	filp_close(&dev_map_file, NULL);
	return 0;
}

struct block_device *get_reserve_partition_bedv(void)
{
	struct block_device *bdev = NULL;
	int retry_wait_for_device = RETRY_COUNT_FOR_GET_DEVICE;
	dev_t devt;

RETRY_DEVICE:
	devt = name_to_dev_t(TARGET_PARTITION_LABEL);
	if (devt) {
		bdev = blkdev_get_by_dev(devt, FMODE_READ | FMODE_WRITE, NULL);
		if (!IS_ERR(bdev)) {
			oprkl_err_print("Get partition success!\n");
			return bdev;
		}
	}
	if(retry_wait_for_device > 0) {
		retry_wait_for_device--;
		oprkl_err_print("Failed to get dev block, retry %d\n", retry_wait_for_device);
		msleep_interruptible(WAITING_FOR_GET_DEVICE);
		goto RETRY_DEVICE;
	}
	oprkl_err_print("Failed to get dev block final\n");
	return NULL;
}

int init_header(struct block_device *bdev, struct kernel_log_thread_header *head) {
	int ret = 0;

	memset(head, 0, sizeof(struct kernel_log_thread_header));
	ret = read_header(bdev, head, sizeof(struct kernel_log_thread_header), KERNEL_HEAD_OFFSET);
	if(ret)
		return ret;

	oprkl_info_print("Now read LOG_THREAD_MAGIC: %x %x %x\n", head->magic[0], head->magic[1], head->magic[2]);

	if (memcmp(head->magic, LOG_THREAD_MAGIC, strlen(LOG_THREAD_MAGIC)) == 0) {
		oprkl_info_print("This is initialized header\n");
		if (head->is_monitoring == 1) {
			oprkl_err_print("Last boot, not complete need feedback\n");
			head->is_needfeedback = phoenix2_feedback_magic;
			head->nobootcount = head->last_bootcount;
			head->is_monitoring = 0;
			head->last_bootcount = 0;
		}
		head->bootcount++;
	} else {
		oprkl_info_print("No match ,The boot is first boot, need init\n");
		memset(head, 0, sizeof(struct kernel_log_thread_header));
		memcpy(head->magic, LOG_THREAD_MAGIC, strlen(LOG_THREAD_MAGIC));
	}
	if (get_boot_mode() == MSM_BOOT_MODE__NORMAL) { //only normal boot need monitor
		oprkl_info_print("The boot is normal boot, need monitor\n");
		head->is_monitoring = 1;
		head->last_bootcount = head->bootcount;
	}
	ret = write_header(bdev, head, sizeof(struct kernel_log_thread_header), KERNEL_HEAD_OFFSET);

	return ret;
}

int clear_monitor_header_flag(struct block_device *bdev, struct kernel_log_thread_header *head) {
	int ret = 0;

	memset(head, 0, sizeof(struct kernel_log_thread_header));
	ret = read_header(bdev, head, sizeof(struct kernel_log_thread_header), KERNEL_HEAD_OFFSET);
	if (ret) {
		oprkl_info_print("Record completed, But read header fail\n");
		return ret;
	}
	head->is_monitoring = 0;
	head->last_bootcount = 0;
	ret = write_header(bdev, head, sizeof(struct kernel_log_thread_header), KERNEL_HEAD_OFFSET);
	if (ret) {
		oprkl_info_print("Record completed, But write header fail\n");
		return ret;
	}
	oprkl_info_print("Record completed, clear monitor flag\n");
	return 0;
}

static int reserve_log_main(void *arg)
{
	struct block_device *bdev = NULL;
	struct kernel_log_thread_header kernel_head;
	struct uefi_log_thread_header uefi_head;
	char *data_buf = NULL;
	char *line_buf = NULL;
	int ret = 0;

	oprkl_info_print("Start %s pid:%d, name:%s\n", __func__, current->pid, current->comm);
	bdev = get_reserve_partition_bedv();
	if (!bdev) {
		goto GETPATH_FAIL;
	}

	data_buf = (char *)kzalloc(PER_LOG_BUF_SIZE * sizeof(char), GFP_KERNEL);
	if (!data_buf) {
		oprkl_err_print("Alloc data_buf failed\n");
		goto EXIT;
	}

	line_buf = (char *)kzalloc(BUF_SIZE_OF_LINE * sizeof(char), GFP_KERNEL);
	if (!line_buf) {
		oprkl_err_print("Alloc line_buf failed\n");
		goto EXIT;
	}

	if(init_header(bdev, &kernel_head)) {
		oprkl_err_print("init_header failed\n");
		goto EXIT;
	}

	if(read_header(bdev, &uefi_head, sizeof(struct uefi_log_thread_header), UEFI_HEAD_OFFSET)) {
		oprkl_err_print("read uefi_log_thread_header failed\n");
		goto EXIT;
	}
	oprkl_info_print("Now kernel boot count %d and uefi boot count %d, is_monitoring %d, log number:[%d / 15]\n", kernel_head.bootcount, uefi_head.bootcount, kernel_head.is_monitoring, kernel_head.bootcount % MAX_LOG_NUM);
	wb_start_offset = (HEADER_BLOCK + (kernel_head.bootcount % MAX_LOG_NUM) * PER_LOG_BLOCK) * WB_BLOCK_SIZE;
	oprkl_info_print("Write back start offset %llu\n", wb_start_offset);
	ret = wb_log(bdev, data_buf, line_buf);
	if(!ret && phx_is_system_server_init_start()) {
		if(clear_monitor_header_flag(bdev, &kernel_head)){
			oprkl_err_print("clear_monitor_header_flag failed\n");
			goto EXIT;
		}
	}
EXIT:
	if (data_buf) {
		kfree(data_buf);
		data_buf = NULL;
		oprkl_info_print("Free data_buf buffer\n");
	}

	if (line_buf) {
		kfree(line_buf);
		line_buf = NULL;
		oprkl_info_print("Free line_buf buffer\n");
	}

	if (bdev) {
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE);
		bdev = NULL;
		oprkl_info_print("Put device\n");
	}

GETPATH_FAIL:
	oprkl_info_print("End %s\n", __func__);
	return 0;
}

static int __init kernel_log_wb_int(void)
{
	oprkl_info_print("kernel_log_wb_int\n");
	tsk = kthread_run(reserve_log_main, NULL, "op_kernel_log");
	if (!tsk)
		oprkl_err_print("kthread init failed\n");
	oprkl_info_print("kthread init done\n");

	return 0;

}

static void __exit kernel_log_wb_exit(void)
{
	oprkl_info_print("bye bye\n");
}

late_initcall(kernel_log_wb_int);
module_exit(kernel_log_wb_exit);
MODULE_LICENSE("GPL v2");
MODULE_LICENSE("Dual BSD/GPL");
