// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _RESERVE_AREA_H_
#define _RESERVE_AREA_H_
#include <linux/mm.h>
#include <linux/ratelimit.h>

#define SIZE_10M 0xA00000
#define GET_UNMMAPED_AREA_FIRST_LOW_LIMIT 0x12c00000
#define ZYGOTE_HEAP_DEFAULT_SIZE (256 << 20)

#define FLAG_BITS 10
#define RESERVE_AREA         0x001
#define ANTI_FRAGMENT_AREA   0x002
#define ZYGOTE_HEAP          0x004
#define RESERVE_LOGGING      0x200

//do not support ZYGOTE_HEAP currently
#define RESERVE_FEATURES (RESERVE_AREA | ANTI_FRAGMENT_AREA | RESERVE_LOGGING)
#define HEAP_SIZE_MASK (~((1 <<FLAG_BITS) - 1))

#define ANTI_FRAGMENT_AREA_BASE_SIZE 0x4900000
#define ANTI_FRAGMENT_AREA_MASK 0x1e00000
#define ANTI_FRAGMENT_AREA_ALIGN (~(0xffff))

extern int svm_oom_pid;
extern unsigned long svm_oom_jiffies;

extern unsigned long get_unmmaped_area_from_anti_fragment(struct mm_struct *mm,
		struct vm_unmapped_area_info *info);
extern unsigned long get_unmmaped_area_from_reserved(struct mm_struct *mm,
		struct vm_unmapped_area_info *info);
extern int get_va_feature_value(unsigned int feature);
extern void trigger_svm_oom_event(struct mm_struct *mm, bool brk_risk, bool is_locked);
#ifdef CONFIG_DUMP_MM_INFO
void dump_mm_info(unsigned long len, unsigned long flags, int dump_vma);
#else /* CONFIG_DUMP_MM_INFO */
static inline void dump_mm_info(unsigned long len, unsigned long flags, int dump_vma)
{
}
#endif/* CONFIG_DUMP_MM_INFO */

#define GET_UNMMAPED_AREA_FIRST_TIME(info) do {    \
	if (mm->va_feature & RESERVE_AREA)                  \
	info.low_limit = max_t(unsigned long,\
			GET_UNMMAPED_AREA_FIRST_LOW_LIMIT, info.low_limit); \
} while (0);

#define GET_UNMMAPED_AREA_THIRD_TIME(info, mm, addr) do {            \
	if ((mm->va_feature & RESERVE_AREA) && offset_in_page(addr))          \
		addr = get_unmmaped_area_from_reserved(mm, &info);   \
} while (0);

#define GET_UNMMPAED_AREA_FROME_ANTI_FRAGMENT(info, mm) do {        \
	if ((mm->va_feature & ANTI_FRAGMENT_AREA) && \
			(info)->high_limit == mm->mmap_base)  { \
		unsigned long addr = get_unmmaped_area_from_anti_fragment(mm, info);  \
		if (!offset_in_page(addr)) \
			return addr; \
	} \
} while (0);

#ifdef CONFIG_MMU
extern void special_arch_pick_mmap_layout(struct mm_struct *mm);
#else
void special_arch_pick_mmap_layout(struct mm_struct *mm) {}
#endif

static inline void update_oom_pid_and_time(unsigned long len, unsigned long val,
		unsigned long flags)
{
	static DEFINE_RATELIMIT_STATE(svm_log_limit, 1*HZ, 1);
	static DEFINE_RATELIMIT_STATE(dump_mm, 300*HZ, 1);

	if (!IS_ERR_VALUE(val))
		return;

	if (__ratelimit(&svm_log_limit)) {
		svm_oom_pid = current->tgid;
		svm_oom_jiffies = jiffies;
	}

	if (__ratelimit(&dump_mm))
		dump_mm_info(len, flags, 1);
	else
		dump_mm_info(len, flags, 0);
}
#endif
