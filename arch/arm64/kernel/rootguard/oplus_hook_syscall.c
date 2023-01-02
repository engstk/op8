// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved..
* VENDOR_EDIT
* File       : oplus_hook_syscall.c
* Description: replace the syscall.c
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/errno.h>
#include <linux/nospec.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>

#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/fpsimd.h>
#include <asm/syscall.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

#ifdef CONFIG_OPLUS_SECURE_GUARD
#define KERNEL_ADDR_LIMIT 0x0000008000000000
#include <asm/uaccess.h>
#include <linux/cred.h>
#include <linux/selinux.h>
#include "oplus_root.h"
#endif /* CONFIG_OPLUS_SECURE_GUARD */

long compat_arm_syscall(struct pt_regs *regs, int scno);
long sys_ni_syscall(void);

#ifdef CONFIG_OPLUS_ROOT_CHECK
extern void oplus_root_check_succ(uid_t uid, uid_t euid, uid_t fsuid, uid_t callnum);
extern bool is_unlocked(void);
extern void oplus_root_reboot(void);
#endif /* CONFIG_OPLUS_ROOT_CHECK */

static long oplus_do_ni_syscall(struct pt_regs *regs, int scno)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs, scno);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	return sys_ni_syscall();
}

static long __oplus_invoke_syscall(struct pt_regs *regs, syscall_fn_t syscall_fn)
{
	return syscall_fn(regs);
}

void oplus_invoke_syscall(struct pt_regs *regs, unsigned int scno,
			   unsigned int sc_nr,
			   const syscall_fn_t syscall_table[])
{
	long ret;
#ifdef CONFIG_OPLUS_ROOT_CHECK

	unsigned int IntUid_1st = current_uid().val;
	unsigned int IntEuid_1st = current_euid().val;
	unsigned int IntFsuid_1st = current_fsuid().val;
#endif /* CONFIG_OPLUS_ROOT_CHECK */
	if (scno < sc_nr) {
		syscall_fn_t syscall_fn;
		syscall_fn = syscall_table[array_index_nospec(scno, sc_nr)];
		ret = __oplus_invoke_syscall(regs, syscall_fn);
	} else {
		ret = oplus_do_ni_syscall(regs, scno);
	}

#ifdef CONFIG_OPLUS_ROOT_CHECK

	if ((IntUid_1st != 0) && (is_unlocked() == 0) ){
		if((scno != __NR_setreuid32) && (scno != __NR_setregid32) && (scno != __NR_setresuid32) && (scno != __NR_setresgid32) && (scno != __NR_setuid32) && (scno != __NR_setgid32)
		&& (scno != __NR_setreuid) && (scno != __NR_setregid) && (scno != __NR_setresuid) && (scno != __NR_setresgid) && (scno != __NR_setuid) && (scno != __NR_setgid)
		){
		//make sure the addr_limit in kernel space
		//KERNEL_DS:        0x7f ffff ffff
		//KERNEL_ADDR_LIMIT:0x80 0000 0000
			if (((current_uid().val < IntUid_1st) || (current_euid().val < IntEuid_1st) || (current_fsuid().val < IntFsuid_1st)) || (get_fs() > KERNEL_ADDR_LIMIT)) {
				oplus_root_check_succ(IntUid_1st, IntEuid_1st, IntFsuid_1st, scno);
				oplus_root_reboot();
			}
		}
	}
#endif /* CONFIG_OPLUS_ROOT_CHECK */
	regs->regs[0] = ret;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) */
