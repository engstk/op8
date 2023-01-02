/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************
* Copyright (c)  2008- 2030  OPLUS Mobile communication Corp.ltd All rights reserved.
*
* File       : oplus_feedback_guard_netlink.h
* Description: For oplus_feedback_guard_netlink
* Version   : 1.0
* Date        : 2020-11-24
* Author    :
* TAG         :
****************************************************************/
#ifndef _OPLUS_FB_GUARD_NETLINK_H
#define _OPLUS_FB_GUARD_NETLINK_H

#define OPLUS_FB_GUARD_TEST_NETLINK_CMD_MIN			0
#define OPLUS_FB_GUARD_TEST_NETLINK_SEND_PID		1
#define OPLUS_FB_GUARD_TEST_NETLINK_RECEV			2
#define OPLUS_FB_GUARD_TEST_NETLINK_CMD_MAX			3

#define OPLUS_FB_GUARD_MSG_TO_KERNEL_BUF_LEN		256
#define OPLUS_FB_GUARD_MSG_FROM_KERNEL_BUF_LEN		(2048 + 128)


#define OPLUS_FB_GUARD_PROTOCAL_NAME				"fb_guard"
#define OPLUS_FB_GUARD_GENL_VERSION					0x01
#define OPLUS_FB_GUARD_PROTOCAL_NAME_MAX_LEN		100

enum {
	FB_GUARD_CMD_ATTR_UNSPEC = 0,
	FB_GUARD_CMD_ATTR_MSG,
	FB_GUARD_CMD_ATTR_OPT,
	__FB_GUARD_CMD_ATTR_MAX,
};

#define FB_GUARD_CMD_ATTR_MAX 	(__FB_GUARD_CMD_ATTR_MAX - 1)

enum {
	FB_GUARD_CMD_GENL_UNSPEC = 0,
	FB_GUARD_CMD_GENL_SENDPID,
	FB_GUARD_CMD_GENL_UPLOAD,
	FB_GUARD_CMD_GENL_TEST_UPLOAD,
};


struct msg_to_kernel {
	struct nlmsghdr n_hd;
	struct genlmsghdr g_hd;
	char buf[OPLUS_FB_GUARD_MSG_TO_KERNEL_BUF_LEN];
};

struct msg_from_kernel {
	struct nlmsghdr n_hd;
	struct genlmsghdr g_hd;
	char buf[OPLUS_FB_GUARD_MSG_FROM_KERNEL_BUF_LEN];
};

struct msg_test_upload {
	unsigned int pro_pid;
	unsigned int val;
};

int oplus_fb_guard_test_netlink(int cmd, unsigned int val);

#endif
