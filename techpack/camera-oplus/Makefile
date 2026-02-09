# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(OPLUS_FEATURE_CAMERA_COMMON), yes)

# auto-detect subdirs
ifeq ($(CONFIG_ARCH_KONA), y)
include $(srctree)/techpack/camera-oplus/config/konacamera.conf
endif

ifeq ($(CONFIG_ARCH_KONA), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera-oplus/config/konacameraconf.h
endif

ifdef CONFIG_SPECTRA_CAMERA
# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE     += \
                -I$(srctree)/techpack/camera-oplus/include/uapi

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    += \
                -I$(srctree)/techpack/camera-oplus/include/uapi \
                -I$(srctree)/techpack/camera-oplus/include
obj-y += drivers/
else
$(info Target not found)
endif

endif
