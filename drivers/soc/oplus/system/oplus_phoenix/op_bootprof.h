// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*=============================================================================

                            INCLUDE FILES FOR MODULE

=============================================================================*/
#ifndef OP_BOOTPROF_H
#define  OP_BOOTPROF_H
#include <linux/seq_file.h>
#define SEQ_printf(m, x...)	    \
	do {			    \
		if (m)		    \
			seq_printf(m, x);	\
		else		    \
			pr_info(x);	    \
	} while (0)


void op_log_boot(const char *str);

#endif
