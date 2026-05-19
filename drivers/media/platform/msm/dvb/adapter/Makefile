# SPDX-License-Identifier: GPL-2.0-only

ccflags-y += -I $(srctree)/drivers/media/dvb-core/
ccflags-y += -I $(srctree)/drivers/media/platform/msm/dvb/include/
ccflags-y += -I $(srctree)/drivers/media/platform/msm/dvb/demux/

obj-$(CONFIG_DVB_MPQ) += mpq-adapter.o

mpq-adapter-y := mpq_adapter.o mpq_stream_buffer.o
