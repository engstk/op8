/*
 *  linux/drivers/soc/oplus/oplus_midas/dispcap_dev.c
 *
 *      Added dispcap drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/cpufreq_times.h>
#include <linux/mutex.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/oplus_ion.h>

#ifdef OPLUS_FEATURE_MIDAS

#define DISPCAP_CTL_SET_CAPTURE_RECT 97
#define DISPCAP_CTL_SET_CAPTURE_INTERVAL 98
#define DISPCAP_CTL_ENABLE_CAPTURE 99
#define DISPCAP_CTL_SET_BUFFER 100
#define DISPCAP_CTL_WAIT_BUFFER_COMPLETE 101
#define DISPCAP_CTL_RET_SUCC 0
#define DISPCAP_CTL_RET_WAIT_TIMEOUT 1
#define DISPCAP_CTL_RET_INVALID -1
#define DISPCAP_CTL_RET_ERROR -2
#define DISPCAP_LOGD(...)
#define DISPCAP_LOGE pr_err

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
bool setCaptureRect(int left, int top, int width, int height)
{
	return false;
}
bool setCaptureInterval(int interval)
{
	return false;
}
bool enableCapture(int en)
{
	return false;
}
bool setUserBuffer(u8 *user_buffer)
{
	return false;
}
#else // #ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
// import from display driver
extern bool setCaptureRect(int left, int top, int width, int height);
extern bool setCaptureInterval(int interval);
extern bool enableCapture(int en);
extern bool setUserBuffer(u8 *user_buffer);
#endif // #ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM

// import from ion driver
typedef void (*fp_buffer_complete_notify)(void *user_buffer);
struct ion_handle;
extern struct ion_device *g_ion_device;
extern void * ion_map_kernel(struct ion_client *client, struct ion_handle *handle);
extern void ion_unmap_kernel(struct ion_client *client, struct ion_handle *handle);
extern bool setBufferCompleteNotifyCallback(fp_buffer_complete_notify cb);

struct disp_capture_rect
{
	int x;
	int y;
	int w;
	int h;
};

struct disp_driver_context
{
	struct ion_handle *dispcap_ion_handle;
};

struct disp_driver {
	dev_t dev;
	struct cdev cdev;
	struct class *dev_class;
	struct device *device;
	struct ion_client *ion_client;
	struct completion dispcap_cmp;
	struct mutex dispcap_lock;
	bool dispcap_used;
};

static struct disp_driver g_disp_driver;

void dispcap_notify_buffer_complete(void *result_buf) {
	DISPCAP_LOGD("start result_buf:%p\n", result_buf);
	complete(&g_disp_driver.dispcap_cmp);
	DISPCAP_LOGD("complete g_disp_driver.dispcap_cmp notify\n");
}
EXPORT_SYMBOL(dispcap_notify_buffer_complete);

static int driver_open(struct inode *inode, struct file *filp)
{
	struct disp_driver_context *context_ptr;

	DISPCAP_LOGD("start\n");

	mutex_lock(&g_disp_driver.dispcap_lock);

	if (g_disp_driver.dispcap_used) {
		DISPCAP_LOGD("dispcap is in used.\n");
		mutex_unlock(&g_disp_driver.dispcap_lock);
		return -1;
	}

	context_ptr = kmalloc(sizeof(struct disp_driver_context), GFP_KERNEL);
	if (NULL == context_ptr) {
		mutex_unlock(&g_disp_driver.dispcap_lock);
		return -ENOMEM;
	}

	context_ptr->dispcap_ion_handle = NULL;

	filp->private_data = context_ptr;

	g_disp_driver.dispcap_used = true;

	mutex_unlock(&g_disp_driver.dispcap_lock);

	return 0;
}

static int driver_release(struct inode *ignored, struct file *filp)
{
	struct disp_driver_context *context_ptr;

	DISPCAP_LOGD("start\n");

	mutex_lock(&g_disp_driver.dispcap_lock);

	context_ptr = filp->private_data;
	if (NULL != context_ptr) {

		// disable capture buffer and unmap
		setUserBuffer(NULL);
		if (NULL != context_ptr->dispcap_ion_handle) {
			ion_unmap_kernel(g_disp_driver.ion_client, context_ptr->dispcap_ion_handle);
			context_ptr->dispcap_ion_handle = NULL;
		}

		kfree(context_ptr);
		filp->private_data = NULL;
	}

	g_disp_driver.dispcap_used = false;

	mutex_unlock(&g_disp_driver.dispcap_lock);

	return 0;
}

static long driver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = -1;
	struct disp_driver_context *context_ptr = filp->private_data;

	DISPCAP_LOGD("start cmd:%d arg:%lu\n", cmd, arg);

	if (NULL == context_ptr) {
		DISPCAP_LOGE("context_ptr error!\n");
		return ret;
	}

	switch (cmd) {
		case DISPCAP_CTL_SET_CAPTURE_RECT: {
				struct disp_capture_rect rect;
				if(0 != copy_from_user(&rect, (void __user *)arg, sizeof(struct disp_capture_rect))) {
					DISPCAP_LOGE("DISPCAP_CTL_SET_CAPTURE_RECT arg error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				if(!setCaptureRect(rect.x, rect.y, rect.w, rect.h)) {
					DISPCAP_LOGE("setCaptureRect error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				DISPCAP_LOGD("setCaptureRect:%d %d %d %d\n", rect.x, rect.y, rect.w, rect.h);
				ret = DISPCAP_CTL_RET_SUCC;
			}
			break;
		case DISPCAP_CTL_SET_CAPTURE_INTERVAL: {
				int interval;
				if(0 != copy_from_user(&interval, (void __user *)arg, sizeof(int))) {
					DISPCAP_LOGE("DISPCAP_CTL_SET_CAPTURE_INTERVAL arg error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				if(!setCaptureInterval(interval)) {
					DISPCAP_LOGE("setCaptureInterval error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				DISPCAP_LOGD("setCaptureInterval:%d\n", interval);
				ret = DISPCAP_CTL_RET_SUCC;
			}
			break;
		case DISPCAP_CTL_ENABLE_CAPTURE: {
				int capture;
				if(0 != copy_from_user(&capture, (void __user *)arg, sizeof(int))) {
					DISPCAP_LOGE("DISPCAP_CTL_ENABLE_CAPTURE arg error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				if(!enableCapture(capture)) {
					DISPCAP_LOGE("setCaptureInterval error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				DISPCAP_LOGD("enableCapture:%d\n", capture);

				setBufferCompleteNotifyCallback(dispcap_notify_buffer_complete);
				DISPCAP_LOGD("setBufferCompleteNotifyCallback:%p\n", dispcap_notify_buffer_complete);

				ret = DISPCAP_CTL_RET_SUCC;
			}
			break;
		case DISPCAP_CTL_SET_BUFFER: {
				int ion_share_fd;
				struct dma_buf *cur_dma_buf;
				void * map_ion_va;

				if(0 != copy_from_user(&ion_share_fd, (void __user *)arg, sizeof(int))) {
					DISPCAP_LOGE("DISPCAP_CTL_SET_BUFFER arg error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}
				DISPCAP_LOGD("ion_share_fd:%d\n", ion_share_fd);

				cur_dma_buf = dma_buf_get(ion_share_fd);
				if (NULL == cur_dma_buf) {
					DISPCAP_LOGE("dma_buf_get failed!\n");
					ret = DISPCAP_CTL_RET_ERROR;
					break;
				}
				DISPCAP_LOGD("cur_dma_buf:%p\n", cur_dma_buf);
				DISPCAP_LOGD("cur_dma_buf size:%lu\n", cur_dma_buf->size);

				mutex_lock(&g_disp_driver.dispcap_lock);

				context_ptr->dispcap_ion_handle = ion_import_dma_buf_fd(g_disp_driver.ion_client, ion_share_fd);
				if (NULL == context_ptr->dispcap_ion_handle) {
					DISPCAP_LOGE("ion_import_dma_buf_fd failed!\n");
					ret = DISPCAP_CTL_RET_ERROR;
					mutex_unlock(&g_disp_driver.dispcap_lock);
					break;
				}
				DISPCAP_LOGD("ion_import_dma_buf_fd ion_handle:%p\n", context_ptr->dispcap_ion_handle);

				map_ion_va = ion_map_kernel(g_disp_driver.ion_client, context_ptr->dispcap_ion_handle);
				if (NULL == map_ion_va) {
					DISPCAP_LOGE("ion_map_kernel failed!\n");
					ret = DISPCAP_CTL_RET_ERROR;
					mutex_unlock(&g_disp_driver.dispcap_lock);
					break;
				}
				DISPCAP_LOGD("ion_map_kernel map_ion_va:%p\n", map_ion_va);

				DISPCAP_LOGD("reinit_completion\n");
				reinit_completion(&g_disp_driver.dispcap_cmp);

				DISPCAP_LOGD("setUserBuffer: %p\n", map_ion_va);
				setUserBuffer(map_ion_va);

				mutex_unlock(&g_disp_driver.dispcap_lock);

				ret = DISPCAP_CTL_RET_SUCC;
			}
			break;
		case DISPCAP_CTL_WAIT_BUFFER_COMPLETE: {
				int wait_ms;
				int wait_ret;
				if(0 != copy_from_user(&wait_ms, (void __user *)arg, sizeof(int))) {
					DISPCAP_LOGE("DISPCAP_CTL_WAIT_BUFFER_COMPLETE arg error!\n");
					ret = DISPCAP_CTL_RET_INVALID;
					break;
				}

				DISPCAP_LOGD("wait_for_completion_interruptible_timeout timeout:%dms begin ...\n", wait_ms);
				wait_ret = wait_for_completion_interruptible_timeout(&g_disp_driver.dispcap_cmp, msecs_to_jiffies(wait_ms));

				mutex_lock(&g_disp_driver.dispcap_lock);

				setUserBuffer(NULL);
				if (NULL != context_ptr->dispcap_ion_handle) {
					ion_unmap_kernel(g_disp_driver.ion_client, context_ptr->dispcap_ion_handle);
					context_ptr->dispcap_ion_handle = NULL;
				}

				mutex_unlock(&g_disp_driver.dispcap_lock);

				DISPCAP_LOGD("wait_for_completion_interruptible_timeout OK wait_ret:%d\n", wait_ret);
				if (0 == wait_ret) {
					ret = DISPCAP_CTL_RET_WAIT_TIMEOUT;
				} else if (0 < wait_ret) {
					ret = DISPCAP_CTL_RET_SUCC;
				} else {
					ret = DISPCAP_CTL_RET_INVALID;
				}
			}
			break;
		default: {
				DISPCAP_LOGE("unknown ioctl cmd:%d\n", cmd);
				ret = DISPCAP_CTL_RET_INVALID;
			}
			break;
	}

	return ret;
}

static struct file_operations io_dev_fops = {
	.owner = THIS_MODULE,
	.open = driver_open,
	.release = driver_release,
	.unlocked_ioctl = driver_ioctl,
};

int __init dispcap_dev_init(void)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	return 0;
#else
	int err = 0;

	DISPCAP_LOGD("start\n");

	g_disp_driver.dev_class = NULL;
	g_disp_driver.device = NULL;
	g_disp_driver.dev_class = NULL;
	g_disp_driver.ion_client = NULL;
	g_disp_driver.dispcap_used = false;
	mutex_init(&g_disp_driver.dispcap_lock);

	err = alloc_chrdev_region(&g_disp_driver.dev, 0, 1, "midas_dispcap");
	if (err < 0) {
		DISPCAP_LOGE("failed to alloc chrdev\n");
		goto fail;
	}

	cdev_init(&g_disp_driver.cdev, &io_dev_fops);

	err = cdev_add(&g_disp_driver.cdev, g_disp_driver.dev, 1);
	if (err < 0) {
		DISPCAP_LOGE("cdev_add g_disp_driver.cdev failed!\n");
		goto unreg_region;
	}

	g_disp_driver.dev_class = class_create(THIS_MODULE, "midas_dispcap_class");
	if (IS_ERR(g_disp_driver.dev_class)) {
		DISPCAP_LOGE("create class g_disp_driver.dev_class error\n");
		goto destroy_cdev;
	}

	g_disp_driver.device = device_create(g_disp_driver.dev_class, NULL, g_disp_driver.dev, NULL, "midas_dispcap");
	if (IS_ERR(g_disp_driver.device)) {
		DISPCAP_LOGE("device_create g_disp_driver.device error\n");
		goto destroy_class;
	}

	g_disp_driver.ion_client = ion_client_create(g_ion_device, "dispcap_ion_client");
	if (NULL == g_disp_driver.ion_client) {
		DISPCAP_LOGE("ion_client_create g_disp_driver.ion_client failed\n");
		goto destroy_device;
	}

	init_completion(&g_disp_driver.dispcap_cmp);

	return 0;

destroy_device:
	device_destroy(g_disp_driver.dev_class, g_disp_driver.dev);

destroy_class:
	class_destroy(g_disp_driver.dev_class);

destroy_cdev:
    cdev_del(&g_disp_driver.cdev);

unreg_region:
	unregister_chrdev_region(g_disp_driver.dev, 1);

fail:
    return -1;
#endif
}

void __exit dispcap_dev_exit(void)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#else
	DISPCAP_LOGD("start\n");

	if (NULL != g_disp_driver.ion_client) {
		ion_client_destroy(g_disp_driver.ion_client);
		g_disp_driver.ion_client = NULL;
	}

	if (g_disp_driver.dev_class) {
		device_destroy(g_disp_driver.dev_class, g_disp_driver.dev);
		class_destroy(g_disp_driver.dev_class);
		g_disp_driver.dev_class = NULL;
    }

    cdev_del(&g_disp_driver.cdev);

    unregister_chrdev_region(g_disp_driver.dev, 1);
#endif
}

#else // #ifdef OPLUS_FEATURE_MIDAS

int __init dispcap_dev_init(void) {
	return 0;
}

void __exit dispcap_dev_exit(void) {
	return;
}

#endif // #ifdef OPLUS_FEATURE_MIDAS