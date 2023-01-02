// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include "jank_netlink.h"
#include "jank_base.h"

#define MAX_DATA_LEN 200
static __u32 recv_pid;

#ifndef USE_GEN_NETLINK
static struct sock *netlink_fd;

int send_to_user(int sock_no, size_t len, const int *data)
{
	int ret = -1;
	int i;
	int num = len + SEND_DATA_LEN;
	int size;
	int dt[MAX_DATA_LEN];
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;

	if (!data)
		return ret;

	if (IS_ERR_OR_NULL(netlink_fd))
		return ret;

	jank_dbg("recv_pid=%d, data=%d\n", recv_pid, data[0]);

	if (recv_pid <= 0)
		return ret;

	if (num > MAX_DATA_LEN) {
		pr_err("cpu_netlink send oversize(%d,MAX:%d) data!\n",
			num, MAX_DATA_LEN);
		return ret;
	}

	dt[0] = sock_no;
	dt[1] = len;

	/*lint -save -e440 -e679*/
	for (i = 0; i + SEND_DATA_LEN < num; i++)
		dt[i + SEND_DATA_LEN] = data[i];

	/*lint -restore*/
	size = sizeof(int) * num;

	skb = alloc_skb(NLMSG_SPACE(size), GFP_ATOMIC);
	if (IS_ERR_OR_NULL(skb)) {
		pr_err("cpu_netlink %s: alloc skb failed!\n",
			__func__);
		return -ENOMEM;
	}
	nlh = nlmsg_put(skb, 0, 0, 0,
		NLMSG_SPACE(size) - sizeof(struct nlmsghdr), 0);

	memcpy(NLMSG_DATA(nlh), (void *)dt, size);

	/* send up msg */
	ret = netlink_unicast(netlink_fd, skb, recv_pid, MSG_DONTWAIT);
	if (ret < 0)
		pr_err("cpu_netlink: netlink_unicast failed! errno = %d\n",
			ret);

	return ret;
}

static void recv_from_user(struct sk_buff *skb)
{
	struct sk_buff *tmp_skb = NULL;
	struct nlmsghdr *nlh = NULL;

	if (IS_ERR_OR_NULL(skb)) {
		pr_err("cpu_netlink: skb is NULL!\n");
		return;
	}

	tmp_skb = skb_get(skb);
	if (tmp_skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(tmp_skb);
		recv_pid = nlh->nlmsg_pid;
	}
}

void create_cpu_netlink(int socket_no)
{
	int ret = 0;
	struct netlink_kernel_cfg cfg = {
		.groups = 0,
		.input = recv_from_user,
	};

	netlink_fd = netlink_kernel_create(&init_net, socket_no, &cfg);
	if (IS_ERR_OR_NULL(netlink_fd)) {
		ret = PTR_ERR(netlink_fd);
		pr_err("cpu_netlink: create cpu netlink error! ret is %d\n", ret);
	}
}

void destroy_cpu_netlink(void)
{
	if (!IS_ERR_OR_NULL(netlink_fd) &&
		!IS_ERR_OR_NULL(netlink_fd->sk_socket)) {
		sock_release(netlink_fd->sk_socket);
		netlink_fd = NULL;
	}
}

#else

static struct genl_family oplus_cpuload_genl_family;

int send_to_user(int sock_no, size_t len, const int *data)
{
	int i;
	int ret = 0;
	void *head;
	size_t size;
	struct sk_buff *skb = NULL;
	int dt[MAX_DATA_LEN];
	int num = len + SEND_DATA_LEN;

	if (recv_pid <= 0)
		return ret;

	if (num > MAX_DATA_LEN) {
		pr_err("cpu_netlink: send oversize(%d,MAX:%d) data!\n",
			num, MAX_DATA_LEN);
		return -EINVAL;
	}

	dt[0] = sock_no;
	dt[1] = len;

	/* fill the data */
	for (i = 0; i + SEND_DATA_LEN < num; i++) {
		dt[i + SEND_DATA_LEN] = data[i];
		pr_info("cpu_netlink: send_to_user: %d\n", data[i]);
	}

	len = sizeof(int) * num;
	size = nla_total_size(len);

	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);
	if (IS_ERR_OR_NULL(skb)) {
		pr_err("cpu_netlink: new genlmsg alloc failed\n");
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, recv_pid, 0, &oplus_cpuload_genl_family,
				0, OPLUS_CPULOAD_CMD_SEND);

	/* add a netlink attribute to a socket buffer */
	if (nla_put(skb, OPLUS_CPULOAD_ATTR_MSG_GENL, len, &dt)) {
		pr_err("cpu_netlink: genl_msg_mk_usr_msg failed!\n");
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skb)));
	genlmsg_end(skb, head);

	/* send to user */
	ret = genlmsg_unicast(&init_net, skb, recv_pid);
	if (ret < 0) {
		pr_err("cpu_netlink: genlmsg_unicast failed! err = %d\n", ret);
		return ret;
	}

	return 0;
}

static int recv_from_user(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *tmp_skb = NULL;
	struct nlmsghdr *nlh = NULL;

	if (IS_ERR_OR_NULL(skb)) {
		pr_err("cpu_netlink: skb is NULL!\n");
		return -EINVAL;
	}

	tmp_skb = skb_get(skb);
	if (tmp_skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(tmp_skb);
		recv_pid = nlh->nlmsg_pid;
		pr_info("cpu_netlink: recv_from_user: %d\n", recv_pid);
	}

	return 0;
}

static const struct genl_ops oplus_cpuload_genl_ops[] = {
	{
		.cmd = OPLUS_CPULOAD_CMD_RECV,
		.flags = 0,
		.doit = recv_from_user,
		.dumpit = NULL,
	},
};

static struct genl_family oplus_cpuload_genl_family = {
	.id = OPLUS_CPULOAD_GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = OPLUS_CPULOAD_FAMILY_NAME,
	.version = OPLUS_CPULOAD_FAMILY_VER,
	.maxattr = OPLUS_CPULOAD_ATTR_MSG_MAX,
	.ops = oplus_cpuload_genl_ops,
	.n_ops = ARRAY_SIZE(oplus_cpuload_genl_ops),
	.module = THIS_MODULE,
};

void create_cpu_netlink(int unused)
{
	if (genl_register_family(&oplus_cpuload_genl_family) != 0)
		pr_err("cpu_netlink: genl_register_family error!\n", __func__);
}

void destroy_cpu_netlink(void)
{
	genl_unregister_family(&oplus_cpuload_genl_family);
}
#endif

