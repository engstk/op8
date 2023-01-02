ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
        dtbo-$(CONFIG_ARCH_LITO) += \
                avicii-overlay.dtbo \

avicii-overlay.dtbo-base := lito.dtb

else
dtb-$(CONFIG_ARCH_LITO) += lito-mtp.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files    := *.dtb *.dtbo
