// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek_common.h"
#include "protocol.h"
#include "data_transfer.h"
#include "mp_test.h"
#include "finger_report.h"

struct core_i2c_data *core_i2c;

#ifdef I2C_DMA
static unsigned char *ilitek_dma_va = NULL;
static dma_addr_t ilitek_dma_pa = 0;

#define DMA_VA_BUFFER   4096

static int dma_alloc(struct core_i2c_data *i2c)
{
	if (i2c->client != NULL) {
		i2c->client->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		ilitek_dma_va = (u8 *) dma_alloc_coherent(&i2c->client->dev, DMA_VA_BUFFER, &ilitek_dma_pa, GFP_KERNEL);
		if (ERR_ALLOC_MEM(ilitek_dma_va)) {
			TPD_INFO("Allocate DMA I2C Buffer failed\n");
			return -ENOMEM;
		}

		memset(ilitek_dma_va, 0, DMA_VA_BUFFER);
		i2c->client->ext_flag |= I2C_DMA_FLAG;
		return 0;
	}

	TPD_INFO("i2c->client is NULL, return fail\n");
	return -ENODEV;
}

static void dma_free(void)
{
	if (ilitek_dma_va != NULL) {
		dma_free_coherent(&core_i2c->client->dev, DMA_VA_BUFFER, ilitek_dma_va, ilitek_dma_pa);

		ilitek_dma_va = NULL;
		ilitek_dma_pa = 0;

		TPD_INFO("Succeed to free DMA buffer\n");
	}
}
#endif /* I2C_DMA */

int core_i2c_write(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	uint8_t check_sum = 0;
	uint8_t *txbuf = NULL;

	struct i2c_msg msgs[] = {
		{
		 .addr = nSlaveId,
		 .flags = 0,	/* write flag. */
		 .len = nSize,
		 .buf = pBuf,
		 },
	};

#ifdef I2C_DMA
	TPD_DEBUG("DMA: size = %d\n", nSize);
	if (nSize > 8) {
		msgs[0].addr = (core_i2c->client->addr & I2C_MASK_FLAG);
		msgs[0].ext_flag = (core_i2c->client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG);
		memcpy(ilitek_dma_va, pBuf, nSize);
		msgs[0].buf = (uint8_t *) ilitek_dma_pa;
	}
#endif /* I2C_DMA */

	/*
	 * NOTE: If TP driver is doing MP test and commanding 0xF1 to FW, we add a checksum
	 * to the last index and plus 1 with size.
	 */

	if (protocol->major >= 5 && protocol->mid >= 4) {
		if (!core_config->icemodeenable && pBuf[0] == 0xF1 && core_mp->run) {
			check_sum = cal_fr_checksum(pBuf, nSize);
			txbuf = (uint8_t*)kcalloc(nSize + 1, sizeof(uint8_t), GFP_KERNEL);
			if (ERR_ALLOC_MEM(txbuf)) {
				TPD_INFO("Failed to allocate CSV mem\n");
				res = -ENOMEM;
				goto out;
			}
			memcpy(txbuf, pBuf, nSize);
			txbuf[nSize] = check_sum;
			msgs[0].buf = txbuf;
			msgs[0].len = nSize + 1;
		}
	}

	if (i2c_transfer(core_i2c->client->adapter, msgs, 1) < 0) {
		if (core_config->do_ic_reset) {
			/* ignore i2c error if doing ic reset */
			res = 0;
		} else {
			res = -EIO;
			TPD_INFO("I2C Write Error, res = %d\n", res);
			goto out;
		}
	}

out:
	ipio_kfree((void **)&txbuf);
	return res;
}
EXPORT_SYMBOL(core_i2c_write);

int core_i2c_read(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = nSlaveId,
		 .flags = I2C_M_RD,	/* read flag */
		 .len = nSize,
		 .buf = pBuf,
		 },
	};

#ifdef I2C_DMA
	TPD_DEBUG("DMA: size = %d\n", nSize);
	if (nSize > 8) {
		msgs[0].addr = (core_i2c->client->addr & I2C_MASK_FLAG);
		msgs[0].ext_flag = (core_i2c->client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG);
		msgs[0].buf = (uint8_t *) ilitek_dma_pa;
	} else {
		msgs[0].buf = pBuf;
	}
#endif /* I2C_DMA */

	if (i2c_transfer(core_i2c->client->adapter, msgs, 1) < 0) {
		res = -EIO;
		TPD_INFO("I2C Read Error, res = %d\n", res);
		goto out;
	}
#ifdef I2C_DMA
	if (nSize > 8) {
		memcpy(pBuf, ilitek_dma_va, nSize);
	}
#endif /* I2C_DMA */

out:
	return res;
}
EXPORT_SYMBOL(core_i2c_read);

int core_i2c_segmental_read(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	int offset = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = nSlaveId,
		 .flags = I2C_M_RD,
		 .len = nSize,
/* .buf = pBuf, */
		 },
	};

	while (nSize > 0) {
		msgs[0].buf = &pBuf[offset];

		if (nSize > core_i2c->seg_len) {
			msgs[0].len = core_i2c->seg_len;
			nSize -= core_i2c->seg_len;
			offset += msgs[0].len;
		} else {
			msgs[0].len = nSize;
			nSize = 0;
		}

		TPD_DEBUG("Length = %d\n", msgs[0].len);

		if (i2c_transfer(core_i2c->client->adapter, msgs, 1) < 0) {
			res = -EIO;
			TPD_INFO("I2C Read Error, res = %d\n", res);
			goto out;
		}
	}

out:
	return res;
}
EXPORT_SYMBOL(core_i2c_segmental_read);

int core_i2c_init(struct i2c_client *client)
{
	core_i2c = kmalloc(sizeof(*core_i2c), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_i2c)) {
		TPD_INFO("Failed to alllocate core_i2c mem %ld\n", PTR_ERR(core_i2c));
		core_i2c_remove();
		return -ENOMEM;
	}

	core_i2c->client = client;
	core_i2c->seg_len = 256;	/* length of segment */

#ifdef I2C_DMA
	if (dma_alloc(core_i2c->client) < 0) {
		TPD_INFO("Failed to alllocate DMA mem %ld\n", PTR_ERR(core_i2c));
		return -ENOMEM;
	}
#endif /* I2C_DMA */

	core_i2c->clk = 400000;
	return 0;
}
EXPORT_SYMBOL(core_i2c_init);

void core_i2c_remove(void)
{
	TPD_INFO("Remove core-i2c members\n");

#ifdef I2C_DMA
	dma_free();
#endif /* I2C_DMA */

	ipio_kfree((void **)&core_i2c);
}
EXPORT_SYMBOL(core_i2c_remove);

struct core_spi_data *core_spi;
void core_spi_remove(void);
int ilitek_core_spi_write_then_read(struct spi_device *spi, uint8_t * txbuf,
	int w_len, uint8_t * rxbuf, int r_len)
{
	int res = 0;
	int retry = 2;
	while(retry--) {
		if (spi_write_then_read(spi, txbuf, w_len, rxbuf, r_len) < 0) {
			TPD_INFO("spi Write Error, retry = %d\n", retry);
			msleep(20);
		}
		else {
			res = 0;
			break;
		}
	}
	if (retry < 0) {
		res = -1;
	}
	return res;
}

int core_Rx_check(uint16_t check)
{
	int size = 0;
	int i = 0;
	int count = 100;
	uint8_t txbuf[5] = { 0 };
	uint8_t rxbuf[4] = {0};
	uint16_t status = 0;
	for(i = 0; i < count; i++)
	{
		txbuf[0] = SPI_WRITE;
		txbuf[1] = 0x25;
		txbuf[2] = 0x94;
		txbuf[3] = 0x0;
		txbuf[4] = 0x2;
		if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
			size = -EIO;
			TPD_INFO("spi Write Error, res = %d\n", size);
            return size;
		}
		txbuf[0] = SPI_READ;
		if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 4) < 0) {
			size = -EIO;
			TPD_INFO("spi Write Error, res = %d\n", size);
            return size;
		}
		status = (rxbuf[2] << 8) + rxbuf[3];
		size = (rxbuf[0] << 8) + rxbuf[1];
		//TPD_INFO("count:%d,status =0x%x size: = %d\n", i, status, size);
		if(status == check)
			return size;
		mdelay(1);
	}
	if (ipio_debug_level & DEBUG_CONFIG) {
		core_config_get_reg_data(0x44008);
		core_config_get_reg_data(0x40054);
		core_config_get_reg_data(0x5101C);
		core_config_get_reg_data(0x51020);
		core_config_get_reg_data(0x4004C);
	}
	size = -EIO;
	return size;
}
int core_Tx_unlock_check(void)
{
	int res = 0;
	int i = 0;
	int count = 100;
	uint8_t txbuf[5] = { 0 };
	uint8_t rxbuf[4] = {0};
	uint16_t unlock = 0;
	for(i = 0; i < count; i++)
	{
		txbuf[0] = SPI_WRITE;
		txbuf[1] = 0x25;
		txbuf[2] = 0x0;
		txbuf[3] = 0x0;
		txbuf[4] = 0x2;
		if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
			res = -EIO;
			TPD_INFO("spi Write Error, res = %d\n", res);
            return res;
		}
		txbuf[0] = SPI_READ;
		if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 4) < 0) {
			res = -EIO;
			TPD_INFO("spi Write Error, res = %d\n", res);
            return res;
		}
		unlock = (rxbuf[2] << 8) + rxbuf[3];
		//TPD_INFO("count:%d,unlock =0x%x\n", i, unlock);
		if(unlock == 0x9881)
			return res;
		mdelay(1);
	}
	return res;
}
int core_ice_mode_read_9881H11(uint8_t *data, uint32_t size)
{
	int res = 0;
	uint8_t txbuf[64] = { 0 };
	//set read address
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x98;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		return res;
	}
	//read data
	txbuf[0] = SPI_READ;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, data, size) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		return res;
	}
	//write data lock
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x94;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	txbuf[5] = (size & 0xFF00) >> 8;
	txbuf[6] = size & 0xFF;
	txbuf[7] = (char)0x98;
	txbuf[8] = (char)0x81;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 9, txbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
	}
	return res;
}

int core_ice_mode_write_9881H11(uint8_t *data, uint32_t size)
{
	int res = 0;
	uint8_t check_sum = 0;
	uint8_t wsize = 0;
	uint8_t *txbuf;
    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*size+9, GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		TPD_INFO("Failed to allocate mem\n");
		res = -ENOMEM;
		goto out;
	}
	//write data
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x4;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	check_sum = cal_fr_checksum(data, size);
	memcpy(txbuf + 5, data, size);
	txbuf[5 + size] = check_sum;
	//size + checksum
	size++;
	wsize = size;
	if(wsize%4 != 0)
	{
		wsize += 4 - (wsize % 4); 
	}
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, wsize + 5, txbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		goto out;
	}
	//write data lock
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x0;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	txbuf[5] = (size & 0xFF00) >> 8;
	txbuf[6] = size & 0xFF;
	txbuf[7] = (char)0x5A;
	txbuf[8] = (char)0xA5;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 9, txbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
	}
out:
	kfree(txbuf);
	return res;
}

int core_ice_mode_disable_9881H11(void)
{
	int res = 0;
	uint8_t txbuf[5] = {0};
	txbuf[0] = 0x82;
	txbuf[1] = 0x1B;
	txbuf[2] = 0x62;
	txbuf[3] = 0x10;
	txbuf[4] = 0x18;
	//TPD_INFO("FW ICE Mode disable\n");
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
	}
	return res;
}

int core_ice_mode_enable_9881H11(void)
{
	int res = 0;
	uint8_t txbuf[5] = {0};
	uint8_t rxbuf[2]= {0};
	txbuf[0] = 0x82;
	txbuf[1] = 0x1F;
	txbuf[2] = 0x62;
	txbuf[3] = 0x10;
	txbuf[4] = 0x18;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 1) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		return res;
	}
	//check recover data
	if(rxbuf[0] == 0x82)
	{
		TPD_INFO("recover data rxbuf:0x%x\n", rxbuf[0]);
		return CHECK_RECOVER;
	}
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 5, rxbuf, 0) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
	}
	return res;
}

int core_spi_check_read_size(void)
{
	int res = 0;
	int size = 0;
	res = core_ice_mode_enable_9881H11();
	if (res < 0) {
		goto out;
	}
	size = core_Rx_check(0x5AA5); 
	if (size < 0) {
		res = -EIO;
		TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
		goto out;
	}
	return size;
out:
	return res;
}

int core_spi_read_data_after_checksize(uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	if (core_ice_mode_read_9881H11(pBuf, nSize) < 0) {
		res = -EIO;
		TPD_INFO("spi read Error, res = %d\n", res);
		goto out;
	}
	if (core_ice_mode_disable_9881H11() < 0) {
		res = -EIO;
		TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
		goto out;
	}
out:
	return res;
}

int core_spi_read_9881H11(uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	int size = 0;
	res = core_ice_mode_enable_9881H11();
	if (res < 0) {
		goto out;
	}
	size = core_Rx_check(0x5AA5); 
	if (size < 0) {
		res = -EIO;
		TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
		goto out;
	}
	if (size > nSize) {
		TPD_INFO("check read size > nSize  size = %d, nSize = %d\n", size, nSize);
        size = nSize;
	}
	if (core_ice_mode_read_9881H11(pBuf, size) < 0) {
		res = -EIO;
		TPD_INFO("spi read Error, res = %d\n", res);
		goto out;
	}
	if (core_ice_mode_disable_9881H11() < 0) {
		res = -EIO;
		TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
		goto out;
	}
	out:
	return res;
}

int core_spi_rx_check_test(void)
{
	int res = 0;
	int size = 0;
	res = core_ice_mode_enable_9881H11();
	if (res < 0) {
		TPD_INFO("ice mode enable error\n");
	}
	size = core_Rx_check(0x5AA5); 
	if (size < 0) {
		res = -EIO;
		TPD_INFO("spi core_Rx_check(0x5AA5) Error, res = %d\n", res);
	}
	if (core_ice_mode_disable_9881H11() < 0) {
		res = -EIO;
		TPD_INFO("spi core_ice_mode_disable_9881H11 Error, res = %d\n", res);
	}
	return res;
}

int core_spi_write_9881H11(uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	uint8_t *txbuf;
    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*nSize+5, GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		TPD_INFO("Failed to allocate mem\n");
		res = -ENOMEM;
		goto out;
	}
	res = core_ice_mode_enable_9881H11();
	if (res < 0) {
		goto out;
	}
	if (core_ice_mode_write_9881H11(pBuf, nSize) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		goto out;
	}
	if(core_Tx_unlock_check() < 0)
	{
		res = -ETXTBSY;
		TPD_INFO("check TX unlock Fail, res = %d\n", res);		
	}
	out:
	kfree(txbuf);
	return res;
}
int core_spi_write(uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	uint8_t *txbuf;
    txbuf = (uint8_t*)kmalloc(sizeof(uint8_t)*nSize+1, GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		TPD_INFO("Failed to allocate mem\n");
		res = -ENOMEM;
		goto out;
	}
	if(core_config->icemodeenable == false)
	{
		res = core_spi_write_9881H11(pBuf, nSize);
		core_ice_mode_disable_9881H11();
		kfree(txbuf);
		return res;
	}
  
	txbuf[0] = SPI_WRITE;
    memcpy(txbuf+1, pBuf, nSize);
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, nSize+1, txbuf, 0) < 0) {
		if (core_config->do_ic_reset) {
			/* ignore spi error if doing ic reset */
			res = 0;
		} else {
			res = -EIO;
			TPD_INFO("spi Write Error, res = %d\n", res);
			goto out;
		}
	}

out:
	kfree(txbuf);
	return res;
}
EXPORT_SYMBOL(core_spi_write);

int core_spi_read(uint8_t *pBuf, uint16_t nSize)
{
	int res = 0;
	uint8_t txbuf[1];
	txbuf[0] = SPI_READ;
	if(core_config->icemodeenable == false)
	{
		return core_spi_read_9881H11(pBuf, nSize);
	}
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, pBuf, nSize) < 0) {
		if (core_config->do_ic_reset) {
			/* ignore spi error if doing ic reset */
			res = 0;
		} else {
			res = -EIO;
			TPD_INFO("spi Read Error, res = %d\n", res);
			goto out;
		}
	}
out:
	return res;
}
EXPORT_SYMBOL(core_spi_read);

int core_spi_check_header(uint8_t *data, uint32_t size)
{
	int res = 0;
	uint8_t txbuf[5] = {0};
	uint8_t rxbuf[2]= {0};
	txbuf[0] = 0x82;
	if (ilitek_core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 1) < 0) {
		res = -EIO;
		TPD_INFO("spi Write Error, res = %d\n", res);
		return res;
	}
	data[0] = rxbuf[0];
	TPD_DEBUG("spi header data rxbuf:0x%x\n", rxbuf[0]);
	//check recover data
	if(rxbuf[0] == 0x82)
	{
		TPD_INFO("recover data rxbuf:0x%x\n", rxbuf[0]);
		return CHECK_RECOVER;
	}
	return 0;
}
EXPORT_SYMBOL(core_spi_check_header);

int core_spi_init(struct spi_device *spi)
{
	int ret = 0;

	core_spi = kmalloc(sizeof(*core_spi), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_spi)) {
		TPD_INFO("Failed to alllocate core_i2c mem %ld\n", PTR_ERR(core_spi));
		core_spi_remove();
		return -ENOMEM;
	}

	core_spi->spi = spi;
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	TPD_INFO("\n");
	ret = spi_setup(spi);
	if (ret < 0){
		TPD_INFO("ERR: fail to setup spi\n");
		return -ENODEV;
	}
	TPD_INFO("%s:name=%s,bus_num=%d,cs=%d,mode=%d,speed=%d\n",__func__,spi->modalias,
	 spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);	
	return 0;
}
EXPORT_SYMBOL(core_spi_init);

void core_spi_remove(void)
{
	TPD_INFO("Remove core-spi members\n");
	ipio_kfree((void **)&core_spi);
}
EXPORT_SYMBOL(core_spi_remove);
