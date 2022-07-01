# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.

oplus_midas_device-objs := midas_dev.o midas_ioctl.o
obj-$(CONFIG_OPLUS_FEATURE_MIDAS) += oplus_midas_device.o

oplus_binder_stats-objs := binder_stats_dev.o
obj-$(CONFIG_OPLUS_FEATURE_BINDER_STATS_ENABLE) += oplus_binder_stats.o

obj-$(CONFIG_OPLUS_FEATURE_SET_ALL_VPU_LATENCY) += vpu_pw_off_latency_proc.o

obj-$(CONFIG_OPLUS_FEATURE_DISPCAP) += dispcap_dev.o

ccflags-y += -I$(srctree)/drivers/staging

