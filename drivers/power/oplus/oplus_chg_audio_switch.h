// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Oplus. All rights reserved.
 */

enum TYPEC_AUDIO_SWITCH_STATE {
	TYPEC_AUDIO_SWITCH_STATE_DPDM = 0x0,
	TYPEC_AUDIO_SWITCH_STATE_FAST_CHG = 0x1,
	TYPEC_AUDIO_SWITCH_STATE_AUDIO = 0x1 << 1,
	TYPEC_AUDIO_SWITCH_STATE_UNKNOW = 0x1 << 2,
	TYPEC_AUDIO_SWITCH_STATE_MASK = 0x7,
	TYPEC_AUDIO_SWITCH_STATE_SUPPORT = 0x1 << 4,
	TYPEC_AUDIO_SWITCH_STATE_NO_RAM = 0x1 << 4,
	TYPEC_AUDIO_SWITCH_STATE_I2C_ERR = 0x1 << 8,
	TYPEC_AUDIO_SWITCH_STATE_INVALID_PARAM = 0x1 << 9,
};

void audio_switch_init(void);
int oplus_set_audio_switch_status(int state);
int oplus_get_audio_switch_status(void);
