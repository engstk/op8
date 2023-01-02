// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/version.h>
#include <linux/uaccess.h>

#include "ilitek_ili9881h.h"
#include "data_transfer.h"
#include "finger_report.h"
#include "mp_test.h"
#include "protocol.h"
#include "firmware.h"


#define EXEC_READ  0
#define EXEC_WRITE 1
#define CSV_FILE_SIZE   (2048 * 1024)

#define INT_CHECK 0
#define POLL_CHECK 1
#define DELAY_CHECK 2
#define RETRY_COUNT 1

#define NORMAL_CSV_PASS_NAME		"mp_pass"
#define NORMAL_CSV_FAIL_NAME		"mp_fail"
#define OPLUS_CSV_PASS_NAME			"oplus_mp_pass"
#define OPLUS_CSV_FAIL_NAME			"oplus_mp_fail"
#define OPLUS_CSV_LCM_PASS_NAME		"oplus_mp_lcm_pass"
#define OPLUS_CSV_LCM_FAIL_NAME		"oplus_mp_lcm_fail"

#define Mathabs(x) ({						\
		long ret;							\
		if (sizeof(x) == sizeof(long)) {	\
		long __x = (x);						\
		ret = (__x < 0) ? -__x : __x;		\
		} else {							\
		int __x = (x);						\
		ret = (__x < 0) ? -__x : __x;		\
		}									\
		ret;								\
	})

#define DUMP(level, fmt, arg...)		\
	do {								\
		if(LEVEL_DEBUG == tp_debug)	\
		printk(fmt, ##arg);				\
	} while (0)

enum mp_test_catalog {
	MUTUAL_TEST = 0,
	SELF_TEST = 1,
	KEY_TEST = 2,
	ST_TEST = 3,
	TX_RX_DELTA = 4,
	UNTOUCH_P2P = 5,
	PIXEL = 6,
	OPEN_TEST = 7,
	PEAK_TO_PEAK_TEST = 8,
	SHORT_TEST = 9,
};

enum open_test_node_type {
	NO_COMPARE = 0x00,  //Not A Area, No Compare
	AA_Area = 0x01,	    //AA Area, Compare using Charge_AA
	Border_Area = 0x02, //Border Area, Compare using Charge_Border
	Notch = 0x04,       //Notch Area, Compare using Charge_Notch
	Round_Corner = 0x08, //Round Corner, No Compare
	Skip_Micro = 0x10   //Skip_Micro, No Compare
};

struct ini_file_data {
	char pSectionName[PARSER_MAX_KEY_NAME_LEN];
	char pKeyName[PARSER_MAX_KEY_NAME_LEN];
	char pKeyValue[PARSER_MAX_KEY_VALUE_LEN];
	int iSectionNameLen;
	int iKeyNameLen;
	int iKeyValueLen;
};
struct ini_file_data * ilitek_ini_file_data = NULL;

int g_ini_items = 0;

/* You must declare a new test in this struct before running a new process of mp test */
struct mp_test_items tItems[] = {
	{.name = "mutual_dac", .desp = "Calibration Data(DAC)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_bg", .desp = "Baseline Data(BG)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_signal", .desp = "Untouch Signal Data(BG-Raw-4096) - Mutual", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_no_bk", .desp = "Raw Data(No BK)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_no_bk_lcm_off", .desp = "Raw Data(No BK) (LCM OFF)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_has_bk", .desp = "Raw Data(Have BK)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_has_bk_lcm_off", .desp = "Raw Data(Have BK) (LCM OFF)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "mutual_bk_dac", .desp = "Manual BK Data(Mutual)", .result = "FAIL", .catalog = MUTUAL_TEST},

	{.name = "self_dac", .desp = "Calibration Data(DAC) - Self", .result = "FAIL", .catalog = SELF_TEST},
	{.name = "self_bg", .desp = "Baselin Data(BG,Self_Tx,Self_Rx)", .result = "FAIL", .catalog = SELF_TEST},
	{.name = "self_signal", .desp = "Untouch Signal Data(BG–Raw-4096) - Self", .result = "FAIL", .catalog = SELF_TEST},
	{.name = "self_no_bk", .desp = "Raw Data(No BK) - Self", .result = "FAIL", .catalog = SELF_TEST},
	{.name = "self_has_bk", .desp = "Raw Data(Have BK) - Self", .result = "FAIL", .catalog = SELF_TEST},
	{.name = "self_bk_dac", .desp = "Manual BK DAC Data(Self_Tx,Self_Rx)", .result = "FAIL", .catalog = SELF_TEST},

	{.name = "key_dac", .desp = "Calibration Data(DAC/ICON)", .result = "FAIL", .catalog = KEY_TEST},
	{.name = "key_bg", .desp = "Key Baseline Data", .result = "FAIL", .catalog = KEY_TEST},
	{.name = "key_no_bk", .desp = "Key Raw Data", .result = "FAIL", .catalog = KEY_TEST},
	{.name = "key_has_bk", .desp = "Key Raw BK DAC", .result = "FAIL", .catalog = KEY_TEST},
	{.name = "key_open", .desp = "Key Raw Open Test", .result = "FAIL", .catalog = KEY_TEST},
	{.name = "key_short", .desp = "Key Raw Short Test", .result = "FAIL", .catalog = KEY_TEST},

	{.name = "st_dac", .desp = "ST Calibration Data(DAC)", .result = "FAIL", .catalog = ST_TEST},
	{.name = "st_bg", .desp = "ST Baseline Data(BG)", .result = "FAIL", .catalog = ST_TEST},
	{.name = "st_no_bk", .desp = "ST Raw Data(No BK)", .result = "FAIL", .catalog = ST_TEST},
	{.name = "st_has_bk", .desp = "ST Raw(Have BK)", .result = "FAIL", .catalog = ST_TEST},
	{.name = "st_open", .desp = "ST Open Data", .result = "FAIL", .catalog = ST_TEST},

	{.name = "tx_short", .desp = "Tx Short Test", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "rx_short", .desp = "Short Test -ILI9881", .result = "FAIL", .catalog = SHORT_TEST},
	{.name = "rx_open", .desp = "RX Open", .result = "FAIL", .catalog = MUTUAL_TEST},

	{.name = "cm_data", .desp = "Untouch Cm Data", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "cs_data", .desp = "Untouch Cs Data", .result = "FAIL", .catalog = MUTUAL_TEST},

	{.name = "tx_rx_delta", .desp = "Tx/Rx Delta", .result = "FAIL", .catalog = TX_RX_DELTA},

	{.name = "p2p", .desp = "Untouch Peak to Peak", .result = "FAIL", .catalog = UNTOUCH_P2P},

	{.name = "pixel_no_bk", .desp = "Pixel Raw (No BK)", .result = "FAIL", .catalog = PIXEL},
	{.name = "pixel_has_bk", .desp = "Pixel Raw (Have BK)", .result = "FAIL", .catalog = PIXEL},

	{.name = "open_integration", .desp = "Open Test(integration)", .result = "FAIL", .catalog = OPEN_TEST},
	{.name = "open_integration_sp", .desp = "Open Test(integration)_SP", .result = "FAIL", .catalog = OPEN_TEST},
	{.name = "open_cap", .desp = "Open Test(Cap)", .result = "FAIL", .catalog = OPEN_TEST},

	{.name = "noise_peak_to_peak_ic", .desp = "Noise Peak to Peak(IC Only)", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},
	{.name = "noise_peak_to_peak_panel", .desp = "Noise Peak To Peak(With Panel)", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},
	{.name = "noise_peak_to_peak_ic_lcm_off", .desp = "Noise Peak to Peak(IC Only) (LCM OFF)", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},
	{.name = "noise_peak_to_peak_panel_lcm_off", .desp = "Noise Peak to Peak(With Panel) (LCM OFF)", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},

	{.name = "doze_raw", .desp = "Doze Raw Data", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "doze_p2p", .desp = "Doze Peak To Peak", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},
	{.name = "doze_raw_td_lcm_off", .desp = "Raw Data_TD (LCM OFF)", .result = "FAIL", .catalog = MUTUAL_TEST},
	{.name = "doze_p2p_td_lcm_off", .desp = "Peak To Peak_TD (LCM OFF)", .result = "FAIL", .catalog = PEAK_TO_PEAK_TEST},
};

int32_t *frame_buf = NULL;
int32_t *key_buf = NULL;
int32_t *frame1_cbk700 = NULL;
int32_t *frame1_cbk250 = NULL;
int32_t *frame1_cbk200 = NULL;

struct core_mp_test_data *core_mp = NULL;

void dump_data(void *data, int type, int len, int row_len, const char *name)
{
	int i = 0;
	int row = 31;
	uint8_t *p8 = NULL;
	int32_t *p32 = NULL;

	if(row_len > 0)
		row = row_len;

	if (LEVEL_DEBUG == tp_debug) {
		if (data == NULL) {
			TPD_INFO("The data going to dump is NULL\n");
			return;
		}

		printk("\n\n");
		printk("Dump %s data\n", name);

		if (type == 8)
			p8 = (uint8_t *) data;
		if (type == 32 || type == 10)
			p32 = (int32_t *) data;

		for (i = 0; i < len; i++) {
			if (type == 8)
				printk(" %4x ", p8[i]);
			else if (type == 32)
				printk(" %4x ", p32[i]);
			else if (type == 10)
				printk(" %4d ", p32[i]);
			if ((i % row) == row-1)
				printk("\n");
		}
		printk("\n\n");
	}
}
EXPORT_SYMBOL(dump_data);

static void dump_benchmark_data(int32_t* max_ptr, int32_t* min_ptr)
{
	int i = 0;

	if (LEVEL_DEBUG == tp_debug) {
		TPD_INFO("benchmark max\n");

		for(i = 0; i < core_mp->frame_len; i++) {
			printk("%d, ", max_ptr[i]);
			if(i % core_mp->xch_len == core_mp->xch_len - 1)
				printk("\n");
		}

		TPD_INFO("benchmark min\n");

		for(i = 0; i < core_mp->frame_len; i++) {
			printk("%d, ", min_ptr[i]);
			if(i % core_mp->xch_len == core_mp->xch_len - 1)
				printk("\n");
		}
	}
}

void dump_node_type_buffer(int32_t* node_ptr, uint8_t *name)
{
	int i = 0;

	if (LEVEL_DEBUG == tp_debug) {
		TPD_INFO("Node Type buffer: %s\n", name);
		for(i=0; i<core_mp->frame_len ; i++)
		{
			printk("%d, ",node_ptr[i]);
			if(i % core_mp->xch_len == core_mp->xch_len-1)
				printk("\n");
		}
	}

}

static void mp_compare_cdc_result(int32_t *tmp, int32_t *max_ts, int32_t *min_ts, int* result)
{
	int x = 0;
	int y = 0;

	if (ERR_ALLOC_MEM(tmp)) {
		TPD_INFO("The data of test item is null (%p)\n", tmp);
		*result = MP_FAIL;
		return;
	}

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len + x;
			if (tmp[shift] > max_ts[shift] || tmp[shift] < min_ts[shift]){
				*result = MP_FAIL;
			}
		}
	}
}

static void mp_compare_cdc_show_result(int32_t *tmp, char *csv, int *csv_len, int type, int32_t *max_ts, int32_t *min_ts, const char *desp)
{
	int x = 0;
	int y = 0;
	int tmp_len = *csv_len;
	int mp_result = MP_PASS;

	if (ERR_ALLOC_MEM(tmp)) {
		TPD_INFO("The data of test item is null (%p)\n", tmp);
		mp_result = MP_FAIL;
		goto out;
	}

	/* print X raw only */
	for (x = 0; x < core_mp->xch_len; x++) {
		if (x == 0) {
			DUMP(DEBUG_MP_TEST, "\n %s ", desp);
			tmp_len += sprintf(csv + tmp_len, "\n      %s ,", desp);
		}

		DUMP(DEBUG_MP_TEST, "  X_%d  ,", (x+1));
		tmp_len += sprintf(csv + tmp_len, "  X_%d  ,", (x+1));
	}

	DUMP(DEBUG_MP_TEST, "\n");
	tmp_len += sprintf(csv + tmp_len, "\n");

	for (y = 0; y < core_mp->ych_len; y++) {
		DUMP(DEBUG_MP_TEST, "  Y_%d  ,", (y+1));
		tmp_len += sprintf(csv + tmp_len, "  Y_%d  ,", (y+1));

		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len + x;

			if ((tmp[shift] <= max_ts[shift] && tmp[shift] >= min_ts[shift]) || (type != TYPE_JUGE)){

				if((tmp[shift] == INT_MAX || tmp[shift] == INT_MIN) && (type == TYPE_BENCHMARK)){
					DUMP(DEBUG_MP_TEST, "%s", "BYPASS,");
					tmp_len += sprintf(csv + tmp_len,"BYPASS,");
				}
				else{
					DUMP(DEBUG_MP_TEST, " %7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, " %7d, ", tmp[shift]);
				}
			} else {
				if (tmp[shift] > max_ts[shift]) {
					DUMP(DEBUG_MP_TEST, " *%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "*%7d,", tmp[shift]);
				} else {
					DUMP(DEBUG_MP_TEST, " #%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "#%7d,", tmp[shift]);
				}
				mp_result = MP_FAIL;
			}
		}

		DUMP(DEBUG_MP_TEST, "\n");
		tmp_len += sprintf(csv + tmp_len, "\n");
	}

out:
	if (type == TYPE_JUGE) {
		if (mp_result == MP_PASS){
			TPD_INFO("\n Result : PASS\n");
			tmp_len += sprintf(csv + tmp_len, "Result : PASS\n");
		} else {
			TPD_INFO("\n Result : FAIL\n");
			tmp_len += sprintf(csv + tmp_len, "Result : FAIL\n");
		}
	}

	*csv_len = tmp_len;
}

static int create_mp_test_frame_buffer(int index, int frame_count)
{
	TPD_DEBUG("Create MP frame buffers (index = %d), count = %d\n",index, frame_count);
	//this item aready created buffer
	if(tItems[index].test_count != 0) {
		TPD_INFO("this item already created buffer\n");
		return 0;
	}
	if (tItems[index].catalog == TX_RX_DELTA) {
		ipio_vfree((void **)&core_mp->tx_delta_buf);
		core_mp->tx_delta_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		ipio_vfree((void **)&core_mp->rx_delta_buf);
		core_mp->rx_delta_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		ipio_vfree((void **)&core_mp->tx_max_buf);
		core_mp->tx_max_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		ipio_vfree((void **)&core_mp->tx_min_buf);
		core_mp->tx_min_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		ipio_vfree((void **)&core_mp->rx_max_buf);
		core_mp->rx_max_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		ipio_vfree((void **)&core_mp->rx_min_buf);
		core_mp->rx_min_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));

		if (ERR_ALLOC_MEM(core_mp->tx_delta_buf) || ERR_ALLOC_MEM(core_mp->rx_delta_buf)) {
			TPD_INFO("Failed to allocate Tx/Rx Delta buffer\n");
			return -ENOMEM;
		}

		if (ERR_ALLOC_MEM(core_mp->tx_max_buf) || ERR_ALLOC_MEM(core_mp->tx_min_buf)) {
			TPD_INFO("Failed to allocate Tx Max/Min buffer\n");
			return -ENOMEM;
		}

		if (ERR_ALLOC_MEM(core_mp->rx_max_buf) || ERR_ALLOC_MEM(core_mp->rx_min_buf)) {
			TPD_INFO("Failed to allocate Rx Max/Min buffe\n");
			return -ENOMEM;
		}

	} else {
		tItems[index].buf = vmalloc(frame_count * core_mp->frame_len * sizeof(int32_t));
		tItems[index].result_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		tItems[index].max_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		tItems[index].min_buf = vmalloc(core_mp->frame_len * sizeof(int32_t));
		if (tItems[index].spec_option == BENCHMARK) {
			tItems[index].bench_mark_max = vmalloc(core_mp->frame_len * sizeof(int32_t));
			tItems[index].bench_mark_min = vmalloc(core_mp->frame_len * sizeof(int32_t));

			if (ERR_ALLOC_MEM(tItems[index].bench_mark_max) || ERR_ALLOC_MEM(tItems[index].bench_mark_min)){
				TPD_INFO("Failed to allocate bench_mark FRAME buffer\n");
				return -ENOMEM;
			}
		}
		if (ERR_ALLOC_MEM(tItems[index].buf) || ERR_ALLOC_MEM(tItems[index].max_buf) ||
				ERR_ALLOC_MEM(tItems[index].min_buf) || ERR_ALLOC_MEM(tItems[index].result_buf)) {
			TPD_INFO("Failed to allocate FRAME buffer\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static int core_mp_ctrl_lcm_status(bool on)
{
	int ret = 0;
	int ctrl = 0;
	int delay = 0;
	uint8_t lcd[15] = {0};
	uint8_t header = 0x0F;

	memset(&lcd, 0xFF, ARRAY_SIZE(lcd));

	ctrl = ((on) ? 1 : 2);
	delay = ((on) ? 100 : 10);

	lcd[0] = header;
	lcd[1] = protocol->mutual_bg;
	lcd[2] = 0;
	lcd[3] = ctrl;

	dump_data(lcd, 8, ARRAY_SIZE(lcd), 0, "LCM Command");

	ret = core_write(core_config->slave_i2c_addr, lcd, ARRAY_SIZE(lcd));
	if (ret < 0) {
		TPD_INFO("Failed to write LCM command\n");
		goto out;
	}

	mdelay(delay);

out:
	return ret;
}

static void mp_calc_nodp(bool long_v)
{
	uint8_t at = 0;
	uint8_t phase = 0;
	uint8_t r2d = 0;
	uint8_t rst = 0;
	uint8_t rst_back = 0;
	uint8_t dac_td = 0;
	uint8_t qsh_pw = 0;
	uint8_t qsh_td = 0;
	uint8_t dn = 0;
	uint16_t tshd = 0;
	uint16_t tsvd_to_tshd = 0;
	uint16_t qsh_tdf = 0;
	uint16_t dp2tp = 0;
	uint16_t twc = 0;
	uint16_t twcm = 0;
	uint16_t tp2dp = 0;
	uint16_t multi_term_num = 0;
	uint16_t tp_tsdh_wait = 0;
	uint16_t ddi_width = 0;
	uint32_t real_tx_dura = 0;
	uint32_t tmp = 0;
	uint32_t tmp1 = 0;

	TPD_DEBUG("DDI Mode = %d\n", long_v);

	tshd = core_mp->nodp.tshd;
	tsvd_to_tshd = core_mp->nodp.tsvd_to_tshd;
	multi_term_num = core_mp->nodp.multi_term_num_120;
	qsh_tdf = core_mp->nodp.qsh_tdf;
	at = core_mp->nodp.auto_trim;
	tp_tsdh_wait = core_mp->nodp.tp_tshd_wait_120;
	ddi_width = core_mp->nodp.ddi_width_120;
	dp2tp = core_mp->nodp.dp_to_tp;
	twc = core_mp->nodp.tx_wait_const;
	twcm = core_mp->nodp.tx_wait_const_multi;
	tp2dp = core_mp->nodp.tp_to_dp;
	phase = core_mp->nodp.phase_adc;
	r2d = core_mp->nodp.r2d_pw;
	rst = core_mp->nodp.rst_pw;
	rst_back = core_mp->nodp.rst_pw_back;
	dac_td = core_mp->nodp.dac_td;
	qsh_pw = core_mp->nodp.qsh_pw;
	qsh_td = core_mp->nodp.qsh_td;
	dn = core_mp->nodp.drop_nodp;

	/* NODP formulation */
	if (!long_v) {
		if (core_mp->nodp.is60HZ) {
			multi_term_num = core_mp->nodp.multi_term_num_60;
			tp_tsdh_wait = core_mp->nodp.tp_tshd_wait_60;
			ddi_width = core_mp->nodp.ddi_width_60;
		}

		if (multi_term_num == 0)
			multi_term_num = 1;

		tmp = ((tshd << 2) - (at << 6) - tp_tsdh_wait - ddi_width * (multi_term_num - 1) - 64 - dp2tp - twc) * 5;
		tmp1 = (phase << 5) - ((twcm * 5 + (phase << 5) + (tp2dp * 5 << 6)) * (multi_term_num - 1));
		real_tx_dura = (tmp - tmp1) / (multi_term_num * 5);

		core_mp->nodp.first_tp_width = (dp2tp * 5  + twc * 5  + ( phase << 5 ) + real_tx_dura * 5 ) / 5;
		core_mp->nodp.tp_width = ( ( ( tp2dp * 10 + phase ) << 6 )  + real_tx_dura * 10 ) / 10;
		core_mp->nodp.txpw = ( qsh_tdf + rst + qsh_pw + qsh_td + 2 );

		if ( core_mp->nodp.txpw % 2 == 1 )
			core_mp->nodp.txpw = core_mp->nodp.txpw + 1;

		core_mp->nodp.nodp = real_tx_dura / core_mp->nodp.txpw / 2;
	} else {
		if (multi_term_num == 0)
			multi_term_num = 1;

		real_tx_dura = (((tshd << 2) - (at << 6) - ddi_width * (11) - 64 - dp2tp - twc) * 5 - (phase << 5) - ((twcm * 5 + (phase << 5) + (tp2dp * 5 << 6)) * (11))) / (12 * 5);

		core_mp->nodp.long_tsdh_wait = (tsvd_to_tshd + 10 ) << 6;

		core_mp->nodp.first_tp_width = (dp2tp * 5  + twc * 5  + ( phase << 5 ) + real_tx_dura * 5) / 5;
		core_mp->nodp.tp_width = (((tp2dp * 10 + phase ) << 6 )  + real_tx_dura * 10) / 10;
		core_mp->nodp.txpw = (qsh_tdf + rst + qsh_pw + qsh_td + 2);

		if ( core_mp->nodp.txpw % 2 == 1 )
			core_mp->nodp.txpw = core_mp->nodp.txpw + 1;

		core_mp->nodp.nodp = real_tx_dura / core_mp->nodp.txpw / 2;
	}

	TPD_DEBUG("Read Tx Duration = %d\n",real_tx_dura);
	TPD_DEBUG("First TP Width = %d\n",core_mp->nodp.first_tp_width);
	TPD_DEBUG("TP Width = %d\n",core_mp->nodp.tp_width);
	TPD_DEBUG("TXPW = %d\n",core_mp->nodp.txpw);
	TPD_DEBUG("NODP = %d\n",core_mp->nodp.nodp);
}

static int allnode_key_cdc_data(int index)
{
	int i = 0;
	int res = 0;
	int len = 0;
	int inDACp = 0;
	int inDACn = 0;
	uint8_t cmd[3] = { 0 };
	uint8_t *ori = NULL;

	len = core_mp->key_len * 2;

	TPD_DEBUG("Read key's length = %d\n", len);
	TPD_DEBUG("core_mp->key_len = %d\n", core_mp->key_len);

	if (len <= 0) {
		TPD_INFO("Length is invalid\n");
		res = -1;
		goto out;
	}

	/* CDC init */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = tItems[index].cmd;
	cmd[2] = 0;

	res = core_write(core_config->slave_i2c_addr, cmd, 3);
	if (res < 0) {
		TPD_INFO("I2C Write Error while initialising cdc\n");
		goto out;
	}

	mdelay(1);

	/* Check busy */
	if (core_config_check_cdc_busy(50) < 0) {
		TPD_INFO("Check busy is timout !\n");
		res = -1;
		goto out;
	}

	mdelay(1);

	/* Prepare to get cdc data */
	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_cdc;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	/* Allocate a buffer for the original */
	ori = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ori)) {
		TPD_INFO("Failed to allocate ori mem (%ld)\n", PTR_ERR(ori));
		goto out;
	}

	mdelay(1);

	/* Get original frame(cdc) data */
	res = core_read(core_config->slave_i2c_addr, ori, len);
	if (res < 0) {
		TPD_INFO("I2C Read Error while getting original cdc data\n");
		goto out;
	}

	dump_data(ori, 8, len, 0, "Key CDC original");

	if (key_buf == NULL) {
		key_buf = kcalloc(core_mp->key_len, sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(key_buf)) {
			TPD_INFO("Failed to allocate FrameBuffer mem (%ld)\n", PTR_ERR(key_buf));
			goto out;
		}
	} else {
		memset(key_buf, 0x0, core_mp->key_len);
	}

	/* Convert original data to the physical one in each node */
	for (i = 0; i < core_mp->frame_len; i++) {
		if (tItems[index].cmd == protocol->key_dac) {
			/* DAC - P */
			if (((ori[(2 * i) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F);
			} else {
				inDACp = ori[(2 * i) + 1] & 0x7F;
			}

			/* DAC - N */
			if (((ori[(1 + (2 * i)) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
			} else {
				inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
			}

			key_buf[i] = (inDACp + inDACn) / 2;
		}
	}

	dump_data(key_buf, 32, core_mp->frame_len, core_mp->xch_len, "Key CDC combined data");

out:
	ipio_kfree((void **)&ori);
	return res;
}

int core_mp_calc_timing_nodp(void)
{
	int ret = 0;
	uint8_t test_type = 0x0;
	uint8_t timing_cmd[15] = {0};
	uint8_t get_timing[64] = {0};
	if (ERR_ALLOC_MEM(core_mp)) {
		TPD_INFO("core_mp is NULL\n");
		return -ENOMEM;
	}

	memset(timing_cmd, 0xFF, sizeof(timing_cmd));

	timing_cmd[0] = protocol->cmd_cdc;
	timing_cmd[1] = protocol->get_timing;
	timing_cmd[2] = test_type;

	/*
	 * To calculate NODP, we need to get timing parameters first from fw,
	 * which returnes 40 bytes data.
	 */
	if (LEVEL_DEBUG == tp_debug)
	{
		dump_data(timing_cmd, 8, sizeof(timing_cmd), 0, "Timing command");
	}
	ret = core_write(core_config->slave_i2c_addr, timing_cmd, sizeof(timing_cmd));
	if (ret < 0) {
		TPD_INFO("Failed to write timing command\n");
		goto out;
	}

	ret = core_read(core_config->slave_i2c_addr, get_timing, sizeof(get_timing));
	if (ret < 0) {
		TPD_INFO("Failed to read timing parameters\n");
		goto out;
	}
	if (LEVEL_DEBUG == tp_debug)
	{
		dump_data(get_timing, 8, 41, 0, "Timing parameters (41bytes)");
	}
	/* Combine timing data */
	core_mp->nodp.is60HZ = false; // This will get from ini file by default.
	core_mp->nodp.isLongV = get_timing[2];
	core_mp->nodp.tshd = (get_timing[3] << 8 ) + get_timing[4];
	core_mp->nodp.multi_term_num_120 = get_timing[5];
	core_mp->nodp.multi_term_num_60 = get_timing[6];
	core_mp->nodp.tsvd_to_tshd = (get_timing[7] << 8 ) + get_timing[8];
	core_mp->nodp.qsh_tdf = (get_timing[9] << 8 ) + get_timing[10];
	core_mp->nodp.auto_trim = get_timing[11];
	core_mp->nodp.tp_tshd_wait_120 = (get_timing[12] << 8 ) + get_timing[13];
	core_mp->nodp.ddi_width_120 = (get_timing[14] << 8 ) + get_timing[15];
	core_mp->nodp.tp_tshd_wait_60 = (get_timing[16] << 8 ) + get_timing[17];
	core_mp->nodp.ddi_width_60 = (get_timing[18] << 8 ) + get_timing[19];
	core_mp->nodp.dp_to_tp = (get_timing[20] << 8 ) + get_timing[21];
	core_mp->nodp.tx_wait_const = (get_timing[22] << 8 ) + get_timing[23];
	core_mp->nodp.tx_wait_const_multi = (get_timing[24] << 8 ) + get_timing[25];
	core_mp->nodp.tp_to_dp = (get_timing[26] << 8 ) + get_timing[27];
	core_mp->nodp.phase_adc = get_timing[28];
	core_mp->nodp.r2d_pw = get_timing[29];
	core_mp->nodp.rst_pw = get_timing[30];
	core_mp->nodp.rst_pw_back = get_timing[31];
	core_mp->nodp.dac_td = get_timing[32];
	core_mp->nodp.qsh_pw = get_timing[33];
	core_mp->nodp.qsh_td = get_timing[34];
	core_mp->nodp.drop_nodp = get_timing[35];
	if (LEVEL_DEBUG == tp_debug)
	{
		TPD_INFO("60HZ = %d\n",core_mp->nodp.is60HZ);
		TPD_INFO("DDI Mode = %d\n",core_mp->nodp.isLongV);
		TPD_INFO("TSHD = %d\n",core_mp->nodp.tshd);
		TPD_INFO("Multi Term Num (120Hz) = %d\n",core_mp->nodp.multi_term_num_120);
		TPD_INFO("Multi Term Num (60Hz) = %d\n",core_mp->nodp.multi_term_num_60);
		TPD_INFO("TSVD to TSHD = %d\n",core_mp->nodp.tsvd_to_tshd);
		TPD_INFO("QSH TDF = %d\n",core_mp->nodp.qsh_tdf);
		TPD_INFO("AutoTrim Variation = %d\n",core_mp->nodp.auto_trim);
		TPD_INFO("TP TSHD Wait (120Hz) = %d\n",core_mp->nodp.tp_tshd_wait_120);
		TPD_INFO("DDI Width (120Hz) = %d\n",core_mp->nodp.ddi_width_120);
		TPD_INFO("TP TSHD Wait (60Hz) = %d\n",core_mp->nodp.tp_tshd_wait_60);
		TPD_INFO("DDI Width (60Hz) = %d\n",core_mp->nodp.ddi_width_60);
		TPD_INFO("DP to TP = %d\n",core_mp->nodp.dp_to_tp);
		TPD_INFO("TX Wait Const = %d\n",core_mp->nodp.tx_wait_const);
		TPD_INFO("TX Wait Const Multi = %d\n",core_mp->nodp.tx_wait_const_multi);
		TPD_INFO("TP to DP = %d\n",core_mp->nodp.tp_to_dp);
		TPD_INFO("Phase ADC = %d\n",core_mp->nodp.phase_adc);
		TPD_INFO("R2D PW = %d\n",core_mp->nodp.r2d_pw);
		TPD_INFO("RST PW = %d\n",core_mp->nodp.rst_pw);
		TPD_INFO("RST PW Back = %d\n",core_mp->nodp.rst_pw_back);
		TPD_INFO("DAC TD = %d\n",core_mp->nodp.dac_td);
		TPD_INFO("QSH PW = %d\n",core_mp->nodp.qsh_pw);
		TPD_INFO("QSH TD = %d\n",core_mp->nodp.qsh_td);
		TPD_INFO("Drop NODP Num = %d\n",core_mp->nodp.drop_nodp);
	}
	mp_calc_nodp(core_mp->nodp.isLongV);

out:
	return ret;
}

static int mp_cdc_get_pv5_4_command(uint8_t *cmd, int len, int index)
{
	int ret = 0;
	char str[128] = {0};
	char tmp[128] = {0};
	char *key = tItems[index].desp;

	if (strncmp(key, "Raw Data_TD (LCM OFF)", strlen(key)) == 0)
		key = "Doze Raw Data";
	else if (strncmp(key, "Peak To Peak_TD (LCM OFF)", strlen(key)) == 0)
		key = "Doze Peak To Peak";
	else if (strncmp(key, "Raw Data(No BK) (LCM OFF)", strlen(key)) == 0)
		key = "Raw Data(No BK)";
	else if (strncmp(key, "Raw Data(Have BK) (LCM OFF)", strlen(key)) == 0)
		key = "Raw Data(Have BK)";
	else if (strncmp(key, "Noise Peak to Peak(With Panel) (LCM OFF)", strlen(key)) == 0 )
		key = "Noise Peak To Peak(With Panel)";


	TPD_DEBUG("%s gets %s command from INI.\n", tItems[index].desp, key);

	ret = core_parser_get_int_data("PV5_4 Command", key, str);
	if (ret < 0) {
		TPD_INFO("Failed to parse PV54 command, ret = %d\n",ret);
		goto out;
	}

	strncpy(tmp, str, ret);
	core_parser_get_u8_array(tmp, cmd);

out:
	return ret;
}


static int mp_cdc_init_cmd_common(uint8_t *cmd, int len, int index)
{
	int ret = 0;

	if (protocol->major >= 5 && protocol->mid >= 4) {
		TPD_DEBUG("Get CDC command with protocol v5.4\n");
		protocol->cdc_len = 15;
		return mp_cdc_get_pv5_4_command(cmd, len, index);
	}

	cmd[0] = protocol->cmd_cdc;
	cmd[1] = tItems[index].cmd;
	cmd[2] = 0;
	protocol->cdc_len = 3;

	if (strcmp(tItems[index].name, "open_integration") == 0)
		cmd[2] = 0x2;
	if (strcmp(tItems[index].name, "open_cap") == 0)
		cmd[2] = 0x3;

	if(tItems[index].catalog == PEAK_TO_PEAK_TEST) {
		cmd[2] = ((tItems[index].frame_count & 0xff00) >> 8);
		cmd[3] = tItems[index].frame_count & 0xff;
		cmd[4] = 0;
		protocol->cdc_len = 5;
		if (strcmp(tItems[index].name, "noise_peak_to_peak_cut") == 0)
			cmd[4] = 0x1;

		TPD_DEBUG("P2P CMD: %d,%d,%d,%d,%d\n",
				cmd[0],cmd[1],cmd[2],cmd[3],cmd[4]);
	}

	return ret;
}

static int allnode_mutual_cdc_data(int index)
{
	int i = 0;
	int res = 0;
	int len = 0;
	int inDACp = 0;
	int inDACn = 0;
	uint8_t cmd[15] = {0};
	uint8_t *ori = NULL;

	/* Multipling by 2 is due to the 16 bit in each node */
	len = (core_mp->xch_len * core_mp->ych_len * 2) + 2;

	TPD_DEBUG("Read X/Y Channel length = %d\n", len);
	TPD_DEBUG("core_mp->frame_len = %d\n", core_mp->frame_len);

	if (len <= 2) {
		TPD_INFO("Length is invalid\n");
		res = -1;
		goto out;
	}

	memset(cmd, 0xFF, protocol->cdc_len);

	/* CDC init */
	mp_cdc_init_cmd_common(cmd, protocol->cdc_len, index);

	dump_data(cmd, 8, protocol->cdc_len, 0, "Mutual CDC command");
	ipd->Mp_test_data_ready = false;
	res = core_write(core_config->slave_i2c_addr, cmd, protocol->cdc_len);
	if (res < 0) {
		TPD_INFO("I2C Write Error while initialising cdc\n");
		goto out;
	}

	/* Check busy */
	TPD_DEBUG("Check busy method = %d\n",core_mp->busy_cdc);
	if (core_mp->busy_cdc == POLL_CHECK) {
		res = core_config_check_cdc_busy(50);
	} else if (core_mp->busy_cdc == INT_CHECK) {
		res = core_config_check_data_ready();
	} else if (core_mp->busy_cdc == DELAY_CHECK) {
		mdelay(600);
	}

	if (res < 0) {
		TPD_INFO("Check busy timeout !\n");
		res = -1;
	}

	/* Prepare to get cdc data */
	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_cdc;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	/* Allocate a buffer for the original */
	ori = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ori)) {
		TPD_INFO("Failed to allocate ori mem (%ld)\n", PTR_ERR(ori));
		goto out;
	}

	/* Get original frame(cdc) data */
	res = core_read(core_config->slave_i2c_addr, ori, len);
	if (res < 0) {
		TPD_INFO("I2C Read Error while getting original cdc data\n");
		goto out;
	}

	dump_data(ori, 8, len, 0, "Mutual CDC original");

	if (frame_buf == NULL) {
		frame_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(frame_buf)) {
			TPD_INFO("Failed to allocate FrameBuffer mem (%ld)\n", PTR_ERR(frame_buf));
			goto out;
		}
	} else {
		memset(frame_buf, 0x0, core_mp->frame_len);
	}

	/* Convert original data to the physical one in each node */
	for (i = 0; i < core_mp->frame_len; i++) {
		if (tItems[index].cmd == protocol->mutual_dac) {
			/* DAC - P */
			if (((ori[(2 * i) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F);
			} else {
				inDACp = ori[(2 * i) + 1] & 0x7F;
			}

			/* DAC - N */
			if (((ori[(1 + (2 * i)) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
			} else {
				inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
			}

			frame_buf[i] = (inDACp + inDACn) / 2;
		} else {
			/* H byte + L byte */
			int32_t tmp = (ori[(2 * i) + 1] << 8) + ori[(1 + (2 * i)) + 1];

			if ((tmp & 0x8000) == 0x8000)
				frame_buf[i] = tmp - 65536;
			else
				frame_buf[i] = tmp;

			if(strncmp(tItems[index].name, "mutual_no_bk", strlen("mutual_no_bk")) == 0 ||
				strncmp(tItems[index].name, "mutual_no_bk_lcm_off", strlen("mutual_no_bk_lcm_off")) == 0)
			{
				if(core_config->chip_id == CHIP_TYPE_ILI9881)
				{
					if(core_config->chip_type == ILI9881_TYPE_H)
						frame_buf[i] -= RAWDATA_NO_BK_DATA_SHIFT_9881H;
					if(core_config->chip_type == ILI9881_TYPE_F)
						frame_buf[i] -= RAWDATA_NO_BK_DATA_SHIFT_9881F;
				}
			}
		}
	}

	dump_data(frame_buf, 32, core_mp->frame_len,  core_mp->xch_len, "Mutual CDC combined");

out:
	ipio_kfree((void **)&ori);
	return res;
}

static void run_pixel_test(int index)
{
	int i = 0;
	int x = 0;
	int y = 0;
	int32_t *p_comb = frame_buf;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int tmp[4] = { 0 }, max = 0;
			int shift = y * core_mp->xch_len;
			int centre = p_comb[shift + x];

			/* if its position is in corner, the number of point
			   we have to minus is around 2 to 3.  */
			if (y == 0 && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y == (core_mp->ych_len - 1) && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y == 0 && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
			} else if (y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
			} else if (y == 0 && x != 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y != 0 && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
				tmp[2] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */

			} else if (y == (core_mp->ych_len - 1) && x != 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y != 0 && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
			} else {
				/* middle minus four directions */
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[2] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[3] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			}

			max = tmp[0];

			for (i = 0; i < 4; i++) {
				if (tmp[i] > max)
					max = tmp[i];
			}

			tItems[index].buf[shift + x] = max;
		}
	}
}

static int run_open_test(int index)
{
	int i = 0;
	int x = 0;
	int y = 0;
	int k = 0;
	int res = 0;
	int border_x[] = {-1, 0, 1, 1, 1, 0, -1, -1};
	int border_y[] = {-1, -1, -1, 0, 1, 1, 1, 0};
	int32_t *p_comb = frame_buf;

	if (strcmp(tItems[index].name, "open_integration") == 0) {
		for (i = 0; i < core_mp->frame_len; i++)
			tItems[index].buf[i] = p_comb[i];
	} else if (strcmp(tItems[index].name, "open_cap") == 0) {
		/* Each result is getting from a 3 by 3 grid depending on where the centre location is.
		   So if the centre is at corner, the number of node grabbed from a grid will be different. */
		for (y = 0; y < core_mp->ych_len; y++) {
			for (x = 0; x < core_mp->xch_len; x++) {
				int sum = 0, avg = 0, count = 0;
				int shift = y * core_mp->xch_len;
				int centre = p_comb[shift + x];

				for (k = 0; k < 8; k++) {
					if (((y + border_y[k] >= 0) && (y + border_y[k] < core_mp->ych_len)) &&
								((x + border_x[k] >= 0) && (x + border_x[k] < core_mp->xch_len))) {
						count++;
						sum += p_comb[(y + border_y[k]) * core_mp->xch_len + (x + border_x[k])];
					}
				}

				avg = (sum + centre) / (count + 1);	/* plus 1 because of centre */
				tItems[index].buf[shift + x] = (centre * 100) / avg;
			}
		}
	}
	return res;
}

static void run_tx_rx_delta_test(int index)
{
	int x = 0;
	int y = 0;
	int32_t *p_comb = frame_buf;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			/* Tx Delta */
			if (y != (core_mp->ych_len - 1)) {
				core_mp->tx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[(shift + 1) + x]);
			}

			/* Rx Delta */
			if (x != (core_mp->xch_len - 1)) {
				core_mp->rx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[shift + (x + 1)]);
			}
		}
	}
}

static void run_untouch_p2p_test(int index)
{
	int x = 0;
	int y = 0;
	int32_t *p_comb = frame_buf;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			if (p_comb[shift + x] > tItems[index].max_buf[shift + x]) {
				tItems[index].max_buf[shift + x] = p_comb[shift + x];
			}

			if (p_comb[shift + x] < tItems[index].min_buf[shift + x]) {
				tItems[index].min_buf[shift + x] = p_comb[shift + x];
			}

			tItems[index].buf[shift + x] =
			    tItems[index].max_buf[shift + x] - tItems[index].min_buf[shift + x];
		}
	}
}

static void compare_MaxMin_result(int index, int32_t *data)
{
	int x = 0;
	int y = 0;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			if (tItems[index].catalog == UNTOUCH_P2P)
				return;
			else if (tItems[index].catalog == TX_RX_DELTA) {
				/* Tx max/min comparison */
				if (core_mp->tx_delta_buf[shift + x] < data[shift + x]) {
					core_mp->tx_max_buf[shift + x] = data[shift + x];
				}

				if (core_mp->tx_delta_buf[shift + x] > data[shift + x]) {
					core_mp->tx_min_buf[shift + x] = data[shift + x];
				}

				/* Rx max/min comparison */
				if (core_mp->rx_delta_buf[shift + x] < data[shift + x]) {
					core_mp->rx_max_buf[shift + x] = data[shift + x];
				}

				if (core_mp->rx_delta_buf[shift + x] > data[shift + x]) {
					core_mp->rx_min_buf[shift + x] = data[shift + x];
				}
			} else {
				if (tItems[index].max_buf[shift + x] < data[shift + x]) {
					tItems[index].max_buf[shift + x] = data[shift + x];
				}

				if (tItems[index].min_buf[shift + x] > data[shift + x]) {
					tItems[index].min_buf[shift + x] = data[shift + x];
				}
			}
		}
	}
}


#define ABS(a,b) ( (a>b) ? (a-b) : (b-a) )
#define ADDR(x,y) ((y*core_mp->xch_len)+(x))

int full_open_rate_compare(int32_t* full_open, int32_t* cbk, int x, int y, int32_t inNodeType, int full_open_rate)
{
	int ret = true;

	if ((inNodeType == NO_COMPARE) || ((inNodeType & Round_Corner) == Round_Corner))
	{
		return true;
	}

	if(full_open[ADDR(x,y)] < (cbk[ADDR(x,y)] * full_open_rate / 100))
		ret = false;

	return ret;
}

int compare_charge(int32_t* charge_rate, int x, int y, int32_t* inNodeType, int Charge_AA, int Charge_Border, int Charge_Notch)
{
	int OpenThreadhold = 0;
	int tempY = 0;
	int tempX = 0;
	int ret = 0;
	int k = 0;
	int sx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	int sy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

	ret = charge_rate[ADDR(x, y)];

	/*Setting Threadhold from node type  */

	if(charge_rate[ADDR(x, y)] == 0)
		return ret;
	else if ((inNodeType[ADDR(x, y)] & AA_Area) == AA_Area)
		OpenThreadhold = Charge_AA;
	else if ((inNodeType[ADDR(x, y)] & Border_Area) == Border_Area)
		OpenThreadhold = Charge_Border;
	else if ((inNodeType[ADDR(x, y)] & Notch) == Notch)
		OpenThreadhold = Charge_Notch;
	else
		return ret;

	/*compare carge rate with 3*3 node*/
	/*by pass => 1.no compare 2.corner 3.Skip_Micro 4.full open fail node*/
	for (k = 0; k < 8; k++) {
		tempX = x + sx[k];
		tempY = y + sy[k];

		if ((tempX < 0) || (tempX >= core_mp->xch_len) || (tempY < 0) || (tempY >= core_mp->ych_len)) /*out of range */
			continue;

		if ((inNodeType[ADDR(tempX, tempY)] == NO_COMPARE) || ((inNodeType[ADDR(tempX, tempY)] & Round_Corner) == Round_Corner) ||
		((inNodeType[ADDR(tempX, tempY)] & Skip_Micro) == Skip_Micro) || charge_rate[ADDR(tempX, tempY)] == 0)
			continue;

		if ((charge_rate[ADDR(tempX, tempY)]-charge_rate[ADDR(x, y)])> OpenThreadhold)
			return OpenThreadhold;
	}
	return ret;
}

/* This will be merged to allnode_mutual_cdc_data in next version */
int allnode_open_cdc_data(int mode, int *buf, int *dac)
{
	int i = 0;
	int res = 0;
	int len = 0;
	int inDACp = 0;
	int inDACn = 0;
	uint8_t cmd[15] = {0};
	uint8_t *ori = NULL;
	char str[128] = {0};
	char tmp[128] = {0};
	char *key[] = {"OPEN DAC", "OPEN Raw1", "OPEN Raw2", "OPEN Raw3"};

	/* Multipling by 2 is due to the 16 bit in each node */
	len = (core_mp->xch_len * core_mp->ych_len * 2) + 2;

	TPD_DEBUG("Read X/Y Channel length = %d\n", len);
	TPD_DEBUG("core_mp->frame_len = %d, mode= %d\n", core_mp->frame_len, mode);

	if (len <= 2) {
		TPD_INFO("Length is invalid\n");
		res = -1;
		goto out;
	}

	/* CDC init. Read command from ini file */
	res = core_parser_get_int_data("PV5_4 Command", key[mode], str);
	if (res < 0) {
		TPD_INFO("Failed to parse PV54 command, res = %d\n",res);
		goto out;
	}

	strncpy(tmp, str, res);
	core_parser_get_u8_array(tmp, cmd);

	dump_data(cmd, 8, sizeof(cmd), 0, "Open SP command");

	ipd->Mp_test_data_ready = false;
	res = core_write(core_config->slave_i2c_addr, cmd, protocol->cdc_len);
	if (res < 0) {
		TPD_INFO("I2C Write Error while initialising cdc\n");
		goto out;
	}

	/* Check busy */
	TPD_INFO("Check busy method = %d\n",core_mp->busy_cdc);
	if (core_mp->busy_cdc == POLL_CHECK) {
		res = core_config_check_cdc_busy(50);
	} else if (core_mp->busy_cdc == INT_CHECK) {
		res = core_config_check_data_ready();
	} else if (core_mp->busy_cdc == DELAY_CHECK) {
		mdelay(600);
	}

	if (res < 0) {
		TPD_INFO("Check busy timeout !\n");
		res = -1;
	}

	/* Prepare to get cdc data */
	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_cdc;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		TPD_INFO("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	/* Allocate a buffer for the original */
	ori = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ori)) {
		TPD_INFO("Failed to allocate ori mem (%ld)\n", PTR_ERR(ori));
		goto out;
	}

	/* Get original frame(cdc) data */
	res = core_read(core_config->slave_i2c_addr, ori, len);
	if (res < 0) {
		TPD_INFO("I2C Read Error while getting original cdc data\n");
		goto out;
	}

	dump_data(ori, 8, len, 0, "Open SP CDC original");

	/* Convert original data to the physical one in each node */
	for (i = 0; i < core_mp->frame_len; i++) {
		if (mode == 0) {
			/* DAC - P */
			if (((ori[(2 * i) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F);
			} else {
				inDACp = ori[(2 * i) + 1] & 0x7F;
			}

			/* DAC - N */
			if (((ori[(1 + (2 * i)) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
			} else {
				inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
			}

			buf[i] = (inDACp + inDACn) / 2;
		} else {
			/* H byte + L byte */
			int32_t tmp = (ori[(2 * i) + 1] << 8) + ori[(1 + (2 * i)) + 1];
			if ((tmp & 0x8000) == 0x8000)
				buf[i] = tmp - 65536;
			else
				buf[i] = tmp;
			buf[i] = (int)((int)(dac[i] * 2 * 10000 * 161 / 100) - (int)(16384 / 2 - (int)buf[i]) * 20000 * 7 / 16384 * 36 / 10) / 31 / 2;
		}
	}
	dump_data(buf, 10, core_mp->frame_len,  core_mp->xch_len, "Open SP CDC combined");
out:
	ipio_kfree((void **)&ori);

	return res;
}

static int open_test_sp(int index)
{
	struct mp_test_P540_open open[tItems[index].frame_count];
	int i = 0;
	int x = 0;
	int y = 0;
	int res = 0;
	int addr = 0;
	int Charge_AA = 0;
	int Charge_Border = 0;
	int Charge_Notch =0;
	int full_open_rate = 0;
	char str[512] = { 0 };
	TPD_DEBUG("index = %d, name = %s, CMD = 0x%x, Frame Count = %d\n",
	    index, tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

	/*
	 * We assume that users who are calling the test forget to config frame count
	 * as 1, so we just help them to set it up.
	 */
	if (tItems[index].frame_count <= 0) {
		TPD_INFO("Frame count is zero, which is at least set as 1\n");
		tItems[index].frame_count = 1;
	}

	if(tItems[index].test_count == 0)
	{
		res = create_mp_test_frame_buffer(index, tItems[index].frame_count);
		if (res < 0)
			goto out;
		frame1_cbk700 = vmalloc(core_mp->frame_len * sizeof(int32_t));
		frame1_cbk250 = vmalloc(core_mp->frame_len * sizeof(int32_t));
		frame1_cbk200 = vmalloc(core_mp->frame_len * sizeof(int32_t));

		if (ERR_ALLOC_MEM(frame1_cbk700) || ERR_ALLOC_MEM(frame1_cbk250) || ERR_ALLOC_MEM(frame1_cbk200)){
			TPD_INFO("Failed to allocate cbk buffer\n");
			return -ENOMEM;
		}
	}

	tItems[index].node_type = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(tItems[index].node_type)){
		TPD_INFO("Failed to allocate node_type FRAME buffer\n");
		return -ENOMEM;
	}

	/* Init Max/Min buffer */
	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
				tItems[index].max_buf[y * core_mp->xch_len + x] = INT_MIN;
				tItems[index].min_buf[y * core_mp->xch_len + x] = INT_MAX;
		}
	}

	if (tItems[index].spec_option == BENCHMARK) {
		core_parser_benchmark(tItems[index].bench_mark_max, tItems[index].bench_mark_min, tItems[index].type_option,tItems[index].desp);
		if (LEVEL_DEBUG == tp_debug)                               
			dump_benchmark_data(tItems[index].bench_mark_max , tItems[index].bench_mark_min);
	}

	core_parser_nodetype(tItems[index].node_type,"Node Type");
	if (LEVEL_DEBUG == tp_debug)                               
		dump_node_type_buffer(tItems[index].node_type, "node type");

	res = core_parser_get_int_data(tItems[index].desp, "Charge_AA", str);
	if (res || res == 0)
		Charge_AA = katoi(str);

	res = core_parser_get_int_data(tItems[index].desp, "Charge_Border", str);
	if (res || res == 0)
		Charge_Border = katoi(str);

	res = core_parser_get_int_data(tItems[index].desp, "Charge_Notch", str);
	if (res || res == 0)
		Charge_Notch = katoi(str);

	res = core_parser_get_int_data(tItems[index].desp, "Full Open", str);
	if (res || res == 0)
		full_open_rate = katoi(str);

	if (res < 0) {
		TPD_INFO("Failed to get parameters from ini file\n");
		goto out;
	}

	TPD_DEBUG("pen test frame_cont %d, AA %d,Border %d, Notch %d, full_open_rate %d \n",
			tItems[index].frame_count,Charge_AA,Charge_Border,Charge_Notch,full_open_rate);

	for(i = 0; i < tItems[index].frame_count ; i++) {
		open[i].cbk_700 = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].cbk_250 = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].cbk_200 = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].charg_rate = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].full_Open = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].dac = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		open[i].cdc = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);

		if (ERR_ALLOC_MEM(open[i].cbk_700 ) || ERR_ALLOC_MEM(open[i].cbk_250 ) ||
			ERR_ALLOC_MEM(open[i].cbk_200 ) || ERR_ALLOC_MEM(open[i].charg_rate ) ||
			ERR_ALLOC_MEM(open[i].full_Open ) || ERR_ALLOC_MEM(open[i].dac ) ||
			ERR_ALLOC_MEM(open[i].cdc )){
				TPD_INFO("Failed to allocate open test buffer\n");
				goto out;
		}
	}

	for (i = 0; i < tItems[index].frame_count; i++) {
		res = allnode_open_cdc_data(0, open[i].dac, open[i].dac);
		if (res < 0) {
			TPD_INFO("Failed to get Open SP DAC data, %d\n", res);
			goto out;
		}
		res = allnode_open_cdc_data(1, open[i].cbk_700, open[i].dac);
		if (res < 0) {
			TPD_INFO("Failed to get Open SP Raw1 data, %d\n", res);
			goto out;
		}
		res = allnode_open_cdc_data(2, open[i].cbk_250, open[i].dac);
		if (res < 0) {
			TPD_INFO("Failed to get Open SP Raw2 data, %d\n", res);
			goto out;
		}
		res = allnode_open_cdc_data(3, open[i].cbk_200, open[i].dac);
		if (res < 0) {
			TPD_INFO("Failed to get Open SP Raw3 data, %d\n", res);
			goto out;
		}
        addr = 0;

		/*record fist frame for drbug*/
		if(i == 0)
		{
			memcpy(frame1_cbk700, open[i].cbk_700, core_mp->frame_len * sizeof(int32_t));
			memcpy(frame1_cbk250, open[i].cbk_250, core_mp->frame_len * sizeof(int32_t));
			memcpy(frame1_cbk200, open[i].cbk_200, core_mp->frame_len * sizeof(int32_t));
		}

		dump_data(open[i].cbk_700, 10, core_mp->frame_len, core_mp->xch_len, "cbk 700");
		dump_data(open[i].cbk_250, 10, core_mp->frame_len, core_mp->xch_len, "cbk 250");
		dump_data(open[i].cbk_200, 10, core_mp->frame_len, core_mp->xch_len, "cbk 200");
        for(y = 0; y < core_mp->ych_len; y++){
            for(x = 0; x < core_mp->xch_len; x++){
                open[i].charg_rate[addr] = open[i].cbk_250[addr] * 100 / open[i].cbk_700[addr];
				open[i].full_Open[addr] = open[i].cbk_700[addr] - open[i].cbk_200[addr];
                addr++;
            }
        }
		if (LEVEL_DEBUG == tp_debug)
		{
			dump_data(open[i].charg_rate, 10, core_mp->frame_len, core_mp->xch_len, "origin charge rate");
			dump_data(open[i].full_Open, 10, core_mp->frame_len, core_mp->xch_len, "origin full open");
		}

        addr = 0;
        for(y = 0; y < core_mp->ych_len; y++){
            for(x = 0; x < core_mp->xch_len; x++){
				if(full_open_rate_compare(open[i].full_Open, open[i].cbk_700, x, y, tItems[index].node_type[addr], full_open_rate) == false)
				{
                	tItems[index].buf[(i * core_mp->frame_len) + addr] = 0;
					open[i].charg_rate[addr] = 0;
				}
                addr++;
            }
        }
		if (LEVEL_DEBUG == tp_debug)
			dump_data(&tItems[index].buf[(i * core_mp->frame_len)], 10, core_mp->frame_len, core_mp->xch_len, "after compare charge rate");

        addr = 0;
        for(y = 0; y < core_mp->ych_len; y++){
            for(x = 0; x < core_mp->xch_len; x++){
                tItems[index].buf[(i * core_mp->frame_len) + addr] = compare_charge(open[i].charg_rate, x, y, tItems[index].node_type, Charge_AA, Charge_Border, Charge_Notch);
                addr++;
            }
        }
		if (LEVEL_DEBUG == tp_debug)
			dump_data(&tItems[index].buf[(i * core_mp->frame_len)], 10, core_mp->frame_len, core_mp->xch_len, "after full_open_rate_compare");

		compare_MaxMin_result(index, &tItems[index].buf[(i * core_mp->frame_len)]);
	}

out:
	ipio_kfree((void **)&tItems[index].node_type);

	for(i = 0; i < tItems[index].frame_count; i++) {
		ipio_kfree((void **)&open[i].cbk_700);
		ipio_kfree((void **)&open[i].cbk_250);
		ipio_kfree((void **)&open[i].cbk_200);
		ipio_kfree((void **)&open[i].charg_rate);
		ipio_kfree((void **)&open[i].full_Open);
		ipio_kfree((void **)&open[i].dac);
	}

	return res;
}

int codeToOhm(int32_t Code)
{
	int douTDF1 = 0;
	int douTDF2 = 0;
	int douTVCH = 24;
	int douTVCL = 8;
	int douCint = 7;
	int douVariation = 64;
	int douRinternal = 930;
	int32_t temp = 0;

	if (core_mp->nodp.isLongV) {
		douTDF1 = 300;
		douTDF2 = 100;
	} else {
		douTDF1 = 219;
		douTDF2 = 100;
	}

	if(Code == 0)
	{
		TPD_INFO("code is invalid\n");
	}
	else
	{
		temp = ((douTVCH - douTVCL) * douVariation * (douTDF1 - douTDF2) * (1<<12) / (9 * Code * douCint)) * 100;
		temp = (temp - douRinternal) / 1000;
	}
	/* Unit = M Ohm */
	return temp;
}

static int short_test(int index,int frame_index)
{
	int j = 0;
	int res = 0;

	if(protocol->major >= 5 && protocol->mid >= 4) {
		/* Calculate code to ohm and save to tItems[index].buf */
		for (j = 0; j < core_mp->frame_len; j++)
			tItems[index].buf[frame_index * core_mp->frame_len + j] = codeToOhm(frame_buf[j]);
	} else {
		for (j = 0; j < core_mp->frame_len; j++)
			tItems[index].buf[frame_index * core_mp->frame_len + j] = frame_buf[j];		
	}

	return res;
}

static int mutual_test(int index)
{
	int i = 0;
	int j = 0;
	int x = 0;
	int y = 0;
	int res = 0;
	int get_frame_cont =1 ;

	TPD_DEBUG("index = %d, name = %s, CMD = 0x%x, Frame Count = %d\n",
	    index, tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

	/*
	 * We assume that users who are calling the test forget to config frame count
	 * as 1, so we just help them to set it up.
	 */
	if (tItems[index].frame_count <= 0) {
		TPD_INFO("Frame count is zero, which is at least set as 1\n");
		tItems[index].frame_count = 1;
	}

	//makesure it's first time
	if(tItems[index].test_count == 0)
	{
		res = create_mp_test_frame_buffer(index, tItems[index].frame_count);
		if (res < 0)
			goto out;
	}

	/* Init Max/Min buffer */
	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			if (tItems[i].catalog == TX_RX_DELTA) {
				core_mp->tx_max_buf[y * core_mp->xch_len + x] = INT_MIN;
				core_mp->rx_max_buf[y * core_mp->xch_len + x] = INT_MIN;
				core_mp->tx_min_buf[y * core_mp->xch_len + x] = INT_MAX;
				core_mp->rx_min_buf[y * core_mp->xch_len + x] = INT_MAX;
			} else {
				tItems[index].max_buf[y * core_mp->xch_len + x] = INT_MIN;
				tItems[index].min_buf[y * core_mp->xch_len + x] = INT_MAX;
			}
		}
	}
	if (tItems[index].catalog != PEAK_TO_PEAK_TEST)
		get_frame_cont = tItems[index].frame_count;

	if (tItems[index].spec_option == BENCHMARK) {
		core_parser_benchmark(tItems[index].bench_mark_max, tItems[index].bench_mark_min, tItems[index].type_option,tItems[index].desp);
		if (LEVEL_DEBUG == tp_debug)                               
			dump_benchmark_data(tItems[index].bench_mark_max , tItems[index].bench_mark_min);
	}

	for (i = 0; i < get_frame_cont; i++) {
		res = allnode_mutual_cdc_data(index);
		if (res < 0) {
			TPD_INFO("Failed to initialise CDC data, %d\n", res);
			goto out;
		}
		switch (tItems[index].catalog) {
		case PIXEL:
			run_pixel_test(index);
			break;
		case UNTOUCH_P2P:
			run_untouch_p2p_test(index);
			break;
		case OPEN_TEST:
			run_open_test(index);
			break;
		case TX_RX_DELTA:
			run_tx_rx_delta_test(index);
			break;
		case SHORT_TEST:
			short_test(index , i);
			break;			
		default:
			for (j = 0; j < core_mp->frame_len; j++)
				tItems[index].buf[i * core_mp->frame_len + j] = frame_buf[j];
			break;
		}
		
		compare_MaxMin_result(index, &tItems[index].buf[i * core_mp->frame_len]);
	}



out:
	return res;
}

static int key_test(int index)
{
	int i;
	int j = 0;
	int res = 0;

	TPD_DEBUG("Item = %s, CMD = 0x%x, Frame Count = %d\n",
	    tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

	if (tItems[index].frame_count == 0) {
		TPD_INFO("Frame count is zero, which at least sets as 1\n");
		res = -EINVAL;
		goto out;
	}

	//makesure it's first time
	if(tItems[index].test_count == 0)
	{
		res = create_mp_test_frame_buffer(index, tItems[index].frame_count);
		if (res < 0)
			goto out;
	}

	for (i = 0; i < tItems[index].frame_count; i++) {
		res = allnode_key_cdc_data(index);
		if (res < 0) {
			TPD_INFO("Failed to initialise CDC data, %d\n", res);
			goto out;
		}

		for (j = 0; j < core_mp->key_len; j++)
			tItems[index].buf[j] = key_buf[j];
	}

	compare_MaxMin_result(index, tItems[index].buf);

out:
	return res;
}

static int self_test(int index)
{
	TPD_INFO("TDDI has no self to be tested currently\n");
	return -1;
}

static int st_test(int index)
{
	TPD_INFO("ST Test is not supported by the driver\n");
	return -1;
}

int mp_test_data_sort_average(int32_t *oringin_data,int index, int32_t *avg_result)
{
	int i = 0;
	int j = 0;
	int k = 0;
	int x = 0;
	int y = 0;
	int len = 5;
	int32_t u32temp = 0;
	int u32up_frame = 0;
	int u32down_frame = 0;
	int32_t* u32sum_raw_data = NULL;
	int32_t* u32data_buff = NULL;

	if(tItems[index].frame_count <= 1)
		return 0;


	if (ERR_ALLOC_MEM(oringin_data)){
		TPD_INFO("Input wrong adress\n");
			return -ENOMEM;
	}

	u32data_buff = kcalloc(core_mp->frame_len * tItems[index].frame_count, sizeof(int32_t), GFP_KERNEL);
	u32sum_raw_data = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(u32sum_raw_data) || (ERR_ALLOC_MEM(u32data_buff))){
		TPD_INFO("Failed to allocate u32sum_raw_data FRAME buffer\n");
		return -ENOMEM;
	}

	for(i = 0 ; i < core_mp->frame_len * tItems[index].frame_count ; i++)
	{
		u32data_buff[i] = oringin_data[i];
	}

	u32up_frame = tItems[index].frame_count * tItems[index].highest_percentage / 100;
	u32down_frame = tItems[index].frame_count * tItems[index].lowest_percentage / 100;
	TPD_DEBUG("Up=%d,Down=%d -%s\n",u32up_frame,u32down_frame,tItems[index].desp);

	if (LEVEL_DEBUG == tp_debug) {
		printk("\n[Show Original frist%d and last%d node data]\n",len,len);
		for(i = 0 ; i < core_mp->frame_len ; i++){
			for(j = 0 ; j < tItems[index].frame_count ; j++){
				if((i<len) || (i >= (core_mp->frame_len-len)))
					printk("%d,",u32data_buff[j* core_mp->frame_len + i]);
			}
			if((i<len) || (i >=(core_mp->frame_len-len)))
				printk("\n");
		}
	}

	for(i = 0 ; i < core_mp->frame_len ; i++){
		for(j = 0 ; j < tItems[index].frame_count-1 ; j++){
			for(k = 0 ; k < (tItems[index].frame_count-1-j) ; k++){
				x=i+k*core_mp->frame_len;
				y=i+(k+1)*core_mp->frame_len;
				if (*(u32data_buff+x) > *(u32data_buff+y))
				{
					u32temp = *(u32data_buff+x);
					*(u32data_buff+x) = *(u32data_buff+y);
					*(u32data_buff+y) = u32temp;
				}
			}
		}
	}

	if (LEVEL_DEBUG == tp_debug) {
		printk("\n[After sorting frist%d and last%d node data]\n",len,len);
		for(i = 0 ; i < core_mp->frame_len ; i++){
			for(j = u32down_frame ; j < tItems[index].frame_count - u32up_frame ; j++){
				if((i<len) || (i >= (core_mp->frame_len-len)))
					printk("%d,",u32data_buff[i + j * core_mp->frame_len]);
			}
			if((i<len) || (i >= (core_mp->frame_len-len)))
				printk("\n");
		}
	}

	for(i = 0 ; i < core_mp->frame_len ; i++){
		u32sum_raw_data[i]=0;
		for(j = u32down_frame ; j < tItems[index].frame_count - u32up_frame ; j++)
			u32sum_raw_data[i] += u32data_buff[i + j * core_mp->frame_len];

		avg_result[i] = u32sum_raw_data[i] / (tItems[index].frame_count - u32down_frame - u32up_frame);
	}


	if (LEVEL_DEBUG == tp_debug) {
		printk("\n[Average result frist%d and last%d node data]\n",len,len);
		for(i = 0 ; i < core_mp->frame_len ; i++){
			if((i<len) || (i >= (core_mp->frame_len-len)))
				printk("%d,",avg_result[i]);
		}
		if((i<len) || (i >= (core_mp->frame_len-len)))
			printk("\n");
	}

	ipio_kfree((void **)&u32data_buff);
	ipio_kfree((void **)&u32sum_raw_data);
	return 0;
}

int core_mp_compare_retry_cdc_result(int i)
{
	int j = 0;
	int test_result = MP_PASS;
	int32_t *max_threshold = NULL;
	int32_t *min_threshold = NULL;

	max_threshold = vmalloc(core_mp->frame_len * sizeof(int32_t));
	min_threshold = vmalloc(core_mp->frame_len * sizeof(int32_t));
	if (ERR_ALLOC_MEM(max_threshold) || ERR_ALLOC_MEM(min_threshold)) {
		TPD_INFO("Failed to allocate threshold FRAME buffer\n");
		test_result = MP_FAIL;
		goto fail_open;
	}

	/* Show test result as below */
	if (tItems[i].catalog == TX_RX_DELTA) {

		if (ERR_ALLOC_MEM(core_mp->rx_delta_buf) || ERR_ALLOC_MEM(core_mp->tx_delta_buf)) {
			TPD_INFO("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
			test_result = MP_FAIL;
			goto fail_open;
		}

		for(j = 0 ;j < core_mp->frame_len ; j++) {
			max_threshold[j] = core_mp->TxDeltaMax;
			min_threshold[j] = core_mp->TxDeltaMin;
		}
		mp_compare_cdc_result(core_mp->tx_max_buf, max_threshold, min_threshold, &test_result);
		mp_compare_cdc_result(core_mp->tx_min_buf, max_threshold, min_threshold, &test_result);
		for(j = 0 ;j < core_mp->frame_len ; j++) {
			max_threshold[j] = core_mp->RxDeltaMax;
			min_threshold[j] = core_mp->RxDeltaMin;
		}
		mp_compare_cdc_result(core_mp->rx_max_buf, max_threshold, min_threshold, &test_result);
		mp_compare_cdc_result(core_mp->rx_min_buf, max_threshold, min_threshold, &test_result);
	} else {

		if (ERR_ALLOC_MEM(tItems[i].buf) || ERR_ALLOC_MEM(tItems[i].max_buf) ||
				ERR_ALLOC_MEM(tItems[i].min_buf) || ERR_ALLOC_MEM(tItems[i].result_buf)) {
			TPD_INFO("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
			test_result = MP_FAIL;
			goto fail_open;
		}

		if ( tItems[i].spec_option == BENCHMARK){
			for(j = 0 ;j < core_mp->frame_len ; j++){
				max_threshold[j] = tItems[i].bench_mark_max[j];
				min_threshold[j] = tItems[i].bench_mark_min[j];
			}
		} else {
			for(j = 0 ;j < core_mp->frame_len ; j++) {
				max_threshold[j] = tItems[i].max;
				min_threshold[j] = tItems[i].min;
			}
		}
		/* general result */
		if(tItems[i].trimmed_mean && tItems[i].catalog != PEAK_TO_PEAK_TEST)
		{
			mp_test_data_sort_average(tItems[i].buf, i, tItems[i].result_buf);
			mp_compare_cdc_result(tItems[i].result_buf, max_threshold, min_threshold, &test_result);
		}
		else
		{
			mp_compare_cdc_result(tItems[i].max_buf, max_threshold, min_threshold, &test_result);
			mp_compare_cdc_result(tItems[i].min_buf, max_threshold, min_threshold, &test_result);
		}
	}

fail_open:
	ipio_vfree((void **)&max_threshold);
	ipio_vfree((void **)&min_threshold);

	return test_result;
}

void core_mp_retry(int index)
{
	core_config_ice_mode_enable();

	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
	}

	core_config_ic_reset();

	core_config_ice_mode_disable();
    ilitek_reset((void *)ipd);

	tItems[index].do_test(index);
	tItems[index].test_count ++;
}


void core_mp_show_result(void)
{
	int i = 0;
	int x = 0;
	int y = 0;
	int j = 0;
	int csv_len = 0;
	int pass_item_count = 0;
	int line_count = 0;
	int get_frame_cont = 1;
	int32_t *max_threshold = NULL;
	int32_t *min_threshold = NULL;
	char *csv = NULL;
	char csv_name[128] = { 0 };
	char *ret_pass_name = NULL;
	char *ret_fail_name = NULL;
	struct file *f = NULL;
	mm_segment_t fs;
	loff_t pos;

	struct timespec now_time;
	struct rtc_time rtc_now_time;
	uint8_t data_buf[128];

	csv = vmalloc(CSV_FILE_SIZE);
	if (ERR_ALLOC_MEM(csv)) {
		TPD_INFO("Failed to allocate CSV mem\n");
		goto fail_open;
	}

	max_threshold = vmalloc(core_mp->frame_len * sizeof(int32_t));
	min_threshold = vmalloc(core_mp->frame_len * sizeof(int32_t));
	if (ERR_ALLOC_MEM(max_threshold) || ERR_ALLOC_MEM(min_threshold)) {
		TPD_INFO("Failed to allocate threshold FRAME buffer\n");
		goto fail_open;
	}
	/* header must has 19 line*/
	csv_len += sprintf(csv + csv_len,"==============================================================================\n");
	line_count++;
	csv_len += sprintf(csv + csv_len,"ILITek C-TP Utility V%s  %x : Driver Sensor Test\n", DRIVER_VERSION, core_config->chip_pid);
	line_count++;
	csv_len += sprintf(csv + csv_len,"Confidentiality Notice:\n");
	line_count++;
	csv_len += sprintf(csv + csv_len,"Any information of this tool is confidential and privileged.\n");
	line_count++;
	csv_len += sprintf(csv + csv_len,"@ ILI TECHNOLOGY CORP. All Rights Reserved.\n");
	line_count++;
	csv_len += sprintf(csv + csv_len,"==============================================================================\n");
	line_count++;
	if (protocol->mid >= 0x3) {/*line7*/
		csv_len += sprintf(csv + csv_len,"Firmware Version ,V%d.%d.%d.%d\n", core_config->firmware_ver[1], core_config->firmware_ver[2], core_config->firmware_ver[3], core_config->firmware_ver[4]);
	} else {
		csv_len += sprintf(csv + csv_len,"Firmware Version ,V%d.%d.%d\n",core_config->firmware_ver[1], core_config->firmware_ver[2], core_config->firmware_ver[3]);
	}
	line_count++;
	csv_len += sprintf(csv + csv_len,"Panel information ,XCH=%d, YCH=%d\n", core_mp->xch_len, core_mp->ych_len);
	line_count++;
	csv_len += sprintf(csv + csv_len,"Test Item:\n");
	line_count++;
	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].run == 1)
		{
			csv_len += sprintf(csv + csv_len, "	  ---%s\n", tItems[i].desp);
			line_count++;
		}
	}
	while(line_count < 19)
	{
		csv_len += sprintf(csv + csv_len,"\n");
		line_count++;
	}
	csv_len += sprintf(csv + csv_len,"==============================================================================\n");

	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].run != 1)
			continue;

		if (tItems[i].item_result == MP_PASS)
		{
			TPD_INFO("\n\n[%s],OK \n", tItems[i].desp);
			csv_len += sprintf(csv + csv_len, "\n\n[%s],OK\n", tItems[i].desp);
		}else{
			TPD_INFO("\n\n[%s],NG \n", tItems[i].desp);
			csv_len += sprintf(csv + csv_len, "\n\n[%s],NG\n", tItems[i].desp);
		}

		TPD_INFO("Frame count = %d\n",tItems[i].frame_count);
		csv_len += sprintf(csv + csv_len, "Frame count = %d\n", tItems[i].frame_count);

		if(tItems[i].trimmed_mean && tItems[i].catalog != PEAK_TO_PEAK_TEST) {
			TPD_INFO("Lowest Percentage = %d\n",tItems[i].lowest_percentage);
			csv_len += sprintf(csv + csv_len, "Lowest Percentage = %d\n", tItems[i].lowest_percentage);

			TPD_INFO("Highest Percentage = %d\n",tItems[i].highest_percentage);
			csv_len += sprintf(csv + csv_len, "Highest Percentage = %d\n", tItems[i].highest_percentage);
		}

		/* Show result of benchmark max and min */
		if ( tItems[i].spec_option == BENCHMARK){
			for(j = 0 ;j < core_mp->frame_len ; j++){
				max_threshold[j] = tItems[i].bench_mark_max[j];
				min_threshold[j] = tItems[i].bench_mark_min[j];
			}

			mp_compare_cdc_show_result(tItems[i].bench_mark_max, csv, &csv_len, TYPE_BENCHMARK, max_threshold, min_threshold,"Max_Bench");
			mp_compare_cdc_show_result(tItems[i].bench_mark_min, csv, &csv_len, TYPE_BENCHMARK, max_threshold, min_threshold,"Min_Bench");
		} else {

			for(j = 0 ;j < core_mp->frame_len ; j++) {
				max_threshold[j] = tItems[i].max;
				min_threshold[j] = tItems[i].min;
			}

			TPD_INFO("Max = %d\n",tItems[i].max);
			csv_len += sprintf(csv + csv_len, "Max = %d\n", tItems[i].max);

			TPD_INFO("Min = %d\n",tItems[i].min);
			csv_len += sprintf(csv + csv_len, "Min = %d\n", tItems[i].min);
		}
		if (strcmp(tItems[i].name, "open_integration_sp") == 0)
		{
			mp_compare_cdc_show_result(frame1_cbk700, csv, &csv_len, TYPE_NO_JUGE, max_threshold, min_threshold,"frame1 cbk700");
			mp_compare_cdc_show_result(frame1_cbk250, csv, &csv_len, TYPE_NO_JUGE, max_threshold, min_threshold,"frame1 cbk250");
			mp_compare_cdc_show_result(frame1_cbk200, csv, &csv_len, TYPE_NO_JUGE, max_threshold, min_threshold,"frame1 cbk200");
			ipio_vfree((void **)&frame1_cbk700);
			ipio_vfree((void **)&frame1_cbk250);
			ipio_vfree((void **)&frame1_cbk200);
		}

		if (tItems[i].catalog == TX_RX_DELTA) {
			if (ERR_ALLOC_MEM(core_mp->rx_delta_buf) || ERR_ALLOC_MEM(core_mp->tx_delta_buf)) {
				TPD_INFO("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
				continue;
			}
		} else {
			if (ERR_ALLOC_MEM(tItems[i].buf) || ERR_ALLOC_MEM(tItems[i].max_buf) ||
					ERR_ALLOC_MEM(tItems[i].min_buf)) {
				TPD_INFO("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
				continue;
			}
		}

		/* Show test result as below */
		if (tItems[i].catalog == KEY_TEST) {
			for (x = 0; x < core_mp->key_len; x++) {
				DUMP(DEBUG_MP_TEST, "KEY_%02d ", x);
				csv_len += sprintf(csv + csv_len, "KEY_%02d,", x);
			}

			DUMP(DEBUG_MP_TEST, "\n");
			csv_len += sprintf(csv + csv_len, "\n");

			for (y = 0; y < core_mp->key_len; y++) {
				DUMP(DEBUG_MP_TEST, " %3d   ", tItems[i].buf[y]);
				csv_len += sprintf(csv + csv_len, " %3d, ", tItems[i].buf[y]);
			}

			DUMP(DEBUG_MP_TEST, "\n");
			csv_len += sprintf(csv + csv_len, "\n");
		} else if (tItems[i].catalog == TX_RX_DELTA) {

			for(j = 0 ;j < core_mp->frame_len ; j++) {
				max_threshold[j] = core_mp->TxDeltaMax;
				min_threshold[j] = core_mp->TxDeltaMin;
			}
			mp_compare_cdc_show_result(core_mp->tx_max_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"TX Max Hold");
			mp_compare_cdc_show_result(core_mp->tx_min_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"TX Min Hold");

			for(j = 0 ;j < core_mp->frame_len ; j++) {
				max_threshold[j] = core_mp->RxDeltaMax;
				min_threshold[j] = core_mp->RxDeltaMin;
			}
			mp_compare_cdc_show_result(core_mp->rx_max_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"RX Max Hold");
			mp_compare_cdc_show_result(core_mp->rx_min_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"RX Min Hold");

		} else {
			/* general result */
			if(tItems[i].trimmed_mean && tItems[i].catalog != PEAK_TO_PEAK_TEST){
				mp_compare_cdc_show_result(tItems[i].result_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"Mean result");
			} else {
				mp_compare_cdc_show_result(tItems[i].max_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"Max Hold");
				mp_compare_cdc_show_result(tItems[i].min_buf, csv, &csv_len, TYPE_JUGE, max_threshold, min_threshold,"Min Hold");
			}
			if(tItems[i].catalog != PEAK_TO_PEAK_TEST)
				get_frame_cont = tItems[i].frame_count;
			/* result of each frame */
			for(j = 0; j < get_frame_cont; j++) {
				char frame_name[128] ={ 0 };
				sprintf(frame_name, "Frame %d", (j+1));
				mp_compare_cdc_show_result(&tItems[i].buf[(j*core_mp->frame_len)], csv, &csv_len, TYPE_NO_JUGE, max_threshold, min_threshold, frame_name);
			}

		}
	}

	memset(csv_name, 0, 128 * sizeof(char));

	csv_len += sprintf(csv + csv_len,"==============================================================================\n");
	csv_len += sprintf(csv + csv_len,"Result_Summary           \n");

	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].run) {
			if (tItems[i].item_result == MP_PASS)
				csv_len += sprintf(csv + csv_len,"   {%s}     ,OK\n",tItems[i].desp);
			else
				csv_len += sprintf(csv + csv_len,"   {%s}     ,NG\n",tItems[i].desp);
		}
	}
	if (!(core_mp->oplus_lcm) && core_mp->osc_test) {
		csv_len += sprintf(csv + csv_len, "\n\nOSC read data 0x%02X 0x%02X 0x%02X 0x%02X\n",\
			core_mp->osc_org_data[0], core_mp->osc_org_data[1], core_mp->osc_org_data[2], core_mp->osc_org_data[3]);
		csv_len += sprintf(csv + csv_len, "OSC threshold(%s%d.%d%% ~ +%d.%d%%)\n",\
			(core_mp->osc_threshold_min < 0 && core_mp->osc_threshold_min > -10) ? "-" : " ",\
			core_mp->osc_threshold_min / 10, (int)Mathabs(core_mp->osc_threshold_min % 10),\
			core_mp->osc_threshold_max / 10, core_mp->osc_threshold_max % 10);
		csv_len += sprintf(csv + csv_len, "trim count is %d OSC offset Percent result: %s%d.%d%%  %s \n",\
			((core_mp->osc_org_data[0] << 8) + core_mp->osc_org_data[1]),\
			(core_mp->osc_offset < 0 && core_mp->osc_offset > -10) ? "-" : " ",\
			core_mp->osc_offset / 10, (int)(Mathabs(core_mp->osc_offset % 10)),\
			(core_mp->osc_result == MP_PASS) ? "OSC test pass" : "OSC test NG");
		if (core_mp->osc_result == MP_FAIL) {
			core_mp->fail_items++;
		}
	}
	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].run) {
			if (tItems[i].item_result == MP_FAIL)
			{
				pass_item_count = 0;
				core_mp->fail_items++;
				//break;
			}
			pass_item_count++;
		}
	}

	/* define csv file name */
	if (core_mp->oplus_run) {
		if (core_mp->oplus_lcm) {
			ret_pass_name = OPLUS_CSV_LCM_PASS_NAME;
			ret_fail_name = OPLUS_CSV_LCM_FAIL_NAME;
		} else {
			ret_pass_name = OPLUS_CSV_PASS_NAME;
			ret_fail_name = OPLUS_CSV_FAIL_NAME;
		}
	} else {
		ret_pass_name = NORMAL_CSV_PASS_NAME;
		ret_fail_name = NORMAL_CSV_FAIL_NAME;
	}

    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    snprintf(data_buf, 128, "_%02d%02d%02d-%02d%02d%02d-utc.csv",
            (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
            rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
	fs = get_fs();
	set_fs(KERNEL_DS);
	sys_mkdir("/sdcard/ILITEK/", 0666);
	set_fs(fs);
	if (core_mp->fail_items) {
		core_mp->final_result = MP_FAIL;
		sprintf(csv_name, "%s/%s%s", CSV_PATH, ret_fail_name, data_buf);
	} else {
		core_mp->final_result = MP_PASS;
		sprintf(csv_name, "%s/%s%s", CSV_PATH, ret_pass_name, data_buf);
	}

	TPD_INFO("Open CSV : %s\n", csv_name);

	if (f == NULL)
		f = filp_open(csv_name, O_WRONLY | O_CREAT | O_TRUNC, 644);

	if (ERR_ALLOC_MEM(f)) {
		TPD_INFO("Failed to open CSV file");
		goto fail_open;
	}

	TPD_INFO("Open CSV succeed, its length = %d\n ", csv_len);

	if (csv_len >= CSV_FILE_SIZE) {
		TPD_INFO("The length saved to CSV is too long !\n");
		goto fail_open;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_write(f, csv, csv_len, &pos);
	set_fs(fs);
	filp_close(f, NULL);

	TPD_INFO("Writing Data into CSV succeed\n");

fail_open:
	ipio_vfree((void **)&csv);
	ipio_vfree((void **)&max_threshold);
	ipio_vfree((void **)&min_threshold);
}
EXPORT_SYMBOL(core_mp_show_result);

void core_mp_run_test(char *item, bool ini)
{
	int i = 0 , count ,test_result = MP_PASS;
	char str[512] = { 0 };

	/* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
	core_mp->ych_len = core_config->tp_info->nYChannelNum;

	/* update key's length if they're changed */
	core_mp->key_len = core_config->tp_info->nKeyCount;

	/* compute the total length in one frame */
	core_mp->frame_len = core_mp->xch_len * core_mp->ych_len;

	if (item == NULL || strncmp(item, " ", strlen(item)) == 0 || core_mp->frame_len == 0 ) {
		tItems[i].result = "FAIL";
		core_mp->final_result = MP_FAIL;
		TPD_INFO("Invaild string or length\n");
		return;
	}

	TPD_DEBUG("item = %s, core type = %d\n", item, core_config->core_type);

	for (i = 0; i < core_mp->mp_items; i++) {
		if (strncmp(item, tItems[i].desp, strlen(item)) == 0) {
			if (ini) {
				core_parser_get_int_data(item, "Enable", str);
				tItems[i].run = katoi(str);
				core_parser_get_int_data(item, "SPEC Option", str);
				tItems[i].spec_option= katoi(str);
				core_parser_get_int_data(item, "Type Option", str);
				tItems[i].type_option= katoi(str);
 				core_parser_get_int_data(item, "Frame Count", str);
				tItems[i].frame_count= katoi(str);
				core_parser_get_int_data(item, "Trimmed Mean", str);
				tItems[i].trimmed_mean= katoi(str);
				core_parser_get_int_data(item, "Lowest Percentage", str);
				tItems[i].lowest_percentage= katoi(str);
 				core_parser_get_int_data(item, "Highest Percentage", str);
				tItems[i].highest_percentage= katoi(str);

				/* Get threshold from ini structure in parser */
				if (strcmp(item, "Tx/Rx Delta") == 0) {
					core_parser_get_int_data(item, "Tx Max", str);
					core_mp->TxDeltaMax = katoi(str);
					core_parser_get_int_data(item, "Tx Min", str);
					core_mp->TxDeltaMin = katoi(str);
					core_parser_get_int_data(item, "Rx Max", str);
					core_mp->RxDeltaMax = katoi(str);
					core_parser_get_int_data(item, "Rx Min", str);
					core_mp->RxDeltaMin = katoi(str);
					TPD_DEBUG("%s: Tx Max = %d, Tx Min = %d, Rx Max = %d,  Rx Min = %d\n",
							tItems[i].desp, core_mp->TxDeltaMax, core_mp->TxDeltaMin,
							core_mp->RxDeltaMax, core_mp->RxDeltaMin);
				} else {
					core_parser_get_int_data(item, "Max", str);
					tItems[i].max = katoi(str);
					core_parser_get_int_data(item, "Min", str);
					tItems[i].min = katoi(str);
				}

				core_parser_get_int_data(item, "Frame Count", str);
				tItems[i].frame_count = katoi(str);

				TPD_DEBUG("%s: run = %d, max = %d, min = %d, frame_count = %d\n", tItems[i].desp,
						tItems[i].run, tItems[i].max, tItems[i].min, tItems[i].frame_count);
			}

			if (tItems[i].run) {
				/* LCM off */
				if (strnstr(tItems[i].desp, "LCM", strlen(tItems[i].desp)) != NULL) {
					if (!core_mp->oplus_run && !core_mp->oplus_lcm)
						core_mp_ctrl_lcm_status(false);
				}

				TPD_INFO("Running Test Item : %s\n", tItems[i].desp);
				tItems[i].do_test(i);
				tItems[i].test_count ++;
				test_result = core_mp_compare_retry_cdc_result(i);

				/* To see if this item needs to do retry  */
				if (core_mp->retry && (test_result == MP_FAIL)) {
					for(count = 0; count < RETRY_COUNT; count++)
					{
						TPD_INFO("retry = %d, item = %s\n", (count+1), tItems[i].desp);
						core_mp_retry(i);
						test_result = core_mp_compare_retry_cdc_result(i);
						if(test_result == MP_PASS)
							break;
					}
				}
				tItems[i].item_result = test_result;
				/* LCM on */
				if (strnstr(tItems[i].desp, "LCM", strlen(tItems[i].desp)) != NULL) {
					if (!core_mp->oplus_run && !core_mp->oplus_lcm)
						core_mp_ctrl_lcm_status(true);
				}
			}
			break;
		}
	}
}
EXPORT_SYMBOL(core_mp_run_test);

int ilitek_osc_check(void)
{
	uint8_t test_cmd[8] = { 0 };
	int res = 0;
	int i = 0;
	char *item = "Osc Check";
	char str[512] = { 0 };
	res = core_parser_get_int_data(item, "Enable", str);
	if (res <= 0) {
		TPD_INFO("Failed to get osc_test enable data, %d\n", res);
		core_mp->osc_test = 1;
	}
	else {
		core_mp->osc_test = katoi(str);
	}
	res = core_parser_get_int_data(item, "Max", str);
	if (res <= 0) {
		TPD_INFO("Failed to get osc_threshold_max data, %d\n", res);
		core_mp->osc_threshold_max = 20;
	}
	else {
		core_mp->osc_threshold_max = katoi(str);
	}
	res = core_parser_get_int_data(item, "Min", str);
	if (res <= 0) {
		TPD_INFO("Failed to get osc_threshold_min data, %d\n", res);
		core_mp->osc_threshold_min = -25;
	}
	else {
		core_mp->osc_threshold_min = katoi(str);
	}
	if (!core_mp->osc_test) {
		core_mp->osc_result = MP_PASS;
		return 0;
	}
	for (i = 0; i < 3; i++) {
		test_cmd[0] = 0xF1;	
		test_cmd[1] = 0x40;	
		test_cmd[2] = 0x00;	
		res = core_write(core_config->slave_i2c_addr, test_cmd, 3);
		if (res < 0) {
			TPD_INFO("Failed to write data, %d\n", res);
			mdelay(20);
			continue;
		}	
		res = core_read(core_config->slave_i2c_addr, test_cmd, 5);
		if (res < 0) {
			TPD_INFO("Failed to read tp set ddi trim code %d\n", res);
			mdelay(20);
			continue;
		}
		else {
			TPD_INFO("read osc value 0x%X 0x%X 0x%X 0x%X\n", test_cmd[0], test_cmd[1], test_cmd[2], test_cmd[3]);
			core_mp->osc_org_data[0] = test_cmd[0];
			core_mp->osc_org_data[1] = test_cmd[1];
			core_mp->osc_org_data[2] = test_cmd[2];
			core_mp->osc_org_data[3] = test_cmd[3];
			if((uint8_t)(test_cmd[2] & 0x80) == (uint8_t)0x80)
			{
				//core_mp->osc_offset = (0x10000 - ((test_cmd[2] << 8) + test_cmd[3]));
				core_mp->osc_offset = (((test_cmd[2] << 8) + test_cmd[3]) - 0x10000);
			}
			else
			{
				core_mp->osc_offset = ((test_cmd[2] << 8) + test_cmd[3]);
			}
			TPD_INFO("osc offset org value is %d,  trim count is %d", core_mp->osc_offset, ((test_cmd[0] << 8) + test_cmd[1]));
			break;
		}
	}
	if (i >= 3) {
		TPD_INFO("Failed to read OSC value, %d\n", res);
		res = -1;
	}
	else {
		if (core_mp->osc_offset > core_mp->osc_threshold_max || core_mp->osc_offset < core_mp->osc_threshold_min) {
			TPD_INFO("OSC check failed, osc_offset = %d osc_threshold_max = %d osc_threshold_min = %d\n", core_mp->osc_offset, core_mp->osc_threshold_max, core_mp->osc_threshold_min);
			core_mp->osc_result = MP_FAIL;
		}
		else {
			TPD_INFO("OSC check pass, osc_offset = %d osc_threshold_max = %d osc_threshold_min = %d\n", core_mp->osc_offset, core_mp->osc_threshold_max, core_mp->osc_threshold_min);
			core_mp->osc_result = MP_PASS;
		}
		res = 0;
	}
	return res;
}

int ilitek_mp_test(struct seq_file *s)
{
	uint8_t test_cmd[4] = { 0 };
    int final_result = -1;
	int osc_offset = 65535;
	int osc_result = 0;
	int osc_test = 0;
	int osc_threshold_min = 0;
	int osc_threshold_max = 0;
	uint8_t osc_org_data[4] = {0};
	mutex_lock(&ipd->plat_mutex);
	ipd->esd_check_enabled = false;
	mutex_unlock(&ipd->plat_mutex);
	ilitek_ini_file_data = (struct ini_file_data *)vmalloc(sizeof(struct ini_file_data) * PARSER_MAX_KEY_NUM);
	if (ERR_ALLOC_MEM(ilitek_ini_file_data)) {
		TPD_INFO("Failed to malloc ilitek_ini_file_data\n");
		goto out;
	}
	if (core_parser_path(ipd->test_limit_name) < 0) {
		TPD_INFO("Failed to parsing INI file\n");
		goto out;
	}

	/* Init MP structure */
	if(core_mp_init() < 0) {
		TPD_INFO("Failed to init mp\n");
		goto out;
	}
	/* Start to run MP test */
	mutex_lock(&ipd->plat_mutex);
	core_mp->run = true;
	mutex_unlock(&ipd->plat_mutex);
	core_mp->oplus_lcm = false;

	/* Switch to Test mode */
	test_cmd[0] = protocol->test_mode;
	core_config_mode_control(test_cmd);
    core_fr->isEnableFR = false;
	ipd->esd_check_enabled = false;
	ipd->irq_timer = jiffies;    //reset esd check trigger base time
	#if 1
	/*
	 * Get timing parameters first.
	 * Howerver, this can be ignored if read them from ini.
	 */
	if (protocol->major >= 5 && protocol->mid >= 4) {
		if (core_mp_calc_timing_nodp() < 0) {
			TPD_INFO("Can't get timing parameters\n");
			//goto out;
		}
	}
	#endif
	ilitek_osc_check();
	//msleep(100);
	/* Do not chang the sequence of test */
	core_mp_run_test("Noise Peak To Peak(With Panel)", true);
	core_mp_run_test("Noise Peak to Peak(IC Only)", true);
	core_mp_run_test("Short Test -ILI9881", true);
	core_mp_run_test("Open Test(integration)_SP", true);
	core_mp_run_test("Raw Data(Have BK)", true);
	core_mp_run_test("Calibration Data(DAC)", true);
	core_mp_run_test("Raw Data(No BK)", true);
	core_mp_run_test("Doze Raw Data", true);
	core_mp_run_test("Doze Peak To Peak", true);

	core_mp_show_result();

#ifndef HOST_DOWNLOAD
	core_config_ice_mode_enable();

	if (core_config_set_watch_dog(false) < 0) {
		TPD_INFO("Failed to disable watch dog\n");
	}

	core_config_ic_reset();
#endif
	/* Switch to Demo mode */
	test_cmd[0] = protocol->demo_mode;
	core_config_mode_control(test_cmd);
#ifdef HOST_DOWNLOAD
	//ilitek_platform_tp_hw_reset(true);
	ilitek_reset((void *)ipd);
#endif
	core_mp->run = false;
    core_fr->isEnableFR = true;

    final_result = core_mp->fail_items;
	if (core_mp->osc_result == MP_FAIL && core_mp->osc_test == 1) {
		osc_result = 1;
	}
	else {
		osc_result = 0;
	}
	osc_test = core_mp->osc_test;
	osc_offset = core_mp->osc_offset;
	osc_org_data[0] = core_mp->osc_org_data[0];
	osc_org_data[1] = core_mp->osc_org_data[1];
	osc_org_data[2] = core_mp->osc_org_data[2];
	osc_org_data[3] = core_mp->osc_org_data[3];
	osc_threshold_max = core_mp->osc_threshold_max;
	osc_threshold_min = core_mp->osc_threshold_min;
	core_mp_test_free();
out:
	if (!ERR_ALLOC_MEM(s)) {
		if (core_config->core_type == CORE_TYPE_B) {
			seq_printf(s, "%d error(s). %s\n", final_result,\
				final_result ? "panel test failed. This panel not use in MP" : "All test passed.");
		} else {
			if (osc_test) {
				seq_printf(s, "%d error(s). %s %s	OSC read data 0x%X 0x%X 0x%X 0x%X;	OSC test threshold(%s%d.%d%% ~ +%d.%d%%);    OSC offset Percent result: %s%d.%d%%\n", final_result + osc_result,\
					(final_result + osc_result) ? "test failed." : "All test passed.", final_result ? "MP test data fail" : "MP test data ok",\
					osc_org_data[0], osc_org_data[1], osc_org_data[2], osc_org_data[3],\
					(osc_threshold_min < 0 && osc_threshold_min > -10) ? "-" : " ", osc_threshold_min / 10, (int)(Mathabs(osc_threshold_min % 10)), osc_threshold_max / 10, osc_threshold_max % 10,\
					(osc_offset < 0 && osc_offset > -10) ? "-" : " ", osc_offset / 10, (int)Mathabs(osc_offset % 10));			  
			}
			else {
				seq_printf(s, "%d error(s). %s\n", final_result,\
					final_result ? "test failed." : "All test passed.");			  
			}
		}
	}
	if (!ERR_ALLOC_MEM(ilitek_ini_file_data)) {
		vfree(ilitek_ini_file_data);
		ilitek_ini_file_data = NULL;
	}
	TPD_INFO("MP Test DONE\n");
	mutex_lock(&ipd->plat_mutex);
	ipd->esd_check_enabled = true;
	mutex_unlock(&ipd->plat_mutex);
	return final_result;
}
EXPORT_SYMBOL(ilitek_mp_test);


int ilitek_mp_black_screen_test(char *message)
{
	uint8_t test_cmd[4] = { 0 };
    int final_result = -1;
    mutex_unlock(&ipd->ts->mutex);
	mutex_lock(&ipd->plat_mutex);
	ipd->esd_check_enabled = false;
	mutex_unlock(&ipd->plat_mutex);
	ilitek_ini_file_data = (struct ini_file_data *)vmalloc(sizeof(struct ini_file_data) * PARSER_MAX_KEY_NUM);
	if (ERR_ALLOC_MEM(ilitek_ini_file_data)) {
		TPD_INFO("Failed to malloc ilitek_ini_file_data\n");
		goto out;
	}
	if (core_parser_path(ipd->test_limit_name) < 0) {
		TPD_INFO("Failed to parsing INI file\n");
		goto out;
	}

	/* Init MP structure */
	if(core_mp_init() < 0) {
		TPD_INFO("Failed to init mp\n");
		goto out;
	}
	mutex_lock(&ipd->plat_mutex);
	core_mp->run = true;
	mutex_unlock(&ipd->plat_mutex);
	core_gesture->entry = false;
	core_mp->oplus_lcm = true;
	/* Switch to Test mode */
	test_cmd[0] = protocol->test_mode;
	core_config_mode_control(test_cmd);
    core_fr->isEnableFR = false;
	ipd->esd_check_enabled = false;
	ipd->irq_timer = jiffies;    //reset esd check trigger base time
	//msleep(100);
	/* Do not chang the sequence of test */
	core_mp_run_test("Raw Data(No BK) (LCM OFF)", true);
	core_mp_run_test("Noise Peak to Peak(With Panel) (LCM OFF)", true);
	core_mp_run_test("Raw Data_TD (LCM OFF)", true);
	core_mp_run_test("Peak To Peak_TD (LCM OFF)", true);

	core_mp_show_result();
#if 0
	core_config_ice_mode_enable();
	core_get_ddi_register();
	core_config_ice_mode_disable();

	core_mp_ctrl_lcm_status(true);

	core_config_ice_mode_enable();
	core_get_ddi_register();
	core_config_ice_mode_disable();
#endif

#ifndef HOST_DOWNLOAD
	/* Code reset */
	core_config_ice_mode_enable();
	core_config_ic_reset();
#endif
	/* Switch to Demo mode */
	test_cmd[0] = protocol->demo_mode;
	core_config_mode_control(test_cmd);
#ifdef HOST_DOWNLOAD
	//ilitek_platform_tp_hw_reset(true);
	//ilitek_reset((void *)ipd);
#endif
	core_mp->run = false;
    core_fr->isEnableFR = true;
	core_mp->oplus_lcm = false;
    final_result = core_mp->fail_items;
	core_mp_test_free();
out:
	mutex_lock(&ipd->ts->mutex);
	if (core_config->core_type == CORE_TYPE_B) {
		sprintf(message, "%d errors. %s", final_result, final_result ? "panel black screen test failed. This panel not use in MP" : "All test passed.");
	} else {
		sprintf(message, "%d errors. %s", final_result, final_result ? "black screen test failed." : "All test passed.");
	}
	if (!ERR_ALLOC_MEM(ilitek_ini_file_data)) {
		vfree(ilitek_ini_file_data);
		ilitek_ini_file_data = NULL;
	}
	TPD_INFO("MP Test DONE\n");
	mutex_lock(&ipd->plat_mutex);
	ipd->esd_check_enabled = true;
	mutex_unlock(&ipd->plat_mutex);
	return final_result;
}
EXPORT_SYMBOL(ilitek_mp_black_screen_test);

void core_mp_test_free(void)
{
	int i = 0;

	TPD_INFO("Free all allocated mem\n");

	core_mp->final_result = MP_FAIL;

	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		tItems[i].run = false;
		tItems[i].max_res = MP_FAIL;
		tItems[i].min_res = MP_FAIL;
		tItems[i].item_result = MP_PASS;
		tItems[i].test_count = 0;
		sprintf(tItems[i].result, "%s", "FAIL");

		if (tItems[i].catalog == TX_RX_DELTA) {
			ipio_vfree((void **)&core_mp->rx_delta_buf);
			ipio_vfree((void **)&core_mp->tx_delta_buf);
			ipio_vfree((void **)&core_mp->tx_max_buf);
			ipio_vfree((void **)&core_mp->tx_min_buf);
			ipio_vfree((void **)&core_mp->rx_max_buf);
			ipio_vfree((void **)&core_mp->rx_min_buf);
		} else {
			if (tItems[i].spec_option == BENCHMARK) {
				ipio_vfree((void **)&tItems[i].bench_mark_max);
				ipio_vfree((void **)&tItems[i].bench_mark_min);
			}
			ipio_vfree((void **)&tItems[i].result_buf);
			ipio_vfree((void **)&tItems[i].buf);
			ipio_vfree((void **)&tItems[i].max_buf);
			ipio_vfree((void **)&tItems[i].min_buf);
		}
	}

	ipio_kfree((void **)&frame_buf);
	ipio_kfree((void **)&key_buf);
	ipio_kfree((void **)&core_mp);
}
EXPORT_SYMBOL(core_mp_test_free);

static void mp_test_init_item(void)
{
	int i = 0;

	core_mp->mp_items = ARRAY_SIZE(tItems);

	/* assign test functions run on MP flow according to their catalog */
	for (i = 0; i < ARRAY_SIZE(tItems); i++) {

		tItems[i].spec_option = 0;
		tItems[i].type_option = 0;
		tItems[i].run = false;
		tItems[i].max = 0;
		tItems[i].max_res = MP_FAIL;
		tItems[i].item_result = MP_PASS;
		tItems[i].min = 0;
		tItems[i].min_res = MP_FAIL;
		tItems[i].frame_count = 0;
		tItems[i].trimmed_mean = 0;
		tItems[i].lowest_percentage = 0;
		tItems[i].highest_percentage = 0;
		tItems[i].result_buf = NULL;
		tItems[i].buf = NULL;
		tItems[i].max_buf = NULL;
		tItems[i].min_buf = NULL;
		tItems[i].bench_mark_max = NULL;
		tItems[i].bench_mark_min = NULL;
		tItems[i].node_type = NULL;
		tItems[i].test_count = 0;

		if (tItems[i].catalog == MUTUAL_TEST) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == TX_RX_DELTA) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == UNTOUCH_P2P) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == PIXEL) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == OPEN_TEST){
			if (strcmp(tItems[i].name, "open_integration_sp") == 0)
				tItems[i].do_test = open_test_sp;
			else
				tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == KEY_TEST) {
			tItems[i].do_test = key_test;
		} else if (tItems[i].catalog == SELF_TEST) {
			tItems[i].do_test = self_test;
		} else if (tItems[i].catalog == ST_TEST) {
			tItems[i].do_test = st_test;
		} else if (tItems[i].catalog == PEAK_TO_PEAK_TEST) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == SHORT_TEST) {
			tItems[i].do_test = mutual_test;
		}

		tItems[i].result = kmalloc(16, GFP_KERNEL);
        sprintf(tItems[i].result, "%s", "FAIL");
	}

	/*
	 * assign protocol command written into firmware via I2C,
	 * which might be differnet if the version of protocol was changed.
	 */
	tItems[0].cmd = protocol->mutual_dac;
	tItems[1].cmd = protocol->mutual_bg;
	tItems[2].cmd = protocol->mutual_signal;
	tItems[3].cmd = protocol->mutual_no_bk;
	tItems[4].cmd = protocol->mutual_has_bk;
	tItems[5].cmd = protocol->mutual_bk_dac;
	tItems[6].cmd = protocol->self_dac;
	tItems[7].cmd = protocol->self_bg;
	tItems[8].cmd = protocol->self_signal;
	tItems[9].cmd = protocol->self_no_bk;
	tItems[10].cmd = protocol->self_has_bk;
	tItems[11].cmd = protocol->self_bk_dac;
	tItems[12].cmd = protocol->key_dac;
	tItems[13].cmd = protocol->key_bg;
	tItems[14].cmd = protocol->key_no_bk;
	tItems[15].cmd = protocol->key_has_bk;
	tItems[16].cmd = protocol->key_open;
	tItems[17].cmd = protocol->key_short;
	tItems[18].cmd = protocol->st_dac;
	tItems[19].cmd = protocol->st_bg;
	tItems[20].cmd = protocol->st_no_bk;
	tItems[21].cmd = protocol->st_has_bk;
	tItems[22].cmd = protocol->st_open;
	tItems[23].cmd = protocol->tx_short;
	tItems[24].cmd = protocol->rx_short;
	tItems[25].cmd = protocol->rx_open;
	tItems[26].cmd = protocol->cm_data;
	tItems[27].cmd = protocol->cs_data;
	tItems[28].cmd = protocol->tx_rx_delta;
	tItems[29].cmd = protocol->mutual_signal;
	tItems[30].cmd = protocol->mutual_no_bk;
	tItems[31].cmd = protocol->mutual_has_bk;
	tItems[32].cmd = protocol->rx_open;
	tItems[33].cmd = protocol->rx_open;
	tItems[34].cmd = protocol->peak_to_peak;
}

int core_mp_init(void)
{
	int res = 0;

	if (!ERR_ALLOC_MEM(core_config->tp_info)) {
		if (core_mp == NULL) {
			core_mp = kzalloc(sizeof(*core_mp), GFP_KERNEL);
			if (ERR_ALLOC_MEM(core_mp)) {
				TPD_INFO("Failed to init core_mp, %ld\n", PTR_ERR(core_mp));
				res = -ENOMEM;
				goto out;
			}

			core_mp->xch_len = core_config->tp_info->nXChannelNum;
			core_mp->ych_len = core_config->tp_info->nYChannelNum;

			core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
			core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

			core_mp->key_len = core_config->tp_info->nKeyCount;
			core_mp->st_len = core_config->tp_info->side_touch_type;

			core_mp->tdf = 240;
			core_mp->busy_cdc = INT_CHECK;

			core_mp->run = false;
			core_mp->retry = true;
			core_mp->oplus_run = true;
			core_mp->oplus_lcm = false;
			core_mp->final_result = MP_FAIL;
			core_mp->fail_items = 0;
			mp_test_init_item();
		}
	} else {
		TPD_INFO("Failed to allocate core_mp mem as did not find TP info\n");
		res = -ENOMEM;
	}

out:
	return res;
}
EXPORT_SYMBOL(core_mp_init);

static int isspace_t(int x)  
{  
    if(x==' '||x=='\t'||x=='\n'||x=='\f'||x=='\b'||x=='\r')  
        return 1;  
    else   
        return 0;  
}

static char *ini_str_trim_r(char *buf)
{
	int len = 0;
	int i = 0;
	char tmp[512] = { 0 };

	len = strlen(buf);

	for (i = 0; i < len; i++) {
		if (buf[i] != ' ')
			break;
	}

	if (i < len)
		strncpy(tmp, (buf + i), (len - i));

	strncpy(buf, tmp, len);
	return buf;
}

/* Count the number of each line and assign the content to tmp buffer */
static int get_ini_phy_line(char *data, char *buffer, int maxlen)
{
	int i = 0;
	int j = 0;
	int iRetNum = -1;
	char ch1 = '\0';

	for (i = 0, j = 0; i < maxlen; j++) {
		ch1 = data[j];
		iRetNum = j + 1;
		if (ch1 == '\n' || ch1 == '\r') {	/* line end */
			ch1 = data[j + 1];
			if (ch1 == '\n' || ch1 == '\r') {
				iRetNum++;
			}

			break;
		} else if (ch1 == 0x00) {
			//iRetNum = -1;
			break;	/* file end */
		}
		
		buffer[i++] = ch1;
	}

	buffer[i] = '\0';
	return iRetNum;
}

static int get_ini_phy_data(char *data, int fsize)
{
	int i = 0;
	int n = 0;
	int res = 0;
	int banchmark_flag = 0;
	int empty_section = 0;
	int nodetype_flag = 0;
	int offset = 0;
	int isEqualSign = 0;
	char *ini_buf = NULL;
	char *tmpSectionName = NULL;
	char *temp;
	char M_CFG_SSL = '[';
	char M_CFG_SSR = ']';
/* char M_CFG_NIS = ':'; */
	char M_CFG_NTS = '#';
	char M_CFG_EQS = '=';

	if (data == NULL) {
		TPD_INFO("INI data is NULL\n");
		res = -EINVAL;
		goto out;
	}

	ini_buf = kzalloc((PARSER_MAX_CFG_BUF + 1) * sizeof(char), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ini_buf)) {
		TPD_INFO("Failed to allocate ini_buf memory, %ld\n", PTR_ERR(ini_buf));
		res = -ENOMEM;
		goto out;
	}

	tmpSectionName = kzalloc((PARSER_MAX_CFG_BUF + 1) * sizeof(char), GFP_KERNEL);
	if (ERR_ALLOC_MEM(tmpSectionName)) {
		TPD_INFO("Failed to allocate tmpSectionName memory, %ld\n", PTR_ERR(tmpSectionName));
		res = -ENOMEM;
		goto out;
	}

	temp = strnstr(data, TYPE_MARK, fsize);
	if(temp != NULL) {
		TPD_DEBUG("Find Type mark, locat = %d",(int)(temp-data));
		if(core_config->core_type == CORE_TYPE_B)
			offset = temp-data;
		else
			fsize = temp-data;
	}
	TPD_INFO("fsize = %d\n", fsize);
	while (true) {
		banchmark_flag = 0;
		empty_section = 0;
		nodetype_flag = 0;
		if (g_ini_items >= PARSER_MAX_KEY_NUM) {
			TPD_INFO("MAX_KEY_NUM: Out of length\n");
			goto out;
		}

		if(offset >= fsize)
			goto out;/*over size*/
		//n = get_ini_phy_line(data + offset, ini_buf, PARSER_MAX_CFG_BUF);
		n = get_ini_phy_line(data + offset, ini_buf, ((offset + PARSER_MAX_CFG_BUF) >= fsize) ? (fsize - offset) : PARSER_MAX_CFG_BUF);
		if (n < 0) {
			TPD_INFO("End of Line\n");
			goto out;
		}

		offset += n;

		n = strlen(ini_str_trim_r(ini_buf));

		if (n == 0 || ini_buf[0] == M_CFG_NTS)
			continue;

		/* Get section names */
		if (n > 2 && ((ini_buf[0] == M_CFG_SSL && ini_buf[n - 1] != M_CFG_SSR))) {
			TPD_INFO("Bad Section: %s\n", ini_buf);
			res = -EINVAL;
			goto out;
		} else {
			if (ini_buf[0] == M_CFG_SSL) {
				ilitek_ini_file_data[g_ini_items].iSectionNameLen = n - 2;
				if (ilitek_ini_file_data[g_ini_items].iSectionNameLen > PARSER_MAX_KEY_NAME_LEN) {
					TPD_INFO("MAX_KEY_NAME_LEN: Out Of Length\n");
					res = INI_ERR_OUT_OF_LINE;
					goto out;
				}

				ini_buf[n - 1] = 0x00;
				strcpy((char *)tmpSectionName, ini_buf + 1);
				banchmark_flag = 0;
				nodetype_flag = 0;
				TPD_DEBUG("Section Name: %s, Len: %d, offset = %d\n", tmpSectionName, n - 2, offset);
				continue;
			}
		}

		/* copy section's name without square brackets to its real buffer */
		strcpy(ilitek_ini_file_data[g_ini_items].pSectionName, tmpSectionName);
		ilitek_ini_file_data[g_ini_items].iSectionNameLen = strlen(tmpSectionName);

		isEqualSign = 0;
		for (i = 0; i < n; i++) {
			if (ini_buf[i] == M_CFG_EQS) {
				isEqualSign = i;
				break;
			}
			if(ini_buf[i] == M_CFG_SSL || ini_buf[i] == M_CFG_SSR){
				empty_section = 1;
				break;
			}
		}

		if (isEqualSign == 0)
		{
			if(empty_section)
				continue;

			if (strstr(ilitek_ini_file_data[g_ini_items].pSectionName,"Benchmark_Data") > 0){
				banchmark_flag = 1;
				isEqualSign =-1;
			}
			else if (strstr(ilitek_ini_file_data[g_ini_items].pSectionName,"Node Type") > 0){
				nodetype_flag = 1;
				isEqualSign =-1;
			}
			else{
				continue;
			}
		}
		if(banchmark_flag){
		/* Get Key names */
			ilitek_ini_file_data[g_ini_items].iKeyNameLen = strlen(BENCHMARK_KEY_NAME);
			strcpy(ilitek_ini_file_data[g_ini_items].pKeyName, BENCHMARK_KEY_NAME);
			ilitek_ini_file_data[g_ini_items].iKeyValueLen = n;
		}
		else if(nodetype_flag){
		/* Get Key names */
			ilitek_ini_file_data[g_ini_items].iKeyNameLen = strlen(NODE_TYPE_KEY_NAME);
			strcpy(ilitek_ini_file_data[g_ini_items].pKeyName, NODE_TYPE_KEY_NAME);
			ilitek_ini_file_data[g_ini_items].iKeyValueLen = n;
		}
		else{
		/* Get Key names */
			ilitek_ini_file_data[g_ini_items].iKeyNameLen = isEqualSign;
			if (ilitek_ini_file_data[g_ini_items].iKeyNameLen > PARSER_MAX_KEY_NAME_LEN) {
				/* ret = CFG_ERR_OUT_OF_LEN; */
				TPD_INFO("MAX_KEY_NAME_LEN: Out Of Length\n");
				res = INI_ERR_OUT_OF_LINE;
				goto out;
			}

			memcpy(ilitek_ini_file_data[g_ini_items].pKeyName,
			ini_buf, ilitek_ini_file_data[g_ini_items].iKeyNameLen);
			ilitek_ini_file_data[g_ini_items].iKeyValueLen = n - isEqualSign - 1;
		}

		/* Get a value assigned to a key */

		if (ilitek_ini_file_data[g_ini_items].iKeyValueLen > PARSER_MAX_KEY_VALUE_LEN) {
			TPD_INFO("MAX_KEY_VALUE_LEN: Out Of Length\n");
			res = INI_ERR_OUT_OF_LINE;
			goto out;
		}

		memcpy(ilitek_ini_file_data[g_ini_items].pKeyValue,
		       ini_buf + isEqualSign + 1, ilitek_ini_file_data[g_ini_items].iKeyValueLen);

		TPD_DEBUG("%s = %s\n", ilitek_ini_file_data[g_ini_items].pKeyName,
		    ilitek_ini_file_data[g_ini_items].pKeyValue);

		g_ini_items++;
	}

out:
	ipio_kfree((void **)&ini_buf);
	ipio_kfree((void **)&tmpSectionName);
	return res;
}

static void init_ilitek_ini_data(void)
{
	int i = 0;

	g_ini_items = 0;

	/* Initialise ini strcture */
	for (i = 0; i < PARSER_MAX_KEY_NUM; i++) {
		memset(ilitek_ini_file_data[i].pSectionName, 0, PARSER_MAX_KEY_NAME_LEN);
		memset(ilitek_ini_file_data[i].pKeyName, 0, PARSER_MAX_KEY_NAME_LEN);
		memset(ilitek_ini_file_data[i].pKeyValue, 0, PARSER_MAX_KEY_VALUE_LEN);
		ilitek_ini_file_data[i].iSectionNameLen = 0;
		ilitek_ini_file_data[i].iKeyNameLen = 0;
		ilitek_ini_file_data[i].iKeyValueLen = 0;
	}
}

/* get_ini_key_value - get ini's key and value based on its section from its array
 *
 * A function is digging into the key and value by its section from the ini array.
 * The comparsion is not only a string's name, but its length.
 */
static int get_ini_key_value(char *section, char *key, char *value)
{
	int i = 0;
	int ret = -2;
	int len = 0;

	len = strlen(key);

	for (i = 0; i < g_ini_items; i++) {
		if (strcmp(section, ilitek_ini_file_data[i].pSectionName) != 0)
			continue;

		if (strcmp(key, ilitek_ini_file_data[i].pKeyName) == 0) {
			memcpy(value, ilitek_ini_file_data[i].pKeyValue, ilitek_ini_file_data[i].iKeyValueLen);
			TPD_DEBUG(" value:%s , pKeyValue: %s\n", value, ilitek_ini_file_data[i].pKeyValue);
			ret = 0;
			break;
		}
	}
	return ret;
}



void core_parser_nodetype(int32_t* type_ptr, char *desp)
{

	int i = 0;
	int j = 0;
	int index1 =0;
	int temp = 0;
	int count = 0;
	char str[512] = { 0 };
	char record = ',';	

	for (i = 0; i < g_ini_items; i++) {

		if ((strstr(ilitek_ini_file_data[i].pSectionName, desp) == NULL) ||
			strcmp(ilitek_ini_file_data[i].pKeyName, NODE_TYPE_KEY_NAME) != 0) {			
				continue;
			}
		
		record = ',';
		for(j=0, index1 = 0; j <= ilitek_ini_file_data[i].iKeyValueLen; j++){

			if(ilitek_ini_file_data[i].pKeyValue[j] == ';' || j == ilitek_ini_file_data[i].iKeyValueLen){

				if(record != '.')
				{
					memset(str,0 ,sizeof(str));
					memcpy(str,&ilitek_ini_file_data[i].pKeyValue[index1], (j -index1));
					temp=katoi(str);
					type_ptr[count] = temp;
					TPD_DEBUG("%04d,",temp);
					count++;	  
				}
				record = ilitek_ini_file_data[i].pKeyValue[j];                                       
				index1 = j+1;    
			}                                        
		}
		TPD_DEBUG("\n");
		
	}
}

void core_parser_benchmark(int32_t* max_ptr, int32_t* min_ptr, int8_t type, char *desp)
{
	int i = 0;
	int j = 0;
	int index1 =0;
	int temp;
	int count = 0;
	char str[512] = { 0 };
	char record = ',';
	int32_t data[4] = {0};
	char Benchmark_desp[512] = {0};
	sprintf(Benchmark_desp, "%s%s", desp, "_Benchmark_Data");
	for (i = 0; i < g_ini_items; i++) {
		if ((strcmp(ilitek_ini_file_data[i].pSectionName, Benchmark_desp) != 0) ||
			strcmp(ilitek_ini_file_data[i].pKeyName, BENCHMARK_KEY_NAME) != 0) {
				continue;
		}

		record = ',';
		for(j = 0, index1 = 0; j <= ilitek_ini_file_data[i].iKeyValueLen; j++) {
			if(ilitek_ini_file_data[i].pKeyValue[j] == ',' || ilitek_ini_file_data[i].pKeyValue[j] == ';' ||
				ilitek_ini_file_data[i].pKeyValue[j] == '.'|| j == ilitek_ini_file_data[i].iKeyValueLen) {

				if(record != '.') {
					memset(str, 0, sizeof(str));
					memcpy(str, &ilitek_ini_file_data[i].pKeyValue[index1], (j - index1));
					temp = katoi(str);
					data[(count % 4)] = temp;
					if ((count/4) >= (core_mp->frame_len)) {
						TPD_INFO("count/4 = %d\n", count/4);
						break;
					}
					if ((count % 4) == 3) {
						if (data[0] == 1) {
							if (type == VALUE) {
								max_ptr[count/4] = data[1] + data[2];
								min_ptr[count/4] = data[1] + data[3];
							} else {
								max_ptr[count/4] = data[1] + (data[1] * data[2]) / 100;
								min_ptr[count/4] = data[1] - (data[1] * data[3]) / 100;
							}
						} else {
							max_ptr[count/4] = INT_MAX;
							min_ptr[count/4] = INT_MIN;
						}
					}
					count++;
				}
				record = ilitek_ini_file_data[i].pKeyValue[j];
				index1 = j + 1;
			}
		}
	}
}
EXPORT_SYMBOL(core_parser_benchmark);

int core_parser_get_u8_array(char *key, uint8_t *buf)
{
	char *s = key;
	char *pToken;
	int res = 0;
	int conut = 0;
    long s_to_long = 0;

	TPD_DEBUG("key = %s\n",key);

	if(isspace_t((int)(unsigned char)*s) == 0)
	{
		while((pToken = strsep(&s, ",")) != NULL){
			res = kstrtol(pToken, 0, &s_to_long);
			if(res == 0)
				buf[conut] = s_to_long;
			else
				TPD_INFO("convert string too long, res = %d\n", res);
			conut++;
		}
	}

	return conut;
}
EXPORT_SYMBOL(core_parser_get_u8_array);

int core_parser_get_int_data(char *section, char *keyname, char *rv)
{
	int len = 0;
	char value[512] = { 0 };

	TPD_DEBUG("section = %s, keyname = %s\n",section,keyname);

	if (rv == NULL || section == NULL || keyname == NULL) {
		TPD_INFO("Parameters are invalid\n");
		return -EINVAL;
	}

	/* return a white-space string if get nothing */
	if (get_ini_key_value(section, keyname, value) < 0) {
		sprintf(rv, "%s", value);
		return 0;
	}

	len = sprintf(rv, "%s", value);
	return len;
}
EXPORT_SYMBOL(core_parser_get_int_data);

int core_parser_path(char *path)
{
	int res = 0;
    const struct firmware *fw = NULL;
	TPD_INFO("path = %s\n", path);
    
    res = request_firmware(&fw, path, &(ipd->spi->dev));
    if (res != 0) {
        TPD_INFO("%s : request mp file failed! ret = %d\n", __func__, res);
		goto out;
    }

	TPD_INFO("fw->size = %d\n", (int)fw->size);
	if (fw->size <= 0) {
		TPD_INFO("The size of file is invaild\n");
		res = -EINVAL;
		goto out;
	}


	init_ilitek_ini_data();

	res = get_ini_phy_data((uint8_t *)fw->data, (int)fw->size);
	if (res < 0) {
		TPD_INFO("Failed to get physical ini data, res = %d\n", res);
		goto out;
	}

	TPD_INFO("Parsing INI file doen\n");

out:
    if(fw != NULL) {
        release_firmware(fw);
    }
	return res;
}
EXPORT_SYMBOL(core_parser_path);
