/************************************************************************************
** File: - oplus_qr.c
** Copyright (C), 2008-2020, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**          1. Add for QR scan
**
** Version: 1.0
** Date :   2022-01-19
** TAG	:   OPLUS_FEATURE_QR_SCAN
**
** ---------------------Revision History: ---------------------
**	<author>            <data>   <version >  <desc>
** ---------------------------------------------------------------
**
************************************************************************************/

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <linux/file.h>
#include <linux/netlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/spinlock.h>

#include <net/genetlink.h>
#include <net/route.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>

#define LOG_TAG "OPLUS_FEATURE_QR_SCAN"
#define _FAMILY_VERSION  1
#define OPLUS_QR_FAMILY_NAME "qr_scan"
#define NETLINK_OPLUS_QR_CMD_MAX (__NETLINK_OPLUS_QR_CMD_MAX - 1)
#define QR_MONITOR_FAMILY_VERSION	1
#define QR_MONITOR_FAMILY "qr_monitor"
#define NLA_DATA(na)		((char *)((char*)(na) + NLA_HDRLEN))
#define GENL_ID_GENERATE	0
#define QR_SCAN_MSG_MAX (__QR_SCAN_MSG_MAX - 1)

enum {
	QR_SCAN_MSG_UNDEFINE,
	QR_SET_ANDROID_PID,
	QR_SCAN_SET_PARAM,
	QR_SCAN_DETECTED,
	QR_SCAN_NO_CONTENT,
	QR_SCAN_NO_FOUND,
	QR_SCAN_POST_SUCCESS,
	QR_SCAN_BAD_GATEWAY,
	__QR_SCAN_MSG_MAX,
};

/*tcp dst and source*/
struct qr_ipv4_package_info {
	int ipv4_source;
	int ipv4_dst;
	int ipv4_saddr;
	int ipv4_daddr;
};

struct qr_ipv6_package_info {
	int ipv6_source;
	int ipv6_dst;
	struct in6_addr ipv6_saddr;
	struct in6_addr ipv6_daddr;
};

/*AP pass qr uid*/
static u32 qr_uid = 0;
/*qr package info,such as port and address*/
static struct qr_ipv4_package_info qr_ipv4_infos;
static struct qr_ipv6_package_info qr_ipv6_infos;
/*AP control if need to check tcp*/
static bool ap_control_need_check = false;
/*portid of android netlink socket*/
static u32 oplus_qr_netlink_pid;

enum {
	NETLINK_OPLUS_QR_CMD_UNSPEC,
	NETLINK_OPLUS_QR_HOOKS,
	__NETLINK_OPLUS_QR_CMD_MAX
};

static int qr_monitor_netlink_nlmsg_handle(struct sk_buff *skb, struct genl_info *info);
static const struct genl_ops qr_monitor_genl_ops[] = {
	{
		.cmd = NETLINK_OPLUS_QR_HOOKS,
		.flags = 0,
		.doit = qr_monitor_netlink_nlmsg_handle,
		.dumpit = NULL,
	},
};

static struct genl_family qr_monitor_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = QR_MONITOR_FAMILY,
	.version = QR_MONITOR_FAMILY_VERSION,
	.maxattr = QR_SCAN_MSG_MAX,
	.ops = qr_monitor_genl_ops,
	.n_ops = ARRAY_SIZE(qr_monitor_genl_ops),
};

static inline int qr_genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_KERNEL);
	if (skb == NULL) {
		return -ENOMEM;
	}
	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &qr_monitor_genl_family, 0, cmd);
	*skbp = skb;
	return 0;
}

static inline int qr_genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data, int len)
{
	int ret;
	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}
	return 0;
}

/* send to user space */
static int oplus_qr_hooks_send_to_user(int msg_type, char *payload, int payload_len)
{
	int ret = 0;
	void * head;
	struct sk_buff *skbuff;
	size_t size;

	if (!oplus_qr_netlink_pid) {
		printk("oplus_qr_monitor: qr_monitor_netlink_send_to_user, can not unicast skbuff, oplus_qr_netlink_pid=0\n");
		return -1;
	}

	/*allocate new buffer cache */
	size = nla_total_size(payload_len);
	ret = qr_genl_msg_prepare_usr_msg(NETLINK_OPLUS_QR_HOOKS, size, oplus_qr_netlink_pid, &skbuff);
	if (ret) {
		return ret;
	}

	ret = qr_genl_msg_mk_usr_msg(skbuff, msg_type, payload, payload_len);
	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, oplus_qr_netlink_pid);
	if (ret < 0) {
		printk(KERN_ERR "oplus_qr_monitor: qr_monitor_netlink_send_to_user, can not unicast skbuff, ret = %d,android_pid pid=%d\n", ret, oplus_qr_netlink_pid);
		return -1;
	}

	printk(" qr_monitor_netlink_send_to_user, skb_len=%u,android_pid pid=%d", skbuff->len, oplus_qr_netlink_pid);

	return 0;
}

/*to check if tcp uid is the qr sk*/
static bool oplus_match_qr_uid_skb(struct sock *sk)
{
	kuid_t check_uid;
	kuid_t sk_uid;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	const struct file *filp = NULL;
#endif

	if (NULL == sk || !sk_fullsock(sk) || NULL == sk->sk_socket) {
	return false;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	filp = sk->sk_socket->file;
	if (NULL == filp) {
		return false;
	}
	sk_uid = filp->f_cred->fsuid;
#else
	sk_uid = sk->sk_uid;
#endif
	check_uid = make_kuid(&init_user_ns, qr_uid);
	if (uid_eq(sk_uid, check_uid)) {
		return true;
	}

	return false;
}

/*to record qr tcp info*/
static void oplus_record_qr_ipv4_skb_info(int source, int dst, int saddr, int daddr)
{
	if ((qr_ipv4_infos.ipv4_source) || (qr_ipv4_infos.ipv4_dst) || (qr_ipv4_infos.ipv4_saddr) || (qr_ipv4_infos.ipv4_daddr)) {
		return;
	}

	qr_ipv4_infos.ipv4_source = source;
	qr_ipv4_infos.ipv4_dst = dst;
	qr_ipv4_infos.ipv4_saddr = saddr;
	qr_ipv4_infos.ipv4_daddr = daddr;
}

static void oplus_record_qr_ipv6_skb_info(int source, int dst, struct in6_addr saddr, struct in6_addr daddr)
{
	if ((qr_ipv6_infos.ipv6_source) || (qr_ipv6_infos.ipv6_dst)) {
		return;
	}

	if ((!ipv6_addr_any(&(qr_ipv6_infos.ipv6_saddr))) && (!ipv6_addr_any(&(qr_ipv6_infos.ipv6_daddr)))) {
		return;
	}

	qr_ipv6_infos.ipv6_source = source;
	qr_ipv6_infos.ipv6_dst = dst;
	qr_ipv6_infos.ipv6_saddr = saddr;
	qr_ipv6_infos.ipv6_daddr = daddr;
}

/*to check qr tcp info*/
static bool oplus_check_qr_ipv4_skb_info(int source, int dst, int saddr, int daddr)
{
	if ((!qr_ipv4_infos.ipv4_source) || (!qr_ipv4_infos.ipv4_dst) || (!qr_ipv4_infos.ipv4_saddr) || (!qr_ipv4_infos.ipv4_daddr)) {
		return false;
	}
	if ((source == qr_ipv4_infos.ipv4_source) && (dst == qr_ipv4_infos.ipv4_dst) && (saddr = qr_ipv4_infos.ipv4_saddr) && (daddr = qr_ipv4_infos.ipv4_daddr)) {
		return true;
	}

	return false;
}

static bool oplus_check_qr_ipv6_skb_info(int source, int dst, struct in6_addr saddr, struct in6_addr daddr)
{
	if ((!qr_ipv6_infos.ipv6_source) || (!qr_ipv6_infos.ipv6_dst)) {
		return false;
	}

	if ((ipv6_addr_any(&(qr_ipv6_infos.ipv6_saddr))) || (ipv6_addr_any(&(qr_ipv6_infos.ipv6_daddr)))) {
		return false;
	}

	if ((source == qr_ipv6_infos.ipv6_source) && (dst == qr_ipv6_infos.ipv6_dst)
		&& (ipv6_addr_equal(&(qr_ipv6_infos.ipv6_saddr), &saddr)) && (ipv6_addr_equal(&(qr_ipv6_infos.ipv6_daddr), &daddr))) {
		return true;
	}

	return false;
}

/*filter the skb about qr send*/
static unsigned int oplus_filter_qr_v4_send_skb(void *p, struct sk_buff *skb, const struct nf_hook_state *s)
{
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	u32 header_len;
	char *userdata = NULL;
	u16 tot_len;
	int length = 0;
	int ipv4_qr_source = 0;
	int ipv4_qr_dst = 0;
	int ipv4_qr_saddr = 0;
	int ipv4_qr_daddr = 0;

	struct sock *sk = skb_to_full_sk(skb);

	if (sk == NULL) {
		return NF_ACCEPT;
	}

	/*confirm qr doing scan now and this skb is the qr uid*/
	if ((!oplus_match_qr_uid_skb(sk)) || (!ap_control_need_check)) {
		return NF_ACCEPT;
	}

	/*get the skb userdata*/
	if ((iph = ip_hdr(skb)) != NULL && iph->protocol == IPPROTO_TCP) {
		tot_len = ntohs(iph->tot_len);

		if (unlikely(skb_linearize(skb))) {
			return NF_ACCEPT;
		}
		iph = ip_hdr(skb);
		tcph = tcp_hdr(skb);
		header_len = iph->ihl * 4 + tcph->doff * 4;
		userdata = (char *)(skb->data + header_len);
		length = skb->len-iph->ihl*4-tcph->doff*4;
		ipv4_qr_source = ntohs(tcph->source);
		ipv4_qr_dst = ntohs(tcph->dest);
		ipv4_qr_saddr = ntohs(iph->saddr);
		ipv4_qr_daddr = ntohs(iph->daddr);
	}

	if (userdata == NULL) {
		return NF_ACCEPT;
	}

	/*qr has send tcpv4 scan package*/
	if (strstr(userdata, "POST /mmtls/") != NULL) {
		oplus_record_qr_ipv4_skb_info(ipv4_qr_source, ipv4_qr_dst, ipv4_qr_saddr, ipv4_qr_daddr);
		oplus_qr_hooks_send_to_user(QR_SCAN_POST_SUCCESS, NULL, 0);
	}

	return NF_ACCEPT;
}

/*filter the skb about qr receive*/
static unsigned int oplus_filter_qr_v4_receive_skb(void *p, struct sk_buff *skb, const struct nf_hook_state *s)
{
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	u32 header_len;
	char *userdata = NULL;
	u16 tot_len;
	int length = 0;
	int ipv4_qr_source = 0;
	int ipv4_qr_dst = 0;
	int ipv4_qr_saddr = 0;
	int ipv4_qr_daddr = 0;
	struct sock *sk = skb_to_full_sk(skb);
	if (sk == NULL) {
		return NF_ACCEPT;
	}

	if ((!oplus_match_qr_uid_skb(sk)) || (!ap_control_need_check)) {
		return NF_ACCEPT;
	}

	if ((iph = ip_hdr(skb)) != NULL && iph->protocol == IPPROTO_TCP) {
		tot_len = ntohs(iph->tot_len);

		if (unlikely(skb_linearize(skb))) {
			return NF_ACCEPT;
		}
		iph = ip_hdr(skb);
		tcph = tcp_hdr(skb);
		header_len = iph->ihl * 4 + tcph->doff * 4;
		userdata = (char *)(skb->data + header_len);
		length = skb->len-iph->ihl*4-tcph->doff*4;
		ipv4_qr_source = ntohs(tcph->source);
		ipv4_qr_dst = ntohs(tcph->dest);
		ipv4_qr_saddr = ntohs(iph->saddr);
		ipv4_qr_daddr = ntohs(iph->daddr);
	}

	if (userdata == NULL) {
		return NF_ACCEPT;
	}

	if (strstr(userdata, "204 No Content") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_NO_CONTENT, NULL, 0);
		printk("qr 204 No Content");
		return NF_ACCEPT;
	}

	if (strstr(userdata, "Bad Gateway") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_BAD_GATEWAY, NULL, 0);
		printk("qr Bad Gateway");
		return NF_ACCEPT;
	}

	if (strstr(userdata, "Not Found") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_NO_FOUND, NULL, 0);
		printk("qr Not Found");
		return NF_ACCEPT;
	}

	if (!oplus_check_qr_ipv4_skb_info(ipv4_qr_dst, ipv4_qr_source, ipv4_qr_daddr, ipv4_qr_saddr)) {
		return NF_ACCEPT;
	}

	/*qr has received tcpv4 scan success package*/
	if (strstr(userdata, "200 OK")!= NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_DETECTED, NULL, 0);
	}

	return NF_ACCEPT;
}


static unsigned int oplus_filter_qr_v6_send_skb(void *p, struct sk_buff *skb, const struct nf_hook_state *s)
{
	struct ipv6hdr *ipv6h = NULL;
	struct tcphdr *tcph = NULL;

	u32 header_len;
	__be16 fo = 0;
	u8 ip_proto;
	int ihl = 0;
	char *userdata = NULL;
	u16 tot_len;
	int ipv6_qr_source = 0;
	int ipv6_qr_dst = 0;
	struct sock *sk = skb_to_full_sk(skb);
	if (sk == NULL) {
		return NF_ACCEPT;
	}
	if ((!oplus_match_qr_uid_skb(sk)) || (!ap_control_need_check)) {
		return NF_ACCEPT;
	}

	if (skb->protocol == htons(ETH_P_IPV6) && (ipv6h = ipv6_hdr(skb)) != NULL
		&& ipv6h->nexthdr == NEXTHDR_TCP) {
		tot_len = ntohs(ipv6h->payload_len);
		ip_proto = ipv6h->nexthdr;
		if (unlikely(skb_linearize(skb))) {
			return NF_ACCEPT;
		}
		ihl = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &ip_proto, &fo); /*ipv6 header length*/
		tcph = tcp_hdr(skb);
		if (NULL == tcph) {
			return NF_ACCEPT;
		}
		ipv6h = ipv6_hdr(skb);
		header_len = ihl + tcph->doff * 4;  /*total length of ipv6 header and tcp header*/
		userdata = (unsigned char *)(skb->data + header_len);   /*tcp payload buffer*/
		ipv6_qr_source = ntohs(tcph->source);
		ipv6_qr_dst = ntohs(tcph->dest);
		}

	if (userdata == NULL) {
		return NF_ACCEPT;
	}

	/*QR has send tcpv6 scan package*/
	if (strstr(userdata, "POST /mmtls/") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_POST_SUCCESS, NULL, 0);
		oplus_record_qr_ipv6_skb_info(ipv6_qr_source, ipv6_qr_dst, ipv6h->saddr, ipv6h->daddr);
	}

	return NF_ACCEPT;
}

static unsigned int oplus_filter_qr_v6_receive_skb(void *p, struct sk_buff *skb, const struct nf_hook_state *s)
{
	struct ipv6hdr *ipv6h = NULL;
	struct tcphdr *tcph = NULL;
	u32 header_len;
	__be16 fo = 0;
	u8 ip_proto;
	int ihl = 0;
	int ipv6_qr_source = 0;
	int ipv6_qr_dst = 0;
	char *userdata = NULL;
	u16 tot_len;
	struct sock *sk = skb_to_full_sk(skb);
	if (sk == NULL) {
		return NF_ACCEPT;
	}

	if ((!oplus_match_qr_uid_skb(sk)) || (!ap_control_need_check)) {
		return NF_ACCEPT;
	}

	if (skb->protocol == htons(ETH_P_IPV6) && (ipv6h = ipv6_hdr(skb)) != NULL
		&& ipv6h->nexthdr == NEXTHDR_TCP) {
		tot_len = ntohs(ipv6h->payload_len);
		ip_proto = ipv6h->nexthdr;
		if (unlikely(skb_linearize(skb))) {
			return NF_ACCEPT;
		}
		ihl = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &ip_proto, &fo); /*ipv6 header length*/
		tcph = tcp_hdr(skb);
		if (NULL == tcph) {
			return NF_ACCEPT;
		}
		ipv6h = ipv6_hdr(skb);
		header_len = ihl + tcph->doff * 4;  /*total length of ipv6 header and tcp header*/
		userdata = (unsigned char *)(skb->data + header_len);   /*tcp payload buffer*/
		ipv6_qr_source = ntohs(tcph->source);
		ipv6_qr_dst = ntohs(tcph->dest);
		}

	if (userdata == NULL) {
		return NF_ACCEPT;
	}

	if (strstr(userdata, "204 No Content") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_NO_CONTENT, NULL, 0);
		printk("qr 204 No Content");
		return NF_ACCEPT;
	}

	if (strstr(userdata, "Bad Gateway") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_BAD_GATEWAY, NULL, 0);
		printk("qr Bad Gateway");
		return NF_ACCEPT;
	}

	if (strstr(userdata, "Not Found") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_NO_FOUND, NULL, 0);
		printk("qr Not Found");
		return NF_ACCEPT;
	}

	if (!oplus_check_qr_ipv6_skb_info(ipv6_qr_dst, ipv6_qr_source, ipv6h->daddr, ipv6h->saddr)) {
		printk("oplus_filter_qr_v6_receive_skb info not match");
		return NF_ACCEPT;
	}

	if(strstr(userdata, "200 OK") != NULL) {
		oplus_qr_hooks_send_to_user(QR_SCAN_DETECTED, NULL, 0);
	}

	return NF_ACCEPT;
}

/*regist a hook function*/
static struct nf_hook_ops filter_qr_skb[] __read_mostly = {
	{
	.hook = oplus_filter_qr_v4_send_skb,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FILTER+2,
	},

	{
	.hook = oplus_filter_qr_v6_send_skb,
	.pf = NFPROTO_IPV6,
	.hooknum =  NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FILTER+2,
	},

	{
	.hook = oplus_filter_qr_v4_receive_skb,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FILTER+2,
	},

	{
	.hook = oplus_filter_qr_v6_receive_skb,
	.pf = NFPROTO_IPV6,
	.hooknum =  NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FILTER+2,
	},
};

static void oplus_qr_hooks_set_qr_param(struct  nlattr *nla)
{
	u32 *data = (u32 *)NLA_DATA(nla);
	qr_uid = data[0];
	ap_control_need_check = data[1];
	if (!ap_control_need_check) {
		qr_ipv6_infos.ipv6_source = 0;
		qr_ipv6_infos.ipv6_dst = 0;
		qr_ipv4_infos.ipv4_source = 0;
		qr_ipv4_infos.ipv4_dst = 0;
		ipv6_addr_set(&(qr_ipv6_infos.ipv6_saddr), 0, 0, 0, 0);
		ipv6_addr_set(&(qr_ipv6_infos.ipv6_daddr), 0, 0, 0, 0);
		qr_ipv4_infos.ipv4_saddr = 0;
		qr_ipv4_infos.ipv4_daddr = 0;
	}
}

static int oplus_qr_hooks_set_android_pid(struct sk_buff *skb)
{
	struct nlmsghdr *nlhdr = nlmsg_hdr(skb);
	oplus_qr_netlink_pid = nlhdr->nlmsg_pid;
	printk("oplus_qr_hooks_set_android_pid pid=%d\n", oplus_qr_netlink_pid);
	return 0;
}

/*receive ap data*/
static int qr_monitor_netlink_nlmsg_handle(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	switch (nla->nla_type) {
	case QR_SET_ANDROID_PID:
		ret = oplus_qr_hooks_set_android_pid(skb);
		break;
	case QR_SCAN_SET_PARAM:
		oplus_qr_hooks_set_qr_param(nla);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int oplus_qr_hooks_netlink_init(void)
{
	int ret;
	ret = genl_register_family(&qr_monitor_genl_family);
	if (ret) {
		printk("[QR_MONITOR]:genl_register_family:%s error,ret = %d\n", OPLUS_QR_FAMILY_NAME, ret);
		return ret;
	} else {
		printk("[QR_MONITOR]:genl_register_family complete, id = %d!\n", qr_monitor_genl_family.id);
	}

	return 0;
}

static void oplus_qr_hooks_netlink_exit(void)
{
	genl_unregister_family(&qr_monitor_genl_family);
}

static int __init oplus_qr_hook_init(void)
{
	int ret = 0;
	printk("oplus qr hook init");
	ret = oplus_qr_hooks_netlink_init();
	if (ret < 0) {
		printk("qr init netlink fail");
		return ret;
	} else {
		printk("qr module register netfilter ops successfully.\n");
	}
	ret = nf_register_net_hooks(&init_net, filter_qr_skb, ARRAY_SIZE(filter_qr_skb));
	if (ret < 0) {
		printk("oplus_qr_init netfilter register failed, ret=%d\n", ret);
		oplus_qr_hooks_netlink_exit();
		return ret;
	} else {
		printk("oplus_qr_init netfilter register successfully.\n");
	}
	return 0;
}

static void __exit oplus_qr_hook_fini(void)
{
	oplus_qr_hooks_netlink_exit();
	nf_unregister_net_hooks(&init_net, filter_qr_skb, ARRAY_SIZE(filter_qr_skb));
	printk("nf_hook_fini.");
}

MODULE_LICENSE("GPL");
module_init(oplus_qr_hook_init);
module_exit(oplus_qr_hook_fini);
