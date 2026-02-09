vendor := $(srctree)/$(src)

ifneq "$(wildcard $(vendor)/oplus)" ""
	subdir-y += oplus
else
ifneq "$(wildcard $(vendor)/qcom)" ""
	subdir-y += qcom
endif
endif
