// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/********************************************************************************
 ** File: - acm.c
 ** Description:
 **     Add this file for ACM
 **
 ** Version: 1.0
 ** Date: 2020-06-26
 ** TAG: OPLUS_FEATURE_ACM
 **
 ** ------------------------- Revision History: --------------------------------
 ** <author>                      <date>       <version>   <desc>
 ** ------------------------------------------------------------------------------
 ********************************************************************************/
#include <linux/version.h>
#include "acm.h"

#define ACM_PHOTO_TYPE 1
#define ACM_VIDEO_TYPE 2
#define ACM_NOMEDIA_TYPE 3

#ifdef CONFIG_OPLUS_FEATURE_ACM3
#define ACM_SPECIAL_TYPE 4
#define ACM_SPECIAL_RENAME_TYPE 5
#endif

#define ACM_NOMEDIA_FNAME ".nomedia"
#define MIN_FNAME_LENGTH 2

#ifndef ACM_FLAG_LOGGING
#define ACM_FLAG_LOGGING (0x01)
#endif
#ifndef ACM_FLAG_DEL
#define ACM_FLAG_DEL (0x01 << 1)
#endif
#ifndef ACM_FLAG_CRT
#define ACM_FLAG_CRT (0x01 << 2)
#endif


#ifdef CONFIG_OPLUS_FEATURE_ACM3
#define MEDIAPROVIDER "com.android.providers.media.module"
#define MEDIAPROVIDER2 "com.google.android.providers.media.module"
#endif

static int acm_flag = (ACM_FLAG_LOGGING | ACM_FLAG_DEL | ACM_FLAG_CRT);

/* Hash table for white list */
static struct acm_htbl acm_hash;

static dev_t acm_devno;
static struct cdev *acm_cdev;
static struct class *acm_class;
static struct device *acm_device;
static struct kset *acm_kset;

/* List for dir */
static struct acm_list acm_dir_list;
/* List for nomedia dir */
static struct acm_list acm_nomediadir_list;
/* List for framework */
static struct acm_list acm_fwk_list;
static struct task_struct *acm_fwk_task;
static struct acm_env fwk_env;

static struct acm_list acm_logging_list;
static struct task_struct *acm_logging_task;
static struct acm_cache logging_cache;
DECLARE_COMPLETION(acm_logging_comp);
static struct acm_env logging_env;

/*
 * The flag of acm state after acm_init.
 * 0 is successful or none-zero errorno failed.
 * It should be initialized as none-zero.
 */
static long acm_init_state = -ENOTTY;

static int valid_len(const char *str, size_t maxlen)
{
	size_t len;

	len = strnlen(str, maxlen);
	if (len == 0 || len > maxlen - 1) {
		return -EINVAL;
	}

	return ACM_SUCCESS;
}

static size_t get_valid_strlen(char *p, size_t maxlen)
{
	size_t len;

	len = strnlen(p, maxlen);
	if (len > maxlen - 1) {
		len = maxlen - 1;
		*(p + len) = '\0';
	}

	return len;
}

static void sleep_if_list_empty(struct acm_list *list)
{
	set_current_state(TASK_INTERRUPTIBLE);
	spin_lock(&list->spinlock);
	if (list_empty(&list->head)) {
		spin_unlock(&list->spinlock);
		schedule();
	} else {
		spin_unlock(&list->spinlock);
	}
	set_current_state(TASK_RUNNING);
}

static struct acm_lnode *get_first_entry(struct list_head *head)
{
	struct list_head *pos = NULL;
	struct acm_lnode *node = NULL;

	pos = head->next;
	node = (struct acm_lnode *)list_entry(pos, struct acm_lnode, lnode);
	list_del(pos);

	return node;
}

/* elf_hash function */
static unsigned int elf_hash(const char *str)
{
	unsigned int x = 0;
	unsigned int hash = 0;
	unsigned int ret;

	while (*str) {
		hash = (hash << ACM_HASH_LEFT_SHIFT) + (*str++);
		x = hash & ACM_HASH_MASK;
		if (x != 0) {
			hash ^= (x >> ACM_HASH_RIGHT_SHIFT);
			hash &= ~x;
		}
	}
	ret = (hash & ACM_HASH_RESULT_MASK) % ACM_HASHTBL_SZ;
	return ret;
}

static struct acm_hnode *acm_hsearch_with_flag(const struct hlist_head *hash,
	const char *keystring, unsigned long long flag)
{
	const struct hlist_head *phead = NULL;
	struct acm_hnode *pnode = NULL;
	unsigned int idx;

	idx = elf_hash(keystring);
	spin_lock(&acm_hash.spinlock);
	phead = &hash[idx];
	hlist_for_each_entry(pnode, phead, hnode) {
		if (pnode) {
			if (!strcmp(pnode->afp.pkgname, keystring)) {
				if (pnode->afp.flag & flag) {
					break;
				}
			}
		}
	}
	spin_unlock(&acm_hash.spinlock);
	return pnode;
}

static struct acm_hnode *acm_hsearch(const struct hlist_head *hash,
	const char *keystring)
{
	const struct hlist_head *phead = NULL;
	struct acm_hnode *pnode = NULL;
	unsigned int idx;

	idx = elf_hash(keystring);
	spin_lock(&acm_hash.spinlock);
	phead = &hash[idx];
	hlist_for_each_entry(pnode, phead, hnode) {
		if (pnode) {
			if (!strcmp(pnode->afp.pkgname, keystring)) {
				break;
			}
		}
	}
	spin_unlock(&acm_hash.spinlock);
	return pnode;
}

static void acm_hash_add(struct acm_htbl *hash_table, struct acm_hnode *hash_node)
{
	struct hlist_head *phead = NULL;
	struct hlist_head *hash = hash_table->head;

	spin_lock(&hash_table->spinlock);
	WARN_ON(hash_table->nr_nodes > HASHTBL_MAX_SZ - 1);
	phead = &hash[elf_hash(hash_node->afp.pkgname)];
	hlist_add_head(&hash_node->hnode, phead);
	hash_table->nr_nodes++;
	spin_unlock(&hash_table->spinlock);
}

static void acm_hash_del(struct acm_htbl *hash_table, struct acm_hnode *hash_node)
{
	spin_lock(&hash_table->spinlock);
	WARN_ON(hash_table->nr_nodes < 1);
	hlist_del(&(hash_node->hnode));
	hash_table->nr_nodes--;
	spin_unlock(&hash_table->spinlock);
	kfree(hash_node);
}

static void acm_dir_list_add(struct list_head *head, struct acm_dnode *node)
{
	spin_lock(&acm_dir_list.spinlock);
	WARN_ON(acm_dir_list.nr_nodes > ACM_DIR_LIST_MAX - 1);
	list_add_tail(&node->lnode, head);
	acm_dir_list.nr_nodes++;
	spin_unlock(&acm_dir_list.spinlock);
}

static void acm_nomediadir_list_add(struct list_head *head, struct acm_ndnode *node)
{
	spin_lock(&acm_nomediadir_list.spinlock);
	WARN_ON(acm_nomediadir_list.nr_nodes > ACM_DIR_LIST_MAX - 1);
	list_add_tail(&node->lnode, head);
	acm_nomediadir_list.nr_nodes++;
	spin_unlock(&acm_nomediadir_list.spinlock);
}

static void acm_fwk_add(struct list_head *head, struct acm_lnode *node)
{
	spin_lock(&acm_fwk_list.spinlock);
	list_add_tail(&node->lnode, head);
	spin_unlock(&acm_fwk_list.spinlock);
}

int acm_opstat(int flag)
{
	return (acm_flag & flag);
}

static int do_cmd_set_opstat(unsigned long args)
{
	int err = 0;
	int acm_flag_bk = acm_flag;

	if (copy_from_user(&acm_flag, (int *)args, sizeof(acm_flag))) {
		err = -EFAULT;
		acm_flag = acm_flag_bk;
		goto do_cmd_set_opstat_ret;
	} else {
		acm_flag &= (ACM_FLAG_LOGGING | ACM_FLAG_DEL | ACM_FLAG_CRT);
	}
do_cmd_set_opstat_ret:
	return err;
}

static int do_cmd_get_opstat(unsigned long args)
{
	int err = 0;
	if (copy_to_user((int *)args, &acm_flag, sizeof(acm_flag))) {
		err = -EFAULT;
		goto do_cmd_get_opstat_ret;
	}
do_cmd_get_opstat_ret:
	return err;
}

static int do_cmd_add_pkg(unsigned long args)
{
	int err = 0;
	struct acm_hnode *hnode = NULL;
	struct acm_hnode *result = NULL;

	hnode = kzalloc(sizeof(*hnode), GFP_KERNEL);
	if (!hnode) {
		return -ENOMEM;
	}
	INIT_HLIST_NODE(&hnode->hnode);

	if (copy_from_user(&hnode->afp, (struct acm_fwk_pkg *)args,
			sizeof(struct acm_fwk_pkg))) {
		err = -EFAULT;
		goto do_cmd_add_ret;
	}

	if (valid_len(hnode->afp.pkgname, ACM_PKGNAME_LEN_MAX)) {
		err = -EINVAL;
		goto do_cmd_add_ret;
	}

	hnode->afp.pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	result = acm_hsearch(acm_hash.head, hnode->afp.pkgname);
	if (result) {
		if (result->afp.flag != hnode->afp.flag) {
			result->afp.flag = hnode->afp.flag;
		}
		err = ACM_SUCCESS;
		goto do_cmd_add_ret;
	}

	acm_hash_add(&acm_hash, hnode);

	return err;

do_cmd_add_ret:
	kfree(hnode);
	return err;
}

static int do_cmd_del_pkg(unsigned long args)
{
	int err = 0;
	struct acm_hnode *hnode = NULL;
	struct acm_fwk_pkg *fwk_pkg = NULL;

	fwk_pkg = kzalloc(sizeof(*fwk_pkg), GFP_KERNEL);
	if (!fwk_pkg) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_pkg->pkgname, (char *)args,
			sizeof(fwk_pkg->pkgname))) {
		err = -EFAULT;
		goto do_cmd_delete_ret;
	}
	if (valid_len(fwk_pkg->pkgname, ACM_PKGNAME_LEN_MAX)) {
		err = -EINVAL;
		goto do_cmd_delete_ret;
	}
	fwk_pkg->pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	hnode = acm_hsearch(acm_hash.head, fwk_pkg->pkgname);
	if (!hnode) {
		pr_err("ACM: Package not found!\n");
		err = -ENODATA;
		goto do_cmd_delete_ret;
	}

	acm_hash_del(&acm_hash, hnode);
	hnode = NULL;

do_cmd_delete_ret:
	kfree(fwk_pkg);
	return err;
}

static int do_cmd_get_pkgflag(unsigned long args)
{
	int err = 0;
	struct acm_hnode *hnode = NULL;
	struct acm_fwk_pkg *fwk_pkg = NULL;

	fwk_pkg = kzalloc(sizeof(*fwk_pkg), GFP_KERNEL);
	if (!fwk_pkg) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_pkg, (struct acm_fwk_pkg *)args,
			sizeof(struct acm_fwk_pkg))) {
		err = -EFAULT;
		goto do_cmd_get_pkgflag_ret;
	}
	if (valid_len(fwk_pkg->pkgname, ACM_PKGNAME_LEN_MAX)) {
		err = -EINVAL;
		goto do_cmd_get_pkgflag_ret;
	}
	fwk_pkg->pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	hnode = acm_hsearch(acm_hash.head, fwk_pkg->pkgname);
	if (!hnode) {
		pr_err("ACM: Package not found!\n");
		err = -ENODATA;
		goto do_cmd_get_pkgflag_ret;
	}
	fwk_pkg->flag = hnode->afp.flag;

	if (copy_to_user((struct acm_fwk_pkg *)args, fwk_pkg,
			sizeof(struct acm_fwk_pkg))) {
		err = -EFAULT;
		goto do_cmd_get_pkgflag_ret;
	}

do_cmd_get_pkgflag_ret:
	kfree(fwk_pkg);
	return err;
}

static int do_cmd_add_dir(unsigned long args)
{
	int err = 0;
	struct acm_dnode *dir_node = NULL;
	struct acm_fwk_dir *fwk_dir = NULL;

	fwk_dir = kzalloc(sizeof(*fwk_dir), GFP_KERNEL);
	if (!fwk_dir) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_dir, (struct acm_fwk_dir *)args,
			sizeof(struct acm_fwk_dir))) {
		pr_err("ACM: Failed to copy dir from user space!\n");
		err = -EFAULT;
		goto add_dir_ret;
	}

	if (valid_len(fwk_dir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		err = -EINVAL;
		goto add_dir_ret;
	}
	fwk_dir->dir[ACM_DIR_MAX - 1] = '\0';

	/* Check whether dir is already in the acm_dir_list */
	spin_lock(&acm_dir_list.spinlock);
	list_for_each_entry(dir_node, &acm_dir_list.head, lnode) {
		if (strncmp(fwk_dir->dir, dir_node->afd.dir, ACM_DIR_MAX - 1) == 0) {
			if (dir_node->afd.flag != fwk_dir->flag) {
				dir_node->afd.flag = fwk_dir->flag;
			}
			spin_unlock(&acm_dir_list.spinlock);
			goto add_dir_ret;
		}
	}
	spin_unlock(&acm_dir_list.spinlock);

	dir_node = kzalloc(sizeof(*dir_node), GFP_KERNEL);
	if (!dir_node) {
		err = -ENOMEM;
		goto add_dir_ret;
	}

	dir_node->afd.flag = fwk_dir->flag;
	memcpy(dir_node->afd.dir, fwk_dir->dir, ACM_DIR_MAX - 1);
	dir_node->afd.dir[ACM_DIR_MAX - 1] = '\0';

	acm_dir_list_add(&acm_dir_list.head, dir_node);

add_dir_ret:
	kfree(fwk_dir);
	return err;
}

static int do_cmd_del_dir(unsigned long args)
{
	int err = 0;
	struct acm_dnode *n = NULL;
	struct acm_dnode *dir_node = NULL;
	struct acm_fwk_dir *fwk_dir = NULL;

	fwk_dir = kzalloc(sizeof(*fwk_dir), GFP_KERNEL);
	if (!fwk_dir) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_dir->dir, (char *)args, sizeof(fwk_dir->dir))) {
		pr_err("ACM: Failed to copy dir from user space!\n");
		err = -EFAULT;
		goto del_dir_ret;
	}
	if (valid_len(fwk_dir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		err = -EINVAL;
		goto del_dir_ret;
	}
	fwk_dir->dir[ACM_DIR_MAX - 1] = '\0';

	spin_lock(&acm_dir_list.spinlock);
	list_for_each_entry_safe(dir_node, n, &acm_dir_list.head, lnode) {
		if (strncmp(dir_node->afd.dir, fwk_dir->dir, ACM_DIR_MAX - 1) == 0) {
			WARN_ON(acm_dir_list.nr_nodes < 1);
			list_del(&dir_node->lnode);
			acm_dir_list.nr_nodes--;
			spin_unlock(&acm_dir_list.spinlock);
			kfree(dir_node);
			dir_node = NULL;
			goto del_dir_ret;
		}
	}
	spin_unlock(&acm_dir_list.spinlock);

del_dir_ret:
	kfree(fwk_dir);
	return err;
}

static int do_cmd_get_dirflag(unsigned long args)
{
	int err = 0;
	struct acm_dnode *dir_node = NULL;
	struct acm_fwk_dir *fwk_dir = NULL;

	fwk_dir = kzalloc(sizeof(*fwk_dir), GFP_KERNEL);
	if (!fwk_dir) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_dir, (struct acm_fwk_dir *)args,
			sizeof(struct acm_fwk_dir))) {
		pr_err("ACM: Failed to copy dir from user space!\n");
		err = -EFAULT;
		goto add_dir_ret;
	}

	if (valid_len(fwk_dir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		err = -EINVAL;
		goto add_dir_ret;
	}
	fwk_dir->dir[ACM_DIR_MAX - 1] = '\0';

	/* Check whether dir is already in the acm_dir_list */
	spin_lock(&acm_dir_list.spinlock);
	list_for_each_entry(dir_node, &acm_dir_list.head, lnode) {
		if (strncmp(fwk_dir->dir, dir_node->afd.dir, ACM_DIR_MAX - 1) == 0) {
			fwk_dir->flag = dir_node->afd.flag;
			break;
		}
	}
	spin_unlock(&acm_dir_list.spinlock);

	if (copy_to_user((struct acm_fwk_dir *)args, fwk_dir,
			sizeof(struct acm_fwk_dir))) {
		pr_err("ACM: Failed to copy dir to user space!\n");
		err = -EFAULT;
		goto add_dir_ret;
	}

add_dir_ret:
	kfree(fwk_dir);
	return err;
}

static int do_cmd_add_nomediadir(unsigned long args)
{
	int err = 0;
	struct acm_ndnode *dir_node = NULL;
	struct acm_fwk_nomediadir *fwk_ndir = NULL;

	fwk_ndir = kzalloc(sizeof(*fwk_ndir), GFP_KERNEL);
	if (!fwk_ndir) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_ndir, (struct acm_fwk_nomediadir *)args,
			sizeof(struct acm_fwk_nomediadir))) {
		pr_err("ACM: Failed to copy nomedia dir from user space!\n");
		err = -EFAULT;
		goto add_dir_ret;
	}

	if (valid_len(fwk_ndir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		err = -EINVAL;
		goto add_dir_ret;
	}
	fwk_ndir->dir[ACM_DIR_MAX - 1] = '\0';

	/* Check whether dir is already in the acm_nomediadir_list */
	spin_lock(&acm_nomediadir_list.spinlock);
	list_for_each_entry(dir_node, &acm_nomediadir_list.head, lnode) {
		if (strncasecmp(fwk_ndir->dir, dir_node->afnd.dir, ACM_DIR_MAX - 1) == 0) {
			spin_unlock(&acm_nomediadir_list.spinlock);
			goto add_dir_ret;
		}
	}
	spin_unlock(&acm_nomediadir_list.spinlock);

	dir_node = kzalloc(sizeof(*dir_node), GFP_KERNEL);
	if (!dir_node) {
		err = -ENOMEM;
		goto add_dir_ret;
	}

	memcpy(dir_node->afnd.dir, fwk_ndir->dir, ACM_DIR_MAX - 1);
	dir_node->afnd.dir[ACM_DIR_MAX - 1] = '\0';

	acm_nomediadir_list_add(&acm_nomediadir_list.head, dir_node);

add_dir_ret:
	kfree(fwk_ndir);
	return err;
}

static int do_cmd_del_nomediadir(unsigned long args)
{
	int err = 0;
	struct acm_ndnode *n = NULL;
	struct acm_ndnode *dir_node = NULL;
	struct acm_fwk_nomediadir *fwk_ndir = NULL;

	fwk_ndir = kzalloc(sizeof(*fwk_ndir), GFP_KERNEL);
	if (!fwk_ndir) {
		return -ENOMEM;
	}

	if (copy_from_user(fwk_ndir->dir, (struct acm_fwk_nomediadir *)args,
			sizeof(struct acm_fwk_nomediadir))) {
		pr_err("ACM: Failed to copy nomedia dir from user space!\n");
		err = -EFAULT;
		goto del_dir_ret;
	}

	if (valid_len(fwk_ndir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		err = -EINVAL;
		goto del_dir_ret;
	}
	fwk_ndir->dir[ACM_DIR_MAX - 1] = '\0';

	spin_lock(&acm_nomediadir_list.spinlock);
	list_for_each_entry_safe(dir_node, n, &acm_nomediadir_list.head, lnode) {
		if (strncasecmp(dir_node->afnd.dir, fwk_ndir->dir, ACM_DIR_MAX - 1) == 0) {
			WARN_ON(acm_nomediadir_list.nr_nodes < 1);
			list_del(&dir_node->lnode);
			acm_nomediadir_list.nr_nodes--;
			spin_unlock(&acm_nomediadir_list.spinlock);
			kfree(dir_node);
			dir_node = NULL;
			goto del_dir_ret;
		}
	}
	spin_unlock(&acm_nomediadir_list.spinlock);

del_dir_ret:
	kfree(fwk_ndir);
	return err;
}

static int do_cmd_search_nomediadir(unsigned long args)
{
	int err = 0;
	struct acm_ndnode *dir_node = NULL;
	struct acm_fwk_nomediadir *fwk_ndir = NULL;

	fwk_ndir = kzalloc(sizeof(*fwk_ndir), GFP_KERNEL);
	if (!fwk_ndir) {
		return err;
	}

	if (copy_from_user(fwk_ndir, (struct acm_fwk_nomediadir *)args,
			sizeof(struct acm_fwk_nomediadir))) {
		pr_err("ACM: Failed to copy nomedia dir from user space!\n");
		goto add_dir_ret;
	}

	if (valid_len(fwk_ndir->dir, ACM_DIR_MAX)) {
		pr_err("ACM: Failed to check the length of dir name!\n");
		goto add_dir_ret;
	}
	fwk_ndir->dir[ACM_DIR_MAX - 1] = '\0';

	/* Check whether dir is already in the acm_nomediadir_list */
	spin_lock(&acm_nomediadir_list.spinlock);
	list_for_each_entry(dir_node, &acm_nomediadir_list.head, lnode) {
		if (strncasecmp(fwk_ndir->dir, dir_node->afnd.dir, ACM_DIR_MAX - 1) == 0) {
			err = 1;
			break;
		}
	}
	spin_unlock(&acm_nomediadir_list.spinlock);

add_dir_ret:
	kfree(fwk_ndir);
	return err;
}

static void acm_logging_add(struct list_head *head, struct acm_lnode *node)
{
	spin_lock(&acm_logging_list.spinlock);
	if (acm_logging_list.nr_nodes > ACM_LIST_MAX_NODES - 1) {
		pr_err("List was too long! Dropped a pkgname!\n");
		spin_unlock(&acm_logging_list.spinlock);
		return;
	}
	list_add_tail(&node->lnode, head);
	acm_logging_list.nr_nodes++;
	spin_unlock(&acm_logging_list.spinlock);
}

static long acm_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	int err = 0;

	if (acm_init_state) {
		pr_err("ACM: Access Control Module init failed! err = %ld\n",
			   acm_init_state);
		return -ENOTTY;
	}

	if (_IOC_TYPE(cmd) != ACM_MAGIC) {
		pr_err("ACM: Failed to check ACM_MAGIC!\n");
		return -EINVAL;
	}

	if (_IOC_NR(cmd) > ACM_CMD_MAXNR) {
		pr_err("ACM: Failed to check ACM_CMD_MAXNR!\n");
		return -EINVAL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	if ((_IOC_DIR(cmd) & _IOC_READ) || (_IOC_DIR(cmd) & _IOC_WRITE)) {
		err = !access_ok((void *)args, _IOC_SIZE(cmd));
	}
#else
	if (_IOC_DIR(cmd) & _IOC_READ) {
		err = !access_ok(VERIFY_WRITE, (void *)args, _IOC_SIZE(cmd));
	} else if (_IOC_DIR(cmd) & _IOC_WRITE) {
		err = !access_ok(VERIFY_READ, (void *)args, _IOC_SIZE(cmd));
	}
#endif

	if (err) {
		pr_err("ACM: Failed to check access permission!\n");
		return -EINVAL;
	}

	switch (cmd) {
	case ACM_SET_OPCTRL:
		err = do_cmd_set_opstat(args);
		break;
	case ACM_GET_OPCTRL:
		err = do_cmd_get_opstat(args);
		break;
	case ACM_ADD_PKG:
		err = do_cmd_add_pkg(args);
		break;
	case ACM_DEL_PKG:
		err = do_cmd_del_pkg(args);
		break;
	case ACM_GET_PKG_FLAG:
		err = do_cmd_get_pkgflag(args);
		break;
	case ACM_ADD_DIR:
		err = do_cmd_add_dir(args);
		break;
	case ACM_DEL_DIR:
		err = do_cmd_del_dir(args);
		break;
	case ACM_GET_DIR_FLAG:
		err = do_cmd_get_dirflag(args);
		break;
	case ACM_ADD_NOMEDIADIR:
		err = do_cmd_add_nomediadir(args);
		break;
	case ACM_DEL_NOMEDIADIR:
		err = do_cmd_del_nomediadir(args);
		break;
	case ACM_SEARCH_NOMEDIADIR:
		err = do_cmd_search_nomediadir(args);
		break;
	default:
		pr_err("ACM: Unknown command!\n");
		return -EINVAL;
	}

	return err;
}

static int set_path(struct acm_lnode *node, struct dentry *dentry)
{
	char *buffer = NULL;
	char *dentry_path = NULL;
	size_t path_len;

	buffer = kzalloc(ACM_PATH_MAX, GFP_KERNEL);
	if (!buffer) {
		return -ENOMEM;
	}

	dentry_path = dentry_path_raw(dentry, buffer, ACM_PATH_MAX);
	if (IS_ERR(dentry_path)) {
		kfree(buffer);
		pr_err("ACM: Failed to get path! err = %lu\n", PTR_ERR(dentry_path));
		return -EINVAL;
	}

	path_len = get_valid_strlen(dentry_path, ACM_PATH_MAX);
	memcpy(node->path, dentry_path, path_len);
	node->path[path_len] = '\0';
	kfree(buffer);
	return ACM_SUCCESS;
}

static int do_get_path_error(struct acm_lnode *node, struct dentry *dentry)
{
	int i;
	int err;
	int depth;
	struct dentry *d[ERR_PATH_MAX_DENTRIES] = {NULL};

	for (i = 0; i < ERR_PATH_MAX_DENTRIES; i++) {
		d[i] = dget(dentry);
	}

	/*
	 * Find the root dentry of the current file system.The d[i] saves the
	 * top ERR_PATH_MAX_DENTRIES-1 dentries to the root dentry.
	 */
	depth = 0;
	while (!IS_ROOT(dentry)) {
		dput(d[0]);
		for (i = 0; i < ERR_PATH_MAX_DENTRIES - 1; i++) {
			d[i] = d[i + 1];
		}
		dentry = d[ERR_PATH_MAX_DENTRIES - 1] = dget_parent(dentry);
		depth++;
	}
	node->depth = depth;

	dentry = d[ERR_PATH_LAST_DENTRY];

	for (i = 0; i < ERR_PATH_MAX_DENTRIES; i++) {
		dput(d[i]);
	}

	dentry = dget(dentry);
	err = set_path(node, dentry);
	if (err) {
		dput(dentry);
		pr_err("ACM: Unknown error! err = %d\n", err);
		return -EINVAL;
	}
	dput(dentry);

	return ACM_SUCCESS;
}

static int delete_log_upload(const char *pkgname, uid_t uid,
	struct dentry *dentry, int file_type, int op)
{
	int err;
	struct dentry *parent;
	struct acm_lnode *new_dmd_node = NULL;

	new_dmd_node = kzalloc(sizeof(*new_dmd_node), GFP_NOFS);
	if (!new_dmd_node) {
		return -ENOMEM;
	}

	if (pkgname != NULL) {
		memcpy(new_dmd_node->pkgname, pkgname, ACM_PKGNAME_LEN_MAX - 1);
	}
	new_dmd_node->pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	new_dmd_node->depth = DEPTH_INIT;
	new_dmd_node->file_type = file_type;
	new_dmd_node->op = op;
	new_dmd_node->uid = uid;

	parent = dget_parent(dentry);
	err = set_path(new_dmd_node, parent);
	if (err) {
		pr_err("ACM: Failed to get full path! err = %d\n", err);
		err = do_get_path_error(new_dmd_node, dentry);
		if (err) {
			pr_err("ACM: Failed to get path for dmd! err = %d\n", err);
		}
	}

	/*
	 * Data in new_dmd_list will be uploaded to unrmd by acm_logging_task, and
	 * then uploaded to dmd server.
	 */
	acm_logging_add(&acm_logging_list.head, new_dmd_node);
	complete(&acm_logging_comp);

	return err;
}

inline void acm_fuse_init_cache(void)
{
	/* do nothing */
}

inline void acm_fuse_free_cache(void)
{
	/* do nothing */
}

static void get_real_pkg_name(char *pkgname, struct task_struct *tsk,
	const uid_t uid)
{
	int i;
	int res;
	struct task_struct *p_tsk = NULL;

	if (uid >= UID_THRESHOLD) {
		p_tsk = tsk;
		while (__kuid_val(task_uid(p_tsk)) >= UID_THRESHOLD) {
			if ((p_tsk->real_parent) != NULL) {
				tsk = p_tsk;
				p_tsk = p_tsk->real_parent->group_leader;
			} else {
				break;
			}
		}
	}

	res = get_cmdline(tsk, pkgname, ACM_PKGNAME_LEN_MAX - 1);
	pkgname[res] = '\0';

	for (i = 0; i < ACM_PKGNAME_LEN_MAX && pkgname[i] != '\0'; i++) {
		if (pkgname[i] == ':') {
			pkgname[i] = '\0';
			break;
		}
	}
}

static int delete_log_upload_fwk(const char *pkgname, uid_t taskuid,
	struct dentry *dentry, int op)
{
	int err;
	struct acm_lnode *new_fwk_node = NULL;

	/* Data not found */
	new_fwk_node = kzalloc(sizeof(*new_fwk_node), GFP_NOFS);
	if (!new_fwk_node) {
		return -ENOMEM;
	}

	if (pkgname != NULL) {
		memcpy(new_fwk_node->pkgname, pkgname, ACM_PKGNAME_LEN_MAX - 1);
	}
	new_fwk_node->pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	err = set_path(new_fwk_node, dentry);
	if (err) {
		kfree(new_fwk_node);
		new_fwk_node = NULL;
		pr_err("ACM: Failed to get path for framework! err = %d\n", err);
		return err;
	}

	new_fwk_node->op = op;
	new_fwk_node->uid = taskuid;

	/* Data in new_fwk_list will be uploaded
	 * to framework by acm_fwk_task
	 */
	acm_fwk_add(&acm_fwk_list.head, new_fwk_node);
	wake_up_process(acm_fwk_task);

	return ACM_SUCCESS;
}

static char *dentry_without_usrrootentry(char *str, int file_type)
{
	int i;
	int num = 0;
	int skip;

#ifdef CONFIG_OPLUS_FEATURE_ACM3
	if (file_type == ACM_SPECIAL_TYPE || file_type == ACM_SPECIAL_RENAME_TYPE)
		skip = 3;
	else
#endif
		skip = 2;

	for (i = 0; i < strlen(str); i++) {
		if (num == skip) {
			break;
		}
		if (*(str + i) == '/') {
			num++;
		}
	}

	return num == skip ? (str + i) : str;
}

static int inquiry_delete_policy(char *pkgname, uid_t taskuid,
	struct dentry *dentry, int file_type, int op)
{
	struct acm_hnode *hsearch_ret = NULL;
	struct acm_dnode *n = NULL;
	struct acm_dnode *dir_node = NULL;
	unsigned int dir_flag = 0;
	char *buffer = NULL;
	char *dentry_path = NULL;

	buffer = kzalloc(ACM_PATH_MAX, GFP_KERNEL);
	if (!buffer) {
		return DEL_ALLOWED;
	}

	dentry_path = dentry_path_raw(dentry, buffer, ACM_PATH_MAX);
	if (IS_ERR(dentry_path)) {
		kfree(buffer);
		pr_err("ACM: Failed to get path! err = %lu\n", PTR_ERR(dentry_path));
		return DEL_ALLOWED;
	}

	spin_lock(&acm_dir_list.spinlock);
	list_for_each_entry_safe(dir_node, n, &acm_dir_list.head, lnode) {
		if (strlen(dentry_path) >= strlen(dir_node->afd.dir)) {
			if (strncasecmp(dentry_without_usrrootentry(dentry_path, file_type),
				dir_node->afd.dir, strlen(dir_node->afd.dir)) == 0) {
				dir_flag = dir_node->afd.flag;
				break;
			}
		}
	}
	spin_unlock(&acm_dir_list.spinlock);

	kfree(buffer);
	if (dir_flag == 0) {
		return DEL_ALLOWED;
	} else {
		if (acm_opstat(ACM_FLAG_LOGGING)) {
			if (delete_log_upload(pkgname, taskuid, dentry, file_type, op)) {
				pr_err("ACM: Failed to upload to dmd! file_type = %d op = %d\n",
					file_type, op);
			}
		}

#ifdef CONFIG_OPLUS_FEATURE_ACM3
		if (file_type == ACM_SPECIAL_TYPE || file_type == ACM_SPECIAL_RENAME_TYPE) {
			return DEL_ALLOWED;
		}
#endif

		if (op == FUSE_RENAME || op == FUSE_RENAME2) {
			return DEL_ALLOWED;
		}

		if (acm_opstat(ACM_FLAG_LOGGING)
			&& !acm_opstat(ACM_FLAG_DEL)) {
			return DEL_ALLOWED;
		}

		if (taskuid < UID_THRESHOLD) {
			return DEL_ALLOWED;
		}
	}

	hsearch_ret = acm_hsearch_with_flag(acm_hash.head, pkgname, dir_flag);
	if (!hsearch_ret) {
		return -ENODATA;
	}

	return DEL_ALLOWED;
}

static int inquiry_create_nomedia_policy(struct dentry *dentry)
{
	int ret = CRT_ALLOWED;
	struct acm_ndnode *dir_node = NULL;
	char *buffer = NULL;
	char *dentry_path = NULL;
	struct dentry *parent = dget_parent(dentry);

	buffer = kzalloc(ACM_PATH_MAX, GFP_KERNEL);
	if (!buffer) {
		return CRT_ALLOWED;
	}

	dentry_path = dentry_path_raw(parent, buffer, ACM_PATH_MAX);
	if (IS_ERR(dentry_path)) {
		kfree(buffer);
		pr_err("ACM: Failed to get path! err = %lu\n", PTR_ERR(dentry_path));
		return CRT_ALLOWED;
	}

	if (IS_ROOT(dget_parent(parent))) {
		kfree(buffer);
		return CRT_NOT_ALLOWED;
	}

	spin_lock(&acm_nomediadir_list.spinlock);
	list_for_each_entry(dir_node, &acm_nomediadir_list.head, lnode) {
		if (strlen(dentry_path) >= strlen(dir_node->afnd.dir)) {
			if (strncasecmp(dentry_path + (strlen(dentry_path) - strlen(dir_node->afnd.dir)),
				dir_node->afnd.dir, strlen(dir_node->afnd.dir)) == 0) {
				if (strstr(dir_node->afnd.dir, "/") == NULL) {
					if (IS_ROOT(dget_parent(dget_parent(parent)))) {
						ret = CRT_NOT_ALLOWED;
						break;
					}
				} else {
					ret = CRT_NOT_ALLOWED;
					break;
				}
			}
		}
	}
	spin_unlock(&acm_nomediadir_list.spinlock);
	kfree(buffer);
	return ret;
}

int acm_search(struct dentry *dentry, int file_type, int op)
{
	int ret = DEL_ALLOWED;
	int err = 0;
	struct task_struct *tsk = NULL;
	char *pkgname = NULL;
	uid_t taskuid = 0;
	/*
	struct timespec __maybe_unused start;
	struct timespec __maybe_unused end;

	getrawmonotonic(&start);
	*/

	if (acm_init_state) {
		pr_err("ACM: Access Control Module init failed! err = %ld\n",
			acm_init_state);
		err = -EINVAL;
		return err;
	}

	tsk = current->group_leader;
	if (!tsk) {
		err = -EINVAL;
		return err;
	}
	taskuid = __kuid_val(task_uid(tsk));

	pkgname = kzalloc(ACM_PKGNAME_LEN_MAX, GFP_NOFS);
	if (!pkgname) {
		err = -ENOMEM;
		return err;
	}

	get_real_pkg_name(pkgname, tsk, taskuid);

	if (valid_len(pkgname, ACM_PKGNAME_LEN_MAX)) {
		pr_err("ACM: Failed to check the length of package name!\n");
		kfree(pkgname);
		return DEL_ALLOWED;
	}

	pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

	if ((acm_opstat(ACM_FLAG_DEL | ACM_FLAG_LOGGING))
		&& (op == FUSE_UNLINK || op == FUSE_RMDIR
			|| op == FUSE_RENAME || op == FUSE_RENAME2)) {
		ret = inquiry_delete_policy(pkgname, taskuid, dentry, file_type, op);
		if (ret != DEL_ALLOWED) {
			err = delete_log_upload_fwk(pkgname, taskuid, dentry, op);
			if (err) {
				pr_err("ACM: Failed to upload to fwk! err = %d ret=%d\n", err, ret);
			}
		}
	} else if ((acm_opstat(ACM_FLAG_CRT))
		&& (op == FUSE_CREATE || op == FUSE_MKNOD)) {
		ret = inquiry_create_nomedia_policy(dentry);
		if (ret != CRT_ALLOWED) {
			err = delete_log_upload_fwk(pkgname, taskuid, dentry, op);
			if (err) {
				pr_err("ACM: Failed to upload to fwk! err = %d ret = %d\n", err, ret);
			}
		}
	}

	if (pkgname != NULL) {
		kfree(pkgname);
	}

	/*
	getrawmonotonic(&end);
	pr_err("ACM: %s TIME_COST: start.tv_sec = %lu start.tv_nsec = %lu
		end.tv_sec = %lu end.tv_nsec = %lu duraion = %lu\n",
		__func__,
		start.tv_sec, start.tv_nsec, end.tv_sec,
		end.tv_nsec, end.tv_nsec - start.tv_nsec);
	*/

	return ret;
}

static int acm_search2(struct dentry *dentry, struct dentry *dentry2,
	int file_type, int op)
{
	int ret = DEL_ALLOWED;
	int err = 0;
	struct task_struct *tsk = NULL;
	char *pkgname = NULL;
	uid_t taskuid = 0;
	/*
	struct timespec __maybe_unused start;
	struct timespec __maybe_unused end;

	getrawmonotonic(&start);
	*/

	if (acm_init_state) {
		pr_err("ACM: Access Control Module init failed! err = %ld\n",
			acm_init_state);
		err = -EINVAL;
		return err;
	}

	tsk = current->group_leader;
	if (!tsk) {
		err = -EINVAL;
		return err;
	}
	taskuid = __kuid_val(task_uid(tsk));

	pkgname = kzalloc(ACM_PKGNAME_LEN_MAX, GFP_NOFS);
	if (!pkgname) {
		err = -ENOMEM;
		return err;
	}

	get_real_pkg_name(pkgname, tsk, taskuid);

	if (valid_len(pkgname, ACM_PKGNAME_LEN_MAX)) {
		pr_err("ACM: Failed to check the length of package name!\n");
		kfree(pkgname);
		return DEL_ALLOWED;
	}

	pkgname[ACM_PKGNAME_LEN_MAX - 1] = '\0';

#ifdef CONFIG_OPLUS_FEATURE_ACM3
	/* has been monitored by fuse, ignore it at f2fs */
	if (strncasecmp(pkgname, MEDIAPROVIDER, sizeof(MEDIAPROVIDER)) == 0) {
		return DEL_ALLOWED;
	}

	if (strncasecmp(pkgname, MEDIAPROVIDER2, sizeof(MEDIAPROVIDER2)) == 0) {
		return DEL_ALLOWED;
	}
#endif

	if ((acm_opstat(ACM_FLAG_DEL | ACM_FLAG_LOGGING))
		&& (op == FUSE_UNLINK || op == FUSE_RMDIR
			|| op == FUSE_RENAME || op == FUSE_RENAME2)) {
		ret = inquiry_delete_policy(pkgname, taskuid, dentry, file_type, op);
		if (ret != DEL_ALLOWED) {
			err = delete_log_upload_fwk(pkgname, taskuid, dentry, op);
			if (err) {
				pr_err("ACM: Failed to upload to fwk! err = %d ret=%d\n", err, ret);
			}
		}
	} else if ((acm_opstat(ACM_FLAG_CRT)
#ifdef CONFIG_OPLUS_FEATURE_ACM3
					&& file_type != ACM_SPECIAL_TYPE && file_type != ACM_SPECIAL_RENAME_TYPE
#endif
)
		&& (op == FUSE_CREATE || op == FUSE_MKNOD || op == FUSE_MKDIR)) {
		ret = inquiry_create_nomedia_policy(dentry);
		if (ret != CRT_ALLOWED) {
			err = delete_log_upload_fwk(pkgname, taskuid, dentry, op);
			if (err) {
				pr_err("ACM: Failed to upload to fwk! err = %d ret = %d\n", err, ret);
			}
		}
	}

	if (pkgname != NULL) {
		kfree(pkgname);
	}

	/*
	getrawmonotonic(&end);
	pr_err("ACM: %s TIME_COST: start.tv_sec = %lu start.tv_nsec = %lu
		end.tv_sec = %lu end.tv_nsec = %lu duraion = %lu\n",
		__func__,
		start.tv_sec, start.tv_nsec, end.tv_sec,
		end.tv_nsec, end.tv_nsec - start.tv_nsec);
	*/

	return ret;
}

static int is_media_extension(const unsigned char *s, const char *sub)
{
	size_t slen = strlen((const char *)s);
	size_t sublen = strlen(sub);

	if (slen < sublen + MIN_FNAME_LENGTH)
		return 0;

	if (s[slen - sublen - 1] != '.')
		return 0;

	if (!strncasecmp((const char *)s + slen - sublen, sub, sublen))
		return 1;
	return 0;
}

static int is_photo_file(struct dentry *dentry)
{
	static const char *const ext[] = {
		"jpg", "jpe", "jpeg", "gif", "png", "bmp", "wbmp",
		"webp", "dng", "cr2", "nef", "nrw", "arw", "rw2",
		"orf", "raf", "pef", "srw", "heic", "heif", NULL
	};
	int i;

	for (i = 0; ext[i]; i++) {
		if (is_media_extension(dentry->d_name.name, ext[i]))
			return ACM_PHOTO_TYPE;
	}

	return 0;
}

static int is_video_file(struct dentry *dentry)
{
	static const char *const ext[] = {
		"mpeg", "mpg", "mp4", "m4v", "mov", "3gp", "3gpp", "3g2",
		"3gpp2", "mkv", "webm", "ts", "avi", "f4v", "flv", "m2ts",
		"divx", "wmv", "asf", "amr", NULL
	};
	int i;

	for (i = 0; ext[i]; i++) {
		if (is_media_extension(dentry->d_name.name, ext[i]))
			return ACM_VIDEO_TYPE;
	}

	return 0;
}

static int is_nomedia_file(struct dentry *dentry)
{
	if (strncasecmp(dentry->d_name.name, ACM_NOMEDIA_FNAME, strlen(ACM_NOMEDIA_FNAME)) == 0)
		return ACM_NOMEDIA_TYPE;
	else
		return 0;
}

static int get_monitor_file_type(struct dentry *dentry)
{
	int file_type = 0;

	if ((file_type = is_photo_file(dentry)) != 0)
		return file_type;

	file_type = is_video_file(dentry);

	return file_type;
}

static int should_monitor_file(struct dentry *dentry, int operation)
{
	int file_type = 0;

	if (operation == FUSE_UNLINK) {
		if (acm_opstat(ACM_FLAG_LOGGING | ACM_FLAG_DEL))
			file_type = get_monitor_file_type(dentry);
	} else if (operation == FUSE_RENAME || operation == FUSE_RENAME2) {
		if (acm_opstat(ACM_FLAG_LOGGING))
			file_type = get_monitor_file_type(dentry);
	} else if (operation == FUSE_CREATE || operation == FUSE_MKNOD ||
		operation == FUSE_MKDIR) {
		if (acm_opstat(ACM_FLAG_CRT))
			file_type = is_nomedia_file(dentry);
	}

	return file_type;
}

int monitor_acm2(struct dentry *dentry, struct dentry *dentry2, int op)
{
	struct inode *inode = d_inode(dentry);
	int file_type = 0x0f;
	int err = 0;

	if (!acm_opstat(ACM_FLAG_LOGGING | ACM_FLAG_DEL | ACM_FLAG_CRT)) {
		goto monitor_ret;
	}

	if (!inode) {
		goto monitor_ret;
	}

	if (S_ISREG(inode->i_mode)) {
		file_type = should_monitor_file(dentry, op);
		if (file_type == 0) {
			goto monitor_ret;
		}
	} else if (S_ISDIR(inode->i_mode)) {
		if (!(acm_opstat(ACM_FLAG_DEL)) &&
			should_monitor_file(dentry, op) != ACM_NOMEDIA_TYPE) {
			goto monitor_ret;
		}
	} else {
		goto monitor_ret;
	}

	err = acm_search2(dentry, dentry2, file_type, op);

monitor_ret:
	return err;
}

#ifdef CONFIG_OPLUS_FEATURE_ACM3
static int is_special_file(struct dentry *dentry)
{
	static const char *const ext[] = {
		".amr", NULL
	};
	int i;

	for (i = 0; ext[i]; i++) {
		if (strstr(dentry->d_name.name, ext[i])) {
			return ACM_SPECIAL_TYPE;
		}
	}

	return 0;
}

int monitor_acm3(struct dentry *dentry, struct dentry *dentry2, int op)
{
	struct inode *inode = d_inode(dentry);
	int file_type = 0x0f;
	int err = 0;

	if (!acm_opstat(ACM_FLAG_LOGGING | ACM_FLAG_DEL | ACM_FLAG_CRT)) {
		goto monitor_ret;
	}

	if (!inode) {
		goto monitor_ret;
	}

	if (S_ISREG(inode->i_mode)) {
		file_type = is_special_file(dentry);
		if (file_type == 0) {
			goto monitor_ret;
		}
		/* only unlink will be record */
		if (op == FUSE_RENAME && file_type == ACM_SPECIAL_TYPE) {
			op = FUSE_UNLINK;
			file_type = ACM_SPECIAL_RENAME_TYPE;
		}
	} else {
		goto monitor_ret;
	}

	err = acm_search2(dentry, dentry2, file_type, op);

monitor_ret:
	return err;
}
#endif

static void set_logging_uevent_env(struct acm_lnode *node)
{
	int idx;

	memset(&logging_env, 0, sizeof(struct acm_env));
	snprintf(logging_env.pkgname, sizeof(logging_env.pkgname), "PKGNAME=%s",
		node->pkgname);
	snprintf(logging_env.uid, sizeof(logging_env.uid), "UID=%u", node->uid);
	snprintf(logging_env.path, sizeof(logging_env.path), "PATH=%s", node->path);
	snprintf(logging_env.depth, sizeof(logging_env.depth), "DEPTH=%d",
		node->depth);
	snprintf(logging_env.file_type, sizeof(logging_env.file_type), "FTYPE=%d",
		node->file_type);
	snprintf(logging_env.nr, sizeof(logging_env.nr), "NR=%d", node->nr);
	snprintf(logging_env.op, sizeof(logging_env.op), "OP=%d", node->op);

	idx = 0;
	logging_env.envp[idx++] = "LOGGING_STAT";
	logging_env.envp[idx++] = logging_env.pkgname;
	logging_env.envp[idx++] = logging_env.uid;
	logging_env.envp[idx++] = logging_env.path;
	logging_env.envp[idx++] = logging_env.depth;
	logging_env.envp[idx++] = logging_env.file_type;
	logging_env.envp[idx++] = logging_env.nr;
	logging_env.envp[idx++] = logging_env.op;
	logging_env.envp[idx] = NULL;
	/* for test */
	pr_info("ACM: %s %s %s %s %s %s %s\n",
		logging_env.envp[0], logging_env.envp[1], logging_env.envp[2],
		logging_env.envp[4], logging_env.envp[5],
		logging_env.envp[6], logging_env.envp[7]);
}

static void upload_delete_log(void)
{
	int i;
	int err = 0;

	for (i = 0; i < logging_cache.count; i++) {
		set_logging_uevent_env(&logging_cache.cache[i]);
		err = kobject_uevent_env(&(acm_cdev->kobj), KOBJ_CHANGE,
				logging_env.envp);
		if (err) {
			pr_err("ACM: Failed to send uevent!\n");
		}
	}

	memset(&logging_cache, 0, sizeof(struct acm_cache));

	/*
	 * Compiler optimization may remove the call to memset(),
	 * causing logging_cache uncleaned, if logging_cache is not accessed
	 * after memset(). So we access the count member after memset()
	 * to avoid this optimization.
	 */
	logging_cache.count = 0;
}

static bool is_a_cache(struct acm_lnode *node, struct acm_lnode *cache_node)
{
	return (node->depth == cache_node->depth) && (node->op == cache_node->op) &&
		(node->file_type == cache_node->file_type) &&
		(strcmp(node->path, cache_node->path) == 0) &&
		(strcmp(node->pkgname, cache_node->pkgname) == 0);
}

static void add_cache(struct acm_lnode *node)
{
	if (logging_cache.count > MAX_CACHED_DELETE_LOG - 1) {
		return;
	}

	memcpy(&logging_cache.cache[logging_cache.count], node,
		sizeof(struct acm_lnode));
	logging_cache.cache[logging_cache.count].nr++;
	logging_cache.count++;
}

/*
 * Return true if in the cache, false if NOT in the cache.
 */
static bool is_cached(struct acm_lnode *node, int *idx)
{
	int i;

	for (i = 0; i < logging_cache.count; i++) {
		if (is_a_cache(node, &logging_cache.cache[i])) {
			*idx = i;
			return true;
		}
	}
	return false;
}

static void cache_log(struct acm_lnode *node)
{
	int which = -1;

	if (is_cached(node, &which)) {
		WARN_ON((which < 0) || (which > MAX_CACHED_DELETE_LOG - 1));
		logging_cache.cache[which].nr++;
	} else {
		add_cache(node);
	}
}

static int cal_nr_slashes(char *str)
{
	int i;
	int len;
	int nr_slashes = 0;

	len = get_valid_strlen(str, ACM_PATH_MAX);

	for (i = 0; i < len; i++) {
		if (*(str + i) == '/') {
			nr_slashes++;
		}
	}
	return nr_slashes;
}

static void set_depth(struct acm_lnode *node)
{
	if (node->depth == DEPTH_INIT) {
		node->depth = cal_nr_slashes(node->path) - 1;
	}
}

static void process_delete_log(struct acm_lnode *node)
{
	set_depth(node);
	cache_log(node);
}

static void process_and_upload_delete_log(struct list_head *list)
{
	struct acm_lnode *node = NULL;

	while (1) {
		if (list_empty(list)) {
			break;
		}
		node = get_first_entry(list);
		process_delete_log(node);
		kfree(node);
		node = NULL;

		if (logging_cache.count > MAX_CACHED_DELETE_LOG - 1) {
			upload_delete_log();
		}
	}
	if (logging_cache.count > 0) {
		upload_delete_log();
	}
}

static int acm_logging_loop(void *data)
{
	struct list_head list = LIST_HEAD_INIT(list);
	/*
	struct timespec __maybe_unused start;
	struct timespec __maybe_unused end;
	*/

	while (!kthread_should_stop()) {
		if (wait_for_completion_interruptible(&acm_logging_comp)) {
			pr_err("ACM: %s is interrupted!\n", __func__);
		}

		msleep(DELETE_LOG_UPLOAD_INTERVAL_MS);

		spin_lock(&acm_logging_list.spinlock);
		list_cut_position(&list, &acm_logging_list.head,
						  acm_logging_list.head.prev);
		acm_logging_list.nr_nodes = 0;
		spin_unlock(&acm_logging_list.spinlock);

		/* getrawmonotonic(&start); */
		process_and_upload_delete_log(&list);
		/* getrawmonotonic(&end); */
		/*
		pr_err("ACM: %s TIME_COST: start.tv_sec = %lu start.tv_nsec = %lu,
			end.tv_sec = %lu end.tv_nsec = %lu duraion = %lu\n",
			__func__,
			start.tv_sec, start.tv_nsec,
			end.tv_sec, end.tv_nsec,
			end.tv_nsec - start.tv_nsec);
		*/
	}

	return ACM_SUCCESS;
}

static void acm_cache_init(void)
{
	memset(&logging_cache.cache, 0, sizeof(logging_cache.cache));
	logging_cache.count = 0;
}

static void set_fwk_uevent_env(struct acm_lnode *node)
{
	int idx;

	memset(&fwk_env, 0, sizeof(struct acm_env));
	snprintf(fwk_env.pkgname, sizeof(fwk_env.pkgname), "PKGNAME=%s",
		node->pkgname);
	snprintf(fwk_env.uid, sizeof(fwk_env.uid), "UID=%u", node->uid);
	snprintf(fwk_env.path, sizeof(fwk_env.path), "FILE_PATH=%s", node->path);
	snprintf(fwk_env.op, sizeof(fwk_env.op), "OP=%d", node->op);

	idx = 0;
	fwk_env.envp[idx++] = "OPERATION_STAT";
	fwk_env.envp[idx++] = fwk_env.pkgname;
	fwk_env.envp[idx++] = fwk_env.uid;
	fwk_env.envp[idx++] = fwk_env.path;
	fwk_env.envp[idx++] = fwk_env.op;
	fwk_env.envp[idx] = NULL;

	/* for test */
	/*
	pr_info("ACM: %s %s %s %s %s\n", fwk_env.envp[0],
		fwk_env.envp[1], fwk_env.envp[2], fwk_env.envp[3], fwk_env.envp[4]);
	*/
}

static void upload_data_to_fwk(void)
{
	int err = 0;
	struct acm_lnode *node = NULL;
	/*
	struct timespec __maybe_unused start;
	struct timespec __maybe_unused end;
	*/

	while (1) {
		spin_lock(&acm_fwk_list.spinlock);
		if (list_empty(&acm_fwk_list.head)) {
			spin_unlock(&acm_fwk_list.spinlock);
			break;
		}
		node = get_first_entry(&acm_fwk_list.head);
		spin_unlock(&acm_fwk_list.spinlock);
		/* getrawmonotonic(&start); */
		set_fwk_uevent_env(node);
		err = kobject_uevent_env(&(acm_cdev->kobj), KOBJ_CHANGE, fwk_env.envp);
		if (err) {
			pr_err("ACM: Failed to upload to fwk!\n");
		}

		kfree(node);
		node = NULL;
		/*
		getrawmonotonic(&end);
		pr_err("ACM: %s TIME_COST: start.tv_sec = %lu start.tv_nsec = %lu,
		end.tv_sec = %lu end.tv_nsec = %lu duraion = %lu\n",
		        __func__,
		        start.tv_sec, start.tv_nsec,
		        end.tv_sec, end.tv_nsec,
		        end.tv_nsec - start.tv_nsec);
		*/
	}
}

static int acm_fwk_loop(void *data)
{
	while (!kthread_should_stop()) {
		upload_data_to_fwk();
		sleep_if_list_empty(&acm_fwk_list);
	}
	return ACM_SUCCESS;
}

static int acm_hash_init(void)
{
	int i;
	struct hlist_head *head = NULL;

	head = kzalloc(sizeof(*head) * ACM_HASHTBL_SZ, GFP_KERNEL);
	if (!head) {
		return -ENOMEM;
	}
	for (i = 0; i < ACM_HASHTBL_SZ; i++) {
		INIT_HLIST_HEAD(&(head[i]));
	}

	acm_hash.head = head;
	acm_hash.nr_nodes = 0;
	spin_lock_init(&acm_hash.spinlock);
	return ACM_SUCCESS;
}

static void acm_list_init(struct acm_list *list)
{
	INIT_LIST_HEAD(&list->head);
	list->nr_nodes = 0;
	spin_lock_init(&list->spinlock);
}

static int acm_task_init(void)
{
	long err = 0;

	/*
	 *Create the acm_fwk_loop task to asynchronously
	 * upload data to framework.
	 */
	acm_fwk_task = kthread_run(acm_fwk_loop, NULL, "acm_fwk_loop");
	if (IS_ERR(acm_fwk_task)) {
		err = PTR_ERR(acm_fwk_task);
		pr_err("ACM: Failed to create acm_fwk_task! err = %ld\n", err);
		return err;
	}

	acm_logging_task = kthread_run(acm_logging_loop, NULL, "acm_logging_loop");
	if (IS_ERR(acm_logging_task)) {
		err = PTR_ERR(acm_logging_task);
		pr_err("ACM: Failed to create acm_logging_task! err = %ld\n", err);
		return err;
	}

	return err;
}

static const struct file_operations acm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = acm_ioctl,
};

static int acm_chr_dev_init(void)
{
	long err;

	/* Dynamiclly allocate a device number */
	err = alloc_chrdev_region(&acm_devno, ACM_DEV_BASE_MINOR, ACM_DEV_COUNT,
				ACM_DEV_NAME);
	if (err) {
		pr_err("ACM: Failed to alloc device number! err = %ld\n", err);
		return err;
	}

	/* Initialize and add a cdev data structure to kernel */
	acm_cdev = cdev_alloc();
	if (!acm_cdev) {
		err = -ENOMEM;
		pr_err("ACM: Failed to alloc memory for cdev! err = %ld\n", err);
		goto free_devno;
	}
	acm_cdev->owner = THIS_MODULE;
	acm_cdev->ops = &acm_fops;
	err = cdev_add(acm_cdev, acm_devno, ACM_DEV_COUNT);
	if (err) {
		pr_err("ACM: Failed to register cdev! err = %ld\n", err);
		goto free_cdev;
	}

	/* Dynamiclly create a device file */
	acm_class = class_create(THIS_MODULE, ACM_DEV_NAME);
	if (IS_ERR(acm_class)) {
		err = PTR_ERR(acm_class);
		pr_err("ACM: Failed to create a class! err = %ld\n", err);
		goto free_cdev;
	}
	acm_device = device_create(acm_class, NULL, acm_devno, NULL, ACM_DEV_NAME);
	if (IS_ERR(acm_device)) {
		err = PTR_ERR(acm_device);
		pr_err("ACM: Failed to create a class! err = %ld\n", err);
		goto free_class;
	}

	/* Initialize uevent stuff */
	acm_kset = kset_create_and_add(ACM_DEV_NAME, NULL, kernel_kobj);
	if (!acm_kset) {
		err = -ENOMEM;
		pr_err("ACM: Failed to create kset! err = %ld\n", err);
		goto free_device;
	}
	acm_cdev->kobj.kset = acm_kset;
	acm_cdev->kobj.uevent_suppress = 0;
	err = kobject_add(&(acm_cdev->kobj), &(acm_kset->kobj), "acm_cdev_kobj");
	if (err) {
		kobject_put(&(acm_cdev->kobj));
		pr_err("ACM: Failed to add kobject to kernel! err = %ld\n", err);
		goto free_kset;
	}

	return err;

free_kset:
	kset_unregister(acm_kset);
free_device:
	device_destroy(acm_class, acm_devno);
free_class:
	class_destroy(acm_class);
free_cdev:
	cdev_del(acm_cdev);
free_devno:
	unregister_chrdev_region(acm_devno, ACM_DEV_COUNT);

	pr_err("ACM: Failed to init acm character device! err = %ld\n", err);
	return err;
}

static int __init acm_init(void)
{
	long err = 0;

	/* Initialize hash table */
	err = acm_hash_init();
	if (err) {
		pr_err("ACM: Failed to initialize hash table! err = %ld\n", err);
		goto fail_hash;
	}

	acm_list_init(&acm_dir_list);
	acm_list_init(&acm_nomediadir_list);
	acm_list_init(&acm_fwk_list);
	acm_list_init(&acm_logging_list);
	acm_cache_init();

	err = acm_task_init();
	if (err) {
		pr_err("ACM: Failed to initialize main task! err = %ld\n", err);
		goto fail_task;
	}

	/* Initialize acm character device */
	err = acm_chr_dev_init();
	if (err) {
		pr_err("ACM: Failed to initialize acm chrdev! err = %ld\n", err);
		goto fail_task;
	}

	pr_info("ACM: Initialize ACM moudule succeed!\n");

	acm_init_state = err;
	return err;

fail_task:
	kfree(acm_hash.head);
	acm_hash.head = NULL;
fail_hash:
	acm_init_state = err;
	return err;
}

static void acm_hash_cleanup(struct hlist_head *hash)
{
	int i;
	struct hlist_head *phead = NULL;
	struct acm_hnode *pnode = NULL;

	spin_lock(&acm_hash.spinlock);
	for (i = 0; i < ACM_HASHTBL_SZ; i++) {
		phead = &hash[i];
		while (!hlist_empty(phead)) {
			pnode = hlist_entry(phead->first, struct acm_hnode, hnode);
			hlist_del(&pnode->hnode);
			kfree(pnode);
			pnode = NULL;
		}
	}
	acm_hash.nr_nodes = 0;
	spin_unlock(&acm_hash.spinlock);
}

static void acm_list_cleanup(struct acm_list *list)
{
	struct acm_lnode *node = NULL;
	struct list_head *head = NULL, *pos = NULL;

	spin_lock(&list->spinlock);
	head = &list->head;
	while (!list_empty(head)) {
		pos = head->next;
		node = (struct acm_lnode *)list_entry(pos, struct acm_lnode, lnode);
		list_del(pos);
		kfree(node);
		node = NULL;
	}
	list->nr_nodes = 0;
	spin_unlock(&list->spinlock);
}

static void __exit acm_exit(void)
{
	device_destroy(acm_class, acm_devno);
	class_destroy(acm_class);
	cdev_del(acm_cdev);
	unregister_chrdev_region(acm_devno, ACM_DEV_COUNT);
	kset_unregister(acm_kset);

	acm_hash_cleanup(acm_hash.head);
	acm_list_cleanup(&acm_dir_list);
	acm_list_cleanup(&acm_nomediadir_list);

	pr_info("ACM: Exited from ACM module.\n");
}

MODULE_LICENSE("GPL");
module_init(acm_init);
module_exit(acm_exit);
