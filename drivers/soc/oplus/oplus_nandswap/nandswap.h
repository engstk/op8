/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2020 Oplus. All rights reserved.
*/

#ifndef _NAND_SWAP_H
#define _NAND_SWAP_H

#include <linux/sched.h>

/* must check if this flag is repeat in <include/linux/mm.h> */
#define VM_NANDSWAP	0x8000000000UL	/* swapin mark */

#define AID_APP		10000		/* first app user */
#define AID_USER_OFFSET	100000		/* offset for uid ranges for each user */

enum {
	NS_OUT_STANDBY,
	NS_OUT_QUEUE,
	NS_OUT_CACHE,
	NS_OUT_DONE,
	NS_IN_QUEUE,
};

enum {
	NS_TYPE_FG,
	NS_TYPE_NAND_ACT,
	NS_TYPE_NAND_ALL,
	NS_TYPE_DROP,
	NS_TYPE_RATIO,
	NS_TYPE_END
};

extern struct task_struct *nswapoutd;
static inline bool current_is_nswapoutd()
{
	return current == nswapoutd;
}
#endif /* _NAND_SWAP_H */
