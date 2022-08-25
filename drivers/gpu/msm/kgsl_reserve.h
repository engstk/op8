// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _KGSL_RESERVE_H_
#define _KGSL_RESERVE_H_
#include <linux/reserve_area.h>

static unsigned long _search_range(struct kgsl_process_private *private,
		struct kgsl_mem_entry *entry,
		unsigned long start, unsigned long end,
		unsigned long len, uint64_t align);

static inline unsigned long kgsl_get_unmmaped_area_from_anti_fragment(
		struct kgsl_process_private *private,
		struct kgsl_mem_entry *entry, unsigned long len,
		uint64_t align)
{
	uint64_t svm_start, svm_end;
	uint64_t lstart, lend;
	unsigned long lresult = 0;

	if (current->mm->va_feature & ANTI_FRAGMENT_AREA) {
		/* get the GPU pagetable's SVM range */
		if (kgsl_mmu_svm_range(private->pagetable, &svm_start, &svm_end,
					entry->memdesc.flags))
			return -ERANGE;

		switch (len) {
			case 4096: case 8192: case 16384: case 32768:
			case 65536: case 131072: case 262144:
				lend = current->mm->va_feature_rnd - (SIZE_10M * (ilog2(len) - 12));
				lstart = current->mm->va_feature_rnd - (SIZE_10M * 7);
				if (lend <= svm_end && lstart >= svm_start) {
					lresult = _search_range(private, entry, lstart, lend, len, align);
					if (!IS_ERR_VALUE(lresult))
						return lresult;
				}
			default:
				break;
		}
	}

	return 0;
}
#endif
