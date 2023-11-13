// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** File : kerbel_fb.c
** Description : BSP kevent fb data
** Version : 1.0
******************************************************************/
#define pr_fmt(fmt) "<kernel_fb>" fmt

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
/*
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <../../../arch/arm64/kernel/secureguard/rootguard/oplus_kevent.h>
#else
#include <linux/oplus_kevent.h>
#endif
#endif
*/
#include <soc/oplus/system/kernel_fb.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <net/genetlink.h>
#include <linux/time64.h>
#include <linux/random.h>

#include "oplus_fb_guard_netlink.h"


#define MAX_LOG (31)
#define MAX_ID (19)
#define LIST_MAX (50)
#define DELAY_REPORT_US (30*1000*1000)
#define MAX_BUF_LEN (2048)
#define CAUSENAME_SIZE 128

struct packets_pool {
	struct list_head packets;
	struct task_struct *flush_task;
	spinlock_t wlock;
	bool wlock_init;
};

struct packet {
	struct kernel_packet_info *pkt;
	struct list_head list;
	int retry;
};

static struct packets_pool *g_pkts = NULL;

static char *const _tag[FB_MAX_TYPE + 1] = {
	"fb_stability",
	"fb_fs",
	"fb_storage",
	"PSW_BSP_SENSOR",
	"fb_boot",
};
static char fid[CAUSENAME_SIZE]={""};


static volatile unsigned int kevent_pid;

#define OPLUS_KEVENT_MAX_UP_PALOAD_LEN			2048
#define OPLUS_KEVENT_TEST_TAG				"test_event"
#define OPLUS_KEVENT_TEST_ID				"test_check"

static int fb_keventupload_sendpid_cmd(struct sk_buff *skb,
	struct genl_info *info)
{
	struct nlattr *na = NULL;
	unsigned int *p_data = NULL;

	pr_info(" kernel recv cmd \n");

	if (info->attrs[FB_GUARD_CMD_ATTR_MSG]) {
		na = info->attrs[FB_GUARD_CMD_ATTR_MSG];
		/*PRINT_FORMAT(nla_data(na),  nla_len(na));*/
		pr_info(" nla_len(na) is %d  \n", nla_len(na));
		p_data = nla_data(na);
		kevent_pid = *p_data;
		pr_info(" kevent_pid is 0x%x  \n", kevent_pid);
	}

	return 0;
}

static int fb_keventupload_test_upload(struct sk_buff *skb,
	struct genl_info *info)
{
	int ret = 0;
	struct nlattr *na = NULL;
	struct msg_test_upload *p_test_upload = NULL;
	struct kernel_packet_info *p_dcs_event = NULL;
	size_t data_len = 0;

	pr_info(" fb_keventupload_test_upload \n");

	if (info->attrs[FB_GUARD_CMD_ATTR_OPT]) {
		na = info->attrs[FB_GUARD_CMD_ATTR_OPT];
		/*PRINT_FORMAT(nla_data(na),  nla_len(na));*/
		pr_info(" nla_len(na) is %d  \n", nla_len(na));
		p_test_upload = (struct msg_test_upload *)nla_data(na);
		kevent_pid = p_test_upload->pro_pid;
		pr_info(" p_test_upload->pro_pid is %u, p_test_upload->val is %u, \n",
			p_test_upload->pro_pid, p_test_upload->val);


		if ((p_test_upload->val) > OPLUS_KEVENT_MAX_UP_PALOAD_LEN) {
			pr_err("[ERROR]:p_test_upload->val too long \n", p_test_upload->val);
			return -1;
		}

		data_len = p_test_upload->val + sizeof(struct kernel_packet_info);
		pr_info(" data_len is %u\n", data_len);
		p_dcs_event = (struct kernel_packet_info *)kmalloc(data_len, GFP_ATOMIC);

		if (NULL == p_dcs_event) {
			pr_err("[ERROR]:kmalloc for p_dcs_event err\n");
			return -1;
		}

		pr_info(" p_dcs_event kmalloc ok .\n");

		memset((unsigned char *)p_dcs_event, 0x00, data_len);
		p_dcs_event->type = 1;
		strncpy(p_dcs_event->log_tag, OPLUS_KEVENT_TEST_TAG,
			sizeof(p_dcs_event->log_tag));
		strncpy(p_dcs_event->event_id, OPLUS_KEVENT_TEST_ID,
			sizeof(p_dcs_event->event_id));
		p_dcs_event->payload_length = p_test_upload->val;
		memset(p_dcs_event->payload, 0xFF, p_test_upload->val);

		ret = fb_kevent_send_to_user(p_dcs_event);

		if (ret) {
			pr_err("[ERROR]:fb_kevent_send_to_user err, ret is %d \n", ret);
		}

		kfree(p_dcs_event);
	}

	return 0;
}

static const struct genl_ops oplus_fb_kevent_upload_ops[] = {
	{
		.cmd		= FB_GUARD_CMD_GENL_SENDPID,
		.doit		= fb_keventupload_sendpid_cmd,
		/*.policy		= taskstats_cmd_get_policy,*/
		/*.flags		= GENL_ADMIN_PERM,*/
	},
	{
		.cmd		= FB_GUARD_CMD_GENL_TEST_UPLOAD,
		.doit		= fb_keventupload_test_upload,
		/*.dumpit		= taskstats2_foreach,*/
		/*.policy		= taskstats_cmd_get_policy,*/
	},
};

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && !IS_ENABLED(CONFIG_OPLUS_KERNEL_SECURE_GUARD)
int kevent_send_to_user(struct kernel_packet_info *userinfo) {return 0;}
#endif
#endif

static struct genl_family oplus_fb_kevent_family __ro_after_init = {
	.name		= OPLUS_FB_GUARD_PROTOCAL_NAME,
	.version	= OPLUS_FB_GUARD_GENL_VERSION,
	.maxattr	= FB_GUARD_CMD_ATTR_MAX,
	.module		= THIS_MODULE,
	.ops		= oplus_fb_kevent_upload_ops,
	.n_ops		= ARRAY_SIZE(oplus_fb_kevent_upload_ops),
};

static inline int genl_msg_prepare_usr_msg(unsigned char cmd, size_t size,
	pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;

	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_KERNEL);

	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &oplus_fb_kevent_family, 0, cmd);

	*skbp = skb;
	return 0;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data,
	int len)
{
	int ret;

	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}

	return 0;
}

int fb_kevent_send_to_user(struct kernel_packet_info *userinfo)
{
	int ret = 0;
	struct sk_buff *skbuff = NULL;
	void *head = NULL;
	size_t data_len = 0;

	/*max_len */
	pr_info(" fb_kevent_send_to_user\n");

	if (userinfo->payload_length >= OPLUS_KEVENT_MAX_UP_PALOAD_LEN) {
		pr_err("[ERROR]:fb_kevent_send_to_user: payload_length out of range\n");
		ret = -1;
		return ret;
	}

	data_len = userinfo->payload_length + sizeof(struct kernel_packet_info);
	pr_info(" data_len is %u\n", data_len);

	ret = genl_msg_prepare_usr_msg(FB_GUARD_CMD_GENL_UPLOAD, data_len, kevent_pid,
			&skbuff);

	if (ret) {
		pr_err("[ERROR]:genl_msg_prepare_usr_msg err, ret is %d \n");
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, FB_GUARD_CMD_ATTR_MSG, userinfo, data_len);

	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));

	genlmsg_end(skbuff, head);

	ret = genlmsg_unicast(&init_net, skbuff, kevent_pid);

	if (ret < 0) {
		return ret;
	}

	return 0;
}

EXPORT_SYMBOL(fb_kevent_send_to_user);



/*
int kevent_send_to_user(struct kernel_packet_info *userinfo)
{
	return fb_kevent_send_to_user(userinfo);
}*/

static struct packet * package_alloc(
	fb_tag tag_id, const char *event_id, unsigned char *payload)
{
	struct packet *packet;
	struct kernel_packet_info *pkt;
	char *tmp = payload;

	if (tag_id > FB_MAX_TYPE || !event_id || !payload || !_tag[tag_id]) {
		return NULL;
        }

	packet = (struct packet *)kmalloc(sizeof(struct packet), GFP_ATOMIC);
	if (!packet) {
		return NULL;
	}

	/*presplit payload, '\n' is not allowed*/
	for(;*tmp != '\0'; tmp++) {
		if (*tmp == '\n') {
			*tmp = '\0';
			break;
		}
	}

	pkt = (struct kernel_packet_info *)
		kzalloc(sizeof(struct kernel_packet_info) + strlen(payload) + 1, GFP_ATOMIC);

	if (!pkt) {
		kfree(packet);
		return NULL;
	}

	packet->pkt = pkt;
	packet->retry = 4; /* retry 4 times at most*/
	pkt->type = 1; /*means only string is available*/

	memcpy(pkt->log_tag, _tag[tag_id], strlen(_tag[tag_id]) > MAX_LOG?MAX_LOG:strlen(_tag[tag_id]));
	memcpy(pkt->event_id, event_id, strlen(event_id) > MAX_ID?MAX_ID:strlen(event_id));
	pkt->payload_length = strlen(payload) + 1;
	memcpy(pkt->payload, payload, strlen(payload));

	return packet;
}

static void package_release(struct packet *packet)
{
	if (packet) {
		kfree(packet->pkt);
		kfree(packet);
	}
}

int oplus_kevent_fb(fb_tag tag_id, const char *event_id, unsigned char *payload)
{
	struct packet *packet;
	unsigned long flags;

	/*ignore before wlock init*/
	if (!g_pkts->wlock_init) {
		return -ENODEV;
        }

	packet = package_alloc(tag_id, event_id, payload);
	if (!packet) {
		return -ENODEV;
        }


	spin_lock_irqsave(&g_pkts->wlock, flags);
	list_add(&packet->list, &g_pkts->packets);
	spin_unlock_irqrestore(&g_pkts->wlock, flags);

	wake_up_process(g_pkts->flush_task);

	return 0;
}
EXPORT_SYMBOL(oplus_kevent_fb);



static unsigned int BKDRHash(char *str, unsigned int len)
{
	unsigned int seed = 131;
        /* 31 131 1313 13131 131313 etc.. */
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

int oplus_kevent_fb_str(fb_tag tag_id, const char *event_id, unsigned char *str)
{
	unsigned char payload[1024] = {0x00};
	unsigned int hashid = 0;
	int ret = 0;
	char strHashSource[CAUSENAME_SIZE] = {0x00};
/*
	unsigned long rdm = 0;
	rdm = get_random_u64();
	snprintf(strHashSource, CAUSENAME_SIZE, "%s %lu", str, rdm);
*/
	snprintf(strHashSource, CAUSENAME_SIZE, "%s", str);
	hashid = BKDRHash(strHashSource, strlen(strHashSource));
/*
	memset(fid, 0 , CAUSENAME_SIZE);
	snprintf(fid, CAUSENAME_SIZE, "%u", hashid);
*/
	ret = snprintf(payload, sizeof(payload),
			"NULL$$EventField@@%u$$FieldData@@%s$$detailData@@%s",  hashid, str,
			_tag[tag_id]);
	pr_err("%s, payload= %s, ret=%d\n", __func__, payload, ret);
	return oplus_kevent_fb(tag_id, event_id, payload);
}
EXPORT_SYMBOL(oplus_kevent_fb_str);

/*thread to deal with the buffer list*/
static int fb_flush_thread(void *arg)
{
	struct packets_pool *pkts_pool = (struct packets_pool *)arg;
	struct packet *tmp, *s;
	unsigned long flags;
	struct list_head list_tmp;

	while(!kthread_should_stop()) {
		if (list_empty(&pkts_pool->packets)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}

		set_current_state(TASK_RUNNING);

		spin_lock_irqsave(&pkts_pool->wlock, flags);
		INIT_LIST_HEAD(&list_tmp);
		list_splice_init(&pkts_pool->packets, &list_tmp);
		spin_unlock_irqrestore(&pkts_pool->wlock, flags);

		list_for_each_entry_safe(s, tmp, &list_tmp, list) {
			if (s->pkt) {
				if (fb_kevent_send_to_user(s->pkt) && s->retry) {
					pr_debug("failed to send feedback %s\n", s->pkt->log_tag);
					s->retry--;
					spin_lock_irqsave(&pkts_pool->wlock, flags);
					list_add(&s->list, &pkts_pool->packets);
					spin_unlock_irqrestore(&pkts_pool->wlock, flags);
				} else {
					package_release(s);
				}

				msleep(20);
			}
		}
	}

	return 0;
}

/*
* @format: tag_id:event_id:payload
*/
static ssize_t kernel_fb_write(struct file *file,
				const char __user *buf,
				size_t count,
				loff_t *lo)
{
	char *r_buf;
	int tag_id = 0;
	char event_id[MAX_ID] = {0};
	int idx1 = 0, idx2 = 0;
	int len;

	r_buf = (char *)kzalloc(MAX_BUF_LEN, GFP_KERNEL);
	if (!r_buf) {
		return count;
        }

	if (copy_from_user(r_buf, buf, MAX_BUF_LEN > count?count:MAX_BUF_LEN)) {
		goto exit;
	}

	r_buf[MAX_BUF_LEN - 1] ='\0'; /*make sure last bype is eof*/
	len = strlen(r_buf);

	tag_id = r_buf[0] - '0';
	if (tag_id > FB_MAX_TYPE || tag_id < 0) {
		goto exit;
	}

	while(idx1 < len) {
		if (r_buf[idx1++] == ':') {
			idx2 = idx1;
			while (idx2 < len) {
				if (r_buf[idx2++] == ':') {
					break;
                                }
			}
			break;
		}
	}

	if (idx1 == len || idx2 == len) {
		goto exit;
        }

	memcpy(event_id, &r_buf[idx1], idx2-idx1-1 > MAX_ID?MAX_ID: idx2-idx1-1);
	event_id[MAX_ID - 1] = '\0';

	oplus_kevent_fb(tag_id, event_id, r_buf + idx2);

exit:

	kfree(r_buf);
	return count;
}

static ssize_t kernel_fb_read(struct file *file,
				char __user *buf,
				size_t count,
				loff_t *ppos)
{
	return count;
}

static const struct file_operations kern_fb_fops = {
	.write = kernel_fb_write,
	.read  = kernel_fb_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t crash_cause_read(struct file *file,
    char __user *buf,
    size_t count,
    loff_t *off)
{
	char page[512] = {0x00};
	int len = 0;

	len = snprintf(page, sizeof(page), "%s", fid);
	len = simple_read_from_buffer(buf, count, off, page, strlen(page));
	return len;
}

static const struct file_operations crash_cause_fops = {
	.read  = crash_cause_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static int __init kernel_fb_init(void)
{
	struct proc_dir_entry *d_entry = NULL;
	int ret = 0;

	pr_err("%s\n", __func__);

	g_pkts = (struct packets_pool *)kzalloc(sizeof(struct packets_pool),
			GFP_KERNEL);

	if (!g_pkts) {
		ret = -ENOMEM;
		goto failed_kzalloc;
	}

	/*register gen_link family*/
	ret = genl_register_family(&oplus_fb_kevent_family);

	if (ret) {
		pr_err("failed genl_register_family");
		goto failed_genl_register_family;
	}

	INIT_LIST_HEAD(&g_pkts->packets);
	spin_lock_init(&g_pkts->wlock);

	g_pkts->flush_task = kthread_create(fb_flush_thread, g_pkts, "fb_flush");
	if (!g_pkts->flush_task) {
		pr_err("failed to kthread_create fb_flush\n");
		ret =  -ENODEV;
		goto failed_kthread_create;
	}

	g_pkts->wlock_init = true;

	d_entry = proc_create_data("kern_fb", 0664, NULL, &kern_fb_fops, NULL);
	if (!d_entry) {
		pr_err("failed to create kern_fb node\n");
		ret = -ENODEV;
		goto failed_proc_create_data;
	}

	d_entry = proc_create_data("crash_cause", 0664, NULL, &crash_cause_fops, NULL);

	if (!d_entry) {
		pr_err("failed to create crash_cause node\n");
		ret = -ENODEV;
		goto failed_proc_create_data;
	}

	pr_info("kernel_fb_init probe ok\n");
	return 0;

failed_proc_create_data:
failed_kthread_create:
	genl_unregister_family(&oplus_fb_kevent_family);
failed_genl_register_family:
	kfree(g_pkts->flush_task);
failed_kzalloc:
	return ret;
}

static void __exit kernel_fb_exit(void)
{
	genl_unregister_family(&oplus_fb_kevent_family);
	kfree(g_pkts->flush_task);
	return;
}

/*core_initcall(kernel_fb_init);*/

module_init(kernel_fb_init);
module_exit(kernel_fb_exit);
MODULE_LICENSE("GPL v2");
