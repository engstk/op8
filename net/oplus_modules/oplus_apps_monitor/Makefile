#
# Makefile for the netfilter modules on top of IPv4.
#
KBUILD_OPTIONS += CONFIG_OPLUS_FEATURE_APP_MONITOR=m

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)
