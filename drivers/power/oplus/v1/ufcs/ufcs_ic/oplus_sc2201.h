// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_SC2201_H_
#define _OPLUS_SC2201_H_
#include "../oplus_chg_track.h"

#define UFCS_IC_NAME "SC2201"

/********************** I2C Slave Addr **************************/
#define SC2201_I2C_ADDR (0x72)
#define SC2201_MAX_REG (0x0264)
#define SC2201_FLAG_NUM (10)

/********************** SC2201 I2C reg map **********************/
#define SC2201_ENABLE_REG_NUM (7)

/*reg02*/
#define SC2201_ADDR_GENERAL_CTRL (0x0002)
#define SC2201_CMD_REG_RST (0x0A)
#define SC2201_CMD_DPDM_EN (0x09)
#define SC2201_CMD_DPDM_DIS (0x08)
#define SC2201_CMD_WDT_MASK (BIT(7) | BIT(6) | BIT(5))
#define SC2201_FLAG_WDT_DISABLE (0)
#define SC2201_FLAG_WDT_5S (BIT(7))

/*reg03 General INT Flag*/
#define SC2201_ADDR_GENERAL_INT_FLAG (0x0003)
#define SC2201_FLAG_TEMP_SHUTDOWN BIT(0)
#define SC2201_FLAG_WATCHDOG_TIMEOUT BIT(1)

/*reg04 General INT MASK*/
#define SC2201_ADDR_GENERAL_INT_MASK (0x0004)
#define SC2201_ADDR_BC1P2_CTRL (0x0005)

/*reg04 DPDM INT MASK*/
#define SC2201_ADDR_DPDM_INT_FLAG (0x0006)
#define SC2201_FLAG_DM_OVP BIT(1)
#define SC2201_FLAG_DP_OVP BIT(2)

/*reg07*/
#define SC2201_ADDR_DPDM_INT_MASK (0x0007)

/*reg08*/
#define SC2201_ADDR_UFCS_CTRL0 (0x0008)
#define SC2201_SEND_SOURCE_HARDRESET BIT(0)
#define SC2201_SEND_CABLE_HARDRESET BIT(1)
#define SC2201_FLAG_BAUD_RATE_VALUE (BIT(4) | BIT(3))
#define SC2201_FLAG_BAUD_NUM_SHIFT (3)
#define SC2201_CMD_EN_CHIP (0x80)
#define SC2201_CMD_DIS_CHIP (0X00)
#define SC2201_CMD_EN_HANDSHAKE BIT(5)
#define SC2201_MASK_EN_HANDSHAKE BIT(5)
#define SC2201_CMD_SND_CMP BIT(2)
#define SC2201_MASK_SND_CMP BIT(2)

/*reg09*/
#define SC2201_ADDR_UFCS_CTRL1 (0x0009)
#define SC2201_CMD_CLR_TX_RX (0x32)
#define SC2201_CMD_CLR_TX (0x22)
#define SC2201_CMD_CLR_RX (0x12)

/*reg0A INT Flag0*/
#define SC2201_ADDR_UFCS_INT_FLAG0 (0x000A)
#define SC2201_FLAG_ACK_RECEIVE_TIMEOUT BIT(0)
#define SC2201_FLAG_MSG_TRANS_FAIL BIT(1)
#define SC2201_FLAG_RX_OVERFLOW BIT(3)
#define SC2201_FLAG_DATA_READY BIT(4)
#define SC2201_FLAG_SENT_PACKET_COMPLETE BIT(5)
#define SC2201_FLAG_HANDSHAKE_SUCCESS BIT(6)
#define SC2201_FLAG_HANDSHAKE_FAIL BIT(7)

/*reg0B INT Flag1*/
#define SC2201_ADDR_UFCS_INT_FLAG1 (0x000B)
#define SC2201_FLAG_HARD_RESET BIT(0)
#define SC2201_FLAG_CRC_ERROR BIT(1)
#define SC2201_FLAG_STOP_ERROR BIT(2)
#define SC2201_FLAG_START_FAIL BIT(3)
#define SC2201_FLAG_LENGTH_ERROR BIT(4)
#define SC2201_FLAG_DATA_BYTE_TIMEOUT BIT(5)
#define SC2201_FLAG_TRAINING_BYTE_ERROR BIT(6)
#define SC2201_FLAG_BAUD_RATE_ERROR BIT(7)

/*reg0c INT Flag1*/
#define SC2201_ADDR_UFCS_INT_FLAG2 (0x000C)
#define SC2201_FLAG_BAUD_RATE_CHANGE BIT(6)
#define SC2201_FLAG_BUS_CONFLICT BIT(7)

/*reg0D*/
#define SC2201_ADDR_UFCS_INT_MASK0 (0x000D)
#define SC2201_CMD_MASK_ACK_TIMEOUT (0x01)

/*reg0E*/
#define SC2201_ADDR_UFCS_INT_MASK1 (0x000E)
#define SC2201_FLAG_MASK_TRANING_BYTE_ERROR (0x40)

/*reg0F*/
#define SC2201_ADDR_UFCS_INT_MASK2 (0x000F)

/*reg tx_buffer*/
#define SC2201_ADDR_TX_LENGTH (0x0050)
#define SC2201_ADDR_TX_BUFFER0 (0x0051)
#define SC2201_ADDR_TX_BUFFER258 (0x00B1)

/*reg rx_buffer*/
#define SC2201_ADDR_RX_LENGTH_L (0x0160)
#define SC2201_ADDR_RX_LENGTH_H (0x0161)
#define SC2201_ADDR_RX_BUFFER0 (0x0162)
#define SC2201_ADDR_RX_BUFFER258 (0x0264)

/*reg dpvh*/
#define SC2201_ADDR_DP_VH (0x30F5)
#define SC2201_MASK_DP_VH (0x5A)

/****************Message Construction Helper*********/
struct oplus_sc2201 {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;

	int ufcs_en_gpio;
	int ufcs_int_gpio;
	int ufcs_int_irq;

	struct pinctrl *pinctrl;
	struct pinctrl_state *ufcs_int_default;
	struct pinctrl_state *ufcs_en_active;
	struct pinctrl_state *ufcs_en_sleep;

	atomic_t suspended;
};
#endif /*_OPLUS_SC2201_H_*/
