//#ifdef OPLUS_FEATURE_MDMFEATURE
/*==============================================================================

 Copyright (c) 2020-2024 OPLUS,  All Rights  Reserved.  OPLUS Proprietary.

-------------------------------------------------------------------------------

                      EDIT HISTORY FOR FILE

  This section contains comments describing changes made to the module.
  Notice that changes are listed in reverse chronological order.

 when       who     what, where, why
 --------   ---     ---------------------------------------------------------
 2021-10-19   ZhengXueqian  create this module

==============================================================================*/
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
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#include <linux/soc/qcom/smem.h>
#endif
#include <linux/proc_fs.h>

/*****xueqian.zheng ******/
#define OPLUS_RESERVE1_BLOCK_SZ             (4096)
#define SMEM_OPLUS_FEATURE_MAP_SZ           (OPLUS_RESERVE1_BLOCK_SZ * 3)
#define SMEM_OPLUS_FEATURE_MAP 126
static wait_queue_head_t mdmfeature_wq;
static int mdmfeature_flag = 0;
static char mdmfeature_buf[SMEM_OPLUS_FEATURE_MAP_SZ] = {0};
/*this write interface just use for test*/
static bool write_mdmfeature_to_smem(char __user * buf, size_t count)
{
    size_t smem_size;
    void *smem_addr;
    /*get mdmfeature info from smem*/
    #ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
    smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
    SMEM_OPLUS_FEATURE_MAP,
    &smem_size);
    #endif
    if (IS_ERR_OR_NULL(smem_addr)) {
        pr_debug("%s: get mdmfeature failure\n", __func__);
        return false;
    }
    memcpy(smem_addr, buf, count);
    return true;
}

static bool read_mdmfeature_from_smem(char __user * buf, size_t* smem_size)
{
    void *smem_addr;
    /*get mdmfeature info from smem*/
    #ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
    smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
    SMEM_OPLUS_FEATURE_MAP,
    smem_size);
    #endif
    if (IS_ERR_OR_NULL(smem_addr)) {
        pr_debug("%s: get mdmfeature failure\n", __func__);
        return false;
    }
    memcpy(buf, smem_addr, *smem_size);
    return true;
}

static ssize_t mdmfeature_write(struct file *file, const char __user * buf,
                size_t count, loff_t * ppos)
{
        /*just for test*/
        if (count > SMEM_OPLUS_FEATURE_MAP_SZ) {
                count = SMEM_OPLUS_FEATURE_MAP_SZ;
        }
        if (count > *ppos) {
                count -= *ppos;
        }
        else
                count = 0;

        printk("mdmfeature_write is called\n");

        if (copy_from_user(mdmfeature_buf, buf, count)) {
                return -EFAULT;
        }
        if (!write_mdmfeature_to_smem(mdmfeature_buf, count)) {
                return -EFAULT;
        }
        *ppos += count;

        mdmfeature_flag = 1;
        wake_up_interruptible(&mdmfeature_wq);

        return count;
}


static unsigned int mdmfeature_poll (struct file *file, struct poll_table_struct *pt)
{
        unsigned int ptr = 0;

        poll_wait(file, &mdmfeature_wq, pt);

        if (mdmfeature_flag) {
                ptr |= POLLIN | POLLRDNORM;
                mdmfeature_flag = 0;
        }
        return ptr;
}

static ssize_t mdmfeature_read(struct file *file, char __user *buf,
                size_t count, loff_t *ppos)
{
        size_t size = 0;

        if (count > SMEM_OPLUS_FEATURE_MAP_SZ) {
                count = SMEM_OPLUS_FEATURE_MAP_SZ;
        }
        size = count < strlen(mdmfeature_buf) ? count : strlen(mdmfeature_buf);

        if (size > *ppos) {
                size -= *ppos;
        }
        else
                size = 0;

        if (copy_to_user(buf, mdmfeature_buf, size)) {
                return -EFAULT;
        }
        if (!read_mdmfeature_from_smem(mdmfeature_buf, &size)) {
                return -EFAULT;
        }
        /*mdmfeature_flag = 0;*/
        *ppos += size;

        return size;
}

static int mdmfeature_release (struct inode *inode, struct file *file)
{
        /*mdmfeature_flag = 0;*/
        /*memset(mdmfeature_buf, 0, SMEM_OPLUS_FEATURE_MAP_SZ);*/
        return 0;
}

#define MDMFEATURE_UPDATE_MAGIC 'M'
#define MDMFEATURE_UPDATE_DSDS _IOWR(MDMFEATURE_UPDATE_MAGIC, 0, int)
#define MDMFEATURE_UPDATE_SSSS _IOWR(MDMFEATURE_UPDATE_MAGIC, 1, int)

struct proc_dir_entry *manifest_parent = NULL;

static void update_manifest(unsigned int update_type)
{
        static const char *telephony_manifest_src[2] = {
                "/vendor/odm/etc/vintf/network_manifest_ssss.xml",
                "/vendor/odm/etc/vintf/network_manifest_dsds.xml",
        };

        mm_segment_t fs;
        static struct proc_dir_entry *telephony_manifest_ssss = NULL;
        static struct proc_dir_entry *telephony_manifest_dsds = NULL;

        printk("update_manifest update_type %d\n", update_type - MDMFEATURE_UPDATE_DSDS);
        fs = get_fs();
        set_fs(KERNEL_DS);

        if (manifest_parent) {
                if (telephony_manifest_ssss) {
                        proc_remove(telephony_manifest_ssss);
                        telephony_manifest_ssss = NULL;
                }
                if (telephony_manifest_dsds) {
                        proc_remove(telephony_manifest_dsds);
                        telephony_manifest_dsds = NULL;
                }

                if (update_type == MDMFEATURE_UPDATE_SSSS) {
                        telephony_manifest_ssss = proc_symlink("network_manifest", manifest_parent, telephony_manifest_src[0]);
                } else {
                        telephony_manifest_dsds = proc_symlink("network_manifest", manifest_parent, telephony_manifest_src[1]);
                }
        }

        set_fs(fs);
        printk("update_manifest update_type %d done\n", update_type - MDMFEATURE_UPDATE_DSDS);
}


static long mdmfeature_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        long ret = 0;

        printk("mdmfeature_ioctl cmd %d\n", cmd - MDMFEATURE_UPDATE_DSDS);

        switch (cmd) {
        case MDMFEATURE_UPDATE_SSSS:
        case MDMFEATURE_UPDATE_DSDS:
                update_manifest(cmd);
                break;
        default:
                ret = -EINVAL;
        }
        return ret;
}


static const struct file_operations mdmfeature_device_fops = {
        .owner  = THIS_MODULE,
        .read   = mdmfeature_read,
        .write        = mdmfeature_write,
        .poll        = mdmfeature_poll,
        .llseek = generic_file_llseek,
        .release = mdmfeature_release,
        .unlocked_ioctl = mdmfeature_ioctl,
};

static struct miscdevice mdmfeature_device = {
        MISC_DYNAMIC_MINOR, "mdmfeature", &mdmfeature_device_fops
};


#if IS_MODULE(CONFIG_OPLUS_FEATURE_SIMCARDNUM)
extern char sim_card_num[];
#endif
static int __init mdmfeature_init(void)
{
        int ret = -1;
        manifest_parent = proc_mkdir("oplusManifest", NULL);
#if IS_MODULE(CONFIG_OPLUS_FEATURE_SIMCARDNUM)
        if (sim_card_num[0] == '0') {
                update_manifest(MDMFEATURE_UPDATE_SSSS);
        } else {
                update_manifest(MDMFEATURE_UPDATE_DSDS);
        }
#else
        update_manifest(MDMFEATURE_UPDATE_DSDS);
#endif
        init_waitqueue_head(&mdmfeature_wq);
        ret = misc_register(&mdmfeature_device);
        printk("mdmfeature_init ret = %d done\n", ret);
        return ret;
}

static void __exit mdmfeature_exit(void)
{
        proc_remove(manifest_parent);
        misc_deregister(&mdmfeature_device);
}

module_init(mdmfeature_init);
module_exit(mdmfeature_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oplus");
//#endif /*OPLUS_FEATURE_MDMFEATURE*/
