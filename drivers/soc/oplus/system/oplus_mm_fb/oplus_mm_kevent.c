// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include "oplus_mm_kevent.h"

#include <net/genetlink.h>
#include "oplus_mm_fb_netlink.h"

static struct mm_kevent_module mm_modules[MM_KEVENT_MODULE_SIZE_MAX];
static volatile bool mm_kevent_init_flag = false;
static spinlock_t mm_slock;
static mm_kevent_recv_user_func mm_fb_kevent_recv_fb = NULL;

/* record connect pid and modules*/
void mm_fb_kevent_add_module(u32 pid, char *module) {
	int i	= 0x0;
	int len = 0x0;

	if (!module) {
		return;
	}

	len = strlen(module);
	if (len > (MM_KEVENT_MODULE_LEN_MAX - 1)) {
		pr_err("mm_kevent: module len is larger than %d error\n", MM_KEVENT_MODULE_LEN_MAX);
		return;
	}

	for (i = 0; i < MM_KEVENT_MODULE_SIZE_MAX; i++) {
		if ((!mm_modules[i].pid) || (!strcmp(mm_modules[i].modl, module))) {
			spin_lock(&mm_slock);
			mm_modules[i].pid = pid;
			memcpy(mm_modules[i].modl, module, len);
			mm_modules[i].modl[len] = 0x0;
			spin_unlock(&mm_slock);
			return;
		}
	}

	return;
}

/* record connect pid and modules*/
int mm_fb_kevent_get_pid(char *module) {
	int i = 0;

	if (!module) {
		return MM_KEVENT_BAD_VALUE;
	}

	for (i = 0; i < MM_KEVENT_MODULE_SIZE_MAX; i++) {
		if (!strcmp(mm_modules[i].modl, module)) {
			return mm_modules[i].pid;
		}
	}

	return MM_KEVENT_BAD_VALUE;
}

static int mm_fb_kevent_send_module(struct sk_buff *skb,
	struct genl_info *info)
{
	struct sk_buff *skbu = NULL;
	struct nlmsghdr *nlh;
	struct nlattr *na = NULL;
	char *pmesg = NULL;

	pr_info("mm_kevent: mm_fb_kevent_send_module enter\n");

	if (!mm_kevent_init_flag) {
		pr_err("%s: mm_kevent: not init error\n", __func__);
		return -1;
	}

	skbu = skb_get(skb);
	if (!skbu) {
		pr_err("mm_kevent: skb_get result is null error\n");
		return -1;
	}

	if (info->attrs[MM_FB_CMD_ATTR_MSG]) {
		na = info->attrs[MM_FB_CMD_ATTR_MSG];
		nlh = nlmsg_hdr(skbu);
		pmesg = (char*)kmalloc(nla_len(na) + 0x10, GFP_KERNEL);
		if (pmesg) {
			memcpy(pmesg, nla_data(na), nla_len(na));
			pmesg[nla_len(na)] = 0x0;
			pr_info("mm_kevent: nla_len(na) %d, pid %d, module: %s\n",
					nla_len(na), nlh->nlmsg_pid, pmesg);
			mm_fb_kevent_add_module(nlh->nlmsg_pid, pmesg);
		}
	}

	if (pmesg) {
		kfree(pmesg);
	}
	if (skbu) {
		kfree_skb(skbu);
	}

	return 0;
}

void mm_fb_kevent_set_recv_user(mm_kevent_recv_user_func recv_func) {
	mm_fb_kevent_recv_fb = recv_func;
}

EXPORT_SYMBOL(mm_fb_kevent_set_recv_user);

static int mm_fb_kevent_test_upload(struct sk_buff *skb,
	struct genl_info *info)
{
	struct sk_buff *skbu = NULL;
	struct nlattr *na = NULL;
	char *pmesg = NULL;

	pr_info("mm_kevent: mm_fb_kevent_test_upload enter\n");

	if (!mm_kevent_init_flag) {
		pr_err("%s: mm_kevent: not init error\n", __func__);
		return -1;
	}

	skbu = skb_get(skb);
	if (!skbu) {
		pr_err("mm_kevent: skb_get result is null error\n");
		return -1;
	}

	if (info->attrs[MM_FB_CMD_ATTR_MSG]) {
		na = info->attrs[MM_FB_CMD_ATTR_MSG];
		pr_info("mm_kevent: nla_len(na) is %d, data= %s\n", nla_len(na), (char *)nla_data(na));

		if (nla_len(na) > OPLUS_MM_MSG_TO_KERNEL_BUF_LEN) {
			pr_err("mm_kevent: message len %d too long error\n", nla_len(na));
			return -1;
		}

		pmesg = (char*)kmalloc(nla_len(na) + 0x10, GFP_KERNEL);
		if (!pmesg) {
			pr_err("mm_kevent: kmalloc return null error\n");
			return -1;
		}
		memcpy(pmesg, nla_data(na), nla_len(na));
		pmesg[nla_len(na)] = 0x0;

		if (mm_fb_kevent_recv_fb) {
			mm_fb_kevent_recv_fb(0, OPLUS_NETLINK_MM_DBG_LV2, pmesg);
		}

		if (pmesg) {
			kfree(pmesg);
		}
	}

	if (skbu) {
		kfree_skb(skbu);
	}

	return 0;
}


#define MM_FB_FAMILY_VERSION	1
#define MM_FB_FAMILY "mm_fb"
#define GENL_ID_GENERATE	0

static const struct genl_ops mm_fb_genl_ops[] = {
	{
		.cmd		= MM_FB_CMD_GENL_SEND_MODULE,
		.doit		= mm_fb_kevent_send_module,
	},
	{
		.cmd		= MM_FB_CMD_GENL_TEST_UPLOAD,
		.doit		= mm_fb_kevent_test_upload,
	},
};

static struct genl_family mm_fb_genl_family __ro_after_init = {
	.id = GENL_ID_GENERATE,
	.name = MM_FB_FAMILY,
	.version = MM_FB_FAMILY_VERSION,
	.maxattr = MM_FB_CMD_ATTR_MAX,
	.module		= THIS_MODULE,
	.ops = mm_fb_genl_ops,
	.n_ops = ARRAY_SIZE(mm_fb_genl_ops),
};

static inline int genl_msg_prepare_usr_msg(unsigned char cmd, size_t size,
	pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;

	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_KERNEL);

	if (skb == NULL) {
		pr_err("mm_kevent: genlmsg_new failed\n");
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &mm_fb_genl_family, 0, cmd);

	*skbp = skb;
	return 0;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data,
	int len)
{
	int ret = 0;

	/* add a netlink attribute to a socket buffer */
	ret = nla_put(skb, type, len, data);

	return ret;
}

/* send to user space */
int mm_fb_kevent_send_to_user(struct mm_kevent_packet *userinfo) {
	int ret;
	int size_use;
	struct sk_buff *skbuff;
	void * head;
	int pid;

	if (!mm_kevent_init_flag) {
		pr_err("%s: mm_kevent: not init error\n", __func__);
		return MM_KEVENT_BAD_VALUE;
	}

	/* protect payload too long problem*/
	if (userinfo->len >= MAX_PAYLOAD_DATASIZE) {
		pr_err("mm_kevent: payload_length out of range error\n");
		return MM_KEVENT_BAD_VALUE;
	}

	pid = mm_fb_kevent_get_pid(userinfo->tag);
	if (pid == MM_KEVENT_BAD_VALUE) {
		pr_err("mm_kevent: tag=%s get pid error\n", userinfo->tag);
		return MM_KEVENT_BAD_VALUE;
	}

	size_use = sizeof(struct mm_kevent_packet) + userinfo->len;
	ret = genl_msg_prepare_usr_msg(MM_FB_CMD_GENL_UPLOAD, size_use, pid, &skbuff);
	if (ret) {
		pr_err("mm_kevent: genl_msg_prepare_usr_msg error, ret is %d \n", ret);
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, MM_FB_CMD_ATTR_MSG, userinfo, size_use);
	if (ret) {
		pr_err("mm_kevent: genl_msg_mk_usr_msg error, ret is %d \n", ret);
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	ret = genlmsg_unicast(&init_net, skbuff, pid);
	if (ret < 0) {
		pr_err("mm_kevent: genlmsg_unicast fail=%d \n", ret);
		return MM_KEVENT_BAD_VALUE;
	}

	return MM_KEVENT_NO_ERROR;
}
EXPORT_SYMBOL(mm_fb_kevent_send_to_user);

int __init mm_fb_kevent_module_init(void) {
	int ret;
	ret = genl_register_family(&mm_fb_genl_family);
	if (ret) {
		pr_err("mm_kevent: genl_register_family:%s error,ret = %d\n", MM_FB_FAMILY, ret);
		return ret;
	} else {
		pr_info("mm_kevent: genl_register_family complete, id = %d!\n", mm_fb_genl_family.id);
	}

	spin_lock_init(&mm_slock);
	memset(mm_modules, 0x0, sizeof(mm_modules));
	mm_kevent_init_flag = true;

	pr_info("mm_kevent: init ok\n");
	return MM_KEVENT_NO_ERROR;
}

void __exit mm_fb_kevent_module_exit(void) {
	genl_unregister_family(&mm_fb_genl_family);
	mm_kevent_init_flag = false;
	pr_info("mm_kevent: exit\n");
}

module_init(mm_fb_kevent_module_init);
module_exit(mm_fb_kevent_module_exit);

MODULE_DESCRIPTION("mm_kevent@1.0");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");

