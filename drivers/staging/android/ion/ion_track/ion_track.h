/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

static struct ion_device *internal_dev = NULL;

static inline void update_internal_dev(struct ion_device *dev)
{
	if (!internal_dev)
		internal_dev = dev;
}
