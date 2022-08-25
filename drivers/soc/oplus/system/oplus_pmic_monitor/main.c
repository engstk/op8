// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/module.h>
#include "oplus_pmic_info.h"

/**************** PMIC GEN2 Begin ****************/
/**********************************************/
static ssize_t pmic_history_magic_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
    char page[16] = {0};
    int len = 0;
	struct PMICHistoryKernelStruct *pmic_history_ptr = NULL;

	pmic_history_ptr = (struct PMICHistoryKernelStruct *)get_pmic_history();
	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 16-len, "NULL\n");
	} else {
		len += snprintf(&page[len], 16-len, "%s\n",pmic_history_ptr->pmic_magic);
	}
    memcpy(buf,page,len);
	return len;
}
pmic_info_attr_ro(pmic_history_magic);

/**********************************************/

/**********************************************/
static ssize_t pmic_history_count_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
    char page[16] = {0};
    int len = 0;
	struct PMICHistoryKernelStruct *pmic_history_ptr = NULL;

	pmic_history_ptr = (struct PMICHistoryKernelStruct *)get_pmic_history();
	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len],16-len, "NULL\n");
	} else {
		len += snprintf(&page[len],16-len, "%ld\n",pmic_history_ptr->log_count);
	}
    memcpy(buf,page,len);
	return len;
}
pmic_info_attr_ro(pmic_history_count);

/**********************************************/
static char * const pon_poff_reason_str[] = {
	[0] = "POWER OFF by N/A 0 ",
	[1] = "POWER OFF by N/A 1 ",
	[2] = "POWER OFF by RAW_XVDD_RB_OCCURED ",
	[3] = "POWER OFF by RAW_DVDD_RB_OCCURED ",
	[4] = "POWER OFF by IMMEDIATE_XVDD_SHUTDOWN ",
	[5] = "POWER OFF by S3_RESET ",
	[6] = "POWER OFF by FAULT_SEQ ",
	[7] = "POWER OFF by POFF_SEQ ",
};

static char * const pon_poff_reason1_str[] = {
	[0] = ":SOFT (Software)",
	[1] = ":PS_HOLD (PS_HOLD/MSM Controlled Shutdown)",
	[2] = ":PMIC_WD (PMIC Watchdog)",
	[3] = ":GP1 (Keypad_Reset1)",
	[4] = ":GP2 (Keypad_Reset2)",
	[5] = ":KPDPWR_AND_RESIN (Power Key and Reset Line)",
	[6] = ":RESIN_N (Reset Line/Volume Down Key)",
	[7] = ":KPDPWR_N (Long Power Key Hold)",
};

static char * const pon_fault_reason_str[] = {
	[0] = ":GP_FAULT0",
	[1] = ":GP_FAULT1",
	[2] = ":GP_FAULT2",
	[3] = ":GP_FAULT3",
	[4] = ":MBG_FAULT",
	[5] = ":OVLO",
	[6] = ":UVLO",
	[7] = ":AVDD_RB",
	[8] = ":N/A 8",
	[9] = ":N/A 9",
	[10] = ":N/A 10",
	[11] = ":FAULT_N",
	[12] = ":PBS_WATCHDOG_TO",
	[13] = ":PBS_NACK",
	[14] = ":RESTART_PON",
	[15] = ":OTST3",
};

static char * const pon_s3_reset_reason[] = {
	[0] = ":N/A 0",
	[1] = ":N/A 1",
	[2] = ":N/A 2",
	[3] = ":N/A 3",
	[4] = ":FAULT_N",
	[5] = ":PBS_WATCHDOG_TO",
	[6] = ":PBS_NACK",
	[7] = ":KPDPWR_ANDOR_RESIN",
};

static char * const unknow_reason_str = ":UNKNOW REASON";
static char * const no_L2_reason_str = ":don't have L2 reason";

static ssize_t poff_reason_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf) {
	char page[512] = {0};
    int len = 0;
	u8  L1_poff_index=0,L2_poff_index=0;
	char * L1_str_ptr=unknow_reason_str;
	char * L2_str_ptr=unknow_reason_str;
	struct PMICHistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICRecordKernelStruct pmic_first_record;
	u8 pmic_device_index=0;
	u64 pmic_history_count=0;

	pmic_history_ptr = (struct PMICHistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len],512-len, "PMIC|0|0x00|0x0000|NULL\n");
		memcpy(buf,page,len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	pmic_history_count = pmic_history_ptr->log_count;

	for (pmic_device_index = 0;pmic_device_index<8;pmic_device_index++) {
		if (DATA_VALID_FLAG != pmic_first_record.pmic_pon_poff_reason[pmic_device_index].data_is_valid) {
			continue;
		}

		L1_poff_index = ffs(pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_OFF_REASON);
		switch (L1_poff_index) {
			case INDEX_RAW_XVDD_RB_OCCURED:
			case INDEX_RAW_DVDD_RB_OCCURED:
			case INDEX_IMMEDIATE_XVDD_SHUTDOWN:
					L1_str_ptr = pon_poff_reason_str[L1_poff_index-1];
					L2_poff_index = 0;
					L2_str_ptr = no_L2_reason_str;
				break;

			case INDEX_S3_RESET:
					L1_str_ptr = pon_poff_reason_str[L1_poff_index-1];
					L2_poff_index = ffs(pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_S3_RESET_REASON);
					if (L2_poff_index <= 0 || L2_poff_index > 8 ) {
						L2_str_ptr = unknow_reason_str;
					} else {
						L2_str_ptr = pon_s3_reset_reason[L2_poff_index-1];
					}
				break;

			case INDEX_FAULT_SEQ:
					L1_str_ptr = pon_poff_reason_str[L1_poff_index-1];
					L2_poff_index = ffs((pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_FAULT_REASON2 << 8)
										|(pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_FAULT_REASON1)
										);
					if (L2_poff_index <= 0 || L2_poff_index > 16 ) {
						L2_str_ptr = unknow_reason_str;
					} else {
						L2_str_ptr = pon_fault_reason_str[L2_poff_index-1];
					}
				break;
			case INDEX_POFF_SEQ:
					L1_str_ptr = pon_poff_reason_str[L1_poff_index-1];
					L2_poff_index = ffs(pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_POFF_REASON1);
					if (L2_poff_index <= 0 || L2_poff_index > 8 ) {
						L2_str_ptr = unknow_reason_str;
					} else {
						L2_str_ptr = pon_poff_reason1_str[L2_poff_index-1];
					}
				break;
			default:
					L1_poff_index = 0;
					L2_poff_index = 0;
					L1_str_ptr = unknow_reason_str;
					L2_str_ptr = unknow_reason_str;
				break;
		}

		len += snprintf(&page[len], 512-len, "PMIC|%d|0x%02X|0x%04X|%s %s (1/%ld)\n",
						pmic_device_index,
						L1_poff_index==0?0:0x1<<(L1_poff_index-1),
						L2_poff_index==0?0:0x1<<(L2_poff_index-1),
						L1_str_ptr,
						L2_str_ptr,
						pmic_history_count);
	}

	memcpy(buf,page,len);
	return len;

}
pmic_info_attr_ro(poff_reason);
/**********************************************/

/**********************************************/
static char * const pon_pon_reason1_str[] = {
	[0] = "POWER ON by HARD_RESET",
	[1] = "POWER ON by SMPL",
	[2] = "POWER ON by RTC",
	[3] = "POWER ON by DC_CHG",
	[4] = "POWER ON by USB_CHG",
	[5] = "POWER ON by PON1",
	[6] = "POWER ON by CBLPWR_N",
	[7] = "POWER ON by KPDPWR_N",
};

static char * const pon_warm_reset_reason1_str[] = {
	[0] = ":SOFT",
	[1] = ":PS_HOLD",
	[2] = ":PMIC_WD",
	[3] = ":GP1",
	[4] = ":GP2",
	[5] = ":KPDPWR_AND_RESIN",
	[6] = ":RESIN_N",
	[7] = ":KPDPWR_N",
};

#define QPNP_WARM_SEQ BIT(6)

static ssize_t pon_reason_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf) {
    char page[512] = {0};
    int len = 0;
	struct PMICHistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICRecordKernelStruct pmic_first_record;
	u8 pmic_device_index=0;
	u8 pon_index=0,pon_warm_reset_index=0;
	u8 pon_pon_reason1_reg_vaule=0,pon_warm_reset_reason1=0;
	u64 pmic_history_count=0;

	pmic_history_ptr = (struct PMICHistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 512-len, "PMIC|0|0x000000000000\n");
		memcpy(buf, page, len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	pmic_history_count = pmic_history_ptr->log_count;

	for (pmic_device_index = 0;pmic_device_index<8;pmic_device_index++) {
		if (DATA_VALID_FLAG != pmic_first_record.pmic_pon_poff_reason[pmic_device_index].data_is_valid) {
			continue;
		}

		pon_pon_reason1_reg_vaule = pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_PON_REASON1;
		pon_index = ffs(pon_pon_reason1_reg_vaule);
		if (0 == pon_index) {
			len += snprintf(&page[len],512-len, "PMIC|%d|0x%02X|NKNOW PON REASON\n",pmic_device_index,0x0);
			continue;
		}

		if (QPNP_WARM_SEQ & pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_ON_REASON) {
			pon_warm_reset_reason1 = pmic_first_record.pmic_pon_poff_reason[pmic_device_index].PON_WARM_RESET_REASON1;
			pon_warm_reset_index = ffs(pon_pon_reason1_reg_vaule);
			len += snprintf(&page[len],512-len, "PMIC|%d|0x%02X|WARM_SEQ:%s %s (1/%ld)\n",
													pmic_device_index,
													pon_pon_reason1_reg_vaule,
													pon_pon_reason1_str[pon_index-1],
													0==pon_warm_reset_index?"NULL":pon_warm_reset_reason1_str[pon_warm_reset_index-1],
													pmic_history_count);
		} else {
			len += snprintf(&page[len],512-len, "PMIC|%d|0x%02X|PON_SEQ:%s (1/%ld)\n",
													pmic_device_index,
													pon_pon_reason1_reg_vaule,
													pon_pon_reason1_str[pon_index-1],
													pmic_history_count);
		}
	}

	memcpy(buf,page,len);
	return len;
}
pmic_info_attr_ro(pon_reason);
/**********************************************/

/**********************************************/
static ssize_t ocp_status_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
    char page[256] = {0};
    int len = 0;
	struct PMICHistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICRecordKernelStruct pmic_first_record;
	u8 pmic_device_index=0;

	pmic_history_ptr = (struct PMICHistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 256-len, "PMIC|0|0x000000000000\n");
		memcpy(buf,page,len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	for (pmic_device_index = 0;pmic_device_index<8;pmic_device_index++) {
		if (DATA_VALID_FLAG != pmic_first_record.pmic_pon_poff_reason[pmic_device_index].data_is_valid) {
			continue;
		}
		len += snprintf(&page[len],256-len, "PMIC|%d|0x%08X%02X%02X\n",
				pmic_device_index,
				pmic_first_record.pmic_pon_poff_reason[pmic_device_index].ldo_ocp_status,
				pmic_first_record.pmic_pon_poff_reason[pmic_device_index].spms_ocp_status,
				pmic_first_record.pmic_pon_poff_reason[pmic_device_index].bob_ocp_status);
	}

    memcpy(buf,page,len);
	return len;
}
pmic_info_attr_ro(ocp_status);

static struct attribute * gen2[] = {
    &pmic_history_magic_attr.attr,
	&pmic_history_count_attr.attr,
	&poff_reason_attr.attr,
	&pon_reason_attr.attr,
	&ocp_status_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = gen2,
};

/**********************************************/
/**************** PMIC GEN2 End ****************/

/**************** PMIC GEN3 Begin ****************/

enum pmic_pon_state {
	PMIC_PON_STATE_FAULT0		= 0x0,
	PMIC_PON_STATE_PON		= 0x1,
	PMIC_PON_STATE_POFF		= 0x2,
	PMIC_PON_STATE_ON		= 0x3,
	PMIC_PON_STATE_RESET		= 0x4,
	PMIC_PON_STATE_OFF		= 0x5,
	PMIC_PON_STATE_FAULT6		= 0x6,
	PMIC_PON_STATE_WARM_RESET	= 0x7,
	PMIC_PON_STATE_MAX			= 0x08,
};

static const char * const pmic_pon_state_label[] = {
	[PMIC_PON_STATE_FAULT0]		= "FAULT",
	[PMIC_PON_STATE_PON]		= "PON",
	[PMIC_PON_STATE_POFF]		= "POFF",
	[PMIC_PON_STATE_ON]		= "ON",
	[PMIC_PON_STATE_RESET]		= "RESET",
	[PMIC_PON_STATE_OFF]		= "OFF",
	[PMIC_PON_STATE_FAULT6]		= "FAULT",
	[PMIC_PON_STATE_WARM_RESET]	= "WARM_RESET",
};

enum pmic_pon_event {
	PMIC_PON_EVENT_PON_TRIGGER_RECEIVED	= 0x01,
	PMIC_PON_EVENT_OTP_COPY_COMPLETE	= 0x02,
	PMIC_PON_EVENT_TRIM_COMPLETE		= 0x03,
	PMIC_PON_EVENT_XVLO_CHECK_COMPLETE	= 0x04,
	PMIC_PON_EVENT_PMIC_CHECK_COMPLETE	= 0x05,
	PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED	= 0x06,
	PMIC_PON_EVENT_RESET_TYPE		= 0x07,
	PMIC_PON_EVENT_WARM_RESET_COUNT		= 0x08,
	PMIC_PON_EVENT_FAULT_REASON_1_2		= 0x09,
	PMIC_PON_EVENT_FAULT_REASON_3		= 0x0A,
	PMIC_PON_EVENT_PBS_PC_DURING_FAULT	= 0x0B,
	PMIC_PON_EVENT_FUNDAMENTAL_RESET	= 0x0C,
	PMIC_PON_EVENT_PON_SEQ_START		= 0x0D,
	PMIC_PON_EVENT_PON_SUCCESS		= 0x0E,
	PMIC_PON_EVENT_WAITING_ON_PSHOLD	= 0x0F,
	PMIC_PON_EVENT_PMIC_SID1_FAULT		= 0x10,
	PMIC_PON_EVENT_PMIC_SID2_FAULT		= 0x11,
	PMIC_PON_EVENT_PMIC_SID3_FAULT		= 0x12,
	PMIC_PON_EVENT_PMIC_SID4_FAULT		= 0x13,
	PMIC_PON_EVENT_PMIC_SID5_FAULT		= 0x14,
	PMIC_PON_EVENT_PMIC_VREG_READY_CHECK	= 0x15,
	PMIC_PON_EVENT_PMIC_MAX				= 0x16,
};

enum pmic_pon_reset_type {
	PMIC_PON_RESET_TYPE_WARM_RESET		= 0x1,
	PMIC_PON_RESET_TYPE_SHUTDOWN		= 0x4,
	PMIC_PON_RESET_TYPE_HARD_RESET		= 0x7,
};

static const char * const pmic_pon_reset_type_label[] = {
	[PMIC_PON_RESET_TYPE_WARM_RESET]	= "WARM_RESET",
	[PMIC_PON_RESET_TYPE_SHUTDOWN]		= "SHUTDOWN",
	[PMIC_PON_RESET_TYPE_HARD_RESET]	= "HARD_RESET",
};

static const char * const pmic_pon_fault_reason1[8] = {
	[0] = "GP_FAULT0",
	[1] = "GP_FAULT1",
	[2] = "GP_FAULT2",
	[3] = "GP_FAULT3",
	[4] = "MBG_FAULT",
	[5] = "OVLO",
	[6] = "UVLO",
	[7] = "AVDD_RB",
};

static const char * const pmic_pon_fault_reason2[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "UNKNOWN(2)",
	[3] = "FAULT_N",
	[4] = "FAULT_WATCHDOG",
	[5] = "PBS_NACK",
	[6] = "RESTART_PON",
	[7] = "OVERTEMP_STAGE3",
};

static const char * const pmic_pon_fault_reason3[8] = {
	[0] = "GP_FAULT4",
	[1] = "GP_FAULT5",
	[2] = "GP_FAULT6",
	[3] = "GP_FAULT7",
	[4] = "GP_FAULT8",
	[5] = "GP_FAULT9",
	[6] = "GP_FAULT10",
	[7] = "GP_FAULT11",
};

static const char * const pmic_pon_s3_reset_reason[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "UNKNOWN(2)",
	[3] = "UNKNOWN(3)",
	[4] = "FAULT_N",
	[5] = "FAULT_WATCHDOG",
	[6] = "PBS_NACK",
	[7] = "KPDPWR_AND/OR_RESIN",
};

static const char * const pmic_pon_pon_pbl_status[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "UNKNOWN(2)",
	[3] = "UNKNOWN(3)",
	[4] = "UNKNOWN(4)",
	[5] = "UNKNOWN(5)",
	[6] = "XVDD",
	[7] = "DVDD",
};

struct pmic_pon_trigger_mapping {
	u16		code;
	const char	*label;
};

static const struct pmic_pon_trigger_mapping pmic_pon_pon_trigger_map[] = {
	{0x0084, "PS_HOLD"},
	{0x0085, "HARD_RESET"},
	{0x0086, "RESIN_N"},
	{0x0087, "KPDPWR_N"},
	{0x0621, "RTC_ALARM"},
	{0x0640, "SMPL"},
	{0x18C0, "PMIC_SID1_GPIO5"},
	{0x31C2, "USB_CHARGER"},
};

static const struct pmic_pon_trigger_mapping pmic_pon_reset_trigger_map[] = {
	{0x0080, "KPDPWR_N_S2"},
	{0x0081, "RESIN_N_S2"},
	{0x0082, "KPDPWR_N_AND_RESIN_N_S2"},
	{0x0083, "PMIC_WATCHDOG_S2"},
	{0x0084, "PS_HOLD"},
	{0x0085, "SW_RESET"},
	{0x0086, "RESIN_N_DEBOUNCE"},
	{0x0087, "KPDPWR_N_DEBOUNCE"},
	{0x21E3, "PMIC_SID2_BCL_ALARM"},
	{0x31F5, "PMIC_SID3_BCL_ALARM"},
	{0x11D0, "PMIC_SID1_OCP"},
	{0x21D0, "PMIC_SID2_OCP"},
	{0x41D0, "PMIC_SID4_OCP"},
	{0x51D0, "PMIC_SID5_OCP"},
};

static int pmic_pon_log_print_reason(char *buf, int buf_size, u8 data,
					const char * const *reason)
{
	int pos = 0;
	int i;
	bool first;

	if (data == 0) {
		pos += scnprintf(buf + pos, buf_size - pos, "None");
	} else {
		first = true;
		for (i = 0; i < 8; i++) {
			if (data & BIT(i)) {
				pos += scnprintf(buf + pos, buf_size - pos,
						"%s%s",
						(first ? "" : ", "), reason[i]);
				first = false;
			}
		}
	}

	return pos;
}

static int get_pon_log_by_state_event(u8 state , u8 event ,
int skip , struct PMICGen3RecordKernelStruct *pmic_record_ptr , struct PmicGen3PonStateStruct *pon_log) {
	int i;
	u8 tmp_state;
	u8 tmp_event;

	if ((NULL == pmic_record_ptr) || (NULL == pon_log)) {
		return -1;
	}

	for (i = 0 ; i < MAX_STATE_RECORDS ; i++) {
		tmp_state = pmic_record_ptr->pmic_state_machine_log[i].state;
		tmp_event = pmic_record_ptr->pmic_state_machine_log[i].event;
		if(0 == tmp_state && 0 == tmp_event) {
			pon_log->state = 0;
			pon_log->event = 0;
			pon_log->data1 = 0;
			pon_log->data0 = 0;
			return -1;
		}
		if (((state == tmp_state) && (event == tmp_event)) ||   /* match state & event */
			((state == PMIC_PON_STATE_MAX) && (event == tmp_event)) || /* match event */
			((state == tmp_state) && (event == PMIC_PON_EVENT_PMIC_MAX)) /* match state */) {
				if (skip > 0) {
					skip = skip - 1;
				} else {
					*pon_log = pmic_record_ptr->pmic_state_machine_log[i];
					return i;
				}
		}
	}

	return -1;
}

#define BUF_SIZE 128
static int pmic_pon_log_parse (struct PmicGen3PonStateStruct *pon_state_machine, char *parse_log) {
	char buf[BUF_SIZE];
	const char *label = NULL;
	int pos = 0;
	int i = 0;
	u16 data;

	if (NULL == pon_state_machine || NULL == parse_log) {
		return -1;
	}
	data = (pon_state_machine->data1 << 8) | pon_state_machine->data0;
	buf[0] = '\0';

	switch (pon_state_machine->event) {
	case PMIC_PON_EVENT_PON_TRIGGER_RECEIVED:
			for (i = 0; i < ARRAY_SIZE(pmic_pon_pon_trigger_map); i++) {
				if (pmic_pon_pon_trigger_map[i].code == data) {
					label = pmic_pon_pon_trigger_map[i].label;
					break;
				}
			}
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					 "PON Trigger: ");
			if (label) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos, "%s",
						 label);
			} else {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						 "SID=0x%X, PID=0x%02X, IRQ=0x%X",
						 pon_state_machine->data1 >> 4, (data >> 4) & 0xFF,
						 pon_state_machine->data0 & 0x7);
			}
			break;

	case PMIC_PON_EVENT_OTP_COPY_COMPLETE:
			scnprintf(buf, BUF_SIZE,
				"OTP Copy Complete: last addr written=0x%04X",
				data);
			break;

	case PMIC_PON_EVENT_TRIM_COMPLETE:
			scnprintf(buf, BUF_SIZE, "Trim Complete: %u bytes written",
				data);
			break;

	case PMIC_PON_EVENT_XVLO_CHECK_COMPLETE:
			scnprintf(buf, BUF_SIZE, "XVLO Check Complete");
			break;

	case PMIC_PON_EVENT_PMIC_CHECK_COMPLETE:
			scnprintf(buf, BUF_SIZE, "PMICs Detected: SID Mask=0x%04X",
				data);
			break;

	case PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED:
			for (i = 0; i < ARRAY_SIZE(pmic_pon_reset_trigger_map); i++) {
				if (pmic_pon_reset_trigger_map[i].code == data) {
					label = pmic_pon_reset_trigger_map[i].label;
					break;
				}
			}
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					 "Reset Trigger: ");
			if (label) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos, "%s",
						 label);
			} else {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						 "SID=0x%X, PID=0x%02X, IRQ=0x%X",
						 pon_state_machine->data1 >> 4, (data >> 4) & 0xFF,
						 pon_state_machine->data0 & 0x7);
			}
			break;

	case PMIC_PON_EVENT_RESET_TYPE:
			if (pon_state_machine->data0 < ARRAY_SIZE(pmic_pon_reset_type_label) &&
				pmic_pon_reset_type_label[pon_state_machine->data0])
				scnprintf(buf, BUF_SIZE, "Reset Type: %s",
					pmic_pon_reset_type_label[pon_state_machine->data0]);
			else
				scnprintf(buf, BUF_SIZE, "Reset Type: UNKNOWN (%u)",
					pon_state_machine->data0);
			break;

	case PMIC_PON_EVENT_WARM_RESET_COUNT:
			scnprintf(buf, BUF_SIZE, "Warm Reset Count: %u", data);
			break;

	case PMIC_PON_EVENT_FAULT_REASON_1_2:
			if (pon_state_machine->data0) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"FAULT_REASON1=");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data0,
						pmic_pon_fault_reason1);
			}
			if (pon_state_machine->data1) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"%sFAULT_REASON2=",
						(pon_state_machine->data0)
							? "; " : "");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data1,
						pmic_pon_fault_reason2);
			}
			break;

	case PMIC_PON_EVENT_FAULT_REASON_3:
			if (!pon_state_machine->data0)
			pos += scnprintf(buf + pos, BUF_SIZE - pos, "FAULT_REASON3=");
			pos += pmic_pon_log_print_reason(buf + pos, BUF_SIZE - pos,
						pon_state_machine->data0, pmic_pon_fault_reason3);
			break;

	case PMIC_PON_EVENT_PBS_PC_DURING_FAULT:
			scnprintf(buf, BUF_SIZE, "PBS PC at Fault: 0x%04X", data);
			break;

	case PMIC_PON_EVENT_FUNDAMENTAL_RESET:
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"Fundamental Reset: ");
			if (pon_state_machine->data1) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"PON_PBL_STATUS=");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data1,
						pmic_pon_pon_pbl_status);
			}
			if (pon_state_machine->data0) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"%sS3_RESET_REASON=",
						(pon_state_machine->data1)
							? "; " : "");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data0,
						pmic_pon_s3_reset_reason);
			}

			break;

	case PMIC_PON_EVENT_PON_SEQ_START:
			scnprintf(buf, BUF_SIZE, "Begin PON Sequence");
			break;

	case PMIC_PON_EVENT_PON_SUCCESS:
			scnprintf(buf, BUF_SIZE, "PON Successful");
			break;

	case PMIC_PON_EVENT_WAITING_ON_PSHOLD:
			scnprintf(buf, BUF_SIZE, "Waiting on PS_HOLD");
			break;

	case PMIC_PON_EVENT_PMIC_SID1_FAULT:
	case PMIC_PON_EVENT_PMIC_SID2_FAULT:
	case PMIC_PON_EVENT_PMIC_SID3_FAULT:
	case PMIC_PON_EVENT_PMIC_SID4_FAULT:
	case PMIC_PON_EVENT_PMIC_SID5_FAULT:
			pos += scnprintf(buf + pos, BUF_SIZE - pos, "PMIC SID%u ",
				pon_state_machine->event - PMIC_PON_EVENT_PMIC_SID1_FAULT + 1);
			if (pon_state_machine->data0) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"FAULT_REASON1=");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data0,
						pmic_pon_fault_reason1);
			}
			if (pon_state_machine->data1) {
				pos += scnprintf(buf + pos, BUF_SIZE - pos,
						"%sFAULT_REASON2=",
						(pon_state_machine->data0)
							? "; " : "");
				pos += pmic_pon_log_print_reason(buf + pos,
						BUF_SIZE - pos, pon_state_machine->data1,
						pmic_pon_fault_reason2);
			}
			break;

	case PMIC_PON_EVENT_PMIC_VREG_READY_CHECK:
			scnprintf(buf, BUF_SIZE, "VREG Check: %sVREG_FAULT detected",
				data ? "" : "No ");
			break;

	default:
			scnprintf(buf, BUF_SIZE, "Unknown Event (0x%02X): data=0x%04X",
				pon_state_machine->event, data);
			break;
	}

	scnprintf(parse_log, BUF_SIZE, buf);

	return 0;
}

/**********************************************/
static ssize_t pmic_history_magic_gen3_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	char page[16] = {0};
	int len = 0;
	struct PMICGen3HistoryKernelStruct *pmic_history_ptr = NULL;

	pmic_history_ptr = (struct PMICGen3HistoryKernelStruct *)get_pmic_history();
	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 16-len, "NULL\n");
	} else {
		len += snprintf(&page[len], 16-len, "%s\n", pmic_history_ptr->pmic_magic);
	}
	memcpy(buf, page, len);
	return len;
}
pmic_gen3_info_attr_ro(pmic_history_magic);

/**********************************************/

/**********************************************/
static ssize_t pmic_history_count_gen3_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	char page[16] = {0};
	int len = 0;
	struct PMICGen3HistoryKernelStruct *pmic_history_ptr = NULL;

	pmic_history_ptr = (struct PMICGen3HistoryKernelStruct *)get_pmic_history();
	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 16-len, "NULL\n");
	} else {
		len += snprintf(&page[len], 16-len, "%ld\n", pmic_history_ptr->log_count);
	}
	memcpy(buf, page, len);
	return len;
}
pmic_gen3_info_attr_ro(pmic_history_count);

/**********************************************/
static ssize_t poff_reason_gen3_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf) {
	char page[512] = {0};
	int len = 0, skip = 0;
	u8 L1_poff_code = 0, L2_poff_code = 0;
	struct PMICGen3HistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICGen3RecordKernelStruct pmic_first_record;
	u64 pmic_history_count = 0;
	struct PmicGen3PonStateStruct tmp_pon_log;
	char parse_log_str1[128] = {0};
	char parse_log_str2[128] = {0};
	char parse_log_str3[128] = {0};

	pmic_history_ptr = (struct PMICGen3HistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 512-len, "PMIC|0|0x00|0x0000|NULL\n");
		memcpy(buf, page, len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	pmic_history_count = pmic_history_ptr->log_count;

	/* Fundamental Reset */
	if(-1 != get_pon_log_by_state_event(PMIC_PON_STATE_MAX, /* ignore state */
									PMIC_PON_EVENT_FUNDAMENTAL_RESET,
									0,
									&pmic_first_record,
									&tmp_pon_log)) {
		/* FAULT */
		L1_poff_code = 0x08;
		L2_poff_code = tmp_pon_log.data1 << 8 | tmp_pon_log.data0;
		pmic_pon_log_parse(&tmp_pon_log, parse_log_str1);

		len += snprintf(&page[len], 512-len, "PMIC|%d|0x%02X|0x%04X|%s (%ld)\n",
				0,
				L1_poff_code,
				L2_poff_code,
				parse_log_str1,
				pmic_history_count);
	}
	/* ->fault(state:6) */
	else if (-1 != get_pon_log_by_state_event(PMIC_PON_STATE_FAULT6,
									PMIC_PON_EVENT_FAULT_REASON_1_2,
									0,
									&pmic_first_record,
									&tmp_pon_log)) {
		/* FAULT */
		L1_poff_code = 0x40;
		L2_poff_code = tmp_pon_log.data1 << 8 | tmp_pon_log.data0;
		pmic_pon_log_parse(&tmp_pon_log, parse_log_str1);

		/* PMIC0 fault log */
		if (-1 != get_pon_log_by_state_event(PMIC_PON_STATE_FAULT6,
									PMIC_PON_EVENT_FAULT_REASON_3,
									0,
									&pmic_first_record,
									&tmp_pon_log)) {
			pmic_pon_log_parse(&tmp_pon_log , parse_log_str2);
		} else {
			snprintf(parse_log_str2, 32, "Can't find Fault_REASON3");
		}

		len += snprintf(&page[len], 512-len, "PMIC|%d|0x%02X|0x%04X|%s,%s (%ld)\n",
				0,
				L1_poff_code,
				L2_poff_code,
				parse_log_str1,
				parse_log_str2,
				pmic_history_count);
	}
	/* on(state:3)->reset(state:4) */
	else if (-1 != get_pon_log_by_state_event(PMIC_PON_STATE_RESET,
									PMIC_PON_EVENT_RESET_TYPE,
									0,
									&pmic_first_record,
									&tmp_pon_log)) {
			L1_poff_code = 0x80;
			pmic_pon_log_parse(&tmp_pon_log, parse_log_str1); /* warm reset/shutdown/hard reset */

			if (PMIC_PON_RESET_TYPE_WARM_RESET == tmp_pon_log.data0) {
				if(-1 != get_pon_log_by_state_event(PMIC_PON_STATE_WARM_RESET,
									PMIC_PON_EVENT_WARM_RESET_COUNT,
									0,
									&pmic_first_record,
									&tmp_pon_log)) {
					pmic_pon_log_parse(&tmp_pon_log, parse_log_str3); /* warm reset count */
					skip = tmp_pon_log.data1 << 8 | tmp_pon_log.data0; /* For warm reset case, we need skip warm reset count-1 */
					skip = skip > 0?skip-1:0;
				} else {
					skip = 2; /* If we can't find reset count, we output the logs for the 3rd reset */
				}
			} else {
				skip = 0;
			}

			if(-1 != get_pon_log_by_state_event(PMIC_PON_STATE_RESET,
								PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED,
								skip,
								&pmic_first_record,
								&tmp_pon_log)) {
				pmic_pon_log_parse(&tmp_pon_log , parse_log_str2);
				L2_poff_code = tmp_pon_log.data1 << 8 | tmp_pon_log.data0;
			} else {
				L2_poff_code = 0;
				snprintf(parse_log_str2 , 32 , "can't find RESET_TRIGGER");
			}

			len += snprintf(&page[len], 512-len, "PMIC|%d|0x%02X|0x%04X|%s,%s,%s (%ld)\n",
							0,
							L1_poff_code,
							L2_poff_code,
							parse_log_str1,
							parse_log_str2,
							parse_log_str3,
							pmic_history_count);
	} else {
		len += snprintf(&page[len], 512-len, "PMIC|0|0x00|0x0000|Can't parse poff reason (%ld)\n", pmic_history_count);
	}

	memcpy(buf, page, len);
	return len;
}
pmic_gen3_info_attr_ro(poff_reason);
/**********************************************/

static ssize_t pon_reason_gen3_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf) {
	char page[512] = {0};
	int len = 0;
	struct PMICGen3HistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICGen3RecordKernelStruct pmic_first_record;
	u64 pmic_history_count = 0;
	struct PmicGen3PonStateStruct tmp_pon_log;
	char parse_log_str1[128] = {0};
	u8  pon_code = 0;

	pmic_history_ptr = (struct PMICGen3HistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 512-len, "PMIC|0|0x000000000000\n");
		memcpy(buf, page, len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	pmic_history_count = pmic_history_ptr->log_count;

	if(-1 != get_pon_log_by_state_event(PMIC_PON_STATE_OFF,
						PMIC_PON_EVENT_PON_TRIGGER_RECEIVED,
						0,
						&pmic_first_record,
						&tmp_pon_log)) {
			pon_code = tmp_pon_log.data1 << 8 | tmp_pon_log.data0;
			pmic_pon_log_parse(&tmp_pon_log, parse_log_str1);
			len += snprintf(&page[len], 512-len, "PMIC|%d|0x%02X|%s (%ld)\n",
							0,
							pon_code,
							parse_log_str1,
							pmic_history_count);
	} else {
			len += snprintf(&page[len], 512-len, "PMIC|0|0x00|Can't parse pon reason (%ld)\n", pmic_history_count);
	}

	memcpy(buf, page, len);
	return len;
}
pmic_gen3_info_attr_ro(pon_reason);

/**********************************************/
static ssize_t ocp_status_gen3_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	char page[256] = {0};
	int len = 0;
	struct PMICGen3HistoryKernelStruct *pmic_history_ptr = NULL;
	struct PMICGen3RecordKernelStruct pmic_first_record;
	u8 pmic_device_index = 0;

	pmic_history_ptr = (struct PMICGen3HistoryKernelStruct *)get_pmic_history();

	if (NULL == pmic_history_ptr) {
		len += snprintf(&page[len], 256-len, "PMIC|0|0x000000000000\n");
		memcpy(buf, page, len);
		return len;
	}

	pmic_first_record = pmic_history_ptr->pmic_record[0];
	for (pmic_device_index = 0; pmic_device_index < 8; pmic_device_index++) {
		if(DATA_VALID_FLAG != pmic_first_record.pmic_ocp_record[pmic_device_index].data_is_valid) {
			continue;
		}
		len += snprintf(&page[len], 256-len, "PMIC|%d|0x%08X%02X%02X\n",
				pmic_device_index,
				pmic_first_record.pmic_ocp_record[pmic_device_index].ldo_ocp_status,
				pmic_first_record.pmic_ocp_record[pmic_device_index].spms_ocp_status,
				pmic_first_record.pmic_ocp_record[pmic_device_index].bob_ocp_status);
	}

	memcpy(buf, page, len);
	return len;
}
pmic_gen3_info_attr_ro(ocp_status);
/**********************************************/

static struct attribute * gen3[] = {
	&pmic_history_magic_gen3_attr.attr,
	&pmic_history_count_gen3_attr.attr,
	&poff_reason_gen3_attr.attr,
	&pon_reason_gen3_attr.attr,
	&ocp_status_gen3_attr.attr,
	NULL,
};

static struct attribute_group attr_gen3_group = {
	.attrs = gen3,
};

/**************** PMIC GEN3 End ****************/

struct kobject *pmic_info_kobj;

static int __init pmic_info_init(void)
{
	int error;

	u64 *magic;

	pmic_info_kobj = kobject_create_and_add("pmic_info", NULL);
	if(!pmic_info_kobj)
		return -ENOMEM;
	magic = (u64 *)get_pmic_history();

	if(NULL == magic) {
		return -1;
	}

	if(PMIC_GEN3_INFO_MAGIC == *magic) {
		error = sysfs_create_group(pmic_info_kobj, &attr_gen3_group);
	} else {
		error = sysfs_create_group(pmic_info_kobj, &attr_group);
	}
	if (error)
		return error;
	return 0;
}

late_initcall_sync(pmic_info_init);

MODULE_LICENSE("GPL v2");
