// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
	yixue.ge@bsp.drv creat for can dump cpu register at panic
*/
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

.text
/*
 *  x0 = struct pt_regs
 */
ENTRY(get_arm64_cpuregs)
	stp x0, x1, [x0]
	stp x2, x3, [x0, 0x10]
	stp x4, x5, [x0, 0x20]
	stp x6, x7, [x0, 0x30]
	stp x8, x9, [x0, 0x40]
	stp x10, x11, [x0, 0x50]
	stp x12, x13, [x0, 0x60]
	stp x14, x15, [x0, 0x70]
	stp x16, x17, [x0, 0x80]
	stp x18, x19, [x0, 0x90]
	stp x20, x21, [x0, 0xA0]
	stp x22, x23, [x0, 0xB0]
	stp x24, x25, [x0, 0xC0]
	stp x26, x27, [x0, 0xd0]
	stp x28, x29, [x0, 0xe0]
	str x30, [x0, 0xf0]
	mov x1, sp
	stp x1, x30, [x0, 0xf8]
	mrs	x1, spsr_el1
	str x1, [x0, 0x108]
	ldr x1, [x0, 0x08]
ENDPROC(get_arm64_cpuregs)
