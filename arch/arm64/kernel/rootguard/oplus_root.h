/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved..
* VENDOR_EDIT
* File       : oplus_root.h
* Description: For rootguard syscall num
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/

#ifdef CONFIG_OPLUS_SECURE_GUARD
#ifndef __ARCH_ARM64_OPLUS_ROOT_H_
#define __ARCH_ARM64_OPLUS_ROOT_H_

#define __NR_setreuid32    203
#define __NR_setregid32    204
#define __NR_setresuid32   208
#define __NR_setresgid32   210
#define __NR_setuid32      213
#define __NR_setgid32      214

#define __NR_setregid      143
#define __NR_setgid        144
#define __NR_setreuid      145
#define __NR_setuid        146
#define __NR_setresuid     147
#define __NR_setresgid     149

#endif
#endif /* CONFIG_OPLUS_SECURE_GUARD */
