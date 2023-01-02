/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************
* Copyright (c)  2008- 2030  OPLUS Mobile communication Corp.ltd All rights reserved.
*
* File       : oplus_mm_fb_netlink.h
* Description : For oplus_mm_fb_netlink
* Version   : 1.0
* Date        : 2020-11-24
* Author    :
* TAG         :
****************************************************************/
#ifndef _OPLUS_MM_NETLINK_H
#define _OPLUS_MM_NETLINK_H

#define OPLUS_MM_MSG_TO_KERNEL_BUF_LEN		256
#define OPLUS_MM_MSG_FROM_KERNEL_BUF_LEN		(1024)


#define OPLUS_MM_PROTOCAL_NAME				"mm_fb"
#define OPLUS_MM_GENL_VERSION					0x01
#define OPLUS_MM_PROTOCAL_NAME_MAX_LEN		100

enum {
	MM_FB_CMD_ATTR_UNSPEC = 0,
	MM_FB_CMD_ATTR_MSG,
	MM_FB_CMD_ATTR_OPT,
	__MM_FB_CMD_ATTR_MAX,
};

#define MM_FB_CMD_ATTR_MAX 	(__MM_FB_CMD_ATTR_MAX - 1)

enum {
	MM_FB_CMD_GENL_UNSPEC = 0,
	MM_FB_CMD_GENL_SEND_MODULE,
	MM_FB_CMD_GENL_UPLOAD,
	MM_FB_CMD_GENL_TEST_UPLOAD,
};


struct msg_to_kernel {
	struct nlmsghdr n_hd;
	struct genlmsghdr g_hd;
	char buf[OPLUS_MM_MSG_TO_KERNEL_BUF_LEN];
};

struct msg_from_kernel {
	struct nlmsghdr n_hd;
	struct genlmsghdr g_hd;
	char buf[OPLUS_MM_MSG_FROM_KERNEL_BUF_LEN];
};

#endif //_OPLUS_MM_NETLINK_H