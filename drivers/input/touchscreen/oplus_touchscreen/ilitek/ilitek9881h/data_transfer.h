/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __DATA_TRANSFER_H
#define __DATA_TRANSFER_H

struct core_i2c_data {
	struct i2c_client *client;
	int clk;
	int seg_len;
};

extern struct core_i2c_data *core_i2c;

extern int core_i2c_write(uint8_t, uint8_t *, uint16_t);
extern int core_i2c_read(uint8_t, uint8_t *, uint16_t);

extern int core_i2c_segmental_read(uint8_t, uint8_t *, uint16_t);

extern int core_i2c_init(struct i2c_client *);
extern void core_i2c_remove(void);

#define SPI_WRITE 		0X82
#define SPI_READ 		0X83
struct core_spi_data {
	struct spi_device *spi;
};

extern struct core_spi_data *core_spi;
extern int core_spi_write(uint8_t *pBuf, uint16_t nSize);
extern int core_spi_read(uint8_t *pBuf, uint16_t nSize);
extern int core_spi_check_header(uint8_t *data, uint32_t size);
extern int core_spi_rx_check_test(void);
extern int core_spi_read_data_after_checksize(uint8_t *pBuf, uint16_t nSize);
extern int core_spi_check_read_size(void);

extern int core_spi_init(struct spi_device *spi);

#endif
