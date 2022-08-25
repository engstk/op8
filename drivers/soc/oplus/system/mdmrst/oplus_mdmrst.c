// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_ARM
#include <linux/sched.h>
#else
#include <linux/wait.h>
#endif
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>


/*****yixue.ge ******/
#define MDMREASON_BUF_LEN 128
static wait_queue_head_t mdmreason_wq;
static int mdmreason_flag = 0;
static char mdmreason_buf[MDMREASON_BUF_LEN] = {0};
void mdmreason_set(char * buf)
{
        if ((buf != NULL) && (buf[0] != '\0')) {
                if (strlen(buf) >= MDMREASON_BUF_LEN) {
                        memcpy(mdmreason_buf, buf, MDMREASON_BUF_LEN - 1);
                        mdmreason_buf[MDMREASON_BUF_LEN - 1] = '\0';
                } else {
                        strcpy(mdmreason_buf, buf);
                }
                mdmreason_flag = 1;
                wake_up_interruptible(&mdmreason_wq);
        }
}
EXPORT_SYMBOL(mdmreason_set);

/*this write interface just use for test*/
static ssize_t mdmreason_write(struct file *file, const char __user * buf,
                size_t count, loff_t * ppos)
{
        /*just for test*/
        if (count > MDMREASON_BUF_LEN) {
                count = MDMREASON_BUF_LEN;
        }
        if (count > *ppos) {
                count -= *ppos;
        }
        else
                count = 0;

        printk("mdmreason_write is called\n");

        if (copy_from_user(mdmreason_buf, buf, count)) {
                return -EFAULT;
        }
        *ppos += count;

        mdmreason_flag = 1;
        wake_up_interruptible(&mdmreason_wq);

        return count;
}


static unsigned int mdmreason_poll (struct file *file, struct poll_table_struct *pt)
{
        unsigned int ptr = 0;

        poll_wait(file, &mdmreason_wq, pt);

        if (mdmreason_flag) {
                ptr |= POLLIN | POLLRDNORM;
                mdmreason_flag = 0;
        }
        return ptr;
}

static ssize_t mdmreason_read(struct file *file, char __user *buf,
                size_t count, loff_t *ppos)
{
        size_t size = 0;

        if (count > MDMREASON_BUF_LEN) {
                count = MDMREASON_BUF_LEN;
        }
        size = count < strlen(mdmreason_buf) ? count : strlen(mdmreason_buf);

        if (size > *ppos) {
                size -= *ppos;
        }
        else
                size = 0;

        if (copy_to_user(buf, mdmreason_buf, size)) {
                return -EFAULT;
        }
        /*mdmreason_flag = 0;*/
        *ppos += size;

        return size;
}

static int mdmreason_release (struct inode *inode, struct file *file)
{
        /*mdmreason_flag = 0;*/
        /*memset(mdmreason_buf, 0, MDMREASON_BUF_LEN);*/
        return 0;
}


static const struct file_operations mdmreason_device_fops = {
        .owner  = THIS_MODULE,
        .read   = mdmreason_read,
        .write        = mdmreason_write,
        .poll        = mdmreason_poll,
        .llseek = generic_file_llseek,
        .release = mdmreason_release,
};

static struct miscdevice mdmreason_device = {
        MISC_DYNAMIC_MINOR, "mdmreason", &mdmreason_device_fops
};



/*****yixue.ge add end******/

wait_queue_head_t mdmrst_wq;
/*unsigned int mdmrest_flg;*/
unsigned int mdmrest_count = 0;

ssize_t mdmrst_read(struct file *file, char __user *buf,
                size_t count, loff_t *ppos)
{
        char *tmp = "1";

        /*wait_event_interruptible(mdmrst_wq, mdmrest_flg);*/
        /*if (copy_to_user(buf, tmp, strlen(tmp)))*/
        if (copy_to_user(buf, &mdmrest_count, 1)) {
                return -EFAULT;
        }
        /*mdmrest_flg = 0;*/

        return strlen(tmp);
}

static const struct file_operations mdmrst_device_fops = {
        .owner  = THIS_MODULE,
        .read   = mdmrst_read,
};

static struct miscdevice mdmrst_device = {
        MISC_DYNAMIC_MINOR, "mdmrst", &mdmrst_device_fops
};

static int __init mdmrst_init(void)
{
        init_waitqueue_head(&mdmrst_wq);
        init_waitqueue_head(&mdmreason_wq);
        misc_register(&mdmreason_device);
        return misc_register(&mdmrst_device);
}

static void __exit mdmrst_exit(void)
{
        misc_deregister(&mdmreason_device);
        misc_deregister(&mdmrst_device);
}

module_init(mdmrst_init);
module_exit(mdmrst_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oplus");
