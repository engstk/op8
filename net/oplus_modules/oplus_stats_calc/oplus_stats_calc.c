#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/version.h>
#include <net/dst.h>
#include <net/genetlink.h>
#include <net/inet_connection_sock.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/tcp_states.h>
#include <net/udp.h>
#include <linux/netfilter_ipv6.h>
#include <linux/crc32.h>

#define LOG_TAG "oplus_stats_calc"

static int s_debug = 1;

#define LOGK(flag, fmt, args...)     \
    do {                             \
        if (flag || s_debug) {       \
            printk("[%s]:" fmt "\n", LOG_TAG, ##args);\
        }                                             \
    } while (0)

static spinlock_t s_stats_calc_lock;
static DEFINE_HASHTABLE(s_iface_uid_stats_map, 8);

static u32 s_user_pid = 0;
static u32 s_stats_count = 0;

static struct genl_family oplus_stats_calc_genl_family;

enum stats_calc_type_et {
	OPLUS_STATS_CALC_MSG_UNSPEC,
	OPLUS_STATS_CALC_MSG_GET_ALL,
	__OPLUS_STATS_CALC_MSG_MAX,
};

#define OPLUS_STATS_CALC_MSG_MAX (__OPLUS_STATS_CALC_MSG_MAX - 1)

enum stats_calc_cmd_type_et {
	OPLUS_STATS_CALC_CMD_UNSPEC,
	OPLUS_STATS_CALC_CMD_DOWN,
	OPLUS_STATS_CALC_CMD_UP,
	__OPLUS_STATS_CALC_CMD_MAX,
};

#define OPLUS_STATS_CALC_CMD_MAX (__OPLUS_STATS_CALC_CMD_MAX - 1)


#define OPLUS_STATS_CALC_FAMILY_NAME "oplus_stats"
#define OPLUS_STATS_CALC_FAMILY_VERSION 1

#pragma pack (4)
struct iface_uid_stats_value {
	char iface[IFNAMSIZ];
	u32 uid;
	u64 rxBytes;
	u64 txBytes;
	u64 rxPackets;
	u64 txPackets;
};
#pragma pack ()

struct iface_uid_stats {
	struct hlist_node node;
	struct iface_uid_stats_value value;
};

static u64 getHashKey(char *iface, u32 uid) {
	u32 crc = crc32(0, iface, strlen(iface));
	u64 result = ((u64)crc) << 32 | uid;
	return result;
}

static struct iface_uid_stats * get_stats_from_map(char *iface, u32 uid, u64 key) {
	struct hlist_node *pos = NULL;
	struct hlist_node *next = NULL;
	struct iface_uid_stats *stats = NULL;

	struct hlist_head *t = &(s_iface_uid_stats_map[hash_min(key, HASH_BITS(s_iface_uid_stats_map))]);
	hlist_for_each_safe(pos, next, t) {
		stats = container_of(pos, struct iface_uid_stats, node);
		if(strcmp(stats->value.iface, iface) == 0  && uid == stats->value.uid) {
			return stats;
		}
	}
	return NULL;
}

static int add_iface_uid_stats(char *iface, u32 uid, u32 len, int dir) {
	u64 key = 0;
	struct iface_uid_stats *stats = NULL;

	key = getHashKey(iface, uid);
	spin_lock_bh(&s_stats_calc_lock);
	stats = get_stats_from_map(iface, uid, key);

	if (stats == NULL) {
		stats = kmalloc(sizeof(struct iface_uid_stats), GFP_ATOMIC);
		if (stats == NULL) {
			spin_unlock_bh(&s_stats_calc_lock);
			return -1;
		}
		memset(stats, 0, sizeof(struct iface_uid_stats));
		INIT_HLIST_NODE(&(stats->node));
		strcpy(stats->value.iface, iface);
		stats->value.uid = uid;
		if(dir == 1) {
			stats->value.rxBytes = len;
			stats->value.rxPackets = 1;
		}else{
			stats->value.txBytes = len;
			stats->value.txPackets = 1;
		}
		hash_add(s_iface_uid_stats_map, &(stats->node), key);
		s_stats_count++;
		LOGK(1, "add_iface_uid_stats add iface %s", iface);
	}else{
		if(dir == 1) {
			stats->value.rxBytes += len;
			stats->value.rxPackets += 1;
		}else{
			stats->value.txBytes += len;
			stats->value.txPackets += 1;
		}
	}
	spin_unlock_bh(&s_stats_calc_lock);
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

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);

	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &oplus_stats_calc_genl_family, 0, cmd);
	LOGK(1, "genl_msg_prepare_usr_msg_1,skb_len=%u,pid=%u,cmd=%u,id=%u\n",
	skb->len, (unsigned int)pid, cmd, oplus_stats_calc_genl_family.id);
	*skbp = skb;
	return 0;
}

static int send_netlink_data(int type, char *data, int len) {
	int ret = 0;
	void * head;
	struct sk_buff *skbuff;
	size_t size;

	if (!s_user_pid) {
		LOGK(1, "send_netlink_data,oplus_score_user_pid=0\n");
		return -1;
	}

	/* allocate new buffer cache */
	size = nla_total_size(len);
	ret = genl_msg_prepare_usr_msg(OPLUS_STATS_CALC_CMD_UP, size, s_user_pid, &skbuff);
	if (ret) {
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, type, data, len);
	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, s_user_pid);
	if(ret < 0) {
		LOGK(1,"genlmsg_unicast return error, ret = %d\n", ret);
		return -1;
	}
	return 0;
}

static int send_all_stats(struct nlattr *nla) {
	char *data = NULL;
	int total_len = 0;
	struct iface_uid_stats_value *pvalue = NULL;
	struct iface_uid_stats *pos = NULL;
	struct hlist_node *next = NULL;
	int count = 0;
	int pkt = 0;
	int ret = 0;

	LOGK(1, "send_stats_to_user %u", s_stats_count);
	spin_lock_bh(&s_stats_calc_lock);
	total_len = sizeof(s_stats_count) + sizeof(struct iface_uid_stats_value) * s_stats_count;
	data = kmalloc(total_len, GFP_ATOMIC);
	if (data == NULL) {
		spin_unlock_bh(&s_stats_calc_lock);
		return -1;
	}
	memset(data, 0, total_len);
	if (s_stats_count != 0) {
		pvalue = (struct iface_uid_stats_value *)(data + sizeof(u32));
		hash_for_each_safe(s_iface_uid_stats_map, pkt, next, pos, node) {
			if (count < s_stats_count) {
				memcpy(pvalue, &pos->value, sizeof(struct iface_uid_stats_value));
				pvalue++;
				count++;
			}
		}
	}
	LOGK(1, "get data count %d %u", count, s_stats_count);
	memcpy(data, &count, sizeof(count));
	spin_unlock_bh(&s_stats_calc_lock);
	ret = send_netlink_data(OPLUS_STATS_CALC_MSG_GET_ALL, data, total_len);
	LOGK(1, "send_netlink_data return %d", ret);
	if (data) {
		kfree(data);
	}
	return 0;
}

static int get_sock_uid(struct sk_buff *skb) {
	struct sock *sk = sk_to_full_sk(skb->sk);
	kuid_t kuid;

	if (!sk || !sk_fullsock(sk))
		return overflowuid;
	kuid = sock_net_uid(sock_net(sk), sk);
	return from_kuid_munged(sock_net(sk)->user_ns, kuid);
}

static unsigned int oplus_stats_calc_input_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	u32 uid = 0;

	if (skb->dev == NULL) {
		LOGK(0, "dev is null %d", skb->skb_iif);
		return NF_ACCEPT;
	}
	uid = get_sock_uid(skb);
	add_iface_uid_stats(skb->dev->name, uid, skb->len, 1);
	return NF_ACCEPT;
}

static unsigned int oplus_stats_calc_output_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	u32 uid = 0;

	if (skb->dev == NULL) {
		LOGK(0, "dev is null %d", skb->skb_iif);
		return NF_ACCEPT;
	}
	uid = get_sock_uid(skb);
	add_iface_uid_stats(skb->dev->name, uid, skb->len, 0);
	return NF_ACCEPT;
}

static struct nf_hook_ops oplus_stats_calc_netfilter_ops[] __read_mostly = {
    {
        .hook = oplus_stats_calc_input_hook,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_LOCAL_IN,
        .priority = NF_IP_PRI_FILTER + 1,
    },
    {
        .hook = oplus_stats_calc_output_hook,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_POST_ROUTING,
        .priority = NF_IP_PRI_FILTER + 1,
    },
};

static int oplus_stats_calc_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	LOGK(0, "set s_user_pid=%u type=%u len=%u.", nlhdr->nlmsg_pid, nla->nla_type, nla->nla_len);
	if (s_user_pid == 0) {
		s_user_pid = nlhdr->nlmsg_pid;
	} else if (s_user_pid != nlhdr->nlmsg_pid) {
		LOGK(1, "user pid changed!! %u - %u", s_user_pid, nlhdr->nlmsg_pid);
		s_user_pid = nlhdr->nlmsg_pid;
	}
	LOGK(1, "nla->nla_type %d", nla->nla_type);

	switch (nla->nla_type) {
	case OPLUS_STATS_CALC_MSG_GET_ALL:
		ret = send_all_stats(nla);
		LOGK(0, "send_all_stats return %d", ret);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static const struct genl_ops oplus_stats_calc_genl_ops[] = {
	{
		.cmd = OPLUS_STATS_CALC_CMD_DOWN,
		.flags = 0,
		.doit = oplus_stats_calc_netlink_rcv_msg,
		.dumpit = NULL,
	},
};

static struct genl_family oplus_stats_calc_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = OPLUS_STATS_CALC_FAMILY_NAME,
	.version = OPLUS_STATS_CALC_FAMILY_VERSION,
	.maxattr = OPLUS_STATS_CALC_CMD_UP + 1,
	.ops = oplus_stats_calc_genl_ops,
	.n_ops = ARRAY_SIZE(oplus_stats_calc_genl_ops),
};

static int oplus_stats_calc_netlink_init(void)
{
	int ret;
	ret = genl_register_family(&oplus_stats_calc_genl_family);
	if (ret) {
		LOGK(1, "genl_register_family:%s failed,ret = %d\n", OPLUS_STATS_CALC_FAMILY_NAME, ret);
		return ret;
	} else {
		LOGK(1, "genl_register_family complete, id = %d!\n", oplus_stats_calc_genl_family.id);
	}

	return 0;
}


static void oplus_stats_calc_netlink_exit(void)
{
	genl_unregister_family(&oplus_stats_calc_genl_family);
}

static struct ctl_table oplus_net_hook_sysctl_table[] = {
	{
		.procname   = "debug",
		.data       = &s_debug,
		.maxlen     = sizeof(int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname   = "count",
		.data       = &s_stats_count,
		.maxlen     = sizeof(int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
	},
	{}
};


static struct ctl_table_header *oplus_stats_calc_table_hdr = NULL;

static int oplus_stats_calc_sysctl_init(void)
{
	oplus_stats_calc_table_hdr = register_net_sysctl(&init_net, "net/oplus_stats_calc", oplus_net_hook_sysctl_table);
	return oplus_stats_calc_table_hdr == NULL ? -ENOMEM : 0;
}

static int __init oplus_stats_calc_init(void)
{
	int ret = 0;

	spin_lock_init(&s_stats_calc_lock);

	ret = oplus_stats_calc_netlink_init();
	if (ret < 0) {
	LOGK(1, "init module failed to init netlink, ret =%d", ret);
		return ret;
	} else {
		LOGK(1, "init module init netlink successfully.");
	}

	ret = nf_register_net_hooks(&init_net, oplus_stats_calc_netfilter_ops, ARRAY_SIZE(oplus_stats_calc_netfilter_ops));
	if (ret < 0) {
		LOGK(1, "oplus_stats_calc_init netfilter register failed, ret=%d", ret);
		oplus_stats_calc_netlink_exit();
		return ret;
	} else {
		LOGK(1, "oplus_stats_calc_init netfilter register successfully.");
	}

	oplus_stats_calc_sysctl_init();
	return ret;
}

static void __exit oplus_stats_calc_fini(void)
{
	LOGK(1, "oplus_stats_fini.");
	oplus_stats_calc_netlink_exit();
	nf_unregister_net_hooks(&init_net, oplus_stats_calc_netfilter_ops, ARRAY_SIZE(oplus_stats_calc_netfilter_ops));
	if (oplus_stats_calc_table_hdr) {
		unregister_net_sysctl_table(oplus_stats_calc_table_hdr);
	}
}

MODULE_LICENSE("GPL");
module_init(oplus_stats_calc_init);
module_exit(oplus_stats_calc_fini);

