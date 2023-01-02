/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/********************************************************************************
 ** File: - acm.h
 ** Description:
 **     Add this file for ACM
 **
 ** Version: 1.0
 ** Date: 2020-06-26
 ** TAG: OPLUS_FEATURE_ACM
 **
 ** ------------------------------- Revision History: ----------------------------
 ** <author>                             <date>       <version>   <desc>
 ** ------------------------------------------------------------------------------
 ********************************************************************************/

#ifndef __DRIVERS_SOC_OPLUS_SYSTEM_ACM_H__
#define __DRIVERS_SOC_OPLUS_SYSTEM_ACM_H__

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <asm-generic/unistd.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/path.h>
#include <linux/mount.h>
#include <linux/fs_struct.h>
#include <uapi/linux/limits.h>
#include <uapi/asm-generic/errno.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/fuse.h>
#include <linux/acm_fs.h>

#define ACM_DEV_NAME "acm"
#define ACM_DEV_BASE_MINOR 0
#define ACM_DEV_COUNT 1

#define ACM_MAGIC 'a'
#define ACM_SET_OPCTRL _IOW(ACM_MAGIC, 0, int)
#define ACM_GET_OPCTRL _IOW(ACM_MAGIC, 1, int)
#define ACM_ADD_PKG _IOW(ACM_MAGIC, 2, struct acm_fwk_pkg)
#define ACM_DEL_PKG _IOW(ACM_MAGIC, 3, char*)
#define ACM_GET_PKG_FLAG _IOW(ACM_MAGIC, 4, struct acm_fwk_pkg)
#define ACM_ADD_DIR _IOR(ACM_MAGIC, 5, struct acm_fwk_dir)
#define ACM_DEL_DIR _IOR(ACM_MAGIC, 6, char*)
#define ACM_GET_DIR_FLAG _IOR(ACM_MAGIC, 7, struct acm_fwk_dir)
#define ACM_ADD_NOMEDIADIR _IOR(ACM_MAGIC, 8, struct acm_fwk_nomediadir)
#define ACM_DEL_NOMEDIADIR _IOR(ACM_MAGIC, 9, struct acm_fwk_nomediadir)
#define ACM_SEARCH_NOMEDIADIR _IOR(ACM_MAGIC, 10, struct acm_fwk_nomediadir)
#define ACM_CMD_MAXNR 10

#define ACM_HASHTBL_SZ 512
#define HASHTBL_MAX_SZ 4096
#define ACM_PATH_MAX 1024
#define DEPTH_INIT (-1)
#define ACM_DIR_MAX 64
#define ACM_DIR_LIST_MAX 1024
#define ACM_DNAME_MAX 256
#define ACM_LIST_MAX_NODES 2048
#define MAX_CACHED_DELETE_LOG 3
#define DELETE_LOG_UPLOAD_INTERVAL_MS 1000

#define PATH_PREFIX_MEDIA "/media"
#define PATH_PREFIX_STORAGE_EMULATED "/storage/emulated"
#define PATH_UNKNOWN "unknown_path"

#define UEVENT_KEY_STR_MAX 16
#define ENV_PKGNAME_MAX (UEVENT_KEY_STR_MAX + ACM_PKGNAME_LEN_MAX)
#define ENV_PATH_MAX 1024
#define ENV_NR_STR_MAX 32
#define ENV_DEPTH_STR_MAX 32
#define ENV_FILE_TYPE_STR_MAX 32

#define ERR_PATH_MAX_DENTRIES 6
#define ERR_PATH_LAST_DENTRY 0

#define ACM_HASH_LEFT_SHIFT 4
#define ACM_HASH_MASK 0xF0000000L
#define ACM_HASH_RESULT_MASK 0x7FFFFFFF
#define ACM_HASH_RIGHT_SHIFT 24

#define DEL_ALLOWED 0
#define ACM_SUCCESS 0

#define CRT_NOT_ALLOWED 0
#define CRT_ALLOWED 1

#define DIR_NONEED_FLAG 0
#define DIR_NEED_FLAG 1

#define ACM_PKGNAME_LEN_MAX 100
#define UID_THRESHOLD 10000

/* package name received from framework */
struct acm_fwk_pkg {
	unsigned long long flag;
	char pkgname[ACM_PKGNAME_LEN_MAX];
};

/* directory received from framework */
struct acm_fwk_dir {
	unsigned long long flag;
	char dir[ACM_DIR_MAX];
};

/* nomedia directory received from framework */
struct acm_fwk_nomediadir {
	char dir[ACM_DIR_MAX];
};

/* white list node */
struct acm_hnode {
	struct hlist_node hnode;
	struct acm_fwk_pkg afp;
};

/* a hash table for white list */
struct acm_htbl {
	struct hlist_head *head;
	spinlock_t spinlock;
	int nr_nodes;
};

/* data node for directory */
struct acm_dnode {
	struct list_head lnode;
	struct acm_fwk_dir afd;
};

/* data node for nomedia directory */
struct acm_ndnode {
	struct list_head lnode;
	struct acm_fwk_nomediadir afnd;
};

/* data node for framework and DMD */
struct acm_lnode {
	struct list_head lnode;
	char pkgname[ACM_PKGNAME_LEN_MAX];
	uid_t uid;
	char path[ACM_PATH_MAX];
	int file_type;
	int depth;
	int op;
	/*
	 * Number of deleted files in a period of time,
	 * only used in cache
	 */
	int nr;
};

struct acm_list {
	struct list_head head;
	unsigned long nr_nodes;
	spinlock_t spinlock;
};

struct acm_cache {
	struct acm_lnode cache[MAX_CACHED_DELETE_LOG];
	int count;
};

struct acm_env {
	char pkgname[ENV_PKGNAME_MAX];
	char uid[ENV_NR_STR_MAX];
	char path[ENV_PATH_MAX];
	char depth[ENV_DEPTH_STR_MAX];
	char file_type[ENV_FILE_TYPE_STR_MAX];
	char nr[ENV_NR_STR_MAX];
	char op[ENV_NR_STR_MAX];
	char *envp[UEVENT_NUM_ENVP];
};

#endif /* __DRIVERS_SOC_OPLUS_SYSTEM_ACM_H__ */
