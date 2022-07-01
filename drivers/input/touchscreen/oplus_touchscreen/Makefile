#
# Makefile for the touchscreen drivers.
#
subdir-ccflags-$(CONFIG_TOUCHPANEL_MTK_PLATFORM) += -I$(srctree)/drivers/misc/mediatek/include/

obj-$(CONFIG_TOUCHPANEL_OPLUS) += util_interface/
obj-$(CONFIG_TOUCHPANEL_OPLUS) += touchpanel_common_driver.o touchpanel_prevention.o
obj-$(CONFIG_TOUCHPANEL_ALGORITHM) += touchpanel_algorithm.o
obj-$(CONFIG_TOUCHPANEL_SYNAPTICS) += Synaptics/
obj-$(CONFIG_TOUCHPANEL_GOODIX) += Goodix/
obj-$(CONFIG_TOUCHPANEL_SAMSUNG) += Samsung/