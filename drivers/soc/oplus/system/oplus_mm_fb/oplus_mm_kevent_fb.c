// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <soc/oplus/system/oplus_mm_kevent_fb.h>
#include "oplus_mm_kevent.h"
#include <linux/version.h>

static void mm_fb_kevent_upload_jobs(struct work_struct *work);

static LIST_HEAD(mm_kevent_list);
static DEFINE_MUTEX(mm_kevent_lock);
static DECLARE_DELAYED_WORK(mm_kevent_upload_work_thread,
			    mm_fb_kevent_upload_jobs);
static struct workqueue_struct *mm_kevent_wq = NULL;
static int mm_kevent_len = 0;
static bool mm_fb_init = false;
#define CAUSENAME_SIZE 128
static char fid[CAUSENAME_SIZE]={"12345678"};

#define LIMIT_UPLOAD_TIME_MS    10000 /*ms*/
struct limit_upload_frq {
	unsigned int last_id;
	ktime_t last_time;
};
static struct limit_upload_frq g_limit;

struct mm_kevent {
	struct list_head head;
	enum OPLUS_MM_DIRVER_FB_EVENT_MODULE module;
	unsigned int event_id;
	struct mutex lock;
	int count;
	int count_total;
	u32 count_limit;
	int rate_limit_ms;
	ktime_t first;
	ktime_t last;
	ktime_t last_upload;
	struct delayed_work dwork;
	char *payload;
	char name[0];
};

#define RELATION_EVENT_LIMIT_NUM    6
struct relation_event_limit {
	unsigned int fst_id;
	ktime_t fst_time;
	unsigned int limit_ms;
	unsigned int limit_id[RELATION_EVENT_LIMIT_NUM];
};

static struct relation_event_limit g_relate[] = {
	{10001, 0, 2000, {10003, 10008, 10041, 10042, 10046, 10047}},
	{10003, 0, 1000, {10008, 10046, 0, 0, 0, 0}},
	{10046, 0, 1000, {10008, 0, 0, 0, 0, 0}}
};

static void record_relation_first_event(unsigned int id)
{
	int lp = 0;

	for (lp = 0; lp < sizeof(g_relate)/sizeof(struct relation_event_limit); lp++) {
		if (id == g_relate[lp].fst_id) {
			g_relate[lp].fst_time = ktime_get();
		}
	}
}

static bool is_relation_event_limit(unsigned int id)
{
	int lp = 0;
	int offset = 0;

	for (lp = 0; lp < sizeof(g_relate)/sizeof(struct relation_event_limit); lp++) {
		for (offset = 0; offset < RELATION_EVENT_LIMIT_NUM; offset++) {
			if ((id == g_relate[lp].limit_id[offset]) && \
					(g_relate[lp].fst_time != 0) && \
					ktime_before(ktime_get(), \
					ktime_add_ms(g_relate[lp].fst_time, g_relate[lp].limit_ms))) {
				return true;
			} else if (0 == g_relate[lp].limit_id[offset]) {
				break;
			}
		}
	}

	return false;
}

static unsigned int BKDRHash(char *str, unsigned int len)
{
	unsigned int seed = 131; /* 31 131 1313 13131 131313 etc.. */
	unsigned int hash = 0;
	unsigned int i    = 0;

	if (str == NULL) {
		return 0;
	}

	for(i = 0; i < len; str++, i++) {
		hash = (hash * seed) + (*str);
	}

	return hash;
}

static void calc_fid(unsigned char *str)
{
	char strHashSource[MAX_PAYLOAD_DATASIZE] = {0x00};
	unsigned int hashid = 0;
	/*struct timespec64 ts64;*/
	unsigned long rdm = 0;
	ktime_t t;

	t = ktime_get();
	rdm = get_random_u64();
	snprintf(strHashSource, MAX_PAYLOAD_DATASIZE, "%lu %lu %s", rdm, t, str);
	hashid = BKDRHash(strHashSource, strlen(strHashSource));
	memset(fid, 0 , CAUSENAME_SIZE);
	snprintf(fid, CAUSENAME_SIZE, "%u", hashid);
	printk(KERN_INFO "calc_fid: fid=%u\n", hashid);
}

static int upload_mm_fb_kevent(unsigned int event_id, unsigned char *payload)
{
	struct mm_kevent_packet *user_msg_info;
	char event_id_str[MAX_PAYLOAD_EVENTID] = {0};
	void *buffer = NULL;
	int len, size;
	int ret = 0;

	printk(KERN_INFO "%s: mm_kevent fb: enter, event_id = %d\n", __func__,
	       event_id);

	mutex_lock(&mm_kevent_lock);
	len = strlen(payload);
	if (len > MAX_PAYLOAD_DATASIZE) {
		printk(KERN_INFO "%s: error: payload len=%d > %d\n",
		        __func__, len, MAX_PAYLOAD_DATASIZE);
		ret = -1;
		goto _exit;
	}
	size = sizeof(struct mm_kevent_packet) + len + 1;

	buffer = kmalloc(size, GFP_ATOMIC);
	if (!buffer) {
		printk(KERN_INFO "%s: kmalloc %d bytes failed\n", __func__, size);
		ret = -1;
		goto _exit;
	}
	memset(buffer, 0, size);
	user_msg_info = (struct mm_kevent_packet *)buffer;
	user_msg_info->type = 1;

	memcpy(user_msg_info->tag, ATLAS_FB_EVENT, sizeof(ATLAS_FB_EVENT));

	snprintf(event_id_str, sizeof(event_id_str) - 1, "%d", event_id);
	memcpy(user_msg_info->event_id, event_id_str, strlen(event_id_str));

	user_msg_info->len = len + 1;
	memcpy(user_msg_info->data, payload, len + 1);

	mm_fb_kevent_send_to_user(user_msg_info);

	kfree(buffer);

	record_relation_first_event(event_id);

_exit:
	mutex_unlock(&mm_kevent_lock);
	return ret;
}


static void mm_fb_kevent_upload_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mm_kevent *new_kevent = container_of(dwork, struct mm_kevent, dwork);
	struct mm_kevent *kevent = NULL, *n = NULL;
	bool found = false;
	int cnt = 0;

	list_for_each_entry_safe(kevent, n, &mm_kevent_list, head) {
		if (!strcmp(kevent->name, new_kevent->name)) {
			found = true;
			break;
		}
	}

	if (!found) {
		if (mm_kevent_len > 200) {
			unsigned char payload[MAX_PAYLOAD_DATASIZE] = "";
			pr_err("mm_kevent large than 200");

			if (OPLUS_MM_DIRVER_FB_EVENT_AUDIO == new_kevent->module) {
				#define PAYLOAD(fmt, ...) \
					if (sizeof(payload) > cnt) \
					cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, fmt, ##__VA_ARGS__);
				PAYLOAD("func@@%s$$", new_kevent->name);
				PAYLOAD("%s", new_kevent->payload ? new_kevent->payload : "NULL");
			} else {
				scnprintf(payload, sizeof(payload), "MSG@@%s", new_kevent->name);
			}
			upload_mm_fb_kevent(new_kevent->event_id, payload);
			goto done;
		}

		kevent = new_kevent;
		kevent->count = 1;
		kevent->count_total = 1;
		new_kevent = NULL;
		mm_kevent_len++;
		list_add_tail(&kevent->head, &mm_kevent_list);
		goto done;
	}

	if (WARN_ON(!kevent)) {
		goto done;
	}

	mutex_lock(&kevent->lock);
	kevent->count++;
	kevent->count_total++;
	kevent->last = new_kevent->first;
	kfree(kevent->payload);
	kevent->payload = new_kevent->payload;
	new_kevent->payload = NULL;
	mutex_unlock(&kevent->lock);
done:
	mm_fb_kevent_upload_jobs(NULL);
	if (new_kevent) {
		kfree(new_kevent->payload);
	}
	kfree(new_kevent);
}

static void mm_fb_kevent_upload_jobs(struct work_struct *work)
{
	struct mm_kevent *kevent = NULL, *n = NULL;
	unsigned char payload[MAX_PAYLOAD_DATASIZE] = "";
	int cnt;

	list_for_each_entry_safe(kevent, n, &mm_kevent_list, head) {
		if (ktime_before(kevent->last, kevent->last_upload)) {
			continue;
		}

		if (kevent->count_limit && (kevent->count_total % kevent->count_limit == 0)) {
			kevent->count_limit <<= 1;
			if (kevent->count_limit > 4096) {
				kevent->count_limit = 4096;
			}
		} else if (!kevent->rate_limit_ms || (kevent->rate_limit_ms &&
						      ktime_before(ktime_get(), ktime_add_ms(kevent->last_upload,
								      kevent->rate_limit_ms)))) {
			continue;
		}

		mutex_lock(&kevent->lock);
		cnt = 0;
		#define PAYLOAD(fmt, ...) \
			if (sizeof(payload) > cnt) \
				cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, fmt, ##__VA_ARGS__);
		if (OPLUS_MM_DIRVER_FB_EVENT_AUDIO == kevent->module) {
			PAYLOAD("func@@%s$$", kevent->name);
			PAYLOAD("CT@@%d$$", kevent->count);
			PAYLOAD("%s", kevent->payload ? kevent->payload : "NULL");
		} else {
			PAYLOAD("EventName@@%s$$", kevent->name);
			PAYLOAD("CT@@%d$$", kevent->count);
			PAYLOAD("FT@@%lu$$", ktime_to_ms(kevent->first) / 1000);
			PAYLOAD("ET@@%lu$$", ktime_to_ms(kevent->last) / 1000);
			PAYLOAD("MSG@@%s", kevent->payload ? kevent->payload : "NULL");
		}

		if (kevent->payload) {
			kfree(kevent->payload);
			kevent->payload = NULL;
		}

		kevent->count = 0;
		kevent->last_upload = ktime_get();
		mutex_unlock(&kevent->lock);
		upload_mm_fb_kevent(kevent->event_id, payload);
	}
	if (mm_kevent_wq) {
		mod_delayed_work(mm_kevent_wq, &mm_kevent_upload_work_thread, 60 * 60 * HZ);
	}
}

static void mm_fb_kevent_upload_recv_user(int type, int flags, char *data)
{
	printk(KERN_INFO "mm_kevent fb recv user type=0x%x, flags=0x%x, data=%s\n",
		type, flags, data);
	#ifdef OPLUS_NETLINK_MM_KEVENT_TEST
	if (flags & OPLUS_NETLINK_MM_DBG_LV2) {
		upload_mm_fb_kevent(OPLUS_MM_EVENTID_TEST_OR_DEBUG, data);
	}
	#endif
}

/* queue a  delaywork to upload the feedback info, can used in interrupt function or timeliness requirement place*/
int upload_mm_fb_kevent_limit(enum OPLUS_MM_DIRVER_FB_EVENT_MODULE module,
			      unsigned int event_id,
		     const char *name, int rate_limit_ms, unsigned int delay_s, char *payload)
{
	struct mm_kevent *kevent = NULL;
	int size;
	char buf[MAX_PAYLOAD_DATASIZE] = {0};

	if (!mm_fb_init || !mm_kevent_wq) {
		pr_err("%s: error: not init or mm_kevent_wq is null\n", __func__);
		return -EINVAL;
	}

	if(is_relation_event_limit(event_id)) {
		pr_info("%s: relation event has feedback before, not feedback %u\n", __func__, event_id);
		return -EINVAL;
	}
	size = strlen(name) + sizeof(*kevent) + 1;
	kevent = kzalloc(size, GFP_ATOMIC);
	if (!kevent) {
		return -ENOMEM;
	}

	kevent->module = module;
	kevent->event_id = event_id;
	kevent->count_limit = 1;
	kevent->last_upload = ktime_get();
	kevent->first = ktime_get();
	kevent->last = ktime_get();
	kevent->rate_limit_ms = rate_limit_ms;
	memcpy(kevent->name, name, strlen(name) + 1);
	if (OPLUS_AUDIO_EVENTID_ADSP_CRASH == event_id) {
		calc_fid(payload);
		scnprintf(buf, MAX_PAYLOAD_DATASIZE, "EventField@@%s$$%s",
				fid, payload ? payload : "NULL");
		kevent->payload = kmemdup(buf, strlen(buf) + 1, GFP_ATOMIC);
	} else {
		kevent->payload = kmemdup(payload, strlen(payload) + 1, GFP_ATOMIC);
	}
	mutex_init(&kevent->lock);
	INIT_DELAYED_WORK(&kevent->dwork, mm_fb_kevent_upload_work);
	if (delay_s > 0) {
		printk(KERN_INFO "%s:feedback delay %d second\n", __func__, delay_s);
		queue_delayed_work(mm_kevent_wq, &kevent->dwork, delay_s * HZ);
	} else {
		queue_delayed_work(mm_kevent_wq, &kevent->dwork, 0);
	}
	printk(KERN_INFO "%s:event_id=%d,payload:%s\n", __func__, event_id, payload);

	return 0;
}
EXPORT_SYMBOL(upload_mm_fb_kevent_limit);

/* upload the feedback info immediately, can't used in interrupt function */
int upload_mm_fb_kevent_to_atlas_limit(unsigned int event_id,
				       unsigned char *payload, int limit_ms)
{
	struct mm_kevent_packet *user_msg_info;
	char event_id_str[MAX_PAYLOAD_EVENTID] = {0};
	void *buffer = NULL;
	int len, size;
	int ret = 0;

	printk(KERN_INFO "mm_kevent fb:upload_mm_fb_kevent_to_atlas_limit enter\n");

	if (!mm_fb_init) {
		pr_err("%s: error, module not init\n", __func__);
		return -EINVAL;
	}

	if(is_relation_event_limit(event_id)) {
		pr_info("%s: relation event has feedback before, not feedback %u\n", __func__, event_id);
		return -EINVAL;
	}

	mutex_lock(&mm_kevent_lock);

	if ((limit_ms > 0) && (g_limit.last_id == event_id)) {
		if (ktime_before(ktime_get(), ktime_add_ms(g_limit.last_time, limit_ms))) {
			printk(KERN_INFO "upload event_id=%d failed, report too often, limit_ms=%d\n",
					event_id, limit_ms);
			ret = -1;
			goto _exit;
		}
	}

	len = strlen(payload);
	if (len > MAX_PAYLOAD_DATASIZE) {
		printk(KERN_INFO "error: payload len=%d > %d\n", len, MAX_PAYLOAD_DATASIZE);
		ret = -1;
		goto _exit;
	}
	size = sizeof(struct mm_kevent_packet) + len + 1;
	buffer = kmalloc(size, GFP_ATOMIC);
	if (!buffer) {
		printk(KERN_INFO "%s: kmalloc %d bytes failed\n", __func__, size);
		ret = -1;
		goto _exit;
	}
	memset(buffer, 0, size);
	user_msg_info = (struct mm_kevent_packet *)buffer;
	user_msg_info->type = 1;

	if ((MAX_PAYLOAD_TAG - 1) > strlen(ATLAS_FB_EVENT)) {
		memcpy(user_msg_info->tag, ATLAS_FB_EVENT, strlen(ATLAS_FB_EVENT));
		user_msg_info->tag[strlen(ATLAS_FB_EVENT)] = 0;
	}

	snprintf(event_id_str, sizeof(event_id_str) - 1, "%d", event_id);
	memcpy(user_msg_info->event_id, event_id_str, strlen(event_id_str));

	user_msg_info->len = len + 1;
	memcpy(user_msg_info->data, payload, len + 1);

	pr_info("%s: mm_kevent: type %d, tag=%s, event_id=%s, len=%zu, payload=%s\n",
		__func__,
		user_msg_info->type, user_msg_info->tag, user_msg_info->event_id,
		user_msg_info->len, user_msg_info->data);
	mm_fb_kevent_send_to_user(user_msg_info);
	kfree(buffer);
	g_limit.last_id = event_id;
	g_limit.last_time = ktime_get();
	record_relation_first_event(event_id);

_exit:
	mutex_unlock(&mm_kevent_lock);
	return ret;
}
EXPORT_SYMBOL(upload_mm_fb_kevent_to_atlas_limit);

#define MM_FB_EVENTID_LEN   5
#define MM_FB_HAL_LIMIT    (30*1000)
#define IS_DIGITAL(x) (((x) >= '0') && ((x) <= '9'))
static ssize_t mm_fb_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *lo)
{
	char *r_buf;
	unsigned int event_id = 0;
	int len, i;

	if (!mm_fb_init) {
		pr_err("%s: error, module not init\n", __func__);
		return -EINVAL;
	}

	r_buf = (char *)kzalloc(MAX_PAYLOAD_DATASIZE, GFP_KERNEL);
	if (!r_buf) {
		return count;
	}

	if (copy_from_user(r_buf, buf,
			   MAX_PAYLOAD_DATASIZE > count ? count : MAX_PAYLOAD_DATASIZE)) {
		goto exit;
	}

	r_buf[MAX_PAYLOAD_DATASIZE - 1] ='\0'; /*make sure last bype is eof*/
	len = strlen(r_buf);
	printk(KERN_INFO "%s: mm_kevent fb len=%d, data=%s\n", __func__, len, r_buf);
	if (len < (MM_FB_EVENTID_LEN + 2)) {
		printk(KERN_INFO "%s: mm_kevent fb data len=%d is error\n", __func__, len);
		goto exit;
	}

	for (i = 0; i < MM_FB_EVENTID_LEN; i++) {
		if (IS_DIGITAL(r_buf[i])) {
			event_id = event_id*10 + r_buf[i] - '0';
		} else {
			printk(KERN_INFO "%s: mm_kevent fb eventid is error, data=%s\n", __func__,
			       r_buf);
			goto exit;
		}
	}

	upload_mm_fb_kevent_to_atlas_limit(event_id, r_buf + MM_FB_EVENTID_LEN + 1,
					   MM_FB_HAL_LIMIT);

exit:
	kfree(r_buf);
	return count;
}

static ssize_t mm_fb_read(struct file *file,
				char __user *buf,
				size_t count,
				loff_t *ppos)
{
	if (!mm_fb_init) {
		pr_err("%s: error, module not init\n", __func__);
		return -EINVAL;
	}

	return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops mm_fb_fops = {
	.proc_write = mm_fb_write,
	.proc_read  = mm_fb_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations mm_fb_fops = {
	.write = mm_fb_write,
	.read  = mm_fb_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t adsp_crash_cause_read(struct file *file,
	char __user *buf,
	size_t count,
	loff_t *off)
{
	char page[CAUSENAME_SIZE] = {0x00};
	int len = 0;

	len = snprintf(page, sizeof(page) - 1, "%s", fid);
	len = simple_read_from_buffer(buf, count, off, page, strlen(page));
	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops adsp_crash_cause_fops = {
	.proc_read  = adsp_crash_cause_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations adsp_crash_cause_fops = {
	.read  = adsp_crash_cause_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

int mm_fb_kevent_init(void)
{
	struct proc_dir_entry *d_entry = NULL;
	int ret = 0;

	mm_kevent_wq = create_workqueue("mm_kevent");
	if (!mm_kevent_wq) {
		ret = -ENOMEM;
		goto failed_create_workqueue;
	}
	queue_delayed_work(mm_kevent_wq, &mm_kevent_upload_work_thread, 0);

	mm_fb_kevent_set_recv_user(mm_fb_kevent_upload_recv_user);

	g_limit.last_id = 0xFFFFFFFF;
	g_limit.last_time = 0;

	d_entry = proc_create_data("mm_fb", 0664, NULL, &mm_fb_fops, NULL);
	if (!d_entry) {
		pr_err("%s: failed to create node\n", __func__);
		ret = -ENODEV;
		goto failed_proc_create_data;
	}

	d_entry = proc_create_data("adsp_crash_cause", 0664, NULL,
				   &adsp_crash_cause_fops, NULL);
	if (!d_entry) {
		pr_err("failed to adsp_crash_cause node\n");
		ret = -ENODEV;
		goto failed_proc_create_data;
	}

	mm_fb_init = true;
	pr_info("%s: init success\n", __func__);

	return 0;

failed_proc_create_data:
	if (mm_kevent_wq) {
		destroy_workqueue(mm_kevent_wq);
		mm_kevent_wq = NULL;
	}
failed_create_workqueue:
	return ret;
}

void mm_fb_kevent_deinit(void)
{
	if (mm_kevent_wq) {
		destroy_workqueue(mm_kevent_wq);
		mm_kevent_wq = NULL;
	}

	mm_fb_kevent_set_recv_user(NULL);
	mm_fb_init = false;
	pr_info("%s: deinit\n", __func__);
}


module_init(mm_fb_kevent_init);
module_exit(mm_fb_kevent_deinit);

MODULE_LICENSE("GPL v2");

