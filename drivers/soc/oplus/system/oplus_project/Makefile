# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.
#ifdef OPLUS_BUG_STABILITY
ifeq ($(CONFIG_KASAN),y)
subdir-ccflags-y += -Wno-format
endif
#endif /*OPLUS_BUG_STABILITY*/
ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
ifeq ($(CONFIG_OPLUS_FEATURE_OPROJECT), y)
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT) 	+= qcom/oplus_project.o
else
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT)	+= gki/oplus_project.o
endif
else
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT) 	+= mtk/oplus_project.o mtk/oplus_project_oldcdt.o
endif
ifeq ($(CONFIG_MTK_SECURITY_SW_SUPPORT), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
endif
