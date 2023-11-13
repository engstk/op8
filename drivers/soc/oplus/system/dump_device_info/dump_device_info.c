// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <soc/qcom/socinfo.h>
#include <linux/soc/qcom/smem.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/pstore_ram.h>
#else
#include <linux/pstore.h>
#endif
#include <linux/notifier.h>
#include "../../../fs/pstore/internal.h"
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/system/device_info.h>


#define SMEM_CHIP_INFO 137

extern struct pstore_info *psinfo;

static char project_version[8];
static char pcb_version[8];
static char rf_version[8];

#define BUILD_PROP  "/my_product/build.prop"
#define VERSION_LENGTH 64
char build_version_key[24] = "ro.build.version.ota=";
char build_version[VERSION_LENGTH];



#define GET_VERSION_INFO_TIMEOUT_MS     150000

struct get_version_info {
        struct delayed_work version_info_work;
        wait_queue_head_t info_thread_wq;
};

struct get_version_info g_version_info;

#define MAX_ITEM 2
#define MAX_LENGTH 32

enum
{
	serialno = 0,
	platform_name,
};


char oem_serialno[16];
char oem_platform_name[16];

const char cmdline_info[MAX_ITEM][MAX_LENGTH] =
{
	"androidboot.serialno=",
	"androidboot.platform_name=",
};

static int __init device_info_init(void)
{
	int i, j;
	char *substr, *target_str;

	for (i = 0; i < MAX_ITEM; i++) {
		substr = strstr(boot_command_line, cmdline_info[i]);
	        if (substr != NULL)
        		substr += strlen(cmdline_info[i]);
	        else
        		continue;

        if (i == serialno)
	        target_str = oem_serialno;
        else if (i == platform_name)
        	target_str = oem_platform_name;

        for (j = 0; substr[j] != ' '; j++)
	        target_str[j] = substr[j];
        target_str[j] = '\0';
	}
	return 1;
}

/*device info init to black*/
static int pstore_device_info_init(void)
{
        size_t oldsize;
        size_t size = 0;

        struct ramoops_context *cxt;
        struct pstore_record record;

        if (psinfo == NULL)
                return -1;

        cxt = psinfo->data;
        size = cxt->device_info_size;

        pstore_record_init(&record, psinfo);
        record.type = PSTORE_TYPE_DEVICE_INFO;
        record.buf = psinfo->buf;
        record.size = size;

        oldsize = psinfo->bufsize;


        if (size > psinfo->bufsize)
                size = psinfo->bufsize;

        memset(record.buf, ' ', size);
        psinfo->write(&record);
        psinfo->bufsize = oldsize;
        return 0;
}

static void pstore_write_device_info(const char *s, unsigned int c)
{
        const char *e = s + c;
        if (psinfo == NULL)
                return;

        while (s < e) {
                struct pstore_record record;
                pstore_record_init(&record, psinfo);
                record.type = PSTORE_TYPE_DEVICE_INFO;

                if (c > psinfo->bufsize)
                        c = psinfo->bufsize;

                record.buf = (char *)s;
                record.size = c;
                psinfo->write(&record);
                s += c;
                c = e - s;
        }
}

static void board_hw_info_init(void)
{
	scnprintf(pcb_version, sizeof(pcb_version), "%d", get_PCB_Version());
	scnprintf(project_version, sizeof(project_version), "%d", get_project());
	scnprintf(rf_version, sizeof(rf_version), "%d", get_Modem_Version());
}

static void write_device_info(const char *key, const char *value)
{
        pstore_write_device_info(key, strlen(key));
        pstore_write_device_info(": ", 2);
        pstore_write_device_info(value, strlen(value));
        pstore_write_device_info("\r\n", 2);
}


static void get_version_info_handle(struct work_struct *work)
{
        struct file *fp;
        int i = 0;
        ssize_t len = 0;
        char *substr, *buf;
        int old_fs = 0;
        loff_t pos, i_size;
        /* limit /my_product/build.prop file max size is 24k*/
        loff_t max_size = 24*1024;

        printk("[get_version_info_handle]\n");
        fp = filp_open(BUILD_PROP, O_RDONLY, 0600);
        if (IS_ERR(fp)) {
                pr_info("open %s file fail fp:%p %d \n", BUILD_PROP, fp, PTR_ERR(fp));
                goto out;
        }

        i_size = i_size_read(file_inode(fp));
        if (i_size <= 0) {
                pr_info("read  %s file size fail \n", BUILD_PROP);
                goto out;
        }
        if (i_size > max_size) {
                pr_info("%s file i_size %lld is greate than max_size %lld \n", BUILD_PROP, i_size, max_size);
                goto out;
        }
        buf = vzalloc(i_size + 1);
        if (!buf) {
                pr_info("%s file isize %lld vmalloc fail \n", BUILD_PROP, i_size);
                goto out;
        }

        old_fs = get_fs();
        set_fs(KERNEL_DS);

        pos = 0;
        len = vfs_read(fp, buf, i_size, &pos);
        if (len < 0) {
                pr_info("read %s file error\n", BUILD_PROP);
        }

        substr = strstr(buf, build_version_key);
        pr_info("\n");
        pr_info("build_version:-%s--\n", substr);

        if (substr != NULL) {
	        while ((*substr != '\n') && (*substr != 0)) {
        	        if (*substr == '=')
                	    break;
                	substr++;
	        }
		if (*substr == '=') {
			while ((*(++substr) != '\n') && (i < VERSION_LENGTH)) {
				build_version[i] = *(substr);
	                	i++;
        		}
		}
        }

        pr_info("build_version_value:%s--\n", build_version);
        write_device_info("software version", build_version);
        vfree(buf);

out:
	if (IS_ERR(fp)) {
                pr_info("open is failed, cannot to read\n");
        } else {
                filp_close(fp, NULL);
                set_fs(old_fs);
        }
}

static int __init init_device_info(void)
{
	int ret = 0;
	printk("pstore device info init");
	ret = pstore_device_info_init();
	if (ret < 0) {
		return ret;
	}
	board_hw_info_init();
	device_info_init();

	write_device_info("project version", project_version);
	write_device_info("pcb version", pcb_version);
	write_device_info("rf version", rf_version);
	write_device_info("soc version", oem_platform_name);
	write_device_info("serial no", oem_serialno);

	write_device_info("kernel version", linux_banner);
	write_device_info("boot command", saved_command_line);

	INIT_DELAYED_WORK(&g_version_info.version_info_work, get_version_info_handle);
	schedule_delayed_work(&g_version_info.version_info_work, msecs_to_jiffies(GET_VERSION_INFO_TIMEOUT_MS));

	return ret;
}

late_initcall(init_device_info);

void save_dump_reason_to_device_info(char *reason) {
	write_device_info("dump reason is ", reason);
}
EXPORT_SYMBOL(save_dump_reason_to_device_info);

