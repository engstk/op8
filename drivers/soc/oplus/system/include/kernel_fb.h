/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __KERNEL_FEEDBACK_H
#define __KERNEL_FEEDBACK_H

typedef enum {
	FB_STABILITY = 0,
	FB_FS,
	FB_STORAGE,
	FB_SENSOR,
	FB_BOOT,
	FB_MAX_TYPE = FB_BOOT,
} fb_tag;

#define FB_STABILITY_ID_CRASH	"202007272030"


#define FB_SENSOR_ID_CRASH	"10004"
#define FB_SENSOR_ID_QMI	"202007272041"

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <../../../arch/arm64/kernel/secureguard/rootguard/oplus_kevent.h>
#else
#include <linux/oplus_kevent.h>
#endif
int oplus_kevent_fb(fb_tag tag_id, const char *event_id, unsigned char *payload);
int oplus_kevent_fb_str(fb_tag tag_id, const char *event_id, unsigned char *str);
#else
struct kernel_packet_info
{
    int type;	 /* 0:root,1:only string,other number represent other type */
    char log_tag[32];	/* logTag */
    char event_id[20];	  /*eventID */
    size_t payload_length;	  /* Length of packet data */
    unsigned char payload[0];	/* Optional packet data */
}__attribute__((packed));

int oplus_kevent_fb(fb_tag tag_id, const char *event_id, unsigned char *payload);
int oplus_kevent_fb_str(fb_tag tag_id, const char *event_id, unsigned char *str);
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
int fb_kevent_send_to_user(struct kernel_packet_info *userinfo);
#pragma GCC diagnostic pop
#endif
