// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020   Oplus. All rights reserved.
* VENDOR_EDIT
* File       : oplus_mount_block.c
* Description: remount block.
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#ifdef CONFIG_OPLUS_SECURE_GUARD
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/capability.h>
#include <linux/mnt_namespace.h>
#include <linux/user_namespace.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/bootmem.h>
#include <linux/task_work.h>
#include <linux/sched/task.h>

#ifdef CONFIG_OPLUS_MOUNT_BLOCK
#include "oplus_guard_general.h"

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#include <linux/oplus_kevent.h>
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */

#ifdef OPLUS_DISALLOW_KEY_INTERFACES
int oplus_mount_block(const char __user *dir_name, unsigned long flags)
{
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
	struct kernel_packet_info* dcs_event;
	char dcs_stack[sizeof(struct kernel_packet_info) + 256];
	const char* dcs_event_tag = "kernel_event";
	const char* dcs_event_id = "mount_report";
	char* dcs_event_payload = NULL;
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */


 	char dname[16] = {0};
	if (dir_name != NULL && copy_from_user(dname,dir_name,8) == 0){
		if ((!strncmp(dname, "/system", 8) || !strncmp(dname, "/vendor", 8))&& !(flags & MS_RDONLY)
			&& (is_normal_boot_mode())) {
			printk(KERN_ERR "[OPLUS]System partition is not permitted to be mounted as readwrite\n");
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		printk(KERN_ERR "do_mount:kevent_send_to_user\n");

		dcs_event = (struct kernel_packet_info*)dcs_stack;
		dcs_event_payload = dcs_stack +
		sizeof(struct kernel_packet_info);

		dcs_event->type = 2;

		strncpy(dcs_event->log_tag, dcs_event_tag,
			sizeof(dcs_event->log_tag));
		strncpy(dcs_event->event_id, dcs_event_id,
			sizeof(dcs_event->event_id));

		dcs_event->payload_length = snprintf(dcs_event_payload, 256, "partition@@system");
		if (dcs_event->payload_length < 256) {
			dcs_event->payload_length += 1;
		}

		kevent_send_to_user(dcs_event);
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */
			return -EPERM;
		}
	}

	return 0;
}
#else
int oplus_mount_block(const char __user *dir_name, unsigned long flags)
{
	return 0;
}
#endif /* OPLUS_DISALLOW_KEY_INTERFACES */
#endif /* CONFIG_OPLUS_MOUNT_BLOCK */
#endif /* CONFIG_OPLUS_SECURE_GUARD */