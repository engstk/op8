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
#include "theia_kevent_kernel.h"

//should match with oplus_theia/include/TheiaKeventThread.h define
//if netlink.h not define, we define here
#ifndef OPLUS_NETLINK_THEIA_KEVENT
#define OPLUS_NETLINK_THEIA_KEVENT 43
#endif

static struct sock *theia_netlink_fd = NULL;
static u32 theia_kevent_pid = -1;
static DEFINE_MUTEX(theia_kevent_mutex);

#define THEIA_KEVENT_DEBUG_PRINTK(a, arg...)\
    do{\
         pr_err("[theia theia_kevent]: " a, ##arg);\
    }while(0)

/* kernel receive message from userspace */
void theia_kevent_receive_from_user(struct sk_buff *__skbbr) {
    struct sk_buff *skbu = NULL;
    struct nlmsghdr *nlh = NULL;
    char* pdata = NULL;
    uint32_t size = 0x0;

    THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_receive_from_user called\n");

    skbu = skb_get(__skbbr);

	if (skbu && skbu->len >= sizeof(struct nlmsghdr)) {
        nlh = (struct nlmsghdr *)skbu->data;
        if((nlh->nlmsg_len >= sizeof(struct nlmsghdr))
            && (__skbbr->len >= nlh->nlmsg_len)){
            size = (nlh->nlmsg_len - NLMSG_LENGTH(0));
            if (size > 0) {
                pdata = (char*)kmalloc(size + 0x10, GFP_KERNEL);
                if (pdata) {
                    memcpy(pdata, NLMSG_DATA(nlh), size);
                    pdata[size] = 0x0;

                    THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_receive_from_user, type:%d pdata:%s\n", nlh->nlmsg_type, pdata);
                    if ((nlh->nlmsg_type == THEIA_KEVENT_CONNECT) && (!strcmp(pdata, THEIA_KEVENT_MODULE))) {
                        theia_kevent_pid = nlh->nlmsg_pid;
                        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_receive_from_user, theia_kevent_pid is %u ..\n", theia_kevent_pid);
                    }
                }
            }
        }
    }

    if (pdata) {
        kfree(pdata);
    }

    if (skbu) {
        kfree_skb(skbu);
    }
}

/* kernel send message to userspace */
int theia_kevent_send_to_user(struct theia_kevent_packet *userinfo) {
    int ret, size, size_use;
    unsigned int o_tail;
    struct sk_buff *skbuff;
    struct nlmsghdr *nlh;
    struct theia_kevent_packet *packet;

    THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user called\n");

    if (theia_kevent_pid == -1) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user theia_kevent_pid is -1, return\n");
        return -1;
    }

    /* protect payload too long problem*/
    if (userinfo->len >= MAX_PAYLOAD_DATASIZE) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user payload_length out of range\n");
        return -1;
    }

    size = NLMSG_SPACE(sizeof(struct theia_kevent_packet) + userinfo->len);

    /*allocate new buffer cache */
    skbuff = alloc_skb(size, GFP_ATOMIC);
    if (skbuff == NULL) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user skbuff alloc_skb failed\n");
        return -1;
    }

    /* fill in the data structure */
    nlh = nlmsg_put(skbuff, 0, 0, 0, size - sizeof(*nlh), 0);
    if (nlh == NULL) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user nlmsg_put failaure\n");
        nlmsg_free(skbuff);
        return -1;
    }

    o_tail = skbuff->tail;
    size_use = sizeof(struct theia_kevent_packet) + userinfo->len;
    packet = NLMSG_DATA(nlh);
    memset(packet, 0, size_use);
    memcpy(packet, userinfo, size_use);
    nlh->nlmsg_len = skbuff->tail - o_tail;

    #if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
    NETLINK_CB(skbuff).pid = 0;
    #else
    NETLINK_CB(skbuff).portid = 0;
    #endif
    NETLINK_CB(skbuff).dst_group = 0;

    ret = netlink_unicast(theia_netlink_fd, skbuff, theia_kevent_pid, MSG_DONTWAIT);
    if (ret < 0) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent_send_to_user send fail=%d, theia_kevent_pid=%d \n", ret, theia_kevent_pid);
        return -1;
    }

    return 0;
}

//send msg to userspace
void SendTheiaKevent(int type, char *log_tag, char *event_id, char *payload) {
    struct theia_kevent_packet *user_msg_info;
    void *buffer = NULL;
    int len, size;

    THEIA_KEVENT_DEBUG_PRINTK("SendTheiaKevent begin\n");

    mutex_lock(&theia_kevent_mutex);

    //alloc memory
    len = strlen(payload);
    size = sizeof(struct theia_kevent_packet) + len + 1;
    buffer = kmalloc(size, GFP_ATOMIC);
    memset(buffer, 0, size);
    user_msg_info = (struct theia_kevent_packet *)buffer;

    //setup type
    user_msg_info->type = type;

    //setup tag
    memcpy(user_msg_info->tag, log_tag, strlen(log_tag) + 1);

    //setup event id
    memcpy(user_msg_info->event_id, event_id, strlen(event_id) + 1);

    //setup payload
    user_msg_info->len = len + 1;
    memcpy(user_msg_info->data, payload, len + 1);

    //send to userspace
    theia_kevent_send_to_user(user_msg_info);
    THEIA_KEVENT_DEBUG_PRINTK("SendTheiaKevent, theia_kevent_send_to_user user_msg_info->data:%s\n", user_msg_info->data);

    //msleep(20);
    kfree(buffer);
    mutex_unlock(&theia_kevent_mutex);
    return;
}

void SendDcsTheiaKevent(char *log_tag, char *event_id, char *logmap)
{
    SendTheiaKevent(THEIA_KEVENT_TYPE_DCS_MSG, log_tag, event_id, logmap);
}

int theia_kevent_module_init(void) {
    struct netlink_kernel_cfg cfg = {
        .groups = 0x0,
        .input = theia_kevent_receive_from_user,
    };

    theia_netlink_fd = netlink_kernel_create(&init_net, OPLUS_NETLINK_THEIA_KEVENT, &cfg);
    if (!theia_netlink_fd) {
        THEIA_KEVENT_DEBUG_PRINTK("theia_kevent can not create a socket\n");
        return -1;
    }

    THEIA_KEVENT_DEBUG_PRINTK("theia_kevent ok\n");
    return 0;
}

void theia_kevent_module_exit(void) {
    sock_release(theia_netlink_fd->sk_socket);
    THEIA_KEVENT_DEBUG_PRINTK("theia_kevent exit\n");
}
