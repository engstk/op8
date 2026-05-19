# SPDX-License-Identifier: GPL-2.0-only
GCC_VERSION      := $(shell $(CONFIG_SHELL) $(srctree)/scripts/gcc-version.sh $(CROSS_COMPILE)gcc)
ccflags-y += -I $(srctree)/drivers/media/video/msm
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/common
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/pproc/cpp
ccflags-y += -I $(srctree)/drivers/media/platform/msm/camera_v2/msm_buf_mgr/

obj-$(CONFIG_MSM_FD) += msm_fd_dev.o msm_fd_hw.o
