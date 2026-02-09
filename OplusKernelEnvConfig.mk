# Copyright (C), 2008-2030, OPLUS Mobile Comm Corp., Ltd
### All rights reserved.
###
### File: - OplusKernelEnvConfig.mk
### Description:
###     you can get the oplus feature variables set in android side in this file
###     this file will add global macro for common oplus added feature
###     BSP team can do customzation by referring the feature variables
### Version: 1.0
### Date: 2020-03-18
### Author: Liang.Sun
###
### ------------------------------- Revision History: ----------------------------
### <author>                        <date>       <version>   <desc>
### ------------------------------------------------------------------------------
##################################################################################

ALLOWED_MACROS := \
OPLUS_ARCH_EXTENDS \
OPLUS_ARCH_INJECT \
OPLUS_BUG_COMPATIBILITY \
OPLUS_BUG_DEBUG \
OPLUS_BUG_STABILITY \
OPLUS_FEATURE_ADFR \
OPLUS_FEATURE_ADFR_KERNEL \
OPLUS_FEATURE_ADSP_RECOVERY \
OPLUS_FEATURE_AUDIODETECT \
OPLUS_FEATURE_AUDIO_FTM \
OPLUS_FEATURE_CAMERA_COMMON \
OPLUS_FEATURE_CAMERA_OIS \
OPLUS_FEATURE_CHG_BASIC \
OPLUS_FEATURE_DUMPDEVICE \
OPLUS_FEATURE_IMPEDANCE_MATCH \
OPLUS_FEATURE_KTV \
OPLUS_FEATURE_MI2S_SLAVE \
OPLUS_FEATURE_MIC_VA_MIC_CLK_SWITCH \
OPLUS_FEATURE_PXLW_IRIS5 \
OPLUS_FEATURE_SENSOR_SMEM \
OPLUS_FEATURE_SSR \
OPLUS_FEATURE_STORAGE_TOOL \
OPLUS_FEATURE_TP_BASIC \
OPLUS_FEATURE_TP_BSPFWUPDATE \
OPLUS_FEATURE_UFSPLUS \
OPLUS_FEATURE_UFS_DRIVER \
OPLUS_FEATURE_WIFI_BDF \
OPLUS_FEATURE_WIFI_DUALSTA_AP_BLACKLIST \
VENDOR_EDIT

$(foreach myfeature,$(ALLOWED_MACROS),\
         $(eval KBUILD_CFLAGS += -D$(myfeature)) \
         $(eval KBUILD_CPPFLAGS += -D$(myfeature)) \
         $(eval CFLAGS_KERNEL += -D$(myfeature)) \
         $(eval CFLAGS_MODULE += -D$(myfeature)) \
)

export OPLUS_FEATURE_ADFR_KERNEL=yes
export OPLUS_FEATURE_CAMERA_COMMON=yes
export OPLUS_FEATURE_PXLW_IRIS5=yes
export OPLUS_FEATURE_UFSPLUS=yes
