// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ilitek.h"

#define USER_STR_BUFF        PAGE_SIZE
#define IOCTL_I2C_BUFF        PAGE_SIZE
#define ILITEK_IOCTL_MAGIC    100
#define ILITEK_IOCTL_MAXNR    21

#define ILITEK_IOCTL_I2C_WRITE_DATA        _IOWR(ILITEK_IOCTL_MAGIC, 0, u8*)
#define ILITEK_IOCTL_I2C_SET_WRITE_LENGTH    _IOWR(ILITEK_IOCTL_MAGIC, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA        _IOWR(ILITEK_IOCTL_MAGIC, 2, u8*)
#define ILITEK_IOCTL_I2C_SET_READ_LENGTH    _IOWR(ILITEK_IOCTL_MAGIC, 3, int)

#define ILITEK_IOCTL_TP_HW_RESET        _IOWR(ILITEK_IOCTL_MAGIC, 4, int)
#define ILITEK_IOCTL_TP_POWER_SWITCH        _IOWR(ILITEK_IOCTL_MAGIC, 5, int)
#define ILITEK_IOCTL_TP_REPORT_SWITCH        _IOWR(ILITEK_IOCTL_MAGIC, 6, int)
#define ILITEK_IOCTL_TP_IRQ_SWITCH        _IOWR(ILITEK_IOCTL_MAGIC, 7, int)

#define ILITEK_IOCTL_TP_DEBUG_LEVEL        _IOWR(ILITEK_IOCTL_MAGIC, 8, int)
#define ILITEK_IOCTL_TP_FUNC_MODE        _IOWR(ILITEK_IOCTL_MAGIC, 9, int)

#define ILITEK_IOCTL_TP_FW_VER            _IOWR(ILITEK_IOCTL_MAGIC, 10, u8*)
#define ILITEK_IOCTL_TP_PL_VER            _IOWR(ILITEK_IOCTL_MAGIC, 11, u8*)
#define ILITEK_IOCTL_TP_CORE_VER        _IOWR(ILITEK_IOCTL_MAGIC, 12, u8*)
#define ILITEK_IOCTL_TP_DRV_VER            _IOWR(ILITEK_IOCTL_MAGIC, 13, u8*)
#define ILITEK_IOCTL_TP_CHIP_ID            _IOWR(ILITEK_IOCTL_MAGIC, 14, u32*)

#define ILITEK_IOCTL_TP_NETLINK_CTRL        _IOWR(ILITEK_IOCTL_MAGIC, 15, int*)
#define ILITEK_IOCTL_TP_NETLINK_STATUS        _IOWR(ILITEK_IOCTL_MAGIC, 16, int*)

#define ILITEK_IOCTL_TP_MODE_CTRL        _IOWR(ILITEK_IOCTL_MAGIC, 17, u8*)
#define ILITEK_IOCTL_TP_MODE_STATUS        _IOWR(ILITEK_IOCTL_MAGIC, 18, int*)
#define ILITEK_IOCTL_ICE_MODE_SWITCH        _IOWR(ILITEK_IOCTL_MAGIC, 19, int)

#define ILITEK_IOCTL_TP_INTERFACE_TYPE        _IOWR(ILITEK_IOCTL_MAGIC, 20, u8*)
#define ILITEK_IOCTL_TP_DUMP_FLASH        _IOWR(ILITEK_IOCTL_MAGIC, 21, int)

static unsigned char g_user_buf[USER_STR_BUFF] = {0};

static int str2hex(char *str)
{
    int strlen, result, intermed, intermedtop;
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

int katoi(char *str)
{
    int result = 0;
    unsigned int digit;
    int sign;

    if (*str == '-') {
        sign = 1;
        str += 1;
    } else {
        sign = 0;
        if (*str == '+') {
            str += 1;
        }
    }

    for (;; str += 1) {
        digit = *str - '0';
        if (digit > 9)
            break;
        result = (10 * result) + digit;
    }

    if (sign) {
        return -result;
    }
    return result;
}

struct file_buffer {
    char *ptr;
    char file_name[128];
    int32_t file_len;
    int32_t file_max_zise;
};

static int file_write(struct file_buffer *file, bool new_open)
{
    struct file *f = NULL;
    mm_segment_t fs;
    loff_t pos;

    if (file->ptr == NULL) {
        ipio_err("str is invaild\n");
        return -1;
    }

    if (file->file_name[0] == 0) {
        ipio_err("file name is invaild\n");
        return -1;
    }

    if (file->file_len >= file->file_max_zise) {
        ipio_err("The length saved to file is too long !\n");
        return -1;
    }

    if (new_open)
        f = filp_open(file->file_name, O_WRONLY | O_CREAT | O_TRUNC, 644);
    else
        f = filp_open(file->file_name, O_WRONLY | O_CREAT | O_APPEND, 644);

    if (ERR_ALLOC_MEM(f)) {
        ipio_err("Failed to open %s file\n", file->file_name);
        return -1;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_write(f, file->ptr, file->file_len, &pos);
    set_fs(fs);
    filp_close(f, NULL);
    return 0;
}

static int debug_mode_get_data(struct file_buffer *file, u8 type, u32 frame_count)
{
    int ret;
    int timeout = 50;
    u8 cmd[2] = { 0 }, row, col;
    s16 temp;
    unsigned char *ptr;
    int j;
    u16 write_index = 0;

    idev->debug_node_open = false;
    idev->debug_data_frame = 0;
    row = idev->ych_num;
    col = idev->xch_num;

    mutex_lock(&idev->touch_mutex);
    cmd[0] = 0xFA;
    cmd[1] = type;
    ret = idev->write(cmd, 2);
    idev->debug_node_open = true;
    mutex_unlock(&idev->touch_mutex);
    if (ret < 0)
        return ret;

    while ((write_index < frame_count) && (timeout > 0)) {
        TPD_INFO("frame = %d,index = %d,count = %d\n", write_index, write_index % 1024, idev->debug_data_frame);
        if ((write_index % 1024) < idev->debug_data_frame) {
            mutex_lock(&idev->touch_mutex);
            file->file_len = 0;
            memset(file->ptr, 0, file->file_max_zise);
            file->file_len += snprintf(file->ptr + file->file_len,
                                       file->file_max_zise - file->file_len,
                                       "\n\nFrame%d,", write_index);
            for (j = 0; j < col; j++)
                file->file_len += snprintf(file->ptr + file->file_len,
                                           file->file_max_zise - file->file_len,
                                           "[X%d] ,", j);
            ptr = &idev->debug_buf[write_index % 1024][35];
            for (j = 0; j < row * col; j++, ptr += 2) {
                temp = (*ptr << 8) + *(ptr + 1);
                if (j % col == 0)
                    file->file_len += snprintf(file->ptr + file->file_len,
                                               file->file_max_zise - file->file_len,
                                               "\n[Y%d] ,", (j / col));
                file->file_len += snprintf(file->ptr + file->file_len,
                                           file->file_max_zise - file->file_len,
                                           "%d, ", temp);
            }
            file->file_len += snprintf(file->ptr + file->file_len,
                                       file->file_max_zise - file->file_len,
                                       "\n[X] ,");
            for (j = 0; j < row + col; j++, ptr += 2) {
                temp = (*ptr << 8) + *(ptr + 1);
                if (j == col)
                    file->file_len += snprintf(file->ptr + file->file_len,
                                               file->file_max_zise - file->file_len,
                                               "\n[Y] ,");
                file->file_len += snprintf(file->ptr + file->file_len,
                                           file->file_max_zise - file->file_len,
                                           "%d, ", temp);
            }
            file_write(file, false);
            write_index++;
            mutex_unlock(&idev->touch_mutex);
            timeout = 50;
        }

        if (write_index % 1024 == 0 && idev->debug_data_frame == 1024)
            idev->debug_data_frame = 0;

        mdelay(100);/*get one frame data taken around 130ms*/
        timeout--;
        if (timeout == 0)
            ipio_err("debug mode get data timeout!\n");
    }
    idev->debug_node_open = false;
    return 0;
}

static ssize_t ilitek_proc_get_delta_data_read(struct file *pFile, char __user *buf, size_t size, loff_t *pos)
{
    s16 *delta = NULL;
    int row = 0, col = 0,  index = 0;
    int ret, i, x, y;
    int read_length = 0;
    u8 cmd[2] = {0};
    u8 *data = NULL;

    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

    idev->esd_check_enabled = false;
    mutex_lock(&idev->touch_mutex);

    row = idev->ych_num;
    col = idev->xch_num;
    read_length = 4 + 2 * row * col + 1 ;

    TPD_INFO("read length = %d\n", read_length);

    data = kcalloc(read_length + 1, sizeof(u8), GFP_KERNEL);
    if (ERR_ALLOC_MEM(data)) {
        ipio_err("Failed to allocate data mem\n");
        goto out;
    }

    delta = kcalloc(P5_X_DEBUG_MODE_PACKET_LENGTH, sizeof(s32), GFP_KERNEL);
    if (ERR_ALLOC_MEM(delta)) {
        ipio_err("Failed to allocate delta mem\n");
        goto out;
    }

    cmd[0] = 0xB7;
    cmd[1] = 0x1; //get delta
    ret = idev->write(cmd, sizeof(cmd));
    if (ret < 0) {
        ipio_err("Failed to write 0xB7,0x1 command, %d\n", ret);
        goto out;
    }

    msleep(20);

    /* read debug packet header */
    ret = idev->read(data, read_length);

    cmd[1] = 0x03; //switch to normal mode
    ret = idev->write(cmd, sizeof(cmd));
    if (ret < 0) {
        ipio_err("Failed to write 0xB7,0x3 command, %d\n", ret);
        goto out;
    }

    for (i = 4, index = 0; index < row * col * 2; i += 2, index++)
        delta[index] = (data[i] << 8) + data[i + 1];

    size = snprintf(g_user_buf + size, PAGE_SIZE - size, "======== Deltadata ========\n");
    TPD_INFO("======== Deltadata ========\n");

    size += snprintf(g_user_buf + size, PAGE_SIZE - size,
                     "Header 0x%x ,Type %d, Length %d\n", data[0], data[1], (data[2] << 8) | data[3]);
    TPD_INFO("Header 0x%x ,Type %d, Length %d\n", data[0], data[1], (data[2] << 8) | data[3]);

    // print delta data
    for (y = 0; y < row; y++) {
        size += snprintf(g_user_buf + size, PAGE_SIZE - size, "[%2d] ", (y + 1));
        TPD_INFO("[%2d] ", (y + 1));

        for (x = 0; x < col; x++) {
            int shift = y * col + x;
            size += snprintf(g_user_buf + size, PAGE_SIZE - size, "%5d", delta[shift]);
            printk(KERN_CONT "%5d", delta[shift]);
        }
        size += snprintf(g_user_buf + size, PAGE_SIZE - size, "\n");
        printk(KERN_CONT "\n");
    }

    ret = copy_to_user(buf, g_user_buf, size);
    if (ret < 0) {
        ipio_err("Failed to copy data to user space");
    }

out:
    *pos += size;
    mutex_unlock(&idev->touch_mutex);
    idev->esd_check_enabled = true;
    ipio_kfree((void **)&data);
    ipio_kfree((void **)&delta);
    return size;
}

static ssize_t ilitek_proc_fw_get_raw_data_read(struct file *pFile, char __user *buf, size_t size, loff_t *pos)
{
    s16 *rawdata = NULL;
    int row = 0, col = 0,  index = 0;
    int ret, i, x, y;
    int read_length = 0;
    u8 cmd[2] = {0};
    u8 *data = NULL;

    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

    idev->esd_check_enabled = false;
    mutex_lock(&idev->touch_mutex);

    row = idev->ych_num;
    col = idev->xch_num;
    read_length = 4 + 2 * row * col + 1 ;

    TPD_INFO("read length = %d\n", read_length);

    data = kcalloc(read_length + 1, sizeof(u8), GFP_KERNEL);
    if (ERR_ALLOC_MEM(data)) {
        ipio_err("Failed to allocate data mem\n");
        goto out;
    }

    rawdata = kcalloc(P5_X_DEBUG_MODE_PACKET_LENGTH, sizeof(s32), GFP_KERNEL);
    if (ERR_ALLOC_MEM(rawdata)) {
        ipio_err("Failed to allocate rawdata mem\n");
        goto out;
    }

    cmd[0] = 0xB7;
    cmd[1] = 0x2; //get rawdata
    ret = idev->write(cmd, sizeof(cmd));
    if (ret < 0) {
        ipio_err("Failed to write 0xB7,0x2 command, %d\n", ret);
        goto out;
    }

    msleep(20);

    /* read debug packet header */
    ret = idev->read(data, read_length);

    cmd[1] = 0x03; //switch to normal mode
    ret = idev->write(cmd, sizeof(cmd));
    if (ret < 0) {
        ipio_err("Failed to write 0xB7,0x3 command, %d\n", ret);
        goto out;
    }

    for (i = 4, index = 0; index < row * col * 2; i += 2, index++)
        rawdata[index] = (data[i] << 8) + data[i + 1];

    size = snprintf(g_user_buf, PAGE_SIZE, "======== RawData ========\n");
    TPD_INFO("======== RawData ========\n");

    size += snprintf(g_user_buf + size, PAGE_SIZE - size,
                     "Header 0x%x ,Type %d, Length %d\n", data[0], data[1], (data[2] << 8) | data[3]);
    TPD_INFO("Header 0x%x ,Type %d, Length %d\n", data[0], data[1], (data[2] << 8) | data[3]);

    // print raw data
    for (y = 0; y < row; y++) {
        size += snprintf(g_user_buf + size, PAGE_SIZE - size, "[%2d] ", (y + 1));
        TPD_INFO("[%2d] ", (y + 1));

        for (x = 0; x < col; x++) {
            int shift = y * col + x;
            size += snprintf(g_user_buf + size, PAGE_SIZE - size, "%5d", rawdata[shift]);
            printk(KERN_CONT "%5d", rawdata[shift]);
        }
        size += snprintf(g_user_buf + size, PAGE_SIZE - size, "\n");
        printk(KERN_CONT "\n");
    }

    ret = copy_to_user(buf, g_user_buf, size);
    if (ret < 0) {
        ipio_err("Failed to copy data to user space");
    }

out:
    *pos += size;
    mutex_unlock(&idev->touch_mutex);
    idev->esd_check_enabled = true;
    ipio_kfree((void **)&data);
    ipio_kfree((void **)&rawdata);
    return size;
}

static ssize_t ilitek_proc_fw_pc_counter_read(struct file *pFile, char __user *buf, size_t size, loff_t *pos)
{
    int pc;

    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));
    mutex_lock(&idev->touch_mutex);
    pc = ilitek_tddi_ic_get_pc_counter();
    mutex_unlock(&idev->touch_mutex);
    size = snprintf(g_user_buf, PAGE_SIZE, "pc counter = 0x%x\n", pc);
    pc = copy_to_user(buf, g_user_buf, size);
    if (pc < 0)
        ipio_err("Failed to copy data to user space");

    *pos += size;
    return size;
}

static u32 rw_reg[5] = {0};
static ssize_t ilitek_proc_rw_tp_reg_read(struct file *pFile, char __user *buf, size_t size, loff_t *pos)
{
    int ret = 0;
    bool mcu_on = 0, read = 0;
    u32 type, addr, read_data, write_data, write_len, stop_mcu;

    if (*pos != 0)
        return 0;

    stop_mcu = rw_reg[0];
    type = rw_reg[1];
    addr = rw_reg[2];
    write_data = rw_reg[3];
    write_len = rw_reg[4];

    TPD_INFO("stop_mcu = %d\n", rw_reg[0]);

    idev->esd_check_enabled = false;
    mutex_lock(&idev->touch_mutex);

    if (stop_mcu == mcu_on) {
        ret = ilitek_ice_mode_ctrl(ENABLE, ON);
        if (ret < 0) {
            ipio_err("Failed to enter ICE mode, ret = %d\n", ret);
            goto out;
        }
    } else {
        ret = ilitek_ice_mode_ctrl(ENABLE, OFF);
        if (ret < 0) {
            ipio_err("Failed to enter ICE mode, ret = %d\n", ret);
            goto out;
        }
    }

    if (type == read) {
        ret = ilitek_ice_mode_read(addr, &read_data, sizeof(u32));
        TPD_INFO("READ:addr = 0x%06x, read = 0x%08x\n", addr, read_data);
        size = snprintf(g_user_buf, PAGE_SIZE, "READ:addr = 0x%06x, read = 0x%08x\n", addr, read_data);
    } else {
        ilitek_ice_mode_write(addr, write_data, write_len);
        TPD_INFO("WRITE:addr = 0x%06x, write = 0x%08x, len =%d byte\n", addr, write_data, write_len);
        size = snprintf(g_user_buf, PAGE_SIZE, "WRITE:addr = 0x%06x, write = 0x%08x, len =%d byte\n", addr, write_data, write_len);
    }

    if (stop_mcu == mcu_on)
        ilitek_ice_mode_ctrl(DISABLE, ON);
    else
        ilitek_ice_mode_ctrl(DISABLE, OFF);

    ret = copy_to_user(buf, g_user_buf, size);
    if (ret < 0)
        ipio_err("Failed to copy data to user space");

out:
    *pos += size;
    mutex_unlock(&idev->touch_mutex);
    idev->esd_check_enabled = true;
    return size;
}

static ssize_t ilitek_proc_rw_tp_reg_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{
    char *token = NULL, *cur = NULL;
    char cmd[256] = { 0 };
    u32 count = 0;

    if (buff != NULL) {
        if (copy_from_user(cmd, buff, size - 1)) {
            TPD_INFO("copy data from user space, failed\n");
            return -1;
        }
    }
    token = cur = cmd;
    while ((token = strsep(&cur, ",")) != NULL) {
        rw_reg[count] = str2hex(token);
        TPD_INFO("rw_reg[%d] = 0x%x\n", count, rw_reg[count]);
        count++;
    }
    return size;
}

static ssize_t ilitek_proc_debug_switch_read(struct file *pFile, char __user *buff, size_t size, loff_t *pos)
{
    int ret = 0;
    int i = 0;
    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));
    idev->debug_data_frame = 0;
    idev->debug_node_open = !idev->debug_node_open;
    if (idev->debug_node_open) {
        if (ERR_ALLOC_MEM(idev->debug_buf)) {
            idev->debug_buf = (unsigned char **)kmalloc(1024  * sizeof(unsigned char *), GFP_KERNEL);
            if (!ERR_ALLOC_MEM(idev->debug_buf)) {
                for (i = 0; i < 1024; i++) {
                    idev->debug_buf[i] = (char *)kmalloc(2048 * sizeof(unsigned char), GFP_KERNEL);
                    if (ERR_ALLOC_MEM(idev->debug_buf[i])) {
                        TPD_INFO("Failed to malloc debug_buf[%d]\n", i);
                    }
                }
            } else {
                TPD_INFO("Failed to malloc debug_buf\n");
            }
        } else {
            TPD_INFO("Already malloc debug_buf\n");
        }
    } else {
        if (!ERR_ALLOC_MEM(idev->debug_buf)) {
            for (i = 0; i < 1024; i++) {
                ipio_kfree((void **)&idev->debug_buf[i]);
            }
            ipio_kfree((void **)&idev->debug_buf);
        } else {
            TPD_INFO("Already free debug_buf\n");
        }
    }
    TPD_INFO(" %s debug_flag message = %x\n", idev->debug_node_open ? "Enabled" : "Disabled", idev->debug_node_open);

    size = snprintf(g_user_buf, USER_STR_BUFF,
                    "debug_node_open : %s\n",
                    idev->debug_node_open ? "Enabled" : "Disabled");

    *pos += size;

    ret = copy_to_user(buff, g_user_buf, size);
    if (ret < 0)
        ipio_err("Failed to copy data to user space");

    return size;
}

static ssize_t ilitek_proc_debug_message_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    unsigned long p = *pos;
    unsigned int count = size;
    int i = 0;
    int send_data_len = 0;
    int ret = 0;
    int data_count = 0;
    int one_data_bytes = 0;
    int need_read_data_len = 0;
    int type = 0;
    unsigned char *tmpbuf = NULL;
    unsigned char tmpbufback[128] = {0};

    mutex_lock(&idev->debug_read_mutex);

    while (idev->debug_data_frame <= 0) {
        if (filp->f_flags & O_NONBLOCK) {
            mutex_unlock(&idev->debug_read_mutex);
            return -EAGAIN;
        }
        wait_event_interruptible(idev->inq, idev->debug_data_frame > 0);
    }

    mutex_lock(&idev->debug_mutex);

    tmpbuf = vmalloc(DEBUG_MESSAGE_MAX_LENGTH);    /* buf size if even */
    if (ERR_ALLOC_MEM(tmpbuf)) {
        ipio_err("buffer vmalloc error\n");
        send_data_len += snprintf(tmpbufback + send_data_len,
                                  128 - send_data_len,
                                  "buffer vmalloc error\n");
        ret = copy_to_user(buff, tmpbufback, send_data_len); /*idev->debug_buf[0] */
        goto out;
    }

    if (idev->debug_data_frame > 0) {
        if (idev->debug_buf[0][0] == P5_X_DEMO_PACKET_ID) {
            need_read_data_len = 43;
        } else if (idev->debug_buf[0][0] == P5_X_I2CUART_PACKET_ID) {
            type = idev->debug_buf[0][3] & 0x0F;

            data_count = idev->debug_buf[0][1] * idev->debug_buf[0][2];

            if (type == 0 || type == 1 || type == 6) {
                one_data_bytes = 1;
            } else if (type == 2 || type == 3) {
                one_data_bytes = 2;
            } else if (type == 4 || type == 5) {
                one_data_bytes = 4;
            }
            need_read_data_len = data_count * one_data_bytes + 1 + 5;
        } else if (idev->debug_buf[0][0] == P5_X_DEBUG_PACKET_ID) {
            send_data_len = 0;    /* idev->debug_buf[0][1] - 2; */
            need_read_data_len = 2040;
        }

        for (i = 0; i < need_read_data_len; i++) {
            send_data_len += snprintf(tmpbuf + send_data_len,
                                      DEBUG_MESSAGE_MAX_LENGTH - send_data_len,
                                      "%02X", idev->debug_buf[0][i]);
            if (send_data_len >= (DEBUG_DATA_MAX_LENGTH * 2)) {
                ipio_err("send_data_len = %d set 4096 i = %d\n", send_data_len, i);
                send_data_len = (DEBUG_DATA_MAX_LENGTH * 2);
                break;
            }
        }
        send_data_len += snprintf(tmpbuf + send_data_len,
                                  DEBUG_MESSAGE_MAX_LENGTH - send_data_len, "\n\n");

        if (p == 5 || size == (DEBUG_DATA_MAX_LENGTH * 2) || size == DEBUG_DATA_MAX_LENGTH) {
            idev->debug_data_frame--;
            if (idev->debug_data_frame < 0) {
                idev->debug_data_frame = 0;
            }

            for (i = 1; i <= idev->debug_data_frame; i++)
                memcpy(idev->debug_buf[i - 1], idev->debug_buf[i], DEBUG_DATA_MAX_LENGTH);
        }
    } else {
        ipio_err("no data send\n");
        send_data_len += snprintf(tmpbuf + send_data_len,
                                  DEBUG_MESSAGE_MAX_LENGTH - send_data_len, "no data send\n");
    }

    /* Preparing to send debug data to user */
    if (size == DEBUG_DATA_MAX_LENGTH * 2)
        ret = copy_to_user(buff, tmpbuf, send_data_len);
    else
        ret = copy_to_user(buff, tmpbuf + p, send_data_len - p);

    if (ret) {
        ipio_err("copy_to_user err\n");
        ret = -EFAULT;
    } else {
        *pos += count;
        ret = count;
        TPD_DEBUG("Read %d bytes(s) from %ld\n", count, p);
    }

out:
    /* ipio_err("send_data_len = %d\n", send_data_len); */
    if (send_data_len <= 0 || send_data_len > DEBUG_DATA_MAX_LENGTH * 2) {
        ipio_err("send_data_len = %d set 2048\n", send_data_len);
        send_data_len = DEBUG_DATA_MAX_LENGTH * 2;
    }

    mutex_unlock(&idev->debug_mutex);
    mutex_unlock(&idev->debug_read_mutex);
    ipio_vfree((void **)&tmpbuf);
    return send_data_len;
}

static ssize_t ilitek_proc_get_debug_mode_data_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    int ret;
    u8 tp_mode;
    struct file_buffer csv;

    if (*pos != 0)
        return 0;

    /* initialize file */
    memset(csv.file_name, 0, sizeof(csv.file_name));
    snprintf(csv.file_name, 128, "%s", DEBUG_DATA_FILE_PATH);
    csv.file_len = 0;
    csv.file_max_zise = DEBUG_DATA_FILE_SIZE;
    csv.ptr = vmalloc(DEBUG_DATA_FILE_SIZE);

    if (ERR_ALLOC_MEM(csv.ptr)) {
        ipio_err("Failed to allocate CSV mem\n");
        goto out;
    }

    /* save data to csv */
    TPD_INFO("Get Raw data %d frame\n", idev->raw_count);
    TPD_INFO("Get Delta data %d frame\n", idev->delta_count);
    csv.file_len += snprintf(csv.ptr + csv.file_len,
                             DEBUG_DATA_FILE_SIZE - csv.file_len,
                             "Get Raw data %d frame\n", idev->raw_count);
    csv.file_len += snprintf(csv.ptr + csv.file_len,
                             DEBUG_DATA_FILE_SIZE - csv.file_len,
                             "Get Delta data %d frame\n", idev->delta_count);
    file_write(&csv, true);

    /* change to debug mode */
    tp_mode = P5_X_FW_DEBUG_MODE;
    mutex_lock(&idev->touch_mutex);
    ret = ilitek_tddi_switch_mode(&tp_mode);
    mutex_unlock(&idev->touch_mutex);
    if (ret < 0)
        goto out;

    /* get raw data */
    csv.file_len = 0;
    memset(csv.ptr, 0, csv.file_max_zise);
    csv.file_len += snprintf(csv.ptr + csv.file_len,
                             DEBUG_DATA_FILE_SIZE - csv.file_len,
                             "\n\n=======Raw data=======");
    file_write(&csv, false);
    ret = debug_mode_get_data(&csv, P5_X_FW_RAW_DATA_MODE, idev->raw_count);
    if (ret < 0)
        goto out;

    /* get delta data */
    csv.file_len = 0;
    memset(csv.ptr, 0, csv.file_max_zise);
    csv.file_len += snprintf(csv.ptr + csv.file_len,
                             DEBUG_DATA_FILE_SIZE - csv.file_len,
                             "\n\n=======Delta data=======");
    file_write(&csv, false);
    ret = debug_mode_get_data(&csv, P5_X_FW_DELTA_DATA_MODE, idev->delta_count);
    if (ret < 0)
        goto out;

    /* change to demo mode */
    tp_mode = P5_X_FW_DEMO_MODE;
    mutex_lock(&idev->touch_mutex);
    ilitek_tddi_switch_mode(&tp_mode);
    mutex_unlock(&idev->touch_mutex);
out:
    ipio_vfree((void **)&csv.ptr);
    return 0;
}

static ssize_t ilitek_proc_get_debug_mode_data_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{
    char *token = NULL, *cur = NULL;
    char cmd[256] = {0};
    u8 temp[256] = {0}, count = 0;

    if (buff != NULL) {
        if (copy_from_user(cmd, buff, size - 1)) {
            TPD_INFO("copy data from user space, failed\n");
            return -1;
        }
    }

    TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);
    token = cur = cmd;
    while ((token = strsep(&cur, ",")) != NULL) {
        temp[count] = str2hex(token);
        TPD_INFO("temp[%d] = %d\n", count, temp[count]);
        count++;
    }

    idev->raw_count = ((temp[0] << 8) | temp[1]);
    idev->delta_count = ((temp[2] << 8) | temp[3]);
    idev->bg_count = ((temp[4] << 8) | temp[5]);

    TPD_INFO("Raw_count = %d, Delta_count = %d, BG_count = %d\n", idev->raw_count, idev->delta_count, idev->bg_count);
    return size;
}

static ssize_t ilitek_node_mp_lcm_on_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    int ret = 0;
    char apk_ret[100] = {0};

    TPD_INFO("Run MP test with LCM on\n");

    if (*pos != 0)
        return 0;

    ilitek_tddi_mp_test_handler(apk_ret, NULL, NULL, ON);

    ret = copy_to_user((char *)buff, apk_ret, sizeof(apk_ret));
    if (ret < 0)
        ipio_err("Failed to copy data to user space\n");

    return ret;
}

static ssize_t ilitek_node_mp_lcm_off_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    int ret = 0;
    char apk_ret[100] = {0};

    TPD_INFO("Run MP test with LCM off\n");

    if (*pos != 0)
        return 0;

    ilitek_tddi_mp_test_handler(apk_ret, NULL, NULL, OFF);

    ret = copy_to_user((char *)buff, apk_ret, sizeof(apk_ret));
    if (ret < 0)
        ipio_err("Failed to copy data to user space\n");

    return ret;
}

static ssize_t ilitek_proc_fw_process_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    int ret = 0;
    u32 len = 0;

    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

    len = snprintf(g_user_buf, USER_STR_BUFF, "%02d\n", idev->fw_update_stat);

    TPD_INFO("update status = %d\n", idev->fw_update_stat);

    ret = copy_to_user((char *) buff, &idev->fw_update_stat, len);
    if (ret < 0) {
        ipio_err("Failed to copy data to user space\n");
    }

    *pos = len;
    return len;
}

static int ilitek_tdd_fw_hex_open(void)
{
    int fsize = 1;
    struct file *f = NULL;
    mm_segment_t old_fs;
    loff_t pos = 0;

    TPD_INFO("Open file method = FILP_OPEN, path = %s\n", UPDATE_FW_PATH);

    f = filp_open(UPDATE_FW_PATH, O_RDONLY, 0644);
    if (ERR_ALLOC_MEM(f)) {
        ipio_err("Failed to open the file at %ld.\n", PTR_ERR(f));
        return -ENOMEM;
    }

    fsize = f->f_inode->i_size;
    TPD_INFO("fsize = %d\n", fsize);
    if (fsize <= 0) {
        ipio_err("The size of file is invaild\n");
        filp_close(f, NULL);
        return -ENOMEM;
    }

    ipio_vfree((void **) & (idev->tp_firmware.data));
    idev->tp_firmware.size = 0;
    //new fw data buffer
    idev->tp_firmware.data = vmalloc(fsize);
    if (idev->tp_firmware.data == NULL) {
        TPD_INFO("kmalloc tp firmware data error\n");

        idev->tp_firmware.data = vmalloc(fsize);
        if (idev->tp_firmware.data == NULL) {
            TPD_INFO("retry kmalloc tp firmware data error\n");
            return -ENOMEM;
        }
    }

    /* ready to map user's memory to obtain data by reading files */
    old_fs = get_fs();
    //set_fs(get_ds());
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_read(f, (u8 *)idev->tp_firmware.data, fsize, &pos);
    set_fs(old_fs);
    filp_close(f, NULL);
    idev->tp_firmware.size = fsize;

    return 0;
}

static ssize_t ilitek_node_fw_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
    int ret = 0;
    u32 len = 0;

    TPD_INFO("Preparing to upgarde firmware\n");

    if (*pos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));
    mutex_lock(&idev->touch_mutex);
    idev->actual_tp_mode = P5_X_FW_DEMO_MODE;

    if (ilitek_tdd_fw_hex_open() < 0) {
        ipio_err("Failed to open hex file\n");
    }

    ret = ilitek_tddi_fw_upgrade();
    mutex_unlock(&idev->touch_mutex);
    len = snprintf(g_user_buf, USER_STR_BUFF,
                   "upgrade firwmare %s\n", (ret != 0) ? "failed" : "succeed");

    ret = copy_to_user((u32 *) buff, g_user_buf, len);
    if (ret < 0)
        ipio_err("Failed to copy data to user space\n");

    return 0;
}

static ssize_t ilitek_proc_debug_level_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
    int ret = 0;
    u32 len = 0;

    if (*pPos != 0)
        return 0;

    memset(g_user_buf, 0, USER_STR_BUFF * sizeof(unsigned char));

    TPD_INFO("Current DEBUG Level = %d\n", ipio_debug_level);

    len = snprintf(g_user_buf, PAGE_SIZE, "Current DEBUG Level = %d\n", ipio_debug_level);

    ret = copy_to_user((u32 *) buff, g_user_buf, len);
    if (ret < 0) {
        ipio_err("Failed to copy data to user space\n");
    }

    *pPos += len;
    return len;
}

static ssize_t ilitek_proc_debug_level_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
    char cmd[10] = { 0 };

    if (buff != NULL) {
        if (copy_from_user(cmd, buff, size - 1)) {
            TPD_INFO("copy data from user space, failed\n");
            return -1;
        }
    }
    ipio_debug_level = katoi(cmd);
    TPD_INFO("ipio_debug_level = %d\n", ipio_debug_level);
    return size;
}

static ssize_t ilitek_node_ioctl_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{
    int i, count = 0;
    char cmd[512] = {0};
    char *token = NULL, *cur = NULL;
    u8 temp[256] = {0};
    u32 *data = NULL;
    u8 tp_mode;

    if (buff != NULL) {
        if (copy_from_user(cmd, buff, size - 1)) {
            TPD_INFO("copy data from user space, failed\n");
            return -1;
        }
    }

    TPD_INFO("size = %d, cmd = %s\n", (int)size, cmd);

    token = cur = cmd;

    data = kcalloc(512, sizeof(u32), GFP_KERNEL);

    while ((token = strsep(&cur, ",")) != NULL) {
        data[count] = str2hex(token);
        TPD_INFO("data[%d] = %x\n", count, data[count]);
        count++;
    }

    TPD_INFO("cmd = %s\n", cmd);
    mutex_lock(&idev->touch_mutex);
    if (strcmp(cmd, "hwreset") == 0) {
        ilitek_tddi_reset_ctrl(TP_HW_RST_ONLY);
    } else if (strcmp(cmd, "icwholereset") == 0) {
        ilitek_ice_mode_ctrl(ENABLE, OFF);
        ilitek_tddi_reset_ctrl(TP_IC_WHOLE_RST);
    } else if (strcmp(cmd, "iccodereset") == 0) {
        ilitek_ice_mode_ctrl(ENABLE, OFF);
        ilitek_tddi_reset_ctrl(TP_IC_CODE_RST);
        ilitek_ice_mode_ctrl(DISABLE, OFF);
    } else if (strcmp(cmd, "getinfo") == 0) {
        ilitek_ice_mode_ctrl(ENABLE, OFF);
        ilitek_tddi_ic_get_info();
        ilitek_ice_mode_ctrl(DISABLE, OFF);
        ilitek_tddi_ic_get_protocl_ver();
        ilitek_tddi_ic_get_fw_ver();
        ilitek_tddi_ic_get_core_ver();
        ilitek_tddi_ic_get_tp_info();
        ilitek_tddi_ic_get_panel_info();
    } else if (strcmp(cmd, "enableicemode") == 0) {
        if (data[1] == ON)
            ilitek_ice_mode_ctrl(ENABLE, ON);
        else
            ilitek_ice_mode_ctrl(ENABLE, OFF);
    } else if (strcmp(cmd, "disableicemode") == 0) {
        ilitek_ice_mode_ctrl(DISABLE, OFF);
    } else if (strcmp(cmd, "enablewqesd") == 0) {
        idev->esd_check_enabled = true;
    } else if (strcmp(cmd, "enablewqbat") == 0) {
        TPD_INFO("oplus kit ctrl this\n");
    } else if (strcmp(cmd, "disablewqesd") == 0) {
        idev->esd_check_enabled = false;
    } else if (strcmp(cmd, "disablewqbat") == 0) {
        TPD_INFO("oplus kit ctrl this\n");
    } else if (strcmp(cmd, "gesture") == 0) {
        idev->gesture = !idev->gesture;
        TPD_INFO("gesture = %d\n", idev->gesture);
    } else if (strcmp(cmd, "esdgesture") == 0) {
        ilitek_tddi_wq_ges_recover();
    } else if (strcmp(cmd, "iceflag") == 0) {
        if (data[1] == ENABLE)
            atomic_set(&idev->ice_stat, ENABLE);
        else
            atomic_set(&idev->ice_stat, DISABLE);
        TPD_INFO("ice mode flag = %d\n", atomic_read(&idev->ice_stat));
    } else if (strcmp(cmd, "gesturenormal") == 0) {
        idev->gesture_mode = P5_X_FW_GESTURE_NORMAL_MODE;
        TPD_INFO("gesture mode = %d\n", idev->gesture_mode);
    } else if (strcmp(cmd, "gestureinfo") == 0) {
        idev->gesture_mode = P5_X_FW_GESTURE_INFO_MODE;
        TPD_INFO("gesture mode = %d\n", idev->gesture_mode);
    } else if (strcmp(cmd, "netlink") == 0) {
        idev->netlink = !idev->netlink;
        TPD_INFO("netlink flag= %d\n", idev->netlink);
    } else if (strcmp(cmd, "switchtestmode") == 0) {
        tp_mode = P5_X_FW_TEST_MODE;
        ilitek_tddi_switch_mode(&tp_mode);
    } else if (strcmp(cmd, "switchdebugmode") == 0) {
        tp_mode = P5_X_FW_DEBUG_MODE;
        ilitek_tddi_switch_mode(&tp_mode);
    } else if (strcmp(cmd, "switchdemomode") == 0) {
        tp_mode = P5_X_FW_DEMO_MODE;
        ilitek_tddi_switch_mode(&tp_mode);
    } else if (strcmp(cmd, "rawdatarecore") == 0) {
        ilitek_tddi_get_tp_recore_ctrl(data[1]);
    } else if (strcmp(cmd, "switchdemodebuginfomode") == 0) {
        tp_mode = P5_X_FW_DEMO_DEBUG_INFO_MODE;
        ilitek_tddi_switch_mode(&tp_mode);
    } else if (strcmp(cmd, "gesturedemoen") == 0) {
        if (data[1] == 0)
            idev->gesture_demo_en = DISABLE;
        else
            idev->gesture_demo_en = ENABLE;
        TPD_INFO("Gesture demo mode control = %d\n",  idev->gesture_demo_en);
        ilitek_tddi_ic_func_ctrl("gesture_demo_en", idev->gesture_demo_en);
    } else if (strcmp(cmd, "gesturefailrsn") == 0) {
        if (data[1] == 0)
            ilitek_set_gesture_fail_reason(DISABLE);
        else
            ilitek_set_gesture_fail_reason(ENABLE);
        TPD_INFO("%s gesture fail reason\n", data[1] ? "ENABLE" : "DISABLE");
    } else if (strcmp(cmd, "dbgflag") == 0) {
        idev->debug_node_open = !idev->debug_node_open;
        TPD_INFO("debug flag message = %d\n", idev->debug_node_open);
    } else if (strcmp(cmd, "iow") == 0) {
        int w_len = 0;
        w_len = data[1];
        TPD_INFO("w_len = %d\n", w_len);

        for (i = 0; i < w_len; i++) {
            temp[i] = data[2 + i];
            TPD_INFO("write[%d] = %x\n", i, temp[i]);
        }

        idev->write(temp, w_len);
    } else if (strcmp(cmd, "ior") == 0) {
        int r_len = 0;
        r_len = data[1];
        TPD_INFO("r_len = %d\n", r_len);
        idev->read(temp, r_len);
        for (i = 0; i < r_len; i++)
            TPD_INFO("read[%d] = %x\n", i, temp[i]);
    } else if (strcmp(cmd, "iowr") == 0) {
        int delay = 0;
        int w_len = 0, r_len = 0;
        w_len = data[1];
        r_len = data[2];
        delay = data[3];
        TPD_INFO("w_len = %d, r_len = %d, delay = %d\n", w_len, r_len, delay);

        for (i = 0; i < w_len; i++) {
            temp[i] = data[4 + i];
            TPD_INFO("write[%d] = %x\n", i, temp[i]);
        }
        idev->write(temp, w_len);
        memset(temp, 0, sizeof(temp));
        mdelay(delay);
        idev->read(temp, r_len);

        for (i = 0; i < r_len; i++)
            TPD_INFO("read[%d] = %x\n", i, temp[i]);
    } else if (strcmp(cmd, "getddiregdata") == 0) {
        TPD_INFO("Get ddi reg one page: page = %x, reg = %x\n", data[1], data[2]);
        ilitek_tddi_ic_get_ddi_reg_onepage(data[1], data[2]);
    } else if (strcmp(cmd, "setddiregdata") == 0) {
        TPD_INFO("Set ddi reg one page: page = %x, reg = %x, data = %x\n", data[1], data[2], data[3]);
        ilitek_tddi_ic_set_ddi_reg_onepage(data[1], data[2], data[3]);
    } else if (strcmp(cmd, "dumpiramdata") == 0) {
        TPD_INFO("Start = 0x%x, End = 0x%x, Dump IRAM path = %s\n", data[1], data[2], DUMP_IRAM_PATH);
        ilitek_tddi_fw_dump_iram_data(data[1], data[2]);
    } else if (strcmp(cmd, "edge_plam_ctrl") == 0) {
        ilitek_tddi_edge_palm_ctrl(data[1]);
    } else {
        ipio_err("Unknown command\n");
    }
    mutex_unlock(&idev->touch_mutex);
    ipio_kfree((void **)&data);
    return size;
}

static void ilitek_plat_irq_enable(void)
{
    unsigned long flag;

    spin_lock_irqsave(&idev->irq_spin, flag);

    if (atomic_read(&idev->irq_stat) == ENABLE)
        goto out;

    if (!idev->irq_num) {
        ipio_err("gpio_to_irq (%d) is incorrect\n", idev->irq_num);
        goto out;
    }

    enable_irq(idev->irq_num);
    atomic_set(&idev->irq_stat, ENABLE);
    TPD_DEBUG("Enable irq success\n");

out:
    spin_unlock_irqrestore(&idev->irq_spin, flag);
}

static void ilitek_plat_irq_disable(void)
{
    unsigned long flag;

    spin_lock_irqsave(&idev->irq_spin, flag);

    if (atomic_read(&idev->irq_stat) == DISABLE)
        goto out;

    if (!idev->irq_num) {
        ipio_err("gpio_to_irq (%d) is incorrect\n", idev->irq_num);
        goto out;
    }

    disable_irq_nosync(idev->irq_num);
    atomic_set(&idev->irq_stat, DISABLE);
    TPD_DEBUG("Disable irq success\n");

out:
    spin_unlock_irqrestore(&idev->irq_spin, flag);
}

static long ilitek_node_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0, length = 0;
    u8 *szBuf = NULL, if_to_user = 0;
    static u16 i2c_rw_length;
    u32 id_to_user[3] = {0};
    char dbg[10] = { 0 };

    if (_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC) {
        ipio_err("The Magic number doesn't match\n");
        return -ENOTTY;
    }

    if (_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR) {
        ipio_err("The number of ioctl doesn't match\n");
        return -ENOTTY;
    }

    szBuf = kcalloc(IOCTL_I2C_BUFF, sizeof(u8), GFP_KERNEL);
    if (ERR_ALLOC_MEM(szBuf)) {
        ipio_err("Failed to allocate mem\n");
        return -ENOMEM;
    }
    mutex_lock(&idev->touch_mutex);
    switch (cmd) {
    case ILITEK_IOCTL_I2C_WRITE_DATA:
        TPD_INFO("ioctl: write len = %d\n", i2c_rw_length);
        if (copy_from_user(szBuf, (u8 *) arg, i2c_rw_length)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        ret = idev->write(&szBuf[0], i2c_rw_length);
        if (ret < 0)
            ipio_err("Failed to write data\n");
        break;
    case ILITEK_IOCTL_I2C_READ_DATA:
        TPD_INFO("ioctl: read len = %d\n", i2c_rw_length);
        ret = idev->read(szBuf, i2c_rw_length);
        if (ret < 0) {
            ipio_err("Failed to read data\n");
            break;
        }
        ret = copy_to_user((u8 *) arg, szBuf, i2c_rw_length);
        if (ret < 0)
            ipio_err("Failed to copy data to user space\n");
        break;
    case ILITEK_IOCTL_I2C_SET_WRITE_LENGTH:
    case ILITEK_IOCTL_I2C_SET_READ_LENGTH:
        i2c_rw_length = arg;
        break;
    case ILITEK_IOCTL_TP_HW_RESET:
        TPD_INFO("ioctl: hw reset\n");
        ilitek_tddi_reset_ctrl(ILITEK_RESET_METHOD);
        break;
    case ILITEK_IOCTL_TP_POWER_SWITCH:
        TPD_INFO("Not implemented yet\n");
        break;
    case ILITEK_IOCTL_TP_REPORT_SWITCH:
        if (copy_from_user(szBuf, (u8 *) arg, 1)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: report switch = %d\n", szBuf[0]);
        if (szBuf[0]) {
            idev->report = ENABLE;
            TPD_INFO("report is enabled\n");
        } else {
            idev->report = DISABLE;
            TPD_INFO("report is disabled\n");
        }
        break;
    case ILITEK_IOCTL_TP_IRQ_SWITCH:
        if (copy_from_user(szBuf, (u8 *) arg, 1)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: irq switch = %d\n", szBuf[0]);
        if (szBuf[0])
            ilitek_plat_irq_enable();
        else
            ilitek_plat_irq_disable();
        break;
    case ILITEK_IOCTL_TP_DEBUG_LEVEL:

        if (copy_from_user(dbg, (u32 *) arg, sizeof(u32))) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        ipio_debug_level = katoi(dbg);
        TPD_INFO("ipio_debug_level = %d", ipio_debug_level);
        break;
    case ILITEK_IOCTL_TP_FUNC_MODE:

        if (copy_from_user(szBuf, (u8 *) arg, 3)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: set func mode = %x,%x,%x\n", szBuf[0], szBuf[1], szBuf[2]);
        idev->write(&szBuf[0], 3);
        break;
    case ILITEK_IOCTL_TP_FW_VER:
        TPD_INFO("ioctl: get fw version\n");
        ret = ilitek_tddi_ic_get_fw_ver();
        if (ret < 0) {
            ipio_err("Failed to get firmware version\n");
            break;
        }
        szBuf[3] = idev->chip->fw_ver & 0xFF;
        szBuf[2] = (idev->chip->fw_ver >> 8) & 0xFF;
        szBuf[1] = (idev->chip->fw_ver >> 16) & 0xFF;
        szBuf[0] = idev->chip->fw_ver >> 24;
        TPD_INFO("Firmware version = %d.%d.%d.%d\n", szBuf[0], szBuf[1], szBuf[2], szBuf[3]);
        ret = copy_to_user((u8 *) arg, szBuf, 4);
        if (ret < 0)
            ipio_err("Failed to copy firmware version to user space\n");
        break;
    case ILITEK_IOCTL_TP_PL_VER:
        TPD_INFO("ioctl: get protocl version\n");
        ret = ilitek_tddi_ic_get_protocl_ver();
        if (ret < 0) {
            ipio_err("Failed to get protocol version\n");
            break;
        }
        szBuf[2] = idev->protocol->ver & 0xFF;
        szBuf[1] = (idev->protocol->ver >> 8) & 0xFF;
        szBuf[0] = idev->protocol->ver >> 16;
        TPD_INFO("Protocol version = %d.%d.%d\n", szBuf[0], szBuf[1], szBuf[2]);
        ret = copy_to_user((u8 *) arg, szBuf, 3);
        if (ret < 0)
            ipio_err("Failed to copy protocol version to user space\n");
        break;
    case ILITEK_IOCTL_TP_CORE_VER:
        TPD_INFO("ioctl: get core version\n");
        ret = ilitek_tddi_ic_get_core_ver();
        if (ret < 0) {
            ipio_err("Failed to get core version\n");
            break;
        }
        szBuf[3] = idev->chip->core_ver & 0xFF;
        szBuf[2] = (idev->chip->core_ver >> 8) & 0xFF;
        szBuf[1] = (idev->chip->core_ver >> 16) & 0xFF;
        szBuf[0] = idev->chip->core_ver >> 24;
        TPD_INFO("Core version = %d.%d.%d.%d\n", szBuf[0], szBuf[1], szBuf[2], szBuf[3]);
        ret = copy_to_user((u8 *) arg, szBuf, 4);
        if (ret < 0)
            ipio_err("Failed to copy core version to user space\n");
        break;
    case ILITEK_IOCTL_TP_DRV_VER:
        TPD_INFO("ioctl: get driver version\n");
        length = snprintf(szBuf, IOCTL_I2C_BUFF, "%s", DRIVER_VERSION);
        ret = copy_to_user((u8 *) arg, szBuf, length);
        if (ret < 0) {
            ipio_err("Failed to copy driver ver to user space\n");
        }
        break;
    case ILITEK_IOCTL_TP_CHIP_ID:
        TPD_INFO("ioctl: get chip id\n");
        ilitek_ice_mode_ctrl(ENABLE, OFF);
        ret = ilitek_tddi_ic_get_info();
        if (ret < 0) {
            ipio_err("Failed to get chip id\n");
            break;
        }
        id_to_user[0] = idev->chip->pid;
        id_to_user[1] = idev->chip->otp_id;
        id_to_user[2] = idev->chip->ana_id;
        ret = copy_to_user((u32 *) arg, id_to_user, sizeof(id_to_user));
        if (ret < 0)
            ipio_err("Failed to copy chip id to user space\n");
        ilitek_ice_mode_ctrl(DISABLE, OFF);
        break;
    case ILITEK_IOCTL_TP_NETLINK_CTRL:
        if (copy_from_user(szBuf, (u8 *) arg, 1)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: netlink ctrl = %d\n", szBuf[0]);
        if (szBuf[0]) {
            idev->netlink = ENABLE;
            TPD_INFO("ioctl: Netlink is enabled\n");
        } else {
            idev->netlink = DISABLE;
            TPD_INFO("ioctl: Netlink is disabled\n");
        }
        break;
    case ILITEK_IOCTL_TP_NETLINK_STATUS:
        TPD_INFO("ioctl: get netlink stat = %d\n", idev->netlink);
        ret = copy_to_user((int *)arg, &idev->netlink, sizeof(int));
        if (ret < 0)
            ipio_err("Failed to copy chip id to user space\n");
        break;
    case ILITEK_IOCTL_TP_MODE_CTRL:

        if (copy_from_user(szBuf, (u8 *) arg, 4)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: switch fw mode = %d\n", szBuf[0]);
        ret = ilitek_tddi_switch_mode(szBuf);
        if (ret < 0) {
            TPD_INFO("switch to fw mode (%d) failed\n", szBuf[0]);
        }
        break;
    case ILITEK_IOCTL_TP_MODE_STATUS:
        TPD_INFO("ioctl: current firmware mode = %d", idev->actual_tp_mode);
        ret = copy_to_user((int *)arg, &idev->actual_tp_mode, sizeof(int));
        if (ret < 0)
            ipio_err("Failed to copy chip id to user space\n");
        break;
    /* It works for host downloado only */
    case ILITEK_IOCTL_ICE_MODE_SWITCH:
        if (copy_from_user(szBuf, (u8 *) arg, 1)) {
            ipio_err("Failed to copy data from user space\n");
            ret = ENOMEM;
            break;
        }
        TPD_INFO("ioctl: switch ice mode = %d", szBuf[0]);
        if (szBuf[0]) {
            atomic_set(&idev->ice_stat, ENABLE);
            TPD_INFO("ioctl: set ice mode enabled\n");
        } else {
            atomic_set(&idev->ice_stat, DISABLE);
            TPD_INFO("ioctl: set ice mode disabled\n");
        }
        break;
    case ILITEK_IOCTL_TP_INTERFACE_TYPE:
        if_to_user = BUS_SPI;
        ret = copy_to_user((u8 *) arg, &if_to_user, sizeof(if_to_user));
        if (ret < 0) {
            ipio_err("Failed to copy interface type to user space\n");
        }
        break;
    case ILITEK_IOCTL_TP_DUMP_FLASH:
        TPD_INFO("ioctl: dump flash data donothing\n");
        break;
    default:
        ret = -ENOTTY;
        break;
    }
    mutex_unlock(&idev->touch_mutex);
    ipio_kfree((void **)&szBuf);
    return ret;
}

static struct proc_dir_entry *proc_dir_ilitek;

typedef struct {
    char *name;
    struct proc_dir_entry *node;
    struct file_operations *fops;
    bool isCreated;
} proc_node_t;

static struct file_operations proc_mp_lcm_on_test_fops = {
    .read = ilitek_node_mp_lcm_on_test_read,
};

static struct file_operations proc_mp_lcm_off_test_fops = {
    .read = ilitek_node_mp_lcm_off_test_read,
};

static struct file_operations proc_debug_message_fops = {
    .read = ilitek_proc_debug_message_read,
};

static struct file_operations proc_debug_message_switch_fops = {
    .read = ilitek_proc_debug_switch_read,
};

static struct file_operations proc_ioctl_fops = {
    .unlocked_ioctl = ilitek_node_ioctl,
    .write = ilitek_node_ioctl_write,
};

static struct file_operations proc_fw_upgrade_fops = {
    .read = ilitek_node_fw_upgrade_read,
};

static struct file_operations proc_fw_process_fops = {
    .read = ilitek_proc_fw_process_read,
};

static struct file_operations proc_get_delta_data_fops = {
    .read = ilitek_proc_get_delta_data_read,
};

static struct file_operations proc_get_raw_data_fops = {
    .read = ilitek_proc_fw_get_raw_data_read,
};

static struct file_operations proc_rw_tp_reg_fops = {
    .read = ilitek_proc_rw_tp_reg_read,
    .write = ilitek_proc_rw_tp_reg_write,
};

static struct file_operations proc_fw_pc_counter_fops = {
    .read = ilitek_proc_fw_pc_counter_read,
};

static struct file_operations proc_get_debug_mode_data_fops = {
    .read = ilitek_proc_get_debug_mode_data_read,
    .write = ilitek_proc_get_debug_mode_data_write,
};

static struct file_operations proc_debug_level_fops = {
    .write = ilitek_proc_debug_level_write,
    .read = ilitek_proc_debug_level_read,
};

static proc_node_t proc_table[] = {
    {"ioctl", NULL, &proc_ioctl_fops, false},
    {"fw_process", NULL, &proc_fw_process_fops, false},
    {"fw_upgrade", NULL, &proc_fw_upgrade_fops, false},
    {"debug_level", NULL, &proc_debug_level_fops, false},
    {"mp_lcm_on_test", NULL, &proc_mp_lcm_on_test_fops, false},
    {"mp_lcm_off_test", NULL, &proc_mp_lcm_off_test_fops, false},
    {"debug_message", NULL, &proc_debug_message_fops, false},
    {"debug_message_switch", NULL, &proc_debug_message_switch_fops, false},
    {"fw_pc_counter", NULL, &proc_fw_pc_counter_fops, false},
    {"show_delta_data", NULL, &proc_get_delta_data_fops, false},
    {"show_raw_data", NULL, &proc_get_raw_data_fops, false},
    {"get_debug_mode_data", NULL, &proc_get_debug_mode_data_fops, false},
    {"rw_tp_reg", NULL, &proc_rw_tp_reg_fops, false},
};

#define NETLINK_USER 21
static struct sock *netlink_skb;
static struct nlmsghdr *netlink_head;
static struct sk_buff *skb_out;
static int netlink_pid;

void netlink_reply_msg(void *raw, int size)
{
    int ret;
    int msg_size = size;
    u8 *data = (u8 *) raw;

    TPD_INFO("The size of data being sent to user = %d\n", msg_size);
    TPD_INFO("pid = %d\n", netlink_pid);
    TPD_INFO("Netlink is enable = %d\n", idev->netlink);

    if (idev->netlink) {
        skb_out = nlmsg_new(msg_size, 0);

        if (!skb_out) {
            ipio_err("Failed to allocate new skb\n");
            return;
        }

        netlink_head = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
        NETLINK_CB(skb_out).dst_group = 0;    /* not in mcast group */

        /* strncpy(NLMSG_DATA(netlink_head), data, msg_size); */
        ipio_memcpy(nlmsg_data(netlink_head), data, msg_size, size);

        ret = nlmsg_unicast(netlink_skb, skb_out, netlink_pid);
        if (ret < 0)
            ipio_err("Failed to send data back to user\n");
    }
}

static void netlink_recv_msg(struct sk_buff *skb)
{
    netlink_pid = 0;

    TPD_INFO("Netlink = %d\n", idev->netlink);

    netlink_head = (struct nlmsghdr *)skb->data;

    TPD_INFO("Received a request from client: %s, %d\n",
             (char *)NLMSG_DATA(netlink_head), (int)strlen((char *)NLMSG_DATA(netlink_head)));

    /* pid of sending process */
    netlink_pid = netlink_head->nlmsg_pid;

    TPD_INFO("the pid of sending process = %d\n", netlink_pid);

    /* TODO: may do something if there's not receiving msg from user. */
    if (netlink_pid != 0) {
        ipio_err("The channel of Netlink has been established successfully !\n");
        idev->netlink = ENABLE;
    } else {
        ipio_err("Failed to establish the channel between kernel and user space\n");
        idev->netlink = DISABLE;
    }
}

static int netlink_init(void)
{
    int ret = 0;

#if KERNEL_VERSION(3, 4, 0) > LINUX_VERSION_CODE
    netlink_skb = netlink_kernel_create(&init_net, NETLINK_USER, netlink_recv_msg, NULL, THIS_MODULE);
#else
    struct netlink_kernel_cfg cfg = {
        .input = netlink_recv_msg,
    };

    netlink_skb = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
#endif

    TPD_INFO("Initialise Netlink and create its socket\n");

    if (!netlink_skb) {
        ipio_err("Failed to create nelink socket\n");
        ret = -EFAULT;
    }
    return ret;
}

void ilitek_tddi_node_init(void)
{
    int i = 0, ret = 0;

    proc_dir_ilitek = proc_mkdir("ilitek", NULL);

    for (; i < ARRAY_SIZE(proc_table); i++) {
        proc_table[i].node = proc_create(proc_table[i].name, 0644, proc_dir_ilitek, proc_table[i].fops);

        if (proc_table[i].node == NULL) {
            proc_table[i].isCreated = false;
            ipio_err("Failed to create %s under /proc\n", proc_table[i].name);
            ret = -ENODEV;
        } else {
            proc_table[i].isCreated = true;
            TPD_INFO("Succeed to create %s under /proc\n", proc_table[i].name);
        }
    }
    netlink_init();
}

static int ilitek_tp_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    char apk_ret[100] = {0};

    if (!ts) {
        return 0;
    }
    TPD_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);

    TPD_INFO("enter %s\n", __func__);
    ilitek_tddi_mp_test_handler(apk_ret, s, NULL, ON);

    operate_mode_switch(ts);

    return 0;
}

static int ilitek_baseline_autotest_open(struct inode *inode, struct file *file)
{
    return single_open(file, ilitek_tp_auto_test_read_func, PDE_DATA(inode));
}

static const struct file_operations ilitek_tp_auto_test_proc_fops = {
    .owner = THIS_MODULE,
    .open  = ilitek_baseline_autotest_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/baseline_test
int ilitek_create_proc_for_oplus(struct touchpanel_data *ts)
{
    int ret = 0;
    // touchpanel_auto_test interface
    struct proc_dir_entry *prEntry_tmp = NULL;

    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp, &ilitek_tp_auto_test_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("Couldn't create proc entry\n");
    }
    return ret;
}
