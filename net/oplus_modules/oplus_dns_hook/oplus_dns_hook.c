/***********************************************************
** Copyright (C), 2008-2021, oplus Mobile Comm Corp., Ltd.
** File: oplus_dns_hook.c
** Description: Add dns hook for quick app.
**
** Version: 1.0
** Date : 2021/8/13
** Author: ShiQianhua
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** shiqianhua 2021/8/13 1.0 build this module
****************************************************************/
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


#define LOG_TAG "oplus_dns_hook"

#define DNS_DST_PORT 53

#define MAX_DNS_HOOK_SIZE 20
#define MAX_QUERY_SIZE    10
#define MAX_ANSWER_SIZE   20

#define MAX_URL_LEN 127

#define DNS_TYPE_CNAME 5
#define DNS_TYPE_A     1

#define DNS_IPV4_LEN       4
#define QUERY_MATCH_EXTEND 5

#pragma pack(1)
typedef struct {
    uint32_t inuse;
    char url[MAX_URL_LEN + 1];
    uint32_t addr;
} dns_hook_data_t;

static int s_debug = 0;
static uint32_t s_dns_hook_enable = 1;
static uint32_t s_user_pid = 0;
static spinlock_t s_dns_hook_lock;

static uint32_t s_dns_hook_count = 0;
static dns_hook_data_t s_hook_addrs[MAX_DNS_HOOK_SIZE];

static int s_first_notify = 0;

typedef struct {
    uint16_t id;
    uint16_t flag;
    uint16_t queryCount;
    uint16_t ansCount;
    uint16_t nsCount;
    uint16_t arCount;
} dnshdr_t;

typedef struct {
    uint16_t offset;
    uint16_t pad;
    uint32_t addr;
} query_info_t;

typedef struct {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t dataLen;
} ans_item_hdr_t;

#define IPV4_ANSWER_ITEM_LEN (sizeof(ans_item_hdr_t) + sizeof(uint32_t))

#define DNS_RSP_FLAG 0x8000
#define DNS_RD_FLAG  0x0100
#define DNS_RA_FLAG  0x0080

static struct ctl_table_header *oplus_dns_hook_table_hdr = NULL;

#define LOGK(flag, fmt, args...)     \
    do {                             \
        if (flag || s_debug) {       \
            printk("[%s]:" fmt "\n", LOG_TAG, ##args);\
        }                                             \
    } while (0)

static int send_first_dns_event(char *hostname);

static int check_dns_package(struct sk_buff *skb, int is_ingress)
{
    struct iphdr *iph = NULL;
    struct ipv6hdr *ipv6h = NULL;
    struct udphdr *udph = NULL;

    if (skb->protocol == htons(ETH_P_IP)) {
        iph = ip_hdr(skb);
        if (!iph) {
            return -1;
        }
        if (iph->protocol != IPPROTO_UDP) {
            return -1;
        }
    } else if (skb->protocol == htons(ETH_P_IPV6)) {
        ipv6h = ipv6_hdr(skb);
        if (!ipv6h) {
            return -1;
        }
        if (ipv6h->nexthdr != IPPROTO_UDP) {
            return -1;
        }
    } else {
        return -1;
    }

    udph = udp_hdr(skb);
    if (!udph) {
        return -1;
    }
    if (is_ingress) {
        if (udph->source != htons(DNS_DST_PORT)) {
            return -1;
        }
    } else {
        if (udph->dest != htons(DNS_DST_PORT)) {
            return -1;
        }
    }

    return 0;
}

static int parse_querys(char *start, int *querys_len, query_info_t *queryList, uint16_t query_size, int *match_count, int hook)
{
    int matchSize = 0;
    int offset = 0;
    int i = 0;
    int j = 0;
	int notify_flag = 0;
	char notify_url[MAX_URL_LEN + 1] = {0};

    spin_lock(&s_dns_hook_lock);
    for (i = 0; i < query_size; i++) {
        char tmpurl[MAX_URL_LEN + 1] = {0};
        uint8_t len = 0;
        uint8_t tmpLen = 0;
        size_t urlLen = strlen(start + offset + 1);
        if (urlLen > MAX_URL_LEN) {
            LOGK(1, "url %s len %lu over %d\n", start + offset + 1, urlLen, MAX_URL_LEN);
            offset += (urlLen + 2 + 4);
            continue;
        }
        strcpy(tmpurl, start + offset + 1);

        tmpLen = *(start + offset);
        // LOGK(1, "tmpurl %s strlen:%u, tmpLen:%d", tmpurl, strlen(tmpurl), tmpLen);
        do {
            if (tmpLen > (urlLen - len)) {
                LOGK(1, "len invalid! %d-%lu-%d", tmpLen, urlLen, len);
                spin_unlock(&s_dns_hook_lock);
                return -1;
            }
            len += tmpLen;
            tmpLen = *(tmpurl + len);
            if (*(tmpurl + len) != 0) {
                *(tmpurl + len) = '.';
            }
            len += 1;
            // LOGK(1, "tmlen:%d, len:%d", tmpLen, len);
        } while (tmpLen != 0);
        LOGK(0, "tmpurl %s len:%d", tmpurl, len);
        for (j = 0; j < MAX_DNS_HOOK_SIZE; j++) {
            if (s_hook_addrs[j].inuse) {
                if (strcmp(tmpurl, s_hook_addrs[j].url) == 0) {
                    queryList->offset = offset + sizeof(dnshdr_t);
                    queryList->addr = s_hook_addrs[j].addr;
                    LOGK(1, "hook %d parse_querys match %d-%d-%s-%u", hook, i, j, tmpurl, queryList->addr);
                    queryList += 1;
                    matchSize++;
					if (s_first_notify == 0) {
						notify_flag = 1;
						strncpy(notify_url, tmpurl, MAX_URL_LEN);
						s_first_notify = 1;
					}
                }
            }
        }
        offset += urlLen + 2 + 4;
    }
    spin_unlock(&s_dns_hook_lock);

	if (notify_flag) {
		send_first_dns_event(notify_url);
	}

    *match_count = matchSize;
    *querys_len = offset;
    return 0;
}

static struct sk_buff *gen_dns_rsp(struct sk_buff *skb, int query_len, query_info_t *query, int match_count)
{
    struct iphdr *iph = NULL;
    struct ipv6hdr *ipv6h = NULL;
    struct sk_buff *skb_rsp = NULL;
    struct udphdr *udph_rsp = NULL;
    dnshdr_t *dnshdr = NULL;
    int leftpayload = 0;
    int i = 0;
    int answer_len = 0;
    char *ans_buf = NULL;
    int ret = 0;

    answer_len = match_count * IPV4_ANSWER_ITEM_LEN;
    ans_buf = kmalloc(answer_len, GFP_ATOMIC);
    if (ans_buf == NULL) {
        LOGK(1, "1 malloc len %d failed", answer_len);
        return NULL;
    }

    for (i = 0; i < match_count; i++) {
        ans_item_hdr_t *hdr = (ans_item_hdr_t *)(ans_buf + IPV4_ANSWER_ITEM_LEN * i);
        hdr->name = ntohs(0xC000 | query[i].offset);
        hdr->type = ntohs(0x1);
        hdr->class = ntohs(0x1);
        hdr->ttl = ntohl(0xff);
        hdr->dataLen = ntohs(sizeof(uint32_t));
        memcpy((char *)(hdr + 1), &query[i].addr, sizeof(uint32_t));
        LOGK(0, "set url add to buf %u", query[i].addr);
    }
    skb_rsp = skb_copy_expand(skb, skb_headroom(skb), skb_tailroom(skb) + answer_len, GFP_ATOMIC);
    if (skb_rsp == NULL) {
        kfree(ans_buf);
        LOGK(1, "skb_copy_expand failed! size: %u", answer_len);
        return NULL;
    }
    __skb_put_data(skb_rsp, ans_buf, answer_len);

    // update ip
    skb_set_mac_header(skb_rsp, 0);
    if (skb_rsp->protocol == htons(ETH_P_IP)) {
        __be32 tmp = 0;
        iph = ip_hdr(skb_rsp);
        tmp = iph->saddr;
        iph->saddr = iph->daddr;
        iph->daddr = tmp;
        iph->tot_len = htons(ntohs(iph->tot_len) + answer_len);
        ip_send_check(iph);
    } else {
        struct in6_addr tmp;
        ipv6h = ipv6_hdr(skb_rsp);
        tmp = ipv6h->saddr;
        ipv6h->saddr = ipv6h->daddr;
        ipv6h->daddr = tmp;
        ipv6h->payload_len = htons(ntohs(ipv6h->payload_len) + answer_len);
    }
    // update udp
    udph_rsp = udp_hdr(skb_rsp);
    udph_rsp->dest = udph_rsp->source;
    udph_rsp->source = htons(DNS_DST_PORT);
    udph_rsp->len = htons(ntohs(udph_rsp->len) + answer_len);
    udph_rsp->check = 0;
    leftpayload = ntohs(udph_rsp->len) - sizeof(struct udphdr) - sizeof(dnshdr_t) - query_len - answer_len;
    LOGK(0, "udplen: %d, leftpayload value %d, queryLen:%d", ntohs(udph_rsp->len), leftpayload, query_len);
    // update answer
    dnshdr = (dnshdr_t *)(skb_transport_header(skb_rsp) + sizeof(struct udphdr));
    dnshdr->flag = htons(DNS_RSP_FLAG | DNS_RD_FLAG | DNS_RA_FLAG);
    dnshdr->ansCount = htons(match_count);
    dnshdr->nsCount = 0;
    dnshdr->arCount = 0;

    if (leftpayload != 0) {
        char *left_buf = NULL;
        left_buf = kmalloc(leftpayload, GFP_ATOMIC);
        if (left_buf == NULL) {
            LOGK(1, "malloc len %d failed", leftpayload);
            kfree_skb(skb_rsp);
            kfree(ans_buf);
            return NULL;
        }
        ret = skb_copy_bits(skb_rsp, skb_transport_offset(skb_rsp) + sizeof(struct udphdr) + sizeof(dnshdr_t) + query_len, left_buf, leftpayload);
        if (ret) {
            LOGK(1, "skb_copy_bits failed! %d", ret);
            kfree_skb(skb_rsp);
            kfree(left_buf);
            kfree(ans_buf);
            return NULL;
        }
        ret = skb_store_bits(skb_rsp, skb_transport_offset(skb_rsp) + sizeof(struct udphdr) + sizeof(dnshdr_t) + query_len, ans_buf, answer_len);
        if (ret) {
            LOGK(1, "skb_store_bits 1 failed! %d", ret);
            kfree_skb(skb_rsp);
            kfree(left_buf);
            kfree(ans_buf);
            return NULL;
        }
        ret = skb_store_bits(skb_rsp, skb_transport_offset(skb_rsp) + sizeof(struct udphdr) + sizeof(dnshdr_t) + query_len + answer_len, left_buf,
                             leftpayload);
        if (ret) {
            LOGK(1, "skb_store_bits 2 failed! %d", ret);
            kfree_skb(skb_rsp);
            kfree(left_buf);
            kfree(ans_buf);
            return NULL;
        }
        kfree(left_buf);
    }
    kfree(ans_buf);
    if (skb_rsp->protocol == htons(ETH_P_IP)) {
        skb_rsp->csum = csum_tcpudp_nofold(iph->saddr, iph->daddr, skb_rsp->len - ip_hdrlen(skb), IPPROTO_UDP, 0);
        udph_rsp->check = __skb_checksum_complete(skb_rsp);
    } else {
        // uint16_t frag_off = 0;
        // int hdrlen = 0;
        // u8 nexthdr = 0;
        // hdrlen = ipv6_skip_exthdr(skb_rsp, sizeof(struct ipv6hdr), &nexthdr, &frag_off);
        // skb_rsp->csum = ~csum_unfold(csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr, skb_rsp->len - hdrlen,
        // IPPROTO_UDP, csum_sub(0, skb_checksum(skb, 0, hdrlen, 0)))); udph_rsp->check =
        // __skb_checksum_complete(skb_rsp);
    }
    //skb_dump(KERN_WARNING, skb, false);
    //skb_dump(KERN_WARNING, skb_rsp, false);

    return skb_rsp;
}

static int dns_hook_process_postrouting(struct sk_buff *skb, int hook, const struct nf_hook_state *state)
{
    dnshdr_t *dnshdr = NULL;
    query_info_t *query_list = NULL;
    struct sk_buff *rsp_skb = NULL;
    uint16_t query_count = 0;
    int query_len = 0;
    int match_count = 0;
    int ret = 0;

    if (!s_dns_hook_enable) {
        return -1;
    }
    if (check_dns_package(skb, 0)) {
        return -1;
    }

    dnshdr = (dnshdr_t *)(skb_transport_header(skb) + sizeof(struct udphdr));
    query_count = ntohs(dnshdr->queryCount);
    LOGK(0, "output query_count: %u", query_count);
    if (query_count == 0 || query_count > MAX_QUERY_SIZE) {
        return -1;
    }
    LOGK(0, "output malloc size %lu", sizeof(query_info_t) * query_count);
    query_list = kmalloc(sizeof(query_info_t) * query_count, GFP_ATOMIC);
    if (query_list == NULL) {
        return -1;
    }
    ret = parse_querys((char *)(dnshdr + 1), &query_len, query_list, query_count, &match_count, hook);
    if (ret) {
        kfree(query_list);
        return -1;
    }

    if (match_count == 0) {
        kfree(query_list);
        return -1;
    }
    LOGK(0, "skb_output return package! match_count %d", match_count);
    rsp_skb = gen_dns_rsp(skb, query_len, query_list, match_count);
    kfree(query_list);
    if (!rsp_skb) {
        return -1;
    }

    nf_ct_attach(rsp_skb, skb);
    if (skb->protocol == htons(ETH_P_IP)) {
        if (ip_route_me_harder(state->net, state->sk, rsp_skb, RTN_UNSPEC)) {
            LOGK(1, "ip_route_me_harder error");
            kfree_skb(rsp_skb);
            return -1;
        }
        ret = ip_local_out(state->net, state->sk, rsp_skb);
        LOGK(0, "ip_local_out return %d", ret);
    } else if(skb->protocol == htons(ETH_P_IPV6)) {
        if (ip6_route_me_harder(state->net, state->sk, rsp_skb)) {
            LOGK(1, "ip6_route_me_harder error");
            kfree_skb(rsp_skb);
            return -1;
        }
        ret = ip6_local_out(state->net, state->sk, rsp_skb);
        LOGK(0, "ip6_local_out return %d", ret);
    }

    return 0;
}

static unsigned int oplus_dns_hook_output_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    if (dns_hook_process_postrouting(skb, state->hook, state) == 0) {
        kfree_skb(skb);
        return NF_STOLEN;
    }
    return NF_ACCEPT;
}

static struct nf_hook_ops oplus_dns_hook_netfilter_ops[] __read_mostly = {
    {
        .hook = oplus_dns_hook_output_hook,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_LOCAL_OUT,
        .priority = NF_IP_PRI_FILTER + 1,
    },
    {
        .hook = oplus_dns_hook_output_hook,
        .pf = NFPROTO_IPV6,
        .hooknum = NF_INET_LOCAL_OUT,
        .priority = NF_IP_PRI_FILTER + 1,
    },
};

enum oplus_dns_hook_msg_type_e {
    OPLUS_DNS_HOOK_MSG_UNSPEC,
    OPLUS_DNS_HOOK_MSG_ENABLE,
    OPLUS_DNS_HOOK_MSG_ADD_URL,
    OPLUS_DNS_HOOK_MSG_DEL_URL,
    OPLUS_DNS_HOOK_MSG_CLEAR_URL,
    OPLUS_DNS_HOOK_MSG_FIRST_NOTIFY,
    __OPLUS_DNS_HOOK_MSG_MAX,
};
#define OPLUS_DNS_HOOK_MSG_MAX (__OPLUS_DNS_HOOK_MSG_MAX - 1)

enum oplus_dns_hook_cmd_type_e {
    OPLUS_DNS_HOOK_CMD_UNSPEC,
    OPLUS_DNS_HOOK_CMD_SET,
    OPLUS_DNS_HOOK_CMD_NOTIFY,
    OPLUS_DNS_HOOK_CMD_MAX,
};

#define OPLUS_DNS_HOOK_CMD_MAX (OPLUS_DNS_HOOK_CMD_MAX - 1)

#define OPLUS_DNS_HOOK_FAMILY_NAME    "dns_hook"
#define OPLUS_DNS_HOOK_FAMILY_VERSION 1

#define OPLUS_NLA_DATA(na) ((char *)((char *)(na) + NLA_HDRLEN))



static int dns_hook_set_enable(struct nlattr *nla)
{
    uint32_t *data = (uint32_t *)OPLUS_NLA_DATA(nla);
    if (s_dns_hook_enable != data[0]) {
        s_dns_hook_enable = data[0];
        LOGK(1, "set enable value %d", s_dns_hook_enable);
    }
    return 0;
}

static int add_dns_hook_url(struct nlattr *nla)
{
    uint32_t *addr = NULL;
    uint8_t *data = (uint8_t *)OPLUS_NLA_DATA(nla);
    uint8_t len = data[0];
    int i = 0;
    int firstNotSetIndex = -1;
    int is_modify = 0;
    int ret = 0;

    if (len > MAX_URL_LEN || ntohs(nla->nla_len) < (5 + len)) {
        return -EINVAL;
    }

    addr = (uint32_t *)(data + len + 1);
    spin_lock(&s_dns_hook_lock);
    for (i = 0; i < MAX_DNS_HOOK_SIZE; i++) {
        if (s_hook_addrs[i].inuse) {
            if (strncmp(data + 1, s_hook_addrs[i].url, len) == 0) {
                s_hook_addrs[i].addr = ntohl(*addr);
                LOGK(1, "update url %s %u", s_hook_addrs[i].url, s_hook_addrs[i].addr);
                is_modify = 1;
                break;
            }
        } else {
            if (firstNotSetIndex == -1) {
                firstNotSetIndex = i;
            }
        }
    }

    if (!is_modify) {
        if (firstNotSetIndex != -1) {
            strncpy(s_hook_addrs[firstNotSetIndex].url, (data + 1), len);
            s_hook_addrs[firstNotSetIndex].addr = ntohl(*addr);
            s_hook_addrs[firstNotSetIndex].inuse = 1;
            s_dns_hook_count++;
            LOGK(1, "add url %s %u", s_hook_addrs[firstNotSetIndex].url, s_hook_addrs[firstNotSetIndex].addr);
        } else {
            char tmp_url[MAX_URL_LEN + 1] = {0};
            strncpy(tmp_url, (data + 1), len);
            LOGK(1, "no more place to set url %s", tmp_url);
            ret = -EOVERFLOW;
        }
    }
    spin_unlock(&s_dns_hook_lock);

    return ret;
}

static int del_dns_hook_url(struct nlattr *nla)
{
    uint8_t *data = (uint8_t *)OPLUS_NLA_DATA(nla);
    uint8_t len = data[0];
    int i = 0;
    int del = 0;

    if (len > MAX_URL_LEN || ntohs(nla->nla_len) < len) {
        return -EINVAL;
    }

    spin_lock(&s_dns_hook_lock);
    for (i = 0; i < MAX_DNS_HOOK_SIZE; i++) {
        if (s_hook_addrs[i].inuse) {
            if (strncmp(data + 1, s_hook_addrs[i].url, len) == 0) {
                LOGK(1, "del url %s %u", s_hook_addrs[i].url, s_hook_addrs[i].addr);
                memset(&s_hook_addrs[i], 0, sizeof(dns_hook_data_t));
                s_hook_addrs[i].inuse = 0;
                s_dns_hook_count--;
                del = 1;
            }
        }
    }
    spin_unlock(&s_dns_hook_lock);

    if (del == 0) {
        return ENOENT;
    }
    return 0;
}

static int clear_dns_hook_url(struct nlattr *nla)
{
    int i = 0;

    spin_lock(&s_dns_hook_lock);
    for (i = 0; i < MAX_DNS_HOOK_SIZE; i++) {
        if (s_hook_addrs[i].inuse) {
            LOGK(1, "clear url %s %u", s_hook_addrs[i].url, s_hook_addrs[i].addr);
            memset(&s_hook_addrs[i], 0, sizeof(dns_hook_data_t));
            s_hook_addrs[i].inuse = 0;
        }
    }
    s_dns_hook_count = 0;
    spin_unlock(&s_dns_hook_lock);
    return 0;
}

static int oplus_dns_hook_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info)
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

    switch (nla->nla_type) {
    case OPLUS_DNS_HOOK_MSG_ENABLE:
        ret = dns_hook_set_enable(nla);
        break;
    case OPLUS_DNS_HOOK_MSG_ADD_URL:
        ret = add_dns_hook_url(nla);
        break;
    case OPLUS_DNS_HOOK_MSG_DEL_URL:
        ret = del_dns_hook_url(nla);
        break;
    case OPLUS_DNS_HOOK_MSG_CLEAR_URL:
        ret = clear_dns_hook_url(nla);
        break;
    default:
        return -EINVAL;
    }
    return ret;
}

static const struct genl_ops oplus_dns_hook_genl_ops[] = {
    {
        .cmd = OPLUS_DNS_HOOK_CMD_SET,
        .flags = 0,
        .doit = oplus_dns_hook_netlink_rcv_msg,
        .dumpit = NULL,
    },
};

static struct genl_family oplus_dns_hook_genl_family = {
    .id = 0,
    .hdrsize = 0,
    .name = OPLUS_DNS_HOOK_FAMILY_NAME,
    .version = OPLUS_DNS_HOOK_FAMILY_VERSION,
    .maxattr = OPLUS_DNS_HOOK_MSG_MAX,
    .ops = oplus_dns_hook_genl_ops,
    .n_ops = ARRAY_SIZE(oplus_dns_hook_genl_ops),
};

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid,
    struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);

	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &oplus_dns_hook_genl_family, 0, cmd);
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


static int send_netlink_data(int type, char *data, int len)
{
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
	ret = genl_msg_prepare_usr_msg(OPLUS_DNS_HOOK_CMD_NOTIFY, size, s_user_pid, &skbuff);
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
	if (ret < 0) {
		LOGK(1, "genlmsg_unicast return error, ret = %d\n", ret);
		return -1;
	}
	return 0;
}


static int send_first_dns_event(char *hostname)
{
	uint32_t len = 0;
	char *data = NULL;
	int ret = 0;
	uint32_t nameLen = 0;

	LOGK(1, "send_first_dns_event %s", hostname);
	len = sizeof(uint32_t) + strlen(hostname) + 1;
	data = kmalloc(len, GFP_ATOMIC);
	if (data == NULL) {
		LOGK(1, "malloc failed %u", len);
		return -1;
	}

	memset(data, 0, len);
	nameLen = strlen(hostname);
	memcpy(data, &nameLen, sizeof(uint32_t));
	strcpy(data + sizeof(uint32_t), hostname);
	ret = send_netlink_data(OPLUS_DNS_HOOK_MSG_FIRST_NOTIFY, data, len - 1);
	kfree(data);
	LOGK(1, "send_netlink_data return %d", ret);

	return ret;
}


static int oplus_dns_hook_netlink_init(void)
{
    int ret;
    ret = genl_register_family(&oplus_dns_hook_genl_family);
    if (ret) {
        LOGK(1, "genl_register_family:%s failed,ret = %d\n", OPLUS_DNS_HOOK_FAMILY_NAME, ret);
        return ret;
    } else {
        LOGK(1, "genl_register_family complete, id = %d!\n", oplus_dns_hook_genl_family.id);
    }

    return 0;
}

static void oplus_dns_hook_netlink_exit(void) { genl_unregister_family(&oplus_dns_hook_genl_family); }

static struct ctl_table oplus_net_hook_sysctl_table[] = {
    {
        .procname = "debug",
        .data = &s_debug,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname = "enable",
        .data = &s_dns_hook_enable,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname = "hook_count",
        .data = &s_dns_hook_count,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname = "first_notify",
        .data = &s_first_notify,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },
    {}
};

static int oplus_dns_hook_sysctl_init(void)
{
    oplus_dns_hook_table_hdr = register_net_sysctl(&init_net, "net/oplus_dns_hook", oplus_net_hook_sysctl_table);
    return oplus_dns_hook_table_hdr == NULL ? -ENOMEM : 0;
}

static int __init oplus_dns_hook_init(void)
{
    int ret = 0;

    memset(s_hook_addrs, 0, sizeof(s_hook_addrs));
    spin_lock_init(&s_dns_hook_lock);

    ret = oplus_dns_hook_netlink_init();
    if (ret < 0) {
        LOGK(1, "init module failed to init netlink, ret =%d", ret);
        return ret;
    } else {
        LOGK(1, "init module init netlink successfully.");
    }

    ret = nf_register_net_hooks(&init_net, oplus_dns_hook_netfilter_ops, ARRAY_SIZE(oplus_dns_hook_netfilter_ops));
    if (ret < 0) {
        LOGK(1, "oplus_dns_hook_init netfilter register failed, ret=%d", ret);
        oplus_dns_hook_netlink_exit();
        return ret;
    } else {
        LOGK(1, "oplus_dns_hook_init netfilter register successfully.");
    }

    oplus_dns_hook_sysctl_init();
    return ret;
}

static void __exit oplus_dns_hook_fini(void)
{
    LOGK(1, "oplus_dns_hook_fini.");
    oplus_dns_hook_netlink_exit();
    nf_unregister_net_hooks(&init_net, oplus_dns_hook_netfilter_ops, ARRAY_SIZE(oplus_dns_hook_netfilter_ops));
    if (oplus_dns_hook_table_hdr) {
        unregister_net_sysctl_table(oplus_dns_hook_table_hdr);
    }
}

MODULE_LICENSE("GPL");
module_init(oplus_dns_hook_init);
module_exit(oplus_dns_hook_fini);

