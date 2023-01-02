// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved.
* VENDOR_EDIT
* File       : oplus_exec_block.c
* Description: For exec block
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#ifdef CONFIG_OPLUS_SECURE_GUARD
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/init.h>

#if defined(CONFIG_OPLUS_EXECVE_BLOCK) || defined(CONFIG_OPLUS_EXECVE_REPORT)

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#include <linux/oplus_kevent.h>
#include <linux/cred.h>
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */

#ifdef OPLUS_DISALLOW_KEY_INTERFACES
#include "oplus_guard_general.h"

static void oplus_report_execveat(const char *path, const char* dcs_event_id)
{
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
	struct kernel_packet_info* dcs_event;
	char dcs_stack[sizeof(struct kernel_packet_info) + 256];
	const char* dcs_event_tag = "kernel_event";
	// const char* dcs_event_id = "execve_report";
	char* dcs_event_payload = NULL;
	int uid = current_uid().val;
	//const struct cred *cred = current_cred();

	dcs_event = (struct kernel_packet_info*)dcs_stack;
	dcs_event_payload = dcs_stack +
		sizeof(struct kernel_packet_info);

	dcs_event->type = 3;

	strncpy(dcs_event->log_tag, dcs_event_tag,
		sizeof(dcs_event->log_tag));
	strncpy(dcs_event->event_id, dcs_event_id,
		sizeof(dcs_event->event_id));

	dcs_event->payload_length = snprintf(dcs_event_payload, 256,
		"%d,path@@%s", uid, path);
	if (dcs_event->payload_length < 256) {
		dcs_event->payload_length += 1;
	}
	kevent_send_to_user(dcs_event);

#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */
	printk(KERN_ERR "=======>[kevent_send_to_user:execve]:common %s result %s\n", path, dcs_event_id);
}

static int oplus_check_execveat_perm(struct file* filp)
{
	char *absolute_path_buf = NULL;
	char *absolute_path = NULL;
	char *context = NULL;
	u32 context_len = 0;
	int rc = 0;
	int retval = 0;

	/*if (!uid_eq(current_uid(), GLOBAL_ROOT_UID))
		goto out_ret;*/

	absolute_path_buf = (char *)__get_free_page(GFP_KERNEL);
	retval = -ENOMEM;
	if (absolute_path_buf == NULL)
		goto out_ret;

	absolute_path = d_path(&filp->f_path, absolute_path_buf, PAGE_SIZE);
	retval = PTR_ERR(absolute_path);
	if (IS_ERR(absolute_path))
                goto out_free;

	retval = 0;
	if (strncmp(absolute_path, "/data", 5))
		goto out_free;
	if (!strncmp(absolute_path, "/data/local/tmp", 15)) {
		goto out_free;
	}
	//add end
	if (!uid_eq(current_uid(), GLOBAL_ROOT_UID)) {
#ifdef CONFIG_OPLUS_EXECVE_REPORT
		oplus_report_execveat(absolute_path, "execve_report");
#endif /* CONFIG_OPLUS_EXECVE_REPORT */
	} else {
#ifdef CONFIG_OPLUS_EXECVE_BLOCK
		rc = get_current_security_context(&context, &context_len);
		retval = -EPERM;
		if (rc) {
			oplus_report_execveat(absolute_path, "execve_block");
			goto out_free;
		}

		retval = -EPERM;
#endif /* CONFIG_OPLUS_EXECVE_BLOCK */
	}

	retval = 0;

out_free:
	kfree(context);
	free_page((unsigned long)absolute_path_buf);

out_ret:
	return retval;
}

int oplus_exec_block(struct file *file)
{
	int  retval = 0;

	if (!is_unlocked()) {
		retval = oplus_check_execveat_perm(file);
		if (retval < 0) {
			return retval;
		}
	}
	return retval;
}
#else
int oplus_exec_block(struct file *file)
{
	int  retval = 0;
	return retval;
}
#endif /* OPLUS_DISALLOW_KEY_INTERFACES */
#endif /* CONFIG_OPLUS_EXECVE_BLOCK or CONFIG_OPLUS_EXECVE_REPORT */
#endif /* CONFIG_OPLUS_SECURE_GUARD */
