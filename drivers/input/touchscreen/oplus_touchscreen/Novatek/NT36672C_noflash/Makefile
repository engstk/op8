#
# Makefile for the touchscreen drivers.
#
ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
endif

subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/mt6763/

# Each configuration option enables a list of files.
#obj-$(CONFIG_TOUCHPANEL_NOVA_NT36672C_NOFLASH) += nvt_drivers_nt36672c_noflash.o
obj-y += nvt_drivers_nt36672c_noflash.o
#endif
