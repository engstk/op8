/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/netfilter_ipv4.h>

extern void statistics_monitor_apps_rtt_via_uid(int if_index, int rtt, struct sock *sk);
extern int apps_monitor_netlink_send_to_user(int msg_type, char *payload, int payload_len);
extern int app_monitor_dl_ctl_msg_handle(struct nlattr *nla);
extern int app_monitor_dl_report_msg_handle(struct nlattr *nla);
extern void oplus_app_power_monitor_init(void);
extern void oplus_app_power_monitor_fini(void);
extern void oplus_app_monitor_update_app_info(struct sock *sk, const struct sk_buff *skb, int send, int retrans);

#define IPV4ADDRTOSTR(addr) \
((unsigned char *)&addr)[0], \
((unsigned char *)&addr)[1], \
((unsigned char *)&addr)[2], \
((unsigned char *)&addr)[3]

#define MONITOR_APPS_NUM_MAX     (32)
#define IFACE_NUM_MAX            (2)
#define WLAN_INDEX               (0)
#define CELL_INDEX               (1)
#define ALL_IF_INDEX             (IFACE_NUM_MAX)

#define DEFAULT_RTT_EXCE_THRED  (100)
#define DEFAULT_RTT_GOOD_THRED  (150)
#define DEFAULT_RTT_FAIR_THRED  (200)
#define DEFAULT_RTT_POOR_THRED  (250)

#define APP_UID_IS_EMPTY         (-1)
#define TIMER_EXPIRES HZ

/*the dual sta feature, the wlan/cell index is changed in oplus_sla.c*/
#define DUAL_STA_WLAN0_INDEX     (0)
#define DUAL_STA_WLAN1_INDEX     (1)
#define DUAL_STA_CELL_INDEX      (2)

/*NLMSG_MIN_TYPE is 0x10,so we start at 0x11*/
enum {
	APPS_MONITOR_MSG_UNDEFINE,
	APPS_MONITOR_SET_ANDROID_PID,
	APPS_MONITOR_SET_APPS_UID,
	APPS_MONITOR_GET_APPS_CELL_RTT,
	APPS_MONITOR_GET_APPS_WLAN_RTT,
	APPS_MONITOR_GET_APPS_ALL_RTT,
	APPS_MONITOR_REPORT_APPS_CELL_RTT,
	APPS_MONITOR_REPORT_APPS_WLAN_RTT,
	APPS_MONITOR_REPORT_APPS_ALL_RTT,
	APPS_MOINTOR_SET_RTT_THRED,
	APPS_MONITOR_GET_DEV_RTT,
	APPS_MONITOR_REPORT_DEV_RTT,
	/*added for power monitor function*/
	APPS_POWER_MONITOR_MSG_DL_CTRL,
	APPS_POWER_MONITOR_MSG_DL_RPT_CTRL,
	APPS_POWER_MONITOR_MSG_UL_INFO,
	APPS_POWER_MONITOR_MSG_UL_BEAT_ALARM,
	APPS_POWER_MONITOR_MSG_UL_PUSH_ALARM,
	APPS_POWER_MONITOR_MSG_UL_TRAFFIC_ALARM,
	__APPS_MONITOR_MSG_MAX,
};
#define APPS_MONITOR_MSG_MAX (__APPS_MONITOR_MSG_MAX - 1)

typedef struct rtt_params {
	u64 rtt_exce_count;
	u64 rtt_good_count;
	u64 rtt_fair_count;
	u64 rtt_poor_count;
	u64 rtt_bad_count;
	u64 rtt_total_count;
} rtt_params;

typedef struct rtt_params_thred {
	u32 rtt_exce_thred;
	u32 rtt_good_thred;
	u32 rtt_fair_thred;
	u32 rtt_poor_thred;
} rtt_params_thred;

typedef struct monitor_app_params {
	int app_uid;
	rtt_params app_rtt[IFACE_NUM_MAX];
} monitor_app_params;

enum {
	APPS_MONITOR_CMD_UNSPEC,
	APPS_MONITOR_CMD_DOWNLINK,
	APPS_MONITOR_CMD_UPLINK,
	__APPS_MONITOR_CMD_MAX
};
#define APPS_MONITOR_CMD_MAX (__APPS_MONITOR_CMD_MAX - 1)

static int g_monitor_apps_num = 0;
static monitor_app_params g_monitor_apps_table[MONITOR_APPS_NUM_MAX];
static rtt_params g_monitor_dev_table[IFACE_NUM_MAX];
static rtt_params_thred g_rtt_params_thred = {DEFAULT_RTT_EXCE_THRED, DEFAULT_RTT_GOOD_THRED, DEFAULT_RTT_FAIR_THRED, DEFAULT_RTT_POOR_THRED};

static u32 apps_monitor_netlink_pid = 0;
static int apps_monitor_debug = 0;
static int rrt_period_report_enable = 1;
static int rrt_period_report_timer = 5; /* 5 sec */
static struct timer_list apps_monitor_timer;

static DEFINE_MUTEX(apps_monitor_netlink_mutex);
static struct ctl_table_header *apps_monitor_table_hrd;
static rwlock_t apps_monitor_lock;

#define apps_monitor_read_lock() 			read_lock_bh(&apps_monitor_lock);
#define apps_monitor_read_unlock() 			read_unlock_bh(&apps_monitor_lock);
#define apps_monitor_write_lock() 			write_lock_bh(&apps_monitor_lock);
#define apps_monitor_write_unlock()			write_unlock_bh(&apps_monitor_lock);

#define APPS_MONITOR_FAMILY_VERSION	1
#define APPS_MONITOR_FAMILY "apps_monitor"
#define NLA_DATA(na)		((char *)((char*)(na) + NLA_HDRLEN))
#define GENL_ID_GENERATE	0

static int apps_monitor_netlink_nlmsg_handle(struct sk_buff *skb, struct genl_info *info);
static const struct genl_ops apps_monitor_genl_ops[] = {
	{
		.cmd = APPS_MONITOR_CMD_DOWNLINK,
		.flags = 0,
		.doit = apps_monitor_netlink_nlmsg_handle,
		.dumpit = NULL,
	},
};

static struct genl_family apps_monitor_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = APPS_MONITOR_FAMILY,
	.version = APPS_MONITOR_FAMILY_VERSION,
	.maxattr = APPS_MONITOR_MSG_MAX,
	.ops = apps_monitor_genl_ops,
	.n_ops = ARRAY_SIZE(apps_monitor_genl_ops),
};

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);
	if (skb == NULL) {
		return -ENOMEM;
	}
	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &apps_monitor_genl_family, 0, cmd);
	*skbp = skb;
	return 0;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data, int len)
{
	int ret;
	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}
	return 0;
}

/* send to user space */
int apps_monitor_netlink_send_to_user(int msg_type, char *payload,
				      int payload_len)
{
	int ret = 0;
	void * head;
	struct sk_buff *skbuff;
	size_t size;

	if (!apps_monitor_netlink_pid) {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_to_user, can not unicast skbuff, apps_monitor_netlink_pid=0\n");
		return -1;
	}

	size = nla_total_size(payload_len);
	ret = genl_msg_prepare_usr_msg(APPS_MONITOR_CMD_UPLINK, size, apps_monitor_netlink_pid, &skbuff);
	if (ret) {
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, msg_type, payload, payload_len);
	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, apps_monitor_netlink_pid);

	if (ret < 0) {
		printk(KERN_ERR "oplus_apps_monitor: apps_monitor_netlink_send_to_user, can not unicast skbuff, ret = %d\n", ret);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(apps_monitor_netlink_send_to_user);

static struct ctl_table apps_monitor_sysctl_table[] = {
	{
		.procname	= "apps_monitor_debug",
		.data		= &apps_monitor_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rrt_period_report_enable",
		.data		= &rrt_period_report_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static void set_monitor_apps_param_default(void)
{
	int i = 0;
	int j = 0;

	for (i = 0; i < MONITOR_APPS_NUM_MAX; ++i) {
		g_monitor_apps_table[i].app_uid = APP_UID_IS_EMPTY;

		for (j = 0; j < IFACE_NUM_MAX; ++j) {
			g_monitor_apps_table[i].app_rtt[j].rtt_exce_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_fair_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_good_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_poor_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_bad_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_total_count = 0;
		}
	}
}

static void clear_apps_rtt_record(int if_index)
{
	int i = 0;
	int j = 0;

	switch (if_index) {
	case WLAN_INDEX:
		for (i = 0; i < g_monitor_apps_num; ++i) {
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_exce_count = 0;
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_fair_count = 0;
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_good_count = 0;
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_poor_count = 0;
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_bad_count = 0;
			g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_total_count = 0;
		}

		break;

	case CELL_INDEX:
		for (i = 0; i < g_monitor_apps_num; ++i) {
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_exce_count = 0;
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_fair_count = 0;
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_good_count = 0;
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_poor_count = 0;
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_bad_count = 0;
			g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_total_count = 0;
		}

		break;

	case ALL_IF_INDEX:
		for (i = 0; i < g_monitor_apps_num; ++i) {
			for (j = 0; j < IFACE_NUM_MAX; ++j) {
				g_monitor_apps_table[i].app_rtt[j].rtt_exce_count = 0;
				g_monitor_apps_table[i].app_rtt[j].rtt_fair_count = 0;
				g_monitor_apps_table[i].app_rtt[j].rtt_good_count = 0;
				g_monitor_apps_table[i].app_rtt[j].rtt_poor_count = 0;
				g_monitor_apps_table[i].app_rtt[j].rtt_bad_count = 0;
				g_monitor_apps_table[i].app_rtt[j].rtt_total_count = 0;
			}
		}

		break;

	default:
		break;
	}
}

static void clear_dev_rtt_record(void)
{
	int i = 0;

	for (i = 0; i < IFACE_NUM_MAX; ++i) {
		g_monitor_dev_table[i].rtt_exce_count = 0;
		g_monitor_dev_table[i].rtt_fair_count = 0;
		g_monitor_dev_table[i].rtt_good_count = 0;
		g_monitor_dev_table[i].rtt_poor_count = 0;
		g_monitor_dev_table[i].rtt_bad_count = 0;
		g_monitor_dev_table[i].rtt_total_count = 0;
	}
}

static int find_skb_uid_index_in_apps_record_table(struct sock *sk)
{
	int sk_uid;
	int index = 0;
	int uid_index = -1;

	if (NULL == sk || !sk_fullsock(sk) || NULL == sk->sk_socket) {
		return uid_index;
	}

	sk_uid = (int)sk->sk_uid.val;

	for (index = 0; index < g_monitor_apps_num; ++index) {
		if(sk_uid ==  g_monitor_apps_table[index].app_uid) {
			uid_index = index;
			break;
		}
	}

	return uid_index;
}

void update_dev_rtt_count(int if_index, int rtt)
{
	if (rtt < 0 || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

	if (rtt <= g_rtt_params_thred.rtt_exce_thred) {
		++(g_monitor_dev_table[if_index].rtt_exce_count);

	} else if (rtt <= g_rtt_params_thred.rtt_good_thred) {
		++(g_monitor_dev_table[if_index].rtt_good_count);

	} else if (rtt <= g_rtt_params_thred.rtt_fair_thred) {
		++(g_monitor_dev_table[if_index].rtt_fair_count);

	} else if (rtt <= g_rtt_params_thred.rtt_poor_thred) {
		++(g_monitor_dev_table[if_index].rtt_poor_count);

	} else {
		++(g_monitor_dev_table[if_index].rtt_bad_count);
	}

	++(g_monitor_dev_table[if_index].rtt_total_count);

	return;
}

void update_app_rtt_count(int if_index, int sk_uid_index, int rtt)
{
	if (rtt < 0 || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

	if (rtt <= g_rtt_params_thred.rtt_exce_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_exce_count);

	} else if (rtt <= g_rtt_params_thred.rtt_good_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_good_count);

	} else if (rtt <= g_rtt_params_thred.rtt_fair_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_fair_count);

	} else if (rtt <= g_rtt_params_thred.rtt_poor_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_poor_count);

	} else {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_bad_count);
	}

	++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_total_count);

	return;
}

void statistics_monitor_apps_rtt_via_uid(int if_index, int rtt, struct sock *sk)
{
	int sk_uid_index = -1;

	if (rtt <= 0 || sk == NULL || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

	apps_monitor_write_lock();
	update_dev_rtt_count(if_index, rtt);

	sk_uid_index = find_skb_uid_index_in_apps_record_table(sk);

	if (sk_uid_index < 0) {
		apps_monitor_write_unlock(); /*need release lock*/
		return;
	}

	update_app_rtt_count(if_index, sk_uid_index, rtt);
	apps_monitor_write_unlock();
}
EXPORT_SYMBOL(statistics_monitor_apps_rtt_via_uid);

static int apps_monitor_sysctl_init(void)
{
	apps_monitor_table_hrd = register_net_sysctl(&init_net,
				 "net/oplus_apps_monitor",
				 apps_monitor_sysctl_table);
	return apps_monitor_table_hrd == NULL ? -ENOMEM : 0;
}

static int apps_monitor_set_android_pid(struct sk_buff *skb)
{
	struct nlmsghdr *nlhdr = nlmsg_hdr(skb);
	apps_monitor_netlink_pid = nlhdr->nlmsg_pid;
	printk("oplus_apps_monitor: apps_monitor_netlink_set_android_pid pid=%d\n", apps_monitor_netlink_pid);
	return 0;
}

static int apps_monitor_set_apps_uid(struct nlattr *nla)
{
	int index = 0;
	/*u32 *uidInfo = (u32 *)NLMSG_DATA(nlh);*/
	u32 *uidInfo = (u32 *)NLA_DATA(nla);
	u32 apps_uid_num = uidInfo[0];
	u32 *apps_uid = &(uidInfo[1]);

	printk("oplus_apps_monitor: apps_monitor_set_apps_uid, uid_num = %u!\n", apps_uid_num);
	if (apps_uid_num >= MONITOR_APPS_NUM_MAX) {
		printk("oplus_apps_monitor: the input apps_uid_num is bigger than MONITOR_APPS_NUM_MAX! \n");
		return -EINVAL;
	}

	set_monitor_apps_param_default();
	g_monitor_apps_num = apps_uid_num;

	for (index = 0; index < apps_uid_num; ++index) {
		g_monitor_apps_table[index].app_uid = apps_uid[index];

		/* if (apps_monitor_debug) { */
		printk("oplus_apps_monitor:apps_monitor_netlink_set_apps_uid, g_monitor_apps_table[%d].app_uid=%d,num=%d\n",
				index, g_monitor_apps_table[index].app_uid, g_monitor_apps_num);
		/*}*/
	}

	return 0;
}

static int apps_monitor_set_rtt_thred(struct nlattr *nla)
{
	u32 *uidInfo = (u32 *)NLA_DATA(nla);
	u32 rtt_thred_num = uidInfo[0];
	u32 *rtt_thred = &(uidInfo[1]);

	if (rtt_thred_num > sizeof(rtt_params_thred) / sizeof(u32)) {
		printk("oplus_apps_monitor: the input rtt_thred_num is bigger than except! the input rtt_thred_num=  %d \n",
		       rtt_thred_num);
		return -EINVAL;
	}

	g_rtt_params_thred.rtt_exce_thred = rtt_thred[0];
	g_rtt_params_thred.rtt_good_thred = rtt_thred[1];
	g_rtt_params_thred.rtt_fair_thred = rtt_thred[2];
	g_rtt_params_thred.rtt_poor_thred = rtt_thred[3];

	return 0;
}

static void print_apps_rtt_record(void)
{
	int i = 0;
	int j = 0;

	for (i = 0; i < g_monitor_apps_num; ++i) {
		for (j = 0; j < IFACE_NUM_MAX; ++j) {
			printk("oplus_apps_monitor: print_apps_rtt_record, the uid = %d, the if_index = %d, RTT = %llu:%llu:%llu:%llu:%llu:%llu\n",
			       g_monitor_apps_table[i].app_uid,
			       j,
			       g_monitor_apps_table[i].app_rtt[j].rtt_exce_count,
			       g_monitor_apps_table[i].app_rtt[j].rtt_good_count,
			       g_monitor_apps_table[i].app_rtt[j].rtt_fair_count,
			       g_monitor_apps_table[i].app_rtt[j].rtt_poor_count,
			       g_monitor_apps_table[i].app_rtt[j].rtt_bad_count,
			       g_monitor_apps_table[i].app_rtt[j].rtt_total_count);
		}
	}
}

static void print_dev_rtt_record(void)
{
	int i = 0;

	for (i = 0; i < IFACE_NUM_MAX; ++i) {
		printk("oplus_apps_monitor: print_dev_rtt_record, the if_index = %d, RTT = %llu:%llu:%llu:%llu:%llu:%llu\n",
		       i,
		       g_monitor_dev_table[i].rtt_exce_count,
		       g_monitor_dev_table[i].rtt_good_count,
		       g_monitor_dev_table[i].rtt_fair_count,
		       g_monitor_dev_table[i].rtt_poor_count,
		       g_monitor_dev_table[i].rtt_bad_count,
		       g_monitor_dev_table[i].rtt_total_count);
	}
}

static int apps_monitor_report_apps_rtt_to_user(int if_index)
{
#define MAX_RTT_MSG_LEN (2048)
	int ret = 0;
	int index = 0;
	int step = 0;
	int rtt_params_size = sizeof(rtt_params);
	int int_size = sizeof(int);
	static char send_msg[MAX_RTT_MSG_LEN] = {0};

	memset(send_msg, 0, MAX_RTT_MSG_LEN);

	if ((rtt_params_size + int_size) * IFACE_NUM_MAX * g_monitor_apps_num >
			MAX_RTT_MSG_LEN) {
		printk("oplus_apps_monitor: apps_monitor_report_apps_rtt_to_user, the RTT Msg is too big, len = %0x \n",
		       (rtt_params_size + int_size) * g_monitor_apps_num);
		return -EINVAL;
	}

	switch (if_index) {
	case WLAN_INDEX:
		for (index = 0; index < g_monitor_apps_num; ++index) {
			step = (rtt_params_size * index)  + index * int_size;
			memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
			memcpy(send_msg + step + int_size,
			       &(g_monitor_apps_table[index].app_rtt[WLAN_INDEX]), rtt_params_size);
		}

		ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_WLAN_RTT,
							(char *) send_msg,
							(rtt_params_size + int_size) * g_monitor_apps_num);
		break;

	case CELL_INDEX:
		for (index = 0; index < g_monitor_apps_num; ++index) {
			step = (rtt_params_size * index)  + index * int_size;
			memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
			memcpy(send_msg + step + int_size,
			       &(g_monitor_apps_table[index].app_rtt[CELL_INDEX]), rtt_params_size);
		}

		ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_CELL_RTT,
							(char *) send_msg,
							(rtt_params_size + int_size) * g_monitor_apps_num);
		break;

	case ALL_IF_INDEX:
		for (index = 0; index < g_monitor_apps_num; ++index) {
			step = rtt_params_size * IFACE_NUM_MAX * index + index * int_size;
			memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
			memcpy(send_msg + step + int_size, &(g_monitor_apps_table[index].app_rtt),
			       rtt_params_size * IFACE_NUM_MAX);
		}

		ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_ALL_RTT,
							(char *) send_msg,
							(rtt_params_size + int_size) * IFACE_NUM_MAX * g_monitor_apps_num);
		break;

	default:
		printk("oplus_apps_monitor: apps_monitor_report_apps_rtt_to_user, the if_index is unvalue! \n");
		return -EINVAL;
	}

	if (ret == 0) {
		if (apps_monitor_debug) {
			print_apps_rtt_record();
		}

		/*report success,clear the rtt record in kernel*/
		clear_apps_rtt_record(if_index);
	} else {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_rtt_to_user fail! \n");
	}

	return ret;
}

static int apps_monitor_report_dev_rtt_to_user(void)
{
#define MAX_DEV_RTT_MSG_LEN (256)
	int ret = 0;
	int index = 0;
	int step = 0;
	int rtt_params_size = sizeof(rtt_params);
	static char send_msg[MAX_DEV_RTT_MSG_LEN] = {0};

	memset(send_msg, 0, MAX_DEV_RTT_MSG_LEN);

	if (rtt_params_size * IFACE_NUM_MAX > MAX_DEV_RTT_MSG_LEN) {
		printk("oplus_apps_monitor: apps_monitor_report_dev_rtt_to_user, the RTT Msg is too big, len = %0x \n",
		       rtt_params_size * IFACE_NUM_MAX);
		return -EINVAL;
	}

	for (index = 0; index < IFACE_NUM_MAX; ++index) {
		step = rtt_params_size * index;
		memcpy(send_msg + step, &g_monitor_dev_table[index], rtt_params_size);
	}

	ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_DEV_RTT,
						(char *) send_msg,
						rtt_params_size * IFACE_NUM_MAX);

	if (ret == 0) {
		if (apps_monitor_debug) {
			print_dev_rtt_record();
		}

		/*report success,clear the rtt record in kernel*/
		clear_dev_rtt_record();
	} else {
		printk("oplus_apps_monitor: apps_monitor_report_dev_rtt_to_user fail! \n");
	}

	return ret;
}

static int apps_monitor_netlink_nlmsg_handle(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	switch (nla->nla_type) {
	case APPS_MONITOR_SET_ANDROID_PID:
		ret = apps_monitor_set_android_pid(skb);
		break;

	case APPS_MONITOR_SET_APPS_UID:
		ret = apps_monitor_set_apps_uid(nla);
		break;

	case APPS_MONITOR_GET_APPS_CELL_RTT:
		ret = apps_monitor_report_apps_rtt_to_user(CELL_INDEX);
		break;

	case APPS_MONITOR_GET_APPS_WLAN_RTT:
		ret = apps_monitor_report_apps_rtt_to_user(WLAN_INDEX);
		break;

	case APPS_MONITOR_GET_APPS_ALL_RTT:
		ret = apps_monitor_report_apps_rtt_to_user(ALL_IF_INDEX);
		break;

	case APPS_MOINTOR_SET_RTT_THRED:
		ret = apps_monitor_set_rtt_thred(nla);
		break;

	case APPS_MONITOR_GET_DEV_RTT:
		ret = apps_monitor_report_dev_rtt_to_user();
		break;

	case APPS_POWER_MONITOR_MSG_DL_CTRL:
		ret = app_monitor_dl_ctl_msg_handle(nla);
		break;

	case APPS_POWER_MONITOR_MSG_DL_RPT_CTRL:
		ret = app_monitor_dl_report_msg_handle(nla);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* static void apps_monitor_netlink_recv(struct sk_buff *skb)
{
	mutex_lock(&apps_monitor_netlink_mutex);
	netlink_rcv_skb(skb, &apps_monitor_netlink_nlmsg_handle);
	mutex_unlock(&apps_monitor_netlink_mutex);
} */

static int apps_monitor_netlink_init(void)
{
	int ret;
	ret = genl_register_family(&apps_monitor_genl_family);
	if (ret) {
		printk("[APPS_MONITOR]:genl_register_family:%s error,ret=%d\n", APPS_MONITOR_FAMILY, ret);
		return ret;
	} else {
		printk("[APPS_MONITOR]:genl_register_family complete,id=%d!\n", apps_monitor_genl_family.id);
	}

	return 0;
}

static void apps_monitor_netlink_exit(void)
{
	/*netlink_kernel_release(apps_monitor_netlink_sock);
	apps_monitor_netlink_sock = NULL;*/
	genl_unregister_family(&apps_monitor_genl_family);
}

static void apps_monitor_timer_function(struct timer_list *t)
{
	if (rrt_period_report_enable && apps_monitor_netlink_pid != 0) {
		apps_monitor_report_apps_rtt_to_user(ALL_IF_INDEX);
		apps_monitor_report_dev_rtt_to_user();
	}

	mod_timer(&apps_monitor_timer,
		  jiffies + rrt_period_report_timer * TIMER_EXPIRES);
}

static int is_need_period_timer(void)
{
	return rrt_period_report_enable;
}

static void apps_monitor_timer_init(void)
{
	timer_setup(&apps_monitor_timer, apps_monitor_timer_function, 0);
	apps_monitor_timer.expires = jiffies + rrt_period_report_timer * TIMER_EXPIRES;
	add_timer(&apps_monitor_timer);
}

static void apps_monitor_timer_del(void)
{
	del_timer(&apps_monitor_timer);
}

static int get_static_index(struct sk_buff *skb)
{
	struct net_device *dev;
	int index = -1;

	dev = skb->dev;
	if (!dev) {
		printk("[apps_monitor]:dev is null,return\n");
		return index;
	}

	if (!strncmp(skb->dev->name, "wlan", 4)) {
		index = WLAN_INDEX;
	} else if (!strncmp(skb->dev->name, "rmnet", 5) ||
			   !strncmp(skb->dev->name, "ccmni", 5)) {
		index = CELL_INDEX;
	}

	return index;
}

static unsigned int oplus_apps_monitor_postrouting_hook4(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk;
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	struct tcp_sock *tp;
	struct inet_connection_sock *icsk;
	int retrans = 0;

	iph = ip_hdr(skb);
	tcph = tcp_hdr(skb);
	if (skb->protocol != htons(ETH_P_IP) || (!iph))
		return NF_ACCEPT;

	if (iph->protocol != IPPROTO_TCP || !tcph)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (!sk) {
		return NF_ACCEPT;
	}

	if (sk->sk_state > TCP_SYN_SENT) {
		return NF_ACCEPT;
	}

	tp = tcp_sk(sk);
	icsk = inet_csk(sk);
	if (icsk->icsk_ca_state >= TCP_CA_Recovery && tp->high_seq !=0 && before(ntohl(tcph->seq), tp->high_seq)) {
		retrans = 1;
	}
	oplus_app_monitor_update_app_info(sk, skb, 1, retrans);

	return NF_ACCEPT;
}

static unsigned int oplus_apps_monitor_input_hook4(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk;
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	struct tcp_sock *tp;
	int rtt;
	int index;

	iph = ip_hdr(skb);
	tcph = tcp_hdr(skb);
	if (skb->protocol != htons(ETH_P_IP) || (!iph))
		return NF_ACCEPT;

	if (iph->protocol != IPPROTO_TCP || !tcph)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (!sk) {
		return NF_ACCEPT;
	}

	if (sk->sk_state > TCP_SYN_SENT) {
		return NF_ACCEPT;
	}

	tp = tcp_sk(sk);
	rtt = (tp->srtt_us >> 3) / 1000;
	index = get_static_index(skb);
	statistics_monitor_apps_rtt_via_uid(index, rtt, sk);
	oplus_app_monitor_update_app_info(sk, skb, 0, 0);

	return NF_ACCEPT;
}

static unsigned int oplus_apps_monitor_postrouting_hook6(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk;
	struct tcphdr *tcph = NULL;
	struct ipv6hdr *ipv6h = NULL;
	struct tcp_sock *tp;
	struct inet_connection_sock *icsk;
	int retrans = 0;

	ipv6h = ipv6_hdr(skb);
	tcph = tcp_hdr(skb);
	if (skb->protocol != htons(ETH_P_IPV6) || (!ipv6h))
		return NF_ACCEPT;

	if ((ipv6h->nexthdr != NEXTHDR_TCP) || (!tcph))
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (!sk) {
		return NF_ACCEPT;
	}

	if (sk->sk_state > TCP_SYN_SENT) {
		return NF_ACCEPT;
	}

	tp = tcp_sk(sk);
	icsk = inet_csk(sk);
	if (icsk->icsk_ca_state >= TCP_CA_Recovery && tp->high_seq !=0 && before(ntohl(tcph->seq), tp->high_seq)) {
		retrans = 1;
	}
	oplus_app_monitor_update_app_info(sk, skb, 1, retrans);

	return NF_ACCEPT;
}

static unsigned int oplus_apps_monitor_input_hook6(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk;
	struct tcphdr *tcph = NULL;
	struct ipv6hdr *ipv6h = NULL;
	int index;
	struct tcp_sock *tp;
	int rtt;

	ipv6h = ipv6_hdr(skb);
	tcph = tcp_hdr(skb);
	if (skb->protocol != htons(ETH_P_IPV6) || (!ipv6h))
		return NF_ACCEPT;

	if ((ipv6h->nexthdr != NEXTHDR_TCP) || (!tcph))
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (!sk) {
		return NF_ACCEPT;
	}

	if (sk->sk_state > TCP_SYN_SENT) {
		return NF_ACCEPT;
	}

	tp = tcp_sk(sk);
	rtt = (tp->srtt_us >> 3) / 1000;
	index = get_static_index(skb);
	statistics_monitor_apps_rtt_via_uid(index, rtt, sk);
	oplus_app_monitor_update_app_info(sk, skb, 0, 0);
	return NF_ACCEPT;
}

static struct nf_hook_ops apps_monitor_netfilter_ops[] __read_mostly =
{
	{
		.hook		= oplus_apps_monitor_postrouting_hook4,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
	{
		.hook		= oplus_apps_monitor_input_hook4,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
		{
		.hook		= oplus_apps_monitor_postrouting_hook6,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
	{
		.hook		= oplus_apps_monitor_input_hook6,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
};

static int __init oplus_apps_monitor_init(void)
{
	int ret = 0;

	rwlock_init(&apps_monitor_lock);
	set_monitor_apps_param_default();
	ret = apps_monitor_netlink_init();

	if (ret < 0) {
		printk("oplus_apps_monitor: oplus_apps_monitor_init module failed to init netlink.\n");
		return ret;
	} else {
		printk("oplus_apps_monitor: oplus_apps_monitor_init module init netlink successfully.\n");
	}

	ret |= apps_monitor_sysctl_init();

	ret = nf_register_net_hooks(&init_net, apps_monitor_netfilter_ops, ARRAY_SIZE(apps_monitor_netfilter_ops));
	if (ret < 0) {
		printk("[apps_monitor]:netfilter register failed, ret=%d\n", ret);
		apps_monitor_netlink_exit();
		return ret;
	} else {
		printk("[apps_monitor]:netfilter register successfully.\n");
	}

	if (is_need_period_timer()) {
		apps_monitor_timer_init();
	}

	oplus_app_power_monitor_init();
	return ret;
}

static void __exit oplus_apps_monitor_fini(void)
{
	clear_apps_rtt_record(ALL_IF_INDEX);
	clear_dev_rtt_record();
	apps_monitor_netlink_exit();

	if (is_need_period_timer()) {
		apps_monitor_timer_del();
	}

	if (apps_monitor_table_hrd) {
		unregister_net_sysctl_table(apps_monitor_table_hrd);
	}

	nf_unregister_net_hooks(&init_net, apps_monitor_netfilter_ops, ARRAY_SIZE(apps_monitor_netfilter_ops));
	oplus_app_power_monitor_fini();
}

module_init(oplus_apps_monitor_init);
module_exit(oplus_apps_monitor_fini);
MODULE_LICENSE("GPL v2");
