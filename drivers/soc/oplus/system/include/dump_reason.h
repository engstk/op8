/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _DUMP_REASON_H
#define _DUMP_REASON_H

#ifdef CONFIG_OPLUS_FEATURE_DUMP_REASON

#define SMEM_DUMP_INFO 129
#define DUMP_REASON_SIZE 256

struct dump_info{
    char    dump_reason[DUMP_REASON_SIZE];  //dump reason
};

extern char *parse_function_builtin_return_address(unsigned long function_address);
extern void save_dump_reason_to_smem(char *info, char *function_name);
#else
static char *parse_function_builtin_return_address(unsigned long function_address) {}
static void save_dump_reason_to_smem(char *info, char *function_name) {}
#endif

#endif
