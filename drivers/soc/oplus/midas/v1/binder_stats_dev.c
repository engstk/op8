// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/cpufreq_times.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/notifier.h>
#include <linux/rbtree.h>
#include <linux/android/binder.h>
#include <linux/hashtable.h>

#if defined(CONFIG_OPLUS_FEATURE_BINDER_STATS_ENABLE)

#define BINDER_STATS_LOGI(...)
#define BINDER_STATS_LOGE pr_err

#define BINDER_STATS_CTL_VERSION_CODE 1

#define BINDER_STATS_CTL_GET_VERSION 100
#define BINDER_STATS_CTL_ENABLE 101
#define BINDER_STATS_CTL_UPDATE 102
#define BINDER_STATS_CTL_CFG_CLEAR 110
#define BINDER_STATS_CTL_CFG_MAX_ITEM 111
#define BINDER_STATS_CTL_CFG_SVR_FILTER_NAME 112
#define BINDER_STATS_CTL_CFG_SVR_FILTER_PROC_COMM 113
#define BINDER_STATS_CTL_CFG_SVR_FILTER_UID 114
#define BINDER_STATS_CTL_CFG_ENABLE_BINDER_COMM 115
#define BINDER_STATS_CTL_RET_SUCC 0
#define BINDER_STATS_CTL_RET_INVALID -1

#define OPLUS_MAX_SERVICE_NAME_LEN 32
#define BINDER_STATS_MAX_COUNT_LIMIT 32768
#define BINDER_STATS_DEFAULT_MAX_COUNT 4096
#define BINDER_STATS_HASH_ORDER 9
#define BINDER_STATS_FILTER_LIMIT_MAX 128

/* import from binder driver */
struct binder_notify {
	struct task_struct *caller_task;
	struct task_struct *binder_task;
	char service_name[OPLUS_MAX_SERVICE_NAME_LEN];
	bool pending_async;
};
extern int register_binderevent_notifier(struct notifier_block *nb);
extern int unregister_binderevent_notifier(struct notifier_block *nb);

static int binder_notify_fn(struct notifier_block *nb, unsigned long action, void *data);
static struct notifier_block binder_nb = {
	 .notifier_call = binder_notify_fn,
};

struct binder_stats_item {
	char caller_proc_comm[TASK_COMM_LEN];
	int caller_pid;
	int caller_tgid;
	int caller_uid;
	char caller_comm[TASK_COMM_LEN];
	char service_name[OPLUS_MAX_SERVICE_NAME_LEN];
	char binder_proc_comm[TASK_COMM_LEN];
	int binder_pid;
	int binder_tgid;
	int binder_uid;
	char binder_comm[TASK_COMM_LEN];
	unsigned int call_count;
};

/*
 * binder_stats : user space mmap the buffer struct.
 * this struct buffer is dynamic alloc,
 * items size is max_item_cnt, it is config by user.
 * items size BINDER_STATS_MAX_COUNT_LIMIT is invalid.
 */
struct binder_stats {
	unsigned int max_item_cnt;
	unsigned int valid_item_cnt;
	struct binder_stats_item items[BINDER_STATS_MAX_COUNT_LIMIT];
};

struct binder_stats_item_ref {
	struct hlist_node hentry;
	struct binder_stats_item *item;
};

struct binder_stats_driver {
	dev_t dev;
	struct cdev cdev;
	struct class *dev_class;
	struct device *device;
	struct mutex lock;
	int version_code;

	struct list_head user_list_head;
	bool regist_binder_stats_flag;
	spinlock_t user_list_lock;
};

struct binder_stats_filter_srv_name {
	char service_name[OPLUS_MAX_SERVICE_NAME_LEN];
	bool intreresting;
};

struct binder_stats_filter_proc_comm {
	char comm[TASK_COMM_LEN];
	bool intreresting;
};

struct binder_stats_filter_uid {
	int uid;
	bool intreresting;
};

struct binder_stats_filter_srv_name_node {
	struct hlist_node hentry;
	char service_name[OPLUS_MAX_SERVICE_NAME_LEN];
	bool intreresting;
};

struct binder_stats_filter_proc_comm_node {
	struct hlist_node hentry;
	char comm[TASK_COMM_LEN];
	bool intreresting;
};

struct binder_stats_filter_uid_node {
	struct hlist_node hentry;
	int uid;
	bool intreresting;
};

struct binder_stats_user_context {
	struct list_head list_node;
	int enable;
	int max_item_cnt;
	struct binder_stats *binder_stats_buf_0; /* double swap buffer 0 */
	struct binder_stats *binder_stats_buf_1; /* double swap buffer 1 */
	struct binder_stats_item_ref *kernel_binder_stats_refs;
	DECLARE_HASHTABLE(kernel_binder_stats_hash, BINDER_STATS_HASH_ORDER);
	struct binder_stats *kernel_binder_stats;
	struct binder_stats *user_binder_stats;
	bool user_mmap_flag;
	spinlock_t buf_lock;

	int enable_binder_comm;
	DECLARE_HASHTABLE(intre_srv_name_hash, BINDER_STATS_HASH_ORDER);
	bool has_intr_srv_name;
	bool has_unintr_srv_name;
	DECLARE_HASHTABLE(intre_proc_comm_hash, BINDER_STATS_HASH_ORDER);
	bool has_intr_proc_comm;
	bool has_unintr_proc_comm;
	DECLARE_HASHTABLE(intre_uid_hash, BINDER_STATS_HASH_ORDER);
	bool has_intr_uid;
	bool has_unintr_uid;
};

struct binder_stats_driver g_binder_stats_driver;

/* key is from caller_comm & binder_comm && service_name */
static inline long long hash_key(struct binder_notify *bn, int enable_binder_comm) {
	long long key = 0;
	int i;
	long long *v1 = (long long *)bn->caller_task->comm;
	long long *v2 = (long long *)bn->binder_task->comm;
	long long *v3 = (long long *)bn->service_name;
	for (i = 0; i < TASK_COMM_LEN/sizeof(long long); ++i) {
		if (1 == enable_binder_comm)
			key += (v1[i] + v2[i]);
		else
			key += v1[i];
	}
	for (i = 0; i < OPLUS_MAX_SERVICE_NAME_LEN/sizeof(long long); ++i) {
		key += v3[i];
	}
	return key;
}

static inline bool hash_match(struct binder_notify *bn, struct binder_stats_item *stats_item, int enable_binder_comm) {
	struct task_struct *caller_proc_task = NULL;
	struct task_struct *binder_proc_task = NULL;
	if (NULL != bn->caller_task->group_leader)
		caller_proc_task = bn->caller_task->group_leader;
	else
		caller_proc_task = bn->caller_task;

	if (NULL != bn->binder_task->group_leader)
		binder_proc_task = bn->binder_task->group_leader;
	else
		binder_proc_task = bn->binder_task;

	if (1 == enable_binder_comm)
		return (0 == strncmp(stats_item->caller_comm, bn->caller_task->comm, TASK_COMM_LEN)
			&& 0 == strncmp(stats_item->caller_proc_comm, caller_proc_task->comm, TASK_COMM_LEN)
			&& 0 == strncmp(stats_item->service_name, bn->service_name, OPLUS_MAX_SERVICE_NAME_LEN)
			&& 0 == strncmp(stats_item->binder_comm, bn->binder_task->comm, TASK_COMM_LEN)
			&& 0 == strncmp(stats_item->binder_proc_comm, binder_proc_task->comm, TASK_COMM_LEN));
	else
		return (0 == strncmp(stats_item->caller_comm, bn->caller_task->comm, TASK_COMM_LEN)
			 && 0 == strncmp(stats_item->caller_proc_comm, caller_proc_task->comm, TASK_COMM_LEN)
			 && 0 == strncmp(stats_item->service_name, bn->service_name, OPLUS_MAX_SERVICE_NAME_LEN)
			 && 0 == strncmp(stats_item->binder_proc_comm, binder_proc_task->comm, TASK_COMM_LEN));
}

static inline long long hash_key_for_str(const char *str, unsigned int len) {
	long long key = 0;
	int i;
	long long *v = (long long *)str;
	for (i = 0; i < len/sizeof(long long); ++i) {
		key += v[i];
		str += sizeof(long long);
	}
	for (i = 0; i < len%sizeof(long long); ++i) {
		key += str[i];
	}
	return key;
}

static bool intreresting_filter(struct binder_stats_user_context *context_ptr,
	struct binder_notify *bn) {
	bool intreresting = false;
	struct task_struct *caller_task;
	struct task_struct *binder_proc_task;
	int binder_uid;
	struct binder_stats_filter_srv_name_node *hash_node_srv_name;
	struct binder_stats_filter_proc_comm_node *hash_node_proc_comm;
	struct binder_stats_filter_uid_node *hash_node_uid;

	caller_task = bn->caller_task;
	binder_proc_task = NULL == bn->binder_task->group_leader? bn->binder_task: bn->binder_task->group_leader;
	binder_uid = from_kuid_munged(current_user_ns(), task_uid(bn->binder_task));

	if (!context_ptr->has_intr_srv_name && !context_ptr->has_intr_proc_comm && !context_ptr->has_intr_uid) {
		intreresting = true;
	} else {
		intreresting = false;
	}

	hash_for_each_possible(context_ptr->intre_srv_name_hash, hash_node_srv_name, hentry,
		hash_key_for_str(bn->service_name, strlen(bn->service_name))) {
		if (0 == strcmp(hash_node_srv_name->service_name, bn->service_name)) {
			if (hash_node_srv_name->intreresting) {
				intreresting = true;
			} else {
				return false;
			}
		}
	}

	hash_for_each_possible(context_ptr->intre_proc_comm_hash, hash_node_proc_comm, hentry,
		hash_key_for_str(binder_proc_task->comm, strlen(binder_proc_task->comm))) {
		if (0 == strcmp(hash_node_proc_comm->comm, binder_proc_task->comm)) {
			if (hash_node_proc_comm->intreresting) {
				intreresting = true;
			} else {
				return false;
			}
		}
	}

	hash_for_each_possible(context_ptr->intre_uid_hash, hash_node_uid, hentry, (long long)binder_uid) {
		if (hash_node_uid->uid == binder_uid) {
			return hash_node_uid->intreresting;
			if (hash_node_uid->intreresting) {
				intreresting = true;
			} else {
				return false;
			}
		}
	}

	return intreresting;
}

static void store_binder_stats_to_kernel(struct binder_notify *data) {
	struct binder_stats_item *find_item = NULL;
	unsigned long flags, ctx_flag;
	struct binder_stats *kernel_binder_stats = NULL;
	struct binder_stats_item_ref *ref_hash_node;
	struct binder_stats_user_context *context_ptr = NULL;
	struct task_struct *caller_task = NULL;
	struct task_struct *binder_task = NULL;

	if (NULL == data || NULL == data->caller_task || NULL == data->binder_task)
		return;

	caller_task = data->caller_task;
	binder_task = data->binder_task;

	/* BINDER_STATS_LOGI("%d(%s) => (%s)(%s) %d(%s)\n", caller_task->pid, caller_task->comm,
			data->service_name, binder_task->comm, binder_task->pid, binder_task->comm); */

	spin_lock_irqsave(&g_binder_stats_driver.user_list_lock, flags);

	list_for_each_entry(context_ptr, &g_binder_stats_driver.user_list_head, list_node) {
		if (NULL == context_ptr || NULL == context_ptr->kernel_binder_stats ||
				NULL == context_ptr->kernel_binder_stats_refs)
			continue;

		find_item = NULL;

		if (!intreresting_filter(context_ptr, data))
			continue;

		spin_lock_irqsave(&context_ptr->buf_lock, ctx_flag);

		kernel_binder_stats = context_ptr->kernel_binder_stats;

		/* find current item with hash */
		hash_for_each_possible(context_ptr->kernel_binder_stats_hash, ref_hash_node, hentry, hash_key(data, context_ptr->enable_binder_comm)) {
			if (hash_match(data, ref_hash_node->item, context_ptr->enable_binder_comm)) {
				find_item = ref_hash_node->item;
				break;
			}
		}

		if (NULL != find_item) {
			find_item->call_count++;
		} else {
			if (kernel_binder_stats->valid_item_cnt + 1 <= kernel_binder_stats->max_item_cnt) {
				find_item = &(kernel_binder_stats->items[kernel_binder_stats->valid_item_cnt]);

				/* caller proc comm */
				if (NULL != caller_task->group_leader)
					strncpy(find_item->caller_proc_comm, caller_task->group_leader->comm, TASK_COMM_LEN);
				else
					strncpy(find_item->caller_proc_comm, caller_task->comm, TASK_COMM_LEN);

				/* caller pid tgid uid */
				find_item->caller_pid = task_pid_nr(caller_task);
				find_item->caller_tgid = task_tgid_nr(caller_task);
				find_item->caller_uid = from_kuid_munged(current_user_ns(), task_uid(caller_task));
				/* caller comm */
				strncpy(find_item->caller_comm, caller_task->comm, TASK_COMM_LEN);

				/* service name */
				strncpy(find_item->service_name, data->service_name, OPLUS_MAX_SERVICE_NAME_LEN);

				/* binder proc comm */
				if (NULL != binder_task->group_leader)
					strncpy(find_item->binder_proc_comm, binder_task->group_leader->comm, TASK_COMM_LEN);
				else
					strncpy(find_item->binder_proc_comm, binder_task->comm, TASK_COMM_LEN);

				/* binder tgid uid */
				find_item->binder_tgid = task_tgid_nr(binder_task);
				find_item->binder_uid = from_kuid_munged(current_user_ns(), task_uid(binder_task));

				/* binder comm pid */
				if (1 == context_ptr->enable_binder_comm) {
					find_item->binder_pid = task_pid_nr(binder_task);
					strncpy(find_item->binder_comm, binder_task->comm, TASK_COMM_LEN);
				} else {
					find_item->binder_pid = find_item->binder_tgid;
					strncpy(find_item->binder_comm, "binderTh", TASK_COMM_LEN);
				}

				/* call count init */
				find_item->call_count = 1;

				/* add hash for improve search performance */
				ref_hash_node = &(context_ptr->kernel_binder_stats_refs[kernel_binder_stats->valid_item_cnt]);
				ref_hash_node->item = find_item;
				hash_add(context_ptr->kernel_binder_stats_hash, &ref_hash_node->hentry, hash_key(data, context_ptr->enable_binder_comm));

				kernel_binder_stats->valid_item_cnt++;
			}
		}

		spin_unlock_irqrestore(&context_ptr->buf_lock, ctx_flag);
	}

	spin_unlock_irqrestore(&g_binder_stats_driver.user_list_lock, flags);
}

static void binder_stats_clear_user_context(struct binder_stats_user_context *context_ptr) {
	unsigned long flags;
	struct binder_stats_item_ref *ref_hash_node;
	struct hlist_node *tmp;
	int i;

	BINDER_STATS_LOGI("start\n");

	/* first remove user node from global user list
		then clear the local user memory */

	spin_lock_irqsave(&g_binder_stats_driver.user_list_lock, flags);
	list_del(&context_ptr->list_node);
	spin_unlock_irqrestore(&g_binder_stats_driver.user_list_lock, flags);

	if (!IS_ERR_OR_NULL(context_ptr->user_binder_stats)) {
		vfree(context_ptr->user_binder_stats);
		context_ptr->user_binder_stats = NULL;
	}

	hash_for_each_safe(context_ptr->kernel_binder_stats_hash, i, tmp, ref_hash_node, hentry) {
		hash_del(&ref_hash_node->hentry);
	}
	if (!IS_ERR_OR_NULL(context_ptr->kernel_binder_stats_refs)) {
		vfree(context_ptr->kernel_binder_stats_refs);
		context_ptr->kernel_binder_stats_refs = NULL;
	}
	if (!IS_ERR_OR_NULL(context_ptr->kernel_binder_stats)) {
		vfree(context_ptr->kernel_binder_stats);
		context_ptr->kernel_binder_stats = NULL;
	}

	context_ptr->binder_stats_buf_0 = NULL;
	context_ptr->binder_stats_buf_1 = NULL;
	context_ptr->user_mmap_flag = false;

	context_ptr->enable = 0;
}

static int user_enable_binder_stats(struct binder_stats_user_context *context_ptr, int enable) {
	unsigned long flags;
	struct binder_stats_item_ref *ref_hash_node;
	struct hlist_node *tmp;
	int i;
	int max_item_cnt = BINDER_STATS_DEFAULT_MAX_COUNT;
	int binder_stats_buffer_size = 0;

	if (0 != enable && 1 != enable) {
		BINDER_STATS_LOGE("enable param error!\n");
		return -1;
	}
	if (context_ptr->enable == enable) {
		BINDER_STATS_LOGE("enable value is same with current value!\n");
		return -1;
	}
	if (context_ptr->user_mmap_flag) {
		BINDER_STATS_LOGE("user mem in used!\n");
		return -1;
	}

	if (1 == enable) {
		if (NULL != context_ptr->binder_stats_buf_0 ||
			NULL != context_ptr->binder_stats_buf_1 ||
			NULL != context_ptr->kernel_binder_stats_refs) {
			BINDER_STATS_LOGE("context_ptr buffer exist!\n");
			return -1;
		}

		if (0 != context_ptr->max_item_cnt) {
			max_item_cnt = context_ptr->max_item_cnt;
		}

		binder_stats_buffer_size = 2*sizeof(unsigned int) + max_item_cnt*sizeof(struct binder_stats_item);

		context_ptr->binder_stats_buf_0 = vmalloc_user(binder_stats_buffer_size);
		if (IS_ERR_OR_NULL(context_ptr->binder_stats_buf_0)) {
			BINDER_STATS_LOGE("malloc failed!\n");
			goto enable_fail;
		}
		context_ptr->binder_stats_buf_0->max_item_cnt = max_item_cnt;
		context_ptr->binder_stats_buf_0->valid_item_cnt = 0;
		context_ptr->kernel_binder_stats = context_ptr->binder_stats_buf_0;

		context_ptr->kernel_binder_stats_refs = vmalloc_user(max_item_cnt*sizeof(struct binder_stats_item_ref));
		if (IS_ERR_OR_NULL(context_ptr->kernel_binder_stats_refs)) {
			BINDER_STATS_LOGE("malloc failed!\n");
			goto enable_fail;
		}

		hash_init(context_ptr->kernel_binder_stats_hash);

		context_ptr->binder_stats_buf_1 = vmalloc_user(binder_stats_buffer_size);
		if (IS_ERR_OR_NULL(context_ptr->binder_stats_buf_1)) {
			BINDER_STATS_LOGE("malloc failed!\n");
			goto enable_fail;
		}
		context_ptr->binder_stats_buf_1->max_item_cnt = max_item_cnt;
		context_ptr->binder_stats_buf_1->valid_item_cnt = 0;
		context_ptr->user_binder_stats = context_ptr->binder_stats_buf_1;

		spin_lock_init(&context_ptr->buf_lock);
		context_ptr->user_mmap_flag = false;

		spin_lock_irqsave(&g_binder_stats_driver.user_list_lock, flags);
		list_add(&context_ptr->list_node, &g_binder_stats_driver.user_list_head);
		spin_unlock_irqrestore(&g_binder_stats_driver.user_list_lock, flags);

		context_ptr->enable = 1;

		return 0;

enable_fail:
		if (!IS_ERR_OR_NULL(context_ptr->binder_stats_buf_1)) {
			vfree(context_ptr->binder_stats_buf_1);
			context_ptr->binder_stats_buf_1 = NULL;
		}
		if (!IS_ERR_OR_NULL(context_ptr->kernel_binder_stats_refs)) {
			vfree(context_ptr->kernel_binder_stats_refs);
			context_ptr->kernel_binder_stats_refs = NULL;
		}
		if (!IS_ERR_OR_NULL(context_ptr->binder_stats_buf_0)) {
			vfree(context_ptr->binder_stats_buf_0);
			context_ptr->binder_stats_buf_0 = NULL;
		}
		context_ptr->user_binder_stats = NULL;
		context_ptr->kernel_binder_stats = NULL;
		hash_for_each_safe(context_ptr->kernel_binder_stats_hash, i, tmp, ref_hash_node, hentry) {
			hash_del(&ref_hash_node->hentry);
		}

		return -1;
	} else {
		if (NULL == context_ptr->user_binder_stats ||
			NULL == context_ptr->kernel_binder_stats ||
			NULL == context_ptr->kernel_binder_stats_refs) {
			BINDER_STATS_LOGE("context_ptr buffer not exit!\n");
			return -1;
		}

		binder_stats_clear_user_context(context_ptr);

		return 0;
	}
}

static int user_update_binder_stats(struct binder_stats_user_context *context_ptr) {
	int ret = 0;
	unsigned long flags;
	struct binder_stats * swap_tmp;
	struct binder_stats_item_ref *ref_hash_node;
	struct hlist_node *tmp;
	int i;

	if (0 == context_ptr->enable) {
		BINDER_STATS_LOGE("function not enabled!\n");
		return -1;
	}

	if (context_ptr->user_mmap_flag) {
		BINDER_STATS_LOGE("memory is used!\n");
		return -1;
	}

	spin_lock_irqsave(&context_ptr->buf_lock, flags);

	/* swap buffer & clear kernel_binder_stats & kernel_binder_stats_hash */
	if (NULL != context_ptr->kernel_binder_stats && NULL != context_ptr->user_binder_stats) {
		swap_tmp = context_ptr->kernel_binder_stats;
		context_ptr->kernel_binder_stats = context_ptr->user_binder_stats;
		context_ptr->user_binder_stats = swap_tmp;

		/* reset cnt & clear hash */
		context_ptr->kernel_binder_stats->valid_item_cnt = 0;
		hash_for_each_safe(context_ptr->kernel_binder_stats_hash, i, tmp, ref_hash_node, hentry) {
			hash_del(&ref_hash_node->hentry);
		}
	} else {
		ret = -1;
	}

	spin_unlock_irqrestore(&context_ptr->buf_lock, flags);

	return ret;
}

static int user_binder_stats_cfg_clear(struct binder_stats_user_context *context_ptr) {
	struct binder_stats_filter_srv_name_node *hash_node_srv_name;
	struct binder_stats_filter_proc_comm_node *hash_node_proc_comm;
	struct binder_stats_filter_uid_node *hash_node_uid;
	struct hlist_node *tmp;
	int i;

	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}

	hash_for_each_safe(context_ptr->intre_srv_name_hash, i, tmp, hash_node_srv_name, hentry) {
		hash_del(&hash_node_srv_name->hentry);
		vfree(hash_node_srv_name);
	}
	context_ptr->has_intr_srv_name = false;
	context_ptr->has_unintr_srv_name = false;

	hash_for_each_safe(context_ptr->intre_proc_comm_hash, i, tmp, hash_node_proc_comm, hentry) {
		hash_del(&hash_node_proc_comm->hentry);
		vfree(hash_node_proc_comm);
	}
	context_ptr->has_intr_proc_comm = false;
	context_ptr->has_unintr_proc_comm = false;

	hash_for_each_safe(context_ptr->intre_uid_hash, i, tmp, hash_node_uid, hentry) {
		hash_del(&hash_node_uid->hentry);
		vfree(hash_node_uid);
	}
	context_ptr->has_intr_uid = false;
	context_ptr->has_unintr_uid = false;

	context_ptr->enable_binder_comm = 0;

	context_ptr->max_item_cnt = 0;

	return 0;
}

static int user_binder_stats_cfg_set_max_item_cnt(struct binder_stats_user_context *context_ptr, int max_item_cnt) {
	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}
	if (max_item_cnt < 0 || max_item_cnt > BINDER_STATS_MAX_COUNT_LIMIT) {
		BINDER_STATS_LOGE("invalid max_item_cnt!\n");
		return -1;
	}

	context_ptr->max_item_cnt = max_item_cnt;

	return 0;
}

static int user_binder_stats_cfg_set_enable_binder_comm(struct binder_stats_user_context *context_ptr, int enable_binder_comm) {
	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}

	if (0 != enable_binder_comm && 1 != enable_binder_comm) {
		BINDER_STATS_LOGE("enable_binder_comm param error!\n");
		return -1;
	}

	context_ptr->enable_binder_comm = enable_binder_comm;

	return 0;
}

static int user_binder_stats_add_intreresting_svr_name(struct binder_stats_user_context *context_ptr,
	struct binder_stats_filter_srv_name *filter_srv_name) {
	int ret = 0, i = 0, cur_cnt = 0;
	struct hlist_node *tmp;
	struct binder_stats_filter_srv_name_node *hash_node_srv_name;

	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}

	hash_for_each_safe(context_ptr->intre_srv_name_hash, i, tmp, hash_node_srv_name, hentry) {
		cur_cnt++;
	}
	if (cur_cnt >= BINDER_STATS_FILTER_LIMIT_MAX) {
		BINDER_STATS_LOGE("max limit！\n");
		return -1;
	}

	hash_node_srv_name = vmalloc_user(sizeof(struct binder_stats_filter_srv_name_node));
	if (IS_ERR_OR_NULL(hash_node_srv_name)) {
		BINDER_STATS_LOGE("malloc err\n");
		return -1;
	}
	strncpy(hash_node_srv_name->service_name, filter_srv_name->service_name, OPLUS_MAX_SERVICE_NAME_LEN);
	hash_node_srv_name->intreresting = filter_srv_name->intreresting;
	hash_add(context_ptr->intre_srv_name_hash, &hash_node_srv_name->hentry,
		hash_key_for_str(hash_node_srv_name->service_name, strlen(hash_node_srv_name->service_name)));

	if (hash_node_srv_name->intreresting) {
		context_ptr->has_intr_srv_name = true;
	} else {
		context_ptr->has_unintr_srv_name = true;
	}

	/* BINDER_STATS_LOGI("%s %d\n", hash_node_srv_name->service_name, hash_node_srv_name->intreresting); */

	return ret;
}

static int user_binder_stats_add_intreresting_proc_comm(struct binder_stats_user_context *context_ptr,
	struct binder_stats_filter_proc_comm *filter_proc_comm) {
	int ret = 0, i = 0, cur_cnt = 0;
	struct hlist_node *tmp;
	struct binder_stats_filter_proc_comm_node *hash_node_proc_comm;

	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}

	hash_for_each_safe(context_ptr->intre_proc_comm_hash, i, tmp, hash_node_proc_comm, hentry) {
		cur_cnt++;
	}
	if (cur_cnt >= BINDER_STATS_FILTER_LIMIT_MAX) {
		BINDER_STATS_LOGE("max limit！\n");
		return -1;
	}

	hash_node_proc_comm = vmalloc_user(sizeof(struct binder_stats_filter_proc_comm_node));
	if (IS_ERR_OR_NULL(hash_node_proc_comm)) {
		BINDER_STATS_LOGE("malloc err\n");
		return -1;
	}
	strncpy(hash_node_proc_comm->comm, filter_proc_comm->comm, TASK_COMM_LEN);
	hash_node_proc_comm->intreresting = filter_proc_comm->intreresting;
	hash_add(context_ptr->intre_proc_comm_hash, &hash_node_proc_comm->hentry,
		hash_key_for_str(hash_node_proc_comm->comm, strlen(hash_node_proc_comm->comm)));

	if (hash_node_proc_comm->intreresting) {
		context_ptr->has_intr_proc_comm = true;
	} else {
		context_ptr->has_unintr_proc_comm = true;
	}

	/*  BINDER_STATS_LOGI("%s %d\n", hash_node_proc_comm->comm, hash_node_proc_comm->intreresting); */

	return ret;
}

static int user_binder_stats_add_intreresting_uid(struct binder_stats_user_context *context_ptr,
	struct binder_stats_filter_uid *filter_uid) {
	int ret = 0, i = 0, cur_cnt = 0;
	struct hlist_node *tmp;
	struct binder_stats_filter_uid_node *hash_node_uid;

	if (1 == context_ptr->enable) {
		BINDER_STATS_LOGE("can not set config when function enabled already!\n");
		return -1;
	}

	hash_for_each_safe(context_ptr->intre_uid_hash, i, tmp, hash_node_uid, hentry) {
		cur_cnt++;
	}
	if (cur_cnt >= BINDER_STATS_FILTER_LIMIT_MAX) {
		BINDER_STATS_LOGE("max limit！\n");
		return -1;
	}

	hash_node_uid = vmalloc_user(sizeof(struct binder_stats_filter_uid_node));
	if (IS_ERR_OR_NULL(hash_node_uid)) {
		BINDER_STATS_LOGE("malloc err\n");
		return -1;
	}
	hash_node_uid->uid = filter_uid->uid;
	hash_node_uid->intreresting = filter_uid->intreresting;
	hash_add(context_ptr->intre_uid_hash, &hash_node_uid->hentry, (long long)hash_node_uid->uid);

	if (hash_node_uid->intreresting) {
		context_ptr->has_intr_uid = true;
	} else {
		context_ptr->has_unintr_uid = true;
	}

	/* BINDER_STATS_LOGI("%d %d\n", hash_node_uid->uid, hash_node_uid->intreresting); */

	return ret;
}

static int binder_notify_fn(struct notifier_block *nb, unsigned long action, void *data) {
	struct binder_notify *bn = NULL;
	if (NULL == data) {
		return 0;
	}
	bn = (struct binder_notify *)data;
	store_binder_stats_to_kernel(bn);
	return 0;
}

static int binder_stats_driver_open(struct inode *inode, struct file *filp) {
	struct binder_stats_user_context *context_ptr;

	BINDER_STATS_LOGI("start\n");

	mutex_lock(&g_binder_stats_driver.lock);

	if (!g_binder_stats_driver.regist_binder_stats_flag) {
		register_binderevent_notifier(&binder_nb);
		g_binder_stats_driver.regist_binder_stats_flag = true;
	}

	context_ptr = vmalloc(sizeof(struct binder_stats_user_context));
	if (IS_ERR_OR_NULL(context_ptr)) {
		BINDER_STATS_LOGE("vmalloc failed\n");
		goto open_fail;
	}

	memset(context_ptr, 0, sizeof(struct binder_stats_user_context));

	/* init config data */

	context_ptr->max_item_cnt = 0;

	context_ptr->enable_binder_comm = 0;

	hash_init(context_ptr->intre_srv_name_hash);
	context_ptr->has_intr_srv_name = false;
	context_ptr->has_unintr_srv_name = false;

	hash_init(context_ptr->intre_proc_comm_hash);
	context_ptr->has_intr_proc_comm = false;
	context_ptr->has_unintr_proc_comm = false;

	hash_init(context_ptr->intre_uid_hash);
	context_ptr->has_intr_uid = false;
	context_ptr->has_unintr_uid = false;

	filp->private_data = context_ptr;

	mutex_unlock(&g_binder_stats_driver.lock);

	return 0;

open_fail:

	mutex_unlock(&g_binder_stats_driver.lock);

	return -1;
}

static int binder_stats_driver_release(struct inode *ignored, struct file *filp) {
	struct binder_stats_user_context *context_ptr;
	unsigned long flags;
	bool clear_notifier = false;
	struct list_head *pos = NULL;
	int list_size = 0;

	BINDER_STATS_LOGI("start\n");

	mutex_lock(&g_binder_stats_driver.lock);

	context_ptr = filp->private_data;
	if (IS_ERR_OR_NULL(context_ptr)) {
		mutex_unlock(&g_binder_stats_driver.lock);
		return -1;
	}

	spin_lock_irqsave(&g_binder_stats_driver.user_list_lock, flags);
	list_size = 0;
	list_for_each(pos, &g_binder_stats_driver.user_list_head) {
		++list_size;
	}
	if (g_binder_stats_driver.regist_binder_stats_flag &&
			((1 == list_size && list_is_last(&context_ptr->list_node, &g_binder_stats_driver.user_list_head)) ||
				list_empty(&g_binder_stats_driver.user_list_head))) {
		clear_notifier = true;
		g_binder_stats_driver.regist_binder_stats_flag = false;
	}
	spin_unlock_irqrestore(&g_binder_stats_driver.user_list_lock, flags);

	if (clear_notifier) {
		unregister_binderevent_notifier(&binder_nb);
	}

	if (!IS_ERR_OR_NULL(context_ptr)) {
		if (1 == context_ptr->enable) {
			binder_stats_clear_user_context(context_ptr);
		}
		vfree(context_ptr);
		filp->private_data = NULL;
	}

	mutex_unlock(&g_binder_stats_driver.lock);

	return 0;
}

static void binder_stats_mmap_close(struct vm_area_struct *vma) {
	struct binder_stats_user_context *context_ptr;
	if (NULL == vma || NULL == vma->vm_file || NULL == vma->vm_file->private_data) {
		BINDER_STATS_LOGE("invalid private_data\n");
		return;
	}
	BINDER_STATS_LOGI("start\n");

	mutex_lock(&g_binder_stats_driver.lock);

	context_ptr = vma->vm_file->private_data;
	context_ptr->user_mmap_flag = false;

	mutex_unlock(&g_binder_stats_driver.lock);
}

static const struct vm_operations_struct binder_stats_mmap_vmops = {
	.close = binder_stats_mmap_close,
};

static int binder_stats_driver_mmap(struct file *filp, struct vm_area_struct *vma) {
	struct binder_stats_user_context *context_ptr;

	BINDER_STATS_LOGI("start\n");

	mutex_lock(&g_binder_stats_driver.lock);

	context_ptr = filp->private_data;

	if (NULL != context_ptr && NULL != context_ptr->user_binder_stats) {
		vma->vm_ops = &binder_stats_mmap_vmops;
		if (remap_vmalloc_range(vma, context_ptr->user_binder_stats, vma->vm_pgoff)) {
			mutex_unlock(&g_binder_stats_driver.lock);
			BINDER_STATS_LOGE("remap failed\n");
			return -EAGAIN;
		}
		context_ptr->user_mmap_flag = true;
	} else {
		mutex_unlock(&g_binder_stats_driver.lock);
		BINDER_STATS_LOGE("remap failed\n");
		return -EAGAIN;
	}

	mutex_unlock(&g_binder_stats_driver.lock);

	return 0;
}

static long binder_stats_driver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	long ret = -1;
	int enable = 0;
	int max_item_cnt = 0;
	int enable_binder_comm = 0;
	struct binder_stats_filter_srv_name filter_srv_name;
	struct binder_stats_filter_proc_comm filter_proc_comm;
	struct binder_stats_filter_uid filter_uid;
	struct binder_stats_user_context *context_ptr = filp->private_data;

	BINDER_STATS_LOGI("start cmd:%d arg:%lu\n", cmd, arg);

	if (NULL == context_ptr) {
		BINDER_STATS_LOGE("context_ptr error!\n");
		return ret;
	}

	mutex_lock(&g_binder_stats_driver.lock);

	switch (cmd) {
	case BINDER_STATS_CTL_GET_VERSION: {
			if (0 != arg) {
				if(0 == copy_to_user((unsigned int *)arg,
					&g_binder_stats_driver.version_code, sizeof(unsigned int))) {
					BINDER_STATS_LOGI("BINDER_STATS_CTL_GET_VERSION %d\n", g_binder_stats_driver.version_code);
					ret = BINDER_STATS_CTL_RET_SUCC;
				} else {
					BINDER_STATS_LOGE("BINDER_STATS_CTL_GET_VERSION failed. copy_to_user error!\n");
					ret = BINDER_STATS_CTL_RET_INVALID;
				}
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_GET_VERSION failed. arg error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_ENABLE: {
			if(0 != copy_from_user(&enable, (int *)arg, sizeof(int))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_ENABLE failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_enable_binder_stats(context_ptr, enable)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_ENABLE failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_UPDATE: {
			if (0 == user_update_binder_stats(context_ptr)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_UPDATE failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_CLEAR: {
			if (0 == user_binder_stats_cfg_clear(context_ptr)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_CLEAR failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_MAX_ITEM: {
			if(0 != copy_from_user(&max_item_cnt, (int *)arg, sizeof(int))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_MAX_ITEM failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_binder_stats_cfg_set_max_item_cnt(context_ptr, max_item_cnt)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_MAX_ITEM failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_ENABLE_BINDER_COMM: {
			if(0 != copy_from_user(&enable_binder_comm, (int *)arg, sizeof(int))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_ENABLE_BINDER_COMM failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_binder_stats_cfg_set_enable_binder_comm(context_ptr, enable_binder_comm)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_ENABLE_BINDER_COMM failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_SVR_FILTER_NAME: {
			if(0 != copy_from_user(&filter_srv_name, (char *)arg, sizeof(struct binder_stats_filter_srv_name))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_NAME failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_binder_stats_add_intreresting_svr_name(context_ptr, &filter_srv_name)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_NAME failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_SVR_FILTER_PROC_COMM: {
			if(0 != copy_from_user(&filter_proc_comm, (char *)arg, sizeof(struct binder_stats_filter_proc_comm))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_PROC_COMM failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_binder_stats_add_intreresting_proc_comm(context_ptr, &filter_proc_comm)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_PROC_COMM failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	case BINDER_STATS_CTL_CFG_SVR_FILTER_UID: {
			if(0 != copy_from_user(&filter_uid, (int *)arg, sizeof(struct binder_stats_filter_uid))) {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_UID failed. copy_to_user error!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
				break;
			}
			if (0 == user_binder_stats_add_intreresting_uid(context_ptr, &filter_uid)) {
				ret = BINDER_STATS_CTL_RET_SUCC;
			} else {
				BINDER_STATS_LOGE("BINDER_STATS_CTL_CFG_SVR_FILTER_UID failed!\n");
				ret = BINDER_STATS_CTL_RET_INVALID;
			}
		}
		break;
	default: {
			BINDER_STATS_LOGE("unknown ioctl cmd:%d\n", cmd);
			ret = BINDER_STATS_CTL_RET_INVALID;
		}
		break;
	}

	mutex_unlock(&g_binder_stats_driver.lock);

	return ret;
}

static struct file_operations io_dev_fops = {
	.owner = THIS_MODULE,
	.open = binder_stats_driver_open,
	.release = binder_stats_driver_release,
	.unlocked_ioctl = binder_stats_driver_ioctl,
	.mmap = binder_stats_driver_mmap,
};

int __init binder_stats_dev_init(void) {
	int err = 0;

	g_binder_stats_driver.version_code = BINDER_STATS_CTL_VERSION_CODE;
	mutex_init(&g_binder_stats_driver.lock);
	spin_lock_init(&g_binder_stats_driver.user_list_lock);
	INIT_LIST_HEAD(&g_binder_stats_driver.user_list_head);

	err = alloc_chrdev_region(&g_binder_stats_driver.dev, 0, 1, "binder_stats");
	if (err < 0) {
		BINDER_STATS_LOGE("failed to alloc chrdev\n");
		goto fail;
	}

	cdev_init(&g_binder_stats_driver.cdev, &io_dev_fops);

	err = cdev_add(&g_binder_stats_driver.cdev, g_binder_stats_driver.dev, 1);
	if (err < 0) {
		BINDER_STATS_LOGE("cdev_add g_binder_stats_driver.cdev failed!\n");
		goto unreg_region;
	}

	g_binder_stats_driver.dev_class = class_create(THIS_MODULE, "binder_stats_class");
	if (IS_ERR(g_binder_stats_driver.dev_class)) {
		BINDER_STATS_LOGE("create class g_binder_stats_driver.dev_class error\n");
		goto destroy_cdev;
	}

	g_binder_stats_driver.device = device_create(g_binder_stats_driver.dev_class,
		NULL, g_binder_stats_driver.dev, NULL, "binder_stats");
	if (IS_ERR(g_binder_stats_driver.device)) {
		BINDER_STATS_LOGE("device_create g_binder_stats_driver.device error\n");
		goto destroy_class;
	}

	g_binder_stats_driver.regist_binder_stats_flag = false;

	return 0;

destroy_class:
	class_destroy(g_binder_stats_driver.dev_class);

destroy_cdev:
	cdev_del(&g_binder_stats_driver.cdev);

unreg_region:
	unregister_chrdev_region(g_binder_stats_driver.dev, 1);

fail:
	return -1;
}


void __exit binder_stats_dev_exit(void) {
	if (g_binder_stats_driver.dev_class) {
		device_destroy(g_binder_stats_driver.dev_class, g_binder_stats_driver.dev);
		class_destroy(g_binder_stats_driver.dev_class);
		g_binder_stats_driver.dev_class = NULL;
	}

	cdev_del(&g_binder_stats_driver.cdev);

	unregister_chrdev_region(g_binder_stats_driver.dev, 1);
}

#else /* defined(CONFIG_OPLUS_FEATURE_BINDER_STATS_ENABLE) */

int __init binder_stats_dev_init(void) {
	return 0;
}

void __exit binder_stats_dev_exit(void) {
}

#endif /* defined(CONFIG_OPLUS_FEATURE_BINDER_STATS_ENABLE) */


module_init(binder_stats_dev_init);
module_exit(binder_stats_dev_exit);
