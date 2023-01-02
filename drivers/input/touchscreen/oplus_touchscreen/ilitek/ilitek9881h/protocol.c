// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek_ili9881h.h"
#include "data_transfer.h"
#include "protocol.h"
#include "firmware.h"
#include "finger_report.h"

#define FUNC_NUM    20

struct DataItem {
	int key;
	char *name;
	int len;
	uint8_t *cmd;
};

struct protocol_sup_list {
	uint8_t major;
	uint8_t mid;
	uint8_t minor;
};

#define K (1024)
#define M (K * K)

struct DataItem *hashArray[FUNC_NUM];
struct protocol_cmd_list *protocol = NULL;
struct core_gesture_data *core_gesture;

int core_write(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
#if (INTERFACE == I2C_INTERFACE)
	return core_i2c_write(nSlaveId, pBuf, nSize);
#elif (INTERFACE == SPI_INTERFACE)
	return core_spi_write(pBuf, nSize);
#endif
}
EXPORT_SYMBOL(core_write);
int core_read(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
#if (INTERFACE == I2C_INTERFACE)
	return core_i2c_read(nSlaveId, pBuf, nSize);
#elif (INTERFACE == SPI_INTERFACE)
	return core_spi_read(pBuf, nSize);
#endif
}
EXPORT_SYMBOL(core_read);

static int hashCode(int key)
{
	return key % FUNC_NUM;
}

static struct DataItem *search_func(int key)
{
	int hashIndex = hashCode(key);

	while (hashArray[hashIndex] != NULL) {
		if (hashArray[hashIndex]->key == key)
			return hashArray[hashIndex];

		hashIndex++;
		hashIndex %= FUNC_NUM;
	}

	return NULL;
}

static void insert_func(int key, int len, uint8_t *cmd, char *name)
{
	int i = 0;
	int hashIndex = 0;
	struct DataItem *tmp = NULL;

	tmp = kmalloc(sizeof(struct DataItem), GFP_KERNEL);
	if(ERR_ALLOC_MEM(tmp)) {
		TPD_INFO("Failed to allocate memory\n");
		return;
	}

	tmp->key = key;
	tmp->name = name;
	tmp->len = len;

	tmp->cmd = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	for (i = 0; i < len; i++)
		tmp->cmd[i] = cmd[i];

	hashIndex = hashCode(key);

	while (hashArray[hashIndex] != NULL) {
		hashIndex++;
		hashIndex %= FUNC_NUM;
	}

	hashArray[hashIndex] = tmp;
}

static void free_func_hash(void)
{
	int i = 0;

	for (i = 0; i < FUNC_NUM; i++)
		ipio_kfree((void **)&hashArray[i]);
}

static void create_func_hash(void)
{
	/* if protocol is updated, we free its allocated mem at all before create new ones. */
	free_func_hash();

	insert_func(0, protocol->func_ctrl_len, protocol->sense_ctrl, "sense_ctrl");
	insert_func(1, protocol->func_ctrl_len, protocol->sleep_ctrl, "sleep_ctrl");
	insert_func(2, protocol->func_ctrl_len, protocol->glove_ctrl, "glove_ctrl");
	insert_func(3, protocol->func_ctrl_len, protocol->stylus_ctrl, "stylus_ctrl");
	insert_func(4, protocol->func_ctrl_len, protocol->tp_scan_mode, "tp_scan_mode");
	insert_func(5, protocol->func_ctrl_len, protocol->lpwg_ctrl, "lpwg_ctrl");
	insert_func(6, protocol->func_ctrl_len, protocol->gesture_ctrl, "gesture_ctrl");
	insert_func(7, protocol->func_ctrl_len, protocol->phone_cover_ctrl, "phone_cover_ctrl");
	insert_func(8, protocol->func_ctrl_len, protocol->finger_sense_ctrl, "finger_sense_ctrl");
	insert_func(9, protocol->window_len, protocol->phone_cover_window, "phone_cover_window");
	insert_func(10, protocol->func_ctrl_len, protocol->finger_sense_ctrl, "finger_sense_ctrl");
	insert_func(11, protocol->func_ctrl_len, protocol->proximity_ctrl, "proximity_ctrl");
	insert_func(12, protocol->func_ctrl_len, protocol->plug_ctrl, "plug_ctrl");
	insert_func(13, protocol->func_ctrl_len, protocol->edge_limit_ctrl, "edge_limit_ctrl");
	insert_func(14, protocol->func_ctrl_len, protocol->lock_point_ctrl, "lock_point_ctrl");
}

static void config_protocol_v5_cmd(void)
{
	if (protocol->mid == 0x0) {
		protocol->func_ctrl_len = 2;

		protocol->sense_ctrl[0] = 0x1;
		protocol->sense_ctrl[1] = 0x0;

		protocol->sleep_ctrl[0] = 0x2;
		protocol->sleep_ctrl[1] = 0x0;

		protocol->glove_ctrl[0] = 0x6;
		protocol->glove_ctrl[1] = 0x0;

		protocol->stylus_ctrl[0] = 0x7;
		protocol->stylus_ctrl[1] = 0x0;

		protocol->tp_scan_mode[0] = 0x8;
		protocol->tp_scan_mode[1] = 0x0;

		protocol->lpwg_ctrl[0] = 0xA;
		protocol->lpwg_ctrl[1] = 0x0;

		protocol->gesture_ctrl[0] = 0xB;
		protocol->gesture_ctrl[1] = 0x3F;

		protocol->phone_cover_ctrl[0] = 0xC;
		protocol->phone_cover_ctrl[1] = 0x0;

		protocol->finger_sense_ctrl[0] = 0xF;
		protocol->finger_sense_ctrl[1] = 0x0;

		protocol->phone_cover_window[0] = 0xD;

		/* Non support on v5.0 */
		/* protocol->proximity_ctrl[0] = 0x10; */
		/* protocol->proximity_ctrl[1] = 0x0; */

		/* protocol->plug_ctrl[0] = 0x11; */
		/* protocol->plug_ctrl[1] = 0x0; */
	} else {
		protocol->func_ctrl_len = 3;

		protocol->sense_ctrl[0] = 0x1;
		protocol->sense_ctrl[1] = 0x1;
		protocol->sense_ctrl[2] = 0x0;

		protocol->sleep_ctrl[0] = 0x1;
		protocol->sleep_ctrl[1] = 0x2;
		protocol->sleep_ctrl[2] = 0x0;

		protocol->glove_ctrl[0] = 0x1;
		protocol->glove_ctrl[1] = 0x6;
		protocol->glove_ctrl[2] = 0x0;

		protocol->stylus_ctrl[0] = 0x1;
		protocol->stylus_ctrl[1] = 0x7;
		protocol->stylus_ctrl[2] = 0x0;

		protocol->tp_scan_mode[0] = 0x1;
		protocol->tp_scan_mode[1] = 0x8;
		protocol->tp_scan_mode[2] = 0x0;

		protocol->lpwg_ctrl[0] = 0x1;
		protocol->lpwg_ctrl[1] = 0xA;
		protocol->lpwg_ctrl[2] = 0x0;

		protocol->gesture_ctrl[0] = 0x1;
		protocol->gesture_ctrl[1] = 0xB;
		protocol->gesture_ctrl[2] = 0x3F;

		protocol->phone_cover_ctrl[0] = 0x1;
		protocol->phone_cover_ctrl[1] = 0xC;
		protocol->phone_cover_ctrl[2] = 0x0;

		protocol->finger_sense_ctrl[0] = 0x1;
		protocol->finger_sense_ctrl[1] = 0xF;
		protocol->finger_sense_ctrl[2] = 0x0;

		protocol->proximity_ctrl[0] = 0x1;
		protocol->proximity_ctrl[1] = 0x10;
		protocol->proximity_ctrl[2] = 0x0;

		protocol->plug_ctrl[0] = 0x1;
		protocol->plug_ctrl[1] = 0x11;
		protocol->plug_ctrl[2] = 0x0;

		protocol->edge_limit_ctrl[0] = 0x1;
		protocol->edge_limit_ctrl[1] = 0x12;
		protocol->edge_limit_ctrl[2] = 0x0;

		protocol->lock_point_ctrl[0] = 0x1;
		protocol->lock_point_ctrl[1] = 0x13;
		protocol->lock_point_ctrl[2] = 0x0;

		protocol->phone_cover_window[0] = 0xE;
	}
	if (protocol->mid >= 0x3)
		protocol->fw_ver_len = 10;
	else
		protocol->fw_ver_len = 4;

	if (protocol->mid == 0x1) {
		protocol->pro_ver_len = 3;
	} else {
		protocol->pro_ver_len = 5;
	}

	protocol->tp_info_len = 15;
	protocol->key_info_len = 30;
	protocol->core_ver_len = 5;
	protocol->window_len = 8;

	/* The commadns about panel information */
	protocol->cmd_read_ctrl = P5_0_READ_DATA_CTRL;
	protocol->cmd_get_tp_info = P5_0_GET_TP_INFORMATION;
	protocol->cmd_get_key_info = P5_0_GET_KEY_INFORMATION;
	protocol->cmd_get_fw_ver = P5_0_GET_FIRMWARE_VERSION;
	protocol->cmd_get_pro_ver = P5_0_GET_PROTOCOL_VERSION;
	protocol->cmd_get_core_ver = P5_0_GET_CORE_VERSION;
	protocol->cmd_mode_ctrl = P5_0_MODE_CONTROL;
	protocol->cmd_cdc_busy = P5_0_CDC_BUSY_STATE;
	protocol->cmd_i2cuart = P5_0_I2C_UART;
	protocol->gesture_mode = P5_0_FIRMWARE_GESTURE_MODE;

	/* The commands about the packets of finger report from FW */
	protocol->unknown_mode = P5_0_FIRMWARE_UNKNOWN_MODE;
	protocol->demo_mode = P5_0_FIRMWARE_DEMO_MODE;
	protocol->debug_mode = P5_0_FIRMWARE_DEBUG_MODE;
	protocol->test_mode = P5_0_FIRMWARE_TEST_MODE;
	protocol->i2cuart_mode = P5_0_FIRMWARE_I2CUART_MODE;

	protocol->demo_pid = P5_0_DEMO_PACKET_ID;
	protocol->debug_pid = P5_0_DEBUG_PACKET_ID;
	protocol->test_pid = P5_0_TEST_PACKET_ID;
	protocol->i2cuart_pid = P5_0_I2CUART_PACKET_ID;
	protocol->ges_pid = P5_0_GESTURE_PACKET_ID;

	protocol->demo_len = P5_0_DEMO_MODE_PACKET_LENGTH;
	protocol->debug_len = P5_0_DEBUG_MODE_PACKET_LENGTH;
	protocol->test_len = P5_0_TEST_MODE_PACKET_LENGTH;

	/* The commands about MP test */
	protocol->cmd_cdc = P5_0_SET_CDC_INIT;
	protocol->cmd_get_cdc = P5_0_GET_CDC_DATA;
	if (protocol->mid < 4) {
		protocol->cdc_len = 3;
	} else {
		protocol->cdc_len = 15;
	}

	protocol->mutual_dac = 0x1;
	protocol->mutual_bg = 0x2;
	protocol->mutual_signal = 0x3;
	protocol->mutual_no_bk = 0x5;
	protocol->mutual_has_bk = 0x8;
	protocol->mutual_bk_dac = 0x10;

	protocol->self_dac = 0xC;
	protocol->self_bg = 0xF;
	protocol->self_signal = 0xD;
	protocol->self_no_bk = 0xE;
	protocol->self_has_bk = 0xB;
	protocol->self_bk_dac = 0x11;

	protocol->key_dac = 0x14;
	protocol->key_bg = 0x16;
	protocol->key_no_bk = 0x7;
	protocol->key_has_bk = 0x15;
	protocol->key_open = 0x12;
	protocol->key_short = 0x13;

	protocol->st_dac = 0x1A;
	protocol->st_bg = 0x1C;
	protocol->st_no_bk = 0x17;
	protocol->st_has_bk = 0x1B;
	protocol->st_open = 0x18;

	protocol->tx_short = 0x19;

	protocol->rx_short = 0x4;
	protocol->rx_open = 0x6;

	protocol->tx_rx_delta = 0x1E;

	protocol->cm_data = 0x9;
	protocol->cs_data = 0xA;

	protocol->trcrq_pin = 0x20;
	protocol->resx2_pin = 0x21;
	protocol->mutual_integra_time = 0x22;
	protocol->self_integra_time = 0x23;
	protocol->key_integra_time = 0x24;
	protocol->st_integra_time = 0x25;
	protocol->peak_to_peak = 0x1D;
	protocol->get_timing = 0x30;
	protocol->doze_p2p = 0x32;
	protocol->doze_raw = 0x33;
}

void core_protocol_func_control(int key, int ctrl)
{
	struct DataItem *tmp = search_func(key);

	if (tmp != NULL) {
		/* last element is used to control this func */
		if (tmp->key != 9) {
			if (tmp->key == 1 && ctrl == 0) {
				if (core_config->ili_sleep_type == SLEEP_IN_GESTURE_PS) {
					TPD_INFO("Gesture mode TP enter sleep in\n");
					tmp->cmd[tmp->len - 1] = 0x00;
				} else if (core_config->ili_sleep_type == SLEEP_IN_DEEP) {
					TPD_INFO("TP enter deep sleep in\n");
					tmp->cmd[tmp->len - 1] = 0x03;
				} else if (core_config->ili_sleep_type == SLEEP_IN_BEGIN_FTM) {
					TPD_INFO("TP enter sleep in at begin of ftm, but not close lcd\n");
					tmp->cmd[tmp->len - 1] = 0x04;
				} else if (core_config->ili_sleep_type == SLEEP_IN_END_FTM) {
					TPD_INFO("TP enter sleep in at end of ftm, close lcd to reduce 1ma\n");
					tmp->cmd[tmp->len - 1] = 0x05;
				} else {
					tmp->cmd[tmp->len - 1] = ctrl;
				}
			} else {
				tmp->cmd[tmp->len - 1] = ctrl;
			}
		}
		TPD_INFO("Found func's name: %s, key = %d cmd: 0x%02X 0x%02X 0x%02X\n",\
			tmp->name, key, tmp->cmd[0], tmp->cmd[1], tmp->cmd[2]);
		core_write(core_config->slave_i2c_addr, tmp->cmd, tmp->len);
		return;
	}

	TPD_INFO("Can't find any main functions\n");
}
EXPORT_SYMBOL(core_protocol_func_control);

int core_protocol_update_ver(uint8_t major, uint8_t mid, uint8_t minor)
{
	int i = 0;

	struct protocol_sup_list pver[] = {
		{0x5, 0x0, 0x0},
		{0x5, 0x1, 0x0},
		{0x5, 0x2, 0x0}, 
		{0x5, 0x3, 0x0},
		{0x5, 0x4, 0x0},
	};

	for (i = 0; i < ARRAY_SIZE(pver); i++) {
		if (pver[i].major == major && pver[i].mid == mid/* && pver[i].minor == minor*/) {
			protocol->major = major;
			protocol->mid = mid;
			protocol->minor = minor;
			TPD_INFO("protocol: major = %d, mid = %d, minor = %d\n",
				 protocol->major, protocol->mid, protocol->minor);

			if (protocol->major == 0x5)
				config_protocol_v5_cmd();

			/* We need to recreate function controls because of the commands updated. */
			create_func_hash();
			return 0;
		}
	}

	TPD_INFO("Doesn't support this version of protocol\n");
	return -1;
}
EXPORT_SYMBOL(core_protocol_update_ver);

void core_gesture_remove(void)
{
	TPD_INFO("Remove core-gesture members\n");
	ipio_kfree((void **)&core_gesture);
}
EXPORT_SYMBOL(core_gesture_remove);

int core_protocol_init(void)
{
	if (protocol == NULL) {
		protocol = kzalloc(sizeof(*protocol), GFP_KERNEL);
		if (ERR_ALLOC_MEM(protocol)) {
			TPD_INFO("Failed to allocate protocol mem, %ld\n", PTR_ERR(protocol));
			core_protocol_remove();
			return -ENOMEM;
		}
	}

	/* The default version must only once be set up at this initial time. */
	core_protocol_update_ver(PROTOCOL_MAJOR, PROTOCOL_MID, PROTOCOL_MINOR);

	core_gesture = kmalloc(sizeof(*core_gesture), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_gesture)) {
		TPD_INFO("Failed to alllocate core_i2c mem %ld\n", PTR_ERR(core_gesture));
		core_gesture_remove();
	}
	core_gesture->entry = false;
	core_gesture->mode = GESTURE_INFO_MPDE;
	return 0;
}
EXPORT_SYMBOL(core_protocol_init);

void core_protocol_remove(void)
{
	TPD_INFO("Remove core-protocol memebers\n");
	ipio_kfree((void **)&protocol);
	free_func_hash();
	core_gesture_remove();
}
EXPORT_SYMBOL(core_protocol_remove);

/*
 * The table contains fundamental data used to program our flash, which
 * would be different according to the vendors.
 */
struct flash_table ft[] = {
	{0xEF, 0x6011, (128 * K), 256, (4 * K), (64 * K)},	/*  W25Q10EW  */
	{0xEF, 0x6012, (256 * K), 256, (4 * K), (64 * K)},	/*  W25Q20EW  */
	{0xC8, 0x6012, (256 * K), 256, (4 * K), (64 * K)},	/*  GD25LQ20B */
	{0xC8, 0x6013, (512 * K), 256, (4 * K), (64 * K)},	/*  GD25LQ40 */
	{0x85, 0x6013, (4 * M), 256, (4 * K), (64 * K)},
	{0xC2, 0x2812, (256 * K), 256, (4 * K), (64 * K)},
	{0x1C, 0x3812, (256 * K), 256, (4 * K), (64 * K)},
};

struct flash_table *flashtab = NULL;

int core_flash_poll_busy(void)
{
	int timer = 500;
	int res = 0;

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x5, 1);
	while (timer > 0) {
		core_config_ice_mode_write(0x041008, 0xFF, 1);

		mdelay(1);

		if ((core_config_read_write_onebyte(0x041010) & 0x03) == 0x00)
			goto out;

		timer--;
	}

	TPD_INFO("Polling busy Time out !\n");
	res = -1;
out:
	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */
	return res;
}
EXPORT_SYMBOL(core_flash_poll_busy);

int core_flash_write_enable(void)
{
	if (core_config_ice_mode_write(0x041000, 0x0, 1) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041004, 0x66aa55, 3) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041008, 0x6, 1) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041000, 0x1, 1) < 0)
		goto out;

	return 0;

out:
	TPD_INFO("Write enable failed !\n");
	return -EIO;
}
EXPORT_SYMBOL(core_flash_write_enable);

void core_flash_enable_protect(bool enable)
{
	TPD_INFO("Set flash protect as (%d)\n", enable);

	if (core_flash_write_enable() < 0) {
		TPD_INFO("Failed to config flash's write enable\n");
		return;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	switch (flashtab->mid) {
	case 0xEF:
		if (flashtab->dev_id == 0x6012 || flashtab->dev_id == 0x6011) {
			core_config_ice_mode_write(0x041008, 0x1, 1);
			core_config_ice_mode_write(0x041008, 0x00, 1);

			if (enable)
				core_config_ice_mode_write(0x041008, 0x7E, 1);
			else
				core_config_ice_mode_write(0x041008, 0x00, 1);
		}
		break;
	case 0xC8:
		if (flashtab->dev_id == 0x6012 || flashtab->dev_id == 0x6013) {
			core_config_ice_mode_write(0x041008, 0x1, 1);
			core_config_ice_mode_write(0x041008, 0x00, 1);

			if (enable)
				core_config_ice_mode_write(0x041008, 0x7A, 1);
			else
				core_config_ice_mode_write(0x041008, 0x00, 1);
		}
		break;
	default:
		TPD_INFO("Can't find flash id, ignore protection\n");
		break;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */
	mdelay(5);
}
EXPORT_SYMBOL(core_flash_enable_protect);

void core_flash_init(uint16_t mid, uint16_t did)
{
	int i = 0;

	TPD_INFO("M_ID = %x, DEV_ID = %x", mid, did);

	flashtab = kzalloc(sizeof(ft), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flashtab)) {
		TPD_INFO("Failed to allocate flashtab memory, %ld\n", PTR_ERR(flashtab));
		return;
	}

	for (; i < ARRAY_SIZE(ft); i++) {
		if (mid == ft[i].mid && did == ft[i].dev_id) {
			TPD_INFO("Find them in flash table\n");

			flashtab->mid = mid;
			flashtab->dev_id = did;
			flashtab->mem_size = ft[i].mem_size;
			flashtab->program_page = ft[i].program_page;
			flashtab->sector = ft[i].sector;
			flashtab->block = ft[i].block;
			break;
		}
	}

	if (i >= ARRAY_SIZE(ft)) {
		TPD_INFO("Can't find them in flash table, apply default flash config\n");
		flashtab->mid = mid;
		flashtab->dev_id = did;
		flashtab->mem_size = (256 * K);
		flashtab->program_page = 256;
		flashtab->sector = (4 * K);
		flashtab->block = (64 * K);
	}

	TPD_INFO("Max Memory size = %d\n", flashtab->mem_size);
	TPD_INFO("Per program page = %d\n", flashtab->program_page);
	TPD_INFO("Sector size = %d\n", flashtab->sector);
	TPD_INFO("Block size = %d\n", flashtab->block);
}
EXPORT_SYMBOL(core_flash_init);

void core_flash_remove(void)
{
	TPD_INFO("Remove core-flash memebers\n");

	ipio_kfree((void **)&flashtab);
}
EXPORT_SYMBOL(core_flash_remove);

/* the list of support chip */
uint32_t ipio_chip_list[] = {
	CHIP_TYPE_ILI7807,
	CHIP_TYPE_ILI9881,
};

uint8_t g_read_buf[128] = { 0 };

struct core_config_data *core_config = NULL;

static void read_flash_info(uint8_t cmd, int len)
{
	int i = 0;
	uint16_t flash_id = 0;
	uint16_t flash_mid = 0;
	uint8_t buf[4] = { 0 };

	/*
	 * This command is used to fix the bug of spi clk for 7807F-AB
	 * when operating with its flash.
	 */
	if (core_config->chip_id == CHIP_TYPE_ILI7807 && core_config->chip_type == ILI7807_TYPE_F_AB) {
		core_config_ice_mode_write(0x4100C, 0x01, 1);
		mdelay(25);
	}

	core_config_ice_mode_write(0x41000, 0x0, 1);	/* CS high */
	core_config_ice_mode_write(0x41004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x41008, cmd, 1);
	for (i = 0; i < len; i++) {
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		buf[i] = core_config_ice_mode_read(0x41010);
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	/* look up flash info and init its struct after obtained flash id. */
	flash_mid = buf[0];
	flash_id = buf[1] << 8 | buf[2];
	core_flash_init(flash_mid, flash_id);
}

/*
 * It checks chip id shifting sepcific bits based on chip's requirement.
 *
 * @pid_data: 4 bytes, reading from firmware.
 *
 */
static uint32_t check_chip_id(uint32_t pid_data)
{
	int i = 0;
	uint32_t id = 0;
	uint32_t type = 0;

	id = pid_data >> 16;
	type = (pid_data & 0x0000FF00) >> 8;

	TPD_INFO("id = 0x%x, type = 0x%x\n", id, type);

	if(id == CHIP_TYPE_ILI9881) {
		for(i = ILI9881_TYPE_F; i <= ILI9881_TYPE_H; i++) {
			if (i == type) {
				core_config->chip_type = i;
				core_config->ic_reset_addr = 0x040050;
				return id;
			}
		}
	}

	if(id == CHIP_TYPE_ILI7807) {
		for(i = ILI7807_TYPE_F_AA; i <= ILI7807_TYPE_H; i++) {
			if (i == type) {
				core_config->chip_type = i;
				if (i == ILI7807_TYPE_F_AB)
					core_config->ic_reset_addr = 0x04004C;
				else if (i == ILI7807_TYPE_H)
					core_config->ic_reset_addr = 0x040050;

				return id;
			}
		}
	}

	return 0;
}

/*
 * Read & Write one byte in ICE Mode.
 */
uint32_t core_config_read_write_onebyte(uint32_t addr)
{
	int res = 0;
	uint32_t data = 0;
	uint8_t szOutBuf[64] = { 0 };

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	res = core_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, szOutBuf, 1);
	if (res < 0)
		goto out;

	data = (szOutBuf[0]);

	return data;

out:
	TPD_INFO("Failed to read/write data in ICE mode, res = %d\n", res);
	return res;
}
EXPORT_SYMBOL(core_config_read_write_onebyte);

uint32_t core_config_ice_mode_read(uint32_t addr)
{
	int res = 0;
	uint8_t szOutBuf[64] = { 0 };
	uint32_t data = 0;

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	res = core_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	data = (szOutBuf[0] + szOutBuf[1] * 256 + szOutBuf[2] * 256 * 256 + szOutBuf[3] * 256 * 256 * 256);

	return data;

out:
	TPD_INFO("Failed to read data in ICE mode, res = %d\n", res);
	return res;
}
EXPORT_SYMBOL(core_config_ice_mode_read);


/*
 * Write commands into firmware in ICE Mode.
 *
 */
int core_config_ice_mode_write(uint32_t addr, uint32_t data, uint32_t size)
{
	int res = 0;
	int i = 0;
	uint8_t szOutBuf[64] = { 0 };

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	for (i = 0; i < size; i++) {
		szOutBuf[i + 4] = (char)(data >> (8 * i));
	}

	res = core_write(core_config->slave_i2c_addr, szOutBuf, size + 4);

	if (res < 0)
		TPD_INFO("Failed to write data in ICE mode, res = %d\n", res);

	return res;
}
EXPORT_SYMBOL(core_config_ice_mode_write);

void core_config_mode_control(uint8_t *from_user)
{
	//int i = 0;
	int mode = 0;
	//int checksum = 0;
	//int codeLength = 8;
	//uint8_t mp_code[8] = { 0 };
	uint8_t cmd[4] = { 0 };

	ilitek_platform_disable_irq();

	if (from_user == NULL) {
		TPD_INFO("Arguments from user space are invaild\n");
		goto out;
	}

	TPD_DEBUG("mode = %x, b1 = %x, b2 = %x, b3 = %x\n",
	    from_user[0], from_user[1], from_user[2], from_user[3]);

	mode = from_user[0];

	if (protocol->major == 0x5) {
		if (mode == protocol->i2cuart_mode) {
			cmd[0] = protocol->cmd_i2cuart;
			cmd[1] = *(from_user + 1);
			cmd[2] = *(from_user + 2);

			TPD_INFO("Switch to I2CUART mode, cmd = %x, b1 = %x, b2 = %x\n", cmd[0], cmd[1], cmd[2]);

			if ((core_write(core_config->slave_i2c_addr, cmd, 3)) < 0) {
				TPD_INFO("Failed to switch I2CUART mode\n");
				goto out;
			}

		} else if (mode == protocol->demo_mode || mode == protocol->debug_mode) {
			cmd[0] = protocol->cmd_mode_ctrl;
			cmd[1] = mode;

			core_fr->actual_fw_mode = mode;

			TPD_INFO("Switch to Demo/Debug mode, cmd = 0x%x, b1 = 0x%x\n", cmd[0], cmd[1]);

			if ((core_write(core_config->slave_i2c_addr, cmd, 2)) < 0) {
				TPD_INFO("Failed to switch Demo/Debug mode\n");
				goto out;
			}



		} else if (mode == protocol->test_mode) {
			#if 0
			cmd[0] = protocol->cmd_mode_ctrl;
			cmd[1] = mode;

			TPD_INFO("Switch to Test mode, cmd = 0x%x, b1 = 0x%x\n", cmd[0], cmd[1]);

			if ((core_write(core_config->slave_i2c_addr, cmd, 2)) < 0) {
				TPD_INFO("Failed to switch Test mode\n");
				goto out;
			}

			cmd[0] = 0xFE;

			/* Read MP Test information to ensure if fw supports test mode. */
			core_write(core_config->slave_i2c_addr, cmd, 1);
			mdelay(10);
			core_read(core_config->slave_i2c_addr, mp_code, codeLength);

			for (i = 0; i < codeLength - 1; i++)
				checksum += mp_code[i];

			if ((-checksum & 0xFF) != mp_code[codeLength - 1]) {
				TPD_INFO("checksume error (0x%x), FW doesn't support test mode.\n",
						(-checksum & 0XFF));
				goto out;
			}
			#endif
			/* FW enter to Test Mode */
			#if (INTERFACE == SPI_INTERFACE)
			core_fr->actual_fw_mode = mode;
			#endif
			if (core_config_mp_move_code() == 0)
				core_fr->actual_fw_mode = mode;

		} else {
			TPD_INFO("Unknown firmware mode: %x\n", mode);
		}
	} else {
		TPD_INFO("Wrong the major version of protocol, 0x%x\n", protocol->major);
	}

out:
	ilitek_platform_enable_irq();
}
EXPORT_SYMBOL(core_config_mode_control);

int core_config_mp_move_code(void)
{
	TPD_INFO("Prepaing to enter Test Mode\n");
#ifdef HOST_DOWNLOAD
	//ilitek_platform_tp_hw_reset(true);
	ilitek_reset((void *)ipd);
#else
	if (core_config_check_cdc_busy(50) < 0) {
		TPD_INFO("Check busy is timout ! Enter Test Mode failed\n");
		return -1;
	}

	if (core_config_ice_mode_enable() < 0) {
		TPD_INFO("Failed to enter ICE mode\n");
		return -1;
	}

	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
	}

	/* DMA Trigger */
	core_config_ice_mode_write(0x41010, 0xFF, 1);

	mdelay(30);

	/* CS High */
	core_config_ice_mode_write(0x041000, 0x1, 1);

	mdelay(60);
	core_fr->actual_fw_mode = protocol->test_mode;
	/* Code reset */
	core_config_ice_mode_write(0x40040, 0xAE, 1);

	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
	}

	core_config_ice_mode_disable();

	if (core_config_check_cdc_busy(300) < 0) {
		TPD_INFO("Check busy is timout ! Enter Test Mode failed\n");
		return -1;
	}
#endif
	TPD_INFO("FW Test Mode ready\n");
	return 0;
}
EXPORT_SYMBOL(core_config_mp_move_code);

/*
 * Doing soft reset on ic.
 *
 * It resets ic's status, moves code and leave ice mode automatically if in
 * that mode.
 */
void core_config_ic_reset(void)
{
#ifdef HOST_DOWNLOAD
	core_config_ice_mode_disable();
#else
	uint32_t key = 0;
	if (core_config->chip_id == CHIP_TYPE_ILI7807) {
		if (core_config->chip_type == ILI7807_TYPE_H)
			key = 0x00117807;
		else
			key = 0x00017807;
	}
	if (core_config->chip_id == CHIP_TYPE_ILI9881) {
		key = 0x00019881;
	}

	TPD_DEBUG("key = 0x%x\n", key);
	if (key != 0) {
		core_config->do_ic_reset = true;
		core_config_ice_mode_write(core_config->ic_reset_addr, key, 4);
		core_config->do_ic_reset = false;
	}

	msleep(300);
#endif
}
EXPORT_SYMBOL(core_config_ic_reset);

void core_config_sense_ctrl(bool start)
{
	TPD_DEBUG("sense start = %d\n", start);

	return core_protocol_func_control(0, start);
}
EXPORT_SYMBOL(core_config_sense_ctrl);

void core_config_sleep_ctrl(bool out)
{
	TPD_DEBUG("Sleep Out = %d\n", out);

	return core_protocol_func_control(1, out);
}
EXPORT_SYMBOL(core_config_sleep_ctrl);

void core_config_glove_ctrl(bool enable, bool seamless)
{
	int cmd = 0x2;		/* default as semaless */

	if (!seamless) {
		if (enable)
			cmd = 0x1;
		else
			cmd = 0x0;
	}

	TPD_INFO("Glove = %d, seamless = %d, cmd = %d\n", enable, seamless, cmd);

	return core_protocol_func_control(2, cmd);
}
EXPORT_SYMBOL(core_config_glove_ctrl);

void core_config_stylus_ctrl(bool enable, bool seamless)
{
	int cmd = 0x2;		/* default as semaless */

	if (!seamless) {
		if (enable)
			cmd = 0x1;
		else
			cmd = 0x0;
	}

	TPD_INFO("stylus = %d, seamless = %d, cmd = %x\n", enable, seamless, cmd);

	return core_protocol_func_control(3, cmd);
}
EXPORT_SYMBOL(core_config_stylus_ctrl);

void core_config_tp_scan_mode(bool mode)
{
	TPD_DEBUG("TP Scan mode = %d\n", mode);

	return core_protocol_func_control(4, mode);
}
EXPORT_SYMBOL(core_config_tp_scan_mode);

void core_config_lpwg_ctrl(bool enable)
{
	TPD_DEBUG("LPWG = %d\n", enable);

	return core_protocol_func_control(5, enable);
}
EXPORT_SYMBOL(core_config_lpwg_ctrl);

void core_config_gesture_ctrl(uint8_t func)
{
	uint8_t max_byte = 0x0;
	uint8_t min_byte = 0x0;

	TPD_INFO("Gesture function = 0x%x\n", func);

	max_byte = 0x3F;
	min_byte = 0x20;

	if (func > max_byte || func < min_byte) {
		TPD_INFO("Gesture ctrl error, 0x%x\n", func);
		return;
	}

	return core_protocol_func_control(6, func);
}
EXPORT_SYMBOL(core_config_gesture_ctrl);

void core_config_phone_cover_ctrl(bool enable)
{
	TPD_INFO("Phone Cover = %d\n", enable);

	return core_protocol_func_control(7, enable);
}
EXPORT_SYMBOL(core_config_phone_cover_ctrl);

void core_config_finger_sense_ctrl(bool enable)
{
	TPD_INFO("Finger sense = %d\n", enable);

	return core_protocol_func_control(0, enable);
}
EXPORT_SYMBOL(core_config_finger_sense_ctrl);

void core_config_proximity_ctrl(bool enable)
{
	TPD_INFO("Proximity = %d\n", enable);

	return core_protocol_func_control(11, enable);
}
EXPORT_SYMBOL(core_config_proximity_ctrl);

void core_config_plug_ctrl(bool out)
{
	TPD_DEBUG("Plug Out = %d\n", out);

	return core_protocol_func_control(12, out);
}
EXPORT_SYMBOL(core_config_plug_ctrl);

void core_config_edge_limit_ctrl(bool enable)
{
	uint8_t cmd[3] = {0};
	cmd[0] = 0x01;
	cmd[1] = 0x12;
	TPD_DEBUG("edge_limit = %d touch_direction = %d\n", enable, ipd->touch_direction);
	if (ipd->fw_edge_limit_support) {
		if (enable || (VERTICAL_SCREEN == ipd->touch_direction)) {
			cmd[2] = 0x01;
			core_write(core_config->slave_i2c_addr, cmd, 3);
		} else if (LANDSCAPE_SCREEN_270 == ipd->touch_direction) {
			cmd[2] = 0x00;
			core_write(core_config->slave_i2c_addr, cmd, 3);
		} else if (LANDSCAPE_SCREEN_90 == ipd->touch_direction) {
			cmd[2] = 0x02;
			core_write(core_config->slave_i2c_addr, cmd, 3);
		}
	} else if (enable) {
		return core_protocol_func_control(13, enable);
	}
}
EXPORT_SYMBOL(core_config_edge_limit_ctrl);

void core_config_headset_ctrl(bool enable)
{
	uint8_t cmd[3] = {0};
	cmd[0] = 0x01;
	cmd[1] = 0x14;
	if (enable) {
		cmd[2] = 0x01;
	} else {
		cmd[2] = 0x00;
	}
	TPD_DEBUG("headset write 0x%X 0x%X 0x%X\n", cmd[0], cmd[1], cmd[2]);
	core_write(core_config->slave_i2c_addr, cmd, 3);
}
EXPORT_SYMBOL(core_config_headset_ctrl);

void core_config_lock_point_ctrl(bool enable)
{
	TPD_DEBUG("lock_point = %d\n", enable);

	return core_protocol_func_control(14, enable);
}
EXPORT_SYMBOL(core_config_lock_point_ctrl);

void core_config_set_phone_cover(uint8_t *pattern)
{
	int i = 0;

	if (pattern == NULL) {
		TPD_INFO("Invaild pattern\n");
		return;
	}

	for(i = 0; i < 8; i++)
		protocol->phone_cover_window[i+1] = pattern[i];

	TPD_INFO("window: cmd = 0x%x\n", protocol->phone_cover_window[0]);
	TPD_INFO("window: ul_x_l = 0x%x, ul_x_h = 0x%x\n", protocol->phone_cover_window[1],
		 protocol->phone_cover_window[2]);
	TPD_INFO("window: ul_y_l = 0x%x, ul_y_l = 0x%x\n", protocol->phone_cover_window[3],
		 protocol->phone_cover_window[4]);
	TPD_INFO("window: br_x_l = 0x%x, br_x_l = 0x%x\n", protocol->phone_cover_window[5],
		 protocol->phone_cover_window[6]);
	TPD_INFO("window: br_y_l = 0x%x, br_y_l = 0x%x\n", protocol->phone_cover_window[7],
		 protocol->phone_cover_window[8]);

	core_protocol_func_control(9, 0);
}
EXPORT_SYMBOL(core_config_set_phone_cover);

/*
 * ic_suspend: Get IC to suspend called from system.
 *
 * The timing when goes to sense stop or houw much times the command need to be called
 * is depending on customer's system requirement, which might be different due to
 * the DDI design or other conditions.
 */
void core_config_ic_suspend(void)
{
	//uint8_t temp[4] = {0};
	core_gesture->suspend = true;
	TPD_INFO("Starting to suspend ...\n");

	/* sense stop */
	core_config_sense_ctrl(false);
	if (ipio_debug_level & DEBUG_GESTURE) {
		core_config_get_reg_data(0x44008);
		core_config_get_reg_data(0x40054);
		core_config_get_reg_data(0x5101C);
		core_config_get_reg_data(0x51020);
		core_config_get_reg_data(0x4004C);
	}
	/* check system busy */
	if (core_config_check_cdc_busy(5) < 0) {
		TPD_INFO("Check busy is timout retry!\n");
		core_config_sense_ctrl(false);
		core_config_check_cdc_busy(5);
	}

	TPD_INFO("Enabled Gesture = %d\n", core_config->isEnableGesture);

	if (core_config->isEnableGesture) {
		//core_fr->actual_fw_mode = P5_0_FIRMWARE_GESTURE_MODE;
		#ifdef HOST_DOWNLOAD
		if (P5_0_FIRMWARE_GESTURE_MODE != core_firmware->enter_mode) {
			if(core_load_gesture_code() < 0) {
				TPD_INFO("load gesture code fail\n");
			}
		}
		#endif
	} else {
		/*deep sleep in */
		core_config->ili_sleep_type = SLEEP_IN_DEEP;
		core_config_sleep_ctrl(false);
	}

	TPD_INFO("Suspend done\n");
}
EXPORT_SYMBOL(core_config_ic_suspend);

void core_config_ic_suspend_ftm(void)
{
	//uint8_t temp[4] = {0};
	core_gesture->suspend = true;
	TPD_INFO("Starting to suspend ...\n");

	/* sense stop */
	core_config_sense_ctrl(false);
	if (ipio_debug_level & DEBUG_GESTURE) {
		core_config_get_reg_data(0x44008);
		core_config_get_reg_data(0x40054);
		core_config_get_reg_data(0x5101C);
		core_config_get_reg_data(0x51020);
		core_config_get_reg_data(0x4004C);
	}
	/* check system busy */
	if (core_config_check_cdc_busy(5) < 0) {
		TPD_INFO("Check busy is timout retry!\n");
		core_config_sense_ctrl(false);
		core_config_check_cdc_busy(5);
	}

	core_config->ili_sleep_type = SLEEP_IN_END_FTM;
	/*ftm deep sleep in */
	core_config_sleep_ctrl(false);

	TPD_INFO("Suspend done\n");
}
EXPORT_SYMBOL(core_config_ic_suspend_ftm);

/*
 * ic_resume: Get IC to resume called from system.
 *
 * The timing when goes to sense start or houw much times the command need to be called
 * is depending on customer's system requirement, which might be different due to
 * the DDI design or other conditions.
 */
void core_config_ic_resume(void)
{
	core_gesture->suspend = false;
	TPD_INFO("Starting to resume ...\n");
	if (core_config->isEnableGesture) {
		#ifdef HOST_DOWNLOAD
		if(core_load_ap_code() < 0)
		{
			TPD_INFO("load ap code fail\n");
			ilitek_platform_tp_hw_reset(true);
		}
		#endif
	}
	else{
		ilitek_platform_tp_hw_reset(true);
	}
	/* sleep out */
	core_config_sleep_ctrl(true);

	/* check system busy */
	if (core_config_check_cdc_busy(50) < 0)
		TPD_INFO("Check busy is timout !\n");

	/* sense start for TP */
	core_config_sense_ctrl(true);
	core_config_mode_control( &protocol->demo_mode);
	/* Soft reset */
	// core_config_ice_mode_enable();
	// mdelay(10);
	// core_config_ic_reset();
	TPD_INFO("Resume done\n");
}
EXPORT_SYMBOL(core_config_ic_resume);

int core_config_ice_mode_disable(void)
{
	uint32_t res = 0;
	uint8_t cmd[4];
	cmd[0] = 0x1b;
	cmd[1] = 0x62;
	cmd[2] = 0x10;
	cmd[3] = 0x18;

	TPD_DEBUG("ICE Mode disabled\n");
	res = core_write(core_config->slave_i2c_addr, cmd, 4);
	core_config->icemodeenable = false;
	return res;
}
EXPORT_SYMBOL(core_config_ice_mode_disable);

int core_config_ice_mode_enable(void)
{
	TPD_DEBUG("ICE Mode enabled\n");
	core_config->icemodeenable = true;
	if (core_config_ice_mode_write(0x181062, 0x0, 0) < 0)
		return -1;

	return 0;
}
EXPORT_SYMBOL(core_config_ice_mode_enable);

int core_config_set_watch_dog(bool enable)
{
	int timeout = 10;
	int ret = 0;
	uint8_t off_bit = 0x5A;
	uint8_t on_bit = 0xA5;
	uint8_t value_low = 0x0;
	uint8_t value_high = 0x0;
	uint32_t wdt_addr = core_config->wdt_addr;

	if (wdt_addr <= 0 || core_config->chip_id <= 0) {
		TPD_INFO("WDT/CHIP ID is invalid\n");
		return -EINVAL;
	}

	/* Config register and values by IC */
	if (core_config->chip_id == CHIP_TYPE_ILI7807) {
		value_low = 0x07;
		value_high = 0x78;
	} else if (core_config->chip_id == CHIP_TYPE_ILI9881 ) {
		value_low = 0x81;
		value_high = 0x98;
	} else {
		TPD_INFO("Unknown CHIP type (0x%x)\n",core_config->chip_id);
		return -ENODEV;
	}

	if (enable) {
		core_config_ice_mode_write(wdt_addr, 1, 1);
	} else {
		core_config_ice_mode_write(wdt_addr, value_low, 1);
		core_config_ice_mode_write(wdt_addr, value_high, 1);
	}

	while (timeout > 0) {
		ret = core_config_ice_mode_read(0x51018);
		TPD_DEBUG("bit = %x\n", ret);

		if (enable) {
			if (CHECK_EQUAL(ret, on_bit) == 0)
				break;
		} else {
			if (CHECK_EQUAL(ret, off_bit) == 0)
				break;
		}

		timeout--;
		mdelay(10);
	}

	if (timeout > 0) {
		if (enable) {
			TPD_INFO("WDT turn on succeed\n");
		} else {
			core_config_ice_mode_write(wdt_addr, 0, 1);
			TPD_INFO("WDT turn off succeed\n");
		}
	} else {
		TPD_INFO("WDT turn on/off timeout !\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(core_config_set_watch_dog);

int core_config_check_cdc_busy(int delay)
{
	int timer = delay;
	int res = -1;
	uint8_t cmd[2] = { 0 };
	uint8_t busy[2] = { 0 };

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_cdc_busy;

	while (timer > 0) {
		mdelay(100);
		core_write(core_config->slave_i2c_addr, cmd, 2);
		mdelay(1);
		core_write(core_config->slave_i2c_addr, &cmd[1], 1);
		mdelay(1);
		core_read(core_config->slave_i2c_addr, busy, 2);
		TPD_DEBUG("CDC busy state = 0x%x\n", busy[0]);
		if (busy[0] == 0x41 || busy[0] == 0x51) {
			res = 0;
			break;
		}
		timer--;
	}
	if (res < 0)
		TPD_INFO("Check busy timeout !!\n");
	return res;
}
EXPORT_SYMBOL(core_config_check_cdc_busy);

int core_config_check_int_status(bool high)
{
	int timer = 1000;
	int res = -1;
	/* From FW request, timeout should at least be 5 sec */
	while (timer) {
		if(high) {
			if (gpio_get_value(ipd->int_gpio)) {
				res = 0;
				break;
			}
		} else {
			if (!gpio_get_value(ipd->int_gpio)) {
				res = 0;
				break;
			}
		}

		mdelay(5);
		timer--;
	}

	if (res < 0)
		TPD_INFO("Check busy timeout !!\n");

	return res;
}
EXPORT_SYMBOL(core_config_check_int_status);

int core_config_check_data_ready(void)
{
	int timer = 600;
	int res = -1;
	/* From FW request, timeout should at least be 3 sec */
	while (timer) {
		if (ipd->Mp_test_data_ready == true) {
			res = 0;
			break;
		}
		mdelay(5);
		timer--;
	}

	if (res < 0)
		TPD_INFO("Check busy timeout !!\n");

	return res;
}
EXPORT_SYMBOL(core_config_check_data_ready);

int core_config_get_project_id(uint8_t *pid_data)
{
	int i = 0;
	int res = 0;
	uint32_t pid_addr = 0x1D000;
	uint32_t pid_size = 10;

	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		return -1;
	}

	/* Disable watch dog */
	core_config_set_watch_dog(false);

	core_config_ice_mode_write(0x041000, 0x0, 1);   /* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);  /* Key */

	core_config_ice_mode_write(0x041008, 0x06, 1);
	core_config_ice_mode_write(0x041000, 0x01, 1);
	core_config_ice_mode_write(0x041000, 0x00, 1);
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);  /* Key */
	core_config_ice_mode_write(0x041008, 0x03, 1);

	core_config_ice_mode_write(0x041008, (pid_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (pid_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (pid_addr & 0x0000FF), 1);

	for(i = 0; i < pid_size; i++) {
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		pid_data[i] = core_config_ice_mode_read(0x41010);
		TPD_INFO("pid_data[%d] = 0x%x\n", i, pid_data[i]);
	}

	core_config_ice_mode_write(0x041010, 0x1, 0);   /* CS high */
	core_config_ic_reset();

	return res;
}
EXPORT_SYMBOL(core_config_get_project_id);

int core_config_get_key_info(void)
{
	int res = 0;
	int i = 0;
	uint8_t cmd[2] = { 0 };

	memset(g_read_buf, 0, sizeof(g_read_buf));

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_key_info;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, &g_read_buf[0], protocol->key_info_len);
	if (res < 0) {
		TPD_INFO("Failed to read data, %d\n", res);
		goto out;
	}
	if (g_read_buf[0] != cmd[1]) {
		TPD_INFO("Failed to read key info packet data[0] = 0x%X\n", g_read_buf[0]);
		goto out;
	}
	if (core_config->tp_info->nKeyCount) {
		if (core_config->tp_info->nKeyCount > 10) {
			TPD_INFO("nKeyCount = %d > 10 force set 10\n", core_config->tp_info->nKeyCount);
			core_config->tp_info->nKeyCount = 10;
		}
		/* NOTE: Firmware not ready yet */
		core_config->tp_info->nKeyAreaXLength = (g_read_buf[0] << 8) + g_read_buf[1];
		core_config->tp_info->nKeyAreaYLength = (g_read_buf[2] << 8) + g_read_buf[3];

		TPD_INFO("key: length of X area = %x\n", core_config->tp_info->nKeyAreaXLength);
		TPD_INFO("key: length of Y area = %x\n", core_config->tp_info->nKeyAreaYLength);

		for (i = 0; i < core_config->tp_info->nKeyCount; i++) {
			core_config->tp_info->virtual_key[i].nId = g_read_buf[i * 5 + 4];
			core_config->tp_info->virtual_key[i].nX = (g_read_buf[i * 5 + 5] << 8) + g_read_buf[i * 5 + 6];
			core_config->tp_info->virtual_key[i].nY = (g_read_buf[i * 5 + 7] << 8) + g_read_buf[i * 5 + 8];
			core_config->tp_info->virtual_key[i].nStatus = 0;

			TPD_INFO("key: id = %d, X = %d, Y = %d\n", core_config->tp_info->virtual_key[i].nId,
				 core_config->tp_info->virtual_key[i].nX, core_config->tp_info->virtual_key[i].nY);
		}
	}

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_key_info);

int core_config_get_tp_info(void)
{
	int res = 0;
	uint8_t cmd[2] = { 0 };

	memset(g_read_buf, 0, sizeof(g_read_buf));

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_tp_info;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, &g_read_buf[0], protocol->tp_info_len);
	if (res < 0) {
		TPD_INFO("Failed to read data, %d\n", res);
		goto out;
	}
	if (g_read_buf[0] != cmd[1]) {
		TPD_INFO("Failed to read tp info packet data[0] = 0x%X\n", g_read_buf[0]);
		goto out;
	}
	/* in protocol v5, ignore the first btye because of a header. */
	core_config->tp_info->nMinX = g_read_buf[1];
	core_config->tp_info->nMinY = g_read_buf[2];
	core_config->tp_info->nMaxX = (g_read_buf[4] << 8) + g_read_buf[3];
	core_config->tp_info->nMaxY = (g_read_buf[6] << 8) + g_read_buf[5];
	core_config->tp_info->nXChannelNum = g_read_buf[7];
	core_config->tp_info->nYChannelNum = g_read_buf[8];
	core_config->tp_info->self_tx_channel_num = g_read_buf[11];
	core_config->tp_info->self_rx_channel_num = g_read_buf[12];
	core_config->tp_info->side_touch_type = g_read_buf[13];
	core_config->tp_info->nMaxTouchNum = g_read_buf[9];
	core_config->tp_info->nKeyCount = g_read_buf[10];

	core_config->tp_info->nMaxKeyButtonNum = 5;

	TPD_DEBUG("minX = %d, minY = %d, maxX = %d, maxY = %d\n",
		 core_config->tp_info->nMinX, core_config->tp_info->nMinY,
		 core_config->tp_info->nMaxX, core_config->tp_info->nMaxY);
	TPD_DEBUG("xchannel = %d, ychannel = %d, self_tx = %d, self_rx = %d\n",
		 core_config->tp_info->nXChannelNum, core_config->tp_info->nYChannelNum,
		 core_config->tp_info->self_tx_channel_num, core_config->tp_info->self_rx_channel_num);
	TPD_DEBUG("side_touch_type = %d, max_touch_num= %d, touch_key_num = %d, max_key_num = %d\n",
		 core_config->tp_info->side_touch_type, core_config->tp_info->nMaxTouchNum,
		 core_config->tp_info->nKeyCount, core_config->tp_info->nMaxKeyButtonNum);

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_tp_info);

int core_config_get_protocol_ver(void)
{
	int res = 0;
	int i = 0;
	int major = 0;
	int mid = 0;
	int minor = 0;
	uint8_t cmd[2] = { 0 };

	memset(g_read_buf, 0, sizeof(g_read_buf));
	memset(core_config->protocol_ver, 0x0, sizeof(core_config->protocol_ver));

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_pro_ver;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, &g_read_buf[0], protocol->pro_ver_len);
	if (res < 0) {
		TPD_INFO("Failed to read data, %d\n", res);
		goto out;
	}
	if (g_read_buf[0] != cmd[1]) {
		TPD_INFO("Failed to read protocol version packet data[0] = 0x%X\n", g_read_buf[0]);
		goto out;
	}
	TPD_DEBUG("protocol->pro_ver_len = %d\n",protocol->pro_ver_len);
	/* ignore the first btye because of a header. */
	for (; i < protocol->pro_ver_len - 1; i++) {
		//TPD_INFO("g_read_buf[%d] = %x\n",i,g_read_buf[i]);
		core_config->protocol_ver[i] = g_read_buf[i + 1];
	}

	TPD_DEBUG("Procotol Version = %d.%d.%d\n",
		 core_config->protocol_ver[0], core_config->protocol_ver[1], core_config->protocol_ver[2]);

	major = core_config->protocol_ver[0];
	mid = core_config->protocol_ver[1];
	minor = core_config->protocol_ver[2];

	/* update protocol if they're different with the default ver set by driver */
	if (major != PROTOCOL_MAJOR || mid != PROTOCOL_MID || minor != PROTOCOL_MINOR) {
		res = core_protocol_update_ver(major, mid, minor);
		if (res < 0)
			TPD_INFO("Protocol version is invalid\n");
	}

out:
	return 0;//res;
}
EXPORT_SYMBOL(core_config_get_protocol_ver);

int core_config_get_core_ver(void)
{
	int res = 0;
	int i = 0;
	uint8_t cmd[2] = { 0 };

	memset(g_read_buf, 0, sizeof(g_read_buf));

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_core_ver;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, &g_read_buf[0], protocol->core_ver_len);
	if (res < 0) {
		TPD_INFO("Failed to read data, %d\n", res);
		goto out;
	}
	if (g_read_buf[0] != cmd[1]) {
		TPD_INFO("Failed to read core version packet data[0] = 0x%X\n", g_read_buf[0]);
		goto out;
	}
	for (; i < protocol->core_ver_len - 1; i++)
		core_config->core_ver[i] = g_read_buf[i + 1];

	/* in protocol v5, ignore the first btye because of a header. */
	TPD_DEBUG("Core Version = %d.%d.%d.%d\n",
		 core_config->core_ver[1], core_config->core_ver[2],
		 core_config->core_ver[3], core_config->core_ver[4]);

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_core_ver);

/*
 * Getting the version of firmware used on the current one.
 *
 */
int core_config_get_fw_ver(void)
{
	int res = 0;
	int i = 0;
	uint8_t cmd[2] = { 0 };
	char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
	memset(g_read_buf, 0, sizeof(g_read_buf));

	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_fw_ver;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("Failed to write data, %d\n", res);
		goto out;
	}

	mdelay(1);

	res = core_read(core_config->slave_i2c_addr, &g_read_buf[0], protocol->fw_ver_len);
	if (res < 0) {
		TPD_INFO("Failed to read fw version %d\n", res);
		goto out;
	}
	if (g_read_buf[0] != cmd[1]) {
		TPD_INFO("Failed to read fw version packet data[0] = 0x%X\n", g_read_buf[0]);
		goto out;
	}
	for (; i < protocol->fw_ver_len; i++)
		core_config->firmware_ver[i] = g_read_buf[i];

	/* in protocol v5, ignore the first btye because of a header. */
	if (protocol->mid >= 0x3)
	{
		TPD_INFO("Firmware Version = %d.%d.%d.%d\n",
		 core_config->firmware_ver[1], core_config->firmware_ver[2], core_config->firmware_ver[3],
		  core_config->firmware_ver[4]);
	}
	else
	{
		TPD_INFO("Firmware Version = %d.%d.%d\n",
		 core_config->firmware_ver[1], core_config->firmware_ver[2], core_config->firmware_ver[3]);
	}
	sprintf(dev_version, "%02X", core_config->firmware_ver[3]);
	if (ipd->fw_version) {
		strlcpy(&(ipd->fw_version[12]), dev_version, 3);
	}
out:
	return res;
}
EXPORT_SYMBOL(core_config_get_fw_ver);

int core_config_get_chip_id(void)
{
	int res = 0;
	static int do_once = 0;
	uint32_t RealID = 0;
	uint32_t PIDData = 0;
	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		goto out;
	}

	mdelay(1);

	PIDData = core_config_ice_mode_read(core_config->pid_addr);
	core_config->chip_pid = PIDData;
	core_config->core_type = PIDData & 0xFF;
	TPD_INFO("PID = 0x%x, Core type = 0x%x\n",
		core_config->chip_pid, core_config->core_type);
	#ifdef CHECK_REG
	core_get_tp_register();
	#endif
	if (PIDData) {
		RealID = check_chip_id(PIDData);
		if (RealID != core_config->chip_id) {
			TPD_INFO("CHIP ID ERROR: 0x%x, TP_TOUCH_IC = 0x%x\n", RealID, TP_TOUCH_IC);
			res = -ENODEV;
			goto out;
		}
	} else {
		TPD_INFO("PID DATA error : 0x%x\n", PIDData);
		res = -EINVAL;
		goto out;
	}

	if (do_once == 0) {
		/* reading flash id needs to let ic entry to ICE mode */
		read_flash_info(0x9F, 4);
		do_once = 1;
	}

out:
	core_config_ic_reset();
#ifndef HOST_DOWNLOAD
	mdelay(150);
#endif
	return res;
}

EXPORT_SYMBOL(core_config_get_chip_id);


uint32_t core_config_get_reg_data(uint32_t addr)
{
	int res = 0;
	uint32_t reg_data = 0;

	res = core_config_ice_mode_enable();
	if (res < 0) {
		TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		goto out;
	}
	mdelay(1);
	reg_data = core_config_ice_mode_read(addr);
	TPD_DEBUG("addr = 0x%X reg_data = 0x%X\n", addr, reg_data);


out:
	core_config_ice_mode_disable();
	return reg_data;
}
EXPORT_SYMBOL(core_config_get_reg_data);

void core_config_wr_pack( int packet )
{
	int retry = 100;
	uint32_t reg_data = 0;
	while(retry--) {
		reg_data = core_config_read_write_onebyte(0x73010);
		if ((reg_data & 0x02) == 0) {
			TPD_INFO("check ok 0x73010 read 0x%X retry = %d\n", reg_data, retry);
			break;
		}
		mdelay(10);
	}
	if (retry <= 0) {
		TPD_INFO("check 0x73010 error read 0x%X\n", reg_data);
	}
	core_config_ice_mode_write(0x73000, packet, 4);
}

uint32_t core_config_rd_pack( int packet)
{
	int retry = 100;
	uint32_t reg_data = 0;
	while(retry--) {
		reg_data = core_config_read_write_onebyte(0x73010);
		if ((reg_data & 0x02) == 0) {
			TPD_INFO("check  ok 0x73010 read 0x%X retry = %d\n", reg_data, retry);
			break;
		}
		mdelay(10);
	}
	if (retry <= 0) {
		TPD_INFO("check 0x73010 error read 0x%X\n", reg_data);
	}
	core_config_ice_mode_write(0x73000, packet, 4);

	retry = 100;
	while(retry--) {
		reg_data = core_config_read_write_onebyte(0x4800A);
		if ((reg_data & 0x02) == 0x02) {
			TPD_INFO("check  ok 0x4800A read 0x%X retry = %d\n", reg_data, retry);
			break;
		}
		mdelay(10);
	}
	if (retry <= 0) {
		TPD_INFO("check 0x4800A error read 0x%X\n", reg_data);
	}
	core_config_ice_mode_write(0x4800A, 0x02, 1);
	reg_data = core_config_ice_mode_read(0x73016);
	return reg_data;
}

void core_get_ddi_register_onlyone(uint8_t page, uint8_t reg)
{
	uint32_t reg_data = 0;
	uint32_t setpage = 0x1FFFFF00 | page;
	uint32_t setreg = 0x2F000100 | (reg << 16);
	TPD_INFO("setpage =  0x%X setreg = 0x%X\n", setpage, setreg);
	////TDI_RD_KEY
	core_config_wr_pack(0x1FFF9487);
	//( *( __IO uint8 *)	(0x4800A) ) =0x2
	core_config_ice_mode_write(0x4800A, 0x02, 1);

	//// Read Page reg
	core_config_wr_pack(setpage);
	reg_data = core_config_rd_pack(setreg);
	TPD_INFO("check page = 0x%X reg = 0x%X read 0x%X\n", page, reg, reg_data);

	////TDI_RD_KEY OFF
	core_config_wr_pack(0x1FFF9400);
}

void core_set_ddi_register_onlyone(uint8_t page, uint8_t reg, uint8_t data)
{
	uint32_t setpage = 0x1FFFFF00 | page;
	uint32_t setreg = 0x1F000100 | (reg << 16) | data;
	TPD_INFO("setpage =  0x%X setreg = 0x%X\n", setpage, setreg);
	////TDI_WR_KEY
	core_config_wr_pack(0x1FFF9527);
	//// Switch to Page
	core_config_wr_pack(setpage);
	//// Page ,
	core_config_wr_pack(setreg);
	////TDI_WR_KEY OFF
	core_config_wr_pack(0x1FFF9500);
}

void core_get_ddi_register(void)
{
	uint32_t reg_data = 0;
	#if 0
	////TDI_WR_KEY
	core_config_wr_pack(0x1FFF9527);
	//// Switch to Page 0E
	core_config_wr_pack(0x1FFFFF0E);
	//// Page 0x0E, Command 0x00 = 8'h80
	core_config_wr_pack(0x1F000180); 
	////TDI_WR_KEY OFF
	core_config_wr_pack(0x1FFF9500);
	#endif
	////TDI_RD_KEY
	core_config_wr_pack(0x1FFF9487);
	//( *( __IO uint8 *)	(0x4800A) ) =0x2
	core_config_ice_mode_write(0x4800A, 0x02, 1);

	//// Read Page6_RDD
	core_config_wr_pack(0x1FFFFF06);
	reg_data = core_config_rd_pack(0x2FDD0100);
	TPD_INFO("check Page6_RDD 0x2FDD0100 read 0x%X\n", reg_data);

	//// Read Page0_R0A
	core_config_wr_pack(0x1FFFFF00);
	reg_data = core_config_rd_pack(0x2F0A0100);
	TPD_INFO("check Page0_R0A 0x2F0A0100 read 0x%X\n", reg_data);

	//// Read Page6_RD3
	core_config_wr_pack(0x1FFFFF06);
	reg_data = core_config_rd_pack(0x2FD30100);
	TPD_INFO("check Page6_RD3 0x2FD30100 read 0x%X\n", reg_data);
	//// Read PageE_R07
	core_config_wr_pack(0x1FFFFF0E);
	reg_data = core_config_rd_pack(0x2F070100);
	TPD_INFO("check PageE_R07 0x2F070100 read 0x%X\n", reg_data);
	//// Read PageE_R06
	core_config_wr_pack(0x1FFFFF0E);
	reg_data = core_config_rd_pack(0x2F060100);
	TPD_INFO("check PageE_R06 0x2F060100 read 0x%X\n", reg_data);
	////TDI_RD_KEY OFF
	core_config_wr_pack(0x1FFF9400);
}

void core_get_tp_register(void)
{
	uint32_t reg_data = 0;
	reg_data = core_config_ice_mode_read(core_config->pid_addr);
	TPD_INFO("PID = 0x%x\n", reg_data);
	reg_data = core_config_ice_mode_read(0x44008);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x44008, reg_data);
	reg_data = core_config_ice_mode_read(0x40044);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x40044, reg_data);
	reg_data = core_config_ice_mode_read(0x40048);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x40048, reg_data);
	reg_data = core_config_ice_mode_read(0x4004C);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x4004C, reg_data);
	reg_data = core_config_ice_mode_read(0x4701C);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x4701C, reg_data);
	reg_data = core_config_ice_mode_read(0x45024);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x45024, reg_data);
	reg_data = core_config_ice_mode_read(0x45028);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x45028, reg_data);
	reg_data = core_config_ice_mode_read(0x71014);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x71014, reg_data);
	reg_data = core_config_ice_mode_read(0x43080);
	TPD_INFO("addr = 0x%X reg_data = 0x%X\n", 0x43080, reg_data);
}

int core_config_init(void)
{
	int i = 0;

	core_config = kzalloc(sizeof(*core_config) * sizeof(uint8_t) * 6, GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_config)) {
		TPD_INFO("Failed to allocate core_config mem, %ld\n", PTR_ERR(core_config));
		core_config_remove();
		return -ENOMEM;
	}

	core_config->tp_info = kzalloc(sizeof(*core_config->tp_info), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_config->tp_info)) {
		TPD_INFO("Failed to allocate core_config->tp_info mem, %ld\n", PTR_ERR(core_config->tp_info));
		core_config_remove();
		return -ENOMEM;
	}

	for (; i < ARRAY_SIZE(ipio_chip_list); i++) {
		if (ipio_chip_list[i] == TP_TOUCH_IC) {
			core_config->chip_id = ipio_chip_list[i];
			core_config->chip_type = 0x0000;

			core_config->do_ic_reset = false;
			core_config->isEnableGesture = false;

			if (core_config->chip_id == CHIP_TYPE_ILI7807) {
				core_config->slave_i2c_addr = ILI7807_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI7807_ICE_MODE_ADDR;
				core_config->pid_addr = ILI7807_PID_ADDR;
				core_config->wdt_addr = ILI7808_WDT_ADDR;
			} else if (core_config->chip_id == CHIP_TYPE_ILI9881) {
				core_config->slave_i2c_addr = ILI9881_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI9881_ICE_MODE_ADDR;
				core_config->pid_addr = ILI9881_PID_ADDR;
				core_config->wdt_addr = ILI9881_WDT_ADDR;
			}
			return 0;
		}
	}

	TPD_INFO("Can't find this chip in support list\n");
	return 0;
}
EXPORT_SYMBOL(core_config_init);

void core_config_remove(void)
{
	TPD_INFO("Remove core-config memebers\n");

	if (core_config != NULL) {
		ipio_kfree((void **)&core_config->tp_info);
		ipio_kfree((void **)&core_config);
	}
}
EXPORT_SYMBOL(core_config_remove);
