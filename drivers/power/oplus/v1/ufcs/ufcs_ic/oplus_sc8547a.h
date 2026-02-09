// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_SC8547A_H_
#define _OPLUS_SC8547A_H_
#include "../oplus_chg_track.h"

#define UFCS_IC_NAME "SC8547A"

/********************** I2C Slave Addr **************************/
#define SC8547A_I2C_ADDR (0x6F)
#define SC8547A_MAX_REG (0x0264)
#define SC8547A_FLAG_NUM (3)

/********************** SC8547A UFCS reg map **********************/
#define SC8547A_ENABLE_REG_NUM (6)

#define SC8547A_ADDR_DEVICE_ID (0x00)
#define SC8547A_ADDR_DEVICE_ID1 (0x01)

#define SC8547A_ADDR_DPDM_CTRL (0x21)
#define SC8547A_CMD_DPDM_EN (0x01)

#define SC8547A_ADDR_OTG_EN (0x3B)

#define SC8547A_ADDR_UFCS_CTRL0 (0x40)
#define SC8547A_SEND_SOURCE_HARDRESET BIT(0)
#define SC8547A_SEND_CABLE_HARDRESET BIT(1)
#define SC8547A_FLAG_BAUD_RATE_VALUE (BIT(4) | BIT(3))
#define SC8547A_FLAG_BAUD_NUM_SHIFT (3)
#define SC8547A_CMD_EN_CHIP (0x80)
#define SC8547A_CMD_DIS_CHIP (0X00)
#define SC8547A_MASK_EN_HANDSHAKE BIT(5)
#define SC8547A_CMD_EN_HANDSHAKE BIT(5)

#define SC8547A_ADDR_GENERAL_INT_FLAG1 (0x41)
#define SC8547A_CMD_MASK_ACK_DISCARD (0x80)

#define SC8547A_ADDR_GENERAL_INT_FLAG2 (0x42)
#define SC8547A_FLAG_ACK_RECEIVE_TIMEOUT BIT(0)
#define SC8547A_FLAG_HARD_RESET BIT(1)
#define SC8547A_FLAG_DATA_READY BIT(2)
#define SC8547A_FLAG_SENT_PACKET_COMPLETE BIT(3)
#define SC8547A_FLAG_CRC_ERROR BIT(4)
#define SC8547A_FLAG_BAUD_RATE_ERROR BIT(5)
#define SC8547A_FLAG_HANDSHAKE_SUCCESS BIT(6)
#define SC8547A_FLAG_HANDSHAKE_FAIL BIT(7)

#define SC8547A_ADDR_GENERAL_INT_FLAG3 (0x43)
#define SC8547A_FLAG_TRAINING_BYTE_ERROR BIT(0)
#define SC8547A_FLAG_MSG_TRANS_FAIL BIT(1)
#define SC8547A_FLAG_DATA_BYTE_TIMEOUT BIT(3)
#define SC8547A_FLAG_BAUD_RATE_CHANGE BIT(4)
#define SC8547A_FLAG_LENGTH_ERROR BIT(5)
#define SC8547A_FLAG_RX_OVERFLOW BIT(6)
#define SC8547A_FLAG_BUS_CONFLICT BIT(7)

#define SC8547A_ADDR_UFCS_INT_MASK0 (0x44)
#define SC8547A_CMD_MASK_ACK_TIMEOUT (0x01)

#define SC8547A_ADDR_UFCS_INT_MASK1 (0x45)
#define SC8547A_MASK_TRANING_BYTE_ERROR (0x01)

/*tx_buffer*/
#define SC8547A_ADDR_TX_LENGTH (0x47)

#define SC8547A_ADDR_TX_BUFFER0 (0x48)

#define SC8547A_ADDR_TX_BUFFER35 (0x6B)

/*rx_buffer*/
#define SC8547A_ADDR_RX_LENGTH (0x6C)
#define SC8547A_ADDR_RX_BUFFER0 (0x6D)
#define SC8547A_ADDR_RX_BUFFER124 (0xE9)
#define SC8547A_LEN_MAX 64

#define SC8547A_ADDR_TXRX_BUFFER_CTRL (0xEA)
#define SC8547A_CMD_CLR_TX_RX (0xC0)

#define SC8547A_DEVICE_ID 0x67

/*********************command buffer**************/
/*avoid to rewrite the baudrate flags*/
#define SC8547A_CMD_SND_CMP BIT(2)
#define SC8547A_MASK_SND_CMP BIT(2)

/****************Message Construction Helper*********/
struct oplus_sc8547a_ufcs {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	atomic_t suspended;
	bool ufcs_enable;
};
#endif /*_OPLUS_SC8547A_H_*/
