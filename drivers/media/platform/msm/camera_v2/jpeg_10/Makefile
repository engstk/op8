# SPDX-License-Identifier: GPL-2.0-only
GCC_VERSION      := $(shell $(CONFIG_SHELL) $(srctree)/scripts/gcc-version.sh $(CROSS_COMPILE)gcc)

ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/jpeg_10
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/sensor/io
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/common

obj-$(CONFIG_MSMB_JPEG) += msm_jpeg_dev.o msm_jpeg_sync.o msm_jpeg_core.o msm_jpeg_hw.o msm_jpeg_platform.o
