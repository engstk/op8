# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.

# ifneq ($(OPLUS_FEATURE_LOWMEM_DBG),) disable this cause of some project not uses.
ifeq ($(PATCHLEVEL),)
$(error VERSION is not defined)
endif

ifeq ($(PATCHLEVEL),)
$(error PATCHLEVEL is not defined)
endif

CONFIG_KERVER_VERSION := $(VERSION).$(PATCHLEVEL)

ifeq ($(CONFIG_KERVER_VERSION),5.4)
obj-$(CONFIG_OPLUS_FEATURE_LOWMEM_DBG)   += lowmem_dbg_5.4.o
else
obj-$(CONFIG_OPLUS_FEATURE_LOWMEM_DBG)   += lowmem_dbg.o
endif


# mtk
ifeq ($(CONFIG_MTK_ION), y)
ifeq ($(CONFIG_KERVER_VERSION),4.19)
ccflags-$(CONFIG_MTK_ION)  += -I$(srctree)/drivers/staging/android/mtk_ion/
endif
ifeq ($(CONFIG_KERVER_VERSION),4.14)
ccflags-$(CONFIG_MTK_ION)  += -I$(srctree)/drivers/staging/android/ion/
endif
endif

# qcom
ccflags-$(CONFIG_QCOM_KGSL)  += -I$(srctree)/drivers/gpu/msm/
# endif
