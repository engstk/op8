#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/ptrace.h>
#include <net/net_namespace.h>
#include <net/genetlink.h>
#include <linux/version.h>
#include <soc/oplus/system/olc.h>

int olc_proc_debug_init(void);
int olc_proc_debug_done(void);

#define OLC_GENL_FAMILY_NAME "olc"
#define OLC_GENL_FAMILY_VERSION 1

#define DEFAULT_REPORT_INTERVAL_SECONDS 60

enum olc_attr_type {
    OLC_ATTR_UNSPEC,
    OLC_ATTR_ENABLE_REPORT,
    OLC_ATTR_STATISTIC_DATA,
    OLC_ATTR_EXCEPTION_INFO,
    OLC_ATTR_NOTIFY_USER_INFO,
    __OLC_ATTR_MAX,
    OLC_ATTR_MAX = __OLC_ATTR_MAX - 1
};

enum olc_cmd_type {
    OLC_CMD_UNSPEC,
    OLC_CMD_ENABLE_REPORT,
    OLC_CMD_GET_RECORD,
    OLC_CMD_NEW_EXCEPTION,
    OLC_CMD_NOTIFY_USER_INFO,
    __OLC_CMD_MAX,
    OLC_CMD_MAX = __OLC_CMD_MAX - 1
};

struct exception_element {
    u32 exception_id;
    u32 dropped;
    u64 expire;
};

struct exception_node {
    struct list_head list;
    struct exception_element data;
};

struct flow_control_param {
    u64 last_event;
    u32 interval_second;
    struct mutex lock;
    struct list_head exception_list;
};

struct statistic_header {
    u32 num_elements;
    u32 element_size;
};

static int enabled = 1;
static u32 olc_user_pid = 0;
static struct flow_control_param flow_control;

static struct nla_policy olc_genl_policy[OLC_ATTR_MAX+1] = {
    [OLC_ATTR_ENABLE_REPORT] = { .type = NLA_U32 },
    //[OLC_ATTR_STATISTIC_DATA] = NLA_POLICY_MIN_LEN(sizeof(struct statistic_header))
    [OLC_ATTR_STATISTIC_DATA] = { .type = NLA_BINARY},
    //[OLC_ATTR_EXCEPTION_INFO] = NLA_POLICY_EXACT_LEN(sizeof(struct exception_info)),
    [OLC_ATTR_EXCEPTION_INFO] = {.type = NLA_BINARY, .len = sizeof(struct exception_info)},
    [OLC_ATTR_NOTIFY_USER_INFO] = { .type = NLA_NUL_STRING },
};

static struct genl_family olc_genl_family;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#ifndef _STRUCT_TIMESPEC
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static void do_gettimeofday(struct timeval *tv)
{
    struct timespec64 now;

    ktime_get_real_ts64(&now);
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_nsec / 1000;
}
#endif

static int olc_netlink_enable(struct sk_buff *skb, struct genl_info *info)
{
    struct nlmsghdr *nlhdr = NULL;
    if (info->attrs[OLC_ATTR_ENABLE_REPORT] == NULL)
    {
        return -EINVAL;
    }

    if (olc_user_pid == 0)
    {
        nlhdr = nlmsg_hdr(skb);
        olc_user_pid = nlhdr->nlmsg_pid;
    }

    enabled = nla_get_u32(info->attrs[OLC_ATTR_ENABLE_REPORT]);
    return 0;
}

static int olc_netlink_notify_user_info(struct sk_buff *skb, struct genl_info *info)
{
    struct nlmsghdr *nlhdr = NULL;
    if (info->attrs[OLC_ATTR_NOTIFY_USER_INFO] == NULL)
    {
        return -EINVAL;
    }
    nlhdr = nlmsg_hdr(skb);
    olc_user_pid = nlhdr->nlmsg_pid;

    return 0;
}


static int olc_netlink_get_record(struct sk_buff *skb, struct genl_info *info)
{
    struct sk_buff *msg = NULL;
    void *msg_head = NULL;
    struct exception_node *node = NULL;
    struct statistic_header payload_head;

    msg = nlmsg_new(4096, GFP_KERNEL);
    if (!msg)
    {
        return -ENOMEM;
    }

    msg_head = genlmsg_put(msg, olc_user_pid, 0, &olc_genl_family, 0, OLC_CMD_GET_RECORD);
    if (msg_head == NULL)
    {
        pr_err("[olc] genlmsg_put failed. \n");
        nlmsg_free(msg);
        return -ENOMEM;
    }

    payload_head.num_elements = 0;
    payload_head.element_size = sizeof(struct exception_element);
    mutex_lock(&flow_control.lock);
    list_for_each_entry(node, &flow_control.exception_list, list)
    {
        payload_head.num_elements++;
    }

    if (nla_put(msg, OLC_ATTR_STATISTIC_DATA, sizeof(struct statistic_header), &payload_head) != 0)
    {
        pr_err("[olc] nla_put payload_head failed. \n");
        genlmsg_cancel(msg, msg_head);
        mutex_unlock(&flow_control.lock);
        return -EMSGSIZE;
    }

    list_for_each_entry(node, &flow_control.exception_list, list)
    {
        if (nla_put(msg, OLC_ATTR_STATISTIC_DATA, sizeof(struct exception_element), &node->data) != 0)
        {
            pr_err("[olc] nla_put exception_element failed. \n");
            genlmsg_cancel(msg, msg_head);
            mutex_unlock(&flow_control.lock);
            return -EMSGSIZE;
        }
        node->data.dropped = 0;
    }
    mutex_unlock(&flow_control.lock);

    genlmsg_end(msg, msg_head);
    return genlmsg_reply(msg, info);
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct genl_ops olc_genl_ops[] = {
    {
        .cmd = OLC_CMD_ENABLE_REPORT,
        .flags = 0,
        .doit = olc_netlink_enable,
        .dumpit = NULL,
    },
    {
        .cmd = OLC_CMD_GET_RECORD,
        .flags = 0,
        .doit = olc_netlink_get_record,
        .dumpit = NULL,
    },
    {
        .cmd = OLC_CMD_NOTIFY_USER_INFO,
        .flags = 0,
        .doit = olc_netlink_notify_user_info,
        .dumpit = NULL,
    }
};

static struct genl_family olc_genl_family = {
    .id         = 0,
    .hdrsize    = 0,
    .name       = OLC_GENL_FAMILY_NAME,
    .version    = OLC_GENL_FAMILY_VERSION,
    .maxattr    = OLC_ATTR_MAX,
    .policy     = olc_genl_policy,
    .ops        = olc_genl_ops,
    .n_ops      = ARRAY_SIZE(olc_genl_ops),
};
#else
static struct genl_ops olc_genl_ops[] = {
    {
        .cmd = OLC_CMD_ENABLE_REPORT,
        .flags = 0,
        .doit = olc_netlink_enable,
        .policy = olc_genl_policy,
        .dumpit = NULL,
    },
    {
        .cmd = OLC_CMD_GET_RECORD,
        .flags = 0,
        .doit = olc_netlink_get_record,
        .policy = olc_genl_policy,
        .dumpit = NULL,
    },
    {
        .cmd = OLC_CMD_NOTIFY_USER_INFO,
        .flags = 0,
        .doit = olc_netlink_notify_user_info,
        .policy = olc_genl_policy,
        .dumpit = NULL,
    }
};

static struct genl_family olc_genl_family = {
    .id         = 0,
    .hdrsize    = 0,
    .name       = OLC_GENL_FAMILY_NAME,
    .version    = OLC_GENL_FAMILY_VERSION,
    .maxattr    = OLC_ATTR_MAX,
   // .policy     = olc_genl_policy,
    .ops        = olc_genl_ops,
    .n_ops      = ARRAY_SIZE(olc_genl_ops),
};
#endif


static int olc_genl_init(void)
{
    int ret = -1;

    ret = genl_register_family(&olc_genl_family);
    if (ret)
    {
        pr_err("[olc] genl_register_family:%s failed, ret=%d \n", olc_genl_family.name, ret);
        return ret;
    }

    pr_info("[olc] genl_register_family complete, id=%d \n", olc_genl_family.id);
    return 0;
}

static int olc_genl_exit(void)
{
    return genl_unregister_family(&olc_genl_family);
}

static int flow_control_init(void)
{
    memset(&flow_control, 0, sizeof(flow_control));
    flow_control.interval_second = DEFAULT_REPORT_INTERVAL_SECONDS;
    mutex_init(&flow_control.lock);
    INIT_LIST_HEAD(&flow_control.exception_list);
    return 0;
}

static int flow_control_check(int exp_id)
{
    int ret = -1;
    struct exception_node *node = NULL;
    int found = 0;

    // FIXME: need to free node sometimes
    flow_control.last_event = jiffies;
    mutex_lock(&flow_control.lock);
    list_for_each_entry(node, &flow_control.exception_list, list)
    {
        if (node->data.exception_id == exp_id)
        {
            found = 1;
            if ((enabled == 0) || (node->data.expire > jiffies))
            {
                node->data.dropped += 1;
                ret = -1;
            }
            else
            {
                node->data.expire = jiffies + flow_control.interval_second * HZ;
                ret = 0;
            }
            break;
        }
    }

    if (!found)
    {
        ret = (enabled == 1)?0:-1;
        node = kmalloc(sizeof(struct exception_node), GFP_KERNEL);
        if (node == NULL)
        {
            pr_err("[olc] kmalloc exception:%d node failed.\n", exp_id);
            mutex_unlock(&flow_control.lock);
            return ret;
        }
        node->data.exception_id = exp_id;
        node->data.dropped = (enabled == 1)?0:1;
        node->data.expire = jiffies + flow_control.interval_second * HZ;
        list_add(&node->list, &flow_control.exception_list);
    }
    mutex_unlock(&flow_control.lock);

    return ret;
}

static int flow_control_exit(void)
{
    struct exception_node *node = NULL;
    struct exception_node *next = NULL;

    mutex_lock(&flow_control.lock);
    list_for_each_entry_safe(node, next, &flow_control.exception_list, list)
    {
        list_del(&node->list);
        kfree(node);
    }
    mutex_unlock(&flow_control.lock);
    mutex_destroy(&flow_control.lock);

    return 0;
}

static int olc_netlink_send_msg(u8 cmd, int attrType, void *data, size_t len)
{
    int ret = -1;
    struct sk_buff *msg = NULL;
    void *msg_head = NULL;
    size_t size;

    size = nla_total_size(len);
    msg = genlmsg_new(size, GFP_KERNEL);
    if (msg == NULL)
    {
        pr_err("[olc] genlmsg_new failed. \n");
        ret = -ENOMEM;
        goto error;
    }

    msg_head = genlmsg_put(msg, olc_user_pid, 0, &olc_genl_family, 0, cmd);
    if (msg_head == NULL)
    {
        pr_err("[olc] genlmsg_put failed. \n");
        ret = -ENOMEM;
        nlmsg_free(msg);
        goto error;
    }

    ret = nla_put(msg, attrType, sizeof(struct exception_info), data);
    if (ret < 0)
    {
        pr_err("[olc] nla_put failed, ret=%d\n", ret);
        nlmsg_free(msg);
        goto error;
    }

    genlmsg_end(msg, msg_head);
    ret = genlmsg_unicast(&init_net, msg, olc_user_pid);
    if (ret < 0)
    {
        pr_err("[olc] genlmsg_unicast failed, ret=%d\n", ret);
    }

error:
    return ret;
}

int olc_raise_exception(struct exception_info *exp)
{
    struct timeval time;

    if (exp == NULL)
    {
        pr_err("[olc] %s: invalid param, null pointer.\n", __func__);
        return -EINVAL;
    }

    if (exp->time == 0)
    {
        do_gettimeofday(&time);
        exp->time = time.tv_sec;
    }


    if (flow_control_check(exp->exceptionId) != 0)
    {
        pr_notice("[olc] drop exception 0x%x\n", exp->exceptionId);
        return -1;
    }

    pr_notice("[olc]exception:id=0x%x,time=%ld,level=%d,atomicLogs=0x%lx,logParams=%s\n",
                exp->exceptionId, exp->time, exp->level, exp->atomicLogs, exp->logParams);
    return olc_netlink_send_msg(OLC_CMD_NEW_EXCEPTION, OLC_ATTR_EXCEPTION_INFO, (void *)exp, sizeof(struct exception_info));
}
EXPORT_SYMBOL(olc_raise_exception);

static int __init olc_init(void)
{
    int ret = -1;

    flow_control_init();
    ret = olc_genl_init();
    if (ret)
    {
        return ret;
    }

    olc_proc_debug_init();
    return 0;
}

static void __exit olc_exit(void)
{
    olc_proc_debug_done();
    olc_genl_exit();
    flow_control_exit();
    return;
}

module_init(olc_init);
module_exit(olc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OPLUS LOG CORE Driver");
MODULE_AUTHOR("OPLUS Inc.");
