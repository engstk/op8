/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef _AW8697_H_
#define _AW8697_H_

/*********************************************************
 *
 * kernel version
 *
 ********************************************************/
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 4, 1)
#define TIMED_OUTPUT
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 14, 1)
#define KERNEL_VERSION_414
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 9, 1)
#define KERNEL_VERSION_49
#endif
/*********************************************************
 *
 * aw8697.h
 *
 ********************************************************/
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#ifdef TIMED_OUTPUT
#include <../../../drivers/staging/android/timed_output.h>
#else
#include <linux/leds.h>
#endif

/*********************************************************
 *
 * marco
 *
 ********************************************************/
#define MAX_I2C_BUFFER_SIZE                 65536

#define AW8697_REG_MAX                      0xff

#define AW8697_SEQUENCER_SIZE               8
#define AW8697_SEQUENCER_LOOP_SIZE          4

#define AW8697_RTP_I2C_SINGLE_MAX_NUM       512

#define HAPTIC_MAX_TIMEOUT                  10000

#define AW8697_VBAT_REFER                   4400
#define AW8697_VBAT_MIN                     3000
#define AW8697_VBAT_MAX                     4500

/* motor config */
#define LRA_0619
//#define LRA_0832

#ifdef LRA_0619
#define AW8697_HAPTIC_F0_PRE                1700    // 170Hz
#define AW8697_HAPTIC_F0_CALI_PERCEN        7       // -7%~7%
#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW8697_HAPTIC_CONT_DRV_LVL          52     // 105*6.1/256=2.50v
#else
#define AW8697_HAPTIC_CONT_DRV_LVL          105     // 105*6.1/256=2.50v
#endif
#define AW8697_HAPTIC_CONT_DRV_LVL_OV       125     // 125*6.1/256=2.98v
#define AW8697_HAPTIC_CONT_TD               0x009a
#define AW8697_HAPTIC_CONT_ZC_THR           0x0ff1
#define AW8697_HAPTIC_CONT_NUM_BRK          3
#endif

#ifdef LRA_0832
#define AW8697_HAPTIC_F0_PRE                2350    // 170Hz
#define AW8697_HAPTIC_F0_CALI_PERCEN        7       // -7%~7%
#define AW8697_HAPTIC_CONT_DRV_LVL          125     // 125*6.1/256=2.98v
#define AW8697_HAPTIC_CONT_DRV_LVL_OV       155     // 155*6.1/256=3.69v
#define AW8697_HAPTIC_CONT_TD               0x006c
#define AW8697_HAPTIC_CONT_ZC_THR           0x0ff1
#define AW8697_HAPTIC_CONT_NUM_BRK          3
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW8697_0832_HAPTIC_F0_PRE                2350    // 235Hz
#define AW8697_0832_HAPTIC_F0_CALI_PERCEN        7       // -7%~7%
#define AW8697_0832_HAPTIC_CONT_DRV_LVL          105     // 125*6.1/256=2.98v
#define AW8697_0832_HAPTIC_CONT_DRV_LVL_OV       125     // 155*6.1/256=3.69v
#define AW8697_0832_HAPTIC_CONT_TD               0x006c
#define AW8697_0832_HAPTIC_CONT_ZC_THR           0x0ff1
#define AW8697_0832_HAPTIC_CONT_NUM_BRK          3

#define AW8697_0815_HAPTIC_F0_PRE                1700    // 170Hz
#define AW8697_0815_HAPTIC_F0_CALI_PERCEN        7       // -7%~7%
#define AW8697_0815_HAPTIC_CONT_DRV_LVL          60     // 105*6.1/256=2.50v
#define AW8697_0815_HAPTIC_CONT_DRV_LVL_OV       125     // 125*6.1/256=2.98v
#define AW8697_0815_HAPTIC_CONT_TD               0x009a
#define AW8697_0815_HAPTIC_CONT_ZC_THR           0x0ff1
#define AW8697_0815_HAPTIC_CONT_NUM_BRK          3

#define AW8697_081538_HAPTIC_F0_PRE                1500    /* 150Hz */
#define AW8697_081538_HAPTIC_F0_CALI_PERCEN        7       /* -7%~7% */
#define AW8697_081538_HAPTIC_CONT_DRV_LVL          118     /* 118*6.1/256=2.81v */
#define AW8697_081538_HAPTIC_CONT_DRV_LVL_OV       118     /* NA */
#define AW8697_081538_HAPTIC_CONT_TD               0x009a
#define AW8697_081538_HAPTIC_CONT_ZC_THR           0x0ff1
#define AW8697_081538_HAPTIC_CONT_NUM_BRK          3
#endif

#define DEV_ID_0619		619
#define DEV_ID_0832		832
#define DEV_ID_1040             1040
#define AW8697_HAPTIC_F0_COEFF              260     //2.604167


/* trig config */
#define AW8697_TRIG_NUM                     3
#define AW8697_TRG1_ENABLE                  1
#define AW8697_TRG2_ENABLE                  1
#define AW8697_TRG3_ENABLE                  1

/*
 * trig default high level
 * ___________           _________________
 *           |           |
 *           |           |
 *           |___________|
 *        first edge
 *                   second edge
 *
 *
 * trig default low level
 *            ___________
 *           |           |
 *           |           |
 * __________|           |_________________
 *        first edge
 *                   second edge
 */
#define AW8697_TRG1_DEFAULT_LEVEL           1       // 1: high level; 0: low level
#define AW8697_TRG2_DEFAULT_LEVEL           1       // 1: high level; 0: low level
#define AW8697_TRG3_DEFAULT_LEVEL           1       // 1: high level; 0: low level

#define AW8697_TRG1_DUAL_EDGE               1       // 1: dual edge; 0: first edge
#define AW8697_TRG2_DUAL_EDGE               1       // 1: dual edge; 0: first edge
#define AW8697_TRG3_DUAL_EDGE               1       // 1: dual edge; 0: first edge

#define AW8697_TRG1_FIRST_EDGE_SEQ          1       // trig1: first edge waveform seq
#define AW8697_TRG1_SECOND_EDGE_SEQ         2       // trig1: second edge waveform seq
#define AW8697_TRG2_FIRST_EDGE_SEQ          1       // trig2: first edge waveform seq
#define AW8697_TRG2_SECOND_EDGE_SEQ         2       // trig2: second edge waveform seq
#define AW8697_TRG3_FIRST_EDGE_SEQ          1       // trig3: first edge waveform seq
#define AW8697_TRG3_SECOND_EDGE_SEQ         2       // trig3: second edge waveform seq


#if AW8697_TRG1_ENABLE
#define AW8697_TRG1_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG1_ENABLE
#else
#define AW8697_TRG1_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG1_DISABLE
#endif

#if AW8697_TRG2_ENABLE
#define AW8697_TRG2_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG2_ENABLE
#else
#define AW8697_TRG2_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG2_DISABLE
#endif

#if AW8697_TRG3_ENABLE
#define AW8697_TRG3_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG3_ENABLE
#else
#define AW8697_TRG3_DEFAULT_ENABLE          AW8697_BIT_TRGCFG2_TRG3_DISABLE
#endif

#if AW8697_TRG1_DEFAULT_LEVEL
#define AW8697_TRG1_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG1_POLAR_POS
#else
#define AW8697_TRG1_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG1_POLAR_NEG
#endif

#if AW8697_TRG2_DEFAULT_LEVEL
#define AW8697_TRG2_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG2_POLAR_POS
#else
#define AW8697_TRG2_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG2_POLAR_NEG
#endif

#if AW8697_TRG3_DEFAULT_LEVEL
#define AW8697_TRG3_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG3_POLAR_POS
#else
#define AW8697_TRG3_DEFAULT_POLAR           AW8697_BIT_TRGCFG1_TRG3_POLAR_NEG
#endif

#if AW8697_TRG1_DUAL_EDGE
#define AW8697_TRG1_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG1_EDGE_POS_NEG
#else
#define AW8697_TRG1_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG1_EDGE_POS
#endif

#if AW8697_TRG2_DUAL_EDGE
#define AW8697_TRG2_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG2_EDGE_POS_NEG
#else
#define AW8697_TRG2_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG2_EDGE_POS
#endif

#if AW8697_TRG3_DUAL_EDGE
#define AW8697_TRG3_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG3_EDGE_POS_NEG
#else
#define AW8697_TRG3_DEFAULT_EDGE            AW8697_BIT_TRGCFG1_TRG3_EDGE_POS
#endif

#define AW8697_RTP_NUM      6

enum aw8697_flags {
    AW8697_FLAG_NONR = 0,
    AW8697_FLAG_SKIP_INTERRUPTS = 1,
};

enum aw8697_haptic_read_write {
    AW8697_HAPTIC_CMD_READ_REG = 0,
    AW8697_HAPTIC_CMD_WRITE_REG = 1,
};


enum aw8697_haptic_work_mode {
    AW8697_HAPTIC_STANDBY_MODE = 0,
    AW8697_HAPTIC_RAM_MODE = 1,
    AW8697_HAPTIC_RTP_MODE = 2,
    AW8697_HAPTIC_TRIG_MODE = 3,
    AW8697_HAPTIC_CONT_MODE = 4,
    AW8697_HAPTIC_RAM_LOOP_MODE = 5,
};

enum aw8697_haptic_bst_mode {
    AW8697_HAPTIC_BYPASS_MODE = 0,
    AW8697_HAPTIC_BOOST_MODE = 1,
};

enum aw8697_haptic_activate_mode {
  AW8697_HAPTIC_ACTIVATE_RAM_MODE = 0,
  AW8697_HAPTIC_ACTIVATE_CONT_MODE = 1,
};

enum aw8697_haptic_vibration_style {
	AW8697_HAPTIC_VIBRATION_CRISP_STYLE = 0,
	AW8697_HAPTIC_VIBRATION_SOFT_STYLE = 1,
};

enum aw8697_haptic_cont_vbat_comp_mode {
    AW8697_HAPTIC_CONT_VBAT_SW_COMP_MODE = 0,
    AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE = 1,
};

enum aw8697_haptic_ram_vbat_comp_mode {
    AW8697_HAPTIC_RAM_VBAT_COMP_DISABLE = 0,
    AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE = 1,
};

enum aw8697_haptic_f0_flag {
    AW8697_HAPTIC_LRA_F0 = 0,
    AW8697_HAPTIC_CALI_F0 = 1,
};

enum aw8697_haptic_pwm_mode {
    AW8697_PWM_48K = 0,
    AW8697_PWM_24K = 1,
    AW8697_PWM_12K = 2,
};

enum aw8697_haptic_clock_type {
    AW8697_HAPTIC_CLOCK_CALI_F0 = 0,
    AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD=1,
};

enum aw8697_haptic_play {
    AW8697_HAPTIC_PLAY_NULL = 0,
    AW8697_HAPTIC_PLAY_ENABLE = 1,
    AW8697_HAPTIC_PLAY_STOP = 2,
    AW8697_HAPTIC_PLAY_GAIN = 8,
};

enum aw8697_haptic_cmd {
    AW8697_HAPTIC_CMD_NULL = 0,
    AW8697_HAPTIC_CMD_ENABLE = 1,
    AW8697_HAPTIC_CMD_HAPTIC = 0x0f,
    AW8697_HAPTIC_CMD_TP = 0x10,
    AW8697_HAPTIC_CMD_SYS = 0xf0,
    AW8697_HAPTIC_CMD_STOP = 255,
};
/*********************************************************
 *
 * struct
 *
 ********************************************************/
struct fileops {
    unsigned char cmd;
    unsigned char reg;
    unsigned char ram_addrh;
    unsigned char ram_addrl;
};

struct ram {
    unsigned int len;
    unsigned int check_sum;
    unsigned int base_addr;
    unsigned char version;
    unsigned char ram_shift;
    unsigned char baseaddr_shift;
};

struct haptic_ctr{
    unsigned char cnt;
    unsigned char cmd;
    unsigned char play;
    unsigned char wavseq;
    unsigned char loop;
    unsigned char gain;
    struct list_head list;
};

struct haptic_audio{
    struct mutex lock;
    struct hrtimer timer;
    struct work_struct work;
    int delay_val;
    int timer_val;
    //unsigned char cnt;
    //struct haptic_ctr data[256];
    struct haptic_ctr ctr;
    struct list_head ctr_list;
};

struct trig{
    unsigned char enable;
    unsigned char default_level;
    unsigned char dual_edge;
    unsigned char frist_seq;
    unsigned char second_seq;
};


#define AAC_RICHTAP
#ifdef AAC_RICHTAP

enum {
    RICHTAP_UNKNOWN = -1,
    RICHTAP_AW_8697 = 0x05,
};

enum {
    MMAP_BUF_DATA_VALID = 0x55,
    MMAP_BUF_DATA_FINISHED = 0xAA,
    MMAP_BUF_DATA_INVALID = 0xFF,
};

#define RICHTAP_IOCTL_GROUP 0x52
#define RICHTAP_GET_HWINFO          _IO(RICHTAP_IOCTL_GROUP, 0x03)
#define RICHTAP_SET_FREQ            _IO(RICHTAP_IOCTL_GROUP, 0x04)
#define RICHTAP_SETTING_GAIN        _IO(RICHTAP_IOCTL_GROUP, 0x05)
#define RICHTAP_OFF_MODE            _IO(RICHTAP_IOCTL_GROUP, 0x06)
#define RICHTAP_TIMEOUT_MODE        _IO(RICHTAP_IOCTL_GROUP, 0x07)
#define RICHTAP_RAM_MODE            _IO(RICHTAP_IOCTL_GROUP, 0x08)
#define RICHTAP_RTP_MODE            _IO(RICHTAP_IOCTL_GROUP, 0x09)
#define RICHTAP_STREAM_MODE         _IO(RICHTAP_IOCTL_GROUP, 0x0A)
#define RICHTAP_UPDATE_RAM          _IO(RICHTAP_IOCTL_GROUP, 0x10)
#define RICHTAP_GET_F0              _IO(RICHTAP_IOCTL_GROUP, 0x11)
#define RICHTAP_STOP_MODE           _IO(RICHTAP_IOCTL_GROUP, 0x12)

#define RICHTAP_MMAP_BUF_SIZE   1000
#define RICHTAP_MMAP_PAGE_ORDER   2
#define RICHTAP_MMAP_BUF_SUM    16

#pragma pack(4)
struct mmap_buf_format {
    uint8_t status;
    uint8_t bit;
    int16_t length;
    uint32_t reserve;
    struct mmap_buf_format *kernel_next;
    struct mmap_buf_format *user_next;
    uint8_t data[RICHTAP_MMAP_BUF_SIZE];
};
#pragma pack()

#endif
struct aw8697 {
    struct regmap *regmap;
    struct i2c_client *i2c;
    struct device *dev;
    struct input_dev *input;

    struct mutex lock;
    struct mutex rtp_lock;
	struct mutex qos_lock;
    struct hrtimer timer;
    struct work_struct vibrator_work;
    struct work_struct rtp_work;
    struct work_struct rtp_single_cycle_work;
    struct work_struct rtp_regroup_work;
    struct delayed_work ram_work;
#ifdef TIMED_OUTPUT
    struct timed_output_dev to_dev;
#else
    struct led_classdev cdev;
#endif
    struct fileops fileops;
    struct ram ram;
    bool haptic_ready;
    bool audio_ready;
    int pre_haptic_number;
    bool rtp_on;
#ifdef KERNEL_VERSION_49
    struct timeval start,end;
#else
	ktime_t kstart, kend;
#endif
    unsigned int timeval_flags;
    unsigned int osc_cali_flag;
    unsigned long int microsecond;
    unsigned int sys_frequency;
    unsigned int rtp_len;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	unsigned int sin_num;
	size_t sin_data_lenght;
	unsigned int sin_add_flag;
#endif
    int reset_gpio;
    int irq_gpio;
    int device_id;

    unsigned char hwen_flag;
    unsigned char flags;
    unsigned char chipid;

    unsigned char play_mode;

    unsigned char activate_mode;

	unsigned char vibration_style;

    unsigned char auto_boost;

    int state;
    int duration;
    int amplitude;
    int index;
    int vmax;
    int gain;
    int level;
    unsigned int gun_type;      //hch 20190917
    unsigned int bullet_nr; //hch 20190917
    unsigned int gun_mode;

    unsigned char seq[AW8697_SEQUENCER_SIZE];
    unsigned char loop[AW8697_SEQUENCER_SIZE];

    unsigned int rtp_cnt;
    unsigned int rtp_file_num;
    unsigned int rtp_loop;
    unsigned int rtp_cycle_flag;
    unsigned int rtp_serial[AW8697_RTP_NUM];

    unsigned char rtp_init;
    unsigned char ram_init;
    unsigned char rtp_routine_on;

    unsigned int f0;
    unsigned int f0_pre;
    unsigned int cont_f0;
    unsigned int cont_td;
    unsigned int cont_zc_thr;
    unsigned char cont_drv_lvl;
    unsigned char cont_drv_lvl_ov;
    unsigned char cont_num_brk;
    unsigned char max_pos_beme;
    unsigned char max_neg_beme;
    unsigned char f0_cali_flag;

    unsigned char ram_vbat_comp;
    unsigned int vbat;
    unsigned int lra;
    unsigned char haptic_real_f0;

    unsigned int ram_test_flag_0;
    unsigned int ram_test_flag_1;

    struct trig trig[AW8697_TRIG_NUM];

    struct haptic_audio haptic_audio;
    unsigned int clock_standard_OSC_lra_rtim_code;
    unsigned int clock_system_f0_cali_lra;
#ifdef AAC_RICHTAP
    uint8_t *rtp_ptr;
    struct mmap_buf_format *start_buf;
    struct work_struct haptic_rtp_work;
        //wait_queue_head_t doneQ;
    bool done_flag;
    bool haptic_rtp_mode;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
    struct work_struct  motor_old_test_work;
    unsigned int motor_old_test_mode;
#endif
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	int boot_mode;
#endif
};

struct aw8697_container{
    int len;
    unsigned char data[];
};


/*********************************************************
 *
 * ioctl
 *
 ********************************************************/
struct aw8697_seq_loop {
    unsigned char loop[AW8697_SEQUENCER_SIZE];
};

struct aw8697_que_seq {
    unsigned char index[AW8697_SEQUENCER_SIZE];
};


#define AW8697_HAPTIC_IOCTL_MAGIC         'h'

#define AW8697_HAPTIC_SET_QUE_SEQ         _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 1, struct aw8697_que_seq*)
#define AW8697_HAPTIC_SET_SEQ_LOOP        _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 2, struct aw8697_seq_loop*)
#define AW8697_HAPTIC_PLAY_QUE_SEQ        _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 3, unsigned int)
#define AW8697_HAPTIC_SET_BST_VOL         _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 4, unsigned int)
#define AW8697_HAPTIC_SET_BST_PEAK_CUR    _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 5, unsigned int)
#define AW8697_HAPTIC_SET_GAIN            _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 6, unsigned int)
#define AW8697_HAPTIC_PLAY_REPEAT_SEQ     _IOWR(AW8697_HAPTIC_IOCTL_MAGIC, 7, unsigned int)

#ifdef OPLUS_FEATURE_CHG_BASIC
#define F0_VAL_MAX_0815                     1800
#define F0_VAL_MIN_0815                     1600
#define F0_VAL_MAX_081538                   1600
#define F0_VAL_MIN_081538                   1400
#define F0_VAL_MAX_0832                     2350
#define F0_VAL_MIN_0832                     2250
#define F0_VAL_MAX_0833                     2380
#define F0_VAL_MIN_0833                     2260
#define F0_VAL_MAX_0619                     1780
#define F0_VAL_MIN_0619                     1650
#define F0_VAL_MAX_1040                     1780
#define F0_VAL_MIN_1040                     1650

#define AW8697_HAPTIC_BASE_VOLTAGE          6000
#define AW8697_HAPTIC_MAX_VOLTAGE           10000
#define AW8697_HAPTIC_LOW_LEVEL_VOL         800
#define AW8697_HAPTIC_LOW_LEVEL_REG_VAL     0
#define AW8697_HAPTIC_MEDIUM_LEVEL_VOL      1600
#define AW8697_HAPTIC_MEDIUM_LEVEL_REG_VAL  0
#define AW8697_HAPTIC_HIGH_LEVEL_VOL        2500

//#define AW8697_HAPTIC_RAM_VBAT_COMP_GAIN  0x80

#define AW8697_WAVEFORM_INDEX_TRADITIONAL_1        1
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_2        2
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_3        3
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_4        4
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_5        5
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_6        6
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_7        7
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_8        8
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_9        9
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_10       10
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_11       11
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_12       12
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_13       13
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_14       14
#define AW8697_WAVEFORM_INDEX_TRADITIONAL_15       15

#define AW8697_RTP_LONG_SOUND_INDEX                44
#define AUDIO_READY_STATUS  1024
#define RINGTONES_START_INDEX   1
#define RINGTONES_END_INDEX  40
#define RINGTONES_SIMPLE_INDEX  48
#define RINGTONES_PURE_INDEX    49
#ifdef CONFIG_OPLUS_HAPTIC_OOS
#define RINGTONES_NEWLIFE_INDEX 98
#endif
#define NEW_RING_START          120
#define NEW_RING_END            160
#define OS12_NEW_RING_START     70
#define OS12_NEW_RING_END       89
#define OPLUS_RING_START       161
#define OPLUS_RING_END         167

#define OPLUS_START_INDEX       201
#define OPLUS_END_INDEX         326
#define OPLUS_RING_START_INDEX  152
#define OPLUS_RING_END_INDEX    231

#define AW8697_WAVEFORM_INDEX_CS_PRESS             16
#ifdef CONFIG_OPLUS_HAPTIC_OOS
#define AW8697_WAVEFORM_INDEX_TRANSIENT            8
#else
#define AW8697_WAVEFORM_INDEX_TRANSIENT            8
#endif
#ifdef CONFIG_OPLUS_HAPTIC_OOS
#ifdef CONFIG_ARCH_LITO
#define AW8697_WAVEFORM_INDEX_SINE_CYCLE           10
#else
#define AW8697_WAVEFORM_INDEX_SINE_CYCLE           9
#endif /* CONFIG_ARCH_LITO */
#else
#define AW8697_WAVEFORM_INDEX_SINE_CYCLE           9
#endif /* CONFIG_OPLUS_HAPTIC_OOS */
#define AW8697_WAVEFORM_INDEX_HIGH_TEMP            51
#define AW8697_WAVEFORM_INDEX_OLD_STEADY           52
#define AW8697_WAVEFORM_INDEX_LISTEN_POP           53


enum aw8697_haptic_custom_level {
    HAPTIC_CUSTOM_LEVEL_WEAK = 0,  // 3V
    HAPTIC_CUSTOM_LEVEL_MEDIUM = 1,// 6V
    HAPTIC_CUSTOM_LEVEL_STRONG = 2,// 9V
};


enum aw8697_haptic_custom_vibration_mode {
    VIBRATION_MODE_TRADITIONAL = 0,
    VIBRATION_MODE_RING = 1,
    VIBRATION_MODE_GAME = 2,
};


enum aw8697_haptic_motor_old_test_mode {
    MOTOR_OLD_TEST_TRANSIENT = 1,
    MOTOR_OLD_TEST_STEADY = 2,
    MOTOR_OLD_TEST_HIGH_TEMP_HUMIDITY = 3,
    MOTOR_OLD_TEST_LISTEN_POP = 4,
    MOTOR_OLD_TEST_ALL_NUM,
};


#endif

#endif

