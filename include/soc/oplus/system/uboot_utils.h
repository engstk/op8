/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __OPLUS_UBOOT_UTIL__
#define __OPLUS_UBOOT_UTIL__

#include <linux/kmsg_dump.h>
#include <soc/qcom/minidump.h>

#define   SIZE_128B  0x00000080
#define   SIZE_256B  0x00000100
#define   SIZE_1KB   0x00000400
#define   SIZE_2KB   0x00000800
#define   SIZE_3KB   0x00000C00
#define   SIZE_4KB   0x00001000
#define   SIZE_5KB   0x00001400
#define   SIZE_6KB   0x00001800
#define   SIZE_8KB   0x00002000
#define   SIZE_10KB  0x00002800
#define   SIZE_12KB  0x00003000
#define   SIZE_16KB  0x00004000
#define   SIZE_18KB  0x00004800
#define   SIZE_20KB  0x00005000
#define   SIZE_22KB  0x00005800
#define   SIZE_24KB  0x00006000
#define   SIZE_26KB  0x00006800
#define   SIZE_28KB  0x00007000
#define   SIZE_30KB  0x00007800
#define   SIZE_32KB  0x00008000
#define   SIZE_36KB  0x00009000
#define   SIZE_34KB  0x00008800
#define   SIZE_38KB  0x00009800
#define   SIZE_36KB  0x00009000
#define   SIZE_40KB  0x0000A000
#define   SIZE_42KB  0x0000A800
#define   SIZE_44KB  0x0000B000
#define   SIZE_46KB  0x0000B800
#define   SIZE_48KB  0x0000C000
#define   SIZE_52KB  0x0000D000
#define   SIZE_56KB  0x0000E000
#define   SIZE_60KB  0x0000F000
#define   SIZE_64KB  0x00010000
#define   SIZE_66KB  0x00010800
#define   SIZE_68KB  0x00011000
#define   SIZE_70KB  0x00011800
#define   SIZE_72KB  0x00012000
#define   SIZE_76KB  0x00013000
#define   SIZE_78KB  0x00013800
#define   SIZE_80KB  0x00014000
#define   SIZE_84KB  0x00015000
#define   SIZE_88KB  0x00016000
#define   SIZE_90KB  0x00016800
#define   SIZE_92KB  0x00017000
#define   SIZE_94KB  0x00017800
#define   SIZE_95KB  0x00017C00
#define   SIZE_96KB  0x00018000
#define  SIZE_100KB  0x00019000
#define  SIZE_110KB  0x0001B800
#define  SIZE_120KB  0x0001E000
#define  SIZE_128KB  0x00020000
#define  SIZE_140KB  0x00023000
#define  SIZE_144KB  0x00024000
#define  SIZE_148KB  0x00025000
#define  SIZE_152KB  0x00026000
#define  SIZE_153KB  0x00026400
#define  SIZE_154KB  0x00026800
#define  SIZE_156KB  0x00027000
#define  SIZE_160KB  0x00028000
#define  SIZE_172KB  0x0002B000
#define  SIZE_184KB  0x0002E000
#define  SIZE_192KB  0x00030000
#define  SIZE_204KB  0x00033000
#define  SIZE_212KB  0x00035000
#define  SIZE_216KB  0x00036000
#define  SIZE_232KB  0x0003A000
#define  SIZE_240KB  0x0003C000
#define  SIZE_256KB  0x00040000
#define  SIZE_294KB  0x00049800
#define  SIZE_296KB  0x0004A000
#define  SIZE_298KB  0x0004A800
#define  SIZE_300KB  0x0004B000
#define  SIZE_306KB  0x0004C800
#define  SIZE_310KB  0x0004D800
#define  SIZE_312KB  0x0004E000
#define  SIZE_314KB  0x0004E800
#define  SIZE_316KB  0x0004f000
#define  SIZE_320KB  0x00050000
#define  SIZE_324KB  0x00051000
#define  SIZE_332KB  0x00053000
#define  SIZE_340KB  0x00055000
#define  SIZE_344KB  0x00056000
#define  SIZE_350KB  0x00057800
#define  SIZE_392KB  0x00062000
#define  SIZE_400KB  0x00064000
#define  SIZE_402KB  0x00064800
#define  SIZE_416KB  0x00068000
#define  SIZE_436KB  0x0006D000
#define  SIZE_440KB  0x0006E000
#define  SIZE_448KB  0x00070000
#define  SIZE_450KB  0x00070800
#define  SIZE_454KB  0x00071800
#define  SIZE_456KB  0x00072000
#define  SIZE_458KB  0x00072800
#define  SIZE_460KB  0x00073000
#define  SIZE_466KB  0x00074800
#define  SIZE_470KB  0x00075800
#define  SIZE_478KB  0x00077800
#define  SIZE_482KB  0x00078800
#define  SIZE_486KB  0x00079800
#define  SIZE_490KB  0x0007A800
#define  SIZE_498KB  0x0007C800
#define  SIZE_504KB  0x0007e000
#define  SIZE_506KB  0x0007e800
#define  SIZE_512KB  0x00080000
#define  SIZE_530KB  0x00084800
#define  SIZE_545KB  0x00088400
#define  SIZE_561KB  0x0008C400
#define  SIZE_612KB  0x00099000
#define  SIZE_680KB  0x000AA000
#define  SIZE_712KB  0x000B2000
#define  SIZE_716KB  0x000B3000
#define  SIZE_724KB  0x000B5000
#define    SIZE_1MB  0x00100000
#define SIZE_1_5_MB  0x00180000
#define    SIZE_2MB  0x00200000
#define    SIZE_6MB  0x00600000
#define   SIZE_16MB  0x01000000
#define   SIZE_17MB  0x01100000
#define   SIZE_68MB  0x04400000
#define   SIZE_72MB  0x04800000
#define   SIZE_80MB  0x05000000
#define  SIZE_150MB  0x09600000
#define    SIZE_1GB  0x40000000
#define  SIZE_1_5GB  0x60000000
#define    SIZE_2GB  0x80000000

#define SCL_SBL1_DDR_BASE                       0x80700000
#define SCL_SBL1_DDR_SIZE                       SIZE_1MB

#define SCL_SBL1_DDR_BOOT_LOG_BUF_SIZE          SIZE_8KB
#define SCL_SBL1_DDR_BOOT_TIME_MARKER_BUF_SIZE  SIZE_256B
#define SERIAL_BUFFER_SIZE                      SIZE_32KB

static void *uboot_reserved_remap = NULL;
static phys_addr_t uboot_base;
static phys_addr_t uboot_size;


#if defined(CONFIG_PRINTK) && defined(CONFIG_OPLUS_FEATURE_UBOOT_LOG)
bool back_kmsg_dump_get_buffer(struct kmsg_dumper *dumper, bool syslog,
			  char *buf, size_t size, size_t *len);

#else
static inline bool back_kmsg_dump_get_buffer(struct kmsg_dumper *dumper, bool syslog,
			  char *buf, size_t size, size_t *len)
{
	return false;
}
#endif

#endif/*__OPLUS_UBOOT_UTIL__*/
