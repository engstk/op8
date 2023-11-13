// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** File : uboot_log.c
** Description : BSP uboot_log back up xbl uefi kernel boot log , cat /proc/boot_dmesg
** Version : 1.0
******************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/kmsg_dump.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kallsyms.h>
#include <linux/init.h>


#include <soc/qcom/memory_dump.h>
#include <soc/qcom/minidump.h>

#include <soc/oplus/system/uboot_utils.h>

#define __KBOOT_LOG_OFFSET				0
#define LINE_MAX_SIZE					1024

static u32  xbl_uefi_logoffset;

/* record kernel boot buffer */
static char *kboot_log_buf = NULL;
static u32 	kboot_log_buf_len;
static size_t total_size = 0;

static struct kmsg_dumper kboot_dumper;
static struct task_struct *ubootback_thread = NULL;


/* record xbl uefi boot buffer */
static char *uboot_log_buf = NULL;
static u32 	uboot_log_buf_len;




/*
 * init uboot/kboot log buffer addr/size
 */
static int uboot_kboot_buffer_init(void)
{
	struct reserved_mem *r_mem = NULL;
	struct device_node *reserved_memory, *kboot_uboot_logmemory;

	reserved_memory = of_find_node_by_path("/reserved-memory");
	if (!reserved_memory) {
		pr_err("No reserved-memory information found in DT\n");
		return -EINVAL;
	}

	kboot_uboot_logmemory = of_get_child_by_name(reserved_memory, "kboot_uboot_logmem");
	if (!kboot_uboot_logmemory) {
		pr_err("Can not find kboot_uboot_logmemory node in reserved-memory\n");
		return -EINVAL;
	}

	r_mem = of_reserved_mem_lookup(kboot_uboot_logmemory);
	if (!r_mem) {
		pr_err("failed to acquire memory region\n");
		return -EINVAL;
	}
	if (of_property_read_u32(kboot_uboot_logmemory, "xbluefi-offset", &xbl_uefi_logoffset)) {
		pr_err("failed to acquire kboot_uboot_logmemory log offset\n");
		return -EINVAL;
	}
	pr_debug("xbl_uefi_memory:base:%llx, size:%llx, xbl_uefi_logoffset:%x\n", r_mem->base, r_mem->size, xbl_uefi_logoffset);

	if ((xbl_uefi_logoffset + SERIAL_BUFFER_SIZE) > r_mem->size) {
		WARN(1, "xbl_uefi logbuf may be out of bounds\n");
		return -ENOMEM;
	}

	uboot_reserved_remap = memremap(r_mem->base, r_mem->size, MEMREMAP_WB);
	if (IS_ERR_OR_NULL(uboot_reserved_remap)) {
		pr_err("failed to remap uefi memory \n");
		uboot_reserved_remap = NULL;
		return -EINVAL;
	}

	uboot_base = r_mem->base;
	uboot_size = r_mem->size;

	uboot_log_buf_len = r_mem->size - xbl_uefi_logoffset;
	kboot_log_buf_len = xbl_uefi_logoffset;
	uboot_log_buf = uboot_reserved_remap + xbl_uefi_logoffset;
	kboot_log_buf = uboot_reserved_remap + __KBOOT_LOG_OFFSET;

	return 0;
}




/*
 * backup and show kernel boot log
 */
static int kboot_seq_read(struct seq_file *s, void *v)
{
	int i = 0;
	uboot_log_buf[uboot_log_buf_len-1] = '\0';
	seq_printf(s, "xbl_uefi boot log begin: \n");
	while (i < uboot_log_buf_len -1) {
		while (!uboot_log_buf[i] && i < uboot_log_buf_len -1) {
			i++;
		}
		seq_printf(s, "%s\n", uboot_log_buf+i);
		while (uboot_log_buf[i] && i < uboot_log_buf_len -1) {
			i++;
		}
	}


	i = 0 ;
	kboot_log_buf[kboot_log_buf_len - 1] = '\0';
	seq_printf(s, "kernel boot log begin: \n");
	while (i < total_size) {
		/* find valid data to print out */
		while (!kboot_log_buf[i] && i < total_size) {
			i++;
		}
		seq_printf(s, "%s\n", kboot_log_buf+i);
		/* find invalid data to avoild interruption by '\0' */
		while (kboot_log_buf[i] && i < total_size) {
			i++;
		}
	}

	return 0;
}


static int kboot_file_open (struct inode *inode, struct file *file)
{
	return single_open(file, &kboot_seq_read, PDE_DATA(inode));
}

const struct file_operations kboot_fops = {
	.owner = THIS_MODULE,
	.open = kboot_file_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Dont printk any log int this thread
 */
static int ubootback_thread_fn(void *data)
{
	size_t line_len = 0;
	u32 idx =0;
	u64 seq =0;

	kboot_dumper.active = true;
	while (kboot_log_buf_len > total_size + 2*LINE_MAX_SIZE)
	{
		kmsg_dump_rewind(&kboot_dumper);
		kboot_dumper.cur_seq = seq;
		kboot_dumper.cur_idx = idx;
		if (back_kmsg_dump_get_buffer(&kboot_dumper, true, kboot_log_buf + total_size, kboot_log_buf_len - total_size, &line_len)) {
			total_size += line_len;
			line_len = 0;
		}
		seq = kboot_dumper.cur_seq;
		idx = kboot_dumper.cur_idx;
		msleep(10*1000);

	}
	return 0;
}

static int __init kernel_uboot_log_init(void)
{
	struct proc_dir_entry *pEntry = NULL;

	memset(kboot_log_buf, 0, kboot_log_buf_len);
	pEntry = proc_create_data("boot_dmesg", 0444, NULL, &kboot_fops, NULL);
	if (!pEntry) {
		pr_err("failed to make boot_dmesg proc node\n");
		return -EINVAL;
	}
	ubootback_thread = kthread_run(ubootback_thread_fn, NULL, "ubootback_thread");
	if(IS_ERR(ubootback_thread)) {
		pr_err("Creating kbootback_thread failed!\n");
		return -EINVAL;
	}
	return 0;
}

/*
 * Add for dump kernel bootlog for minidump
 */
static void __init register_boot_log_buf(void)
{
	struct md_region md_entry = {};

	/*Register ubootlog to minidump, first idx would be from bss section */
	strlcpy(md_entry.name, "UBOOTLOG", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t) uboot_reserved_remap;
	md_entry.phys_addr = uboot_base;
	md_entry.size = uboot_size;

	if (msm_minidump_add_region(&md_entry))
		pr_err("Failed to add UBOOTLOG in Minidump\n");

	return;
}

static int __init oplus_uboot_device_init(void)
{
	if (!uboot_kboot_buffer_init()) {
		register_boot_log_buf();
		kernel_uboot_log_init();

	}
	return 0;
}


late_initcall(oplus_uboot_device_init);
