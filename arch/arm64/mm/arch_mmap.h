// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __ARCH_MMAP_H__
#define __ARCH_MMAP_H__
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)
static int mmap_is_legacy(struct rlimit *rlim_stack);
static unsigned long mmap_base(unsigned long rnd, struct rlimit *rlim_stack);
#else
static int mmap_is_legacy(void);
static unsigned long mmap_base(unsigned long rnd);
#endif
unsigned long arch_mmap_rnd(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)
void special_arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;
	unsigned long old_mmap_base = mm->mmap_base;
	unsigned long new_mmap_base;
	struct rlimit *rlim_stack = &current->signal->rlim[RLIMIT_STACK];

	if ((current->flags & PF_RANDOMIZE)
			&& !mmap_is_legacy(rlim_stack)) {
		random_factor = arch_mmap_rnd() % (1 << 25);
		new_mmap_base = mmap_base(random_factor, rlim_stack);
		mm->mmap_base = max_t(unsigned long, new_mmap_base, old_mmap_base);
	}
}
#else
void special_arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;
	unsigned long old_mmap_base = mm->mmap_base;
	unsigned long new_mmap_base;

	if ((current->flags & PF_RANDOMIZE)
			&& !mmap_is_legacy()) {
		random_factor = arch_mmap_rnd() % (1 << 25);
		new_mmap_base = mmap_base(random_factor);
		mm->mmap_base = max_t(unsigned long, new_mmap_base, old_mmap_base);
	}
}
#endif /* LINUX_VERSION_CODE */
#endif /* __ARCH_MMAP_H__ */
