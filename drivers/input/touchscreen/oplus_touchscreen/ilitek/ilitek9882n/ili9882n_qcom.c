/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "ili9882n.h"

/* Debug level */
bool ili_debug_en = DEBUG_OUTPUT;
EXPORT_SYMBOL(ili_debug_en);

struct ilitek_ts_data *ilits;
extern int tp_register_times;
extern void lcd_queue_load_tp_fw(void);
extern void __attribute__((weak)) switch_spi7cs_state(bool normal) {return;}

#if SPI_DMA_TRANSFER_SPLIT
#define DMA_TRANSFER_MAX_CHUNK      4   // number of chunks to be transferred.
#define DMA_TRANSFER_MAX_LEN        4096 // length of a chunk.

int ili_spi_write_then_read_split(struct spi_device *spi,
                                  const void *txbuf, unsigned n_tx,
                                  void *rxbuf, unsigned n_rx)
{
    struct spi_transfer xfer[DMA_TRANSFER_MAX_CHUNK];
    int status = -1, duplex_len = 0;
    int xfercnt = 0, xferlen = 0, xferloop = 0;
    int offset = 0;
    u8 cmd = 0;
    struct spi_message message;

    if ((n_tx > SPI_TX_BUF_SIZE) || (n_rx > SPI_RX_BUF_SIZE)) {
        ILI_ERR("Tx/Rx length is greater than spi local buf, abort\n");
        status = -ENOMEM;
        goto out;
    }

    spi_message_init(&message);
    memset(xfer, 0, sizeof(xfer));
    memset(ilits->spi_tx, 0x0, SPI_TX_BUF_SIZE);
    memset(ilits->spi_rx, 0x0, SPI_RX_BUF_SIZE);

    if ((n_tx > 0) && (n_rx > 0)) {
        cmd = SPI_READ;
    } else {
        cmd = SPI_WRITE;
    }

    switch (cmd) {
        case SPI_WRITE:
            if (n_tx % DMA_TRANSFER_MAX_LEN) {
                xferloop = (n_tx / DMA_TRANSFER_MAX_LEN) + 1;
            } else {
                xferloop = n_tx / DMA_TRANSFER_MAX_LEN;
            }

            if (xferloop > DMA_TRANSFER_MAX_CHUNK) {
                ILI_ERR("xferloop = %d > %d\n", xferloop, DMA_TRANSFER_MAX_CHUNK);
                status = -EINVAL;
                break;
            }

            xferlen = n_tx;
            memcpy(ilits->spi_tx, (u8 *)txbuf, xferlen);

            for (xfercnt = 0; xfercnt < xferloop; xfercnt++) {
                if (xferlen > DMA_TRANSFER_MAX_LEN) {
                    xferlen = DMA_TRANSFER_MAX_LEN;
                }

                xfer[xfercnt].len = xferlen;
                xfer[xfercnt].tx_buf = ilits->spi_tx + xfercnt * DMA_TRANSFER_MAX_LEN;
                spi_message_add_tail(&xfer[xfercnt], &message);
                xferlen = n_tx - (xfercnt + 1) * DMA_TRANSFER_MAX_LEN;
            }

            status = spi_sync(spi, &message);
            break;

        case SPI_READ:
            if (n_tx > DMA_TRANSFER_MAX_LEN) {
                ILI_ERR("Tx length must be lower than dma length (%d).\n", DMA_TRANSFER_MAX_LEN);
                status = -EINVAL;
                break;
            }

            if (!atomic_read(&ilits->ice_stat)) {
                offset = 2;
            }

            memcpy(ilits->spi_tx, txbuf, n_tx);
            duplex_len = n_tx + n_rx + offset;

            if (duplex_len % DMA_TRANSFER_MAX_LEN) {
                xferloop = (duplex_len / DMA_TRANSFER_MAX_LEN) + 1;
            } else {
                xferloop = duplex_len / DMA_TRANSFER_MAX_LEN;
            }

            if (xferloop > DMA_TRANSFER_MAX_CHUNK) {
                ILI_ERR("xferloop = %d > %d\n", xferloop, DMA_TRANSFER_MAX_CHUNK);
                status = -EINVAL;
                break;
            }

            xferlen = duplex_len;

            for (xfercnt = 0; xfercnt < xferloop; xfercnt++) {
                if (xferlen > DMA_TRANSFER_MAX_LEN) {
                    xferlen = DMA_TRANSFER_MAX_LEN;
                }

                xfer[xfercnt].len = xferlen;
                xfer[xfercnt].tx_buf = ilits->spi_tx;
                xfer[xfercnt].rx_buf = ilits->spi_rx + xfercnt * DMA_TRANSFER_MAX_LEN;
                spi_message_add_tail(&xfer[xfercnt], &message);
                xferlen = duplex_len - (xfercnt + 1) * DMA_TRANSFER_MAX_LEN;
            }

            status = spi_sync(spi, &message);

            if (status == 0) {
                ILI_DBG("ilits->spi_rx[1] 0x%X\n", ilits->spi_rx[1]);

                if (ilits->spi_rx[1] != SPI_ACK && !atomic_read(&ilits->ice_stat)) {
                    status = DO_SPI_RECOVER;
                    ILI_ERR("Do spi recovery: rxbuf[1] = 0x%x, ice = %d\n", ilits->spi_rx[1],
                            atomic_read(&ilits->ice_stat));
                    break;
                }

                memcpy((u8 *)rxbuf, ilits->spi_rx + offset + 1, n_rx);
            } else {
                ILI_ERR("spi read fail, status = %d\n", status);
                //status = DO_SPI_RECOVER;
            }

            break;

    }

out:
    return status;
}
#else
int ili_spi_write_then_read_direct(struct spi_device *spi,
                                   const void *txbuf, unsigned n_tx,
                                   void *rxbuf, unsigned n_rx)
{
    int status = -1, duplex_len = 0;
    int offset = 0;
    u8 cmd;
    struct spi_message message;
    struct spi_transfer xfer;

    if (n_rx > SPI_RX_BUF_SIZE) {
        ILI_ERR("Rx length is greater than spi local buf, abort\n");
        status = -ENOMEM;
        goto out;
    }

    spi_message_init(&message);
    memset(&xfer, 0, sizeof(xfer));

    if ((n_tx > 0) && (n_rx > 0)) {
        cmd = SPI_READ;
    } else {
        cmd = SPI_WRITE;
    }

    switch (cmd) {
        case SPI_WRITE:
            xfer.len = n_tx;
            xfer.tx_buf = txbuf;
            spi_message_add_tail(&xfer, &message);
            status = spi_sync(spi, &message);
            break;

        case SPI_READ:
            if (!atomic_read(&ilits->ice_stat)) {
                offset = 2;
            }

            duplex_len = n_tx + n_rx + offset;

            if ((duplex_len > SPI_TX_BUF_SIZE) ||
                (duplex_len > SPI_RX_BUF_SIZE)) {
                ILI_ERR("duplex_len is over than dma buf, abort\n");
                status = -ENOMEM;
                break;
            }

            memset(ilits->spi_tx, 0x0, SPI_TX_BUF_SIZE);
            memset(ilits->spi_rx, 0x0, SPI_RX_BUF_SIZE);
            xfer.len = duplex_len;
            memcpy(ilits->spi_tx, txbuf, n_tx);
            xfer.tx_buf = ilits->spi_tx;
            xfer.rx_buf = ilits->spi_rx;
            spi_message_add_tail(&xfer, &message);
            status = spi_sync(spi, &message);

            if (status == 0) {
                if (ilits->spi_rx[1] != SPI_ACK && !atomic_read(&ilits->ice_stat)) {
                    status = DO_SPI_RECOVER;
                    ILI_ERR("Do spi recovery: rxbuf[1] = 0x%x, ice = %d\n", ilits->spi_rx[1],
                            atomic_read(&ilits->ice_stat));
                    break;
                }

                memcpy((u8 *)rxbuf, ilits->spi_rx + offset + 1, n_rx);
            } else {
                ILI_ERR("spi read fail, status = %d\n", status);
                //status = DO_SPI_RECOVER;
            }

            break;

        default:
            ILI_INFO("Unknown command 0x%x\n", cmd);
            break;
    }

out:
    return status;
}
#endif

static int ili_spi_mp_pre_cmd(u8 cdc)
{
    u8 pre[5] = {0};

    if (!atomic_read(&ilits->mp_stat) || cdc != P5_X_SET_CDC_INIT ||
        ilits->chip->core_ver >= CORE_VER_1430) {
        return 0;
    }

    ILI_DBG("mp test with pre commands\n");
    pre[0] = SPI_WRITE;
    pre[1] = 0x0;// dummy byte
    pre[2] = 0x2;// write len byte
    pre[3] = P5_X_READ_DATA_CTRL;
    pre[4] = P5_X_GET_CDC_DATA;

    if (ilits->spi_write_then_read(ilits->spi, pre, 5, NULL, 0) < 0) {
        ILI_ERR("Failed to write pre commands\n");
        return -1;
    }

    pre[0] = SPI_WRITE;
    pre[1] = 0x0;// dummy byte
    pre[2] = 0x1;// write len byte
    pre[3] = P5_X_GET_CDC_DATA;

    if (ilits->spi_write_then_read(ilits->spi, pre, 4, NULL, 0) < 0) {
        ILI_ERR("Failed to write pre commands\n");
        return -1;
    }

    return 0;
}

static int ili_spi_pll_clk_wakeup(void)
{
    int index = 0;
    u8 wdata[32] = {0};
    u8 wakeup[9] = {0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3};
    u32 wlen = sizeof(wakeup);
    wdata[0] = SPI_WRITE;
    wdata[1] = wlen >> 8;
    wdata[2] = wlen & 0xff;
    index = 3;
    ipio_memcpy(&wdata[index], wakeup, wlen, wlen);
    ILI_INFO("Write dummy to wake up spi pll clk\n");
    wlen += index;

    if (ilits->spi_write_then_read(ilits->spi, wdata, wlen, NULL, 0) < 0) {
        ILI_INFO("spi slave write error\n");
        return -1;
    }

    return 0;
}

static int ili_spi_wrapper(u8 *txbuf, u32 wlen, u8 *rxbuf, u32 rlen, bool spi_irq, bool i2c_irq)
{
    int ret = 0;
    int mode = 0, index = 0;
    u8 wdata[128] = {0};
    u8 checksum = 0;
    bool ice = atomic_read(&ilits->ice_stat);

    if (wlen > 0) {
        if (!txbuf) {
            ILI_ERR("txbuf is null\n");
            return -ENOMEM;
        }

        /* 3 bytes data consist of length and header */
        if ((wlen + 3) > sizeof(wdata)) {
            ILI_ERR("WARNING! wlen(%d) > wdata(%d), using wdata length to transfer\n", wlen,
                    (int)sizeof(wdata));
            wlen = sizeof(wdata) - 3;
        }
    }

    if (rlen > 0) {
        if (!rxbuf) {
            ILI_ERR("rxbuf is null\n");
            return -ENOMEM;
        }
    }

    if (rlen > 0 && !wlen) {
        mode = SPI_READ;
    } else {
        mode = SPI_WRITE;
    }

    if (ilits->int_pulse) {
        ilits->detect_int_stat = ili_ic_check_int_pulse;
    } else {
        ilits->detect_int_stat = ili_ic_check_int_level;
    }

    if (spi_irq) {
        atomic_set(&ilits->cmd_int_check, ENABLE);
    }

    switch (mode) {
        case SPI_WRITE:
#if ( PLL_CLK_WAKEUP_TP_RESUME == ENABLE )
            if (ilits->pll_clk_wakeup == true)
#else
            if ((ilits->pll_clk_wakeup == true) && (ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE))
#endif
            {
                ret = ili_spi_pll_clk_wakeup();

                if (ret < 0) {
                    ILI_ERR("Wakeup pll clk error\n");
                    break;
                }
            }

            if (ice) {
                wdata[0] = SPI_WRITE;
                index = 1;
            } else {
                wdata[0] = SPI_WRITE;
                wdata[1] = wlen >> 8;
                wdata[2] = wlen & 0xff;
                index = 3;
            }

            ipio_memcpy(&wdata[index], txbuf, wlen, wlen);
            wlen += index;
            /*
            * NOTE: If TP driver is doing MP test and commanding 0xF1 to FW, we add a checksum
            * to the last index and plus 1 with size.
            */
            if (atomic_read(&ilits->mp_stat) && wdata[index] == P5_X_SET_CDC_INIT) {
                checksum = ili_calc_packet_checksum(&wdata[index], wlen - index);
                wdata[wlen] = checksum;
                wlen++;
                wdata[1] = (wlen - index) >> 8;
                wdata[2] = (wlen - index) & 0xff;
                ili_dump_data(wdata, 8, wlen, 0, "mp cdc cmd with checksum");
            }

            ret = ilits->spi_write_then_read(ilits->spi, wdata, wlen, txbuf, 0);

            if (ret < 0) {
                ILI_INFO("spi-wrapper write error\n");
                break;
            }

            /* Won't break if it needs to read data following with writing. */
            if (!rlen) {
                break;
            }

        case SPI_READ:
            if (!ice && spi_irq) {
                /* Check INT triggered by FW when sending cmds. */
                if (ilits->detect_int_stat(false) < 0) {
                    ILI_ERR("ERROR! Check INT timeout\n");
                    ret = -ETIME;
                    if (ilits->actual_tp_mode == P5_X_FW_TEST_MODE) {
                        break;
                    }
                }
            }

            ret = ili_spi_mp_pre_cmd(wdata[3]);

            if (ret < 0) {
                ILI_ERR("spi-wrapper mp pre cmd error\n");
            }

            wdata[0] = SPI_READ;
            ret = ilits->spi_write_then_read(ilits->spi, wdata, 1, rxbuf, rlen);

            if (ret < 0) {
                ILI_ERR("spi-wrapper read error\n");
            }

            break;
        default:
            ILI_ERR("Unknown spi mode (%d)\n", mode);
            ret = -EINVAL;
            break;
    }

    if (spi_irq) {
        atomic_set(&ilits->cmd_int_check, DISABLE);
    }

    return ret;
}

void ili_dump_data(void *data, int type, int len, int row_len, const char *name)
{
    int i, row = 31;
    u8 *p8 = NULL;
    s32 *p32 = NULL;
    s16 *p16 = NULL;

    if (!ili_debug_en) {
        return;
    }

    if (row_len > 0) {
        row = row_len;
    }

    if (data == NULL) {
        ILI_ERR("The data going to dump is NULL\n");
        return;
    }

    pr_cont("\n\n");
    pr_cont("ILITEK: Dump %s data\n", name);
    pr_cont("ILITEK: ");

    if (type == 8) {
        p8 = (u8 *) data;
    }

    if (type == 32 || type == 10) {
        p32 = (s32 *) data;
    }

    if (type == 16) {
        p16 = (s16 *) data;
    }

    for (i = 0; i < len; i++) {
        if (type == 8) {
            pr_cont(" %4x ", p8[i]);
        } else if (type == 32) {
            pr_cont(" %4x ", p32[i]);
        } else if (type == 10) {
            pr_cont(" %4d ", p32[i]);
        } else if (type == 16) {
            pr_cont(" %4d ", p16[i]);
        }

        if ((i % row) == row - 1) {
            pr_cont("\n");
            pr_cont("ILITEK: ");
        }
    }

    pr_cont("\n\n");
}

int ili_move_mp_code_iram(void)
{
    ILI_INFO("Download MP code to iram\n");
    return ili_fw_upgrade_handler();
}

int ili_proximity_near(int mode)
{
    int ret = 0;
    ilits->prox_near = true;

    switch (mode) {
        case DDI_POWER_ON:
            /*
             * If the power of VSP and VSN keeps alive when proximity near event
             * occures, TP can just go to sleep in.
             */
            ret = ili_ic_func_ctrl("sleep", SLEEP_IN);

            if (ret < 0) {
                ILI_ERR("Write sleep in cmd failed\n");
            }

            break;

        case DDI_POWER_OFF:
            ILI_INFO("DDI POWER OFF, do nothing\n");
            break;

        default:
            ILI_ERR("Unknown mode (%d)\n", mode);
            ret = -EINVAL;
            break;
    }

    return ret;
}

int ili_proximity_far(int mode)
{
    int ret = 0;
    u8 cmd[2] = {0};

    if (!ilits->prox_near) {
        ILI_INFO("No proximity near event, break\n");
        return 0;
    }

    switch (mode) {
        case WAKE_UP_GESTURE_RECOVERY:
            /*
             * If the power of VSP and VSN has been shut down previsouly,
             * TP should go through gesture recovery to get back.
             */
            ili_gesture_recovery();
            break;

        case WAKE_UP_SWITCH_GESTURE_MODE:
            /*
             * If the power of VSP and VSN keeps alive in the event of proximity near,
             * TP can be just recovered by switching gesture mode to get back.
             */
            cmd[0] = 0xF6;
            cmd[1] = 0x0A;
            ILI_INFO("write prepare gesture command 0xF6 0x0A\n");
            ret = ilits->wrapper(cmd, 2, NULL, 0, ON, OFF);

            if (ret < 0) {
                ILI_INFO("write prepare gesture command error\n");
                break;
            }

            ret = ili_switch_tp_mode(P5_X_FW_GESTURE_MODE);

            if (ret < 0) {
                ILI_ERR("Switch to gesture mode failed during proximity far\n");
            }

            break;

        default:
            ILI_ERR("Unknown mode (%d)\n", mode);
            ret = -EINVAL;
            break;
    }

    ilits->prox_near = false;
    return ret;
}

void ili_set_gesture_symbol(void)
{
    u8 cmd[7] = {0};
    struct gesture_symbol *ptr_sym = &ilits->ges_sym;
    u8 *ptr;
    ptr = (u8 *) ptr_sym;
    cmd[0] = P5_X_READ_DATA_CTRL;
    cmd[1] = 0x01;
    cmd[2] = 0x0A;
    cmd[3] = 0x08;
    cmd[4] = ptr[0];
    cmd[5] = ptr[1];
    cmd[6] = ptr[2];
    ili_dump_data(cmd, 8, sizeof(cmd), 0, "Gesture symbol");

    if (ilits->wrapper(cmd, 2, NULL, 0, ON, OFF) < 0) {
        ILI_ERR("Write pre cmd failed\n");
        return;
    }

    if (ilits->wrapper(&cmd[1], (sizeof(cmd) - 1), NULL, 0, ON, OFF)) {
        ILI_ERR("Write gesture symbol fail\n");
        return;
    }

    ILI_DBG(" double_tap = %d\n", ilits->ges_sym.double_tap);
    ILI_DBG(" alphabet_line_2_top = %d\n", ilits->ges_sym.alphabet_line_2_top);
    ILI_DBG(" alphabet_line_2_bottom = %d\n", ilits->ges_sym.alphabet_line_2_bottom);
    ILI_DBG(" alphabet_line_2_left = %d\n", ilits->ges_sym.alphabet_line_2_left);
    ILI_DBG(" alphabet_line_2_right = %d\n", ilits->ges_sym.alphabet_line_2_right);
    ILI_DBG(" alphabet_w = %d\n", ilits->ges_sym.alphabet_w);
    ILI_DBG(" alphabet_c = %d\n", ilits->ges_sym.alphabet_c);
    ILI_DBG(" alphabet_E = %d\n", ilits->ges_sym.alphabet_E);
    ILI_DBG(" alphabet_V = %d\n", ilits->ges_sym.alphabet_V);
    ILI_DBG(" alphabet_O = %d\n", ilits->ges_sym.alphabet_O);
    ILI_DBG(" alphabet_S = %d\n", ilits->ges_sym.alphabet_S);
    ILI_DBG(" alphabet_Z = %d\n", ilits->ges_sym.alphabet_Z);
    ILI_DBG(" alphabet_V_down = %d\n", ilits->ges_sym.alphabet_V_down);
    ILI_DBG(" alphabet_V_left = %d\n", ilits->ges_sym.alphabet_V_left);
    ILI_DBG(" alphabet_V_right = %d\n", ilits->ges_sym.alphabet_V_right);
    ILI_DBG(" alphabet_two_line_2_bottom= %d\n", ilits->ges_sym.alphabet_two_line_2_bottom);
    ILI_DBG(" alphabet_F= %d\n", ilits->ges_sym.alphabet_F);
    ILI_DBG(" alphabet_AT= %d\n", ilits->ges_sym.alphabet_AT);
}

int ili_move_gesture_code_iram(int mode)
{
    int i, ret = 0, timeout = 10;
    u8 cmd[2] = {0};
    u8 cmd_write[3] = {0x01, 0x0A, 0x05};
    /*
     * NOTE: If functions need to be added during suspend,
     * they must be called before gesture cmd reaches to FW.
     */
    ILI_INFO("Gesture code loaded by %s\n", ilits->gesture_load_code ? "driver" : "firmware");

    if (!ilits->gesture_load_code) {
        ret = ili_set_tp_data_len(mode, true, NULL);
        goto out;
    }

    /*pre-command for ili_ic_func_ctrl("lpwg", 0x3)*/
    cmd[0] = P5_X_READ_DATA_CTRL;
    cmd[1] = 0x1;
    ret = ilits->wrapper(cmd, sizeof(cmd), NULL, 0, OFF, OFF);

    if (ret < 0) {
        ILI_ERR("Write 0xF6,0x1 failed\n");
        goto out;
    }

    ret = ili_ic_func_ctrl("lpwg", 0x3);

    if (ret < 0) {
        ILI_ERR("write gesture flag failed\n");
        goto out;
    }

    ret = ili_set_tp_data_len(mode, true, NULL);

    if (ret < 0) {
        ILI_ERR("Failed to set tp data length\n");
        goto out;
    }

    /* Prepare Check Ready */
    cmd[0] = P5_X_READ_DATA_CTRL;
    cmd[1] = 0xA;
    ret = ilits->wrapper(cmd, sizeof(cmd), NULL, 0, OFF, OFF);

    if (ret < 0) {
        ILI_ERR("Write 0xF6,0xA failed\n");
        goto out;
    }

    ili_irq_enable();

    for (i = 0; i < timeout; i++) {
        /* Check ready for load code */
        ret = ilits->wrapper(cmd_write, sizeof(cmd_write), cmd, sizeof(u8), ON, OFF);
        ILI_DBG("gesture ready byte = 0x%x\n", cmd[0]);

        if (cmd[0] == 0x91) {
            ILI_INFO("Ready to load gesture code\n");
            break;
        }
    }

    ili_irq_disable();

    if (i >= timeout) {
        ILI_ERR("Gesture is not ready (0x%x), try to run its recovery\n", cmd[0]);
        return ili_gesture_recovery();
    }

    ret = ili_fw_upgrade_handler();

    if (ret < 0) {
        ILI_ERR("FW upgrade failed during moving code\n");
        goto out;
    }

    /* Resume gesture loader */
    ret = ili_ic_func_ctrl("lpwg", 0x6);

    if (ret < 0) {
        ILI_ERR("write resume loader error");
        goto out;
    }

out:
    return ret;
}

u8 ili_calc_packet_checksum(u8 *packet, int len)
{
    int i;
    s32 sum = 0;

    for (i = 0; i < len; i++) {
        sum += packet[i];
    }

    return (u8)((-sum) & 0xFF);
}

int ili_touch_esd_gesture_iram(void)
{
    int ret = 0, retry = 100;
    u32 answer = 0;
    int ges_pwd_addr = SPI_ESD_GESTURE_CORE146_PWD_ADDR;
    int ges_pwd = ESD_GESTURE_CORE146_PWD;
    int ges_run = SPI_ESD_GESTURE_CORE146_RUN;
    int pwd_len = 2;
    ret = ili_ice_mode_ctrl(ENABLE, OFF);

    if (ret < 0) {
        ILI_ERR("Enable ice mode failed during gesture recovery\n");
        return ret;
    }

    if (ilits->chip->core_ver < CORE_VER_1460) {
        if (ilits->chip->core_ver >= CORE_VER_1420) {
            ges_pwd_addr = I2C_ESD_GESTURE_PWD_ADDR;
        } else {
            ges_pwd_addr = SPI_ESD_GESTURE_PWD_ADDR;
        }

        ges_pwd = ESD_GESTURE_PWD;
        ges_run = SPI_ESD_GESTURE_RUN;
        pwd_len = 4;
    }

    ILI_INFO("ESD Gesture PWD Addr = 0x%X, PWD = 0x%X, GES_RUN = 0%X, core_ver = 0x%X\n",
             ges_pwd_addr, ges_pwd, ges_run, ilits->chip->core_ver);
    /* write a special password to inform FW go back into gesture mode */
    ret = ili_ice_mode_write(ges_pwd_addr, ges_pwd, pwd_len);

    if (ret < 0) {
        ILI_ERR("write password failed\n");
        goto out;
    }

    /* Host download gives effect to FW receives password successed */
    ilits->actual_tp_mode = P5_X_FW_AP_MODE;
    ret = ili_fw_upgrade_handler();

    if (ret < 0) {
        ILI_ERR("FW upgrade failed during gesture recovery\n");
        goto out;
    }

    /* Wait for fw running code finished. */
    if (ilits->info_from_hex || (ilits->chip->core_ver >= CORE_VER_1410)) {
        msleep(50);
    }

    ret = ili_ice_mode_ctrl(ENABLE, ON);

    if (ret < 0) {
        ILI_ERR("Enable ice mode failed during gesture recovery\n");
        goto fail;
    }

    /* polling another specific register to see if gesutre is enabled properly */
    do {
        ret = ili_ice_mode_read(ges_pwd_addr, &answer, pwd_len);

        if (ret < 0) {
            ILI_ERR("Read gesture answer error\n");
            goto fail;
        }

        if (answer != ges_run) {
            ILI_INFO("ret = 0x%X, answer = 0x%X\n", answer, ges_run);
        }

        mdelay(1);
    } while (answer != ges_run && --retry > 0);

    if (retry <= 0) {
        ILI_ERR("Enter gesture failed\n");
        ret = -1;
        goto fail;
    }

    ILI_INFO("Enter gesture successfully\n");
    ret = ili_ice_mode_ctrl(DISABLE, ON);

    if (ret < 0) {
        ILI_ERR("Disable ice mode failed during gesture recovery\n");
        goto fail;
    }

    ILI_INFO("Gesture code loaded by %s\n", ilits->gesture_load_code ? "driver" : "firmware");

    if (!ilits->gesture_load_code) {
        ilits->actual_tp_mode = P5_X_FW_GESTURE_MODE;
        ili_set_tp_data_len(ilits->gesture_mode, false, NULL);
        goto out;
    }

    /* Load gesture code */
    ilits->actual_tp_mode = P5_X_FW_GESTURE_MODE;
    ili_set_tp_data_len(ilits->gesture_mode, false, NULL);
    ret = ili_fw_upgrade_handler();

    if (ret < 0) {
        ILI_ERR("Failed to load code during gesture recovery\n");
        goto out;
    }

    /* Resume gesture loader */
    ret = ili_ic_func_ctrl("lpwg", 0x6);

    if (ret < 0) {
        ILI_ERR("write resume loader error");
        goto fail;
    }

out:
    return ret;
fail:
    ili_ice_mode_ctrl(DISABLE, ON);
    return ret;
}

#ifdef CONFIG_OPLUS_TP_APK
static void ili_write_log_buf(u8 main_id, u8 sec_id)
{
    log_buf_write(ilits->ts, main_id);
    sec_id = sec_id | 0x80;
    log_buf_write(ilits->ts, sec_id);
}


static void ili_demo_debug_info_id0(u8 *buf, int len)
{
    static struct ili_demo_debug_info_id0 id_last;
    struct ili_demo_debug_info_id0 id0;
    //ILI_INFO("id0 len = %d,strucy len = %ld", (int)len, sizeof(id0));
    ipio_memcpy(&id0, buf, sizeof(id0), len);

    if (id_last.sys_powr_state_e != id0.sys_powr_state_e) {
        ili_write_log_buf(1, id0.sys_powr_state_e);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("app_sys_powr_state_e = %d\n", id0.sys_powr_state_e);
        }
    }

    if (id_last.sys_state_e != id0.sys_state_e) {
        ili_write_log_buf(2, id0.sys_state_e);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("app_sys_state_e = %d\n", id0.sys_state_e);
        }
    }

    if (id_last.tp_state_e != id0.tp_state_e) {
        ili_write_log_buf(3, id0.tp_state_e);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("tp_state_e = %d\n", id0.tp_state_e);
        }
    }

    if (id_last.touch_palm_state != id0.touch_palm_state) {
        ili_write_log_buf(4, id0.touch_palm_state);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("touch_palm_state_e = %d\n", id0.touch_palm_state);
        }
    }

    if (id_last.app_an_statu_e != id0.app_an_statu_e) {
        ili_write_log_buf(5, id0.app_an_statu_e);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("app_an_statu_e = %d\n", id0.app_an_statu_e);
        }
    }

    if (id_last.app_sys_bg_err != id0.app_sys_bg_err) {
        ili_write_log_buf(6, id0.app_sys_bg_err);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("app_sys_check_bg_abnormal = %d\n", id0.app_sys_bg_err);
        }
    }

    if (id_last.g_b_wrong_bg != id0.g_b_wrong_bg) {
        ili_write_log_buf(7, id0.g_b_wrong_bg);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("g_b_wrong_bg = %d\n", id0.g_b_wrong_bg);
        }
    }

    if (id_last.reserved0 != id0.reserved0) {
        ili_write_log_buf(8, id0.reserved0);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("reserved0 = %d\n", id0.reserved0);
        }
    }

    if (id_last.normal_mode != id0.normal_mode) {
        if (id0.normal_mode) {
            log_buf_write(ilits->ts, 9);
        } else {
            log_buf_write(ilits->ts, 10);
        }
    }

    if (id_last.charger_mode != id0.charger_mode) {
        if (id0.charger_mode) {
            log_buf_write(ilits->ts, 11);
        } else {
            log_buf_write(ilits->ts, 12);
        }
    }

    if (id_last.glove_mode != id0.glove_mode) {
        if (id0.glove_mode) {
            log_buf_write(ilits->ts, 13);
        } else {
            log_buf_write(ilits->ts, 14);
        }
    }

    if (id_last.stylus_mode != id0.stylus_mode) {
        if (id0.stylus_mode) {
            log_buf_write(ilits->ts, 15);
        } else {
            log_buf_write(ilits->ts, 16);
        }
    }

    if (id_last.multi_mode != id0.multi_mode) {
        if (id0.multi_mode) {
            log_buf_write(ilits->ts, 17);
        } else {
            log_buf_write(ilits->ts, 18);
        }
    }

    if (id_last.noise_mode != id0.noise_mode) {
        if (id0.noise_mode) {
            log_buf_write(ilits->ts, 19);
        } else {
            log_buf_write(ilits->ts, 20);
        }
    }

    if (id_last.palm_plus_mode != id0.palm_plus_mode) {
        if (id0.palm_plus_mode) {
            log_buf_write(ilits->ts, 21);
        } else {
            log_buf_write(ilits->ts, 22);
        }
    }

    if (id_last.floating_mode != id0.floating_mode) {
        if (id0.floating_mode) {
            log_buf_write(ilits->ts, 23);
        } else {
            log_buf_write(ilits->ts, 24);
        }
    }

    if (tp_debug > 0 || ili_debug_en) {
        ILI_INFO("debug state is 0x%02X.\n", buf[3]);
    }

    if (id_last.algo_pt_status0 != id0.algo_pt_status0) {
        ili_write_log_buf(25, id0.algo_pt_status0);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status0 = %d\n", id0.algo_pt_status0);
        }
    }

    if (id_last.algo_pt_status1 != id0.algo_pt_status1) {
        ili_write_log_buf(26, id0.algo_pt_status1);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status1 = %d\n", id0.algo_pt_status1);
        }
    }

    if (id_last.algo_pt_status2 != id0.algo_pt_status2) {
        ili_write_log_buf(27, id0.algo_pt_status2);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status2 = %d\n", id0.algo_pt_status2);
        }
    }

    if (id_last.algo_pt_status3 != id0.algo_pt_status3) {
        ili_write_log_buf(28, id0.algo_pt_status3);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status3 = %d\n", id0.algo_pt_status3);
        }
    }

    if (id_last.algo_pt_status4 != id0.algo_pt_status4) {
        ili_write_log_buf(29, id0.algo_pt_status4);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status4 = %d\n", id0.algo_pt_status4);
        }
    }

    if (id_last.algo_pt_status5 != id0.algo_pt_status5) {
        ili_write_log_buf(30, id0.algo_pt_status5);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status5 = %d\n", id0.algo_pt_status5);
        }
    }

    if (id_last.algo_pt_status6 != id0.algo_pt_status6) {
        ili_write_log_buf(31, id0.algo_pt_status6);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status6 = %d\n", id0.algo_pt_status6);
        }
    }

    if (id_last.algo_pt_status7 != id0.algo_pt_status7) {
        ili_write_log_buf(32, id0.algo_pt_status7);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status7 = %d\n", id0.algo_pt_status7);
        }
    }

    if (id_last.algo_pt_status8 != id0.algo_pt_status8) {
        ili_write_log_buf(33, id0.algo_pt_status8);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status8 = %d\n", id0.algo_pt_status8);
        }
    }

    if (id_last.algo_pt_status9 != id0.algo_pt_status9) {
        ili_write_log_buf(34, id0.algo_pt_status9);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status9 = %d\n", id0.algo_pt_status9);
        }
    }

    if (id_last.reserved2 != id0.reserved2) {
        ili_write_log_buf(35, id0.reserved2);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("algo_pt_status9 = %d\n", id0.reserved2);
        }
    }

    if (id0.hopping_flg) {
        if (id_last.hopping_index != id0.hopping_index) {
            ili_write_log_buf(36, id0.hopping_index);

            if (tp_debug > 0 || ili_debug_en) {
                ILI_INFO("hopping_index = %d\n", id0.hopping_index);
                ILI_INFO("hopping_flg = %d\n", id0.hopping_flg);
                ILI_INFO("freq = %dK\n",
                         (id0.frequency_h << 8) + id0.frequency_l);
            }
        }
    }

    if (id_last.reserved3 != id0.reserved3) {
        ili_write_log_buf(37, id0.reserved3);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("reserved3 = %d\n", id0.reserved3);
        }
    }

    if (id_last.reserved4 != id0.reserved4) {
        ili_write_log_buf(38, id0.reserved4);

        if (tp_debug > 0 || ili_debug_en) {
            ILI_INFO("reserved4 = %d\n", id0.reserved4);
        }
    }

    ipio_memcpy(&id_last, &id0, sizeof(id_last), sizeof(id0));
}

#endif /*CONFIG_OPLUS_TP_APK*/

void ili_demo_debug_info_mode(u8 *buf, size_t len)
{
    u8 *info_ptr;
    u8 info_id, info_len;
    ili_report_ap_mode(buf, P5_X_DEMO_MODE_PACKET_LEN);
    info_ptr = buf + P5_X_DEMO_MODE_PACKET_LEN;
    info_len = info_ptr[0];
    info_id = info_ptr[1];
    ILI_INFO("info len = %d ,id = %d\n", info_len, info_id);

    //ilits->demo_debug_info[info_id](&info_ptr[1] , info_len);

    if (info_id == 0) {
#ifdef CONFIG_OPLUS_TP_APK
        ilits->demo_debug_info[info_id](&info_ptr[1], info_len);
#endif /*CONFIG_OPLUS_TP_APK*/
    } else {
        ILI_INFO("not support this id %d\n", info_id);
    }
}

static void ilitek_tddi_touch_send_debug_data(u8 *buf, int len)
{
    int index;
    mutex_lock(&ilits->debug_mutex);

    if (!ilits->netlink && !ilits->dnp) {
        goto out;
    }

    /* Send data to netlink */
    if (ilits->netlink) {
        ili_netlink_reply_msg(buf, len);
        goto out;
    }

    /* Sending data to apk via the node of debug_message node */
    if (ilits->dnp) {
        index = ilits->dbf;

        if (!ilits->dbl[ilits->dbf].mark) {
            ilits->dbf = ((ilits->dbf + 1) % TR_BUF_LIST_SIZE);
        } else {
            if (ilits->dbf == 0) {
                index = TR_BUF_LIST_SIZE - 1;
            } else {
                index = ilits->dbf - 1;
            }
        }

        if (ilits->dbl[index].data == NULL) {
            ILI_ERR("BUFFER %d error\n", index);
            goto out;
        }

        ipio_memcpy(ilits->dbl[index].data, buf, len, 2048);
        ilits->dbl[index].mark = true;
        wake_up_interruptible(&(ilits->inq));
        goto out;
    }

out:
    mutex_unlock(&ilits->debug_mutex);
}

void ili_report_ap_mode(u8 *buf, int len)
{
    int i = 0;
    u32 xop = 0, yop = 0;

    for (i = 0; i < MAX_TOUCH_NUM; i++) {
        if ((buf[(4 * i) + 1] == 0xFF) && (buf[(4 * i) + 2] == 0xFF)
            && (buf[(4 * i) + 3] == 0xFF)) {
            continue;
        }

        xop = (((buf[(4 * i) + 1] & 0xF0) << 4) | (buf[(4 * i) + 2]));
        yop = (((buf[(4 * i) + 1] & 0x0F) << 8) | (buf[(4 * i) + 3]));

        if (ilits->trans_xy) {
            ilits->points[i].x = xop;
            ilits->points[i].y = yop;
        } else {
            ilits->points[i].x = xop * ilits->panel_wid / TPD_WIDTH;
            ilits->points[i].y = yop * ilits->panel_hei / TPD_HEIGHT;
        }

        ilits->pointid_info = ilits->pointid_info | (1 << i);
        ilits->points[i].z = buf[(4 * i) + 4];
        ilits->points[i].width_major = buf[(4 * i) + 4];
        ilits->points[i].touch_major = buf[(4 * i) + 4];
        ilits->points[i].status = 1;
        ILI_DBG("original x = %d, y = %d p = %d\n", xop, yop, buf[(4 * i) + 4]);
    }

    ilitek_tddi_touch_send_debug_data(buf, len);
}

void ili_debug_mode_report_point(u8 *buf, int len)
{
    int i = 0;
    u32 xop = 0, yop = 0;
    static u8 p[MAX_TOUCH_NUM];

    for (i = 0; i < MAX_TOUCH_NUM; i++) {
        if ((buf[(3 * i)] == 0xFF) && (buf[(3 * i) + 1] == 0xFF)
            && (buf[(3 * i) + 2] == 0xFF)) {
            continue;
        }

        xop = (((buf[(3 * i)] & 0xF0) << 4) | (buf[(3 * i) + 1]));
        yop = (((buf[(3 * i)] & 0x0F) << 8) | (buf[(3 * i) + 2]));

        if (ilits->trans_xy) {
            ilits->points[i].x = xop;
            ilits->points[i].y = yop;
        } else {
            ilits->points[i].x = xop * ilits->panel_wid / TPD_WIDTH;
            ilits->points[i].y = yop * ilits->panel_hei / TPD_HEIGHT;
        }

        ilits->pointid_info = ilits->pointid_info | (1 << i);
        ilits->points[i].z = buf[(4 * i) + 4];
        ilits->points[i].status = 1;

        /*
        * Since there's no pressure data in debug mode, we make fake values
        * for android system if pressure needs to be reported.
        */
        if (p[i] == 1) {
            p[i] = 2;
        } else {
            p[i] = 1;
        }

        ilits->points[i].width_major = p[i];
        ilits->points[i].touch_major = p[i];
        ilits->points[i].z = p[i];
        ILI_DBG("original x = %d, y = %d p = %d\n", xop, yop, p[i]);
    }
}

void ili_report_debug_mode(u8 *buf, int len)
{
    ili_debug_mode_report_point(buf + 5, len);
    ilitek_tddi_touch_send_debug_data(buf, len);
}

void ili_report_debug_lite_mode(u8 *buf, int len)
{
    ili_debug_mode_report_point(buf + 4, len);
    ilitek_tddi_touch_send_debug_data(buf, len);
}

void ili_report_gesture_mode(u8 *buf, int len)
{
    int lu_x = 0, lu_y = 0, rd_x = 0, rd_y = 0, score = 0;
    u8 ges[P5_X_GESTURE_INFO_LENGTH] = {0};
    struct gesture_coordinate *gc = ilits->gcoord;
    bool transfer = ilits->trans_xy;

    ipio_memcpy(ges, buf, len, P5_X_GESTURE_INFO_LENGTH);

    memset(gc, 0x0, sizeof(struct gesture_coordinate));
#ifdef CONFIG_OPLUS_TP_APK

    if (ilits->debug_gesture_sta) {
        if (ilits->ts->gesture_buf) {
            int tmp_len = len ;

            if (tmp_len > P5_X_GESTURE_INFO_LENGTH) {
                tmp_len = P5_X_GESTURE_INFO_LENGTH;
            }

            memcpy(ilits->ts->gesture_buf, buf, tmp_len);
        }
    }

#endif /*CONFIG_OPLUS_TP_APK*/

    if (P5_X_GESTURE_FAIL_ID == ges[0]) {
        ILI_INFO("gesture fail reason code = 0x%02x", ges[1]);
        goto send_debug_data;
    }

    gc->code = ges[1];
    score = ges[36];
    ILI_INFO("gesture code = 0x%x, score = %d\n", gc->code, score);
    /* Parsing gesture coordinate */
    gc->pos_start.x = ((ges[4] & 0xF0) << 4) | ges[5];
    gc->pos_start.y = ((ges[4] & 0x0F) << 8) | ges[6];
    gc->pos_end.x   = ((ges[7] & 0xF0) << 4) | ges[8];
    gc->pos_end.y   = ((ges[7] & 0x0F) << 8) | ges[9];
    gc->pos_1st.x   = ((ges[16] & 0xF0) << 4) | ges[17];
    gc->pos_1st.y   = ((ges[16] & 0x0F) << 8) | ges[18];
    gc->pos_2nd.x   = ((ges[19] & 0xF0) << 4) | ges[20];
    gc->pos_2nd.y   = ((ges[19] & 0x0F) << 8) | ges[21];
    gc->pos_3rd.x   = ((ges[22] & 0xF0) << 4) | ges[23];
    gc->pos_3rd.y   = ((ges[22] & 0x0F) << 8) | ges[24];
    gc->pos_4th.x   = ((ges[25] & 0xF0) << 4) | ges[26];
    gc->pos_4th.y   = ((ges[25] & 0x0F) << 8) | ges[27];

    switch (gc->code) {
        case GESTURE_DOUBLECLICK:
            gc->type  = DouTap;
            gc->clockwise = 1;
            gc->pos_end.x = gc->pos_start.x;
            gc->pos_end.y = gc->pos_start.y;
            break;

        case GESTURE_LEFT:
            gc->type  = Right2LeftSwip;
            gc->clockwise = 1;
            break;

        case GESTURE_RIGHT:
            gc->type  = Left2RightSwip;
            gc->clockwise = 1;
            break;

        case GESTURE_UP:
            gc->type  = Down2UpSwip;
            gc->clockwise = 1;
            break;

        case GESTURE_DOWN:
            gc->type  = Up2DownSwip;
            gc->clockwise = 1;
            break;

        case GESTURE_O:
            gc->type  = Circle;
            gc->clockwise = (ges[34] > 1) ? 0 : ges[34];
            lu_x = (((ges[28] & 0xF0) << 4) | (ges[29]));
            lu_y = (((ges[28] & 0x0F) << 8) | (ges[30]));
            rd_x = (((ges[31] & 0xF0) << 4) | (ges[32]));
            rd_y = (((ges[31] & 0x0F) << 8) | (ges[33]));
            gc->pos_1st.x = ((rd_x + lu_x) / 2);
            gc->pos_1st.y = lu_y;
            gc->pos_2nd.x = lu_x;
            gc->pos_2nd.y = ((rd_y + lu_y) / 2);
            gc->pos_3rd.x = ((rd_x + lu_x) / 2);
            gc->pos_3rd.y = rd_y;
            gc->pos_4th.x = rd_x;
            gc->pos_4th.y = ((rd_y + lu_y) / 2);
            break;

        case GESTURE_W:
            gc->type  = Wgestrue;
            gc->clockwise = 1;
            break;

        case GESTURE_M:
            gc->type  = Mgestrue;
            gc->clockwise = 1;
            break;

        case GESTURE_V:
            gc->type  = UpVee;
            gc->clockwise = 1;
            break;

        case GESTURE_V_DOWN :
            gc->type  = DownVee;
            gc->clockwise = 1;
            break;

        case GESTURE_V_LEFT :
            gc->type  = LeftVee;
            gc->clockwise = 1;
            break;

        case GESTURE_V_RIGHT :
            gc->type  = RightVee;
            gc->clockwise = 1;
            break;

        case GESTURE_C:
            gc->type  = GESTURE_C;
            gc->clockwise = 1;
            break;

        case GESTURE_E:
            gc->type  = GESTURE_E;
            gc->clockwise = 1;
            break;

        case GESTURE_S:
            gc->type  = GESTURE_S;
            gc->clockwise = 1;
            break;

        case GESTURE_Z:
            gc->type  = GESTURE_Z;
            gc->clockwise = 1;
            break;

        case GESTURE_TWOLINE_DOWN:
            gc->type  = DouSwip;
            gc->clockwise = 1;
            gc->pos_1st.x  = (((ges[10] & 0xF0) << 4) | (ges[11]));
            gc->pos_1st.y  = (((ges[10] & 0x0F) << 8) | (ges[12]));
            gc->pos_2nd.x  = (((ges[13] & 0xF0) << 4) | (ges[14]));
            gc->pos_2nd.y  = (((ges[13] & 0x0F) << 8) | (ges[15]));
            break;

        default:
            ILI_ERR("Unknown gesture code\n");
            break;
    }

    if (!transfer) {
        gc->pos_start.x = gc->pos_start.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_start.y = gc->pos_start.y * ilits->panel_hei / TPD_HEIGHT;
        gc->pos_end.x   = gc->pos_end.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_end.y   = gc->pos_end.y * ilits->panel_hei / TPD_HEIGHT;
        gc->pos_1st.x   = gc->pos_1st.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_1st.y   = gc->pos_1st.y * ilits->panel_hei / TPD_HEIGHT;
        gc->pos_2nd.x   = gc->pos_2nd.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_2nd.y   = gc->pos_2nd.y * ilits->panel_hei / TPD_HEIGHT;
        gc->pos_3rd.x   = gc->pos_3rd.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_3rd.y   = gc->pos_3rd.y * ilits->panel_hei / TPD_HEIGHT;
        gc->pos_4th.x   = gc->pos_4th.x * ilits->panel_wid / TPD_WIDTH;
        gc->pos_4th.y   = gc->pos_4th.y * ilits->panel_hei / TPD_HEIGHT;
    }

    ILI_INFO("Transfer = %d, Type = %d, clockwise = %d\n", transfer, gc->type, gc->clockwise);
    ILI_INFO("Gesture Points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
             gc->pos_start.x, gc->pos_start.y,
             gc->pos_end.x, gc->pos_end.y,
             gc->pos_1st.x, gc->pos_1st.y,
             gc->pos_2nd.x, gc->pos_2nd.y,
             gc->pos_3rd.x, gc->pos_3rd.y,
             gc->pos_4th.x, gc->pos_4th.y);
send_debug_data:
    ilitek_tddi_touch_send_debug_data(buf, len);
}

void ili_report_i2cuart_mode(u8 *buf, int len)
{
    int type = buf[3] & 0x0F;
    int need_read_len = 0, one_data_bytes = 0;
    int actual_len = len - 5;
    int uart_len;
    u8 *uart_buf = NULL, *total_buf = NULL;
    ILI_DBG("data[3] = %x, type = %x, actual_len = %d\n",
            buf[3], type, actual_len);
    need_read_len = buf[1] * buf[2];

    if (type == 0 || type == 1 || type == 6) {
        one_data_bytes = 1;
    } else if (type == 2 || type == 3) {
        one_data_bytes = 2;
    } else if (type == 4 || type == 5) {
        one_data_bytes = 4;
    }

    need_read_len =  need_read_len * one_data_bytes + 1;
    ILI_DBG("need_read_len = %d  one_data_bytes = %d\n", need_read_len, one_data_bytes);

    if (need_read_len <= actual_len) {
        ilitek_tddi_touch_send_debug_data(buf, len);
        goto out;
    }

    uart_len = need_read_len - actual_len;
    ILI_DBG("uart len = %d\n", uart_len);
    uart_buf = kcalloc(uart_len, sizeof(u8), GFP_KERNEL);

    if (ERR_ALLOC_MEM(uart_buf)) {
        ILI_ERR("Failed to allocate uart_buf memory %ld\n", PTR_ERR(uart_buf));
        goto out;
    }

    if (ilits->wrapper(NULL, 0, uart_buf, uart_len, OFF, OFF) < 0) {
        ILI_ERR("i2cuart read data failed\n");
        goto out;
    }

    total_buf = kcalloc(len + uart_len, sizeof(u8), GFP_KERNEL);

    if (ERR_ALLOC_MEM(total_buf)) {
        ILI_ERR("Failed to allocate total_buf memory %ld\n", PTR_ERR(total_buf));
        goto out;
    }

    memcpy(total_buf, buf, len);
    memcpy(total_buf + len, uart_buf, uart_len);
    ilitek_tddi_touch_send_debug_data(total_buf, len + uart_len);
out:
    ili_kfree((void **)&uart_buf);
    ili_kfree((void **)&total_buf);
    return;
}

int ili_mp_test_handler(char *apk, bool lcm_on, struct seq_file *s, char *message)
{
    int ret = 0;
    //u32 reg_data = 0;

    if (atomic_read(&ilits->fw_stat)) {
        ILI_ERR("fw upgrade processing, ignore\n");
        return -EMP_FW_PROC;
    }

    atomic_set(&ilits->mp_stat, ENABLE);

    if (ilits->actual_tp_mode != P5_X_FW_TEST_MODE) {
        ret = ili_switch_tp_mode(P5_X_FW_TEST_MODE);

        if (ret < 0) {
            ILI_ERR("Switch MP mode failed\n");
            ret = -EMP_MODE;

            if (!ERR_ALLOC_MEM(message)) {
                snprintf(message, MESSAGE_SIZE, "Switch MP mode failed\n");
            }

            if (!ERR_ALLOC_MEM(s)) {
                seq_printf(s, "Switch MP mode failed\n");
            }

            goto out;
        }
        //wait mp test code run
    }

#if 0
   ret = ili_ice_mode_ctrl(ENABLE, ON);
   if (ret < 0)
      ILI_ERR("Enable ice mode failed while reading pc counter\n");
   ILI_INFO("0x51024 Write 0xB521\n");
   if (ili_ice_mode_write(0x51024, 0xB521, 2) < 0)
       ILI_ERR("0x51024 Write 0xB521 failed\n");
   if (ili_ice_mode_read(0x51024, &reg_data, sizeof(u32)) < 0)
      ILI_ERR("Read 0x51024 error\n");
   ILI_INFO("0x51024 read 0x%X\n", reg_data);
   if (ili_ice_mode_ctrl(DISABLE, ON) < 0)
      ILI_ERR("Disable ice mode failed\n");
#endif

    ret = ili_mp_test_main(apk, lcm_on, s, message);
out:
    ilits->actual_tp_mode = P5_X_FW_AP_MODE;

    /*
     * If there's running mp test with lcm off, we suspose that
     * users will soon call resume from suspend. TP mode will be changed
     * from MP to AP mode until resume finished.
     */
    if (!lcm_on) {
        atomic_set(&ilits->mp_stat, DISABLE);
        return ret;
    }

    if (ilits->fw_upgrade_mode == UPGRADE_IRAM) {
        if (ili_fw_upgrade_handler() < 0) {
            ILI_ERR("FW upgrade failed during mp test\n");
        }
    } else {
        if (ili_reset_ctrl(ilits->reset) < 0) {
            ILI_ERR("TP Reset failed during mp test\n");
        }
    }

    atomic_set(&ilits->mp_stat, DISABLE);
    return ret;
}

int ili_switch_tp_mode(u8 mode)
{
    int ret = 0;
    bool ges_dbg = false;
    atomic_set(&ilits->tp_sw_mode, START);
    ilits->actual_tp_mode = mode;

    /* able to see cdc data in gesture mode */
    if (ilits->tp_data_format == DATA_FORMAT_DEBUG &&
        ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE) {
        ges_dbg = true;
    }

    switch (ilits->actual_tp_mode) {
        case P5_X_FW_AP_MODE:
            ILI_INFO("Switch to AP mode\n");
            ilits->wait_int_timeout = AP_INT_TIMEOUT;

            if (ilits->fw_upgrade_mode == UPGRADE_IRAM) {
                if (ili_fw_upgrade_handler() < 0) {
                    ILI_ERR("FW upgrade failed\n");
                }
            } else {
                ret = ili_reset_ctrl(ilits->reset);
            }

            if (ret < 0) {
                ILI_ERR("TP Reset failed\n");
            }

            break;

        case P5_X_FW_GESTURE_MODE:
            ILI_INFO("Switch to Gesture mode\n");
            ilits->wait_int_timeout = AP_INT_TIMEOUT;
            ret = ili_move_gesture_code_iram(ilits->gesture_mode);

            if (ret < 0) {
                ILI_ERR("Move gesture code failed\n");
            }

            if (ges_dbg) {
                ILI_INFO("Enable gesture debug func\n");
                ili_set_tp_data_len(DATA_FORMAT_GESTURE_DEBUG, false, NULL);
            }

            break;

        case P5_X_FW_TEST_MODE:
            ILI_INFO("Switch to Test mode\n");
            ilits->wait_int_timeout = MP_INT_TIMEOUT;
            ret = ili_move_mp_code_iram();
            break;

        default:
            ILI_ERR("Unknown TP mode: %x\n", mode);
            ret = -1;
            break;
    }

    if (ret < 0) {
        ILI_ERR("Switch TP mode (%d) failed \n", mode);
    }

    ILI_DBG("Actual TP mode = %d\n", ilits->actual_tp_mode);
    atomic_set(&ilits->tp_sw_mode, END);
    return ret;
}

int ili_gesture_recovery(void)
{
    int ret = 0;
    //atomic_set(&ilits->esd_stat, START);
    ILI_INFO("Doing gesture recovery\n");
    ret = ili_touch_esd_gesture_iram();
    //atomic_set(&ilits->esd_stat, END);
    return ret;
}

void ili_spi_recovery(void)
{
    atomic_set(&ilits->esd_stat, START);
    tp_touch_btnkey_release();
    ILI_INFO("Doing spi recovery\n");

    if (ili_fw_upgrade_handler() < 0) {
        ILI_ERR("FW upgrade failed\n");
    }

    atomic_set(&ilits->esd_stat, END);
}

static int ili_esd_spi_check(void)
{
    int ret = 0;
    u8 tx = SPI_WRITE, rx = 0;
    ret = ilits->spi_write_then_read(ilits->spi, &tx, 1, &rx, 1);
    ILI_DBG("spi esd check ret = %d\n", ret);

    if (ret == DO_SPI_RECOVER) {
        ILI_ERR("ret = 0x%x\n", ret);
        return -1;
    }

    return 0;
}

static int ili_esd_check(struct work_struct *work)
{
    int ret = 0;

    if (ili_esd_spi_check() < 0) {
        ILI_ERR("SPI ACK failed, doing spi recovery\n");
        ili_spi_recovery();
        ret = -1;
    }

    return ret;
}

int ili_sleep_handler(int mode)
{
    int ret = 0;
    bool sense_stop = true;
    atomic_set(&ilits->tp_sleep, START);

    if (atomic_read(&ilits->fw_stat) ||
        atomic_read(&ilits->mp_stat)) {
        ILI_INFO("fw upgrade or mp still running, ignore sleep requst\n");
        atomic_set(&ilits->tp_sleep, END);
        return 0;
    }

    ILI_INFO("Sleep Mode = %d\n", mode);

    if (ilits->ss_ctrl) {
        sense_stop = true;
    } else if ((ilits->chip->core_ver >= CORE_VER_1430)) {
        sense_stop = false;
    } else {
        sense_stop = true;
    }

    switch (mode) {
        case TP_SUSPEND:
            ILI_INFO("TP suspend start\n");
            ilits->tp_suspend = true;

            if (sense_stop) {
                if (ili_ic_func_ctrl("sense", DISABLE) < 0) {
                    ILI_ERR("Write sense stop cmd failed\n");
                }

                if (ili_ic_check_busy(10, 20) < 0) {
                    ILI_ERR("Check busy timeout during suspend\n");
                }
            }

            if (ilits->gesture) {
                ili_switch_tp_mode(P5_X_FW_GESTURE_MODE);
            } else {
                if (ili_ic_func_ctrl("sleep", SLEEP_IN) < 0) {
                    ILI_ERR("Write sleep in cmd failed\n");
                }
            }

            ILI_INFO("TP suspend end\n");
            break;

        case TP_DEEP_SLEEP:
            ILI_INFO("TP deep suspend start\n");
            ilits->tp_suspend = true;

            if (sense_stop) {
                if (ili_ic_func_ctrl("sense", DISABLE) < 0) {
                    ILI_ERR("Write sense stop cmd failed\n");
                }

                if (ili_ic_check_busy(10, 20) < 0) {
                    ILI_ERR("Check busy timeout during deep suspend\n");
                }
            }

            if (ilits->gesture) {
                ili_switch_tp_mode(P5_X_FW_GESTURE_MODE);
            } else {
                if (ili_ic_func_ctrl("sleep", DEEP_SLEEP_IN) < 0) {
                    ILI_ERR("Write deep sleep in cmd failed\n");
                }
            }

            ILI_INFO("TP deep suspend end\n");
            break;

        case TP_RESUME:
#if !RESUME_BY_DDI
            ILI_INFO("TP resume start\n");
            /* Set tp as demo mode and reload code if it's iram. */
            ilits->actual_tp_mode = P5_X_FW_AP_MODE;

            if (ilits->fw_upgrade_mode == UPGRADE_IRAM) {
                if (ili_fw_upgrade_handler() < 0) {
                    ILI_ERR("FW upgrade failed during resume\n");
                }
            } else {
                if (ili_reset_ctrl(ilits->reset) < 0) {
                    ILI_ERR("TP Reset failed during resume\n");
                }
            }

            ilits->tp_suspend = false;
            ILI_INFO("TP resume end\n");
#endif
            break;

        default:
            ILI_ERR("Unknown sleep mode, %d\n", mode);
            ret = -EINVAL;
            break;
    }

    atomic_set(&ilits->tp_sleep, END);
    return ret;
}

int ili_fw_upgrade_handler(void)
{
    int ret = 0;
    atomic_set(&ilits->fw_stat, START);
    ilits->fw_update_stat = FW_STAT_INIT;
    ret = ili_fw_upgrade(ilits->fw_open);

    if (ret != 0) {
        ILI_INFO("FW upgrade fail\n");
        ilits->fw_update_stat = FW_UPDATE_FAIL;
    } else {
        ILI_INFO("FW upgrade pass\n");
        ilits->fw_update_stat = FW_UPDATE_PASS;
    }

    atomic_set(&ilits->fw_stat, END);
    return ret;
}

int ili_set_tp_data_len(int format, bool send, u8 *data)
{
    u8 cmd[10] = {0}, ctrl = 0, debug_ctrl = 0;
    u16 self_key = 2;
    int ret = 0, tp_mode = ilits->actual_tp_mode, len = 0;

    switch (format) {
        case DATA_FORMAT_DEMO:
        case DATA_FORMAT_GESTURE_DEMO:
            len = P5_X_DEMO_MODE_PACKET_LEN;
            ctrl = DATA_FORMAT_DEMO_CMD;
            break;

        case DATA_FORMAT_DEBUG:
        case DATA_FORMAT_GESTURE_DEBUG:
            len = (2 * ilits->xch_num * ilits->ych_num) + (ilits->stx * 2) + (ilits->srx * 2);
            len += 2 * self_key + (8 * 2) + 1 + 35;
            ctrl = DATA_FORMAT_DEBUG_CMD;
            break;

        case DATA_FORMAT_DEMO_DEBUG_INFO:
            /*only suport SPI interface now, so defult use size 1024 buffer*/
            len = P5_X_DEMO_MODE_PACKET_LEN +
                  P5_X_DEMO_DEBUG_INFO_ID0_LENGTH + P5_X_INFO_HEADER_LENGTH;
            ctrl = DATA_FORMAT_DEMO_DEBUG_INFO_CMD;
            break;

        case DATA_FORMAT_GESTURE_INFO:
            len = P5_X_GESTURE_INFO_LENGTH;
            ctrl = DATA_FORMAT_GESTURE_INFO_CMD;
            break;

        case DATA_FORMAT_GESTURE_NORMAL:
            len = P5_X_GESTURE_NORMAL_LENGTH;
            ctrl = DATA_FORMAT_GESTURE_NORMAL_CMD;
            break;

        case DATA_FORMAT_GESTURE_SPECIAL_DEMO:
            if (ilits->gesture_demo_ctrl == ENABLE) {
                if (ilits->gesture_mode == DATA_FORMAT_GESTURE_INFO) {
                    len = P5_X_GESTURE_INFO_LENGTH + P5_X_INFO_HEADER_LENGTH + P5_X_INFO_CHECKSUM_LENGTH;
                } else {
                    len = P5_X_DEMO_MODE_PACKET_LEN + P5_X_INFO_HEADER_LENGTH + P5_X_INFO_CHECKSUM_LENGTH;
                }
            } else {
                if (ilits->gesture_mode == DATA_FORMAT_GESTURE_INFO) {
                    len = P5_X_GESTURE_INFO_LENGTH;
                } else {
                    len = P5_X_GESTURE_NORMAL_LENGTH;
                }
            }

            ILI_INFO("Gesture demo mode control = %d\n",  ilits->gesture_demo_ctrl);
            ili_ic_func_ctrl("gesture_demo_en", ilits->gesture_demo_ctrl);
            ILI_INFO("knock_en setting\n");
            ili_ic_func_ctrl("knock_en", 0x8);
            break;

        case DATA_FORMAT_DEBUG_LITE_ROI:
            debug_ctrl = DATA_FORMAT_DEBUG_LITE_ROI_CMD;
            ctrl = DATA_FORMAT_DEBUG_LITE_CMD;
            break;

        case DATA_FORMAT_DEBUG_LITE_WINDOW:
            debug_ctrl = DATA_FORMAT_DEBUG_LITE_WINDOW_CMD;
            ctrl = DATA_FORMAT_DEBUG_LITE_CMD;
            break;

        case DATA_FORMAT_DEBUG_LITE_AREA:
            //if (cmd == NULL) {
            //    ILI_ERR("DATA_FORMAT_DEBUG_LITE_AREA error cmd\n");
            //    return -1;
            //}
            debug_ctrl = DATA_FORMAT_DEBUG_LITE_AREA_CMD;
            ctrl = DATA_FORMAT_DEBUG_LITE_CMD;
            cmd[3] = data[0];
            cmd[4] = data[1];
            cmd[5] = data[2];
            cmd[6] = data[3];
            break;

        default:
            ILI_ERR("Unknow TP data format\n");
            return -1;
    }

    if (ctrl == DATA_FORMAT_DEBUG_LITE_CMD) {
        len = P5_X_DEBUG_LITE_LENGTH;
        cmd[0] = P5_X_MODE_CONTROL;
        cmd[1] = ctrl;
        cmd[2] = debug_ctrl;
        ret = ilits->wrapper(cmd, 10, NULL, 0, ON, OFF);

        if (ret < 0) {
            ILI_ERR("switch to format %d failed\n", format);
            ili_switch_tp_mode(P5_X_FW_AP_MODE);
        }
    } else if (tp_mode == P5_X_FW_AP_MODE ||
               format == DATA_FORMAT_GESTURE_DEMO ||
               format == DATA_FORMAT_GESTURE_DEBUG ||
               format == DATA_FORMAT_DEMO) {
        cmd[0] = P5_X_MODE_CONTROL;
        cmd[1] = ctrl;
        ret = ilits->wrapper(cmd, 2, NULL, 0, ON, OFF);

        if (ret < 0) {
            ILI_ERR("switch to format %d failed\n", format);
            ili_switch_tp_mode(P5_X_FW_AP_MODE);
        }
    } else if (tp_mode == P5_X_FW_GESTURE_MODE) {
        /*set gesture symbol*/
        ili_set_gesture_symbol();

        if (send) {
            ret = ili_ic_func_ctrl("lpwg", ctrl);

            if (ret < 0) {
                ILI_ERR("write gesture mode failed\n");
            }
        }
    }

    ilits->tp_data_format = format;
    ilits->tp_data_len = len;
    ILI_INFO("TP mode = %d, format = %d, len = %d\n",
             tp_mode, ilits->tp_data_format, ilits->tp_data_len);
    return ret;
}

int ili_report_handler(void)
{
    int ret = 0, pid = 0;
    u8  checksum = 0, pack_checksum = 0;
    u8 *trdata = NULL;
    int rlen = 0;
    int tmp = ili_debug_en;

    /* Just in case these stats couldn't be blocked in top half context */
    if (!ilits->report || atomic_read(&ilits->tp_reset) ||
        atomic_read(&ilits->fw_stat) || atomic_read(&ilits->tp_sw_mode) ||
        atomic_read(&ilits->mp_stat) || atomic_read(&ilits->tp_sleep)) {
        ILI_INFO("ignore report request\n");
        return -1;
    }

    if (ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE) {
        __pm_stay_awake(ilits->ws);
    }

    rlen = ilits->tp_data_len;
    ILI_DBG("Packget length = %d\n", rlen);

    if (!rlen || rlen > TR_BUF_SIZE) {
        ILI_ERR("Length of packet is invaild\n");
        ret = -1;
        goto out;
    }

    memset(ilits->tr_buf, 0x0, TR_BUF_SIZE);
    ret = ilits->wrapper(NULL, 0, ilits->tr_buf, rlen, OFF, OFF);

    if (ret < 0) {
        ILI_ERR("Read report packet failed, ret = %d\n", ret);

        if (ret == DO_SPI_RECOVER) {
            ili_ic_get_pc_counter(DO_SPI_RECOVER);

            if (ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE && ilits->gesture && !ilits->prox_near) {
                ILI_ERR("Gesture failed, doing gesture recovery\n");

                if (ili_gesture_recovery() < 0) {
                    ILI_ERR("Failed to recover gesture\n");
                }
            } else {
                ILI_ERR("SPI ACK failed, doing spi recovery\n");
                ili_spi_recovery();
            }
        }

        goto out;
    }

    ili_dump_data(ilits->tr_buf, 8, rlen, 0, "finger report");
    checksum = ili_calc_packet_checksum(ilits->tr_buf, rlen - 1);
    pack_checksum = ilits->tr_buf[rlen - 1];
    trdata = ilits->tr_buf;
    pid = trdata[0];
    ILI_DBG("Packet ID = %x\n", pid);

    if (checksum != pack_checksum && pid != P5_X_I2CUART_PACKET_ID) {
        ILI_ERR("Checksum Error (0x%X)! Pack = 0x%X, len = %d\n", checksum, pack_checksum, rlen);
        ili_debug_en = DEBUG_ALL;
        ili_dump_data(trdata, 8, rlen, 0, "finger report with wrong");
        ili_debug_en = tmp;
        ret = -1;
        goto out;
    }

    if (pid == P5_X_INFO_HEADER_PACKET_ID) {
        trdata = ilits->tr_buf + P5_X_INFO_HEADER_LENGTH;
        pid = trdata[0];
    }

    switch (pid) {
        case P5_X_DEMO_PACKET_ID:
            ili_report_ap_mode(trdata, rlen);
            break;

        case P5_X_DEBUG_PACKET_ID:
            ili_report_debug_mode(trdata, rlen);
            break;

        case P5_X_DEBUG_LITE_PACKET_ID:
            ili_report_debug_lite_mode(trdata, rlen);
            break;

        case P5_X_I2CUART_PACKET_ID:
            ret = -1;
            ili_report_i2cuart_mode(trdata, rlen);
            break;

        case P5_X_GESTURE_PACKET_ID:
        case P5_X_GESTURE_FAIL_ID:
            ili_report_gesture_mode(trdata, rlen);
            break;

        case P5_X_DEMO_DEBUG_INFO_PACKET_ID:
            ili_demo_debug_info_mode(trdata, rlen);
            break;

        default:
            ILI_ERR("Unknown packet id, %x\n", pid);
            ret = -1;
            break;
    }

out:

    if (ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE) {
        __pm_relax(ilits->ws);
    }

    return ret;
}

int ili_reset_ctrl(int mode)
{
    int ret = 0;
    atomic_set(&ilits->tp_reset, START);

    switch (mode) {
        case TP_IC_CODE_RST:
            ILI_INFO("TP IC Code RST \n");
            ret = ili_ic_code_reset(OFF);

            if (ret < 0) {
                ILI_ERR("IC Code reset failed\n");
            }

            break;

        case TP_IC_WHOLE_RST:
            ILI_INFO("TP IC whole RST\n");
            ret = ili_ic_whole_reset(OFF);

            if (ret < 0) {
                ILI_ERR("IC whole reset failed\n");
            }

            break;

        case TP_HW_RST_ONLY:
            ILI_INFO("TP HW RST\n");
            ili_tp_reset();
            break;

        default:
            ILI_ERR("Unknown reset mode, %d\n", mode);
            ret = -EINVAL;
            break;
    }

    /*
     * Since OTP must be folloing with reset, except for code rest,
     * the stat of ice mode should be set as 0.
     */
    if (mode != TP_IC_CODE_RST) {
        atomic_set(&ilits->ice_stat, DISABLE);
    }

    ilits->tp_data_format = DATA_FORMAT_DEMO;
    ilits->tp_data_len = P5_X_DEMO_MODE_PACKET_LEN;
    ilits->pll_clk_wakeup = true;
    atomic_set(&ilits->tp_reset, END);
    return ret;
}

int ili_tddi_init(void)
{
    ILI_INFO("driver version = %s\n", DRIVER_VERSION);
    /* Must do hw reset once in first time for work normally if tp reset is avaliable */
#if !TDDI_RST_BIND

    if (ili_reset_ctrl(ilits->reset) < 0) {
        ILI_ERR("TP Reset failed during init\n");
    }

#endif
    ilits->tp_data_format = DATA_FORMAT_DEMO;

    /*
     * This status of ice enable will be reset until process of fw upgrade runs.
     * it might cause unknown problems if we disable ice mode without any
     * codes inside touch ic.
     */
    if (ili_ice_mode_ctrl(ENABLE, OFF) < 0) {
        ILI_ERR("Failed to enable ice mode during ili_tddi_init\n");
    }

    if (ili_ic_dummy_check() < 0) {
        ILI_ERR("Not found ilitek chip\n");
        //return -ENODEV;
    }

    if (ili_ic_get_info() < 0) {
        ILI_ERR("Chip info is incorrect\n");
    }

    ili_node_init();
    // update fw in probe
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

    if (ilits->ts->boot_mode == RECOVERY_BOOT
        || is_oem_unlocked() || ilits->ts->fw_update_in_probe_with_headfile) {
        if (ili_fw_upgrade_handler() < 0) {
            ILI_ERR("FW upgrade failed\n");
        }
    }

#else

    if (ilits->ts->boot_mode == MSM_BOOT_MODE__RECOVERY
        || is_oem_unlocked() || ilits->ts->fw_update_in_probe_with_headfile) {
        if (ili_fw_upgrade_handler() < 0) {
            ILI_ERR("FW upgrade failed\n");
        }
    }

#endif

#if 0 //LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	ilits->ws = wakeup_source_register("ili_wakelock");
#else
	ilits->ws = wakeup_source_register(ilits->dev, "ili_wakelock");
#endif

    if (!ilits->ws) {
        ILI_ERR("wakeup source request failed\n");
    }

    return 0;
}

int ili_core_spi_setup(int num)
{
    u32 freq[] = {
        TP_SPI_CLK_1M,
        TP_SPI_CLK_2M,
        TP_SPI_CLK_3M,
        TP_SPI_CLK_4M,
        TP_SPI_CLK_5M,
        TP_SPI_CLK_6M,
        TP_SPI_CLK_7M,
        TP_SPI_CLK_8M,
        TP_SPI_CLK_9M,
        TP_SPI_CLK_10M,
        TP_SPI_CLK_11M,
        TP_SPI_CLK_12M,
        TP_SPI_CLK_13M,
        TP_SPI_CLK_14M,
        TP_SPI_CLK_15M
    };

    if (num >= ARRAY_SIZE(freq)) {
        ILI_ERR("Invaild clk freq set default value\n");
        num = 7;
        //return -1;
    }

    ILI_INFO("spi clock = %d\n", freq[num]);
    ilits->spi->chip_select = 0; //modify reg=0 for more tp vendor share same spi interface
    ilits->spi->mode = SPI_MODE_0;
    ilits->spi->bits_per_word = 8;
    ilits->spi->max_speed_hz = freq[num];

    if (spi_setup(ilits->spi) < 0) {
        ILI_ERR("Failed to setup spi device\n");
        return -ENODEV;
    }

    ILI_INFO("name = %s, bus_num = %d,cs = %d, mode = %d, speed = %d\n",
             ilits->spi->modalias,
             ilits->spi->master->bus_num,
             ilits->spi->chip_select,
             ilits->spi->mode,
             ilits->spi->max_speed_hz);
    return 0;
}

void ili_tp_reset(void)
{
    ILI_INFO("edge delay = %d\n", ilits->rst_edge_delay);
    /* Need accurate power sequence, do not change it to msleep */
    gpio_direction_output(ilits->tp_rst, 1);
    mdelay(1);
    gpio_set_value(ilits->tp_rst, 0);
    mdelay(5);
    gpio_set_value(ilits->tp_rst, 1);
    mdelay(ilits->rst_edge_delay);
}

void ili_irq_disable(void)
{
    unsigned long flag;
    spin_lock_irqsave(&ilits->irq_spin, flag);

    if (atomic_read(&ilits->irq_stat) == DISABLE) {
        goto out;
    }

    if (!ilits->irq_num) {
        ILI_ERR("gpio_to_irq (%d) is incorrect\n", ilits->irq_num);
        goto out;
    }

    disable_irq_nosync(ilits->irq_num);
    atomic_set(&ilits->irq_stat, DISABLE);
    ILI_DBG("Disable irq success\n");
out:
    spin_unlock_irqrestore(&ilits->irq_spin, flag);
}

void ili_irq_enable(void)
{
    unsigned long flag;
    spin_lock_irqsave(&ilits->irq_spin, flag);

    if (atomic_read(&ilits->irq_stat) == ENABLE) {
        goto out;
    }

    if (!ilits->irq_num) {
        ILI_ERR("gpio_to_irq (%d) is incorrect\n", ilits->irq_num);
        goto out;
    }

    enable_irq(ilits->irq_num);
    atomic_set(&ilits->irq_stat, ENABLE);
    ILI_DBG("Enable irq success\n");
out:
    spin_unlock_irqrestore(&ilits->irq_spin, flag);
}

/*******Part4:Call Back Function implement*******/
static int ilitek_ftm_process(void *chip_data)
{
    int ret = -1;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("\n");
    chip_info->hw_res->reset_gpio = chip_info->ts->hw_res.reset_gpio;
    chip_info->tp_int = chip_info->ts->hw_res.irq_gpio;
    chip_info->tp_rst = chip_info->ts->hw_res.reset_gpio;
    mutex_lock(&chip_info->touch_mutex);
    chip_info->actual_tp_mode = P5_X_FW_AP_MODE;

    if (!TDDI_RST_BIND) {
        ili_reset_ctrl(chip_info->reset);
    }

    ili_ice_mode_ctrl(ENABLE, OFF);

    if (ili_ic_get_info() < 0) {
        ILI_ERR("Not found ilitek chips\n");
    }

    ret = ili_fw_upgrade_handler();

    if (ret < 0) {
        ILI_INFO("Failed to upgrade firmware, ret = %d\n", ret);
    }

    ILI_INFO("FTM tp enter sleep\n");
    /*ftm sleep in */
    ili_ic_func_ctrl("sleep", SLEEP_IN_FTM_BEGIN);
    mutex_unlock(&chip_info->touch_mutex);
    return ret;
}

static void ilitek_ftm_process_extra(void)
{
    int ret = 0;

    if (ilits != NULL && ilits->ts != NULL) {
        ILI_INFO("ilits->ts->boot_mode = %d\n", ilits->ts->boot_mode);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

        if ((ilits->ts->boot_mode == META_BOOT || ilits->ts->boot_mode == FACTORY_BOOT))
#else
        if ((ilits->ts->boot_mode == MSM_BOOT_MODE__FACTORY ||
             ilits->ts->boot_mode == MSM_BOOT_MODE__RF ||
             ilits->ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
        {
            mutex_lock(&ilits->touch_mutex);
            ilits->actual_tp_mode = P5_X_FW_AP_MODE;
            ret = ili_fw_upgrade_handler();
            mutex_unlock(&ilits->touch_mutex);

            if (ret < 0) {
                ILI_INFO("Failed to upgrade firmware, ret = %d\n", ret);
            }

            mutex_lock(&ilits->touch_mutex);
            ili_ic_func_ctrl("sense", DISABLE);
            //ili_ic_check_busy(5, 35);
            msleep(10);
            ili_ic_func_ctrl("sleep", DEEP_SLEEP_IN);
            mutex_unlock(&ilits->touch_mutex);
            msleep(60);
            ILI_INFO("mdelay 60 ms test for ftm wait sleep\n");
        }
    }
}

static void ilitek_reset_queue_work_prepare(void)
{
    ILI_INFO("\n");
    mutex_lock(&ilits->touch_mutex);
    atomic_set(&ilits->fw_stat, ENABLE);
    ili_reset_ctrl(ilits->reset);
    ilits->ignore_first_irq = true;
    ili_ice_mode_ctrl(ENABLE, OFF);
    ilits->ddi_rest_done = true;
    //mdelay(5);
    mutex_unlock(&ilits->touch_mutex);
}

static int ilitek_reset(void *chip_data)
{
    int ret = -1;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;

	ILI_INFO("%s.\n", __func__);

    mutex_lock(&chip_info->touch_mutex);
    ilits->tp_suspend = false;
    chip_info->actual_tp_mode = P5_X_FW_AP_MODE;
    ret = ili_fw_upgrade_handler();

    if (ret < 0) {
        ILI_ERR("Failed to upgrade firmware, ret = %d\n", ret);
    }

    mutex_unlock(&chip_info->touch_mutex);
    return 0;
}

static int ilitek_power_control(void *chip_data, bool enable)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("set reset pin low\n");

    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
    }

    return 0;
}

static int ilitek_get_chip_info(void *chip_data)
{
    ILI_INFO("\n");
    return 0;
}

static u8 ilitek_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    int ret = 0;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    chip_info->pointid_info = 0;
    chip_info->irq_timer = jiffies;    //reset esd check trigger base time
    memset(chip_info->points, 0, sizeof(struct point_info) * 10);
    memset(chip_info->gcoord, 0x0, sizeof(struct gesture_coordinate));

    if (atomic_read(&ilits->cmd_int_check) == ENABLE) {
        atomic_set(&ilits->cmd_int_check, DISABLE);
        ILI_INFO("CMD INT detected, ignore\n");
        wake_up_interruptible(&(ilits->inq));
        return IRQ_IGNORE;
    }

    /*ignore first irq after hw rst pin reset*/
    if (ilits->ignore_first_irq) {
        ILI_INFO("ignore_first_irq\n");
        ilits->ignore_first_irq = false;
        return IRQ_IGNORE;
    }

    if (!chip_info->fw_update_stat || !ilits->report || atomic_read(&ilits->tp_reset) ||
        atomic_read(&ilits->fw_stat) || atomic_read(&ilits->tp_sw_mode) ||
        atomic_read(&ilits->mp_stat) || atomic_read(&ilits->tp_sleep) ||
        atomic_read(&ilits->esd_stat)) {
        ILI_INFO("ignore interrupt !\n");
        return IRQ_IGNORE;
    }

    if ((gesture_enable == 1) && is_suspended) {
        //mdelay(40);
        mutex_lock(&ilits->touch_mutex);
        ret = ili_report_handler();
        mutex_unlock(&ilits->touch_mutex);

        if (ret < 0 || (chip_info->gcoord->code == 0)) {
            return IRQ_IGNORE;
        }

        return IRQ_GESTURE;
    } else if (is_suspended) {
        return IRQ_IGNORE;
    }

    mutex_lock(&ilits->touch_mutex);
    ret = ili_report_handler();
    mutex_unlock(&ilits->touch_mutex);

    if (ret < 0) {
        ILI_ERR("get point info error ignore\n");

        if (ret == DO_SPI_RECOVER) {
            ILI_ERR("restore status for esd recovery\n");
            operate_mode_switch(ilits->ts);
        }

        return IRQ_IGNORE;
    }

    return IRQ_TOUCH;
}

static int ilitek_get_touch_points(void *chip_data, struct point_info *points,
                                   int max_num)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    memcpy(points, chip_info->points, sizeof(struct point_info) * MAX_TOUCH_NUM);
    return chip_info->pointid_info;
}

static int ilitek_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    gesture->clockwise = ilits->gcoord->clockwise;
    gesture->gesture_type = ilits->gcoord->type;
    gesture->Point_start.x = ilits->gcoord->pos_start.x;
    gesture->Point_start.y = ilits->gcoord->pos_start.y;
    gesture->Point_end.x = ilits->gcoord->pos_end.x;
    gesture->Point_end.y = ilits->gcoord->pos_end.y;
    gesture->Point_1st.x = ilits->gcoord->pos_1st.x;
    gesture->Point_1st.y = ilits->gcoord->pos_1st.y;
    gesture->Point_2nd.x = ilits->gcoord->pos_2nd.x;
    gesture->Point_2nd.y = ilits->gcoord->pos_2nd.y;
    gesture->Point_3rd.x = ilits->gcoord->pos_3rd.x;
    gesture->Point_3rd.y = ilits->gcoord->pos_3rd.y;
    gesture->Point_4th.x = ilits->gcoord->pos_4th.x;
    gesture->Point_4th.y = ilits->gcoord->pos_4th.y;
    return 0;
}

static int ilitek_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = 0;
    uint8_t temp[64] = {0};
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;

    if (!chip_info->fw_update_stat || atomic_read(&chip_info->tp_reset)
        || atomic_read(&chip_info->fw_stat)
        || atomic_read(&chip_info->tp_sw_mode)
        || atomic_read(&chip_info->mp_stat)
        || atomic_read(&chip_info->tp_sleep)
        || atomic_read(&chip_info->esd_stat)) {
        ILI_INFO("doing other process!\n");
        return ret;
    }

    mutex_lock(&chip_info->touch_mutex);

    switch (mode) {
        case MODE_NORMAL:
            ILI_DBG("MODE_NORMAL flag = %d\n", flag);
            ret = 0;
            break;

        case MODE_SLEEP:
            ILI_INFO("MODE_SLEEP flag = %d\n", flag);

            if (chip_info->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
                ili_sleep_handler(TP_DEEP_SLEEP);
            } else {
                ili_proximity_near(DDI_POWER_ON);
            }

            break;

        case MODE_GESTURE:
            ILI_INFO("MODE_GESTURE flag = %d\n", flag);

            if (chip_info->sleep_type == DEEP_SLEEP_IN) {
                ILI_INFO("TP in deep sleep not support gesture flag = %d\n", flag);
                break;
            }

            chip_info->gesture = flag;

            if (flag) {
                if (chip_info->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
                    ili_sleep_handler(TP_SUSPEND);
                } else {
                    ili_proximity_far(WAKE_UP_SWITCH_GESTURE_MODE);
                }

#ifdef CONFIG_OPLUS_TP_APK

                if (chip_info->debug_gesture_sta) {
                    ili_gesture_fail_reason(ENABLE);
                }

#endif /*CONFIG_OPLUS_TP_APK*/
                chip_info->actual_tp_mode = P5_X_FW_GESTURE_MODE;
            }

            break;

        case MODE_EDGE:
            ILI_INFO("MODE_EDGE flag = %d\n", flag);

            if (flag || (VERTICAL_SCREEN == chip_info->touch_direction)) {
                temp[0] = 0x01;
            } else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
                temp[0] = 0x00;
            } else if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
                temp[0] = 0x02;
            }

            if (ili_ic_func_ctrl("edge_palm", temp[0]) < 0) {
                ILI_ERR("write edge_palm flag failed\n");
            }

            break;

        case MODE_HEADSET:
            ILI_INFO("MODE_HEADSET flag = %d\n", flag);

            if (ili_ic_func_ctrl("ear_phone", flag) < 0) {
                ILI_ERR("write ear_phone flag failed\n");
            }

            break;

        case MODE_CHARGE:
            ILI_INFO("MODE_CHARGE flag = %d\n", flag);

            if (ili_ic_func_ctrl("plug", !flag) < 0) {
                ILI_ERR("write plug flag failed\n");
            }

            break;

        case MODE_GAME:
            ILI_INFO("MODE_GAME flag = %d\n", flag);

            if (ili_ic_func_ctrl("lock_point", !flag) < 0) {
                ILI_ERR("write lock_point flag failed\n");
            }

            break;

        default:
            ILI_INFO("%s: Wrong mode.\n", __func__);
    }

    mutex_unlock(&chip_info->touch_mutex);
    return ret;
}

static fw_check_state ilitek_fw_check(void *chip_data,
                                      struct resolution_info *resolution_info,
                                      struct              panel_info *panel_data)
{
    int ret = 0;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("%s: call\n", __func__);
    mutex_lock(&chip_info->touch_mutex);
    ret = ili_ic_get_fw_ver();
    mutex_unlock(&chip_info->touch_mutex);

    if (ret < 0) {
        ILI_ERR("%s: get fw info failed\n", __func__);
    } else {
        panel_data->TP_FW = (chip_info->chip->fw_ver >> 8) & 0xFF;
        ILI_INFO("firmware_ver = %02X\n", panel_data->TP_FW);
    }

    return FW_NORMAL;
}

static void copy_fw_to_buffer(struct ilitek_ts_data *chip_info,
                              const struct firmware *fw)
{
    if (fw) {
        //free already exist fw data buffer
        ili_vfree((void **) & (chip_info->tp_fw.data));
        chip_info->tp_fw.size = 0;
        //new fw data buffer
        chip_info->tp_fw.data = vmalloc(fw->size);

        if (chip_info->tp_fw.data == NULL) {
            ILI_ERR("vmalloc tp firmware data error\n");
            chip_info->tp_fw.data = vmalloc(fw->size);

            if (chip_info->tp_fw.data == NULL) {
                ILI_ERR("retry kmalloc tp firmware data error\n");
                return;
            }
        }

        //copy bin fw to data buffer
        memcpy((u8 *)chip_info->tp_fw.data, (u8 *)(fw->data), fw->size);
        ILI_INFO("copy_fw_to_buffer fw->size=%zu\n", fw->size);
        chip_info->tp_fw.size = fw->size;
    }

    return;
}

static fw_update_state ilitek_fw_update(void *chip_data,
                                        const struct firmware *fw,
                                        bool force)
{
    int ret = 0;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    static bool get_ic_info_flag = true;
    ILI_INFO("%s start\n", __func__);

    //request firmware failed, get from headfile
    if (fw == NULL) {
        ILI_INFO("request firmware failed\n");
    }

    mutex_lock(&chip_info->touch_mutex);
    copy_fw_to_buffer(chip_info, fw);

    if (get_ic_info_flag) {
        /* Must do hw reset once in first time for work normally if tp reset is avaliable */
        if (!TDDI_RST_BIND) {
            ili_reset_ctrl(chip_info->reset);
        }

        ili_ice_mode_ctrl(ENABLE, OFF);

        if (ili_ic_get_info() < 0) {
            ILI_ERR("Not found ilitek chips\n");
        }

        get_ic_info_flag = false;
    }

    chip_info->actual_tp_mode = P5_X_FW_AP_MODE;
    ilits->oplus_fw_update = true;
    ret = ili_fw_upgrade_handler();
    ilits->oplus_fw_update = false;
    mutex_unlock(&chip_info->touch_mutex);

    if (ret < 0) {
        ILI_ERR("Failed to upgrade firmware, ret = %d\n", ret);
        return ret;
    }

    return FW_UPDATE_SUCCESS;
}

static int ilitek_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    //int len = 0;
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    /*len = strlen(panel_data->fw_name);
    if (len > 3) {
        panel_data->fw_name[len - 3] = 'b';
        panel_data->fw_name[len - 2] = 'i';
        panel_data->fw_name[len - 1] = 'n';
    }
    len = strlen(panel_data->test_limit_name);
    if ((len > 3)) {
        panel_data->test_limit_name[len - 3] = 'i';
        panel_data->test_limit_name[len - 2] = 'n';
        panel_data->test_limit_name[len - 1] = 'i';
    }*/
    chip_info->tp_type = panel_data->tp_type;
    /*get ftm firmware ini from touch.h*/
    chip_info->p_firmware_headfile = &panel_data->firmware_headfile;
    ILI_INFO("chip_info->tp_type = %d, "
             "panel_data->test_limit_name = %s, panel_data->fw_name = %s\n",
             chip_info->tp_type,
             panel_data->test_limit_name, panel_data->fw_name);
    return 0;
}

static void ilitek_black_screen_test(void *chip_data, char *message)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("enter %s\n", __func__);
    mutex_lock(&chip_info->touch_mutex);
    ili_mp_test_handler(NULL, OFF, NULL, message);
    mutex_unlock(&chip_info->touch_mutex);
}

static int ilitek_esd_handle(void *chip_data)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    unsigned int timer = jiffies_to_msecs(jiffies - chip_info->irq_timer);
    int ret = 0;
    chip_info->irq_timer = jiffies;
    mutex_lock(&chip_info->touch_mutex);

    if ((chip_info->esd_check_enabled)
        && (timer >= WQ_ESD_DELAY)
        && chip_info->fw_update_stat) {
        ret = ili_esd_check(NULL);
    } else {
        ILI_DBG("Undo esd_check = %d timer = %d chip_info->fw_update_stat = %d\n", \
                chip_info->esd_check_enabled, timer, chip_info->fw_update_stat);
    }

    mutex_unlock(&chip_info->touch_mutex);
    return ret;
}

static void ilitek_set_touch_direction(void *chip_data, u8 dir)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    chip_info->touch_direction = dir;
}

static u8 ilitek_get_touch_direction(void *chip_data)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->touch_direction;
}

static bool ilitek_irq_throw_away(void *chip_data)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_DBG("\n");

    if ((ilits->actual_tp_mode == P5_X_FW_TEST_MODE)
        || atomic_read(&chip_info->cmd_int_check) == ENABLE) {
        atomic_set(&chip_info->cmd_int_check, DISABLE);
        wake_up_interruptible(&(chip_info->inq));
        ILI_INFO("CMD INT detected, wake up the throw away irq!\n");
        return true;
    }

    return false;
}

static int ilitek_reset_gpio_control(void *chip_data, bool enable)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        ILI_INFO("%s: set reset state %d\n", __func__, enable);
        gpio_set_value(chip_info->hw_res->reset_gpio, enable);
    }

    return 0;
}

static struct oplus_touchpanel_operations ilitek_ops = {
    .ftm_process                = ilitek_ftm_process,
    .ftm_process_extra          = ilitek_ftm_process_extra,
    .reset                      = ilitek_reset,
    .power_control              = ilitek_power_control,
    .get_chip_info              = ilitek_get_chip_info,
    .trigger_reason             = ilitek_trigger_reason,
    .get_touch_points           = ilitek_get_touch_points,
    .get_gesture_info           = ilitek_get_gesture_info,
    .mode_switch                = ilitek_mode_switch,
    .fw_check                   = ilitek_fw_check,
    .fw_update                  = ilitek_fw_update,
    .get_vendor                 = ilitek_get_vendor,
    .black_screen_test          = ilitek_black_screen_test,
    .esd_handle                 = ilitek_esd_handle,
    .reset_gpio_control         = ilitek_reset_gpio_control,
    .set_touch_direction        = ilitek_set_touch_direction,
    .get_touch_direction        = ilitek_get_touch_direction,
    .tp_queue_work_prepare      = ilitek_reset_queue_work_prepare,
    .tp_irq_throw_away          = ilitek_irq_throw_away,
};

static int ilitek_read_debug_data(struct seq_file *s,
                                  struct ilitek_ts_data *chip_info,
                                  u8 read_type)
{
    int ret;
    u8 test_cmd[4] = { 0 };
    int i = 0;
    int j = 0;
    int xch = ilits->xch_num;
    int ych = ilits->ych_num;
    u8 *buf = ilits->tr_buf;

    if (ERR_ALLOC_MEM(buf)) {
        ILI_ERR("Failed to allocate packet memory, %ld\n", PTR_ERR(buf));
        return -1;
    }

    mutex_lock(&ilits->touch_mutex);
    ret = ili_set_tp_data_len(DATA_FORMAT_DEBUG, false, NULL);

    if (ret < 0) {
        ILI_ERR("Failed to switch debug mode\n");
        seq_printf(s, "get data failed\n");
        mutex_unlock(&ilits->touch_mutex);
        ili_kfree((void **)&buf);
        return -1;
    }

    test_cmd[0] = 0xFA;
    test_cmd[1] = read_type;
    ILI_INFO("debug cmd 0x%X, 0x%X", test_cmd[0], test_cmd[1]);
    ret = ilits->wrapper(test_cmd, 2, NULL, 0, ON, OFF);
    atomic_set(&ilits->cmd_int_check, ENABLE);
    enable_irq(ilits->irq_num);//because oplus disable

    for (i = 0; i < 10; i++) {
        int rlen = 0;
        ret = ilits->detect_int_stat(false);
        rlen = ilits->tp_data_len;
        ILI_INFO("Packget length = %d\n", rlen);
        ret = ilits->wrapper(NULL, 0, buf, rlen, OFF, OFF);

        if (ret < 0 || rlen < 0 || rlen >= TR_BUF_SIZE) {
            ILI_ERR("Length of packet is invaild\n");
            continue;
        }

        if (buf[0] == P5_X_DEBUG_PACKET_ID) {
            break;
        }

        atomic_set(&ilits->cmd_int_check, DISABLE);
    }

    disable_irq_nosync(ilits->irq_num);

    switch (read_type) {
        case P5_X_FW_RAW_DATA_MODE:
            seq_printf(s, "raw_data:\n");
            break;

        case P5_X_FW_DELTA_DATA_MODE:
            seq_printf(s, "diff_data:\n");
            break;

        default:
            seq_printf(s, "read type not support\n");
            break;
    }

    if (i < 10) {
        for (i = 0; i < ych; i++) {
            seq_printf(s, "[%2d]", i);

            for (j = 0; j < xch; j++) {
                s16 temp;
                temp = (s16)((buf[(i * xch + j) * 2 + 35] << 8)
                             + buf[(i * xch + j) * 2 + 35 + 1]);
                seq_printf(s, "%5d,", temp);
            }

            seq_printf(s, "\n");
        }

        for (i = 0; i < MAX_TOUCH_NUM; i++) {
            if ((buf[(3 * i) + 5] != 0xFF) || (buf[(3 * i) + 6] != 0xFF)
                || (buf[(3 * i) + 7] != 0xFF)) {
                break; // has touch
            }
        }

        if (i == MAX_TOUCH_NUM) {
            tp_touch_btnkey_release();
        }
    } else {
        seq_printf(s, "get data failed\n");
    }

    /* change to demo mode */
    if (ili_set_tp_data_len(DATA_FORMAT_DEMO, false, NULL) < 0) {
        ILI_ERR("Failed to set tp data length\n");
    }

    mutex_unlock(&ilits->touch_mutex);
    return 0;
}

#ifdef CONFIG_OPLUS_TP_APK
static int ilitek_read_debug_diff(struct seq_file *s,
                                  struct ilitek_ts_data *chip_info)
{
    int ret;
    int i = 0;
    int j = 0;
    int xch = ilits->xch_num;
    int ych = ilits->ych_num;
    u16 *debug_buf = NULL;
    mutex_lock(&ilits->touch_mutex);
    ILI_INFO("Get data");
    ret = ili_ic_func_ctrl("tp_recore", 2);

    if (ret < 0) {
        ILI_ERR("cmd fail\n");
        goto out;
    }

    debug_buf = kzalloc(xch * ych * 2, GFP_KERNEL);

    if (debug_buf == NULL) {
        ILI_ERR("allocate debug_buf failed\n");
        goto out;
    }

    ret = ili_get_tp_recore_data(debug_buf, xch * ych * 2);

    if (ret < 0) {
        ILI_ERR("get data fail\n");
    }

    ILI_INFO("recore reset");
    ret = ili_ic_func_ctrl("tp_recore", 3);

    if (ret < 0) {
        ILI_ERR("cmd fail\n");
        goto out;
    }

    seq_printf(s, "debug finger down diff data:\n");

    for (i = 0; i < ych; i++) {
        seq_printf(s, "[%2d]", i);

        for (j = 0; j < xch; j++) {
            seq_printf(s, "%5d,", (s16)(debug_buf[(i * xch + j) ]));
        }

        seq_printf(s, "\n");
    }

out:
    ili_kfree((void **)&debug_buf);
    mutex_unlock(&ilits->touch_mutex);
    return 0;
}
#endif /*CONFIG_OPLUS_TP_APK*/

static void ilitek_delta_read(struct seq_file *s, void *chip_data)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
    ilitek_read_debug_data(s, chip_info, P5_X_FW_DELTA_DATA_MODE);
#ifdef CONFIG_OPLUS_TP_APK

    if (chip_info->debug_mode_sta) {
        ilitek_read_debug_diff(s, chip_info);
    }

#endif /*CONFIG_OPLUS_TP_APK*/
}

static void ilitek_baseline_read(struct seq_file *s, void *chip_data)
{
    struct ilitek_ts_data *chip_info = (struct ilitek_ts_data *)chip_data;
    ILI_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
    ilitek_read_debug_data(s, chip_info, P5_X_FW_RAW_DATA_MODE);
}

static void ilitek_main_register_read(struct seq_file *s, void *chip_data)
{
    ILI_INFO("\n");
}

static struct debug_info_proc_operations ilitek_debug_info_proc_ops = {
    .baseline_read = ilitek_baseline_read,
    .delta_read = ilitek_delta_read,
    .main_register_read = ilitek_main_register_read,
};

static int ilitek_tp_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;

    if (!ts) {
        return 0;
    }

    ILI_INFO("s->size = %d  s->count = %d\n", (int)s->size, (int)s->count);
    atomic_set(&ilits->mp_stat, ENABLE);
    if (mutex_is_locked(&ts->mutex)) {
        ILI_INFO("ts mutex is locked, delay 2ms\n");
        mdelay(2);
    }
    mutex_lock(&ilits->touch_mutex);
    ILI_INFO("enter %s\n", __func__);
    ili_mp_test_handler(NULL, ON, s, NULL);
    mutex_unlock(&ilits->touch_mutex);
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
static int ilitek_create_proc_for_oplus(struct touchpanel_data *ts)
{
    int ret = 0;
    // touchpanel_auto_test interface
    struct proc_dir_entry *prEntry_tmp = NULL;
    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp,
                                   &ilitek_tp_auto_test_proc_fops, ts);

    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        ILI_ERR("Couldn't create proc entry\n");
    }

    return ret;
}

#ifdef CONFIG_OPLUS_TP_APK

static void ili_apk_game_set(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    ilitek_mode_switch(chip_data, MODE_GAME, on_off);
    chip_info->lock_point_status = on_off;
}

static bool ili_apk_game_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->lock_point_status;
}

static void ili_apk_debug_set(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;

    if (on_off) {
        ili_set_tp_data_len(DATA_FORMAT_DEMO_DEBUG_INFO, false, NULL);
        ili_get_tp_recore_ctrl(1);
    } else {
        ili_set_tp_data_len(DATA_FORMAT_DEMO, false, NULL);
        ili_get_tp_recore_ctrl(0);
    }

    chip_info->debug_mode_sta = on_off;
}

static bool ili_apk_debug_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->debug_mode_sta;
}

static void ili_apk_gesture_debug(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    //get_gesture_fail_reason(on_off);
    chip_info->debug_gesture_sta = on_off;
}

static bool  ili_apk_gesture_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->debug_gesture_sta;
}

static int  ili_apk_gesture_info(void *chip_data, char *buf, int len)
{
    int ret = 0;
    int i;
    int num;

    if (len < 2) {
        return 0;
    }

    buf[0] = 255;

    if (ilits->ts->gesture_buf[0] == 0xAA) {
        switch (ilits->ts->gesture_buf[1]) {   //judge gesture type
            case GESTURE_RIGHT :
                buf[0]  = Left2RightSwip;
                break;

            case GESTURE_LEFT :
                buf[0]  = Right2LeftSwip;
                break;

            case GESTURE_DOWN  :
                buf[0]  = Up2DownSwip;
                break;

            case GESTURE_UP :
                buf[0]  = Down2UpSwip;
                break;

            case GESTURE_DOUBLECLICK:
                buf[0]  = DouTap;
                break;

            case GESTURE_V :
                buf[0]  = UpVee;
                break;

            case GESTURE_V_DOWN :
                buf[0]  = DownVee;
                break;

            case GESTURE_V_LEFT :
                buf[0]  = LeftVee;
                break;

            case GESTURE_V_RIGHT :
                buf[0]  = RightVee;
                break;

            case GESTURE_O  :
                buf[0] = Circle;
                break;

            case GESTURE_M  :
                buf[0]  = Mgestrue;
                break;

            case GESTURE_W :
                buf[0]  = Wgestrue;
                break;

            case GESTURE_TWOLINE_DOWN :
                buf[0]  = DouSwip;
                break;

            default:
                buf[0] = UnkownGesture;
                break;
        }
    } else if (ilits->ts->gesture_buf[0] == 0xAE) {
        buf[0] = ilits->ts->gesture_buf[1] + 128;
    }

    //buf[0] = gesture_buf[0];
    num = ilits->ts->gesture_buf[35];

    if (num > 40) {
        num = 40;
    }

    ret = 2;
    buf[1] = num;

    for (i = 0; i < num; i++) {
        int x;
        int y;
        x = (ilits->ts->gesture_buf[40 + i * 3] & 0xF0) << 4;
        x = x | (ilits->ts->gesture_buf[40 + i * 3 + 1]);
        y = (ilits->ts->gesture_buf[40 + i * 3] & 0x0F) << 8;
        y = y | ilits->ts->gesture_buf[40 + i * 3 + 2];

        if (!ilits->trans_xy) {
            x = x * ilits->panel_wid / TPD_WIDTH;
            y = y * ilits->panel_hei / TPD_HEIGHT;
        }

        if (len < i * 4 + 2) {
            break;
        }

        buf[i * 4 + 2] = x & 0xFF;
        buf[i * 4 + 3] = (x >> 8) & 0xFF;
        buf[i * 4 + 4] = y & 0xFF;
        buf[i * 4 + 5] = (y >> 8) & 0xFF;
        ret += 4;
    }

    return ret;
}


static void ili_apk_earphone_set(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    ilitek_mode_switch(chip_data, MODE_HEADSET, on_off);
    chip_info->earphone_sta = on_off;
}

static bool ili_apk_earphone_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->earphone_sta;
}

static void ili_apk_charger_set(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    ilitek_mode_switch(chip_data, MODE_CHARGE, on_off);
    chip_info->plug_status = on_off;
}

static bool ili_apk_charger_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->plug_status;
}

static void ili_apk_noise_set(void *chip_data, bool on_off)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    //ilitek_mode_switch(chip_data, MODE_CHARGE, on_off);
    ili_ic_func_ctrl("freq_scan", on_off);
    chip_info->noise_sta = on_off;
}

static bool ili_apk_noise_get(void *chip_data)
{
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    return chip_info->noise_sta;
}


static int  ili_apk_tp_info_get(void *chip_data, char *buf, int len)
{
    int ret;
    struct ilitek_ts_data *chip_info;
    chip_info = (struct ilitek_ts_data *)chip_data;
    ret = snprintf(buf, len, "IC:ILI%04X\nFW_VER:0x%02X\nCH:%dX%d\n",
                   chip_info->chip->id,
                   chip_info->chip->fw_ver >> 8 & 0xFF,
                   chip_info->hw_res->TX_NUM,
                   chip_info->hw_res->RX_NUM);

    if (ret > len) {
        ret = len;
    }

    return ret;
}

static void ili_init_oplus_apk_op(struct touchpanel_data *ts)
{
    ts->apk_op = kzalloc(sizeof(APK_OPERATION), GFP_KERNEL);

    if (ts->apk_op) {
        ts->apk_op->apk_game_set = ili_apk_game_set;
        ts->apk_op->apk_game_get = ili_apk_game_get;
        ts->apk_op->apk_debug_set = ili_apk_debug_set;
        ts->apk_op->apk_debug_get = ili_apk_debug_get;
        //apk_op->apk_proximity_set = ili_apk_proximity_set;
        //apk_op->apk_proximity_dis = ili_apk_proximity_dis;
        ts->apk_op->apk_noise_set = ili_apk_noise_set;
        ts->apk_op->apk_noise_get = ili_apk_noise_get;
        ts->apk_op->apk_gesture_debug = ili_apk_gesture_debug;
        ts->apk_op->apk_gesture_get = ili_apk_gesture_get;
        ts->apk_op->apk_gesture_info = ili_apk_gesture_info;
        ts->apk_op->apk_earphone_set = ili_apk_earphone_set;
        ts->apk_op->apk_earphone_get = ili_apk_earphone_get;
        ts->apk_op->apk_charger_set = ili_apk_charger_set;
        ts->apk_op->apk_charger_get = ili_apk_charger_get;
        ts->apk_op->apk_tp_info_get = ili_apk_tp_info_get;
        //apk_op->apk_data_type_set = ili_apk_data_type_set;
        //apk_op->apk_rawdata_get = ili_apk_rawdata_get;
        //apk_op->apk_diffdata_get = ili_apk_diffdata_get;
        //apk_op->apk_basedata_get = ili_apk_basedata_get;
        //ts->apk_op->apk_backdata_get = ili_apk_backdata_get;
        //apk_op->apk_debug_info = ili_apk_debug_info;
    } else {
        ILI_ERR("Can not kzalloc apk op.\n");
    }
}
#endif /*CONFIG_OPLUS_TP_APK*/

static void ilitek_flag_data_init(void)
{
    ilits->phys = "SPI";
    ilits->wrapper = ili_spi_wrapper;
    ilits->detect_int_stat = ili_ic_check_int_pulse;
    ilits->int_pulse = true;
    ilits->mp_retry = false;
#if SPI_DMA_TRANSFER_SPLIT
    ilits->spi_write_then_read = ili_spi_write_then_read_split;
#else
    ilits->spi_write_then_read = ili_spi_write_then_read_direct;
#endif
    ilits->actual_tp_mode = P5_X_FW_AP_MODE;
    ilits->tp_data_format = DATA_FORMAT_DEMO;
    ilits->tp_data_len = P5_X_DEMO_MODE_PACKET_LEN;
    ilits->tp_data_mode = AP_MODE;

    if (TDDI_RST_BIND) {
        ilits->reset = TP_IC_WHOLE_RST;
    } else {
        ilits->reset = TP_HW_RST_ONLY;
    }

    ilits->rst_edge_delay = 10;
    ilits->fw_open = FILP_OPEN;
    ilits->fw_upgrade_mode = UPGRADE_IRAM;
    ilits->gesture_mode = DATA_FORMAT_GESTURE_INFO;
    ilits->gesture_demo_ctrl = DISABLE;
    ilits->wtd_ctrl = OFF;
    ilits->report = ENABLE;
    ilits->netlink = DISABLE;
    ilits->dnp = DISABLE;
    ilits->info_from_hex = ENABLE;
    ilits->wait_int_timeout = AP_INT_TIMEOUT;
    ilits->gesture = DISABLE;
    ilits->ges_sym.double_tap = DOUBLE_TAP;
    ilits->ges_sym.alphabet_line_2_top = ALPHABET_LINE_2_TOP;
    ilits->ges_sym.alphabet_line_2_bottom = ALPHABET_LINE_2_BOTTOM;
    ilits->ges_sym.alphabet_line_2_left = ALPHABET_LINE_2_LEFT;
    ilits->ges_sym.alphabet_line_2_right = ALPHABET_LINE_2_RIGHT;
    ilits->ges_sym.alphabet_m = ALPHABET_M;
    ilits->ges_sym.alphabet_w = ALPHABET_W;
    ilits->ges_sym.alphabet_c = ALPHABET_C;
    ilits->ges_sym.alphabet_E = ALPHABET_E;
    ilits->ges_sym.alphabet_V = ALPHABET_V;
    ilits->ges_sym.alphabet_O = ALPHABET_O;
    ilits->ges_sym.alphabet_S = ALPHABET_S;
    ilits->ges_sym.alphabet_Z = ALPHABET_Z;
    ilits->ges_sym.alphabet_V_down = ALPHABET_V_DOWN;
    ilits->ges_sym.alphabet_V_left = ALPHABET_V_LEFT;
    ilits->ges_sym.alphabet_V_right = ALPHABET_V_RIGHT;
    ilits->ges_sym.alphabet_two_line_2_bottom = ALPHABET_TWO_LINE_2_BOTTOM;
    ilits->ges_sym.alphabet_F = ALPHABET_F;
    ilits->ges_sym.alphabet_AT = ALPHABET_AT;
    mutex_init(&ilits->touch_mutex);
    mutex_init(&ilits->debug_mutex);
    mutex_init(&ilits->debug_read_mutex);
    init_waitqueue_head(&(ilits->inq));
    spin_lock_init(&ilits->irq_spin);
    atomic_set(&ilits->irq_stat, ENABLE);
    atomic_set(&ilits->ice_stat, DISABLE);
    atomic_set(&ilits->tp_reset, END);
    atomic_set(&ilits->fw_stat, END);
    atomic_set(&ilits->mp_stat, DISABLE);
    atomic_set(&ilits->tp_sleep, END);
    atomic_set(&ilits->cmd_int_check, DISABLE);
    atomic_set(&ilits->esd_stat, END);
    atomic_set(&ilits->tp_sw_mode, END);
    return;
}

static int ilitek_alloc_global_data(struct spi_device *spi)
{
    ilits = devm_kzalloc(&spi->dev, sizeof(struct ilitek_ts_data), GFP_KERNEL);

    if (ERR_ALLOC_MEM(ilits)) {
        ILI_ERR("Failed to allocate ts memory, %ld\n", PTR_ERR(ilits));
        return -ENOMEM;
    }

    ilits->update_buf = kzalloc(MAX_HEX_FILE_SIZE, GFP_KERNEL | GFP_DMA);

    if (ERR_ALLOC_MEM(ilits->update_buf)) {
        ILI_ERR("fw kzalloc error\n");
        return -ENOMEM;
    }

    /* Used for receiving touch data only, do not mix up with others. */
    ilits->tr_buf = kzalloc(TR_BUF_SIZE, GFP_ATOMIC);

    if (ERR_ALLOC_MEM(ilits->tr_buf)) {
        ILI_ERR("failed to allocate touch report buffer\n");
        return -ENOMEM;
    }

    ilits->spi_tx = kzalloc(SPI_TX_BUF_SIZE, GFP_KERNEL | GFP_DMA);

    if (ERR_ALLOC_MEM(ilits->spi_tx)) {
        ILI_ERR("Failed to allocate spi tx buffer\n");
        return -ENOMEM;
    }

    ilits->spi_rx = kzalloc(SPI_RX_BUF_SIZE, GFP_KERNEL | GFP_DMA);

    if (ERR_ALLOC_MEM(ilits->spi_rx)) {
        ILI_ERR("Failed to allocate spi rx buffer\n");
        return -ENOMEM;
    }

    ilits->gcoord = kzalloc(sizeof(struct gesture_coordinate), GFP_KERNEL);

    if (ERR_ALLOC_MEM(ilits->gcoord)) {
        ILI_ERR("Failed to allocate gresture coordinate buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void ilitek_free_global_data(void)
{
    ILI_INFO("remove ilitek dev\n");

    if (!ilits) {
        return;
    }

    ili_irq_disable();
    devm_free_irq(ilits->dev, ilits->irq_num, NULL);
    mutex_lock(&ilits->touch_mutex);
    gpio_free(ilits->tp_int);
    gpio_free(ilits->tp_rst);

    if (ilits->ws) {
        wakeup_source_unregister(ilits->ws);
    }

    ili_kfree((void **)&ilits->tr_buf);
    ili_kfree((void **)&ilits->gcoord);
    ili_kfree((void **)&ilits->update_buf);
    ili_kfree((void **)&ilits->spi_tx);
    ili_kfree((void **)&ilits->spi_rx);
    mutex_unlock(&ilits->touch_mutex);
    ili_kfree((void **)&ilits);
}


int __maybe_unused ilitek9882n_spi_probe(struct spi_device *spi)
{
    struct touchpanel_data *ts = NULL;
    int ret = 0;
    ILI_INFO("ilitek spi probe\n");

    if (tp_register_times > 0) {
        ILI_ERR("TP driver have success loaded %d times, exit\n",
                tp_register_times);
        return -1;
    }

    if (!spi) {
        ILI_ERR("spi device is NULL\n");
        return -ENODEV;
    }

    if (spi->master->flags & SPI_MASTER_HALF_DUPLEX) {
        ILI_ERR("Full duplex not supported by master\n");
        return -EIO;
    }

    /*step1:Alloc chip_info*/
    if (ilitek_alloc_global_data(spi) < 0) {
        ret = -ENOMEM;
        goto err_out;
    }

    ts = common_touch_data_alloc();

    if (ts == NULL) {
        ILI_ERR("ts kzalloc error\n");
        goto err_out;
    }

    memset(ts, 0, sizeof(*ts));
    ilits->ts = ts;
    ilits->spi = spi;
    ilits->dev = &spi->dev;
    ilitek_flag_data_init();
    ili_ic_init();

    if (ili_core_spi_setup(SPI_CLK) < 0) {
        ILI_ERR("ili_core_spi_setup error\n");
    }

    ts->debug_info_ops = &ilitek_debug_info_proc_ops;
    ts->s_client = spi;
    ts->irq = spi->irq;
    spi_set_drvdata(spi, ts);
    ts->dev = &spi->dev;
    ts->chip_data = ilits;
    ilits->hw_res = &ts->hw_res;
    /*get ftm firmware ini from touch.h*/
    ilits->p_firmware_headfile = &ts->panel_data.firmware_headfile;
    //idev->tp_type = TP_AUO;
    ts->ts_ops = &ilitek_ops;
    /*register common touch device*/
    ret = register_common_touch_device(ts);

    if (ret < 0) {
        ILI_INFO("\n");
        goto abnormal_register_driver;
    }

    ts->tp_suspend_order = TP_LCD_SUSPEND;
    ts->tp_resume_order = LCD_TP_RESUME;
    ts->esd_handle_support = false;
    ts->irq_need_dev_resume_ok = true;
    /*get default info from dts*/
    ilits->hw_res->reset_gpio = ts->hw_res.reset_gpio;
    ilits->hw_res->irq_gpio = ts->hw_res.irq_gpio;
    ilits->tp_int = ts->hw_res.irq_gpio;
    ilits->tp_rst = ts->hw_res.reset_gpio;
    ilits->irq_num = ts->irq;
    ILI_INFO("platform probe tp_int = %d tp_rst = %d irq_num = %d\n", ilits->tp_int, ilits->tp_rst,
             ilits->irq_num);
    ilits->fw_name = ts->panel_data.fw_name;
    ilits->test_limit_name = ts->panel_data.test_limit_name;
    ILI_INFO("fw_name = %s test_limit_name = %s\n", ilits->fw_name, ilits->test_limit_name);
    ILI_INFO("ts->resolution_info.max_x = %d ts->resolution_info.max_y = %d\n", \
             ts->resolution_info.max_x, ts->resolution_info.max_y);

    if (!ERR_ALLOC_MEM(ilits->p_firmware_headfile)) {
        ILI_INFO("ILI firmware_size = 0x%lX\n", ilits->p_firmware_headfile->firmware_size);
    }

    //ili_irq_disable();
    if (ili_tddi_init() < 0) {
        ILI_ERR("ILITEK Driver probe failed\n");
        //ili_irq_enable();
        //goto err_out;
    }

    //ili_irq_enable();
    ilitek_create_proc_for_oplus(ts);
#ifdef CONFIG_OPLUS_TP_APK
    ilits->demo_debug_info[0] = ili_demo_debug_info_id0;
    ili_init_oplus_apk_op(ts);
#endif /*CONFIG_OPLUS_TP_APK*/

    if (ts->esd_handle_support) {
        ts->esd_info.esd_work_time = msecs_to_jiffies(WQ_ESD_DELAY);
        ILI_INFO("%s:change esd handle time to %d s\n",
                 __func__,
                 ts->esd_info.esd_work_time / HZ);
    }

    ILI_INFO("ILITEK Driver loaded successfully!\n");
    return 0;
abnormal_register_driver:
    switch_spi7cs_state(false);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY ||
         ts->boot_mode == MSM_BOOT_MODE__RF ||
         ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
#if 0 //LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
        ilits->ws = wakeup_source_register("ili_wakelock");
#else
        ilits->ws = wakeup_source_register(ilits->dev, "ili_wakelock");
#endif
        if (!ilits->ws) {
            ILI_ERR("wakeup source request failed\n");
        }

        ILI_INFO("ftm mode probe end ok\n");
        return 0;
    }
	
err_out:
    spi_set_drvdata(spi, NULL);
    ILI_INFO("err_spi_setup end\n");
    common_touch_data_free(ts);
    ilitek_free_global_data();
    return ret;
}

int __maybe_unused ilitek9882n_spi_remove(struct spi_device *spi)
{
    struct touchpanel_data *ts = spi_get_drvdata(spi);
    ILI_INFO("\n");
    spi_set_drvdata(spi, NULL);
    ili_kfree((void **)&ts);
    ilitek_free_global_data();
    return 0;
}

static int ilitek_spi_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    ILI_INFO("%s is called\n", __func__);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY ||
         ts->boot_mode == MSM_BOOT_MODE__RF ||
         ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        ILI_INFO("ilitek_spi_resume do nothing in ftm\n");
        return 0;
    }

    tp_i2c_resume(ts);
    return 0;
}

static int ilitek_spi_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);
    ILI_INFO("%s: is called\n", __func__);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY ||
         ts->boot_mode == MSM_BOOT_MODE__RF ||
         ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        ILI_INFO("ilitek_spi_suspend do nothing in ftm\n");
        return 0;
    }

    tp_i2c_suspend(ts);
    return 0;
}

static const struct dev_pm_ops tp_pm_ops = {
    .suspend = ilitek_spi_suspend,
    .resume = ilitek_spi_resume,
};

/*
 * The name in the table must match the definiation
 * in a dts file.
 *
 */
static struct of_device_id tp_match_table[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    { .compatible = "oplus,tp_noflash",},
#else
    { .compatible =  TPD_DEVICE,},
#endif
    { },
};

static struct spi_driver tp_spi_driver = {
    .driver = {
        .name    = TPD_DEVICE,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
    .probe = ilitek9882n_spi_probe,
    .remove = ilitek9882n_spi_remove,
};

int __init tp_driver_init_ili_9882n(void)
{
    int res = 0;
    ILI_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE)){
        ILI_ERR("TP driver is already register\n");
        return -1;
    }

    get_oem_verified_boot_state();
    res = spi_register_driver(&tp_spi_driver);

    if (res < 0) {
        ILI_ERR("Failed to add spi driver\n");
        return -ENODEV;
    }

    ILI_INFO("Succeed to add driver\n");
    return res;
}

void __exit tp_driver_exit_ili_9882n(void)
{
    spi_unregister_driver(&tp_spi_driver);
}

module_init(tp_driver_init_ili_9882n);
module_exit(tp_driver_exit_ili_9882n);

MODULE_DESCRIPTION("Ilitek Touchscreen Driver");
MODULE_LICENSE("GPL");
