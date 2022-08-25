ifeq ($(CONFIG_OPLUS_SECURE_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
else ifeq ($(CONFIG_OPLUS_SECURE_MTK),y)
ccflags-y += -DMTK_PLATFORM
endif

ifneq ($(TARGET_PRODUCT),qssi)
obj-$(CONFIG_OPLUS_SECURE_COMMON)     +=  oplus_secure_common.o
endif
