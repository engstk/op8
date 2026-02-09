// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_NU2112A_H_
#define _OPLUS_NU2112A_H_

/************************VOOCPHY REG***********************************/

/* Register 00h */
#define NU2112A_REG_00 0x00
#define NU2112A_BAT_OVP_DIS_MASK 0x80
#define NU2112A_BAT_OVP_DIS_SHIFT 7
#define NU2112A_BAT_OVP_ENABLE 0
#define NU2112A_BAT_OVP_DISABLE 1

/* Register 01h */
#define NU2112A_REG_01 0x01

/* Register 02h */
#define NU2112A_REG_02 0x02

/* Register 03h */
#define NU2112A_REG_03 0x03

/* Register 04h */
#define NU2112A_REG_04 0x04

/* Register 05h */
#define NU2112A_REG_05 0x05
#define NU2112A_CHG_EN_MASK 0x80
#define NU2112A_PMID2OUT_OVP_DIS_SHIFT 7
#define NU2112A_PMID2OUT_OVP_ENABLE 0
#define NU2112A_PMID2OUT_OVP_DISABLE 1

/* Register 06h */
#define NU2112A_REG_06 0x06

/* Register 07h */
#define NU2112A_REG_07 0x07
#define NU2112A_CHG_EN_MASK 0x80
#define NU2112A_CHG_EN_SHIFT 7
#define NU2112A_CHG_ENABLE 1
#define NU2112A_CHG_DISABLE 0

#define NU2112A_REG_RESET_MASK 0x40
#define NU2112A_REG_RESET_SHIFT 6
#define NU2112A_NO_REG_RESET 0
#define NU2112A_RESET_REG 1

/* Register 08h */
#define NU2112A_REG_08 0x08

/* Register 09h */
#define NU2112A_REG_09 0x09

/* Register 0Ah */
#define NU2112A_REG_0A 0x0A

/* Register 0Bh */
#define NU2112A_REG_0B 0x0B

/* Register 0Ch */
#define NU2112A_REG_0C 0x0C
#define NU2112A_IBUS_UCP_DIS_MASK 0x80
#define NU2112A_IBUS_UCP_DIS_SHIFT 7
#define NU2112A_IBUS_UCP_ENABLE 0
#define NU2112A_IBUS_UCP_DISABLE 1

/* Register 0Dh */
#define NU2112A_REG_0D 0x0D

/* Register 0Eh */
#define NU2112A_REG_0E 0x0E

/* Register 0Fh */
#define NU2112A_REG_0F 0x0F

/* Register 10h */
#define NU2112A_REG_10 0x10
#define NU2112A_CP_SWITCHING_STAT_MASK 0x10
#define NU2112A_CP_SWITCHING_STAT_SHIFT 4

/* Register 11h */
#define NU2112A_REG_11 0x11
#define NU2112A_VBAT_OVP_FLAG_MASK 0x40
#define NU2112A_VBAT_OVP_FLAG_SHIFT 6
#define NU2112A_IBAT_OCP_FLAG_MASK 0x20
#define NU2112A_IBAT_OCP_FLAG_SHIFT 5
#define NU2112A_VBUS_OVP_FLAG_MASK 0x10
#define NU2112A_VBUS_OVP_FLAG_SHIFT 4
#define NU2112A_IBUS_OCP_FLAG_MASK 0x08
#define NU2112A_IBUS_OCP_FLAG_SHIFT 3

/* Register 12h */
#define NU2112A_REG_12 0x12
#define NU2112A_TSD_FLAG_MASK 0x80
#define NU2112A_TSD_FLAG_SHIFT 7
#define NU2112A_PMID2VOUT_OVP_FLAG_MASK 0x20
#define NU2112A_PMID2VOUT_OVP_FLAG_SHIFT 5
#define NU2112A_PMID2VOUT_UVP_FLAG_MASK 0x10
#define NU2112A_PMID2VOUT_UVP_FLAG_SHIFT 4

/* Register 13h */
#define NU2112A_REG_13 0x13

/* Register 14h */
#define NU2112A_REG_14 0x14
#define NU2112A_SS_TIMEOUT_FLAG_MASK 0x04
#define NU2112A_SS_TIMEOUT_FLAG_SHIFT 2

#define NU2112A_PIN_DIAG_FALL_FLAG_MASK 0x01
#define NU2112A_PIN_DIAG_FALL_FLAG_SHIFT 0

/* Register 15h */
#define NU2112A_REG_15 0x15

/* Register 16h */
#define NU2112A_REG_16 0x16

/* Register 17h */
#define NU2112A_REG_17 0x17

/* Register 18h */
#define NU2112A_REG_18 0x18
#define NU2112A_ADC_ENABLE_SHIFT 7

/* Register 19h */
#define NU2112A_REG_19 0x19

/* Register 1Ah */
#define NU2112A_REG_1A 0x1A
#define NU2112A_IBUS_POL_H_MASK 0x1F
#define NU2112A_IBUS_ADC_LSB 1

/* Register 1Bh */
#define NU2112A_REG_1B 0x1B

/* Register 1Ch */
#define NU2112A_REG_1C 0x1C
#define NU2112A_VBUS_POL_H_MASK 0x7F
#define NU2112A_VBUS_ADC_LSB 1

/* Register 1Dh */
#define NU2112A_REG_1D 0x1D

/* Register 1Eh */
#define NU2112A_REG_1E 0x1E
#define NU2112A_VAC_POL_H_MASK 0x7F
#define NU2112A_VAC_ADC_LSB 1

/* Register 1Fh */
#define NU2112A_REG_1F 0x1F
#define NU2112A_VAC_POL_L_MASK 0xFF

/* Register 20h */
#define NU2112A_REG_20 0x20
#define NU2112A_VOUT_POL_H_MASK 0x1F
#define NU2112A_VOUT_ADC_LSB 1

/* Register 21h */
#define NU2112A_REG_21 0x21

/* Register 22h */
#define NU2112A_REG_22 0x22
#define NU2112A_VBAT_POL_H_MASK 0x1F
#define NU2112A_VBAT_ADC_LSB 1

/* Register 23h */
#define NU2112A_REG_23 0x23
#define NU2112A_VBAT_POL_L_MASK 0xFF

/* Register 24h */
#define NU2112A_REG_24 0x24

/* Register 2Bh */
#define NU2112A_REG_2B 0x2B
#define NU2112A_VOOC_EN_MASK 0x80
#define NU2112A_VOOC_EN_SHIFT 7
#define NU2112A_VOOC_ENABLE 1
#define NU2112A_VOOC_DISABLE 0

#define NU2112A_SOFT_RESET_MASK 0x02
#define NU2112A_SOFT_RESET_SHIFT 1
#define NU2112A_SOFT_RESET 1

#define NU2112A_SEND_SEQ_MASK 0x01
#define NU2112A_SEND_SEQ_SHIFT 0
#define NU2112A_SEND_SEQ 1

/* Register 2Ch */
#define NU2112A_REG_2C 0x2C
#define NU2112A_TX_WDATA_POL_H_MASK 0x03

/* Register 2Dh */
#define NU2112A_REG_2D 0x2D
#define NU2112A_TX_WDATA_POL_L_MASK 0xFF

/* Register 2Eh */
#define NU2112A_REG_2E 0x2E
#define NU2112A_RX_RDATA_POL_H_MASK 0xFF

/* Register 2Fh */
#define NU2112A_REG_2F 0x2F

/* Register 30h */
#define NU2112A_REG_30 0x30

/* Register 31h */
#define NU2112A_REG_31 0x31
#define NU2112A_PRE_WDATA_POL_H_MASK 0x03

/* Register 32h */
#define NU2112A_REG_32 0x32
#define NU2112A_PRE_WDATA_POL_L_MASK 0xFF

/* Register 33h */
#define NU2112A_REG_33 0x33

/* Register 35h */
#define NU2112A_REG_35 0x35

/* Register 36h */
#define NU2112A_REG_36 0x36

/* Register 3Ah */
#define NU2112A_REG_3A 0x3A

/* Register 3Ch */
#define NU2112A_REG_3C 0x3C

#endif /*_OPLUS_NU2112A_H_*/
