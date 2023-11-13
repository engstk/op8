// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
#include "iris/dsi_iris5_api.h"
#include "iris/dsi_iris5_gpio.h"
#endif
#ifdef OPLUS_BUG_STABILITY
#include <soc/oplus/boot_mode.h>
#include "oplus_display_private_api.h"
#include "oplus_dc_diming.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_aod.h"
#include "oplus_bl.h"
#include "oplus_display_panel_common.h"
#include "oplus_display_panel_cabc.h"
#endif

#ifdef CONFIG_OPLUS_FEATURE_MISC
#include <soc/oplus/system/oplus_misc.h>
#endif

#ifdef OPLUS_FEATURE_ADFR
#include "oplus_adfr.h"
#include "sde_trace.h"
#endif /* OPLUS_FEATURE_ADFR */
#include "sde_dbg.h"

/**
 * topology is currently defined by a set of following 3 values:
 * 1. num of layer mixers
 * 2. num of compression encoders
 * 3. num of interfaces
 */
#define TOPOLOGY_SET_LEN 3
#define MAX_TOPOLOGY 5

#define DSI_PANEL_DEFAULT_LABEL  "Default dsi panel"

#define DEFAULT_PANEL_JITTER_NUMERATOR		2
#define DEFAULT_PANEL_JITTER_DENOMINATOR	1
#define DEFAULT_PANEL_JITTER_ARRAY_SIZE		2
#define MAX_PANEL_JITTER		10
#define DEFAULT_PANEL_PREFILL_LINES	25
#define MIN_PREFILL_LINES      35

#ifdef OPLUS_BUG_STABILITY
/* add for Olso bringup */
unsigned int dis_set_first_level = 0;
extern int lcd_set_bias(bool enable);
extern int lcd_bl_set_led_brightness(int value);
extern int turn_on_ktz8866_hw_en(bool on);
/* A tablet Pad, modify mipi */
bool mipi_c_phy_oslo_flag;

/* Add for solve sau issue*/
extern int lcd_closebl_flag;
/* Add for fingerprint silence*/
extern int lcd_closebl_flag_fp;
#ifdef CONFIG_REGULATOR_TPS65132
extern void TPS65132_pw_enable(int enable);
#endif /* CONFIG_REGULATOR_TPS65132 */

#ifdef OPLUS_BUG_STABILITY
extern int last_fps;
extern volatile bool panel_initialized_flag;
#endif /*OPLUS_BUG_STABILITY*/

__attribute__((weak)) void lcd_queue_load_tp_fw(void) {return;}
__attribute__((weak)) int tp_gesture_enable_flag(void) {return 0;}
__attribute__((weak)) int tp_control_cs_gpio(bool enable) {return 0;}
static int tp_black_power_on_ff_flag = 0;
static int tp_cs_flag = 0;

int backlight_max = 2047;
module_param(backlight_max, int, 0644);

static int mdss_tp_black_gesture_status(void){
	int ret = 0;
	/*default disable tp gesture*/
	//tp add the interface for check black status to ret
	ret = tp_gesture_enable_flag();
	pr_err("[TP]%s:ret = %d\n", __func__, ret);
	return ret;
}

static atomic_t esd_check_happened = ATOMIC_INIT(0);
int get_esd_check_happened(void)
{
	return atomic_read(&esd_check_happened);
}
void set_esd_check_happened(int val)
{
	atomic_set(&esd_check_happened, val);

	DSI_INFO("%s, esd_check_happened = %d\n", __func__, get_esd_check_happened());
}
EXPORT_SYMBOL_GPL(set_esd_check_happened);

static atomic_t bl_frame_done_cnt = ATOMIC_INIT(0);
void set_bl_frame_done_cnt(void)
{
	atomic_add_unless(&bl_frame_done_cnt, -1, 0);
}
EXPORT_SYMBOL_GPL(set_bl_frame_done_cnt);
#endif /* OPLUS_BUG_STABILITY */

enum dsi_dsc_ratio_type {
	DSC_8BPC_8BPP,
	DSC_10BPC_8BPP,
	DSC_12BPC_8BPP,
	DSC_10BPC_10BPP,
	DSC_RATIO_TYPE_MAX
};

static u32 dsi_dsc_rc_buf_thresh[] = {0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54,
		0x62, 0x69, 0x70, 0x77, 0x79, 0x7b, 0x7d, 0x7e};

/*
 * DSC 1.1
 * Rate control - Min QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_min_qp_1_1[][15] = {
	{0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 13},
	{0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 17},
	{0, 4, 9, 9, 11, 11, 11, 11, 11, 11, 13, 13, 13, 15, 21},
	{0, 4, 5, 6, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 15},
	};

/*
 * DSC 1.1 SCR
 * Rate control - Min QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_min_qp_1_1_scr1[][15] = {
	{0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12},
	{0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16},
	{0, 4, 9, 9, 11, 11, 11, 11, 11, 11, 13, 13, 13, 17, 20},
	{0, 4, 5, 6, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 15},
	};

/*
 * DSC 1.1
 * Rate control - Max QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_max_qp_1_1[][15] = {
	{4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 11, 12, 13, 13, 15},
	{4, 8, 9, 10, 11, 11, 11, 12, 13, 14, 15, 16, 17, 17, 19},
	{12, 12, 13, 14, 15, 15, 15, 16, 17, 18, 19, 20, 21, 21, 23},
	{7, 8, 9, 10, 11, 11, 11, 12, 13, 13, 14, 14, 15, 15, 16},
	};

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
static char dsi_dsc_rc_range_max_qp_1_1_pxlw[15] = {
	8, 8, 9, 10, 11, 11, 11, 12, 13, 13, 14, 14, 15, 15, 16};
#endif

/*
 * DSC 1.1 SCR
 * Rate control - Max QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_max_qp_1_1_scr1[][15] = {
	{4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13},
	{8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17},
	{12, 12, 13, 14, 15, 15, 15, 16, 17, 18, 18, 19, 19, 20, 23},
	{7, 8, 9, 10, 11, 11, 11, 12, 13, 13, 14, 14, 15, 15, 16},
	};

/*
 * DSC 1.1 and DSC 1.1 SCR
 * Rate control - bpg offset values
 */
static char dsi_dsc_rc_range_bpg_offset[] = {2, 0, 0, -2, -4, -6, -8, -8,
		-8, -10, -10, -12, -12, -12, -12};

int dsi_dsc_create_pps_buf_cmd(struct msm_display_dsc_info *dsc, char *buf,
				int pps_id)
{
	char *bp;
	char data;
	int i, bpp;
	char *dbgbp;

	dbgbp = buf;
	bp = buf;
	/* First 7 bytes are cmd header */
	*bp++ = 0x0A;
	*bp++ = 1;
	*bp++ = 0;
	*bp++ = 0;
	*bp++ = dsc->pps_delay_ms;
	*bp++ = 0;
	*bp++ = 128;

	*bp++ = (dsc->version & 0xff);		/* pps0 */
	*bp++ = (pps_id & 0xff);		/* pps1 */
	bp++;					/* pps2, reserved */

	data = dsc->line_buf_depth & 0x0f;
	data |= ((dsc->bpc & 0xf) << 4);
	*bp++ = data;				/* pps3 */

	bpp = dsc->bpp;
	bpp <<= 4;				/* 4 fraction bits */
	data = (bpp >> 8);
	data &= 0x03;				/* upper two bits */
	data |= ((dsc->block_pred_enable & 0x1) << 5);
	data |= ((dsc->convert_rgb & 0x1) << 4);
	data |= ((dsc->enable_422 & 0x1) << 3);
	data |= ((dsc->vbr_enable & 0x1) << 2);
	*bp++ = data;				/* pps4 */
	*bp++ = (bpp & 0xff);			/* pps5 */

	*bp++ = ((dsc->pic_height >> 8) & 0xff); /* pps6 */
	*bp++ = (dsc->pic_height & 0x0ff);	/* pps7 */
	*bp++ = ((dsc->pic_width >> 8) & 0xff);	/* pps8 */
	*bp++ = (dsc->pic_width & 0x0ff);	/* pps9 */

	*bp++ = ((dsc->slice_height >> 8) & 0xff);/* pps10 */
	*bp++ = (dsc->slice_height & 0x0ff);	/* pps11 */
	*bp++ = ((dsc->slice_width >> 8) & 0xff); /* pps12 */
	*bp++ = (dsc->slice_width & 0x0ff);	/* pps13 */

	*bp++ = ((dsc->chunk_size >> 8) & 0xff);/* pps14 */
	*bp++ = (dsc->chunk_size & 0x0ff);	/* pps15 */

	*bp++ = (dsc->initial_xmit_delay >> 8) & 0x3; /* pps16, bit 0, 1 */
	*bp++ = (dsc->initial_xmit_delay & 0xff);/* pps17 */

	*bp++ = ((dsc->initial_dec_delay >> 8) & 0xff); /* pps18 */
	*bp++ = (dsc->initial_dec_delay & 0xff);/* pps19 */

	bp++;					/* pps20, reserved */

	*bp++ = (dsc->initial_scale_value & 0x3f); /* pps21 */

	*bp++ = ((dsc->scale_increment_interval >> 8) & 0xff); /* pps22 */
	*bp++ = (dsc->scale_increment_interval & 0xff); /* pps23 */

	*bp++ = ((dsc->scale_decrement_interval >> 8) & 0xf); /* pps24 */
	*bp++ = (dsc->scale_decrement_interval & 0x0ff);/* pps25 */

	bp++;					/* pps26, reserved */

	*bp++ = (dsc->first_line_bpg_offset & 0x1f);/* pps27 */

	*bp++ = ((dsc->nfl_bpg_offset >> 8) & 0xff);/* pps28 */
	*bp++ = (dsc->nfl_bpg_offset & 0x0ff);	/* pps29 */
	*bp++ = ((dsc->slice_bpg_offset >> 8) & 0xff);/* pps30 */
	*bp++ = (dsc->slice_bpg_offset & 0x0ff);/* pps31 */

	*bp++ = ((dsc->initial_offset >> 8) & 0xff);/* pps32 */
	*bp++ = (dsc->initial_offset & 0x0ff);	/* pps33 */

	*bp++ = ((dsc->final_offset >> 8) & 0xff);/* pps34 */
	*bp++ = (dsc->final_offset & 0x0ff);	/* pps35 */

	*bp++ = (dsc->min_qp_flatness & 0x1f);	/* pps36 */
	*bp++ = (dsc->max_qp_flatness & 0x1f);	/* pps37 */

	*bp++ = ((dsc->rc_model_size >> 8) & 0xff);/* pps38 */
	*bp++ = (dsc->rc_model_size & 0x0ff);	/* pps39 */

	*bp++ = (dsc->edge_factor & 0x0f);	/* pps40 */

	*bp++ = (dsc->quant_incr_limit0 & 0x1f);	/* pps41 */
	*bp++ = (dsc->quant_incr_limit1 & 0x1f);	/* pps42 */

	data = ((dsc->tgt_offset_hi & 0xf) << 4);
	data |= (dsc->tgt_offset_lo & 0x0f);
	*bp++ = data;				/* pps43 */

	for (i = 0; i < 14; i++)
		*bp++ = (dsc->buf_thresh[i] & 0xff); /* pps44 - pps57 */

	for (i = 0; i < 15; i++) {		/* pps58 - pps87 */
		data = (dsc->range_min_qp[i] & 0x1f);
		data <<= 3;
		data |= ((dsc->range_max_qp[i] >> 2) & 0x07);
		*bp++ = data;
		data = (dsc->range_max_qp[i] & 0x03);
		data <<= 6;
		data |= (dsc->range_bpg_offset[i] & 0x3f);
		*bp++ = data;
	}

	return 128;
}

static int dsi_panel_vreg_get(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	struct regulator *vreg = NULL;

	for (i = 0; i < panel->power_info.count; i++) {
		vreg = devm_regulator_get(panel->parent,
					  panel->power_info.vregs[i].vreg_name);
		rc = PTR_RET(vreg);
		if (rc) {
			DSI_ERR("failed to get %s regulator\n",
			       panel->power_info.vregs[i].vreg_name);
			goto error_put;
		}
		panel->power_info.vregs[i].vreg = vreg;
	}

	return rc;
error_put:
	for (i = i - 1; i >= 0; i--) {
		devm_regulator_put(panel->power_info.vregs[i].vreg);
		panel->power_info.vregs[i].vreg = NULL;
	}
	return rc;
}

static int dsi_panel_vreg_put(struct dsi_panel *panel)
{
	int rc = 0;
	int i;

	for (i = panel->power_info.count - 1; i >= 0; i--)
		devm_regulator_put(panel->power_info.vregs[i].vreg);

	return rc;
}

static int dsi_panel_gpio_request(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;

	if (gpio_is_valid(r_config->reset_gpio)) {
		rc = gpio_request(r_config->reset_gpio, "reset_gpio");
		if (rc) {
			DSI_ERR("request for reset_gpio failed, rc=%d\n", rc);
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
            if (iris_is_chip_supported()) {
				if (!strcmp(panel->type, "primary"))
					goto error;
				rc = 0;
			} else
#endif
			goto error;
		}
	}

	if (gpio_is_valid(r_config->disp_en_gpio)) {
		rc = gpio_request(r_config->disp_en_gpio, "disp_en_gpio");
		if (rc) {
			DSI_ERR("request for disp_en_gpio failed, rc=%d\n", rc);
			goto error_release_reset;
		}
	}

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpio_request(panel->bl_config.en_gpio, "bklt_en_gpio");
		if (rc) {
			DSI_ERR("request for bklt_en_gpio failed, rc=%d\n", rc);
			goto error_release_disp_en;
		}
	}

	if (gpio_is_valid(r_config->lcd_mode_sel_gpio)) {
		rc = gpio_request(r_config->lcd_mode_sel_gpio, "mode_gpio");
		if (rc) {
			DSI_ERR("request for mode_gpio failed, rc=%d\n", rc);
			goto error_release_mode_sel;
		}
	}
	if (gpio_is_valid(panel->vddr_gpio)) {
		rc = gpio_request(panel->vddr_gpio, "vddr_gpio");
		if (rc) {
			DSI_ERR("request for vddr_gpio failed, rc=%d\n", rc);
			if (gpio_is_valid(panel->vddr_gpio))
				gpio_free(panel->vddr_gpio);
		}
	}
	if (gpio_is_valid(panel->panel_test_gpio)) {
		rc = gpio_request(panel->panel_test_gpio, "panel_test_gpio");
		if (rc) {
			DSI_WARN("request for panel_test_gpio failed, rc=%d\n",
				 rc);
			panel->panel_test_gpio = -1;
			rc = 0;
		}
	}

#ifdef OPLUS_BUG_STABILITY
	if (gpio_is_valid(r_config->panel_vout_gpio)) {
		rc = gpio_request(r_config->panel_vout_gpio, "panel_vout_gpio");
		if (rc) {
			DSI_ERR("request for panel_vout_gpio failed, rc=%d\n", rc);
			if (gpio_is_valid(r_config->panel_vout_gpio))
				gpio_free(r_config->panel_vout_gpio);
		}
	}

	if (gpio_is_valid(r_config->panel_vddr_aod_en_gpio)) {
		rc = gpio_request(r_config->panel_vddr_aod_en_gpio, "panel_vddr_aod_en_gpio");
		if (rc) {
			DSI_ERR("request for panel_vddr_aod_en_gpio failed, rc=%d\n", rc);
			if (gpio_is_valid(r_config->panel_vddr_aod_en_gpio))
				gpio_free(r_config->panel_vddr_aod_en_gpio);
		}
	}

	if (gpio_is_valid(r_config->tp_cs_gpio)) {
		rc = gpio_request(r_config->tp_cs_gpio, "panel_tp_cs_gpio");
		if (rc) {
			DSI_ERR("request for panel_tp_cs_gpio failed, rc=%d\n", rc);
			if (gpio_is_valid(r_config->tp_cs_gpio))
				gpio_free(r_config->tp_cs_gpio);
		}
	}

	if (gpio_is_valid(r_config->panel_te_esd_gpio)) {
		rc = gpio_request(r_config->panel_te_esd_gpio, "panel_te_esd_gpio");
		if (rc)
			DSI_ERR("request for  panel_te_esd_gpio failed, rc=%d", rc);
	}
	gpio_direction_input(r_config->panel_te_esd_gpio);
#endif

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		if (gpio_is_valid(panel->vsync_switch_gpio)) {
			rc = gpio_request(panel->vsync_switch_gpio, "vsync_switch_gpio");
			if (rc) {
				DSI_ERR("adfr request for vsync_switch_gpio failed, rc=%d\n", rc);
			}
		}
	}
#endif /*OPLUS_FEATURE_ADFR*/

	goto error;
error_release_mode_sel:
	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_free(panel->bl_config.en_gpio);
error_release_disp_en:
	if (gpio_is_valid(r_config->disp_en_gpio))
		gpio_free(r_config->disp_en_gpio);
error_release_reset:
	if (gpio_is_valid(r_config->reset_gpio))
		gpio_free(r_config->reset_gpio);
error:
	return rc;
}

static int dsi_panel_gpio_release(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;

	if (gpio_is_valid(r_config->reset_gpio))
		gpio_free(r_config->reset_gpio);

	if (gpio_is_valid(r_config->disp_en_gpio))
		gpio_free(r_config->disp_en_gpio);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_free(panel->bl_config.en_gpio);

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		gpio_free(panel->reset_config.lcd_mode_sel_gpio);

	if (gpio_is_valid(panel->vddr_gpio))
		gpio_free(panel->vddr_gpio);

	if (gpio_is_valid(panel->panel_test_gpio))
		gpio_free(panel->panel_test_gpio);

#ifdef OPLUS_BUG_STABILITY
	if (gpio_is_valid(r_config->panel_vout_gpio))
		gpio_free(r_config->panel_vout_gpio);
#endif

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		if (gpio_is_valid(panel->vsync_switch_gpio))
			gpio_free(panel->vsync_switch_gpio);
	}
#endif /*OPLUS_FEATURE_ADFR*/

	return rc;
}

int dsi_panel_trigger_esd_attack(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config;

	if (!panel) {
		DSI_ERR("Invalid panel param\n");
		return -EINVAL;
	}

	r_config = &panel->reset_config;
	if (!r_config) {
		DSI_ERR("Invalid panel reset configuration\n");
		return -EINVAL;
	}

	if (gpio_is_valid(r_config->reset_gpio)) {
		gpio_set_value(r_config->reset_gpio, 0);
		SDE_EVT32(SDE_EVTLOG_FUNC_CASE1);
		DSI_INFO("GPIO pulled low to simulate ESD\n");
		return 0;
	}
	DSI_ERR("failed to pull down gpio\n");
	return -EINVAL;
}

static int dsi_panel_reset(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int i;

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for dsi_panel_reset\n");
#endif
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_dual_supported() && panel->is_secondary)
		return rc;
	iris_reset();
#endif

	if (gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		rc = gpio_direction_output(panel->reset_config.disp_en_gpio, 1);
		if (rc) {
			DSI_ERR("unable to set dir for disp gpio rc=%d\n", rc);
			goto exit;
		}
	}

	if (r_config->count) {
		rc = gpio_direction_output(r_config->reset_gpio,
			r_config->sequence[0].level);
		if (rc) {
			DSI_ERR("unable to set dir for rst gpio rc=%d\n", rc);
			goto exit;
		}
	}

	for (i = 0; i < r_config->count; i++) {
		gpio_set_value(r_config->reset_gpio,
			       r_config->sequence[i].level);


		if (r_config->sequence[i].sleep_ms)
			usleep_range(r_config->sequence[i].sleep_ms * 1000,
				(r_config->sequence[i].sleep_ms * 1000) + 100);
	}

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpio_direction_output(panel->bl_config.en_gpio, 1);
		if (rc)
			DSI_ERR("unable to set dir for bklt gpio rc=%d\n", rc);
	}

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio)) {
		bool out = true;

		if ((panel->reset_config.mode_sel_state == MODE_SEL_DUAL_PORT)
				|| (panel->reset_config.mode_sel_state
					== MODE_GPIO_LOW))
			out = false;
		else if ((panel->reset_config.mode_sel_state
				== MODE_SEL_SINGLE_PORT) ||
				(panel->reset_config.mode_sel_state
				 == MODE_GPIO_HIGH))
			out = true;

		rc = gpio_direction_output(
			panel->reset_config.lcd_mode_sel_gpio, out);
		if (rc)
			DSI_ERR("unable to set dir for mode gpio rc=%d\n", rc);
	}

	if (gpio_is_valid(panel->panel_test_gpio)) {
		rc = gpio_direction_input(panel->panel_test_gpio);
		if (rc)
			DSI_WARN("unable to set dir for panel test gpio rc=%d\n",
					rc);
	}

exit:
	return rc;
}

static int dsi_panel_set_pinctrl_state(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct pinctrl_state *state;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	if (enable)
		state = panel->pinctrl.active;
	else
		state = panel->pinctrl.suspend;

	rc = pinctrl_select_state(panel->pinctrl.pinctrl, state);
	if (rc)
		DSI_ERR("[%s] failed to set pin state, rc=%d\n",
				panel->name, rc);

	return rc;
}

#ifdef OPLUS_BUG_STABILITY
static int dsi_panel_1p8_on_off(struct dsi_panel *panel , int value)
{
	int rc = 0;

	if (gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		rc = gpio_direction_output(panel->reset_config.disp_en_gpio, value);
		if (rc) {
			DSI_ERR("unable to set dir for disp gpio rc=%d\n", rc);
		}
	}
	return rc;
}
#endif

static int dsi_panel_power_on(struct dsi_panel *panel)
{
	int rc = 0;

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for dsi_panel_power_on\n");
#endif
#ifdef OPLUS_BUG_STABILITY
	if (!strstr(panel->oplus_priv.vendor_name,"NT36672C")) {
		rc = dsi_pwr_enable_regulator(&panel->power_info, true);
		if (rc) {
			DSI_ERR("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
			goto exit;
		}
	}
#endif /*OPLUS_BUG_STABILITY*/
	if (gpio_is_valid(panel->reset_config.tp_cs_gpio)) {
		rc = gpio_direction_output(panel->reset_config.tp_cs_gpio, 1);
		if (rc)
			DSI_ERR("unable to set dir for tp_cs_gpio rc=%d", rc);
		gpio_set_value(panel->reset_config.tp_cs_gpio, 1);
	}
#ifdef OPLUS_BUG_STABILITY
	dis_set_first_level = 1;
	if(panel->nt36523w_ktz8866) {
		/* add for oslo bringup */
		/*ktz8866 power*/
		pr_info("%d %s dis_set_first_level=%d\n", __LINE__, __func__, dis_set_first_level);
		rc = turn_on_ktz8866_hw_en(true);
		if (rc) {
			DSI_ERR("[%s] failed to turn_on_ktz8866_hw_en, rc=%d\n",
				panel->name, rc);
			goto error_disable_vregs;
		}
		rc = lcd_set_bias(true);
		pr_info("lcd_set_bias(true)\n");
		if (rc) {
			DSI_ERR("[%s] failed to lcd_set_bias, rc=%d\n",
				panel->name, rc);
			goto error_disable_vregs;
		}
	}
#endif /*OPLUS_BUG_STABILITY*/

	if (gpio_is_valid(panel->vddr_gpio)) {
		rc = gpio_direction_output(panel->vddr_gpio, 1);
		DSI_ERR("enable vddr gpio\n");
		if (rc) {
			DSI_ERR("unable to set dir for vddr gpio rc=%d\n", rc);
			goto error_disable_vddr;
		}
	}
	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		DSI_ERR("[%s] failed to set pinctrl, rc=%d\n", panel->name, rc);
		goto error_disable_vregs;
	}

#ifdef OPLUS_BUG_STABILITY
	if (gpio_is_valid(panel->reset_config.panel_vout_gpio)) {
		rc = gpio_direction_output(panel->reset_config.panel_vout_gpio, 1);
		if (rc)
			DSI_ERR("unable to set dir for panel_vout_gpio rc=%d", rc);
		gpio_set_value(panel->reset_config.panel_vout_gpio, 1);
	}
	if (gpio_is_valid(panel->reset_config.panel_vddr_aod_en_gpio)) {
		rc = gpio_direction_output(panel->reset_config.panel_vddr_aod_en_gpio, 1);
		if (rc)
			DSI_ERR("unable to set dir for panel_vddr_aod_en_gpio rc=%d", rc);
		gpio_set_value(panel->reset_config.panel_vddr_aod_en_gpio, 1);
	}

	if (strstr(panel->oplus_priv.vendor_name,"NT36672C")) {
		if(get_esd_check_happened())
			set_esd_check_happened(0);

		if (gpio_is_valid(panel->reset_config.reset_gpio)) {
			gpio_set_value(panel->reset_config.reset_gpio, 0);
			usleep_range(1000, 1000);
		}

		if(tp_cs_flag == 1)
		{
			tp_cs_flag = 0;
			tp_control_cs_gpio(true);
		}
	}
#endif

#ifdef OPLUS_BUG_STABILITY
	if (!strcmp(panel->name,"samsung amb655uv01 amoled fhd+ panel with DSC") ||
		!strcmp(panel->name,"boe nt37800 amoled fhd+ panel with DSC") ||
		!strcmp(panel->name,"samsung fhd amoled") ||
		!strcmp(panel->name, "nt36523 lcd vid mode dsi panel")) {
		usleep_range(10, 11);
	} else if (!strcmp(panel->name,"samsung ams643xf01 amoled fhd+ panel") ||
			!strcmp(panel->name,"samsung SOFE03F dsc cmd mode panel")) {
		usleep_range(10000, 10100);
		rc = dsi_panel_reset(panel);
	} else if (!strcmp(panel->name,"samsung S6E3HC3 dsc cmd mode panel")) {
		usleep_range(2000, 2100);
		rc = dsi_panel_reset(panel);
	} else {
		rc = dsi_panel_reset(panel);
	}
#endif
	if (rc) {
		DSI_ERR("[%s] failed to reset panel, rc=%d\n", panel->name, rc);
		goto error_disable_gpio;
	}

	goto exit;

error_disable_gpio:
	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_set_value(panel->bl_config.en_gpio, 0);

	(void)dsi_panel_set_pinctrl_state(panel, false);

error_disable_vddr:
		if (gpio_is_valid(panel->vddr_gpio))
			gpio_set_value(panel->vddr_gpio, 0);

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);

#ifdef CONFIG_REGULATOR_TPS65132
	if (panel->oplus_priv.is_tps65132_support) {
		TPS65132_pw_enable(0);
	}
#endif /*CONFIG_REGULATOR_TPS65132*/

exit:
	return rc;
}

static int dsi_panel_power_off(struct dsi_panel *panel)
{
	int rc = 0;

#ifdef OPLUS_BUG_STABILITY
	int esd_check = get_esd_check_happened();
	pr_err("debug for dsi_panel_power_off\n");
#endif

#ifdef OPLUS_BUG_STABILITY
	if(!esd_check && mdss_tp_black_gesture_status()) {
		tp_black_power_on_ff_flag = 0;
		pr_err("%s : [TP] tp gesture is enable, not to dsi_panel_power_off, tp_black_power_on_ff_flag = %d\n",
			__func__, tp_black_power_on_ff_flag);
		return rc;
	}

	tp_black_power_on_ff_flag = 1;
	pr_err("%s:[TP]tp_black_power_on_ff_flag = %d\n",__func__,tp_black_power_on_ff_flag);
#endif /*OPLUS_BUG_STABILITY*/

	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (!strcmp(panel->name,"samsung SOFE03F dsc cmd mode panel")) {
		usleep_range(10000, 10100);
	}

	if (gpio_is_valid(panel->reset_config.reset_gpio) &&
					!panel->reset_gpio_always_on)
		gpio_set_value(panel->reset_config.reset_gpio, 0);

#ifdef OPLUS_BUG_STABILITY
	if (strstr(panel->oplus_priv.vendor_name,"NT36672C")) {
		tp_control_cs_gpio(false);
		tp_cs_flag = 1;
		msleep(100);
	}
#endif /*OPLUS_BUG_STABILITY*/

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	iris_power_off(panel);
#endif

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		gpio_set_value(panel->reset_config.lcd_mode_sel_gpio, 0);

#ifdef CONFIG_REGULATOR_TPS65132
	if (panel->oplus_priv.is_tps65132_support) {
		TPS65132_pw_enable(0);
		usleep_range(100000, 100100);
	}
#endif

#ifdef OPLUS_BUG_STABILITY
	if (!strcmp(panel->name,"samsung SOFE03F dsc cmd mode panel")) {
		usleep_range(10000, 11000);
	}
	if (gpio_is_valid(panel->reset_config.panel_vout_gpio))
		gpio_set_value(panel->reset_config.panel_vout_gpio, 0);
#endif

	if (gpio_is_valid(panel->vddr_gpio)) {
		gpio_set_value(panel->vddr_gpio, 0);
		DSI_ERR("disable vddr gpio\n");
		msleep(1);
	}

	if (gpio_is_valid(panel->panel_test_gpio)) {
		rc = gpio_direction_input(panel->panel_test_gpio);
		if (rc)
			DSI_WARN("set dir for panel test gpio failed rc=%d\n",
				 rc);
	}

	rc = dsi_panel_set_pinctrl_state(panel, false);
	if (rc) {
		DSI_ERR("[%s] failed set pinctrl state, rc=%d\n", panel->name,
		       rc);
	}

#ifdef OPLUS_BUG_STABILITY
	if(panel->nt36523w_ktz8866) {
	/* add for oslo bringup */
	/*ktz8866 power off*/
		pr_info("lcd_set_bias false\n");
		rc = lcd_set_bias(false);
		if (rc) {
			DSI_ERR("[%s] failed set lcd_set_bias false, rc=%d\n", panel->name, rc);
		}

		rc = turn_on_ktz8866_hw_en(false);
		if (rc) {
			DSI_ERR("[%s] failed set turn_on_ktz8866_hw_en false, rc=%d\n", panel->name, rc);
		}

		if(gpio_is_valid(panel->reset_config.tp_cs_gpio)) {
			rc = gpio_direction_output(panel->reset_config.tp_cs_gpio, 0);
			if (rc)
				DSI_ERR("unable to set dir for tp_cs_gpio rc=%d", rc);
			gpio_set_value(panel->reset_config.tp_cs_gpio, 0);
		}
	}
#endif

	rc = dsi_pwr_enable_regulator(&panel->power_info, false);
	if (rc)
		DSI_ERR("[%s] failed to enable vregs, rc=%d\n",
				panel->name, rc);
#ifdef OPLUS_BUG_STABILITY
	/* Add for ensure complete power down done of hardware */
	usleep_range(70 * 1000, (70 * 1000) + 100);
#endif

	return rc;
}

#ifdef OPLUS_BUG_STABILITY
extern int oplus_seed_backlight;
extern u32 oplus_backlight_delta;
extern ktime_t oplus_backlight_time;
extern int oplus_dimlayer_bl_enabled;
extern int oplus_dimlayer_bl_enable_real;
extern int oplus_dimlayer_bl_alpha;
extern int oplus_dimlayer_bl_alpha_v2;
#endif

#ifndef OPLUS_BUG_STABILITY
static int dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum dsi_cmd_set_type type)
#else  /*OPLUS_BUG_STABILITY*/
const char *cmd_set_prop_map[];
int dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum dsi_cmd_set_type type)
#endif /*OPLUS_BUG_STABILITY*/
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;
#if defined(OPLUS_BUG_STABILITY)
	struct dsi_panel_cmd_set *oplus_cmd_set = NULL;
#endif
	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mode = panel->cur_mode;

	cmds = mode->priv_info->cmd_sets[type].cmds;
	count = mode->priv_info->cmd_sets[type].count;
	state = mode->priv_info->cmd_sets[type].state;
	SDE_EVT32(type, state, count);

#ifdef OPLUS_BUG_STABILITY
	rc = dsi_panel_tx_cmd_hbm_pre_check(panel, type, cmd_set_prop_map);
	if (rc == 1) {
		return 0;
	}

	if (type != DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ON
		&& type != DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_OFF) {
		#ifdef OPLUS_FEATURE_ADFR
			if (type != DSI_CMD_FAKEFRAME) {
			    pr_err("dsi_cmd %s\n", cmd_set_prop_map[type]);
			}
		#else
		        pr_err("dsi_cmd %s\n", cmd_set_prop_map[type]);
		#endif
	}

	if (oplus_seed_backlight) {
		oplus_cmd_set = oplus_dsi_update_seed_backlight(panel, oplus_seed_backlight, type);
		if (!IS_ERR_OR_NULL(oplus_cmd_set)) {
			cmds = oplus_cmd_set->cmds;
			count = oplus_cmd_set->count;
			state = oplus_cmd_set->state;
		}
	}
#endif /*OPLUS_BUG_STABILITY*/

	if (count == 0) {
		DSI_DEBUG("[%s] No commands to be sent for state(%d)\n",
			 panel->name, type);
		goto error;
	}

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    if (iris_is_chip_supported() && iris_is_pt_mode(panel)) {
		rc = iris_pt_send_panel_cmd(panel, &(mode->priv_info->cmd_sets[type]));
		if (rc)
			DSI_ERR("iris_pt_send_panel_cmd failed\n");
		return rc;
    }
#endif

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		#ifdef OPLUS_BUG_STABILITY
		if (panel->oplus_priv.skip_mipi_last_cmd)
			cmds->msg.flags &= ~MIPI_DSI_MSG_LASTCOMMAND;
		#endif /* OPLUS_BUG_STABILITY */

		if (type == DSI_CMD_SET_VID_TO_CMD_SWITCH)
			cmds->msg.flags |= MIPI_DSI_MSG_ASYNC_OVERRIDE;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			DSI_ERR("failed to set cmds(%d), rc=%d\n", type, rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms*1000,
					((cmds->post_wait_ms*1000)+10));
		cmds++;
	}

#ifdef OPLUS_BUG_STABILITY
	dsi_panel_tx_cmd_hbm_post_check(panel, type);
#endif /*OPLUS_BUG_STABILITY*/

error:
	return rc;
}

static int dsi_panel_pinctrl_deinit(struct dsi_panel *panel)
{
	int rc = 0;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	devm_pinctrl_put(panel->pinctrl.pinctrl);

	return rc;
}

static int dsi_panel_pinctrl_init(struct dsi_panel *panel)
{
	int rc = 0;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	/* TODO:  pinctrl is defined in dsi dt node */
	panel->pinctrl.pinctrl = devm_pinctrl_get(panel->parent);
	if (IS_ERR_OR_NULL(panel->pinctrl.pinctrl)) {
		rc = PTR_ERR(panel->pinctrl.pinctrl);
		DSI_ERR("failed to get pinctrl, rc=%d\n", rc);
		goto error;
	}

	panel->pinctrl.active = pinctrl_lookup_state(panel->pinctrl.pinctrl,
						       "panel_active");
	if (IS_ERR_OR_NULL(panel->pinctrl.active)) {
		rc = PTR_ERR(panel->pinctrl.active);
		DSI_ERR("failed to get pinctrl active state, rc=%d\n", rc);
		goto error;
	}

	panel->pinctrl.suspend =
		pinctrl_lookup_state(panel->pinctrl.pinctrl, "panel_suspend");

	if (IS_ERR_OR_NULL(panel->pinctrl.suspend)) {
		rc = PTR_ERR(panel->pinctrl.suspend);
		DSI_ERR("failed to get pinctrl suspend state, rc=%d\n", rc);
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_wled_register(struct dsi_panel *panel,
		struct dsi_backlight_config *bl)
{
	struct backlight_device *bd;

	bd = backlight_device_get_by_type(BACKLIGHT_RAW);
	if (!bd) {
		DSI_ERR("[%s] fail raw backlight register\n", panel->name);
		return -EPROBE_DEFER;
	}

	bl->raw_bd = bd;
	return 0;
}

#ifdef CONFIG_OPLUS_FEATURE_MISC
static int saved_backlight = -1;

int dsi_panel_backlight_get(void)
{
	return saved_backlight;
}
#endif

#ifdef OPLUS_BUG_STABILITY
int enable_global_hbm_flags = 0;
#endif
static int dsi_panel_dcs_set_display_brightness_c2(struct mipi_dsi_device *dsi,
			u32 bl_lvl)
{
	u16 brightness = (u16)bl_lvl;
	u8 first_byte = brightness & 0xff;
	u8 second_byte = brightness >> 8;
	u8 payload[8] = {second_byte, first_byte,
		second_byte, first_byte,
		second_byte, first_byte,
		second_byte, first_byte};

	return mipi_dsi_dcs_write(dsi, 0xC2, payload, sizeof(payload));
}

static int dsi_panel_update_backlight(struct dsi_panel *panel,
	u32 bl_lvl)
{
	int rc = 0;
	struct mipi_dsi_device *dsi;
	struct dsi_backlight_config *bl;

	if (!panel || (bl_lvl > 0xffff)) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	dsi = &panel->mipi_device;
#ifdef CONFIG_OPLUS_FEATURE_MISC
	saved_backlight = bl_lvl;
#endif
	bl = &panel->bl_config;

	if (panel->bl_config.bl_inverted_dbv)
		bl_lvl = (((bl_lvl & 0xff) << 8) | (bl_lvl >> 8));

#ifdef OPLUS_BUG_STABILITY
	if (get_oplus_display_scene() == OPLUS_DISPLAY_AOD_SCENE) {
		/* Don't set backlight; just update AoD mode */
		oplus_update_aod_light_mode_unlock(panel);
		return 0;
	}

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (panel->oplus_priv.is_aod_ramless && !oplus_display_mode) {
		if (bl_lvl == 0)
			oplus_panel_process_dimming_v3(panel, bl_lvl);
		return 0;
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	if (panel->is_hbm_enabled) {
		if ((bl_lvl != 0)) {
			pr_err("backlight smooth check racing issue is_hbm_enabled\n");
			return 0;
		} else {
			if (!strcmp(panel->name,"boe nt37800 amoled fhd+ panel with DSC")) {
				oplus_panel_process_dimming_v3(panel, bl_lvl);
				return 0;
			}
		}
	}

	if((bl_lvl == 0) && oplus_display_get_hbm_mode()) {
		pr_err("set backlight 0 and recovery hbm to 0\n");
		__oplus_display_set_hbm(0);
	}

	if (oplus_display_get_hbm_mode()) {
		pr_err("backlight smooth check racing issue oplus_display_get_hbm_mode\n");
		return rc;
	}

	if (bl_lvl > 1) {
		if (bl_lvl > oplus_last_backlight)
			oplus_backlight_delta = bl_lvl - oplus_last_backlight;
		else
			oplus_backlight_delta = oplus_last_backlight - bl_lvl;
		oplus_backlight_time = ktime_get();
	}
	if (oplus_dimlayer_bl_enabled != oplus_dimlayer_bl_enable_real) {
		oplus_dimlayer_bl_enable_real = oplus_dimlayer_bl_enabled;
		if (oplus_dimlayer_bl_enable_real) {
			pr_err("Enter DC backlight\n");
		} else {
			pr_err("Exit DC backlight\n");
		}
	}

	bl_lvl = oplus_panel_process_dimming_v2(panel, bl_lvl, false);
	bl_lvl = oplus_panel_process_dimming_v3(panel, bl_lvl);

	if (oplus_dimlayer_bl_enable_real) {
		/*
		 * avoid effect power and aod mode
		 */
		if (bl_lvl > 1)
			bl_lvl = oplus_dimlayer_bl_alpha;
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")
		&& (bl_lvl <= PANEL_MAX_NOMAL_BRIGHTNESS)) {
			bl_lvl = backlight_buf[bl_lvl];
	}

	if(OPLUS_DISPLAY_NORMAL_SCENE == get_oplus_display_scene()){
		const struct mipi_dsi_host_ops *ops = dsi->host->ops;
		char payload[] = {MIPI_DCS_WRITE_CONTROL_DISPLAY, 0xE0};
		struct mipi_dsi_msg msg;

		if (!strcmp(panel->name,"samsung S6E3HC3 dsc cmd mode panel")) {
			if(panel->panel_id2 <= 2) {
				if(bl_lvl > HBM_BASE_600NIT)
					bl_lvl = HBM_BASE_600NIT;
				if (bl_lvl > panel->bl_config.bl_normal_max_level)
					bl_lvl = backlight_500_600nit_buf[bl_lvl - PANEL_MAX_NOMAL_BRIGHTNESS];
			} else {
				if (bl_lvl >= HBM_BASE_600NIT) {
					if((bl_lvl - HBM_BASE_600NIT > 5) && (enable_global_hbm_flags == 0))
						mipi_dsi_dcs_set_display_brightness(dsi, 3538);
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
					bl_lvl = backlight_600_800nit_buf[bl_lvl - HBM_BASE_600NIT];
					enable_global_hbm_flags = 1;
				} else {
					if(enable_global_hbm_flags == 1) {
						payload[1] = 0x20;
						memset(&msg, 0, sizeof(msg));
						msg.channel = dsi->channel;
						msg.tx_buf = payload;
						msg.tx_len = sizeof(payload);
						msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
						rc = ops->transfer(dsi->host, &msg);
						enable_global_hbm_flags = 0;
						mipi_dsi_dcs_set_display_brightness(dsi, 2047);
					}
					if (bl_lvl > PANEL_MAX_NOMAL_BRIGHTNESS) {
						bl_lvl = backlight_500_600nit_buf[bl_lvl - PANEL_MAX_NOMAL_BRIGHTNESS];
					}
				}
			}
		} else if ((!strcmp(panel->name,"samsung ams643ye01 amoled fhd+ panel") ||
			!strcmp(panel->name,"samsung ams643ye01 in 20057 amoled fhd+ panel") ||
			!strcmp(panel->name,"s6e3fc3_fhd_oled_cmd_samsung")) &&
			OPLUS_DISPLAY_AOD_SCENE != get_oplus_display_scene()) {
			if ((bl_lvl > panel->bl_config.bl_normal_max_level) && (oplus_last_backlight <= panel->bl_config.bl_normal_max_level))
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
			else if((bl_lvl <= panel->bl_config.bl_normal_max_level) && (oplus_last_backlight > panel->bl_config.bl_normal_max_level))
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT_SWITCH);

			if (rc)
				DSI_ERR("[%s] failed to send DSI_CMD_HBM cmds, rc=%d\n", panel->name, rc);
		} else if (!strcmp(panel->name,"samsung ams643ye01 in 20127 amoled fhd+ panel") &&
			OPLUS_DISPLAY_AOD_SCENE != get_oplus_display_scene()) {
			if (bl_lvl > panel->bl_config.bl_normal_max_level && bl_lvl < 2065) {
				bl_lvl = 2064;
			}
			if ((bl_lvl > panel->bl_config.bl_normal_max_level)&&(oplus_last_backlight <= panel->bl_config.bl_normal_max_level)) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER1_SWITCH);
				oplus_dsi_display_enable_and_waiting_for_next_te_irq();
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER2_SWITCH);
			} else if ((bl_lvl <= panel->bl_config.bl_normal_max_level)&&(oplus_last_backlight > panel->bl_config.bl_normal_max_level)) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT1_SWITCH);
				oplus_dsi_display_enable_and_waiting_for_next_te_irq();
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT2_SWITCH);
			}

			if (rc)
				DSI_ERR("[%s] failed to send DSI_CMD_HBM cmds, rc=%d\n", panel->name, rc);
		} else if (!strcmp(panel->oplus_priv.vendor_name, "AMB655X")) {
			if (bl_lvl > panel->bl_config.bl_normal_max_level) {
				enable_global_hbm_flags = 1;
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
				if (rc < 0)
					pr_err("send DSI_CMD_HBM_ENTER_SWITCH fail\n");
			} else if(enable_global_hbm_flags) {
				enable_global_hbm_flags = 0;
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT_SWITCH);
				if (rc < 0)
					pr_err("send DSI_CMD_HBM_ENTER_SWITCH fail\n");
			}
		} else if (!strcmp(panel->name,"nt36672c dsjm fhd plus video mode dsi panel")) {
			/* do nothing */
		} else if (oplus_panel_support_global_hbm_switch(panel, bl_lvl)) {
			DSI_DEBUG("is_support_panel_global_hbm_switch\n");
		} else {
			if (bl_lvl > panel->bl_config.bl_normal_max_level)
				payload[1] = 0xE0;
			else
				payload[1] = 0x20;

			memset(&msg, 0, sizeof(msg));
			msg.channel = dsi->channel;
			msg.tx_buf = payload;
			msg.tx_len = sizeof(payload);
			msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;

		#if defined(OPLUS_FEATURE_PXLW_IRIS5)
            if (iris_is_chip_supported() && iris_is_pt_mode(panel)) {
				struct dsi_cmd_desc hbm_cmd = {msg, 1, 1};
				struct dsi_panel_cmd_set cmdset = {.state = DSI_CMD_SET_STATE_HS, .count = 1,.cmds = &hbm_cmd,};
				rc = iris_pt_send_panel_cmd(panel, &cmdset);
			} else
		#endif
				rc = ops->transfer(dsi->host, &msg);

			if (rc < 0)
				pr_err("failed to backlight bl_lvl %d - ret=%d\n", bl_lvl, rc);
		}
	}
#endif /* OPLUS_BUG_STABILITY */

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    if (iris_is_chip_supported() && iris_is_pt_mode(panel))
        rc = iris_update_backlight(1, bl_lvl);
	else
		rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);
#else
	rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);
#endif
	if (panel->bl_config.bl_dcs_subtype == 0xc2)
		rc = dsi_panel_dcs_set_display_brightness_c2(dsi, bl_lvl);
	else
		rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);

	if (rc < 0)
		DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);

	if (panel->oplus_priv.low_light_adjust_gamma_support) {
		if(bl_lvl == 0) {
			panel->oplus_priv.low_light_gamma_is_adjusted = false;
		} else if (bl_lvl <= panel->oplus_priv.low_light_adjust_gamma_level) {
			if (!panel->oplus_priv.low_light_gamma_is_adjusted) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_GAMMA_LOWBL_COMMAND);
				if (rc < 0) {
					pr_err("send DSI_GAMMA_LOWBL_COMMAND fail\n");
					panel->oplus_priv.low_light_gamma_is_adjusted = false;
				} else {
					panel->oplus_priv.low_light_gamma_is_adjusted = true;
					pr_info("bl_lvl=%d, send DSI_GAMMA_LOWBL_COMMAND ok\n", bl_lvl);
				}
			}
		} else if (bl_lvl > panel->oplus_priv.low_light_adjust_gamma_level){
			if (panel->oplus_priv.low_light_gamma_is_adjusted) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_GAMMA_NOMAL_COMMAND);
				if (rc < 0) {
					pr_err("send DSI_GAMMA_LOWBL_COMMAND fail\n");
					panel->oplus_priv.low_light_gamma_is_adjusted = true;
				} else {
					panel->oplus_priv.low_light_gamma_is_adjusted = false;
					pr_info("bl_lvl=%d, send DSI_GAMMA_NOMAL_COMMAND ok\n", bl_lvl);
				}
			}
		}
	}

#ifdef OPLUS_BUG_STABILITY
	oplus_panel_process_dimming_v2_post(panel, false);
	oplus_last_backlight = bl_lvl;
#endif /* OPLUS_BUG_STABILITY */

	return rc;
}

static int dsi_panel_update_pwm_backlight(struct dsi_panel *panel,
	u32 bl_lvl)
{
	int rc = 0;
	u32 duty = 0;
	u32 period_ns = 0;
	struct dsi_backlight_config *bl;

	if (!panel) {
		DSI_ERR("Invalid Params\n");
		return -EINVAL;
	}

	bl = &panel->bl_config;
	if (!bl->pwm_bl) {
		DSI_ERR("pwm device not found\n");
		return -EINVAL;
	}

	period_ns = bl->pwm_period_usecs * NSEC_PER_USEC;
	duty = bl_lvl * period_ns;
	duty /= bl->bl_max_level;

	rc = pwm_config(bl->pwm_bl, duty, period_ns);
	if (rc) {
		DSI_ERR("[%s] failed to change pwm config, rc=\n", panel->name,
			rc);
		goto error;
	}

	if (bl_lvl == 0 && bl->pwm_enabled) {
		pwm_disable(bl->pwm_bl);
		bl->pwm_enabled = false;
		return 0;
	}

	if (!bl->pwm_enabled) {
		rc = pwm_enable(bl->pwm_bl);
		if (rc) {
			DSI_ERR("[%s] failed to enable pwm, rc=\n", panel->name,
				rc);
			goto error;
		}

		bl->pwm_enabled = true;
	}

error:
	return rc;
}

int dsi_panel_set_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	if (panel->host_config.ext_bridge_mode)
		return 0;

#ifdef OPLUS_BUG_STABILITY
	/* Add for silence and sau reboot */
	if(lcd_closebl_flag) {
		pr_err("silence reboot we should set backlight to zero\n");
		bl_lvl = 0;
	} else if (bl_lvl > 0) {
		lcd_closebl_flag_fp = 0;
	}
#endif
	DSI_DEBUG("backlight type:%d lvl:%d\n", bl->type, bl_lvl);
//#ifdef OPLUS_BUG_STABILITY
	if(!strcmp(panel->name,"samsung amb655xl08 amoled fhd+ panel")) {
		lcdinfo_notify(LCM_BRIGHTNESS_TYPE, &bl_lvl);
		DSI_DEBUG("backlight type:%d lvl:%d\n", bl->type, bl_lvl);
	}
	panel->bl_config.bl_lvl_backup = bl_lvl;
//#endif /* OPLUS_BUG_STABILITY */
	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		rc = backlight_device_set_brightness(bl->raw_bd, bl_lvl);
		break;
	case DSI_BACKLIGHT_DCS:
		rc = dsi_panel_update_backlight(panel, bl_lvl);
		break;
	case DSI_BACKLIGHT_EXTERNAL:
		#ifdef OPLUS_BUG_STABILITY
		/* add for oslo bringup */
		if(panel->nt36523w_ktz8866){
			if(bl_lvl < 0 || bl_lvl > 0x7ff) {
				DSI_ERR("%d %s oslo invalid backlight value = %d\n", __LINE__, __func__, bl_lvl);
			} else {
				if(1 == dis_set_first_level) {
					rc = mipi_dsi_dcs_set_display_brightness(&panel->mipi_device, 2047);
					if (rc < 0)
						DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);
				}
				rc = lcd_bl_set_led_brightness(bl_lvl);
			}
		}
		#endif
		break;
	case DSI_BACKLIGHT_PWM:
		rc = dsi_panel_update_pwm_backlight(panel, bl_lvl);
		break;
	default:
		DSI_ERR("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
	}

	return rc;
}

static u32 dsi_panel_get_brightness(struct dsi_backlight_config *bl)
{
	u32 cur_bl_level;
	struct backlight_device *bd = bl->raw_bd;

	/* default the brightness level to 50% */
	cur_bl_level = bl->bl_max_level >> 1;

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		/* Try to query the backlight level from the backlight device */
		if (bd->ops && bd->ops->get_brightness)
			cur_bl_level = bd->ops->get_brightness(bd);
		break;
	case DSI_BACKLIGHT_DCS:
	case DSI_BACKLIGHT_EXTERNAL:
	case DSI_BACKLIGHT_PWM:
	default:
		/*
		 * Ideally, we should read the backlight level from the
		 * panel. For now, just set it default value.
		 */
		break;
	}

	DSI_DEBUG("cur_bl_level=%d\n", cur_bl_level);
	return cur_bl_level;
}

void dsi_panel_bl_handoff(struct dsi_panel *panel)
{
	struct dsi_backlight_config *bl = &panel->bl_config;

	bl->bl_level = dsi_panel_get_brightness(bl);
}

static int dsi_panel_pwm_register(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	bl->pwm_bl = devm_of_pwm_get(panel->parent, panel->panel_of_node, NULL);
	if (IS_ERR_OR_NULL(bl->pwm_bl)) {
		rc = PTR_ERR(bl->pwm_bl);
		DSI_ERR("[%s] failed to request pwm, rc=%d\n", panel->name,
			rc);
		return rc;
	}

	return 0;
}

static int dsi_panel_bl_register(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		rc = dsi_panel_wled_register(panel, bl);
		break;
	case DSI_BACKLIGHT_DCS:
		break;
	case DSI_BACKLIGHT_EXTERNAL:
		break;
	case DSI_BACKLIGHT_PWM:
		rc = dsi_panel_pwm_register(panel);
		break;
	default:
		DSI_ERR("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
		goto error;
	}

error:
	return rc;
}

static void dsi_panel_pwm_unregister(struct dsi_panel *panel)
{
	struct dsi_backlight_config *bl = &panel->bl_config;

	devm_pwm_put(panel->parent, bl->pwm_bl);
}

static int dsi_panel_bl_unregister(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		break;
	case DSI_BACKLIGHT_DCS:
		break;
	case DSI_BACKLIGHT_EXTERNAL:
		break;
	case DSI_BACKLIGHT_PWM:
		dsi_panel_pwm_unregister(panel);
		break;
	default:
		DSI_ERR("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_timing(struct dsi_mode_info *mode,
				  struct dsi_parser_utils *utils)
{
	int rc = 0;
	u64 tmp64 = 0;
	struct dsi_display_mode *display_mode;
	struct dsi_display_mode_priv_info *priv_info;

	display_mode = container_of(mode, struct dsi_display_mode, timing);

	priv_info = display_mode->priv_info;

	rc = utils->read_u64(utils->data,
			"qcom,mdss-dsi-panel-clockrate", &tmp64);
	if (rc == -EOVERFLOW) {
		tmp64 = 0;
		rc = utils->read_u32(utils->data,
			"qcom,mdss-dsi-panel-clockrate", (u32 *)&tmp64);
	}

	mode->clk_rate_hz = !rc ? tmp64 : 0;
	display_mode->priv_info->clk_rate_hz = mode->clk_rate_hz;

	rc = utils->read_u32(utils->data, "qcom,mdss-mdp-transfer-time-us",
				&mode->mdp_transfer_time_us);
	if (!rc)
		display_mode->priv_info->mdp_transfer_time_us =
			mode->mdp_transfer_time_us;
	else
		display_mode->priv_info->mdp_transfer_time_us = 0;

	rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-panel-framerate",
				&mode->refresh_rate);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-panel-framerate, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-panel-width",
				  &mode->h_active);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-panel-width, rc=%d\n",
				rc);
		goto error;
	}

	rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-h-front-porch",
				  &mode->h_front_porch);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-h-front-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-h-back-porch",
				  &mode->h_back_porch);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-h-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-h-pulse-width",
				  &mode->h_sync_width);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-h-pulse-width, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-h-sync-skew",
				  &mode->h_skew);
	if (rc)
		DSI_ERR("qcom,mdss-dsi-h-sync-skew is not defined, rc=%d\n",
				rc);

	DSI_DEBUG("panel horz active:%d front_portch:%d back_porch:%d sync_skew:%d\n",
		mode->h_active, mode->h_front_porch, mode->h_back_porch,
		mode->h_sync_width);

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-panel-height",
				  &mode->v_active);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-panel-height, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-v-back-porch",
				  &mode->v_back_porch);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-v-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-v-front-porch",
				  &mode->v_front_porch);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-v-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-v-pulse-width",
				  &mode->v_sync_width);
	if (rc) {
		DSI_ERR("failed to read qcom,mdss-dsi-v-pulse-width, rc=%d\n",
		       rc);
		goto error;
	}
	DSI_DEBUG("panel vert active:%d front_portch:%d back_porch:%d pulse_width:%d\n",
		mode->v_active, mode->v_front_porch, mode->v_back_porch,
		mode->v_sync_width);

error:
	return rc;
}

static int dsi_panel_parse_pixel_format(struct dsi_host_common_cfg *host,
					struct dsi_parser_utils *utils,
					const char *name)
{
	int rc = 0;
	u32 bpp = 0;
	enum dsi_pixel_format fmt;
	const char *packing;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bpp", &bpp);
	if (rc) {
		DSI_ERR("[%s] failed to read qcom,mdss-dsi-bpp, rc=%d\n",
		       name, rc);
		return rc;
	}

	host->bpp = bpp;

	switch (bpp) {
	case 3:
		fmt = DSI_PIXEL_FORMAT_RGB111;
		break;
	case 8:
		fmt = DSI_PIXEL_FORMAT_RGB332;
		break;
	case 12:
		fmt = DSI_PIXEL_FORMAT_RGB444;
		break;
	case 16:
		fmt = DSI_PIXEL_FORMAT_RGB565;
		break;
	case 18:
		fmt = DSI_PIXEL_FORMAT_RGB666;
		break;
	case 24:
	default:
		fmt = DSI_PIXEL_FORMAT_RGB888;
		break;
	}

	if (fmt == DSI_PIXEL_FORMAT_RGB666) {
		packing = utils->get_property(utils->data,
					  "qcom,mdss-dsi-pixel-packing",
					  NULL);
		if (packing && !strcmp(packing, "loose"))
			fmt = DSI_PIXEL_FORMAT_RGB666_LOOSE;
	}

	host->dst_format = fmt;
	return rc;
}

static int dsi_panel_parse_lane_states(struct dsi_host_common_cfg *host,
				       struct dsi_parser_utils *utils,
				       const char *name)
{
	int rc = 0;
	bool lane_enabled;
	u32 num_of_lanes = 0;

	lane_enabled = utils->read_bool(utils->data,
					    "qcom,mdss-dsi-lane-0-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_0 : 0);

	lane_enabled = utils->read_bool(utils->data,
					     "qcom,mdss-dsi-lane-1-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_1 : 0);

	lane_enabled = utils->read_bool(utils->data,
					    "qcom,mdss-dsi-lane-2-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_2 : 0);

	lane_enabled = utils->read_bool(utils->data,
					     "qcom,mdss-dsi-lane-3-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_3 : 0);

	if (host->data_lanes & DSI_DATA_LANE_0)
		num_of_lanes++;
	if (host->data_lanes & DSI_DATA_LANE_1)
		num_of_lanes++;
	if (host->data_lanes & DSI_DATA_LANE_2)
		num_of_lanes++;
	if (host->data_lanes & DSI_DATA_LANE_3)
		num_of_lanes++;

	host->num_data_lanes = num_of_lanes;

	if (host->data_lanes == 0) {
		DSI_ERR("[%s] No data lanes are enabled, rc=%d\n", name, rc);
		rc = -EINVAL;
	}

	return rc;
}

static int dsi_panel_parse_color_swap(struct dsi_host_common_cfg *host,
				      struct dsi_parser_utils *utils,
				      const char *name)
{
	int rc = 0;
	const char *swap_mode;

	swap_mode = utils->get_property(utils->data,
			"qcom,mdss-dsi-color-order", NULL);
	if (swap_mode) {
		if (!strcmp(swap_mode, "rgb_swap_rgb")) {
			host->swap_mode = DSI_COLOR_SWAP_RGB;
		} else if (!strcmp(swap_mode, "rgb_swap_rbg")) {
			host->swap_mode = DSI_COLOR_SWAP_RBG;
		} else if (!strcmp(swap_mode, "rgb_swap_brg")) {
			host->swap_mode = DSI_COLOR_SWAP_BRG;
		} else if (!strcmp(swap_mode, "rgb_swap_grb")) {
			host->swap_mode = DSI_COLOR_SWAP_GRB;
		} else if (!strcmp(swap_mode, "rgb_swap_gbr")) {
			host->swap_mode = DSI_COLOR_SWAP_GBR;
		} else {
			DSI_ERR("[%s] Unrecognized color order-%s\n",
			       name, swap_mode);
			rc = -EINVAL;
		}
	} else {
		DSI_DEBUG("[%s] Falling back to default color order\n", name);
		host->swap_mode = DSI_COLOR_SWAP_RGB;
	}

	/* bit swap on color channel is not defined in dt */
	host->bit_swap_red = false;
	host->bit_swap_green = false;
	host->bit_swap_blue = false;
	return rc;
}

static int dsi_panel_parse_triggers(struct dsi_host_common_cfg *host,
				    struct dsi_parser_utils *utils,
				    const char *name)
{
	const char *trig;
	int rc = 0;

	trig = utils->get_property(utils->data,
			"qcom,mdss-dsi-mdp-trigger", NULL);
	if (trig) {
		if (!strcmp(trig, "none")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_NONE;
		} else if (!strcmp(trig, "trigger_te")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_TE;
		} else if (!strcmp(trig, "trigger_sw")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_SW;
		} else if (!strcmp(trig, "trigger_sw_te")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_SW_TE;
		} else {
			DSI_ERR("[%s] Unrecognized mdp trigger type (%s)\n",
			       name, trig);
			rc = -EINVAL;
		}

	} else {
		DSI_DEBUG("[%s] Falling back to default MDP trigger\n",
			 name);
		host->mdp_cmd_trigger = DSI_TRIGGER_SW;
	}

	trig = utils->get_property(utils->data,
			"qcom,mdss-dsi-dma-trigger", NULL);
	if (trig) {
		if (!strcmp(trig, "none")) {
			host->dma_cmd_trigger = DSI_TRIGGER_NONE;
		} else if (!strcmp(trig, "trigger_te")) {
			host->dma_cmd_trigger = DSI_TRIGGER_TE;
		} else if (!strcmp(trig, "trigger_sw")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW;
		} else if (!strcmp(trig, "trigger_sw_seof")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW_SEOF;
		} else if (!strcmp(trig, "trigger_sw_te")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW_TE;
		} else {
			DSI_ERR("[%s] Unrecognized mdp trigger type (%s)\n",
			       name, trig);
			rc = -EINVAL;
		}

	} else {
		DSI_DEBUG("[%s] Falling back to default MDP trigger\n", name);
		host->dma_cmd_trigger = DSI_TRIGGER_SW;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-te-pin-select",
			&host->te_mode);
	if (rc) {
		DSI_WARN("[%s] fallback to default te-pin-select\n", name);
		host->te_mode = 1;
		rc = 0;
	}

	return rc;
}

static int dsi_panel_parse_misc_host_config(struct dsi_host_common_cfg *host,
					    struct dsi_parser_utils *utils,
					    const char *name)
{
	u32 val = 0;
	int rc = 0;
	bool panel_cphy_mode = false;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-t-clk-post", &val);
	if (!rc) {
		host->t_clk_post = val;
		DSI_DEBUG("[%s] t_clk_post = %d\n", name, val);
	}

	val = 0;
	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-t-clk-pre", &val);
	if (!rc) {
		host->t_clk_pre = val;
		DSI_DEBUG("[%s] t_clk_pre = %d\n", name, val);
	}

	host->t_clk_pre_extend = utils->read_bool(utils->data,
						"qcom,mdss-dsi-t-clk-pre-extend");

	host->ignore_rx_eot = utils->read_bool(utils->data,
						"qcom,mdss-dsi-rx-eot-ignore");

	host->append_tx_eot = utils->read_bool(utils->data,
						"qcom,mdss-dsi-tx-eot-append");

	host->ext_bridge_mode = utils->read_bool(utils->data,
					"qcom,mdss-dsi-ext-bridge-mode");

	host->force_hs_clk_lane = utils->read_bool(utils->data,
					"qcom,mdss-dsi-force-clock-lane-hs");
	panel_cphy_mode = utils->read_bool(utils->data,
					"qcom,panel-cphy-mode");
	host->phy_type = panel_cphy_mode ? DSI_PHY_TYPE_CPHY
						: DSI_PHY_TYPE_DPHY;

	return 0;
}

static void dsi_panel_parse_split_link_config(struct dsi_host_common_cfg *host,
					struct dsi_parser_utils *utils,
					const char *name)
{
	int rc = 0;
	u32 val = 0;
	bool supported = false;
	struct dsi_split_link_config *split_link = &host->split_link;

	supported = utils->read_bool(utils->data, "qcom,split-link-enabled");

	if (!supported) {
		DSI_DEBUG("[%s] Split link is not supported\n", name);
		split_link->split_link_enabled = false;
		return;
	}

	rc = utils->read_u32(utils->data, "qcom,sublinks-count", &val);
	if (rc || val < 1) {
		DSI_DEBUG("[%s] Using default sublinks count\n", name);
		split_link->num_sublinks = 2;
	} else {
		split_link->num_sublinks = val;
	}

	rc = utils->read_u32(utils->data, "qcom,lanes-per-sublink", &val);
	if (rc || val < 1) {
		DSI_DEBUG("[%s] Using default lanes per sublink\n", name);
		split_link->lanes_per_sublink = 2;
	} else {
		split_link->lanes_per_sublink = val;
	}

	DSI_DEBUG("[%s] Split link is supported %d-%d\n", name,
		split_link->num_sublinks, split_link->lanes_per_sublink);
	split_link->split_link_enabled = true;
}

static int dsi_panel_parse_host_config(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	rc = dsi_panel_parse_pixel_format(&panel->host_config, utils,
					  panel->name);
	if (rc) {
		DSI_ERR("[%s] failed to get pixel format, rc=%d\n",
		panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_lane_states(&panel->host_config, utils,
					 panel->name);
	if (rc) {
		DSI_ERR("[%s] failed to parse lane states, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_color_swap(&panel->host_config, utils,
					panel->name);
	if (rc) {
		DSI_ERR("[%s] failed to parse color swap config, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_triggers(&panel->host_config, utils,
				      panel->name);
	if (rc) {
		DSI_ERR("[%s] failed to parse triggers, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_misc_host_config(&panel->host_config, utils,
					      panel->name);
	if (rc) {
		DSI_ERR("[%s] failed to parse misc host config, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	dsi_panel_parse_split_link_config(&panel->host_config, utils,
						panel->name);

error:
	return rc;
}

static int dsi_panel_parse_qsync_caps(struct dsi_panel *panel,
				     struct device_node *of_node)
{
	int rc = 0;
	u32 val = 0, i;
	struct dsi_qsync_capabilities *qsync_caps = &panel->qsync_caps;
	struct dsi_parser_utils *utils = &panel->utils;
	const char *name = panel->name;

	/**
	 * "mdss-dsi-qsync-min-refresh-rate" is defined in cmd mode and
	 *  video mode when there is only one qsync min fps present.
	 */
	rc = of_property_read_u32(of_node,
				  "qcom,mdss-dsi-qsync-min-refresh-rate",
				  &val);
	if (rc)
		DSI_DEBUG("[%s] qsync min fps not defined rc:%d\n",
			panel->name, rc);

	qsync_caps->qsync_min_fps = val;

	/**
	 * "dsi-supported-qsync-min-fps-list" may be defined in video
	 *  mode, only in dfps case when "qcom,dsi-supported-dfps-list"
	 *  is defined.
	 */
	qsync_caps->qsync_min_fps_list_len = utils->count_u32_elems(utils->data,
				  "qcom,dsi-supported-qsync-min-fps-list");
	if (qsync_caps->qsync_min_fps_list_len < 1)
		goto qsync_support;

	/**
	 * qcom,dsi-supported-qsync-min-fps-list cannot be defined
	 *  along with qcom,mdss-dsi-qsync-min-refresh-rate.
	 */
	if (qsync_caps->qsync_min_fps_list_len >= 1 &&
		qsync_caps->qsync_min_fps) {
		DSI_ERR("[%s] Both qsync nodes are defined\n",
				name);
		rc = -EINVAL;
		goto error;
	}

	if (panel->dfps_caps.dfps_list_len !=
			qsync_caps->qsync_min_fps_list_len) {
		DSI_ERR("[%s] Qsync min fps list mismatch with dfps\n", name);
		rc = -EINVAL;
		goto error;
	}

	qsync_caps->qsync_min_fps_list =
		kcalloc(qsync_caps->qsync_min_fps_list_len, sizeof(u32),
			GFP_KERNEL);
	if (!qsync_caps->qsync_min_fps_list) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data,
			"qcom,dsi-supported-qsync-min-fps-list",
			qsync_caps->qsync_min_fps_list,
			qsync_caps->qsync_min_fps_list_len);
	if (rc) {
		DSI_ERR("[%s] Qsync min fps list parse failed\n", name);
		rc = -EINVAL;
		goto error;
	}

	qsync_caps->qsync_min_fps = qsync_caps->qsync_min_fps_list[0];

	for (i = 1; i < qsync_caps->qsync_min_fps_list_len; i++) {
		if (qsync_caps->qsync_min_fps_list[i] <
				qsync_caps->qsync_min_fps)
			qsync_caps->qsync_min_fps =
				qsync_caps->qsync_min_fps_list[i];
	}

qsync_support:
	/* allow qsync support only if DFPS is with VFP approach */
	if ((panel->dfps_caps.dfps_support) &&
	    !(panel->dfps_caps.type == DSI_DFPS_IMMEDIATE_VFP))
		panel->qsync_caps.qsync_min_fps = 0;

error:
	if (rc < 0) {
		qsync_caps->qsync_min_fps = 0;
		qsync_caps->qsync_min_fps_list_len = 0;
	}
	return rc;
}

static int dsi_panel_parse_dyn_clk_caps(struct dsi_panel *panel)
{
	int rc = 0;
	bool supported = false;
	struct dsi_dyn_clk_caps *dyn_clk_caps = &panel->dyn_clk_caps;
	struct dsi_parser_utils *utils = &panel->utils;
	const char *name = panel->name;
	const char *type;

	supported = utils->read_bool(utils->data, "qcom,dsi-dyn-clk-enable");

	if (!supported) {
		dyn_clk_caps->dyn_clk_support = false;
		return rc;
	}

	dyn_clk_caps->bit_clk_list_len = utils->count_u32_elems(utils->data,
			"qcom,dsi-dyn-clk-list");

	if (dyn_clk_caps->bit_clk_list_len < 1) {
		DSI_ERR("[%s] failed to get supported bit clk list\n", name);
		return -EINVAL;
	}

	dyn_clk_caps->bit_clk_list = kcalloc(dyn_clk_caps->bit_clk_list_len,
			sizeof(u32), GFP_KERNEL);
	if (!dyn_clk_caps->bit_clk_list)
		return -ENOMEM;

	rc = utils->read_u32_array(utils->data, "qcom,dsi-dyn-clk-list",
			dyn_clk_caps->bit_clk_list,
			dyn_clk_caps->bit_clk_list_len);

	if (rc) {
		DSI_ERR("[%s] failed to parse supported bit clk list\n", name);
		return -EINVAL;
	}

	dyn_clk_caps->dyn_clk_support = true;

	type = utils->get_property(utils->data,
		"qcom,dsi-dyn-clk-type", NULL);
	if (!type) {
		dyn_clk_caps->type = DSI_DYN_CLK_TYPE_LEGACY;
		dyn_clk_caps->maintain_const_fps = false;
		return 0;
	}
	if (!strcmp(type, "constant-fps-adjust-hfp")) {
		dyn_clk_caps->type = DSI_DYN_CLK_TYPE_CONST_FPS_ADJUST_HFP;
		dyn_clk_caps->maintain_const_fps = true;
	} else if (!strcmp(type, "constant-fps-adjust-vfp")) {
		dyn_clk_caps->type = DSI_DYN_CLK_TYPE_CONST_FPS_ADJUST_VFP;
		dyn_clk_caps->maintain_const_fps = true;
	} else {
		dyn_clk_caps->type = DSI_DYN_CLK_TYPE_LEGACY;
		dyn_clk_caps->maintain_const_fps = false;
	}
	DSI_DEBUG("Dynamic clock type is [%s]\n", type);
	return 0;
}

static int dsi_panel_parse_dfps_caps(struct dsi_panel *panel)
{
	int rc = 0;
	bool supported = false;
	struct dsi_dfps_capabilities *dfps_caps = &panel->dfps_caps;
	struct dsi_parser_utils *utils = &panel->utils;
	const char *name = panel->name;
	const char *type;
	u32 i;

	supported = utils->read_bool(utils->data,
			"qcom,mdss-dsi-pan-enable-dynamic-fps");

	if (!supported) {
		DSI_DEBUG("[%s] DFPS is not supported\n", name);
		dfps_caps->dfps_support = false;
		return rc;
	}

	type = utils->get_property(utils->data,
			"qcom,mdss-dsi-pan-fps-update", NULL);
	if (!type) {
		DSI_ERR("[%s] dfps type not defined\n", name);
		rc = -EINVAL;
		goto error;
	} else if (!strcmp(type, "dfps_suspend_resume_mode")) {
		dfps_caps->type = DSI_DFPS_SUSPEND_RESUME;
	} else if (!strcmp(type, "dfps_immediate_clk_mode")) {
		dfps_caps->type = DSI_DFPS_IMMEDIATE_CLK;
	} else if (!strcmp(type, "dfps_immediate_porch_mode_hfp")) {
		dfps_caps->type = DSI_DFPS_IMMEDIATE_HFP;
	} else if (!strcmp(type, "dfps_immediate_porch_mode_vfp")) {
		dfps_caps->type = DSI_DFPS_IMMEDIATE_VFP;
	} else {
		DSI_ERR("[%s] dfps type is not recognized\n", name);
		rc = -EINVAL;
		goto error;
	}

	dfps_caps->dfps_list_len = utils->count_u32_elems(utils->data,
				  "qcom,dsi-supported-dfps-list");
	if (dfps_caps->dfps_list_len < 1) {
		DSI_ERR("[%s] dfps refresh list not present\n", name);
		rc = -EINVAL;
		goto error;
	}

	dfps_caps->dfps_list = kcalloc(dfps_caps->dfps_list_len, sizeof(u32),
			GFP_KERNEL);
	if (!dfps_caps->dfps_list) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data,
			"qcom,dsi-supported-dfps-list",
			dfps_caps->dfps_list,
			dfps_caps->dfps_list_len);
	if (rc) {
		DSI_ERR("[%s] dfps refresh rate list parse failed\n", name);
		rc = -EINVAL;
		goto error;
	}
	dfps_caps->dfps_support = true;

	/* calculate max and min fps */
	dfps_caps->max_refresh_rate = dfps_caps->dfps_list[0];
	dfps_caps->min_refresh_rate = dfps_caps->dfps_list[0];

	for (i = 1; i < dfps_caps->dfps_list_len; i++) {
		if (dfps_caps->dfps_list[i] < dfps_caps->min_refresh_rate)
			dfps_caps->min_refresh_rate = dfps_caps->dfps_list[i];
		else if (dfps_caps->dfps_list[i] > dfps_caps->max_refresh_rate)
			dfps_caps->max_refresh_rate = dfps_caps->dfps_list[i];
	}

error:
	return rc;
}

static int dsi_panel_parse_video_host_config(struct dsi_video_engine_cfg *cfg,
					     struct dsi_parser_utils *utils,
					     const char *name)
{
	int rc = 0;
	const char *traffic_mode;
	u32 vc_id = 0;
	u32 val = 0;
	u32 line_no = 0;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-h-sync-pulse", &val);
	if (rc) {
		DSI_DEBUG("[%s] fallback to default h-sync-pulse\n", name);
		cfg->pulse_mode_hsa_he = false;
	} else if (val == 1) {
		cfg->pulse_mode_hsa_he = true;
	} else if (val == 0) {
		cfg->pulse_mode_hsa_he = false;
	} else {
		DSI_ERR("[%s] Unrecognized value for mdss-dsi-h-sync-pulse\n",
		       name);
		rc = -EINVAL;
		goto error;
	}

	cfg->hfp_lp11_en = utils->read_bool(utils->data,
						"qcom,mdss-dsi-hfp-power-mode");

	cfg->hbp_lp11_en = utils->read_bool(utils->data,
						"qcom,mdss-dsi-hbp-power-mode");

	cfg->hsa_lp11_en = utils->read_bool(utils->data,
						"qcom,mdss-dsi-hsa-power-mode");

	cfg->last_line_interleave_en = utils->read_bool(utils->data,
					"qcom,mdss-dsi-last-line-interleave");

	cfg->eof_bllp_lp11_en = utils->read_bool(utils->data,
					"qcom,mdss-dsi-bllp-eof-power-mode");

	cfg->bllp_lp11_en = utils->read_bool(utils->data,
					"qcom,mdss-dsi-bllp-power-mode");

	traffic_mode = utils->get_property(utils->data,
				       "qcom,mdss-dsi-traffic-mode",
				       NULL);
	if (!traffic_mode) {
		DSI_DEBUG("[%s] Falling back to default traffic mode\n", name);
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_PULSES;
	} else if (!strcmp(traffic_mode, "non_burst_sync_pulse")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_PULSES;
	} else if (!strcmp(traffic_mode, "non_burst_sync_event")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_START_EVENTS;
	} else if (!strcmp(traffic_mode, "burst_mode")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_BURST_MODE;
	} else {
		DSI_ERR("[%s] Unrecognized traffic mode-%s\n", name,
		       traffic_mode);
		rc = -EINVAL;
		goto error;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-virtual-channel-id",
				  &vc_id);
	if (rc) {
		DSI_DEBUG("[%s] Fallback to default vc id\n", name);
		cfg->vc_id = 0;
	} else {
		cfg->vc_id = vc_id;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-dma-schedule-line",
				  &line_no);
	if (rc) {
		DSI_DEBUG("[%s] set default dma scheduling line no\n", name);
		cfg->dma_sched_line = 0x1;
		/* do not fail since we have default value */
		rc = 0;
	} else {
		cfg->dma_sched_line = line_no;
	}

error:
	return rc;
}

static int dsi_panel_parse_cmd_host_config(struct dsi_cmd_engine_cfg *cfg,
					   struct dsi_parser_utils *utils,
					   const char *name)
{
	u32 val = 0;
	int rc = 0;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-wr-mem-start", &val);
	if (rc) {
		DSI_DEBUG("[%s] Fallback to default wr-mem-start\n", name);
		cfg->wr_mem_start = 0x2C;
	} else {
		cfg->wr_mem_start = val;
	}

	val = 0;
	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-wr-mem-continue",
				  &val);
	if (rc) {
		DSI_DEBUG("[%s] Fallback to default wr-mem-continue\n", name);
		cfg->wr_mem_continue = 0x3C;
	} else {
		cfg->wr_mem_continue = val;
	}

	/* TODO:  fix following */
	cfg->max_cmd_packets_interleave = 0;

	val = 0;
	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-te-dcs-command",
				  &val);
	if (rc) {
		DSI_DEBUG("[%s] fallback to default te-dcs-cmd\n", name);
		cfg->insert_dcs_command = true;
	} else if (val == 1) {
		cfg->insert_dcs_command = true;
	} else if (val == 0) {
		cfg->insert_dcs_command = false;
	} else {
		DSI_ERR("[%s] Unrecognized value for mdss-dsi-te-dcs-command\n",
		       name);
		rc = -EINVAL;
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_panel_mode(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	bool panel_mode_switch_enabled;
	enum dsi_op_mode panel_mode;
	const char *mode;

	mode = utils->get_property(utils->data,
			"qcom,mdss-dsi-panel-type", NULL);
	if (!mode) {
		DSI_DEBUG("[%s] Fallback to default panel mode\n", panel->name);
		panel_mode = DSI_OP_VIDEO_MODE;
	} else if (!strcmp(mode, "dsi_video_mode")) {
		panel_mode = DSI_OP_VIDEO_MODE;
	} else if (!strcmp(mode, "dsi_cmd_mode")) {
		panel_mode = DSI_OP_CMD_MODE;
	} else {
		DSI_ERR("[%s] Unrecognized panel type-%s\n", panel->name, mode);
		rc = -EINVAL;
		goto error;
	}

	panel_mode_switch_enabled = utils->read_bool(utils->data,
					"qcom,mdss-dsi-panel-mode-switch");

	DSI_DEBUG("%s: panel operating mode switch feature %s\n", __func__,
		(panel_mode_switch_enabled ? "enabled" : "disabled"));

	if (panel_mode == DSI_OP_VIDEO_MODE || panel_mode_switch_enabled) {
		rc = dsi_panel_parse_video_host_config(&panel->video_config,
						       utils,
						       panel->name);
		if (rc) {
			DSI_ERR("[%s] Failed to parse video host cfg, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	if (panel_mode == DSI_OP_CMD_MODE || panel_mode_switch_enabled) {
		rc = dsi_panel_parse_cmd_host_config(&panel->cmd_config,
						     utils,
						     panel->name);
		if (rc) {
			DSI_ERR("[%s] Failed to parse cmd host config, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	panel->panel_mode = panel_mode;
	panel->panel_mode_switch_enabled = panel_mode_switch_enabled;
error:
	return rc;
}

static int dsi_panel_parse_phy_props(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val = 0;
	const char *str;
	struct dsi_panel_phy_props *props = &panel->phy_props;
	struct dsi_parser_utils *utils = &panel->utils;
	const char *name = panel->name;

	rc = utils->read_u32(utils->data,
		  "qcom,mdss-pan-physical-width-dimension", &val);
	if (rc) {
		DSI_DEBUG("[%s] Physical panel width is not defined\n", name);
		props->panel_width_mm = 0;
		rc = 0;
	} else {
		props->panel_width_mm = val;
	}

	rc = utils->read_u32(utils->data,
				  "qcom,mdss-pan-physical-height-dimension",
				  &val);
	if (rc) {
		DSI_DEBUG("[%s] Physical panel height is not defined\n", name);
		props->panel_height_mm = 0;
		rc = 0;
	} else {
		props->panel_height_mm = val;
	}

	str = utils->get_property(utils->data,
			"qcom,mdss-dsi-panel-orientation", NULL);
	if (!str) {
		props->rotation = DSI_PANEL_ROTATE_NONE;
	} else if (!strcmp(str, "180")) {
		props->rotation = DSI_PANEL_ROTATE_HV_FLIP;
	} else if (!strcmp(str, "hflip")) {
		props->rotation = DSI_PANEL_ROTATE_H_FLIP;
	} else if (!strcmp(str, "vflip")) {
		props->rotation = DSI_PANEL_ROTATE_V_FLIP;
	} else {
		DSI_ERR("[%s] Unrecognized panel rotation-%s\n", name, str);
		rc = -EINVAL;
		goto error;
	}
error:
	return rc;
}
const char *cmd_set_prop_map[DSI_CMD_SET_MAX] = {
	"qcom,mdss-dsi-pre-on-command",
	"qcom,mdss-dsi-on-command",
	"qcom,mdss-dsi-post-panel-on-command",
	"qcom,mdss-dsi-pre-off-command",
	"qcom,mdss-dsi-off-command",
	"qcom,mdss-dsi-post-off-command",
	"qcom,mdss-dsi-pre-res-switch",
	"qcom,mdss-dsi-res-switch",
	"qcom,mdss-dsi-post-res-switch",
	"qcom,cmd-to-video-mode-switch-commands",
	"qcom,cmd-to-video-mode-post-switch-commands",
	"qcom,video-to-cmd-mode-switch-commands",
	"qcom,video-to-cmd-mode-post-switch-commands",
	"qcom,mdss-dsi-panel-status-command",
	"qcom,mdss-dsi-lp1-command",
	"qcom,mdss-dsi-lp2-command",
	"qcom,mdss-dsi-nolp-command",
	"PPS not parsed from DTSI, generated dynamically",
	"ROI not parsed from DTSI, generated dynamically",
	"qcom,mdss-dsi-timing-switch-command",
	"qcom,mdss-dsi-post-mode-switch-on-command",
	"qcom,mdss-dsi-qsync-on-commands",
	"qcom,mdss-dsi-qsync-off-commands",
#ifdef OPLUS_BUG_STABILITY
	"qcom,mdss-dsi-post-on-backlight",
	"qcom,mdss-dsi-aod-on-command",
	"qcom,mdss-dsi-aod-off-command",
	"qcom,mdss-dsi-hbm-on-command",
	"qcom,mdss-dsi-hbm-off-command",
	"qcom,mdss-dsi-aod-hbm-on-command",
	"qcom,mdss-dsi-aod-hbm-off-command",
	"qcom,mdss-dsi-seed-0-command",
	"qcom,mdss-dsi-seed-1-command",
	"qcom,mdss-dsi-seed-2-command",
	"qcom,mdss-dsi-seed-3-command",
	"qcom,mdss-dsi-seed-4-command",
	"qcom,mdss-dsi-seed-0-switch-command",
	"qcom,mdss-dsi-seed-1-switch-command",
	"qcom,mdss-dsi-seed-2-switch-command",
	"qcom,mdss-dsi-seed-0-dc-switch-command",
	"qcom,mdss-dsi-seed-1-dc-switch-command",
	"qcom,mdss-dsi-seed-2-dc-switch-command",
	"qcom,mdss-dsi-seed-0-dc-command",
	"qcom,mdss-dsi-seed-1-dc-command",
	"qcom,mdss-dsi-seed-2-dc-command",
	"qcom,mdss-dsi-seed-off-command",
	"qcom,mdss-dsi-normal-hbm-on-command",
	"qcom,mdss-dsi-aod-high-mode-command",
	"qcom,mdss-dsi-aod-low-mode-command",
	"qcom,mdss-dsi-spr-0-command",
	"qcom,mdss-dsi-spr-1-command",
	"qcom,mdss-dsi-spr-2-command",
	"qcom,mdss-dsi-data-dimming-on-command",
	"qcom,mdss-dsi-data-dimming-off-command",
	"qcom,mdss-dsi-osc-clk-mode0-command",
	"qcom,mdss-dsi-osc-clk-mode1-command",
	"qcom,mdss-dsi-osc-clk-mode2-command",
	"qcom,mdss-dsi-osc-clk-mode3-command",
	"qcom,mdss-dsi-failsafe-on-command",
	"qcom,mdss-dsi-failsafe-off-command",
	"qcom,mdss-dsi-seed-enter-command",
	"qcom,mdss-dsi-seed-exit-command",
	"qcom,mdss-dsi-panel-id1-command",
	"qcom,mdss-dsi-panel-read-register-open-command",
	"qcom,mdss-dsi-panel-read-register-close-command",
	"qcom,mdss-dsi-mca-on-command",
	"qcom,mdss-dsi-mca-off-command",
	"qcom,mdss-dsi-loading-effect-1-command",
	"qcom,mdss-dsi-loading-effect-2-command",
	"qcom,mdss-dsi-loading-effect-off-command",
	"qcom,mdss-dsi-hbm-enter-switch-command",
	"qcom,mdss-dsi-hbm-enter1-switch-command",
	"qcom,mdss-dsi-hbm-enter2-switch-command",
	"qcom,mdss-dsi-hbm-exit-switch-command",
	"qcom,mdss-dsi-hbm-exit1-switch-command",
	"qcom,mdss-dsi-hbm-exit2-switch-command",
	"qcom,mdss-dsi-aor-restore-command",
	"qcom,mdss-dsi-lp1-pvt-command",
	"qcom,mdss-dsi-nolp-pvt-command",
	"qcom,mdss-dsi-aod-hbm-on-pvt-command",
	"qcom,mdss-dsi-aod-hbm-off-pvt-command",
	"qcom,mdss-dsi-dly-off-command",
	"qcom,mdss-dsi-panel-register-read-command",
	"qcom,mdss-dsi-panel-level2-key-enable-command",
	"qcom,mdss-dsi-panel-level2-key-disable-command",
	"qcom,mdss-dsi-fps-switch-command",
	"qcom,mdss-dsi-cabc-off-command",
	"qcom,mdss-dsi-cabc-1-command",
	"qcom,mdss-dsi-cabc-2-command",
	"qcom,mdss-dsi-cabc-3-command",
	"qcom,mdss-dsi-gamma-nomal-command",
	"qcom,mdss-dsi-gamma-lowbl-command",
/* add for optimizing the display effect under low backlight brightness */
	"qcom,mdss-dsi-panel-dimming-gamma-command",
	"qcom,mdss-dsi-fps60-command",
	"qcom,mdss-dsi-fps120-command",
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	"iris,abyp-panel-command",
#endif
#endif /*OPLUS_BUG_STABILITY*/

#ifdef OPLUS_FEATURE_ADFR
	"qcom,mdss-dsi-qsync-min-fps-0-command",
	"qcom,mdss-dsi-qsync-min-fps-1-command",
	"qcom,mdss-dsi-qsync-min-fps-2-command",
	"qcom,mdss-dsi-qsync-min-fps-3-command",
	"qcom,mdss-dsi-qsync-min-fps-4-command",
	"qcom,mdss-dsi-qsync-min-fps-5-command",
	"qcom,mdss-dsi-qsync-min-fps-6-command",
	"qcom,mdss-dsi-qsync-min-fps-7-command",
	"qcom,mdss-dsi-qsync-min-fps-8-command",
	"qcom,mdss-dsi-qsync-min-fps-9-command",
	"qcom,mdss-dsi-fakeframe-command",
	"qcom,mdss-dsi-adfr-pre-switch-command",
#endif
};

const char *cmd_set_state_map[DSI_CMD_SET_MAX] = {
	"qcom,mdss-dsi-pre-on-command-state",
	"qcom,mdss-dsi-on-command-state",
	"qcom,mdss-dsi-post-on-command-state",
	"qcom,mdss-dsi-pre-off-command-state",
	"qcom,mdss-dsi-off-command-state",
	"qcom,mdss-dsi-post-off-command-state",
	"qcom,mdss-dsi-pre-res-switch-state",
	"qcom,mdss-dsi-res-switch-state",
	"qcom,mdss-dsi-post-res-switch-state",
	"qcom,cmd-to-video-mode-switch-commands-state",
	"qcom,cmd-to-video-mode-post-switch-commands-state",
	"qcom,video-to-cmd-mode-switch-commands-state",
	"qcom,video-to-cmd-mode-post-switch-commands-state",
	"qcom,mdss-dsi-panel-status-command-state",
	"qcom,mdss-dsi-lp1-command-state",
	"qcom,mdss-dsi-lp2-command-state",
	"qcom,mdss-dsi-nolp-command-state",
	"PPS not parsed from DTSI, generated dynamically",
	"ROI not parsed from DTSI, generated dynamically",
	"qcom,mdss-dsi-timing-switch-command-state",
	"qcom,mdss-dsi-post-mode-switch-on-command-state",
	"qcom,mdss-dsi-qsync-on-commands-state",
	"qcom,mdss-dsi-qsync-off-commands-state",
#ifdef OPLUS_BUG_STABILITY
	"qcom,mdss-dsi-post-on-backlight-state",
	"qcom,mdss-dsi-aod-on-command-state",
	"qcom,mdss-dsi-aod-off-command-state",
	"qcom,mdss-dsi-hbm-on-command-state",
	"qcom,mdss-dsi-hbm-off-command-state",
	"qcom,mdss-dsi-aod-hbm-on-command-state",
	"qcom,mdss-dsi-aod-hbm-off-command-state",
	"qcom,mdss-dsi-seed-0-command-state",
	"qcom,mdss-dsi-seed-1-command-state",
	"qcom,mdss-dsi-seed-2-command-state",
	"qcom,mdss-dsi-seed-3-command-state",
	"qcom,mdss-dsi-seed-4-command-state",
	"qcom,mdss-dsi-seed-0-switch-command-state",
	"qcom,mdss-dsi-seed-1-switch-command-state",
	"qcom,mdss-dsi-seed-2-switch-command-state",
	"qcom,mdss-dsi-seed-0-dc-switch-command-state",
	"qcom,mdss-dsi-seed-1-dc-switch-command-state",
	"qcom,mdss-dsi-seed-2-dc-switch-command-state",
	"qcom,mdss-dsi-seed-0-dc-command-state",
	"qcom,mdss-dsi-seed-1-dc-command-state",
	"qcom,mdss-dsi-seed-2-dc-command-state",
	"qcom,mdss-dsi-seed-off-command-state",
	"qcom,mdss-dsi-normal-hbm-on-command-state",
	"qcom,mdss-dsi-aod-high-mode-command-state",
	"qcom,mdss-dsi-aod-low-mode-command-state",
	"qcom,mdss-dsi-spr-0-command-state",
	"qcom,mdss-dsi-spr-1-command-state",
	"qcom,mdss-dsi-spr-2-command-state",
	"qcom,mdss-dsi-data-dimming-on-command-state",
	"qcom,mdss-dsi-data-dimming-off-command-state",
	"qcom,mdss-dsi-osc-clk-mode0-command-state",
	"qcom,mdss-dsi-osc-clk-mode1-command-state",
	"qcom,mdss-dsi-osc-clk-mode2-command-state",
	"qcom,mdss-dsi-osc-clk-mode3-command-state",
	"qcom,mdss-dsi-failsafe-on-command-state",
	"qcom,mdss-dsi-failsafe-off-command-state",
	"qcom,mdss-dsi-seed-enter-command-state",
	"qcom,mdss-dsi-seed-exit-command-state",
	"qcom,mdss-dsi-panel-id1-command-state",
	"qcom,mdss-dsi-panel-read-register-open-state",
	"qcom,mdss-dsi-panel-read-register-close-state",
	"qcom,mdss-dsi-mca-on-command-state",
	"qcom,mdss-dsi-mca-off-command-state",
	"qcom,mdss-dsi-loading-effect-1-command-state",
	"qcom,mdss-dsi-loading-effect-2-command-state",
	"qcom,mdss-dsi-loading-effect-off-command-state",
	"qcom,mdss-dsi-hbm-enter-switch-command-state",
	"qcom,mdss-dsi-hbm-enter1-switch-command-state",
	"qcom,mdss-dsi-hbm-enter2-switch-command-state",
	"qcom,mdss-dsi-hbm-exit-switch-command-state",
	"qcom,mdss-dsi-hbm-exit1-switch-command-state",
	"qcom,mdss-dsi-hbm-exit2-switch-command-state",
	"qcom,mdss-dsi-aor-restore-command-state",
	"qcom,mdss-dsi-lp1-pvt-command-state",
	"qcom,mdss-dsi-nolp-pvt-command-state",
	"qcom,mdss-dsi-aod-hbm-on-pvt-command-state",
	"qcom,mdss-dsi-aod-hbm-off-pvt-command-state",
	"qcom,mdss-dsi-dly-off-command-state",
	"qcom,mdss-dsi-panel-register-read-command-state",
	"qcom,mdss-dsi-panel-level2-key-enable-command-state",
	"qcom,mdss-dsi-panel-level2-key-disable-command-state",
	"qcom,mdss-dsi-fps-switch-command-state",
	"qcom,mdss-dsi-cabc-off-command-state",
	"qcom,mdss-dsi-cabc-1-command-state",
	"qcom,mdss-dsi-cabc-2-command-state",
	"qcom,mdss-dsi-cabc-3-command-state",
	"qcom,mdss-dsi-gamma-nomal-command-state",
	"qcom,mdss-dsi-gamma-lowbl-command-state",
/* add for optimizing the display effect under low backlight brightness */
	"qcom,mdss-dsi-panel-dimming-gamma-command-state",
	"qcom,mdss-dsi-fps60-command-state",
	"qcom,mdss-dsi-fps120-command-state",
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	"iris,abyp-panel-command-state",
#endif
#endif /*OPLUS_BUG_STABILITY*/

#ifdef OPLUS_FEATURE_ADFR
	"qcom,mdss-dsi-qsync-min-fps-0-command-state",
	"qcom,mdss-dsi-qsync-min-fps-1-command-state",
	"qcom,mdss-dsi-qsync-min-fps-2-command-state",
	"qcom,mdss-dsi-qsync-min-fps-3-command-state",
	"qcom,mdss-dsi-qsync-min-fps-4-command-state",
	"qcom,mdss-dsi-qsync-min-fps-5-command-state",
	"qcom,mdss-dsi-qsync-min-fps-6-command-state",
	"qcom,mdss-dsi-qsync-min-fps-7-command-state",
	"qcom,mdss-dsi-qsync-min-fps-8-command-state",
	"qcom,mdss-dsi-qsync-min-fps-9-command-state",
	"qcom,mdss-dsi-fakeframe-command-state",
	"qcom,mdss-dsi-adfr-pre-switch-command-state",
#endif
};

static int dsi_panel_get_cmd_pkt_count(const char *data, u32 length, u32 *cnt)
{
	const u32 cmd_set_min_size = 7;
	u32 count = 0;
	u32 packet_length;
	u32 tmp;

	while (length >= cmd_set_min_size) {
		packet_length = cmd_set_min_size;
		tmp = ((data[5] << 8) | (data[6]));
		packet_length += tmp;
		if (packet_length > length) {
			DSI_ERR("format error\n");
			return -EINVAL;
		}
		length -= packet_length;
		data += packet_length;
		count++;
	}

	*cnt = count;
	return 0;
}

static int dsi_panel_create_cmd_packets(const char *data,
					u32 length,
					u32 count,
					struct dsi_cmd_desc *cmd)
{
	int rc = 0;
	int i, j;
	u8 *payload;

	for (i = 0; i < count; i++) {
		u32 size;

		cmd[i].msg.type = data[0];
		cmd[i].last_command = (data[1] == 1);
		cmd[i].msg.channel = data[2];
		cmd[i].msg.flags |= data[3];
		cmd[i].msg.ctrl = 0;
		cmd[i].post_wait_ms = cmd[i].msg.wait_ms = data[4];
		cmd[i].msg.tx_len = ((data[5] << 8) | (data[6]));

		size = cmd[i].msg.tx_len * sizeof(u8);

		payload = kzalloc(size, GFP_KERNEL);
		if (!payload) {
			rc = -ENOMEM;
			goto error_free_payloads;
		}

		for (j = 0; j < cmd[i].msg.tx_len; j++)
			payload[j] = data[7 + j];

		cmd[i].msg.tx_buf = payload;
		data += (7 + cmd[i].msg.tx_len);
	}

	return rc;
error_free_payloads:
	for (i = i - 1; i >= 0; i--) {
		cmd--;
		kfree(cmd->msg.tx_buf);
	}

	return rc;
}

static void dsi_panel_destroy_cmd_packets(struct dsi_panel_cmd_set *set)
{
	u32 i = 0;
	struct dsi_cmd_desc *cmd;

	for (i = 0; i < set->count; i++) {
		cmd = &set->cmds[i];
		kfree(cmd->msg.tx_buf);
	}
}

static void dsi_panel_dealloc_cmd_packets(struct dsi_panel_cmd_set *set)
{
	kfree(set->cmds);
}

static int dsi_panel_alloc_cmd_packets(struct dsi_panel_cmd_set *cmd,
					u32 packet_count)
{
	u32 size;

	size = packet_count * sizeof(*cmd->cmds);
	cmd->cmds = kzalloc(size, GFP_KERNEL);
	if (!cmd->cmds)
		return -ENOMEM;

	cmd->count = packet_count;
	return 0;
}

static int dsi_panel_parse_cmd_sets_sub(struct dsi_panel_cmd_set *cmd,
					enum dsi_cmd_set_type type,
					struct dsi_parser_utils *utils)
{
	int rc = 0;
	u32 length = 0;
	const char *data;
	const char *state;
	u32 packet_count = 0;

	data = utils->get_property(utils->data, cmd_set_prop_map[type],
			&length);
	if (!data) {
		DSI_DEBUG("%s commands not defined\n", cmd_set_prop_map[type]);
		rc = -ENOTSUPP;
		goto error;
	}

	DSI_DEBUG("type=%d, name=%s, length=%d\n", type,
		cmd_set_prop_map[type], length);

	print_hex_dump_debug("", DUMP_PREFIX_NONE,
		       8, 1, data, length, false);

	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc) {
		DSI_ERR("commands failed, rc=%d\n", rc);
		goto error;
	}
	DSI_DEBUG("[%s] packet-count=%d, %d\n", cmd_set_prop_map[type],
		packet_count, length);

	rc = dsi_panel_alloc_cmd_packets(cmd, packet_count);
	if (rc) {
		DSI_ERR("failed to allocate cmd packets, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_create_cmd_packets(data, length, packet_count,
					  cmd->cmds);
	if (rc) {
		DSI_ERR("failed to create cmd packets, rc=%d\n", rc);
		goto error_free_mem;
	}

	state = utils->get_property(utils->data, cmd_set_state_map[type], NULL);
	if (!state || !strcmp(state, "dsi_lp_mode")) {
		cmd->state = DSI_CMD_SET_STATE_LP;
	} else if (!strcmp(state, "dsi_hs_mode")) {
		cmd->state = DSI_CMD_SET_STATE_HS;
	} else {
		DSI_ERR("[%s] command state unrecognized-%s\n",
		       cmd_set_state_map[type], state);
		goto error_free_mem;
	}

	return rc;
error_free_mem:
	kfree(cmd->cmds);
	cmd->cmds = NULL;
error:
	return rc;

}

static int dsi_panel_parse_cmd_sets(
		struct dsi_display_mode_priv_info *priv_info,
		struct dsi_parser_utils *utils)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set;
	u32 i;

	if (!priv_info) {
		DSI_ERR("invalid mode priv info\n");
		return -EINVAL;
	}

	for (i = DSI_CMD_SET_PRE_ON; i < DSI_CMD_SET_MAX; i++) {
		set = &priv_info->cmd_sets[i];
		set->type = i;
		set->count = 0;

		if (i == DSI_CMD_SET_PPS) {
			rc = dsi_panel_alloc_cmd_packets(set, 1);
			if (rc)
				DSI_ERR("failed to allocate cmd set %d, rc = %d\n",
					i, rc);
			set->state = DSI_CMD_SET_STATE_LP;
            #if defined(OPLUS_FEATURE_PXLW_IRIS5)
            if (iris_is_chip_supported())
                set->state = DSI_CMD_SET_STATE_HS;
            #endif
		} else {
			rc = dsi_panel_parse_cmd_sets_sub(set, i, utils);
			if (rc)
				DSI_DEBUG("failed to parse set %d\n", i);
		}
	}

	rc = 0;
	return rc;
}

static int dsi_panel_parse_reset_sequence(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_reset_seq *seq;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	arr = utils->get_property(utils->data,
			"qcom,mdss-dsi-reset-sequence", &length);
	if (!arr) {
		DSI_ERR("[%s] dsi-reset-sequence not found\n", panel->name);
		rc = -EINVAL;
		goto error;
	}
	if (length & 0x1) {
		DSI_ERR("[%s] syntax error for dsi-reset-sequence\n",
		       panel->name);
		rc = -EINVAL;
		goto error;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);

	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "qcom,mdss-dsi-reset-sequence",
					arr_32, length);
	if (rc) {
		DSI_ERR("[%s] cannot read dso-reset-seqience\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->reset_config.sequence = seq;
	panel->reset_config.count = count;

	for (i = 0; i < length; i += 2) {
		seq->level = arr_32[i];
		seq->sleep_ms = arr_32[i + 1];
		seq++;
	}


error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

static int dsi_panel_parse_misc_features(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;

	panel->ulps_feature_enabled =
		utils->read_bool(utils->data, "qcom,ulps-enabled");

	DSI_DEBUG("%s: ulps feature %s\n", __func__,
		(panel->ulps_feature_enabled ? "enabled" : "disabled"));

	panel->ulps_suspend_enabled =
		utils->read_bool(utils->data, "qcom,suspend-ulps-enabled");

	DSI_DEBUG("%s: ulps during suspend feature %s\n", __func__,
		(panel->ulps_suspend_enabled ? "enabled" : "disabled"));

	panel->te_using_watchdog_timer = utils->read_bool(utils->data,
					"qcom,mdss-dsi-te-using-wd");

	panel->sync_broadcast_en = utils->read_bool(utils->data,
			"qcom,cmd-sync-wait-broadcast");

	panel->lp11_init = utils->read_bool(utils->data,
			"qcom,mdss-dsi-lp11-init");

	panel->reset_gpio_always_on = utils->read_bool(utils->data,
			"qcom,platform-reset-gpio-always-on");

	#ifdef OPLUS_BUG_STABILITY
	panel->nt36523w_ktz8866 = utils->read_bool(utils->data,
			"qcom,nt36523w-ktz8866");
	/* A tablet Pad, modify mipi */
	mipi_c_phy_oslo_flag = panel->nt36523w_ktz8866;
	#endif

	return 0;
}

static int dsi_panel_parse_jitter_config(
				struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	u32 jitter[DEFAULT_PANEL_JITTER_ARRAY_SIZE] = {0, 0};
	u64 jitter_val = 0;

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "qcom,mdss-dsi-panel-jitter",
				jitter, DEFAULT_PANEL_JITTER_ARRAY_SIZE);
	if (rc) {
		DSI_DEBUG("panel jitter not defined rc=%d\n", rc);
	} else {
		jitter_val = jitter[0];
		jitter_val = div_u64(jitter_val, jitter[1]);
	}

	if (rc || !jitter_val || (jitter_val > MAX_PANEL_JITTER)) {
		priv_info->panel_jitter_numer = DEFAULT_PANEL_JITTER_NUMERATOR;
		priv_info->panel_jitter_denom =
					DEFAULT_PANEL_JITTER_DENOMINATOR;
	} else {
		priv_info->panel_jitter_numer = jitter[0];
		priv_info->panel_jitter_denom = jitter[1];
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-panel-prefill-lines",
				  &priv_info->panel_prefill_lines);
	if (rc) {
		DSI_DEBUG("panel prefill lines are not defined rc=%d\n", rc);
		priv_info->panel_prefill_lines = mode->timing.v_back_porch +
			mode->timing.v_sync_width + mode->timing.v_front_porch;
	} else if (priv_info->panel_prefill_lines >=
					DSI_V_TOTAL(&mode->timing)) {
		DSI_DEBUG("invalid prefill lines config=%d setting to:%d\n",
		priv_info->panel_prefill_lines, DEFAULT_PANEL_PREFILL_LINES);

		priv_info->panel_prefill_lines = DEFAULT_PANEL_PREFILL_LINES;
	}

	return 0;
}

#ifdef OPLUS_BUG_STABILITY
__attribute__((weak)) int dsi_panel_parse_panel_power_cfg(struct dsi_panel *panel)
{
	return 0;
}
#endif /* OPLUS_BUG_STABILITY */

static int dsi_panel_parse_power_cfg(struct dsi_panel *panel)
{
	int rc = 0;
	char *supply_name;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	if (!strcmp(panel->type, "primary"))
		supply_name = "qcom,panel-supply-entries";
	else
		supply_name = "qcom,panel-sec-supply-entries";

	rc = dsi_pwr_of_get_vreg_data(&panel->utils,
			&panel->power_info, supply_name);
	if (rc) {
		DSI_ERR("[%s] failed to parse vregs\n", panel->name);
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_gpios(struct dsi_panel *panel)
{
	int rc = 0;
	const char *data;
	struct dsi_parser_utils *utils = &panel->utils;
	char *reset_gpio_name, *mode_set_gpio_name;
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	bool is_primary = false;
#endif

	if (!strcmp(panel->type, "primary")) {
		reset_gpio_name = "qcom,platform-reset-gpio";
		mode_set_gpio_name = "qcom,panel-mode-gpio";
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported())
		is_primary = true;
#endif
	} else {
		reset_gpio_name = "qcom,platform-sec-reset-gpio";
		mode_set_gpio_name = "qcom,panel-sec-mode-gpio";
	}

	panel->reset_config.reset_gpio = utils->get_named_gpio(utils->data,
					      reset_gpio_name, 0);
	if (!gpio_is_valid(panel->reset_config.reset_gpio) &&
		!panel->host_config.ext_bridge_mode) {
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
		if (iris_is_chip_supported()) {
			if (is_primary) {
				rc = panel->reset_config.reset_gpio;
                DSI_ERR("[%s] failed get primary reset gpio, rc=%d\n", panel->name, rc);
				goto error;
			}
		} else {
#endif
		    rc = panel->reset_config.reset_gpio;
		    DSI_ERR("[%s] failed get reset gpio, rc=%d\n", panel->name, rc);
		    goto error;
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
       }
#endif
	}

#ifdef OPLUS_BUG_STABILITY
	panel->reset_config.panel_vout_gpio = utils->get_named_gpio(utils->data,
					      "qcom,platform-panel-vout-gpio", 0);

	if (!gpio_is_valid(panel->reset_config.panel_vout_gpio)) {
		DSI_DEBUG("[%s] failed get panel_vout_gpio, rc=%d\n", panel->name, rc);
	}

	panel->reset_config.panel_te_esd_gpio = utils->get_named_gpio(utils->data,
		                  "qcom,platform-panel-te-esd-gpio", 0);

	if (!gpio_is_valid(panel->reset_config.panel_te_esd_gpio)) {
		DSI_DEBUG("[%s:%d] platform-panel-te-esd-gpio", __func__, __LINE__);
	}
	panel->reset_config.panel_vddr_aod_en_gpio = utils->get_named_gpio(utils->data,
					      "qcom,platform-panel-vddr-aod-en-gpio", 0);

	if (!gpio_is_valid(panel->reset_config.panel_vddr_aod_en_gpio)) {
		DSI_ERR("[%s] failed get panel_vddr_aod_en_gpio, rc=%d\n", panel->name, rc);
	}
	panel->vddr_gpio = utils->get_named_gpio(utils->data, "qcom,vddr-gpio", 0);
	if (!gpio_is_valid(panel->vddr_gpio)) {
		DSI_DEBUG("[%s] vddr-gpio is not set, rc=%d\n",
			 panel->name, rc);
	}

	panel->reset_config.tp_cs_gpio = utils->get_named_gpio(utils->data,
					      "qcom,platform-tp-cs-gpio", 0);
	if (!gpio_is_valid(panel->reset_config.tp_cs_gpio)) {
		DSI_ERR("[%s] failed get qcom,platform-tp-cs-gpio, rc=%d\n", panel->name, rc);
	}
#endif

	panel->reset_config.disp_en_gpio = utils->get_named_gpio(utils->data,
						"qcom,5v-boost-gpio",
						0);
	if (!gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		DSI_DEBUG("[%s] 5v-boot-gpio is not set, rc=%d\n",
			 panel->name, rc);
		panel->reset_config.disp_en_gpio =
				utils->get_named_gpio(utils->data,
					"qcom,platform-en-gpio", 0);
		if (!gpio_is_valid(panel->reset_config.disp_en_gpio)) {
			DSI_DEBUG("[%s] platform-en-gpio is not set, rc=%d\n",
				 panel->name, rc);
		}
	}

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		panel->vsync_switch_gpio = utils->get_named_gpio(utils->data, "qcom,vsync-switch-gpio", 0);
		if (!gpio_is_valid(panel->vsync_switch_gpio)) {
			DSI_DEBUG("[%s] vsync_switch_gpio is not set, rc=%d\n", panel->name, rc);
		}
	}
#endif /*OPLUS_FEATURE_ADFR*/

	panel->reset_config.lcd_mode_sel_gpio = utils->get_named_gpio(
		utils->data, mode_set_gpio_name, 0);
	if (!gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		DSI_DEBUG("mode gpio not specified\n");

	DSI_DEBUG("mode gpio=%d\n", panel->reset_config.lcd_mode_sel_gpio);

	data = utils->get_property(utils->data,
		"qcom,mdss-dsi-mode-sel-gpio-state", NULL);
	if (data) {
		if (!strcmp(data, "single_port"))
			panel->reset_config.mode_sel_state =
				MODE_SEL_SINGLE_PORT;
		else if (!strcmp(data, "dual_port"))
			panel->reset_config.mode_sel_state =
				MODE_SEL_DUAL_PORT;
		else if (!strcmp(data, "high"))
			panel->reset_config.mode_sel_state =
				MODE_GPIO_HIGH;
		else if (!strcmp(data, "low"))
			panel->reset_config.mode_sel_state =
				MODE_GPIO_LOW;
	} else {
		/* Set default mode as SPLIT mode */
		panel->reset_config.mode_sel_state = MODE_SEL_DUAL_PORT;
	}

	/* TODO:  release memory */
	rc = dsi_panel_parse_reset_sequence(panel);
	if (rc) {
		DSI_ERR("[%s] failed to parse reset sequence, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	panel->panel_test_gpio = utils->get_named_gpio(utils->data,
					"qcom,mdss-dsi-panel-test-pin",
					0);
	if (!gpio_is_valid(panel->panel_test_gpio))
		DSI_DEBUG("%s:%d panel test gpio not specified\n", __func__,
			 __LINE__);

error:
	return rc;
}

static int dsi_panel_parse_bl_pwm_config(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val;
	struct dsi_backlight_config *config = &panel->bl_config;
	struct dsi_parser_utils *utils = &panel->utils;

	rc = utils->read_u32(utils->data, "qcom,bl-pmic-pwm-period-usecs",
				  &val);
	if (rc) {
		DSI_ERR("bl-pmic-pwm-period-usecs is not defined, rc=%d\n", rc);
		goto error;
	}
	config->pwm_period_usecs = val;

error:
	return rc;
}

static int dsi_panel_parse_bl_config(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val = 0;
	const char *bl_type;
	const char *data;
	struct dsi_parser_utils *utils = &panel->utils;
	char *bl_name;

	if (!strcmp(panel->type, "primary"))
		bl_name = "qcom,mdss-dsi-bl-pmic-control-type";
	else
		bl_name = "qcom,mdss-dsi-sec-bl-pmic-control-type";

	bl_type = utils->get_property(utils->data, bl_name, NULL);
	if (!bl_type) {
		panel->bl_config.type = DSI_BACKLIGHT_UNKNOWN;
	} else if (!strcmp(bl_type, "bl_ctrl_pwm")) {
		panel->bl_config.type = DSI_BACKLIGHT_PWM;
	} else if (!strcmp(bl_type, "bl_ctrl_wled")) {
		panel->bl_config.type = DSI_BACKLIGHT_WLED;
	} else if (!strcmp(bl_type, "bl_ctrl_dcs")) {
		panel->bl_config.type = DSI_BACKLIGHT_DCS;
	} else if (!strcmp(bl_type, "bl_ctrl_external")) {
		panel->bl_config.type = DSI_BACKLIGHT_EXTERNAL;
	} else {
		DSI_DEBUG("[%s] bl-pmic-control-type unknown-%s\n",
			 panel->name, bl_type);
		panel->bl_config.type = DSI_BACKLIGHT_UNKNOWN;
	}

	data = utils->get_property(utils->data, "qcom,bl-update-flag", NULL);
	if (!data) {
		panel->bl_config.bl_update = BL_UPDATE_NONE;
	} else if (!strcmp(data, "delay_until_first_frame")) {
		panel->bl_config.bl_update = BL_UPDATE_DELAY_UNTIL_FIRST_FRAME;
	} else {
		DSI_DEBUG("[%s] No valid bl-update-flag: %s\n",
						panel->name, data);
		panel->bl_config.bl_update = BL_UPDATE_NONE;
	}

	panel->bl_config.bl_scale = MAX_BL_SCALE_LEVEL;
	panel->bl_config.bl_scale_sv = MAX_SV_BL_SCALE_LEVEL;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-min-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] bl-min-level unspecified, defaulting to zero\n",
			 panel->name);
		panel->bl_config.bl_min_level = 0;
	} else {
		panel->bl_config.bl_min_level = val;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-max-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] bl-max-level unspecified, defaulting to max level\n",
			 panel->name);
		panel->bl_config.bl_max_level = MAX_BL_LEVEL;
	} else {
		panel->bl_config.bl_max_level = backlight_max;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-brightness-max-level",
		&val);
	if (rc) {
		DSI_DEBUG("[%s] brigheness-max-level unspecified, defaulting to 255\n",
			 panel->name);
		panel->bl_config.brightness_max_level = 255;
	} else {
		panel->bl_config.brightness_max_level = backlight_max;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-ctrl-dcs-subtype",
		&val);
	if (rc) {
		DSI_DEBUG("[%s] bl-ctrl-dcs-subtype, defautling to zero\n",
			panel->name);
		panel->bl_config.bl_dcs_subtype = 0;
	} else {
		panel->bl_config.bl_dcs_subtype = val;
	}

	panel->bl_config.bl_inverted_dbv = utils->read_bool(utils->data,
		"qcom,mdss-dsi-bl-inverted-dbv");

#ifdef OPLUS_BUG_STABILITY
	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-normal-max-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] bl-max-level unspecified, defaulting to max level\n",
			 panel->name);
		panel->bl_config.bl_normal_max_level = 1023;
	} else {
		panel->bl_config.bl_normal_max_level = val;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-brightness-normal-max-level",
		&val);
	if (rc) {
		DSI_DEBUG("[%s] brigheness-max-level unspecified, defaulting to 1023\n",
			 panel->name);
		panel->bl_config.brightness_normal_max_level = 1023;
	} else {
		panel->bl_config.brightness_normal_max_level = val;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-brightness-default-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] brightness-default-level unspecified, defaulting normal max\n",
			 panel->name);
		panel->bl_config.brightness_default_level = panel->bl_config.brightness_max_level;
	} else {
		panel->bl_config.brightness_default_level = val;
	}
	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-dc-backlight-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] dc backlight unspecified, defaulting to default level 260\n",
			 panel->name);
		oplus_dimlayer_bl_alpha_v2 = 260;
	} else {
		oplus_dimlayer_bl_alpha_v2 = val;
	}
#endif

	if (panel->bl_config.type == DSI_BACKLIGHT_PWM) {
		rc = dsi_panel_parse_bl_pwm_config(panel);
		if (rc) {
			DSI_ERR("[%s] failed to parse pwm config, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	panel->bl_config.en_gpio = utils->get_named_gpio(utils->data,
					      "qcom,platform-bklight-en-gpio",
					      0);
	if (!gpio_is_valid(panel->bl_config.en_gpio)) {
		if (panel->bl_config.en_gpio == -EPROBE_DEFER) {
			DSI_DEBUG("[%s] failed to get bklt gpio, rc=%d\n",
					panel->name, rc);
			rc = -EPROBE_DEFER;
			goto error;
		} else {
			DSI_DEBUG("[%s] failed to get bklt gpio, rc=%d\n",
					 panel->name, rc);
			rc = 0;
			goto error;
		}
	}

error:
	return rc;
}

void dsi_dsc_pclk_param_calc(struct msm_display_dsc_info *dsc, int intf_width)
{
	int slice_per_pkt, slice_per_intf;
	int bytes_in_slice, total_bytes_per_intf;

	if (!dsc || !dsc->slice_width || !dsc->slice_per_pkt ||
	    (intf_width < dsc->slice_width)) {
		DSI_ERR("invalid input, intf_width=%d slice_width=%d\n",
			intf_width, dsc ? dsc->slice_width : -1);
		return;
	}

	slice_per_pkt = dsc->slice_per_pkt;
	slice_per_intf = DIV_ROUND_UP(intf_width, dsc->slice_width);

	/*
	 * If slice_per_pkt is greater than slice_per_intf then default to 1.
	 * This can happen during partial update.
	 */
	if (slice_per_pkt > slice_per_intf)
		slice_per_pkt = 1;

	bytes_in_slice = DIV_ROUND_UP(dsc->slice_width * dsc->bpp, 8);
	total_bytes_per_intf = bytes_in_slice * slice_per_intf;

	dsc->eol_byte_num = total_bytes_per_intf % 3;
	dsc->pclk_per_line =  DIV_ROUND_UP(total_bytes_per_intf, 3);
	dsc->bytes_in_slice = bytes_in_slice;
	dsc->bytes_per_pkt = bytes_in_slice * slice_per_pkt;
	dsc->pkt_per_line = slice_per_intf / slice_per_pkt;
}


int dsi_dsc_populate_static_param(struct msm_display_dsc_info *dsc)
{
	int bpp, bpc;
	int mux_words_size;
	int groups_per_line, groups_total;
	int min_rate_buffer_size;
	int hrd_delay;
	int pre_num_extra_mux_bits, num_extra_mux_bits;
	int slice_bits;
	int data;
	int final_value, final_scale;
	int ratio_index, mod_offset;

	dsc->rc_model_size = 8192;

	if (dsc->version == 0x11 && dsc->scr_rev == 0x1)
		dsc->first_line_bpg_offset = 15;
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	else if(iris_is_chip_supported() && (dsc->bpc == 10) && (dsc->bpp == 10))
		dsc->first_line_bpg_offset = 9;
#endif
	else
		dsc->first_line_bpg_offset = 12;

	dsc->edge_factor = 6;
	dsc->tgt_offset_hi = 3;
	dsc->tgt_offset_lo = 3;
	dsc->enable_422 = 0;
	dsc->convert_rgb = 1;
	dsc->vbr_enable = 0;

	dsc->buf_thresh = dsi_dsc_rc_buf_thresh;

	bpp = dsc->bpp;
	bpc = dsc->bpc;

	if ((bpc == 12) && (bpp == 8))
		ratio_index = DSC_12BPC_8BPP;
	else if ((bpc == 10) && (bpp == 8))
		ratio_index = DSC_10BPC_8BPP;
	else if ((bpc == 10) && (bpp == 10))
		ratio_index = DSC_10BPC_10BPP;
	else
		ratio_index = DSC_8BPC_8BPP;

	if (dsc->version == 0x11 && dsc->scr_rev == 0x1) {
		dsc->range_min_qp =
			dsi_dsc_rc_range_min_qp_1_1_scr1[ratio_index];
		dsc->range_max_qp =
			dsi_dsc_rc_range_max_qp_1_1_scr1[ratio_index];
	} else {
		dsc->range_min_qp = dsi_dsc_rc_range_min_qp_1_1[ratio_index];
		dsc->range_max_qp = dsi_dsc_rc_range_max_qp_1_1[ratio_index];
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
        if (iris_is_chip_supported() && ratio_index == DSC_10BPC_10BPP)
            dsc->range_max_qp = dsi_dsc_rc_range_max_qp_1_1_pxlw;
#endif
	}
	dsc->range_bpg_offset = dsi_dsc_rc_range_bpg_offset;

	if (bpp == 8) {
		dsc->initial_offset = 6144;
		dsc->initial_xmit_delay = 512;
	} else if (bpp == 10) {
		dsc->initial_offset = 5632;
		dsc->initial_xmit_delay = 410;
	} else {
		dsc->initial_offset = 2048;
		dsc->initial_xmit_delay = 341;
	}

	dsc->line_buf_depth = bpc + 1;

	if (bpc == 8) {
		dsc->input_10_bits = 0;
		dsc->min_qp_flatness = 3;
		dsc->max_qp_flatness = 12;
		dsc->quant_incr_limit0 = 11;
		dsc->quant_incr_limit1 = 11;
		mux_words_size = 48;
	} else if (bpc == 10) { /* 10bpc */
		dsc->input_10_bits = 1;
		dsc->min_qp_flatness = 7;
		dsc->max_qp_flatness = 16;
		dsc->quant_incr_limit0 = 15;
		dsc->quant_incr_limit1 = 15;
		mux_words_size = 48;
	} else { /* 12 bpc */
		dsc->input_10_bits = 0;
		dsc->min_qp_flatness = 11;
		dsc->max_qp_flatness = 20;
		dsc->quant_incr_limit0 = 19;
		dsc->quant_incr_limit1 = 19;
		mux_words_size = 64;
	}

	mod_offset = dsc->slice_width % 3;
	switch (mod_offset) {
	case 0:
		dsc->slice_last_group_size = 2;
		break;
	case 1:
		dsc->slice_last_group_size = 0;
		break;
	case 2:
		dsc->slice_last_group_size = 1;
		break;
	default:
		break;
	}

	dsc->det_thresh_flatness = 2 << (bpc - 8);

	groups_per_line = DIV_ROUND_UP(dsc->slice_width, 3);

	dsc->chunk_size = dsc->slice_width * bpp / 8;
	if ((dsc->slice_width * bpp) % 8)
		dsc->chunk_size++;

	/* rbs-min */
	min_rate_buffer_size =  dsc->rc_model_size - dsc->initial_offset +
			dsc->initial_xmit_delay * bpp +
			groups_per_line * dsc->first_line_bpg_offset;

	hrd_delay = DIV_ROUND_UP(min_rate_buffer_size, bpp);

	dsc->initial_dec_delay = hrd_delay - dsc->initial_xmit_delay;

	dsc->initial_scale_value = 8 * dsc->rc_model_size /
			(dsc->rc_model_size - dsc->initial_offset);

	slice_bits = 8 * dsc->chunk_size * dsc->slice_height;

	groups_total = groups_per_line * dsc->slice_height;

	data = dsc->first_line_bpg_offset * 2048;

	dsc->nfl_bpg_offset = DIV_ROUND_UP(data, (dsc->slice_height - 1));

	pre_num_extra_mux_bits = 3 * (mux_words_size + (4 * bpc + 4) - 2);

	num_extra_mux_bits = pre_num_extra_mux_bits - (mux_words_size -
		((slice_bits - pre_num_extra_mux_bits) % mux_words_size));

	data = 2048 * (dsc->rc_model_size - dsc->initial_offset
		+ num_extra_mux_bits);
	dsc->slice_bpg_offset = DIV_ROUND_UP(data, groups_total);

	data = dsc->initial_xmit_delay * bpp;
	final_value =  dsc->rc_model_size - data + num_extra_mux_bits;

	final_scale = 8 * dsc->rc_model_size /
		(dsc->rc_model_size - final_value);

	dsc->final_offset = final_value;

	data = (final_scale - 9) * (dsc->nfl_bpg_offset +
		dsc->slice_bpg_offset);
	dsc->scale_increment_interval = (2048 * dsc->final_offset) / data;

	dsc->scale_decrement_interval = groups_per_line /
		(dsc->initial_scale_value - 8);

	return 0;
}


static int dsi_panel_parse_phy_timing(struct dsi_display_mode *mode,
		struct dsi_parser_utils *utils)
{
	const char *data;
	u32 len, i;
	int rc = 0;
	struct dsi_display_mode_priv_info *priv_info;
	u64 pixel_clk_khz;

	if (!mode || !mode->priv_info)
		return -EINVAL;

	priv_info = mode->priv_info;

	data = utils->get_property(utils->data,
			"qcom,mdss-dsi-panel-phy-timings", &len);
	if (!data) {
		DSI_DEBUG("Unable to read Phy timing settings\n");
	} else {
		priv_info->phy_timing_val =
			kzalloc((sizeof(u32) * len), GFP_KERNEL);
		if (!priv_info->phy_timing_val)
			return -EINVAL;

		for (i = 0; i < len; i++)
			priv_info->phy_timing_val[i] = data[i];

		priv_info->phy_timing_len = len;
	}

	if (mode->panel_mode == DSI_OP_VIDEO_MODE) {
		/*
		 *  For command mode we update the pclk as part of
		 *  function dsi_panel_calc_dsi_transfer_time( )
		 *  as we set it based on dsi clock or mdp transfer time.
		 */
		pixel_clk_khz = (DSI_H_TOTAL_DSC(&mode->timing) *
				DSI_V_TOTAL(&mode->timing) *
				mode->timing.refresh_rate);
		do_div(pixel_clk_khz, 1000);
		mode->pixel_clk_khz = pixel_clk_khz;
	}

	return rc;
}

static int dsi_panel_parse_dsc_params(struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	u32 data;
	int rc = -EINVAL;
	int intf_width;
	const char *compression;
	struct dsi_display_mode_priv_info *priv_info;

	if (!mode || !mode->priv_info)
		return -EINVAL;

	priv_info = mode->priv_info;

	priv_info->dsc_enabled = false;
	compression = utils->get_property(utils->data,
			"qcom,compression-mode", NULL);
	if (compression && !strcmp(compression, "dsc"))
		priv_info->dsc_enabled = true;

	if (!priv_info->dsc_enabled) {
		DSI_DEBUG("dsc compression is not enabled for the mode\n");
		return 0;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-version", &data);
	if (rc) {
		priv_info->dsc.version = 0x11;
		rc = 0;
	} else {
		priv_info->dsc.version = data & 0xff;
		/* only support DSC 1.1 rev */
		if (priv_info->dsc.version != 0x11) {
			DSI_ERR("%s: DSC version:%d not supported\n", __func__,
					priv_info->dsc.version);
			rc = -EINVAL;
			goto error;
		}
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-scr-version", &data);
	if (rc) {
		priv_info->dsc.scr_rev = 0x0;
		rc = 0;
	} else {
		priv_info->dsc.scr_rev = data & 0xff;
		/* only one scr rev supported */
		if (priv_info->dsc.scr_rev > 0x1) {
			DSI_ERR("%s: DSC scr version:%d not supported\n",
					__func__, priv_info->dsc.scr_rev);
			rc = -EINVAL;
			goto error;
		}
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-slice-height", &data);
	if (rc) {
		DSI_ERR("failed to parse qcom,mdss-dsc-slice-height\n");
		goto error;
	}
	priv_info->dsc.slice_height = data;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-slice-width", &data);
	if (rc) {
		DSI_ERR("failed to parse qcom,mdss-dsc-slice-width\n");
		goto error;
	}
	priv_info->dsc.slice_width = data;

	intf_width = mode->timing.h_active;
	if (intf_width % priv_info->dsc.slice_width) {
		DSI_ERR("invalid slice width for the intf width:%d slice width:%d\n",
			intf_width, priv_info->dsc.slice_width);
		rc = -EINVAL;
		goto error;
	}

	priv_info->dsc.pic_width = mode->timing.h_active;
	priv_info->dsc.pic_height = mode->timing.v_active;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-slice-per-pkt", &data);
	if (rc) {
		DSI_ERR("failed to parse qcom,mdss-dsc-slice-per-pkt\n");
		goto error;
	} else if (!data || (data > 2)) {
		DSI_ERR("invalid dsc slice-per-pkt:%d\n", data);
		goto error;
	}
	priv_info->dsc.slice_per_pkt = data;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-bit-per-component",
		&data);
	if (rc) {
		DSI_ERR("failed to parse qcom,mdss-dsc-bit-per-component\n");
		goto error;
	}
	priv_info->dsc.bpc = data;

	rc = utils->read_u32(utils->data, "qcom,mdss-pps-delay-ms", &data);
	if (rc) {
		DSI_DEBUG("pps-delay-ms not specified, defaulting to 0\n");
		data = 0;
	}
	priv_info->dsc.pps_delay_ms = data;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsc-bit-per-pixel",
			&data);
	if (rc) {
		DSI_ERR("failed to parse qcom,mdss-dsc-bit-per-pixel\n");
		goto error;
	}
	priv_info->dsc.bpp = data;

	priv_info->dsc.block_pred_enable = utils->read_bool(utils->data,
		"qcom,mdss-dsc-block-prediction-enable");

	priv_info->dsc.full_frame_slices = DIV_ROUND_UP(intf_width,
		priv_info->dsc.slice_width);

	dsi_dsc_populate_static_param(&priv_info->dsc);
	dsi_dsc_pclk_param_calc(&priv_info->dsc, intf_width);

	mode->timing.dsc_enabled = true;
	mode->timing.dsc = &priv_info->dsc;

error:
	return rc;
}

static int dsi_panel_parse_hdr_config(struct dsi_panel *panel)
{
	int rc = 0;
	struct drm_panel_hdr_properties *hdr_prop;
	struct dsi_parser_utils *utils = &panel->utils;

	hdr_prop = &panel->hdr_props;
	hdr_prop->hdr_enabled = utils->read_bool(utils->data,
		"qcom,mdss-dsi-panel-hdr-enabled");

	if (hdr_prop->hdr_enabled) {
		rc = utils->read_u32_array(utils->data,
				"qcom,mdss-dsi-panel-hdr-color-primaries",
				hdr_prop->display_primaries,
				DISPLAY_PRIMARIES_MAX);
		if (rc) {
			DSI_ERR("%s:%d, Unable to read color primaries,rc:%u\n",
					__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}

		rc = utils->read_u32(utils->data,
			"qcom,mdss-dsi-panel-peak-brightness",
			&(hdr_prop->peak_brightness));
		if (rc) {
			DSI_ERR("%s:%d, Unable to read hdr brightness, rc:%u\n",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}

		rc = utils->read_u32(utils->data,
			"qcom,mdss-dsi-panel-blackness-level",
			&(hdr_prop->blackness_level));
		if (rc) {
			DSI_ERR("%s:%d, Unable to read hdr brightness, rc:%u\n",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}
	}
	return 0;
}

static int dsi_panel_parse_topology(
		struct dsi_display_mode_priv_info *priv_info,
		struct dsi_parser_utils *utils,
		int topology_override)
{
	struct msm_display_topology *topology;
	u32 top_count, top_sel, *array = NULL;
	int i, len = 0;
	int rc = -EINVAL;

	len = utils->count_u32_elems(utils->data, "qcom,display-topology");
	if (len <= 0 || len % TOPOLOGY_SET_LEN ||
			len > (TOPOLOGY_SET_LEN * MAX_TOPOLOGY)) {
		DSI_ERR("invalid topology list for the panel, rc = %d\n", rc);
		return rc;
	}

	top_count = len / TOPOLOGY_SET_LEN;

	array = kcalloc(len, sizeof(u32), GFP_KERNEL);
	if (!array)
		return -ENOMEM;

	rc = utils->read_u32_array(utils->data,
			"qcom,display-topology", array, len);
	if (rc) {
		DSI_ERR("unable to read the display topologies, rc = %d\n", rc);
		goto read_fail;
	}

	topology = kcalloc(top_count, sizeof(*topology), GFP_KERNEL);
	if (!topology) {
		rc = -ENOMEM;
		goto read_fail;
	}

	for (i = 0; i < top_count; i++) {
		struct msm_display_topology *top = &topology[i];

		top->num_lm = array[i * TOPOLOGY_SET_LEN];
		top->num_enc = array[i * TOPOLOGY_SET_LEN + 1];
		top->num_intf = array[i * TOPOLOGY_SET_LEN + 2];
	}

	if (topology_override >= 0 && topology_override < top_count) {
		DSI_INFO("override topology: cfg:%d lm:%d comp_enc:%d intf:%d\n",
			topology_override,
			topology[topology_override].num_lm,
			topology[topology_override].num_enc,
			topology[topology_override].num_intf);
		top_sel = topology_override;
		goto parse_done;
	}

	rc = utils->read_u32(utils->data,
			"qcom,default-topology-index", &top_sel);
	if (rc) {
		DSI_ERR("no default topology selected, rc = %d\n", rc);
		goto parse_fail;
	}

	if (top_sel >= top_count) {
		rc = -EINVAL;
		DSI_ERR("default topology is specified is not valid, rc = %d\n",
			rc);
		goto parse_fail;
	}

	DSI_INFO("default topology: lm: %d comp_enc:%d intf: %d\n",
		topology[top_sel].num_lm,
		topology[top_sel].num_enc,
		topology[top_sel].num_intf);

parse_done:
	memcpy(&priv_info->topology, &topology[top_sel],
		sizeof(struct msm_display_topology));
parse_fail:
	kfree(topology);
read_fail:
	kfree(array);

	return rc;
}

static int dsi_panel_parse_roi_alignment(struct dsi_parser_utils *utils,
					 struct msm_roi_alignment *align)
{
	int len = 0, rc = 0;
	u32 value[6];
	struct property *data;

	if (!align)
		return -EINVAL;

	memset(align, 0, sizeof(*align));

	data = utils->find_property(utils->data,
			"qcom,panel-roi-alignment", &len);
	len /= sizeof(u32);
	if (!data) {
		DSI_ERR("panel roi alignment not found\n");
		rc = -EINVAL;
	} else if (len != 6) {
		DSI_ERR("incorrect roi alignment len %d\n", len);
		rc = -EINVAL;
	} else {
		rc = utils->read_u32_array(utils->data,
				"qcom,panel-roi-alignment", value, len);
		if (rc)
			DSI_DEBUG("error reading panel roi alignment values\n");
		else {
			align->xstart_pix_align = value[0];
			align->ystart_pix_align = value[1];
			align->width_pix_align = value[2];
			align->height_pix_align = value[3];
			align->min_width = value[4];
			align->min_height = value[5];
		}

		DSI_INFO("roi alignment: [%d, %d, %d, %d, %d, %d]\n",
			align->xstart_pix_align,
			align->width_pix_align,
			align->ystart_pix_align,
			align->height_pix_align,
			align->min_width,
			align->min_height);
	}

	return rc;
}

static int dsi_panel_parse_partial_update_caps(struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	struct msm_roi_caps *roi_caps = NULL;
	const char *data;
	int rc = 0;

	if (!mode || !mode->priv_info) {
		DSI_ERR("invalid arguments\n");
		return -EINVAL;
	}

	roi_caps = &mode->priv_info->roi_caps;

	memset(roi_caps, 0, sizeof(*roi_caps));

	data = utils->get_property(utils->data,
		"qcom,partial-update-enabled", NULL);
	if (data) {
		if (!strcmp(data, "dual_roi"))
			roi_caps->num_roi = 2;
		else if (!strcmp(data, "single_roi"))
			roi_caps->num_roi = 1;
		else {
			DSI_INFO(
			"invalid value for qcom,partial-update-enabled: %s\n",
			data);
			return 0;
		}
	} else {
		DSI_DEBUG("partial update disabled as the property is not set\n");
		return 0;
	}

	roi_caps->merge_rois = utils->read_bool(utils->data,
			"qcom,partial-update-roi-merge");

	roi_caps->enabled = roi_caps->num_roi > 0;

	DSI_DEBUG("partial update num_rois=%d enabled=%d\n", roi_caps->num_roi,
			roi_caps->enabled);

	if (roi_caps->enabled)
		rc = dsi_panel_parse_roi_alignment(utils,
				&roi_caps->align);

	if (rc)
		memset(roi_caps, 0, sizeof(*roi_caps));

	return rc;
}

static int dsi_panel_parse_panel_mode_caps(struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	bool vid_mode_support, cmd_mode_support;

	if (!mode || !mode->priv_info) {
		DSI_ERR("invalid arguments\n");
		return -EINVAL;
	}

	vid_mode_support = utils->read_bool(utils->data,
				"qcom,mdss-dsi-video-mode");

	cmd_mode_support = utils->read_bool(utils->data,
				"qcom,mdss-dsi-cmd-mode");

	if (cmd_mode_support)
		mode->panel_mode = DSI_OP_CMD_MODE;
	else if (vid_mode_support)
		mode->panel_mode = DSI_OP_VIDEO_MODE;
	else
		return -EINVAL;

	return 0;
};


static int dsi_panel_parse_dms_info(struct dsi_panel *panel)
{
	int dms_enabled;
	const char *data;
	struct dsi_parser_utils *utils = &panel->utils;

	panel->dms_mode = DSI_DMS_MODE_DISABLED;
	dms_enabled = utils->read_bool(utils->data,
		"qcom,dynamic-mode-switch-enabled");
	if (!dms_enabled)
		return 0;

	data = utils->get_property(utils->data,
			"qcom,dynamic-mode-switch-type", NULL);
	if (data && !strcmp(data, "dynamic-resolution-switch-immediate")) {
		panel->dms_mode = DSI_DMS_MODE_RES_SWITCH_IMMEDIATE;
	} else {
		DSI_ERR("[%s] unsupported dynamic switch mode: %s\n",
							panel->name, data);
		return -EINVAL;
	}

	return 0;
};

#ifdef OPLUS_BUG_STABILITY
int dsi_panel_fps60_cmd_set(struct dsi_panel *panel)
{
       int rc = 0;
       if (!panel) {
               DSI_ERR("Invalid params\n");
               return -EINVAL;
       }
       mutex_lock(&panel->panel_lock);
       rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_FPS60);
       if (rc)
           DSI_ERR("fps [%s] failed to send DSI_CMD_SET_FPS60 cmds, rc=%d\n",
                   panel->name, rc);
       mutex_unlock(&panel->panel_lock);
       return rc;
}
int dsi_panel_fps120_cmd_set(struct dsi_panel *panel)
{

       int rc = 0;
       if (!panel) {
               DSI_ERR("Invalid params\n");
               return -EINVAL;
       }
       mutex_lock(&panel->panel_lock);
       rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_FPS120);
       if (rc)
           DSI_ERR("fps [%s] failed to send DSI_CMD_SET_FPS120 cmds, rc=%d\n",
                   panel->name, rc);
       mutex_unlock(&panel->panel_lock);
       return rc;
}
#endif /*OPLUS_BUG_STABILITY*/

/*
 * The length of all the valid values to be checked should not be greater
 * than the length of returned data from read command.
 */
static bool
dsi_panel_parse_esd_check_valid_params(struct dsi_panel *panel, u32 count)
{
	int i;
	struct drm_panel_esd_config *config = &panel->esd_config;

	for (i = 0; i < count; ++i) {
		if (config->status_valid_params[i] >
				config->status_cmds_rlen[i]) {
			DSI_DEBUG("ignore valid params\n");
			return false;
		}
	}

	return true;
}

static bool dsi_panel_parse_esd_status_len(struct dsi_parser_utils *utils,
	char *prop_key, u32 **target, u32 cmd_cnt)
{
	int tmp;

	if (!utils->find_property(utils->data, prop_key, &tmp))
		return false;

	tmp /= sizeof(u32);
	if (tmp != cmd_cnt) {
		DSI_ERR("request property(%d) do not match cmd count(%d)\n",
				tmp, cmd_cnt);
		return false;
	}

	*target = kcalloc(tmp, sizeof(u32), GFP_KERNEL);
	if (IS_ERR_OR_NULL(*target)) {
		DSI_ERR("Error allocating memory for property\n");
		return false;
	}

	if (utils->read_u32_array(utils->data, prop_key, *target, tmp)) {
		DSI_ERR("cannot get values from dts\n");
		kfree(*target);
		*target = NULL;
		return false;
	}

	return true;
}

static void dsi_panel_esd_config_deinit(struct drm_panel_esd_config *esd_config)
{
	kfree(esd_config->status_buf);
	kfree(esd_config->return_buf);
	kfree(esd_config->status_value);
	kfree(esd_config->status_valid_params);
	kfree(esd_config->status_cmds_rlen);
	kfree(esd_config->status_cmd.cmds);
}

int dsi_panel_parse_esd_reg_read_configs(struct dsi_panel *panel)
{
	struct drm_panel_esd_config *esd_config;
	int rc = 0;
	u32 tmp;
	u32 i, status_len, *lenp;
	struct property *data;
	struct dsi_parser_utils *utils = &panel->utils;

	if (!panel) {
		DSI_ERR("Invalid Params\n");
		return -EINVAL;
	}

	esd_config = &panel->esd_config;
	if (!esd_config)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&esd_config->status_cmd,
				DSI_CMD_SET_PANEL_STATUS, utils);
	if (!esd_config->status_cmd.count) {
		DSI_ERR("panel status command parsing failed\n");
		rc = -EINVAL;
		goto error;
	}

	if (!dsi_panel_parse_esd_status_len(utils,
		"qcom,mdss-dsi-panel-status-read-length",
			&panel->esd_config.status_cmds_rlen,
				esd_config->status_cmd.count)) {
		DSI_ERR("Invalid status read length\n");
		rc = -EINVAL;
		goto error1;
	}

	if (dsi_panel_parse_esd_status_len(utils,
		"qcom,mdss-dsi-panel-status-valid-params",
			&panel->esd_config.status_valid_params,
				esd_config->status_cmd.count)) {
		if (!dsi_panel_parse_esd_check_valid_params(panel,
					esd_config->status_cmd.count)) {
			rc = -EINVAL;
			goto error2;
		}
	}

	status_len = 0;
	lenp = esd_config->status_valid_params ?: esd_config->status_cmds_rlen;
	for (i = 0; i < esd_config->status_cmd.count; ++i)
		status_len += lenp[i];

	if (!status_len) {
		rc = -EINVAL;
		goto error2;
	}

	/*
	 * Some panel may need multiple read commands to properly
	 * check panel status. Do a sanity check for proper status
	 * value which will be compared with the value read by dsi
	 * controller during ESD check. Also check if multiple read
	 * commands are there then, there should be corresponding
	 * status check values for each read command.
	 */
	data = utils->find_property(utils->data,
			"qcom,mdss-dsi-panel-status-value", &tmp);
	tmp /= sizeof(u32);
	if (!IS_ERR_OR_NULL(data) && tmp != 0 && (tmp % status_len) == 0) {
		esd_config->groups = tmp / status_len;
	} else {
		DSI_ERR("error parse panel-status-value\n");
		rc = -EINVAL;
		goto error2;
	}

	esd_config->status_value =
		kzalloc(sizeof(u32) * status_len * esd_config->groups,
			GFP_KERNEL);
	if (!esd_config->status_value) {
		rc = -ENOMEM;
		goto error2;
	}

	esd_config->return_buf = kcalloc(status_len * esd_config->groups,
			sizeof(unsigned char), GFP_KERNEL);
	if (!esd_config->return_buf) {
		rc = -ENOMEM;
		goto error3;
	}

	esd_config->status_buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (!esd_config->status_buf) {
		rc = -ENOMEM;
		goto error4;
	}

	rc = utils->read_u32_array(utils->data,
		"qcom,mdss-dsi-panel-status-value",
		esd_config->status_value, esd_config->groups * status_len);
	if (rc) {
		DSI_DEBUG("error reading panel status values\n");
		memset(esd_config->status_value, 0,
				esd_config->groups * status_len);
	}

	return 0;

error4:
	kfree(esd_config->return_buf);
error3:
	kfree(esd_config->status_value);
error2:
	kfree(esd_config->status_valid_params);
	kfree(esd_config->status_cmds_rlen);
error1:
	kfree(esd_config->status_cmd.cmds);
error:
	return rc;
}

static int dsi_panel_parse_esd_config(struct dsi_panel *panel)
{
	int rc = 0;
	const char *string;
	struct drm_panel_esd_config *esd_config;
	struct dsi_parser_utils *utils = &panel->utils;
	u8 *esd_mode = NULL;

	esd_config = &panel->esd_config;
	esd_config->status_mode = ESD_MODE_MAX;
	esd_config->esd_enabled = utils->read_bool(utils->data,
		"qcom,esd-check-enabled");

#ifdef OPLUS_BUG_STABILITY
	switch(get_boot_mode())
	{
		case MSM_BOOT_MODE__RF:
		case MSM_BOOT_MODE__WLAN:
		case MSM_BOOT_MODE__FACTORY:
			esd_config->esd_enabled = 0x0;
			pr_err("%s force disable esd check while in rf,wlan and factory mode, esd staus: 0x%x\n",
						__func__, esd_config->esd_enabled);
			break;

		default:
			break;
	}
#endif /*OPLUS_BUG_STABILITY*/

	if (!esd_config->esd_enabled)
		return 0;

	rc = utils->read_string(utils->data,
			"qcom,mdss-dsi-panel-status-check-mode", &string);
	if (!rc) {
		if (!strcmp(string, "bta_check")) {
			esd_config->status_mode = ESD_MODE_SW_BTA;
		} else if (!strcmp(string, "reg_read")) {
			esd_config->status_mode = ESD_MODE_REG_READ;
		} else if (!strcmp(string, "te_signal_check")) {
			if (panel->panel_mode == DSI_OP_CMD_MODE) {
				esd_config->status_mode = ESD_MODE_PANEL_TE;
			} else {
				DSI_ERR("TE-ESD not valid for video mode\n");
				rc = -EINVAL;
				goto error;
			}
		} else {
			DSI_ERR("No valid panel-status-check-mode string\n");
			rc = -EINVAL;
			goto error;
		}
	} else {
		DSI_DEBUG("status check method not defined!\n");
		rc = -EINVAL;
		goto error;
	}

	if (panel->esd_config.status_mode == ESD_MODE_REG_READ) {
		rc = dsi_panel_parse_esd_reg_read_configs(panel);
		if (rc) {
			DSI_ERR("failed to parse esd reg read mode params, rc=%d\n",
						rc);
			goto error;
		}
		esd_mode = "register_read";
	} else if (panel->esd_config.status_mode == ESD_MODE_SW_BTA) {
		esd_mode = "bta_trigger";
	} else if (panel->esd_config.status_mode ==  ESD_MODE_PANEL_TE) {
		esd_mode = "te_check";
	}

	DSI_DEBUG("ESD enabled with mode: %s\n", esd_mode);

	return 0;

error:
	panel->esd_config.esd_enabled = false;
	return rc;
}

static void dsi_panel_update_util(struct dsi_panel *panel,
				  struct device_node *parser_node)
{
	struct dsi_parser_utils *utils = &panel->utils;

	if (parser_node) {
		*utils = *dsi_parser_get_parser_utils();
		utils->data = parser_node;

		DSI_DEBUG("switching to parser APIs\n");

		goto end;
	}

	*utils = *dsi_parser_get_of_utils();
	utils->data = panel->panel_of_node;
end:
	utils->node = panel->panel_of_node;
}

struct dsi_panel *dsi_panel_get(struct device *parent,
				struct device_node *of_node,
				struct device_node *parser_node,
				const char *type,
				int topology_override)
{
	struct dsi_panel *panel;
	struct dsi_parser_utils *utils;
	const char *panel_physical_type;
	int rc = 0;

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	panel->panel_of_node = of_node;
	panel->parent = parent;
	panel->type = type;

	dsi_panel_update_util(panel, parser_node);
	utils = &panel->utils;

	panel->name = utils->get_property(utils->data,
				"qcom,mdss-dsi-panel-name", NULL);
	if (!panel->name)
		panel->name = DSI_PANEL_DEFAULT_LABEL;

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    iris_query_capability(panel);
#endif
	/*
	 * Set panel type to LCD as default.
	 */
	panel->panel_type = DSI_DISPLAY_PANEL_TYPE_LCD;
	panel_physical_type = utils->get_property(utils->data,
				"qcom,mdss-dsi-panel-physical-type", NULL);
	if (panel_physical_type && !strcmp(panel_physical_type, "oled"))
		panel->panel_type = DSI_DISPLAY_PANEL_TYPE_OLED;
	rc = dsi_panel_parse_host_config(panel);
	if (rc) {
		DSI_ERR("failed to parse host configuration, rc=%d\n",
				rc);
		goto error;
	}

	rc = dsi_panel_parse_panel_mode(panel);
	if (rc) {
		DSI_ERR("failed to parse panel mode configuration, rc=%d\n",
				rc);
		goto error;
	}

	rc = dsi_panel_parse_dfps_caps(panel);
	if (rc)
		DSI_ERR("failed to parse dfps configuration, rc=%d\n", rc);

	rc = dsi_panel_parse_qsync_caps(panel, of_node);
	if (rc)
		DSI_DEBUG("failed to parse qsync features, rc=%d\n", rc);

	rc = dsi_panel_parse_dyn_clk_caps(panel);
	if (rc)
		DSI_ERR("failed to parse dynamic clk config, rc=%d\n", rc);

	rc = dsi_panel_parse_phy_props(panel);
	if (rc) {
		DSI_ERR("failed to parse panel physical dimension, rc=%d\n",
				rc);
		goto error;
	}

	rc = dsi_panel_parse_gpios(panel);
	if (rc) {
		DSI_ERR("failed to parse panel gpios, rc=%d\n", rc);
		goto error;
	}

#ifdef OPLUS_BUG_STABILITY
	rc = dsi_panel_parse_oplus_config(panel);
	if (rc)
		DSI_ERR("failed to parse panel config, rc=%d\n", rc);
#endif /* OPLUS_BUG_STABILITY */

#ifdef OPLUS_BUG_STABILITY
/*Jiasong.ZhongPSW.MM.Display.LCD.Stable,2020-09-17 add for DC backlight */
	rc = dsi_panel_parse_oplus_dc_config(panel);
	if (rc)
		DSI_ERR("failed to parse dc config, rc=%d\n", rc);
#endif /* OPLUS_BUG_STABILITY */

	rc = dsi_panel_parse_power_cfg(panel);
	if (rc)
		DSI_ERR("failed to parse power config, rc=%d\n", rc);

#ifdef OPLUS_BUG_STABILITY
	rc = dsi_panel_parse_panel_power_cfg(panel);
	if (rc)
		DSI_DEBUG("failed to parse panel_power config, rc=%d\n", rc);
#endif /* OPLUS_BUG_STABILITY */
	rc = dsi_panel_parse_bl_config(panel);
	if (rc) {
		DSI_ERR("failed to parse backlight config, rc=%d\n", rc);
		if (rc == -EPROBE_DEFER)
			goto error;
	}

	rc = dsi_panel_parse_misc_features(panel);
	if (rc)
		DSI_ERR("failed to parse misc features, rc=%d\n", rc);

	rc = dsi_panel_parse_hdr_config(panel);
	if (rc)
		DSI_ERR("failed to parse hdr config, rc=%d\n", rc);

	rc = dsi_panel_get_mode_count(panel);
	if (rc) {
		DSI_ERR("failed to get mode count, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_parse_dms_info(panel);
	if (rc)
		DSI_DEBUG("failed to get dms info, rc=%d\n", rc);

	rc = dsi_panel_parse_esd_config(panel);
	if (rc)
		DSI_DEBUG("failed to parse esd config, rc=%d\n", rc);

	panel->power_mode = SDE_MODE_DPMS_OFF;
	drm_panel_init(&panel->drm_panel);
	panel->drm_panel.dev = &panel->mipi_device.dev;
	panel->mipi_device.dev.of_node = of_node;

	rc = drm_panel_add(&panel->drm_panel);
	if (rc)
		goto error;

	mutex_init(&panel->panel_lock);

	return panel;
error:
	kfree(panel);
	return ERR_PTR(rc);
}

void dsi_panel_put(struct dsi_panel *panel)
{
	drm_panel_remove(&panel->drm_panel);

	/* free resources allocated for ESD check */
	dsi_panel_esd_config_deinit(&panel->esd_config);

	kfree(panel);
}

int dsi_panel_drv_init(struct dsi_panel *panel,
		       struct mipi_dsi_host *host)
{
	int rc = 0;
	struct mipi_dsi_device *dev;

	if (!panel || !host) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

#ifdef OPLUS_BUG_STABILITY
	set_esd_check_happened(0);
#endif

	mutex_lock(&panel->panel_lock);

	dev = &panel->mipi_device;

	dev->host = host;
	/*
	 * We dont have device structure since panel is not a device node.
	 * When using drm panel framework, the device is probed when the host is
	 * create.
	 */
	dev->channel = 0;
	dev->lanes = 4;

	panel->host = host;
	rc = dsi_panel_vreg_get(panel);
	if (rc) {
		DSI_ERR("[%s] failed to get panel regulators, rc=%d\n",
		       panel->name, rc);
		goto exit;
	}

	rc = dsi_panel_pinctrl_init(panel);
	if (rc) {
		DSI_ERR("[%s] failed to init pinctrl, rc=%d\n",
				panel->name, rc);
		goto error_vreg_put;
	}

	rc = dsi_panel_gpio_request(panel);
	if (rc) {
		DSI_ERR("[%s] failed to request gpios, rc=%d\n", panel->name,
		       rc);
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
        if (iris_is_chip_supported()) {
            if (!strcmp(panel->type, "primary"))
                goto error_pinctrl_deinit;
	        rc = 0;
	    } else 
#endif
		    goto error_pinctrl_deinit;
	    
	}


	rc = dsi_panel_bl_register(panel);
	if (rc) {
		if (rc != -EPROBE_DEFER)
			DSI_ERR("[%s] failed to register backlight, rc=%d\n",
			       panel->name, rc);
		goto error_gpio_release;
	}

	goto exit;

error_gpio_release:
	(void)dsi_panel_gpio_release(panel);
error_pinctrl_deinit:
	(void)dsi_panel_pinctrl_deinit(panel);
error_vreg_put:
	(void)dsi_panel_vreg_put(panel);
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_drv_deinit(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_bl_unregister(panel);
	if (rc)
		DSI_ERR("[%s] failed to unregister backlight, rc=%d\n",
		       panel->name, rc);

	rc = dsi_panel_gpio_release(panel);
	if (rc)
		DSI_ERR("[%s] failed to release gpios, rc=%d\n", panel->name,
		       rc);

	rc = dsi_panel_pinctrl_deinit(panel);
	if (rc)
		DSI_ERR("[%s] failed to deinit gpios, rc=%d\n", panel->name,
		       rc);

	rc = dsi_panel_vreg_put(panel);
	if (rc)
		DSI_ERR("[%s] failed to put regs, rc=%d\n", panel->name, rc);

	panel->host = NULL;
	memset(&panel->mipi_device, 0x0, sizeof(panel->mipi_device));

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_validate_mode(struct dsi_panel *panel,
			    struct dsi_display_mode *mode)
{
	return 0;
}

int dsi_panel_get_mode_count(struct dsi_panel *panel)
{
	const u32 SINGLE_MODE_SUPPORT = 1;
	struct dsi_parser_utils *utils;
	struct device_node *timings_np, *child_np;
	int num_dfps_rates, num_bit_clks;
	int num_video_modes = 0, num_cmd_modes = 0;
	int count, rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	utils = &panel->utils;

	panel->num_timing_nodes = 0;

	timings_np = utils->get_child_by_name(utils->data,
			"qcom,mdss-dsi-display-timings");
	if (!timings_np && !panel->host_config.ext_bridge_mode) {
		DSI_ERR("no display timing nodes defined\n");
		rc = -EINVAL;
		goto error;
	}

	count = utils->get_child_count(timings_np);
	if ((!count && !panel->host_config.ext_bridge_mode) ||
		count > DSI_MODE_MAX) {
		DSI_ERR("invalid count of timing nodes: %d\n", count);
		rc = -EINVAL;
		goto error;
	}

	/* No multiresolution support is available for video mode panels.
	 * Multi-mode is supported for video mode during POMS is enabled.
	 */
	if (panel->panel_mode != DSI_OP_CMD_MODE &&
		!panel->host_config.ext_bridge_mode &&
		!panel->panel_mode_switch_enabled)
		count = SINGLE_MODE_SUPPORT;

	panel->num_timing_nodes = count;
	dsi_for_each_child_node(timings_np, child_np) {
		if (utils->read_bool(child_np, "qcom,mdss-dsi-video-mode"))
			num_video_modes++;
		else if (utils->read_bool(child_np,
					"qcom,mdss-dsi-cmd-mode"))
			num_cmd_modes++;
		else if (panel->panel_mode == DSI_OP_VIDEO_MODE)
			num_video_modes++;
		else if (panel->panel_mode == DSI_OP_CMD_MODE)
			num_cmd_modes++;
	}

	num_dfps_rates = !panel->dfps_caps.dfps_support ? 1 :
					panel->dfps_caps.dfps_list_len;

	num_bit_clks = !panel->dyn_clk_caps.dyn_clk_support ? 1 :
					panel->dyn_clk_caps.bit_clk_list_len;

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (panel->oplus_priv.is_aod_ramless) {
		/* Inflate num_of_modes by fps and bit clks in dfps */
		panel->num_display_modes = (num_cmd_modes * num_bit_clks) +
				(num_video_modes * num_bit_clks * num_dfps_rates);
	} else {
		/*
		 * Inflate num_of_modes by fps and bit clks in dfps.
		 * Single command mode for video mode panels supporting
		 * panel operating mode switch.
		 */
		num_video_modes = num_video_modes * num_bit_clks * num_dfps_rates;

		if ((panel->panel_mode == DSI_OP_VIDEO_MODE) &&
				(panel->panel_mode_switch_enabled))
			num_cmd_modes  = 1;
		else
			num_cmd_modes = num_cmd_modes * num_bit_clks;

		panel->num_display_modes = num_video_modes + num_cmd_modes;
	}
#else
	/*
	 * Inflate num_of_modes by fps and bit clks in dfps.
	 * Single command mode for video mode panels supporting
	 * panel operating mode switch.
	 */
	num_video_modes = num_video_modes * num_bit_clks * num_dfps_rates;

	if ((panel->panel_mode == DSI_OP_VIDEO_MODE) &&
			(panel->panel_mode_switch_enabled))
		num_cmd_modes  = 1;
	else
		num_cmd_modes = num_cmd_modes * num_bit_clks;

	panel->num_display_modes = num_video_modes + num_cmd_modes;
#endif /* OPLUS_BUG_STABILITY */

error:
	return rc;
}

int dsi_panel_get_phy_props(struct dsi_panel *panel,
			    struct dsi_panel_phy_props *phy_props)
{
	int rc = 0;

	if (!panel || !phy_props) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	memcpy(phy_props, &panel->phy_props, sizeof(*phy_props));
	return rc;
}

int dsi_panel_get_dfps_caps(struct dsi_panel *panel,
			    struct dsi_dfps_capabilities *dfps_caps)
{
	int rc = 0;

	if (!panel || !dfps_caps) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	memcpy(dfps_caps, &panel->dfps_caps, sizeof(*dfps_caps));
	return rc;
}

void dsi_panel_put_mode(struct dsi_display_mode *mode)
{
	int i;

	if (!mode->priv_info)
		return;

	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		dsi_panel_destroy_cmd_packets(&mode->priv_info->cmd_sets[i]);
		dsi_panel_dealloc_cmd_packets(&mode->priv_info->cmd_sets[i]);
	}

	kfree(mode->priv_info);
}

void dsi_panel_calc_dsi_transfer_time(struct dsi_host_common_cfg *config,
		struct dsi_display_mode *mode, u32 frame_threshold_us)
{
	u32 frame_time_us,nslices;
	u64 min_bitclk_hz, total_active_pixels, bits_per_line, pclk_rate_hz,
		dsi_transfer_time_us, pixel_clk_khz;
	struct msm_display_dsc_info *dsc = mode->timing.dsc;
	struct dsi_mode_info *timing = &mode->timing;
	struct dsi_display_mode *display_mode;
	u32 jitter_numer, jitter_denom, prefill_lines;
	u32 min_threshold_us, prefill_time_us;

	/* Packet overlead in bits,2 bytes header + 2 bytes checksum
	 * + 1 byte dcs data command.
        */
	const u32 packet_overhead = 56;

	display_mode = container_of(timing, struct dsi_display_mode, timing);

	jitter_numer = display_mode->priv_info->panel_jitter_numer;
	jitter_denom = display_mode->priv_info->panel_jitter_denom;

	frame_time_us = mult_frac(1000, 1000, (timing->refresh_rate));

	if (timing->dsc_enabled) {
		nslices = (timing->h_active)/(dsc->slice_width);
		/* (slice width x bit-per-pixel + packet overhead) x
		 * number of slices x height x fps / lane
		 */
		bits_per_line = ((dsc->slice_width * dsc->bpp) +
				packet_overhead) * nslices;
		bits_per_line = bits_per_line / (config->num_data_lanes);

		min_bitclk_hz = (bits_per_line * timing->v_active *
					timing->refresh_rate);
	} else {
		total_active_pixels = ((DSI_H_ACTIVE_DSC(timing)
					* timing->v_active));
		/* calculate the actual bitclk needed to transfer the frame */
		min_bitclk_hz = (total_active_pixels * (timing->refresh_rate) *
				(config->bpp));
		do_div(min_bitclk_hz, config->num_data_lanes);
	}

	timing->min_dsi_clk_hz = min_bitclk_hz;

	if (timing->clk_rate_hz) {
		/* adjust the transfer time proportionately for bit clk*/
		dsi_transfer_time_us = frame_time_us * min_bitclk_hz;
		do_div(dsi_transfer_time_us, timing->clk_rate_hz);
		timing->dsi_transfer_time_us = dsi_transfer_time_us;

	} else if (mode->priv_info->mdp_transfer_time_us) {
		timing->dsi_transfer_time_us =
			mode->priv_info->mdp_transfer_time_us;
	} else {

		min_threshold_us = mult_frac(frame_time_us,
				jitter_numer, (jitter_denom * 100));
		/*
		 * Increase the prefill_lines proportionately as recommended
		 * 35lines for 60fps, 52 for 90fps, 70lines for 120fps.
		 */
		prefill_lines = mult_frac(MIN_PREFILL_LINES,
				timing->refresh_rate, 60);

		prefill_time_us = mult_frac(frame_time_us, prefill_lines,
				(timing->v_active));

		/*
		 * Threshold is sum of panel jitter time, prefill line time
		 * plus 100usec buffer time.
		 */
		min_threshold_us = min_threshold_us + 100 + prefill_time_us;

		DSI_DEBUG("min threshold time=%d\n", min_threshold_us);

		if (min_threshold_us > frame_threshold_us)
			frame_threshold_us = min_threshold_us;

		timing->dsi_transfer_time_us = frame_time_us -
			frame_threshold_us;
	}

	timing->mdp_transfer_time_us = timing->dsi_transfer_time_us;

	/* Force update mdp xfer time to hal,if clk and mdp xfer time is set */
	if (mode->priv_info->mdp_transfer_time_us && timing->clk_rate_hz) {
		timing->mdp_transfer_time_us =
			mode->priv_info->mdp_transfer_time_us;
	}

	/* Calculate pclk_khz to update modeinfo */
	pclk_rate_hz =  min_bitclk_hz * frame_time_us;
	do_div(pclk_rate_hz, timing->dsi_transfer_time_us);

	pixel_clk_khz = pclk_rate_hz * config->num_data_lanes;
	do_div(pixel_clk_khz, config->bpp);
	display_mode->pixel_clk_khz = pixel_clk_khz;

	display_mode->pixel_clk_khz =  display_mode->pixel_clk_khz / 1000;
}


int dsi_panel_get_mode(struct dsi_panel *panel,
			u32 index, struct dsi_display_mode *mode,
			int topology_override)
{
	struct device_node *timings_np, *child_np;
	struct dsi_parser_utils *utils;
	struct dsi_display_mode_priv_info *prv_info;
	u32 child_idx = 0;
	int rc = 0, num_timings;
	void *utils_data = NULL;

	if (!panel || !mode) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	utils = &panel->utils;

	mode->priv_info = kzalloc(sizeof(*mode->priv_info), GFP_KERNEL);
	if (!mode->priv_info) {
		rc = -ENOMEM;
		goto done;
	}

	prv_info = mode->priv_info;

	timings_np = utils->get_child_by_name(utils->data,
		"qcom,mdss-dsi-display-timings");
	if (!timings_np) {
		DSI_ERR("no display timing nodes defined\n");
		rc = -EINVAL;
		goto parse_fail;
	}

	num_timings = utils->get_child_count(timings_np);
	if (!num_timings || num_timings > DSI_MODE_MAX) {
		DSI_ERR("invalid count of timing nodes: %d\n", num_timings);
		rc = -EINVAL;
		goto parse_fail;
	}

	utils_data = utils->data;

	dsi_for_each_child_node(timings_np, child_np) {
		if (index != child_idx++)
			continue;

		utils->data = child_np;

		rc = dsi_panel_parse_timing(&mode->timing, utils);
		if (rc) {
			DSI_ERR("failed to parse panel timing, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_dsc_params(mode, utils);
		if (rc) {
			DSI_ERR("failed to parse dsc params, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_topology(prv_info, utils,
				topology_override);
		if (rc) {
			DSI_ERR("failed to parse panel topology, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_cmd_sets(prv_info, utils);
		if (rc) {
			DSI_ERR("failed to parse command sets, rc=%d\n", rc);
			goto parse_fail;
		}

#ifdef OPLUS_BUG_STABILITY
		rc = dsi_panel_parse_oplus_mode_config(mode, utils);
		if (rc)
			DSI_ERR(
			"failed to parse oplus config, rc=%d\n", rc);
#endif

		rc = dsi_panel_parse_jitter_config(mode, utils);
		if (rc)
			DSI_ERR(
			"failed to parse panel jitter config, rc=%d\n", rc);

		rc = dsi_panel_parse_phy_timing(mode, utils);
		if (rc) {
			DSI_ERR(
			"failed to parse panel phy timings, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_partial_update_caps(mode, utils);
		if (rc)
			DSI_ERR("failed to partial update caps, rc=%d\n", rc);

		if (panel->panel_mode_switch_enabled) {
			rc = dsi_panel_parse_panel_mode_caps(mode, utils);
			if (rc) {
				rc = 0;
				mode->panel_mode = panel->panel_mode;
				DSI_INFO(
				"POMS: panel mode isn't specified in timing[%d]\n",
				child_idx);
			}
		} else {
			mode->panel_mode = panel->panel_mode;
		}

#ifdef OPLUS_FEATURE_ADFR
		// ignore the return result
		if (oplus_adfr_is_support()) {
			dsi_panel_parse_adfr(mode, utils);
		}
#endif
	}
	goto done;

parse_fail:
	kfree(mode->priv_info);
	mode->priv_info = NULL;
done:
	utils->data = utils_data;
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_get_host_cfg_for_mode(struct dsi_panel *panel,
				    struct dsi_display_mode *mode,
				    struct dsi_host_config *config)
{
	int rc = 0;
	struct dsi_dyn_clk_caps *dyn_clk_caps = &panel->dyn_clk_caps;

	if (!panel || !mode || !config) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	config->panel_mode = panel->panel_mode;
	memcpy(&config->common_config, &panel->host_config,
	       sizeof(config->common_config));

	if (panel->panel_mode == DSI_OP_VIDEO_MODE) {
		memcpy(&config->u.video_engine, &panel->video_config,
		       sizeof(config->u.video_engine));
	} else {
		memcpy(&config->u.cmd_engine, &panel->cmd_config,
		       sizeof(config->u.cmd_engine));
	}

	memcpy(&config->video_timing, &mode->timing,
	       sizeof(config->video_timing));
	config->video_timing.mdp_transfer_time_us =
			mode->priv_info->mdp_transfer_time_us;
	config->video_timing.dsc_enabled = mode->priv_info->dsc_enabled;
	config->video_timing.dsc = &mode->priv_info->dsc;

	if (dyn_clk_caps->dyn_clk_support)
		config->bit_clk_rate_hz_override = mode->timing.clk_rate_hz;
	else
		config->bit_clk_rate_hz_override = mode->priv_info->clk_rate_hz;

	config->esc_clk_rate_hz = 19200000;
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_prepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    iris_power_on(panel);
#endif

#ifdef OPLUS_BUG_STABILITY
	if (strstr(panel->oplus_priv.vendor_name,"NT36672C")) {
		if (gpio_is_valid(panel->reset_config.reset_gpio) && mdss_tp_black_gesture_status()) {
			rc = gpio_direction_output(panel->reset_config.reset_gpio, 0);
			if (rc) {
				DSI_ERR("unable to set dir for reset gpio rc=%d\n", rc);
			}
			gpio_set_value(panel->reset_config.reset_gpio, 0);
			usleep_range(9000, 10000);
			pr_err("%s: reset gpio 0\n", __func__);
		}
		if ((0 == mdss_tp_black_gesture_status()) || (1 == tp_black_power_on_ff_flag)) {
			tp_black_power_on_ff_flag = 0;
			pr_info("%s:[TP] tp_black_power_on_ff_flag = %d\n",__func__,tp_black_power_on_ff_flag);
			dsi_panel_1p8_on_off(panel,true);
			rc = dsi_pwr_enable_regulator(&panel->power_info, true);
			if (rc) {
				DSI_ERR("[%s][TP] failed to enable vregs, rc=%d\n", panel->name, rc);
				goto error;
			}
		}
	#ifdef CONFIG_REGULATOR_TPS65132
		if (panel->oplus_priv.is_tps65132_support) {
			TPS65132_pw_enable(1);
			usleep_range(2000, 3000);
			DSI_INFO("dsi_panel_power_on TPS65132 power on success\n");
		}
	#endif /*CONFIG_REGULATOR_TPS65132*/
	}
#endif /*OPLUS_BUG_STABILITY*/

	/* If LP11_INIT is set, panel will be powered up during prepare() */
	if (panel->lp11_init)
		goto error;

	rc = dsi_panel_power_on(panel);
	if (rc) {
		DSI_ERR("[%s] panel power on failed, rc=%d\n", panel->name, rc);
		#if defined(OPLUS_FEATURE_PXLW_IRIS5)
		if (iris_is_chip_supported()) {
			if (iris_vdd_valid())
				iris_disable_vdd();
			else
				iris_control_pwr_regulator(false);
		}
		#endif
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_update_pps(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set = NULL;
	struct dsi_display_mode_priv_info *priv_info = NULL;

	if (!panel || !panel->cur_mode) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_dual_supported() && panel->is_secondary)
		return rc;
#endif

	mutex_lock(&panel->panel_lock);

	priv_info = panel->cur_mode->priv_info;

	set = &priv_info->cmd_sets[DSI_CMD_SET_PPS];

	dsi_dsc_create_pps_buf_cmd(&priv_info->dsc, panel->dsc_pps_cmd, 0);
	rc = dsi_panel_create_cmd_packets(panel->dsc_pps_cmd,
					  DSI_CMD_PPS_SIZE, 1, set->cmds);
	if (rc) {
		DSI_ERR("failed to create cmd packets, rc=%d\n", rc);
		goto error;
	}

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    if (iris_is_chip_supported() && iris_is_pt_mode(panel))
        rc = iris_pt_send_panel_cmd(panel, &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_PPS]));
	else
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PPS);
#endif
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_PPS cmds, rc=%d\n",
			panel->name, rc);
	}

	dsi_panel_destroy_cmd_packets(set);
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_lp1(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for dsi_panel_set_lp1\n");
#endif

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized)
		goto exit;

	/*
	 * Consider LP1->LP2->LP1.
	 * If the panel is already in LP mode, do not need to
	 * set the regulator.
	 * IBB and AB power mode would be set at the same time
	 * in PMIC driver, so we only call ibb setting that is enough.
	 */
	if (dsi_panel_is_type_oled(panel) &&
		panel->power_mode != SDE_MODE_DPMS_LP2)
		dsi_pwr_panel_regulator_mode_set(&panel->power_info,
			"ibb", REGULATOR_MODE_IDLE);
	if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3") && (panel->panel_id2 >= 5)) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP1_PVT);
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP1);
	}
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_LP1 cmd, rc=%d\n",
		       panel->name, rc);
#ifdef OPLUS_BUG_STABILITY
	oplus_set_aod_gamma_data_status(panel);
	oplus_update_aod_light_mode_unlock(panel);
	panel->need_power_on_backlight = true;
	set_oplus_display_power_status(OPLUS_DISPLAY_POWER_DOZE);
#endif
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_lp2(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for dsi_panel_set_lp2\n");
#endif

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized)
		goto exit;

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP2);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_LP2 cmd, rc=%d\n",
		       panel->name, rc);
#ifdef OPLUS_BUG_STABILITY
	set_oplus_display_power_status(OPLUS_DISPLAY_POWER_DOZE_SUSPEND);
#endif
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_nolp(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for dsi_panel_set_nolp\n");
#endif

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized)
		goto exit;

	/*
	 * Consider about LP1->LP2->NOLP.
	 */
	if (dsi_panel_is_type_oled(panel) &&
	    (panel->power_mode == SDE_MODE_DPMS_LP1 ||
	     panel->power_mode == SDE_MODE_DPMS_LP2))
		dsi_pwr_panel_regulator_mode_set(&panel->power_info,
			"ibb", REGULATOR_MODE_NORMAL);
	if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3") && (panel->panel_id2 >= 5)) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP_PVT);
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
	}
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_NOLP cmd, rc=%d\n",
		       panel->name, rc);

#ifdef OPLUS_BUG_STABILITY
	if ((!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01") ||
		!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01IN20057") ||
		!strcmp(panel->name, "s6e3fc3_fhd_oled_cmd_samsung") ||
		!strcmp(panel->oplus_priv.vendor_name, "SOFE03F")) &&
		(panel->bl_config.bl_level > panel->bl_config.brightness_normal_max_level)) {
		if (!strcmp(panel->name,"samsung ams643ye01 in 20127 amoled fhd+ panel")) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER1_SWITCH);
			oplus_dsi_display_enable_and_waiting_for_next_te_irq();
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER2_SWITCH);
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
		}
		oplus_panel_update_backlight_unlock(panel);
	}
#endif

#ifdef OPLUS_BUG_STABILITY
	set_oplus_display_power_status(OPLUS_DISPLAY_POWER_ON);
#endif
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_prepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
#ifdef OPLUS_BUG_STABILITY
	if (!strcmp(panel->name,"samsung amb655uv01 amoled fhd+ panel with DSC") ||
		!strcmp(panel->name,"boe nt37800 amoled fhd+ panel with DSC") ||
		!strcmp(panel->name,"samsung fhd amoled") ||
		!strcmp(panel->name, "nt36523 lcd vid mode dsi panel")) {
		usleep_range(6000, 6100);
		dsi_panel_reset(panel);
#endif /* OPLUS_BUG_STABILITY */
	}
	if (panel->lp11_init) {
		rc = dsi_panel_power_on(panel);
		if (rc) {
			DSI_ERR("[%s] panel power on failed, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}
#ifdef OPLUS_BUG_STABILITY
	else {
		usleep_range(2000, 2100);
	}
#endif /* OPLUS_BUG_STABILITY */

#ifdef OPLUS_BUG_STABILITY
	lcd_queue_load_tp_fw();
#endif

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_ON);
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_PRE_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_panel_roi_prepare_dcs_cmds(struct dsi_panel_cmd_set *set,
		struct dsi_rect *roi, int ctrl_idx, int unicast)
{
	static const int ROI_CMD_LEN = 5;

	int rc = 0;

	/* DTYPE_DCS_LWRITE */
	char *caset, *paset;

	set->cmds = NULL;

	caset = kzalloc(ROI_CMD_LEN, GFP_KERNEL);
	if (!caset) {
		rc = -ENOMEM;
		goto exit;
	}
	caset[0] = 0x2a;
	caset[1] = (roi->x & 0xFF00) >> 8;
	caset[2] = roi->x & 0xFF;
	caset[3] = ((roi->x - 1 + roi->w) & 0xFF00) >> 8;
	caset[4] = (roi->x - 1 + roi->w) & 0xFF;

	paset = kzalloc(ROI_CMD_LEN, GFP_KERNEL);
	if (!paset) {
		rc = -ENOMEM;
		goto error_free_mem;
	}
	paset[0] = 0x2b;
	paset[1] = (roi->y & 0xFF00) >> 8;
	paset[2] = roi->y & 0xFF;
	paset[3] = ((roi->y - 1 + roi->h) & 0xFF00) >> 8;
	paset[4] = (roi->y - 1 + roi->h) & 0xFF;

	set->type = DSI_CMD_SET_ROI;
	set->state = DSI_CMD_SET_STATE_LP;
	set->count = 2; /* send caset + paset together */
	set->cmds = kcalloc(set->count, sizeof(*set->cmds), GFP_KERNEL);
	if (!set->cmds) {
		rc = -ENOMEM;
		goto error_free_mem;
	}
	set->cmds[0].msg.channel = 0;
	set->cmds[0].msg.type = MIPI_DSI_DCS_LONG_WRITE;
	set->cmds[0].msg.flags = unicast ? MIPI_DSI_MSG_UNICAST : 0;
	set->cmds[0].msg.ctrl = unicast ? ctrl_idx : 0;
	set->cmds[0].msg.tx_len = ROI_CMD_LEN;
	set->cmds[0].msg.tx_buf = caset;
	set->cmds[0].msg.rx_len = 0;
	set->cmds[0].msg.rx_buf = 0;
	set->cmds[0].msg.wait_ms = 0;
	set->cmds[0].last_command = 0;
	set->cmds[0].post_wait_ms = 0;

	set->cmds[1].msg.channel = 0;
	set->cmds[1].msg.type = MIPI_DSI_DCS_LONG_WRITE;
	set->cmds[1].msg.flags = unicast ? MIPI_DSI_MSG_UNICAST : 0;
	set->cmds[1].msg.ctrl = unicast ? ctrl_idx : 0;
	set->cmds[1].msg.tx_len = ROI_CMD_LEN;
	set->cmds[1].msg.tx_buf = paset;
	set->cmds[1].msg.rx_len = 0;
	set->cmds[1].msg.rx_buf = 0;
	set->cmds[1].msg.wait_ms = 0;
	set->cmds[1].last_command = 1;
	set->cmds[1].post_wait_ms = 0;

	goto exit;

error_free_mem:
	kfree(caset);
	kfree(paset);
	kfree(set->cmds);

exit:
	return rc;
}

int dsi_panel_send_qsync_on_dcs(struct dsi_panel *panel,
		int ctrl_idx)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

#ifdef OPLUS_FEATURE_ADFR
	DSI_INFO("ctrl:%d qsync on\n", ctrl_idx);
	SDE_ATRACE_INT("qsync_mode_cmd", 1);
#else
	DSI_DEBUG("ctrl:%d qsync on\n", ctrl_idx);
#endif /* OPLUS_FEATURE_ADFR */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_QSYNC_ON);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_QSYNC_ON cmds rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_send_qsync_off_dcs(struct dsi_panel *panel,
		int ctrl_idx)
{
	int rc = 0;

#ifdef OPLUS_FEATURE_ADFR
	if (!panel || !panel->cur_mode) {
#else
	if (!panel) {
#endif /* OPLUS_FEATURE_ADFR */
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

#ifdef OPLUS_FEATURE_ADFR
	DSI_INFO("ctrl:%d qsync off\n", ctrl_idx);
	SDE_ATRACE_INT("qsync_mode_cmd", 0);
	SDE_ATRACE_INT("oplus_adfr_qsync_mode_minfps_cmd", panel->cur_mode->timing.refresh_rate);
#else
	DSI_DEBUG("ctrl:%d qsync off\n", ctrl_idx);
#endif /* OPLUS_FEATURE_ADFR */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_QSYNC_OFF);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_QSYNC_OFF cmds rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_send_roi_dcs(struct dsi_panel *panel, int ctrl_idx,
		struct dsi_rect *roi)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set;
	struct dsi_display_mode_priv_info *priv_info;

	if (!panel || !panel->cur_mode) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;
	set = &priv_info->cmd_sets[DSI_CMD_SET_ROI];

	rc = dsi_panel_roi_prepare_dcs_cmds(set, roi, ctrl_idx, true);
	if (rc) {
		DSI_ERR("[%s] failed to prepare DSI_CMD_SET_ROI cmds, rc=%d\n",
				panel->name, rc);
		return rc;
	}
	DSI_DEBUG("[%s] send roi x %d y %d w %d h %d\n", panel->name,
			roi->x, roi->y, roi->w, roi->h);
	SDE_EVT32(roi->x, roi->y, roi->w, roi->h);

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ROI);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_ROI cmds, rc=%d\n",
				panel->name, rc);

	mutex_unlock(&panel->panel_lock);

	dsi_panel_destroy_cmd_packets(set);
	dsi_panel_dealloc_cmd_packets(set);

	return rc;
}

int dsi_panel_pre_mode_switch_to_video(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CMD_TO_VID_SWITCH);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_CMD_TO_VID_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_mode_switch_to_cmd(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_VID_TO_CMD_SWITCH);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_CMD_TO_VID_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_mode_switch_to_cmd(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_VID_TO_CMD_SWITCH);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_CMD_TO_VID_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	oplus_ramless_panel_update_aod_area_unlock();
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_mode_switch_to_vid(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_CMD_TO_VID_SWITCH);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_CMD_TO_VID_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_fps_change(struct dsi_panel *panel)
{
	int rc = 0;
	static unsigned int fps_tmp = 0;

	if(panel->cur_mode->timing.refresh_rate == 60 || panel->cur_mode->timing.refresh_rate == 120) {
		if (fps_tmp == 90) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_FPS_CHANGE);
			if (rc)
				DSI_ERR("[%s] failed to send DSI_CMD_FPS_CHANGE cmds, rc=%d\n",
					panel->name, rc);
		}
	} else if(panel->cur_mode->timing.refresh_rate == 90) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_FPS_CHANGE);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_FPS_CHANGE cmds, rc=%d\n",
					panel->name, rc);
			}
	}
	fps_tmp = panel->cur_mode->timing.refresh_rate;
	pr_info("fps_tmp = %d\n", fps_tmp);
	return rc;
}

int dsi_panel_switch(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_dual_supported() && panel->is_secondary)
		return rc;
#endif
	mutex_lock(&panel->panel_lock);

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported()) {
		rc = iris_switch(panel,
				&(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH]),
				&panel->cur_mode->timing);
	} else
#endif
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);

	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_TIMING_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	if (panel->oplus_priv.is_90fps_switch) {
		dsi_panel_fps_change(panel);
	}
#ifdef OPLUS_BUG_STABILITY
	if (panel->oplus_priv.gamma_switch_enable)
		gamma_switch(panel);
#endif /*OPLUS_BUG_STABILITY*/

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		/* reset adfr auto mode status as panel mode will be change after timing switch */
		dsi_panel_adfr_status_reset(panel);
		if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			oplus_adfr_resolution_vsync_switch(panel);
		} else {
			/* make sure the cur_h_active is the newest status */
			panel->cur_h_active = panel->cur_mode->timing.h_active;
		}
	}
#endif /* OPLUS_FEATURE_ADFR */

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_switch(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    if (iris_is_dual_supported() && panel->is_secondary)
		return rc;
#endif
	mutex_lock(&panel->panel_lock);

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported()) {
		rc = iris_post_switch(panel,
				&(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_POST_TIMING_SWITCH]),
				&panel->cur_mode->timing);
	} else
#endif

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_TIMING_SWITCH);


	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_POST_TIMING_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_enable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}


#ifdef OPLUS_BUG_STABILITY
	pr_err("%s\n", __func__);
#endif

#ifdef OPLUS_BUG_STABILITY
	if (panel->nt36523w_ktz8866) {
		mutex_lock(&panel->panel_lock);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ON);
		if (rc) {
			DSI_ERR("[%s] failed to send DSI_CMD_SET_ON cmds, rc=%d\n", panel->name, rc);
		} else {
			panel->panel_initialized = true;
			panel_initialized_flag = true;
		}
		mutex_unlock(&panel->panel_lock);

		if (panel->cur_mode->timing.refresh_rate == 60) {
			rc = dsi_panel_fps60_cmd_set(panel);
			if (rc)
				DSI_ERR("fps60 failed to set cmd\n");
		}
		return rc;
	}
#endif /*OPLUS_BUG_STABILITY*/

	mutex_lock(&panel->panel_lock);

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		oplus_adfr_vsync_switch_reset(panel);
	}
#endif /* OPLUS_FEATURE_ADFR */

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
    if (iris_is_chip_supported())
        rc = iris_enable(panel, &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_ON]));
	else
#endif
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ON);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_ON cmds, rc=%d\n",
		       panel->name, rc);
	else
		panel->panel_initialized = true;
#ifdef OPLUS_BUG_STABILITY
	if (panel->oplus_priv.gamma_switch_enable && (panel->cur_mode->timing.refresh_rate == 90)) {
		dsi_panel_write_gamma_90(panel);
	}

/* add for optimizing the display effect under low backlight brightness */
	rc = oplus_dimming_gamma_write(panel);
	if (rc)
		DSI_ERR("Failed to write dimming gamma, rc=%d\n", rc);
#endif /*OPLUS_BUG_STABILITY*/

#ifdef OPLUS_FEATURE_ADFR
	if (oplus_adfr_is_support()) {
		dsi_panel_adfr_status_reset(panel);
	}
#endif /* OPLUS_FEATURE_ADFR */

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (panel->is_secondary) {
		mutex_unlock(&panel->panel_lock);
		return rc;
	}
#endif

#ifdef OPLUS_BUG_STABILITY
	panel->need_power_on_backlight = true;
	set_oplus_display_power_status(OPLUS_DISPLAY_POWER_ON);
#endif

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_enable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_ON);

	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_POST_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_disable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_PRE_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_disable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

#ifdef OPLUS_BUG_STABILITY
	pr_err("%s\n", __func__);
#endif

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_dual_supported() && panel->is_secondary)
		return rc;
#endif

	mutex_lock(&panel->panel_lock);

	/* Avoid sending panel off commands when ESD recovery is underway */
	if (!atomic_read(&panel->esd_recovery_pending)) {
		/*
		 * Need to set IBB/AB regulator mode to STANDBY,
		 * if panel is going off from AOD mode.
		 */

		if (dsi_panel_is_type_oled(panel) &&
			(panel->power_mode == SDE_MODE_DPMS_LP1 ||
			panel->power_mode == SDE_MODE_DPMS_LP2))
			dsi_pwr_panel_regulator_mode_set(&panel->power_info,
				"ibb", REGULATOR_MODE_STANDBY);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OFF);
	#ifdef OPLUS_BUG_STABILITY
		if (!strcmp(panel->oplus_priv.vendor_name,"NT37800")) {
			if ((panel->power_mode == SDE_MODE_DPMS_LP1 ||
			     panel->power_mode == SDE_MODE_DPMS_LP2))
				usleep_range(80000, 81000);
		}
	#endif /* OPLUS_BUG_STABILITY */

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
		if (iris_is_chip_supported())			
            iris_disable(panel, NULL);
#endif


		if (rc) {
			/*
			 * Sending panel off commands may fail when  DSI
			 * controller is in a bad state. These failures can be
			 * ignored since controller will go for full reset on
			 * subsequent display enable anyway.
			 */
			pr_warn_ratelimited("[%s] failed to send DSI_CMD_SET_OFF cmds, rc=%d\n",
					panel->name, rc);
			rc = 0;
		}
	}
	panel->panel_initialized = false;
#ifdef OPLUS_BUG_STABILITY
	last_fps = 0;
#endif /*OPLUS_BUG_STABILITY*/

#ifdef OPLUS_BUG_STABILITY
	panel->is_hbm_enabled = false;
	panel_initialized_flag = false;
	set_oplus_display_power_status(OPLUS_DISPLAY_POWER_OFF);
#endif
	panel->power_mode = SDE_MODE_DPMS_OFF;

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_unprepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_POST_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

#ifdef OPLUS_BUG_STABILITY
	if (strstr(panel->oplus_priv.vendor_name,"NT36672C")) {
		if (1 != tp_gesture_enable_flag()) {
			pr_info("%s:%d tp gesture is off set reset 0\n", __func__, __LINE__);
			if (gpio_is_valid(panel->reset_config.reset_gpio))
				gpio_set_value(panel->reset_config.reset_gpio, 0);
			usleep_range(5000, 6000);
		}
	}
#endif/* OPLUS_BUG_STABILITY */

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_unprepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_power_off(panel);
	if (rc) {
		DSI_ERR("[%s] panel power_Off failed, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}
