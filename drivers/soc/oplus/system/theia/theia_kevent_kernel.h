/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/
#ifndef __THEIA_KEVENT_KERNEL_H_
#define __THEIA_KEVENT_KERNEL_H_

#define MAX_PAYLOAD_TAG_SIZE            128
#define MAX_PAYLOAD_EVENTID_SIZE         128
#define MAX_PAYLOAD_DATASIZE        1024
#define THEIA_KEVENT_CONNECT        0x01

/*
THEIA_KEVENT_TYPE_COMMON_STRING:
theia_kevent_packet.data: string
*/
#define THEIA_KEVENT_TYPE_COMMON_STRING 1

/*
THEIA_KEVENT_TYPE_DCS_MSG
theia_kevent_packet.tag: dcs tag
theia_kevent_packet.event_id: dcs event id
theia_kevent_packet.data: is logmap format: logmap{key1:value1;key2:value2;key3:value3 ...}
*/
#define THEIA_KEVENT_TYPE_DCS_MSG 2

#define THEIA_KEVENT_MODULE 	"theia_kevent"

struct theia_kevent_packet {
    int  type;                    /* 1: common string 2: dcs message*/
    char tag[MAX_PAYLOAD_TAG_SIZE];            /* tag */
    char event_id[MAX_PAYLOAD_EVENTID_SIZE];     /* eventID */
    size_t len;                 /* Length of packet data */
    unsigned char data[0];            /* Optional packet data */
}__attribute__((packed));

int theia_kevent_module_init(void);
void theia_kevent_module_exit(void);
void SendTheiaKevent(int type, char *log_tag, char *event_id, char *payload);
void SendDcsTheiaKevent(char *log_tag, char *event_id, char *logmap);

#endif