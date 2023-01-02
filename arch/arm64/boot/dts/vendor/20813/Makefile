ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_LAGOON) += \
		lagoon-mtp-ebba-overlay.dtbo

lagoon-mtp-ebba-overlay.dtbo-base := lagoon-ebba.dtb
else
dtb-$(CONFIG_ARCH_LAGOON) += \
		lagoon-mtp.dtb
endif
always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files    := *.dtb *.dtbo

