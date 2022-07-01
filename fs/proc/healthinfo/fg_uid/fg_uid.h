/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#ifndef _FG_UID_H
#define _FG_UID_H

#include <linux/spinlock.h>

#define MAX_ARRAY_LENGTH 256
#define FS_FG_INFO_PATH "fg_info"
#define FS_FG_UIDS "fg_uids"

extern bool is_fg(int uid);
struct fg_info {
    int fg_num;
    int fg_uids;
};

#endif /*_FG_UID_H*/

