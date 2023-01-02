// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek_ili9881h.h"
#include "firmware.h"
#include "finger_report.h"
#include "data_transfer.h"
#include "protocol.h"
#include "mp_test.h"

#define USER_STR_BUFF	128
#define ILITEK_IOCTL_MAGIC	100
#define ILITEK_IOCTL_MAXNR	19

#define ILITEK_IOCTL_I2C_WRITE_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 0, uint8_t*)
#define ILITEK_IOCTL_I2C_SET_WRITE_LENGTH	_IOWR(ILITEK_IOCTL_MAGIC, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 2, uint8_t*)
#define ILITEK_IOCTL_I2C_SET_READ_LENGTH	_IOWR(ILITEK_IOCTL_MAGIC, 3, int)

#define ILITEK_IOCTL_TP_HW_RESET			_IOWR(ILITEK_IOCTL_MAGIC, 4, int)
#define ILITEK_IOCTL_TP_POWER_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 5, int)
#define ILITEK_IOCTL_TP_REPORT_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 6, int)
#define ILITEK_IOCTL_TP_IRQ_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 7, int)

#define ILITEK_IOCTL_TP_DEBUG_LEVEL			_IOWR(ILITEK_IOCTL_MAGIC, 8, int)
#define ILITEK_IOCTL_TP_FUNC_MODE			_IOWR(ILITEK_IOCTL_MAGIC, 9, int)

#define ILITEK_IOCTL_TP_FW_VER				_IOWR(ILITEK_IOCTL_MAGIC, 10, uint8_t*)
#define ILITEK_IOCTL_TP_PL_VER				_IOWR(ILITEK_IOCTL_MAGIC, 11, uint8_t*)
#define ILITEK_IOCTL_TP_CORE_VER			_IOWR(ILITEK_IOCTL_MAGIC, 12, uint8_t*)
#define ILITEK_IOCTL_TP_DRV_VER				_IOWR(ILITEK_IOCTL_MAGIC, 13, uint8_t*)
#define ILITEK_IOCTL_TP_CHIP_ID				_IOWR(ILITEK_IOCTL_MAGIC, 14, uint32_t*)

#define ILITEK_IOCTL_TP_NETLINK_CTRL		_IOWR(ILITEK_IOCTL_MAGIC, 15, int*)
#define ILITEK_IOCTL_TP_NETLINK_STATUS		_IOWR(ILITEK_IOCTL_MAGIC, 16, int*)

#define ILITEK_IOCTL_TP_MODE_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 17, uint8_t*)
#define ILITEK_IOCTL_TP_MODE_STATUS			_IOWR(ILITEK_IOCTL_MAGIC, 18, int*)
#define ILITEK_IOCTL_ICE_MODE_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 19, int)

unsigned char g_user_buf[USER_STR_BUFF] = { 0 };

int katoi(char *string)
{
	int result = 0;
	unsigned int digit = 0;
	int sign = 0;

	if (*string == '-') {
		sign = 1;
		string += 1;
	} else {
		sign = 0;
		if (*string == '+') {
			string += 1;
		}
	}

	for (;; string += 1) {
		digit = *string - '0';
		if (digit > 9)
			break;
		result = (10 * result) + digit;
	}

	if (sign) {
		return -result;
	}
	return result;
}
EXPORT_SYMBOL(katoi);

int str2hex(char *str)
{
	int strlen = 0;
	int result = 0;
	int intermed = 0;
	int intermedtop = 0;
	char *s = str;

	while (*s != 0x0) {
		s++;
	}

	strlen = (int)(s - str);
	s = str;
	if (*s != 0x30) {
		return -1;
	}

	s++;

	if (*s != 0x78 && *s != 0x58) {
		return -1;
	}
	s++;

	strlen = strlen - 3;
	result = 0;
	while (*s != 0x0) {
		intermed = *s & 0x0f;
		intermedtop = *s & 0xf0;
		if (intermedtop == 0x60 || intermedtop == 0x40) {
			intermed += 0x09;
		}
		intermed = intermed << (strlen << 2);
		result = result | intermed;
		strlen -= 1;
		s++;
	}
	return result;
}
EXPORT_SYMBOL(str2hex);

static ssize_t ilitek_proc_debug_switch_read(struct file *pFile, char __user *buff, size_t nCount, loff_t *pPos)
{
	int res = 0;
	int i = 0;
	if (*pPos != 0)
		return 0;
	mutex_lock(&ipd->ilitek_debug_mutex);
	memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));
	ipd->debug_data_frame = 0;
	ipd->debug_node_open = !ipd->debug_node_open;
	if (ipd->debug_node_open) {
		if (ERR_ALLOC_MEM(ipd->debug_buf)) {
			ipd->debug_buf = (unsigned char **)kmalloc(1024  * sizeof(unsigned char *), GFP_KERNEL);
			if (!ERR_ALLOC_MEM(ipd->debug_buf)) {
				for (i = 0; i < 1024; i++) {
					ipd->debug_buf[i] = (char *)kmalloc(2048 * sizeof(unsigned char), GFP_KERNEL);
					if (ERR_ALLOC_MEM(ipd->debug_buf)) {
						TPD_INFO("Failed to malloc debug_buf[%d]\n", i);
					}
				}
			}
			else {
				TPD_INFO("Failed to malloc debug_buf\n");
			}
		}
		else {
			TPD_INFO("Already malloc debug_buf\n");
		}
	}
	else {
		if (!ERR_ALLOC_MEM(ipd->debug_buf)) {
			for (i = 0; i < 1024; i++) {
				ipio_kfree((void **)&ipd->debug_buf[i]);
			}
			ipio_kfree((void **)&ipd->debug_buf);
		}
		else {
			TPD_INFO("Already free debug_buf\n");
		}
	}
	TPD_INFO(" %s debug_flag message = %x set debug_data_frame = 0\n", ipd->debug_node_open ? "Enabled" : "Disabled", ipd->debug_node_open);

	nCount = sprintf(g_user_buf, "ipd->debug_node_open : %s\n", ipd->debug_node_open ? "Enabled" : "Disabled");

	*pPos += nCount;

	res = copy_to_user(buff, g_user_buf, nCount);
	if (res < 0) {
		TPD_INFO("Failed to copy data to user space");
	}
	mutex_unlock(&ipd->ilitek_debug_mutex);
	return nCount;
}

static ssize_t ilitek_proc_debug_message_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int ret = 0;
	unsigned char buffer[512] = { 0 };

	/* check the buffer size whether it exceeds the local buffer size or not */
	if (size > 512) {
		TPD_INFO("buffer exceed 512 bytes\n");
		size = 512;
	}

	ret = copy_from_user(buffer, buff, size - 1);
	if (ret < 0) {
		TPD_INFO("copy data from user space, failed");
		return -1;
	}

	if (strcmp(buffer, "dbg_flag") == 0) {
		ipd->debug_node_open = !ipd->debug_node_open;
		TPD_INFO(" %s debug_flag message(%X).\n", ipd->debug_node_open ? "Enabled" : "Disabled",
			 ipd->debug_node_open);
	}
	return size;
}

static ssize_t ilitek_proc_debug_message_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	unsigned long p = *pPos;
	unsigned int count = size;
	int i = 0;
	int send_data_len = 0;
	size_t ret = 0;
	int data_count = 0;
	int one_data_bytes = 0;
	int need_read_data_len = 0;
	int type = 0;
	unsigned char *tmpbuf = NULL;
	unsigned char tmpbufback[128] = { 0 };

	mutex_lock(&ipd->ilitek_debug_read_mutex);

	while (ipd->debug_data_frame <= 0) {
		if (filp->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		wait_event_interruptible(ipd->inq, ipd->debug_data_frame > 0);
	}

	mutex_lock(&ipd->ilitek_debug_mutex);

	tmpbuf = vmalloc(4096);	/* buf size if even */
	if (ERR_ALLOC_MEM(tmpbuf)) {
		TPD_INFO("buffer vmalloc error\n");
		send_data_len += sprintf(tmpbufback + send_data_len, "buffer vmalloc error\n");
		ret = copy_to_user(buff, tmpbufback, send_data_len); /*ipd->debug_buf[0] */
	} else {
		if (ipd->debug_data_frame > 0) {
			if (ipd->debug_buf[0][0] == 0x5A) {
				//need_read_data_len = 43;
			} else if (ipd->debug_buf[0][0] == 0x7A) {
				type = ipd->debug_buf[0][3] & 0x0F;

				data_count = ipd->debug_buf[0][1] * ipd->debug_buf[0][2];

				if (type == 0 || type == 1 || type == 6) {
					one_data_bytes = 1;
				} else if (type == 2 || type == 3) {
					one_data_bytes = 2;
				} else if (type == 4 || type == 5) {
					one_data_bytes = 4;
				}
				//need_read_data_len = data_count * one_data_bytes + 1 + 5;
			}

			send_data_len = 0;	/* ipd->debug_buf[0][1] - 2; */
			need_read_data_len = 2040;
			if (need_read_data_len <= 0) {
				TPD_INFO("parse data err data len = %d\n", need_read_data_len);
				send_data_len +=
				    sprintf(tmpbuf + send_data_len, "parse data err data len = %d\n",
					    need_read_data_len);
			} else {
				for (i = 0; i < need_read_data_len; i++) {
					send_data_len += sprintf(tmpbuf + send_data_len, "%02X", ipd->debug_buf[0][i]);
					if (send_data_len >= 4096) {
						TPD_INFO("send_data_len = %d set 4096 i = %d\n", send_data_len, i);
						send_data_len = 4096;
						break;
					}
				}
			}
			send_data_len += sprintf(tmpbuf + send_data_len, "\n\n");

			if (p == 5 || size == 4096 || size == 2048) {
				ipd->debug_data_frame--;
				if (ipd->debug_data_frame < 0) {
					ipd->debug_data_frame = 0;
				}

				for (i = 1; i <= ipd->debug_data_frame; i++) {
					memcpy(ipd->debug_buf[i - 1], ipd->debug_buf[i], 2048);
				}
			}
		} else {
			TPD_INFO("no data send\n");
			send_data_len += sprintf(tmpbuf + send_data_len, "no data send\n");
		}

		/* Preparing to send data to user */
		if (size == 4096)
			ret = copy_to_user(buff, tmpbuf, send_data_len);
		else
			ret = copy_to_user(buff, tmpbuf + p, send_data_len - p);

		if (ret) {
			TPD_INFO("copy_to_user err\n");
			ret = -EFAULT;
		} else {
			*pPos += count;
			ret = count;
			TPD_DEBUG("Read %d bytes(s) from %ld\n", count, p);
		}
	}
	/* TPD_INFO("send_data_len = %d\n", send_data_len); */
	if (send_data_len <= 0 || send_data_len > 4096) {
		TPD_INFO("send_data_len = %d set 2048\n", send_data_len);
		send_data_len = 4096;
	}
	if (tmpbuf != NULL) {
		vfree(tmpbuf);
		tmpbuf = NULL;
	}

	mutex_unlock(&ipd->ilitek_debug_mutex);
	mutex_unlock(&ipd->ilitek_debug_read_mutex);
	return send_data_len;
}

static ssize_t ilitek_proc_mp_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int ret = 0;
	uint32_t len = 0;
	//uint8_t test_cmd[2] = { 0 };

	if (*pPos != 0)
		return 0;

	ret = ilitek_mp_test((struct seq_file *)NULL);
	if (ret) {
		TPD_INFO("mp test fail\n");
	}
	*pPos = len;
	TPD_INFO("MP Test DONE\n");
	return len;
}

static ssize_t ilitek_proc_mp_test_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int i = 0;
	int res = 0;
	int count = 0;
	char cmd[64] = {0};
	char str[512] = {0};
	char *token = NULL;
	char *cur = NULL;
	uint8_t *va = NULL;
	uint8_t test_cmd[4] = {0};

	if (buff != NULL) {
		res = copy_from_user(cmd, buff, size - 1);
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	if (size > 64) {
		TPD_INFO("The size of string is too long\n");
		return size;
	}

	token = cur = cmd;

	va = kcalloc(64, sizeof(uint8_t), GFP_KERNEL);

	while ((token = strsep(&cur, ",")) != NULL) {
		va[count] = katoi(token);
		TPD_INFO("data[%d] = %x\n", count, va[count]);
		count++;
	}

	TPD_INFO("cmd = %s\n", cmd);

	/* Init MP structure */
	if(core_mp_init() < 0) {
		TPD_INFO("Failed to init mp\n");
		return size;
	}

	/* Switch to Test mode */
	test_cmd[0] = protocol->test_mode;
	core_config_mode_control(test_cmd);

	ilitek_platform_disable_irq();

	for (i = 0; i < core_mp->mp_items; i++) {
		if (strcmp(cmd, tItems[i].name) == 0) {
			strcpy(str, tItems[i].desp);
			tItems[i].run = 1;
			tItems[i].max = va[1];
			tItems[i].min = va[2];
			tItems[i].frame_count = va[3];
			break;
		}
	}

	core_mp_run_test(str, false);

	core_mp_show_result();

	core_mp_test_free();

#ifndef HOST_DOWNLOAD
	/* Code reset */
	core_config_ice_mode_enable();

	/* Disable watch dog */
	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
	}

	core_config_ic_reset();
#endif

	/* Switch to Demo mode it prevents if fw fails to be switched */
	test_cmd[0] = protocol->demo_mode;
	core_config_mode_control(test_cmd);

#ifdef HOST_DOWNLOAD
	ilitek_platform_tp_hw_reset(true);
#endif

	ilitek_platform_enable_irq();

	TPD_INFO("MP Test DONE\n");
	ipio_kfree((void **)&va);
	return size;
}

static ssize_t ilitek_proc_mp_black_screen_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int ret = 0;
	uint32_t len = 0;
	char buffer[256] = { 0 };

	if (*pPos != 0)
		return 0;

	ret = ilitek_mp_black_screen_test(buffer);
	if (ret) {
		TPD_INFO("mp test fail\n");
	}
	*pPos = len;
	TPD_INFO("MP black screen Test DONE\n");
	return len;
}

static ssize_t ilitek_proc_debug_level_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

	len = sprintf(g_user_buf, "%d", ipio_debug_level);

	TPD_INFO("Current DEBUG Level = %d\n", ipio_debug_level);
	TPD_INFO("You can set one of levels for debug as below:\n");
	TPD_INFO("DEBUG_NONE = %d\n", DEBUG_NONE);
	TPD_INFO("DEBUG_IRQ = %d\n", DEBUG_IRQ);
	TPD_INFO("DEBUG_FINGER_REPORT = %d\n", DEBUG_FINGER_REPORT);
	TPD_INFO("DEBUG_FIRMWARE = %d\n", DEBUG_FIRMWARE);
	TPD_INFO("DEBUG_CONFIG = %d\n", DEBUG_CONFIG);
	TPD_INFO("DEBUG_I2C = %d\n", DEBUG_I2C);
	TPD_INFO("DEBUG_BATTERY = %d\n", DEBUG_BATTERY);
	TPD_INFO("DEBUG_MP_TEST = %d\n", DEBUG_MP_TEST);
	TPD_INFO("DEBUG_IOCTL = %d\n", DEBUG_IOCTL);
	TPD_INFO("DEBUG_NETLINK = %d\n", DEBUG_NETLINK);
	TPD_INFO("DEBUG_ALL = %d\n", DEBUG_ALL);

	res = copy_to_user((uint32_t *) buff, &ipio_debug_level, len);
	if (res < 0) {
		TPD_INFO("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_debug_level_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = { 0 };

	if (buff != NULL) {
		res = copy_from_user(cmd, buff, size - 1);
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	ipio_debug_level = katoi(cmd);

	TPD_INFO("ipio_debug_level = %d\n", ipio_debug_level);

	return size;
}

static ssize_t ilitek_proc_gesture_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

	len = sprintf(g_user_buf, "%d", core_config->isEnableGesture);

	TPD_INFO("isEnableGesture = %d\n", core_config->isEnableGesture);

	res = copy_to_user((uint32_t *) buff, &core_config->isEnableGesture, len);
	if (res < 0) {
		TPD_INFO("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_gesture_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = { 0 };

	if (buff != NULL) {
		res = copy_from_user(cmd, buff, size - 1);
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	if (strcmp(cmd, "on") == 0) {
		TPD_INFO("enable gesture mode\n");
		core_config->isEnableGesture = true;
	} else if (strcmp(cmd, "off") == 0) {
		TPD_INFO("disable gesture mode\n");
		core_config->isEnableGesture = false;
	} else if (strcmp(cmd, "info") == 0) {
		TPD_INFO("gesture info mode\n");
		core_gesture->mode = GESTURE_INFO_MPDE;
	} else if (strcmp(cmd, "normal") == 0) {
		TPD_INFO("gesture normal mode\n");
		core_gesture->mode = GESTURE_NORMAL_MODE;
	} else
		TPD_INFO("Unknown command\n");

	return size;
}

static ssize_t ilitek_proc_check_battery_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

	len = sprintf(g_user_buf, "%d", ipd->isEnablePollCheckPower);

	TPD_INFO("isEnablePollCheckPower = %d\n", ipd->isEnablePollCheckPower);

	res = copy_to_user((uint32_t *) buff, &ipd->isEnablePollCheckPower, len);
	if (res < 0) {
		TPD_INFO("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_check_battery_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = { 0 };

	if (buff != NULL) {
		res = copy_from_user(cmd, buff, size - 1);
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);

#ifdef ENABLE_BATTERY_CHECK
	if (strcmp(cmd, "on") == 0) {
		TPD_INFO("Start the thread of check power status\n");
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
		ipd->isEnablePollCheckPower = true;
	} else if (strcmp(cmd, "off") == 0) {
		TPD_INFO("Cancel the thread of check power status\n");
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		ipd->isEnablePollCheckPower = false;
	} else
		TPD_INFO("Unknown command\n");
#else
	TPD_INFO("You need to enable its MACRO before operate it.\n");
#endif

	return size;
}

static ssize_t ilitek_proc_fw_process_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	/*
	 * If file position is non-zero,  we assume the string has been read
	 * and indicates that there is no more data to be read.
	 */
	if (*pPos != 0)
		return 0;

	memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

	len = sprintf(g_user_buf, "%02d", core_firmware->update_status);

	TPD_INFO("update status = %d\n", core_firmware->update_status);

	res = copy_to_user((uint32_t *) buff, &core_firmware->update_status, len);
	if (res < 0) {
		TPD_INFO("Failed to copy data to user space");
	}

	*pPos = len;

	return len;
}

/*
 * To avoid the restriction of selinux, we assigned a fixed path where locates firmware file,
 * reading (cat) this node to notify driver running the upgrade process from user space.
 */
static ssize_t ilitek_proc_fw_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	TPD_INFO("Preparing to upgarde firmware\n");

	if (*pPos != 0)
		return 0;
    ipd->apk_upgrade = 1;
	ilitek_platform_disable_irq();

	if (ipd->isEnablePollCheckPower)
		cancel_delayed_work_sync(&ipd->check_power_status_work);
#ifdef HOST_DOWNLOAD
	res = ilitek_platform_tp_hw_reset(true);
#else
	res = core_firmware_upgrade(UPDATE_FW_PATH, false);
#endif
	ilitek_platform_enable_irq();

	if (ipd->isEnablePollCheckPower)
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);

	if (res < 0) {
		core_firmware->update_status = res;
		TPD_INFO("Failed to upgrade firwmare\n");
	} else {
		core_firmware->update_status = 100;
		TPD_INFO("Succeed to upgrade firmware\n");
	}
    ipd->apk_upgrade = 0;
	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_iram_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	TPD_INFO("Preparing to upgarde firmware by IRAM\n");

	if (*pPos != 0)
		return 0;

	ilitek_platform_disable_irq();

	res = core_firmware_upgrade(UPDATE_FW_PATH, true);

	ilitek_platform_enable_irq();

	if (res < 0) {
		/* return the status to user space even if any error occurs. */
		core_firmware->update_status = res;
		TPD_INFO("Failed to upgrade firwmare by IRAM, res = %d\n", res);
	} else {
		TPD_INFO("Succeed to upgrade firmware by IRAM\n");
	}

	*pPos = len;

	return len;
}

/* for debug */
static ssize_t ilitek_proc_ioctl_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;
	uint8_t cmd[2] = { 0 };

	if (*pPos != 0)
		return 0;

	if (size < 4095 && size > 0) {
		res = copy_from_user(cmd, buff, ((size - 1) > 2) ? 2 : (size - 1));
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	TPD_INFO("size = %d, cmd = %d", (int)size, cmd[0]);

	/* test */
	if (cmd[0] == 0x1) {
		TPD_INFO("HW Reset\n");
		ilitek_platform_tp_hw_reset(true);
	} else if (cmd[0] == 0x02) {
		TPD_INFO("Disable IRQ\n");
		ilitek_platform_disable_irq();
	} else if (cmd[0] == 0x03) {
		TPD_INFO("Enable IRQ\n");
		ilitek_platform_enable_irq();
	} else if (cmd[0] == 0x04) {
		TPD_INFO("Get Chip id\n");
		core_config_get_chip_id();
	}

	*pPos = len;

	return len;
}

/* for debug */
static ssize_t ilitek_proc_ioctl_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	int count = 0;
	int i = 0;
	int w_len = 0;
	int r_len = 0;
	int delay = 0;
	char cmd[512] = { 0 };
	char *token = NULL;
	char *cur = NULL;
	uint8_t temp[256] = { 0 };
	uint8_t *data = NULL;
	if (buff != NULL) {
		res = copy_from_user(cmd, buff, size - 1);
		if (res < 0) {
			TPD_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	token = cur = cmd;

	data = kmalloc(512 * sizeof(uint8_t), GFP_KERNEL);
	memset(data, 0, 512);

	while ((token = strsep(&cur, ",")) != NULL) {
		data[count] = str2hex(token);
		TPD_INFO("data[%d] = %x\n",count, data[count]);
		count++;
	}

	TPD_INFO("cmd = %s\n", cmd);

	if (strcmp(cmd, "reset") == 0) {
		TPD_INFO("HW Reset\n");
		ilitek_platform_tp_hw_reset(true);
	} else if (strcmp(cmd, "disirq") == 0) {
		TPD_INFO("Disable IRQ\n");
		ilitek_platform_disable_irq();
	} else if (strcmp(cmd, "enairq") == 0) {
		TPD_INFO("Enable IRQ\n");
		ilitek_platform_enable_irq();
	} else if (strcmp(cmd, "getchip") == 0) {
		TPD_INFO("Get Chip id\n");
		core_config_get_chip_id();
	} else if (strcmp(cmd, "dispcc") == 0) {
		TPD_INFO("disable phone cover\n");
		core_config_phone_cover_ctrl(false);
	} else if (strcmp(cmd, "enapcc") == 0) {
		TPD_INFO("enable phone cover\n");
		core_config_phone_cover_ctrl(true);
	} else if (strcmp(cmd, "disfsc") == 0) {
		TPD_INFO("disable finger sense\n");
		core_config_finger_sense_ctrl(false);
	} else if (strcmp(cmd, "enafsc") == 0) {
		TPD_INFO("enable finger sense\n");
		core_config_finger_sense_ctrl(true);
	} else if (strcmp(cmd, "disprox") == 0) {
		TPD_INFO("disable proximity\n");
		core_config_proximity_ctrl(false);
	} else if (strcmp(cmd, "enaprox") == 0) {
		TPD_INFO("enable proximity\n");
		core_config_proximity_ctrl(true);
	} else if (strcmp(cmd, "disglove") == 0) {
		TPD_INFO("disable glove function\n");
		core_config_glove_ctrl(false, false);
	} else if (strcmp(cmd, "enaglove") == 0) {
		TPD_INFO("enable glove function\n");
		core_config_glove_ctrl(true, false);
	} else if (strcmp(cmd, "glovesl") == 0) {
		TPD_INFO("set glove as seamless\n");
		core_config_glove_ctrl(true, true);
	} else if (strcmp(cmd, "enastylus") == 0) {
		TPD_INFO("enable stylus\n");
		core_config_stylus_ctrl(true, false);
	} else if (strcmp(cmd, "disstylus") == 0) {
		TPD_INFO("disable stylus\n");
		core_config_stylus_ctrl(false, false);
	} else if (strcmp(cmd, "stylussl") == 0) {
		TPD_INFO("set stylus as seamless\n");
		core_config_stylus_ctrl(true, true);
	} else if (strcmp(cmd, "tpscan_ab") == 0) {
		TPD_INFO("set TP scan as mode AB\n");
		core_config_tp_scan_mode(true);
	} else if (strcmp(cmd, "tpscan_b") == 0) {
		TPD_INFO("set TP scan as mode B\n");
		core_config_tp_scan_mode(false);
	} else if (strcmp(cmd, "phone_cover") == 0) {
		TPD_INFO("set size of phone conver window\n");
		core_config_set_phone_cover(data);
	} else if (strcmp(cmd, "debugmode") == 0) {
		TPD_INFO("debug mode test enter\n");
		temp[0] = protocol->debug_mode;
		core_config_mode_control(temp);		
	} else if (strcmp(cmd, "baseline") == 0) {
		TPD_INFO("test baseline raw\n");
		temp[0] = protocol->debug_mode;
		core_config_mode_control(temp);
		ilitek_platform_disable_irq();
		temp[0] = 0xFA;
		temp[1] = 0x08;
		core_write(core_config->slave_i2c_addr, temp, 2);
		ilitek_platform_enable_irq();
	} else if (strcmp(cmd, "delac_on") == 0) {
		TPD_INFO("test get delac\n");
		temp[0] = protocol->debug_mode;
		core_config_mode_control(temp);
		ilitek_platform_disable_irq();
		temp[0] = 0xFA;
		temp[1] = 0x03;
		core_write(core_config->slave_i2c_addr, temp, 2);
		ilitek_platform_enable_irq();
	} else if (strcmp(cmd, "delac_off") == 0) {
		TPD_INFO("test get delac\n");
		temp[0] = protocol->demo_mode;
		core_config_mode_control(temp);
	}else if (strcmp(cmd, "test") == 0) {
		TPD_INFO("test test_reset test 1\n");
		gpio_direction_output(ipd->reset_gpio, 1);
		mdelay(1);
		gpio_set_value(ipd->reset_gpio, 0);
		mdelay(1);
		gpio_set_value(ipd->reset_gpio, 1);
		mdelay(10);
	}
	else if (strcmp(cmd, "gt") == 0) {
		TPD_INFO("test Gesture test\n");
		//core_load_gesture_code();
	}
	else if (strcmp(cmd, "getregdata") == 0) {
		TPD_INFO("test getregdata\n");
		res = core_config_ice_mode_enable();
		if (res < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		}
		//mdelay(20);
		
		core_get_tp_register();
		core_get_ddi_register();
		core_config_ice_mode_disable();
	}
	else if (strcmp(cmd, "gettpregdata") == 0) {
		TPD_INFO("test gettpregdata set reg is 0x%X\n",\
			(data[1] << 24 | data[2] << 16 | data[3] << 8 | data[4]));
		core_config_get_reg_data(data[1] << 24 | data[2] << 16 | data[3] << 8 | data[4]);
	}
	else if (strcmp(cmd, "getddiregdata") == 0) {
		TPD_INFO("test getregdata\n");
		res = core_config_ice_mode_enable();
		if (res < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		}
		core_get_ddi_register();
		core_config_ice_mode_disable();
	}
	else if (strcmp(cmd, "getoneddiregdata") == 0) {
		TPD_INFO("test getoneddiregdata\n");
		res = core_config_ice_mode_enable();
		if (res < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		}
		core_get_ddi_register_onlyone(data[1], data[2]);
		core_config_ice_mode_disable();
	}
	else if (strcmp(cmd, "setoneddiregdata") == 0) {
		TPD_INFO("test getoneddiregdata\n");
		res = core_config_ice_mode_enable();
		if (res < 0) {
			TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
		}
		core_set_ddi_register_onlyone(data[1], data[2], data[3]);
		core_config_ice_mode_disable();
	}
	else if (strcmp(cmd, "gt1") == 0) {
		TPD_INFO("test Gesture test 1\n");
		temp[0] = 0x01;
		temp[1] = 0x01;
		temp[2] = 0x00;
		w_len = 3;
		core_write(core_config->slave_i2c_addr, temp, w_len);
		if (core_config_check_cdc_busy(50) < 0)
			TPD_INFO("Check busy is timout !\n");
	} else if (strcmp(cmd, "gt2") == 0) {
		temp[0] = 0x01;
		temp[1] = 0x0A;
		temp[2] = 0x01;
		w_len = 3;
		core_write(core_config->slave_i2c_addr, temp, w_len);
		TPD_INFO("test Gesture test\n");	
	} else if (strcmp(cmd, "i2c_w") == 0) {
		w_len = data[1];
		TPD_INFO("w_len = %d\n", w_len);

		for (i = 0; i < w_len; i++) {
			temp[i] = data[2 + i];
			TPD_INFO("i2c[%d] = %x\n", i, temp[i]);
		}

		core_write(core_config->slave_i2c_addr, temp, w_len);
	} else if (strcmp(cmd, "i2c_r") == 0) {
		r_len = data[1];
		TPD_INFO("r_len = %d\n", r_len);

		core_read(core_config->slave_i2c_addr, &temp[0], r_len);

		for (i = 0; i < r_len; i++)
			TPD_INFO("temp[%d] = %x\n", i, temp[i]);
	} else if (strcmp(cmd, "i2c_w_r") == 0) {
		w_len = data[1];
		r_len = data[2];
		delay = data[3];
		TPD_INFO("w_len = %d, r_len = %d, delay = %d\n", w_len, r_len, delay);

		for (i = 0; i < w_len; i++) {
			temp[i] = data[4 + i];
			TPD_INFO("temp[%d] = %x\n", i, temp[i]);
		}

		core_write(core_config->slave_i2c_addr, temp, w_len);

		memset(temp, 0, sizeof(temp));
		mdelay(delay);

		core_read(core_config->slave_i2c_addr, &temp[0], r_len);

		for (i = 0; i < r_len; i++)
			TPD_INFO("temp[%d] = %x\n", i, temp[i]);
	} else {
		TPD_INFO("Unknown command\n");
	}

	ipio_kfree((void **)&data);
	return size;
}


static long ilitek_proc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0;
	int length = 0;
	uint8_t szBuf[512] = { 0 };
	static uint16_t i2c_rw_length = 0;
	uint32_t id_to_user = 0x0;
	char dbg[10] = { 0 };

	TPD_DEBUG("cmd = %d\n", _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC) {
		TPD_INFO("The Magic number doesn't match\n");
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR) {
		TPD_INFO("The number of ioctl doesn't match\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		res = copy_from_user(szBuf, (uint8_t *) arg, i2c_rw_length);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			res = core_write(core_config->slave_i2c_addr, &szBuf[0], i2c_rw_length);
			if (res < 0) {
				TPD_INFO("Failed to write data via i2c\n");
			}
		}
		break;

	case ILITEK_IOCTL_I2C_READ_DATA:
		res = core_read(core_config->slave_i2c_addr, szBuf, i2c_rw_length);
		if (res < 0) {
			TPD_INFO("Failed to read data via i2c\n");
		} else {
			res = copy_to_user((uint8_t *) arg, szBuf, i2c_rw_length);
			if (res < 0) {
				TPD_INFO("Failed to copy data to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_I2C_SET_WRITE_LENGTH:
	case ILITEK_IOCTL_I2C_SET_READ_LENGTH:
		i2c_rw_length = arg;
		break;

	case ILITEK_IOCTL_TP_HW_RESET:
		ilitek_platform_tp_hw_reset(true);
		break;

	case ILITEK_IOCTL_TP_POWER_SWITCH:
		TPD_INFO("Not implemented yet\n");
		break;

	case ILITEK_IOCTL_TP_REPORT_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *) arg, 1);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			if (szBuf[0]) {
				core_fr->isEnableFR = true;
				TPD_DEBUG("Function of finger report was enabled\n");
			} else {
				core_fr->isEnableFR = false;
				TPD_DEBUG("Function of finger report was disabled\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_IRQ_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *) arg, 1);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			if (szBuf[0]) {
				ilitek_platform_enable_irq();
			} else {
				ilitek_platform_disable_irq();
			}
		}
		break;

	case ILITEK_IOCTL_ICE_MODE_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *) arg, 1);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			if (szBuf[0]) {
				core_config->icemodeenable = true;
			} else {
				core_config->icemodeenable = false;
			}
		}
		TPD_INFO("core_config->icemodeenable = %d\n", core_config->icemodeenable);
		break;

	case ILITEK_IOCTL_TP_DEBUG_LEVEL:
		res = copy_from_user(dbg, (uint32_t *) arg, sizeof(uint32_t));
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			ipio_debug_level = katoi(dbg);
			TPD_INFO("ipio_debug_level = %d", ipio_debug_level);
		}
		break;

	case ILITEK_IOCTL_TP_FUNC_MODE:
		TPD_INFO("\n");
		res = copy_from_user(szBuf, (uint8_t *) arg, 3);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			core_write(core_config->slave_i2c_addr, &szBuf[0], 3);
		}
		TPD_INFO("\n");
		break;

	case ILITEK_IOCTL_TP_FW_VER:
		TPD_INFO("\n");
		res = core_config_get_fw_ver();
		if (res < 0) {
			TPD_INFO("Failed to get firmware version\n");
		} else {
			res = copy_to_user((uint8_t *) arg, core_config->firmware_ver, protocol->fw_ver_len);
			if (res < 0) {
				TPD_INFO("Failed to copy firmware version to user space\n");
			}
		}
		TPD_INFO("\n");
		break;

	case ILITEK_IOCTL_TP_PL_VER:
		TPD_INFO("\n");
		res = core_config_get_protocol_ver();
		if (res < 0) {
			TPD_INFO("Failed to get protocol version\n");
		} else {
			res = copy_to_user((uint8_t *) arg, core_config->protocol_ver, protocol->pro_ver_len);
			if (res < 0) {
				TPD_INFO("Failed to copy protocol version to user space\n");
			}
		}
		TPD_INFO("\n");
		break;

	case ILITEK_IOCTL_TP_CORE_VER:
		TPD_INFO("\n");
		res = core_config_get_core_ver();
		if (res < 0) {
			TPD_INFO("Failed to get core version\n");
		} else {
			res = copy_to_user((uint8_t *) arg, core_config->core_ver, protocol->core_ver_len);
			if (res < 0) {
				TPD_INFO("Failed to copy core version to user space\n");
			}
		}
		TPD_INFO("\n");
		break;

	case ILITEK_IOCTL_TP_DRV_VER:
		length = sprintf(szBuf, "%s", DRIVER_VERSION);
		if (!length) {
			TPD_INFO("Failed to convert driver version from definiation\n");
		} else {
			res = copy_to_user((uint8_t *) arg, szBuf, length);
			if (res < 0) {
				TPD_INFO("Failed to copy driver ver to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_CHIP_ID:
		res = core_config_get_chip_id();
		if (res < 0) {
			TPD_INFO("Failed to get chip id\n");
		} else {
			id_to_user = core_config->chip_id << 16 | core_config->chip_type;

			res = copy_to_user((uint32_t *) arg, &id_to_user, sizeof(uint32_t));
			if (res < 0) {
				TPD_INFO("Failed to copy chip id to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_CTRL:
		res = copy_from_user(szBuf, (uint8_t *) arg, 1);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			if (szBuf[0]) {
				core_fr->isEnableNetlink = true;
				TPD_DEBUG("Netlink has been enabled\n");
			} else {
				core_fr->isEnableNetlink = false;
				TPD_DEBUG("Netlink has been disabled\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_STATUS:
		TPD_DEBUG("Netlink is enabled : %d\n", core_fr->isEnableNetlink);
		res = copy_to_user((int *)arg, &core_fr->isEnableNetlink, sizeof(int));
		if (res < 0) {
			TPD_INFO("Failed to copy chip id to user space\n");
		}
		break;

	case ILITEK_IOCTL_TP_MODE_CTRL:
		res = copy_from_user(szBuf, (uint8_t *) arg, 4);
		if (res < 0) {
			TPD_INFO("Failed to copy data from user space\n");
		} else {
			core_config_mode_control(szBuf);
		}
		break;

	case ILITEK_IOCTL_TP_MODE_STATUS:
		TPD_DEBUG("Current firmware mode : %d", core_fr->actual_fw_mode);
		res = copy_to_user((int *)arg, &core_fr->actual_fw_mode, sizeof(int));
		if (res < 0) {
			TPD_INFO("Failed to copy chip id to user space\n");
		}
		break;

	default:
		res = -ENOTTY;
		break;
	}

	return res;
}

struct proc_dir_entry *proc_dir_ilitek;
struct proc_dir_entry *proc_ioctl;
struct proc_dir_entry *proc_fw_process;
struct proc_dir_entry *proc_fw_upgrade;
struct proc_dir_entry *proc_iram_upgrade;
struct proc_dir_entry *proc_gesture;
struct proc_dir_entry *proc_debug_level;
struct proc_dir_entry *proc_mp_test;
struct proc_dir_entry *proc_debug_message;
struct proc_dir_entry *proc_debug_message_switch;

struct file_operations proc_ioctl_fops = {
	.unlocked_ioctl = ilitek_proc_ioctl,
	.read = ilitek_proc_ioctl_read,
	.write = ilitek_proc_ioctl_write,
};

struct file_operations proc_fw_process_fops = {
	.read = ilitek_proc_fw_process_read,
};

struct file_operations proc_fw_upgrade_fops = {
	.read = ilitek_proc_fw_upgrade_read,
};

struct file_operations proc_iram_upgrade_fops = {
	.read = ilitek_proc_iram_upgrade_read,
};

struct file_operations proc_gesture_fops = {
	.write = ilitek_proc_gesture_write,
	.read = ilitek_proc_gesture_read,
};

struct file_operations proc_check_battery_fops = {
	.write = ilitek_proc_check_battery_write,
	.read = ilitek_proc_check_battery_read,
};

struct file_operations proc_debug_level_fops = {
	.write = ilitek_proc_debug_level_write,
	.read = ilitek_proc_debug_level_read,
};

struct file_operations proc_mp_test_fops = {
	.write = ilitek_proc_mp_test_write,
	.read = ilitek_proc_mp_test_read,
};

struct file_operations proc_mp_black_screen_test_fops = {
	.read = ilitek_proc_mp_black_screen_test_read,
};

struct file_operations proc_debug_message_fops = {
	.write = ilitek_proc_debug_message_write,
	.read = ilitek_proc_debug_message_read,
};

struct file_operations proc_debug_message_switch_fops = {
	.read = ilitek_proc_debug_switch_read,
};

/**
 * This struct lists all file nodes will be created under /proc filesystem.
 *
 * Before creating a node that you want, declaring its file_operations structure
 * is necessary. After that, puts the structure into proc_table, defines its
 * node's name in the same row, and the init function lterates the table and
 * creates all nodes under /proc.
 *
 */
typedef struct {
	char *name;
	struct proc_dir_entry *node;
	struct file_operations *fops;
	bool isCreated;
} proc_node_t;

proc_node_t proc_table[] = {
	{"ioctl", NULL, &proc_ioctl_fops, false},
	{"fw_process", NULL, &proc_fw_process_fops, false},
	{"fw_upgrade", NULL, &proc_fw_upgrade_fops, false},
	{"iram_upgrade", NULL, &proc_iram_upgrade_fops, false},
	{"gesture", NULL, &proc_gesture_fops, false},
	{"check_battery", NULL, &proc_check_battery_fops, false},
	{"debug_level", NULL, &proc_debug_level_fops, false},
	{"mp_test", NULL, &proc_mp_test_fops, false},
	{"mp_black_screen_test", NULL, &proc_mp_black_screen_test_fops, false},
	{"debug_message", NULL, &proc_debug_message_fops, false},
	{"debug_message_switch", NULL, &proc_debug_message_switch_fops, false},
};

static int tp_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    if (!ts) {
        return 0;
    }
	TPD_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
	if (s->size <= (4096 * 2)) {
		s->count = s->size;
		return 0;
	}

	ilitek_mp_test(s);

    operate_mode_switch(ts);

    return 0;
}

static int baseline_autotest_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_auto_test_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_auto_test_proc_fops = {
    .owner = THIS_MODULE,
    .open  = baseline_autotest_open,
    .read  = seq_read,
    .release = single_release,
};


//proc/touchpanel/baseline_test
int ilitek_create_proc_for_oplus(struct touchpanel_data *ts)
{
    int ret = 0;
    // touchpanel_auto_test interface
    struct proc_dir_entry *prEntry_tmp = NULL;
    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp, &tp_auto_test_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("Couldn't create proc entry\n");
    }
    return ret;
}

#define NETLINK_USER 21
struct sock *_gNetLinkSkb;
struct nlmsghdr *_gNetLinkHead;
struct sk_buff *_gSkbOut;
int _gPID;

void netlink_reply_msg(void *raw, int size)
{
	int res = 0;
	int msg_size = size;
	uint8_t *data = (uint8_t *) raw;

	TPD_DEBUG("The size of data being sent to user = %d\n", msg_size);
	TPD_DEBUG("pid = %d\n", _gPID);
	TPD_DEBUG("Netlink is enable = %d\n", core_fr->isEnableNetlink);

	if (core_fr->isEnableNetlink) {
		_gSkbOut = nlmsg_new(msg_size, 0);

		if (!_gSkbOut) {
			TPD_INFO("Failed to allocate new skb\n");
			return;
		}

		_gNetLinkHead = nlmsg_put(_gSkbOut, 0, 0, NLMSG_DONE, msg_size, 0);
		NETLINK_CB(_gSkbOut).dst_group = 0;	/* not in mcast group */

		/* strncpy(NLMSG_DATA(_gNetLinkHead), data, msg_size); */
		memcpy(nlmsg_data(_gNetLinkHead), data, msg_size);

		res = nlmsg_unicast(_gNetLinkSkb, _gSkbOut, _gPID);
		if (res < 0)
			TPD_INFO("Failed to send data back to user\n");
	}
}
EXPORT_SYMBOL(netlink_reply_msg);

static void netlink_recv_msg(struct sk_buff *skb)
{
	_gPID = 0;

	TPD_DEBUG("Netlink is enable = %d\n", core_fr->isEnableNetlink);

	_gNetLinkHead = (struct nlmsghdr *)skb->data;

	TPD_DEBUG("Received a request from client: %s, %d\n",
	    (char *)NLMSG_DATA(_gNetLinkHead), (int)strlen((char *)NLMSG_DATA(_gNetLinkHead)));

	/* pid of sending process */
	_gPID = _gNetLinkHead->nlmsg_pid;

	TPD_DEBUG("the pid of sending process = %d\n", _gPID);

	/* TODO: may do something if there's not receiving msg from user. */
	if (_gPID != 0) {
		TPD_INFO("The channel of Netlink has been established successfully !\n");
		core_fr->isEnableNetlink = true;
	} else {
		TPD_INFO("Failed to establish the channel between kernel and user space\n");
		core_fr->isEnableNetlink = false;
	}
}

static int netlink_init(void)
{
	int res = 0;

#if KERNEL_VERSION(3, 4, 0) > LINUX_VERSION_CODE
	_gNetLinkSkb = netlink_kernel_create(&init_net, NETLINK_USER, netlink_recv_msg, NULL, THIS_MODULE);
#else
	struct netlink_kernel_cfg cfg = {
		.input = netlink_recv_msg,
	};

	_gNetLinkSkb = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
#endif

	TPD_INFO("Initialise Netlink and create its socket\n");

	if (!_gNetLinkSkb) {
		TPD_INFO("Failed to create nelink socket\n");
		res = -EFAULT;
	}

	return res;
}

int ilitek_proc_init(void)
{
	int i = 0;
	int res = 0;

	proc_dir_ilitek = proc_mkdir("ilitek", NULL);

	for (; i < ARRAY_SIZE(proc_table); i++) {
		proc_table[i].node = proc_create(proc_table[i].name, 0666, proc_dir_ilitek, proc_table[i].fops);

		if (proc_table[i].node == NULL) {
			proc_table[i].isCreated = false;
			TPD_INFO("Failed to create %s under /proc\n", proc_table[i].name);
			res = -ENODEV;
		} else {
			proc_table[i].isCreated = true;
			TPD_INFO("Succeed to create %s under /proc\n", proc_table[i].name);
		}
	}

	netlink_init();

	return res;
}
EXPORT_SYMBOL(ilitek_proc_init);

void ilitek_proc_remove(void)
{
	int i = 0;

	for (; i < ARRAY_SIZE(proc_table); i++) {
		if (proc_table[i].isCreated == true) {
			TPD_INFO("Removed %s under /proc\n", proc_table[i].name);
			remove_proc_entry(proc_table[i].name, proc_dir_ilitek);
		}
	}

	remove_proc_entry("ilitek", NULL);
	netlink_kernel_release(_gNetLinkSkb);
}
EXPORT_SYMBOL(ilitek_proc_remove);
