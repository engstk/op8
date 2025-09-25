obj-y     +=  oplus_fp_common.o fingerprint_event.o
ccflags-$(CONFIG_OPLUS_FINGERPRINT_GKI_DISABLE) += -DFINGERPRINT_GKI_DISABLE

### Platform marco
ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
endif
