// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <soc/oplus/healthinfo.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/ratelimit.h>
#include <linux/blkdev.h>
#ifdef CONFIG_OPLUS_MEM_MONITOR
#include <linux/healthinfo/memory_monitor.h>
#endif
/*#ifdef OPLUS_FEATURE_UFSPLUS
#if defined(CONFIG_UFSFEATURE)
#include "../../../drivers/scsi/oufs/ufsfeature.h"
#endif
#endif
*/
#include <linux/blkdev.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>
#ifdef CONFIG_PROCESS_RECLAIM_ENHANCE
#include <linux/process_mm_reclaim.h>
#endif
#ifdef CONFIG_KSWAPD_DEBUG_STATISTICS
#include <soc/oplus/lowmem_dbg.h>
#endif
#include <linux/version.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>


#define BUFFER_SIZE_S 256
#define BUFFER_SIZE_M 512
#define BUFFER_SIZE_L 2048
#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif
#ifdef CONFIG_OPLUS_JANK
extern u32 get_cpu_load(u32 win_cnt, struct cpumask *mask);
#endif

struct sched_stat_para sched_para[OHM_SCHED_TOTAL];
static char *sched_list[OHM_TYPE_TOTAL] = {
        /* SCHED_STATS 0 -11 */
        "iowait",
        "sched_latency",
        "fsync",
        "emmcio",
        "dstate",
        "sched_default_05",
        "sched_default_06",
        "sched_default_07",
        "sched_default_08",
        "sched_default_09",
        "sched_default_10",
        "sched_default_11",
        /* OTHER_TYPE 12 - */
        "cur_cpu_load",
        "memory_monitor",
        "io_panic",
        "svm_monitor",
        "rlimit_monitor",
        "ionwait_monitor",
	"mem_vma_alloc_err",
	"blk_monitor"
};

/******  Action  ******/
#define MAX_OHMEVENT_PARAM 4
#define OH_MSG_LEN 256
static struct kobject *ohm_kobj = NULL;
static struct work_struct ohm_detect_ws;
static char *ohm_detect_env[MAX_OHMEVENT_PARAM] = { "OHMACTION=uevent", NULL };
static bool ohm_action_ctrl = false;
static char msg_buf[OH_MSG_LEN] = {0};

void ohm_action_trig(int type)
{
        if (!ohm_action_ctrl) {
                ohm_err_deferred("ctrl off\n");
                return;
        }
        ohm_debug_deferred("%s trig action\n", sched_list[type]);
        if (OHM_MEM_MON == type || OHM_SCHED_FSYNC == type) {
                if (!ohm_kobj) {
                        ohm_err_deferred("kobj NULL\n");
                        return;
                }
                sprintf(ohm_detect_env[1], "OHMTYPE=%s", sched_list[type]);
		ohm_detect_env[MAX_OHMEVENT_PARAM - 2] = NULL;
                ohm_detect_env[MAX_OHMEVENT_PARAM - 1] = NULL;
                schedule_work(&ohm_detect_ws);
        }
}

void ohm_action_trig_with_msg(int type, char *msg)
{
	int len;

        if (!ohm_action_ctrl) {
                ohm_err("ctrl off\n");
                return;
        }

	if (!ohm_kobj) {
                ohm_err("kobj NULL\n");
                return;
        }
        if (OHM_SVM_MON == type || OHM_RLIMIT_MON == type || OHM_MEM_VMA_ALLOC_ERR == type) {
                sprintf(ohm_detect_env[1], "OHMTYPE=%s", sched_list[type]);
		len = snprintf(msg_buf, OH_MSG_LEN-1, "OHMMSG=%s", msg);
		msg_buf[len] = '\0';
		ohm_detect_env[MAX_OHMEVENT_PARAM - 2] = msg_buf;
		ohm_detect_env[MAX_OHMEVENT_PARAM - 1] = NULL;
                schedule_work(&ohm_detect_ws);
        }
}

void ohm_detect_work(struct work_struct *work)
{
        ohm_debug("Uevent Para: %s, %s\n", ohm_detect_env[0], ohm_detect_env[1]);
        kobject_uevent_env(ohm_kobj, KOBJ_CHANGE, ohm_detect_env);
        ohm_debug("Uevent Done!\n");
}

void ohm_action_init(void)
{
        int i = 0;
        for(i = 1; i < MAX_OHMEVENT_PARAM - 2; i++) {
                ohm_detect_env[i] = kzalloc(50, GFP_KERNEL);
                if (!ohm_detect_env[i]) {
                        ohm_err("kzalloc ohm uevent param failed\n");
                        goto ohm_action_init_free_memory;
                }
        }

        ohm_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
        if (!ohm_kobj) {
                goto ohm_action_init_kobj_failed;
        }
        INIT_WORK(&ohm_detect_ws, ohm_detect_work);
        ohm_debug("Success !\n");
        return;

ohm_action_init_kobj_failed:
        ohm_err("Ohm kobj init err\n");
ohm_action_init_free_memory:
        for(i--; i > 0; i--) {
                kfree(ohm_detect_env[i]);
        }
        ohm_err("Failed !\n");
}

/******  Sched record  ******/
static inline void ohm_sched_stat_record_common(struct sched_stat_para *sched_stat,struct sched_stat_common *stat_common, u64 delta_ms)
{
        stat_common->total_ms += delta_ms;
        stat_common->total_cnt++;

        if (delta_ms > stat_common->max_ms) {
            stat_common->max_ms = delta_ms;
        }
        if (delta_ms >= sched_stat->high_thresh_ms) {
            stat_common->high_cnt++;
        } else if (delta_ms >= sched_stat->low_thresh_ms) {
            stat_common->low_cnt++;
        }
}

#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif
void ohm_schedstats_record(int sched_type, struct task_struct *task, u64 delta_ms)
{
    struct sched_stat_para *sched_stat = &sched_para[sched_type];
    static DEFINE_RATELIMIT_STATE(ratelimit, 60*HZ, 1);
	struct long_wait_record *plwr;
	struct timespec64 ts;
	u32 index;

    if (unlikely(!sched_stat->ctrl)){
        return;
    }

    sched_stat->delta_ms = delta_ms;
    ohm_sched_stat_record_common(sched_stat, &sched_stat->all, delta_ms);

    if (task_is_fg(task)) {
        ohm_sched_stat_record_common(sched_stat, &sched_stat->fg, delta_ms);
        if (unlikely(delta_ms >= sched_stat->high_thresh_ms)){
             if (sched_para[sched_type].logon  && __ratelimit(&ratelimit)) {
                        ohm_debug_deferred("[%s / %s] high_cnt, delay = %llu ms\n",
                                sched_list[sched_type], "fg", delta_ms);
                }
             if (sched_para[sched_type].trig)
                ohm_action_trig(sched_type);
        }
    }
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	if (test_task_ux(task)){
        ohm_sched_stat_record_common(sched_stat, &sched_stat->ux, delta_ms);
    }
#endif
	if (unlikely(delta_ms >= sched_stat->low_thresh_ms)) {
		index = (u32)atomic_inc_return(&sched_stat->lwr_index);
		plwr = &sched_stat->last_n_lwr[index & LWR_MASK];
		plwr->pid = (u32)task->pid;

		ktime_get_real_ts64(&ts);
		plwr->timestamp = (u64)ts.tv_sec;
		plwr->timestamp_ns = (u64)ts.tv_nsec;

		plwr->ms = delta_ms;
	}

    return;
}

/****  Ctrl init  ****/
/*
        CTRL - TOTAL -32
        CTRL0:          logon
iowait record;

        CTRL1:
sched latency;

        CTRL2:          logon           trig
fsync record;

        CTRL3:
emmcio record;
        ******
        ******
        CTRL12:
cpu load cur;

        CTRL13:         logon           trig
mem mon;
......;
......;
        CTRL31:
                ......;
*/
#define OHM_LIST_MAGIC          0x5a000000
#define OHM_CTRL_MAX            32
#define OHM_INT_MAX             20
#define OHM_CTRL_IOWAIT         BIT(OHM_SCHED_IOWAIT)
#define OHM_CTRL_SCHEDLATENCY   BIT(OHM_SCHED_SCHEDLATENCY)
#define OHM_CTRL_FSYNC          BIT(OHM_SCHED_FSYNC)
#define OHM_CTRL_EMMCIO         BIT(OHM_SCHED_EMMCIO)
#define OHM_CTRL_DSTATE         BIT(OHM_SCHED_DSTATE)
#define OHM_CTRL_SCHEDTOTAL     OHM_CTRL_EMMCIO | OHM_CTRL_FSYNC | OHM_CTRL_SCHEDLATENCY | OHM_CTRL_IOWAIT | OHM_CTRL_DSTATE
#define OHM_CTRL_CPU_CUR        BIT(OHM_CPU_LOAD_CUR)
#define OHM_CTRL_MEMMON         BIT(OHM_MEM_MON)
#define OHM_CTRL_IOPANIC_MON    BIT(OHM_IOPANIC_MON)
#define OHM_CTRL_SVM		BIT(OHM_SVM_MON)
#define OHM_CTRL_RLIMIT		BIT(OHM_RLIMIT_MON)
#define OHM_CTRL_IONMON         BIT(OHM_ION_MON)
#define OHM_CTRL_BLKMON		BIT(OHM_BLK_MON)


/*
ohm_ctrl_list    = 0x5a0fffff
ohm_logon_list = 0x5a002005
ohm_trig_list    = 0x5a002000
*/

/*Default*/
static int ohm_ctrl_list = OHM_LIST_MAGIC | OHM_CTRL_CPU_CUR | OHM_CTRL_MEMMON |  OHM_CTRL_IONMON | OHM_CTRL_SCHEDTOTAL | OHM_CTRL_BLKMON;
static int ohm_logon_list = OHM_LIST_MAGIC;
static int ohm_trig_list = OHM_LIST_MAGIC;

bool ohm_cpu_ctrl = true;
bool ohm_cpu_logon = false;
bool ohm_cpu_trig = false;

bool ohm_memmon_ctrl = true;
bool ohm_memmon_logon = false;
bool ohm_memmon_trig = false;

bool ohm_iopanic_mon_ctrl = true;
bool ohm_iopanic_mon_logon = false;
bool ohm_iopanic_mon_trig = false;

bool ohm_ionmon_ctrl = true;
bool ohm_ionmon_logon = false;
bool ohm_ionmon_trig = false;

bool ohm_blkmon_ctrl = true;
bool ohm_blkmon_logon = false;
bool ohm_blkmon_trig = false;

/******  Para Update  *****/
#define LOW_THRESH_MS_DEFAULT   100
#define HIGH_THRESH_MS_DEFAULT  500
/* low thresh 10~1000ms*/
#define LOW_THRESH_MS_LOW       10
#define LOW_THRESH_MS_HIGH      1000
/* high thresh 100~5000ms*/
#define HIGH_THRESH_MS_LOW      50
#define HIGH_THRESH_MS_HIGH     5000

struct thresh_para {
        int l_ms;
        int h_ms;
};

struct thresh_para ohm_thresh_para[OHM_SCHED_TOTAL] = {
        { LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
        { LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
        { LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
        { 100, 200},
        { LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
};

void ohm_para_update(void)
{
        int i;
        for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ ) {
                if ( ohm_thresh_para[i].l_ms < LOW_THRESH_MS_LOW
                        || ohm_thresh_para[i].l_ms > LOW_THRESH_MS_HIGH
                        || ohm_thresh_para[i].h_ms < HIGH_THRESH_MS_LOW
                        || ohm_thresh_para[i].h_ms > HIGH_THRESH_MS_HIGH ) {
                        /********** Legal Check **********/
                        ohm_err("Para illegal: sched_type %s, l_ms %d, h_ms %d\n",
                                sched_list[i], ohm_thresh_para[i].l_ms, ohm_thresh_para[i].h_ms);
                        ohm_thresh_para[i].l_ms = LOW_THRESH_MS_DEFAULT;
                        ohm_thresh_para[i].h_ms = HIGH_THRESH_MS_DEFAULT;
                        return;
                }
                sched_para[i].low_thresh_ms = ohm_thresh_para[i].l_ms;
                sched_para[i].high_thresh_ms = ohm_thresh_para[i].h_ms;
        }
        ohm_debug("Success update ohm_para!\n");
}

/****  Init  ****/
void ohm_trig_init(void)
{
        int i;
        ohm_memmon_trig = (ohm_trig_list & OHM_CTRL_MEMMON) ? true : false;
        ohm_cpu_trig = (ohm_trig_list & OHM_CTRL_CPU_CUR) ? true : false;
        ohm_iopanic_mon_trig = (ohm_trig_list & OHM_CTRL_IOPANIC_MON) ? true : false;
		ohm_ionmon_trig = (ohm_trig_list & OHM_CTRL_IONMON) ? true : false;
		ohm_blkmon_trig = (ohm_trig_list & OHM_CTRL_BLKMON) ? true : false;
        for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ ) {
                sched_para[i].trig = (ohm_trig_list & BIT(i)) ? true : false;
		}
        return;
}

void ohm_logon_init(void)
{
        int i;
        ohm_cpu_logon = (ohm_logon_list & OHM_CTRL_CPU_CUR) ? true : false;
        ohm_memmon_logon = (ohm_logon_list & OHM_CTRL_MEMMON) ? true : false;
        ohm_iopanic_mon_logon = (ohm_logon_list & OHM_CTRL_IOPANIC_MON) ? true : false;
		ohm_ionmon_logon = (ohm_logon_list & OHM_CTRL_IONMON) ? true : false;
		ohm_blkmon_logon = (ohm_logon_list & OHM_CTRL_BLKMON) ? true : false;
        for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ ) {
                sched_para[i].logon = (ohm_logon_list & BIT(i)) ? true : false;
		}
        return;
}

void ohm_ctrl_init(void)
{
        int i;
        ohm_cpu_ctrl = (ohm_ctrl_list & OHM_CTRL_CPU_CUR) ? true : false;
        ohm_memmon_ctrl = (ohm_ctrl_list & OHM_CTRL_MEMMON) ? true : false;
        ohm_iopanic_mon_ctrl = (ohm_ctrl_list & OHM_CTRL_IOPANIC_MON) ? true : false;
		ohm_ionmon_ctrl = (ohm_ctrl_list & OHM_CTRL_IONMON) ? true : false;
		ohm_blkmon_ctrl = (ohm_ctrl_list & OHM_CTRL_BLKMON) ? true : false;
        for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ ) {
                sched_para[i].ctrl = (ohm_ctrl_list & BIT(i)) ? true : false;
		}
        return;
}

static inline void _ohm_para_init(struct sched_stat_para *sched_para)
{
	   sched_para->delta_ms = 0;
	   memset(&sched_para->all, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->ux, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->fg, 0 , sizeof(struct sched_stat_common));

	   return;
}

void ohm_para_init(void)
{
        int i;
        for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ ) {
               _ohm_para_init(&sched_para[i]);
               sched_para[i].low_thresh_ms = 100;
               sched_para[i].high_thresh_ms = 500;
        }
        sched_para[OHM_SCHED_EMMCIO].low_thresh_ms = 100;
        sched_para[OHM_SCHED_EMMCIO].high_thresh_ms = 200;
        ohm_ctrl_init();
        ohm_logon_init();
        ohm_trig_init();
        ohm_debug("origin list: ctrl 0x%08x, logon 0x%08x, trig 0x%08x\n", ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
        return;
}

#define LATENCY_STRING_FORMAT(BUF, MODULE, SCHED_STAT) sprintf(BUF, \
        #MODULE"_ctrl: %s\n"#MODULE"_logon: %s\n"#MODULE"_trig: %s\n" \
        #MODULE"_delta_ms: %llu\n"#MODULE"_low_thresh_ms: %d\n"#MODULE"_high_thresh_ms: %d\n" \
        #MODULE"_max_ms: %llu\n"#MODULE"_high_cnt: %llu\n"#MODULE"_low_cnt: %llu\n" \
        #MODULE"_total_ms: %llu\n"#MODULE"_total_cnt: %llu\n" \
        #MODULE"_fg_max_ms: %llu\n"#MODULE"_fg_high_cnt: %llu\n"#MODULE"_fg_low_cnt: %llu\n" \
        #MODULE"_fg_total_ms: %llu\n"#MODULE"_fg_total_cnt: %llu\n" \
        #MODULE"_ux_max_ms: %llu\n"#MODULE"_ux_high_cnt: %llu\n"#MODULE"_ux_low_cnt: %llu\n" \
        #MODULE"_ux_total_ms: %llu\n"#MODULE"_ux_total_cnt: %llu\n", \
        SCHED_STAT->ctrl ? "true":"false", \
        SCHED_STAT->logon ? "true":"false", \
        SCHED_STAT->trig ? "true":"false", \
        SCHED_STAT->delta_ms, \
        SCHED_STAT->low_thresh_ms, \
        SCHED_STAT->high_thresh_ms, \
        SCHED_STAT->all.max_ms, \
        SCHED_STAT->all.high_cnt, \
        SCHED_STAT->all.low_cnt, \
        SCHED_STAT->all.total_ms, \
        SCHED_STAT->all.total_cnt, \
        SCHED_STAT->fg.max_ms, \
        SCHED_STAT->fg.high_cnt, \
        SCHED_STAT->fg.low_cnt, \
        SCHED_STAT->fg.total_ms, \
        SCHED_STAT->fg.total_cnt, \
        SCHED_STAT->ux.max_ms, \
        SCHED_STAT->ux.high_cnt, \
        SCHED_STAT->ux.low_cnt, \
        SCHED_STAT->ux.total_ms, \
        SCHED_STAT->ux.total_cnt)

#define LWR_STRING_FORMAT(BUF, LENGTH, SCHED_STAT)														\
do {																									\
	int index;																							\
																										\
	for (index = 0; index < LWR_SIZE; index++) {														\
		LENGTH += sprintf(BUF + LENGTH, "long wait record slot %d: %u %u %llu %llu %u\n", index,		\
						SCHED_STAT->last_n_lwr[index].pid,												\
						SCHED_STAT->last_n_lwr[index].priv,												\
						SCHED_STAT->last_n_lwr[index].timestamp,										\
						SCHED_STAT->last_n_lwr[index].timestamp_ns,										\
						SCHED_STAT->last_n_lwr[index].ms);												\
	}																									\
}																										\
while(0)

static inline ssize_t sched_data_to_user(char __user *buff, size_t count, loff_t *off, char *format_str, int len)
{
    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }
    if (copy_to_user(buff, format_str, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;

    return (len < count ? len : count);
}

#ifdef CONFIG_OPLUS_JANK
static int ohm_get_cpuload(void)
{
	unsigned int cpuload = 0;
	int i;
	struct cpumask mask;
	cpumask_clear(&mask);
	for (i = 0; i < 7; i++)
		cpumask_set_cpu(i, &mask);
	cpuload = get_cpu_load(1, &mask);
	return cpuload;
}
#endif

/******  Cur cpuloading  ******/
static ssize_t cpu_load_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_S] = {0};
        int len = 0;
#ifdef CONFIG_OPLUS_JANK
        int load = ohm_get_cpuload();
#else
        int load = ohm_get_cur_cpuload(ohm_cpu_ctrl);
#endif
        if(load < 0)
                load = 0;
        len = sprintf( page, "cur_cpuloading: %d\n""cur_cpu_ctrl: %s\n""cur_cpu_logon: %s\n""cur_cpu_trig: %s\n",
                load, (ohm_cpu_ctrl ? "true" : "false"), (ohm_cpu_logon ? "true" : "false"), (ohm_cpu_trig ? "true" : "false"));

        return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_cpu_load_fops = {
        .read = cpu_load_read,
};

/******  Sched latency stat  *****/
static ssize_t sched_latency_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	int ret_len = 0;
	struct sched_stat_para *sched_stat = &sched_para[OHM_SCHED_SCHEDLATENCY];
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
	if (!page) {
		return -ENOMEM;
	}
	len = LATENCY_STRING_FORMAT(page, sched_latency, sched_stat);
	LWR_STRING_FORMAT(page, len, sched_stat);
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}

static const struct file_operations proc_sched_latency_fops = {
       .read = sched_latency_read,
};

/****** Sched iowait stat  *****/
static ssize_t iowait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	int ret_len = 0;
	struct sched_stat_para *sched_stat = &sched_para[OHM_SCHED_IOWAIT];
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
	if (!page) {
		return -ENOMEM;
	}
	len = LATENCY_STRING_FORMAT(page, iowait, sched_stat);
	LWR_STRING_FORMAT(page, len, sched_stat);
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}

static const struct file_operations proc_iowait_fops = {
       .read = iowait_read,
};

/****** Sched sync wait stat  ******/
static ssize_t fsync_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	int ret_len = 0;
	struct sched_stat_para *sched_stat = &sched_para[OHM_SCHED_FSYNC];
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
        if (!page) {
                return -ENOMEM;
        }

	len = LATENCY_STRING_FORMAT(page, fsync, sched_stat);
	LWR_STRING_FORMAT(page, len, sched_stat);
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}

static const struct file_operations proc_fsync_wait_fops = {
       .read = fsync_wait_read,
};

/****** emcdrv_iowait stat  ******/
/* Emmc - 1 ; Ufs - 2 */
int ohm_flash_type = OHM_FLASH_TYPE_UFS;
static ssize_t emmcio_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	int ret_len = 0;
	struct sched_stat_para *sched_stat = &sched_para[OHM_SCHED_EMMCIO];
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
	if (!page) {
                return -ENOMEM;
        }
	len = LATENCY_STRING_FORMAT(page, emmcio, sched_stat);
	LWR_STRING_FORMAT(page, len, sched_stat);
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}


static const struct file_operations proc_emmcio_fops = {
       .read = emmcio_read,
};

/****** dstat statistics  ******/
static ssize_t dstate_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        int len = 0;
	int ret_len = 0;
        struct sched_stat_para *sched_stat = &sched_para[OHM_SCHED_DSTATE];
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
	if (!page) {
                return -ENOMEM;
        }
	len = LATENCY_STRING_FORMAT(page, dstate, sched_stat);
	LWR_STRING_FORMAT(page, len, sched_stat);
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}

static const struct file_operations proc_dstate_fops = {
       .read = dstate_read,

};

/******  mem monitor read  ******/
#ifdef CONFIG_OPLUS_MEM_MONITOR
static ssize_t alloc_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[1024] = {0};
        int len = 0;

        len = sprintf(page, "total_alloc_wait_h_cnt: %lld\n""total_alloc_wait_l_cnt: %lld\n"
                "ux_alloc_wait_h_cnt: %lld\n""ux_alloc_wait_l_cnt: %lld\n"
                "ux_alloc_wait_max_ms: %lld\n""ux_alloc_wait_max_order: %lld\n"
                "fg_alloc_wait_h_cnt: %lld\n""fg_alloc_wait_l_cnt: %lld\n"
                "total_alloc_wait_max_ms: %lld\n""total_alloc_wait_max_order: %lld\n"
                "fg_alloc_wait_max_ms: %lld\n""fg_alloc_wait_max_order: %lld\n"
                "alloc_wait_ctrl: %s\n""alloc_wait_logon: %s\n""alloc_wait_trig: %s\n",
                allocwait_para.total_alloc_wait.high_cnt,allocwait_para.total_alloc_wait.low_cnt,
                allocwait_para.ux_alloc_wait.high_cnt,allocwait_para.ux_alloc_wait.low_cnt,
                allocwait_para.ux_alloc_wait.max_ms,allocwait_para.ux_alloc_wait_max_order,
                allocwait_para.fg_alloc_wait.high_cnt,allocwait_para.fg_alloc_wait.low_cnt,
                allocwait_para.total_alloc_wait.max_ms,allocwait_para.total_alloc_wait_max_order,
                allocwait_para.fg_alloc_wait.max_ms,allocwait_para.fg_alloc_wait_max_order,
                ohm_memmon_ctrl ? "true":"false", ohm_memmon_logon ? "true":"false", ohm_memmon_trig ? "true":"false");

	LWR_STRING_FORMAT(page, len, (&allocwait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_alloc_wait_fops = {
       .read = alloc_wait_read,
};


static ssize_t ion_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_L] = {0};
        int len = 0;

        len = sprintf(page, "total_ion_wait_h_cnt: %lld\n""total_ion_wait_l_cnt: %lld\n"
                "fg_ion_wait_h_cnt: %lld\n""fg_ion_wait_l_cnt: %lld\n"
                "ux_ion_wait_h_cnt: %lld\n""ux_ion_wait_l_cnt: %lld\n"
                "total_ion_wait_max_ms: %lld\n"
                "ion_wait_ctrl: %s\n""ion_wait_logon: %s\n""ion_wait_trig: %s\n",
                ionwait_para.total_ion_wait.high_cnt,ionwait_para.total_ion_wait.low_cnt,
                ionwait_para.fg_ion_wait.high_cnt,ionwait_para.fg_ion_wait.low_cnt,
                ionwait_para.ux_ion_wait.high_cnt,ionwait_para.ux_ion_wait.low_cnt,
                ionwait_para.total_ion_wait.max_ms,
                ohm_ionmon_ctrl ? "true":"false", ohm_ionmon_logon ? "true":"false", ohm_ionmon_trig ? "true":"false");

	LWR_STRING_FORMAT(page, len, (&ionwait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_ion_wait_fops = {
       .read = ion_wait_read,
};
#endif /*CONFIG_OPLUS_MEM_MONITOR*/

#ifdef CONFIG_OPLUS_BLK_MONITOR
extern struct blk_wait_para q2i_wait_para;
extern struct blk_wait_para i2d_wait_para;
extern struct blk_wait_para q2c_wait_para;
extern struct blk_wait_para d2c_wait_para;
extern void blkmon_init(void);

static ssize_t blk_q2c_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_L];
	int len = 0;

	len = sprintf(page, "wait_ctrl: %s\n""wait_logon: %s\n""wait_trig: %s\n"
				"wait_h_cnt: %llu\n""wait_l_cnt: %llu\n""wait_max_ms: %llu\n",
				ohm_blkmon_ctrl ? "true" : "false",
				ohm_blkmon_logon ? "true" : "false",
				ohm_blkmon_trig ? "true" : "false",
				q2c_wait_para.wait_stat.high_cnt,
				q2c_wait_para.wait_stat.low_cnt,
				q2c_wait_para.wait_stat.max_ms);
	LWR_STRING_FORMAT(page, len, (&q2c_wait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_blk_q2c_wait_fops = {
	.read = blk_q2c_wait_read,
};

static ssize_t blk_q2i_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_L];
	int len = 0;

	len = sprintf(page, "wait_ctrl: %s\n""wait_logon: %s\n""wait_trig: %s\n"
				"wait_h_cnt: %llu\n""wait_l_cnt: %llu\n""wait_max_ms: %llu\n",
				ohm_blkmon_ctrl ? "true" : "false",
				ohm_blkmon_logon ? "true" : "false",
				ohm_blkmon_trig ? "true" : "false",
				q2i_wait_para.wait_stat.high_cnt,
				q2i_wait_para.wait_stat.low_cnt,
				q2i_wait_para.wait_stat.max_ms);
	LWR_STRING_FORMAT(page, len, (&q2i_wait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_blk_q2i_wait_fops = {
	.read = blk_q2i_wait_read,
};

static ssize_t blk_i2d_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_L];
	int len = 0;

	len = sprintf(page, "wait_ctrl: %s\n""wait_logon: %s\n""wait_trig: %s\n"
				"wait_h_cnt: %llu\n""wait_l_cnt: %llu\n""wait_max_ms: %llu\n",
				ohm_blkmon_ctrl ? "true" : "false",
				ohm_blkmon_logon ? "true" : "false",
				ohm_blkmon_trig ? "true" : "false",
				i2d_wait_para.wait_stat.high_cnt,
				i2d_wait_para.wait_stat.low_cnt,
				i2d_wait_para.wait_stat.max_ms);
	LWR_STRING_FORMAT(page, len, (&i2d_wait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_blk_i2d_wait_fops = {
	.read = blk_i2d_wait_read,
};

static ssize_t blk_d2c_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_L];
	int len = 0;

	len = sprintf(page, "wait_ctrl: %s\n""wait_logon: %s\n""wait_trig: %s\n"
				"wait_h_cnt: %llu\n""wait_l_cnt: %llu\n""wait_max_ms: %llu\n",
				ohm_blkmon_ctrl ? "true" : "false",
				ohm_blkmon_logon ? "true" : "false",
				ohm_blkmon_trig ? "true" : "false",
				d2c_wait_para.wait_stat.high_cnt,
				d2c_wait_para.wait_stat.low_cnt,
				d2c_wait_para.wait_stat.max_ms);
	LWR_STRING_FORMAT(page, len, (&d2c_wait_para));

	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_blk_d2c_wait_fops = {
	.read = blk_d2c_wait_read,
};
#endif /* CONFIG_OPLUS_BLK_MONITOR */

/******  Proc para   ******/
static ssize_t ohm_para_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_S] = {0};
        int len = 0;

        len = sprintf( page, "action: %s\n""ctrl: 0x%08x\n""logon: 0x%08x\n""trig: 0x%08x\n",
                (ohm_action_ctrl ? "true":"false"), ohm_ctrl_list, ohm_logon_list, ohm_trig_list);

        return sched_data_to_user(buff, count, off, page, len);
}

static ssize_t ohm_para_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
        char write_data[32] = {0};
        char ctrl_list[32] = {0};
	int action_ctrl;

	if(len > 31)
		return -EFAULT;

        if (copy_from_user(&write_data, buff, len)) {
                ohm_err("write error.\n");
                return -EFAULT;
        }
        write_data[len] = '\0';
        if (write_data[len - 1] == '\n') {
                write_data[len - 1] = '\0';
        }

        if (0 == strncmp(write_data, "ohmctrl", 7)) {
                strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
                ctrl_list[OHM_INT_MAX] = '\0';
                ohm_ctrl_list = (int)simple_strtol(ctrl_list, NULL, 10);
                ohm_ctrl_init();
        } else if (0 == strncmp(write_data, "ohmlogon", 8)) {
                strncpy(ctrl_list, &write_data[8], OHM_INT_MAX);
                ctrl_list[OHM_INT_MAX] = '\0';
                ohm_logon_list = (int)simple_strtol(ctrl_list, NULL, 10);
                ohm_logon_init();
        } else if (0 == strncmp(write_data, "ohmtrig", 7)) {
                strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
                ctrl_list[OHM_INT_MAX] = '\0';
                ohm_trig_list = (int)simple_strtol(ctrl_list, NULL, 10);
                ohm_trig_init();
        } else if (0 == strncmp(write_data, "ohmparaupdate", 13)) {
                ohm_para_update();
                return len;
        } else if (0 == strncmp(write_data, "ohmacitonctrl", 13)){
		strncpy(ctrl_list, &write_data[13], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		action_ctrl =  (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_action_ctrl = action_ctrl != 0 ? 1 : 0;
	} else {
                ohm_err("input illegal\n");
                return -EFAULT;
        }
        ohm_debug("write: %s, set: %s, ctrl: 0x%08x, logon: 0x%08x, trig: 0x%08x\n",
                write_data, ctrl_list, ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
	return len;
}

static const struct file_operations proc_para_fops = {
        .read = ohm_para_read,
        .write = ohm_para_write,
};

/******  iowait hung show  ******/
unsigned int  iowait_hung_cnt = 0;
unsigned int  iowait_panic_cnt = 0;
static ssize_t iowait_hung_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_M] = {0};
        int len = 0;

        len = sprintf( page, "iowait_hung_cnt: %u\n""iowait_panic_cnt: %u\n"
                "ohm_iopanic_mon_ctrl: %s\n""ohm_iopanic_mon_logon: %s\n""ohm_iopanic_mon_trig: %s\n",
                iowait_hung_cnt, iowait_panic_cnt,
                (ohm_iopanic_mon_ctrl ? "true" : "false"), (ohm_iopanic_mon_logon ? "true" : "false"), (ohm_iopanic_mon_trig ? "true" : "false"));

        return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_iowait_hung_fops = {
       .read = iowait_hung_read,
};

/******  cpu info show  ******/
extern unsigned int cpufreq_quick_get_max(unsigned int cpu);
static ssize_t cpu_info_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	unsigned int cpu;
	unsigned long scale_capacity = 0, last_capacity = 0;
	char *page = kzalloc(BUFFER_SIZE_L, GFP_KERNEL);
	int ret_len = 0;
	if (!page) {
		return -ENOMEM;
	}
    for_each_possible_cpu(cpu) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	scale_capacity = arch_scale_cpu_capacity(NULL,cpu);
#else
	scale_capacity = arch_scale_cpu_capacity(cpu);
#endif
        if (scale_capacity == last_capacity) {
            continue;
        }
        last_capacity = scale_capacity;
        len += snprintf(page + len, sizeof(page) - len, "%u ", cpufreq_quick_get_max(cpu));
    }
	ret_len = sched_data_to_user(buff, count, off, page, len);
	kfree(page);
	return ret_len;
}

static const struct file_operations proc_cpu_info_fops = {
       .read = cpu_info_read,
};

/* thresh read/write */
static ssize_t ohm_thresh_read_common(struct file *filp, char __user *buff, size_t count, loff_t *off,int type)
{
        char page[BUFFER_SIZE_S] = {0};
        int len = 0;

        len = sprintf( page," %s_thresh:\n h_ms = %dms,l_ms = %dms\n",
                sched_list[type], ohm_thresh_para[type].h_ms, ohm_thresh_para[type].l_ms);

        return sched_data_to_user(buff, count, off, page, len);
}

static ssize_t ohm_thresh_read_iowait(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        return ohm_thresh_read_common(filp, buff, count, off, OHM_SCHED_IOWAIT);
}

static ssize_t ohm_thresh_read_sched_latency(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        return ohm_thresh_read_common(filp, buff, count, off, OHM_SCHED_SCHEDLATENCY);
}

static ssize_t ohm_thresh_read_fsync(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        return ohm_thresh_read_common(filp, buff, count, off, OHM_SCHED_FSYNC);
}

static ssize_t ohm_thresh_write_common(struct file *file, const char __user *buff, size_t len, loff_t *ppos, int type)
{
        char write_data[32] = {0};
        char thresh_list[32] = {0};
        int thresh = 0;

	if(len > 31)
		return -EFAULT;

        if (copy_from_user(&write_data, buff, len)) {
                ohm_err("write error.\n");
                return -EFAULT;
        }
        write_data[len] = '\0';
        if (write_data[len - 1] == '\n') {
                write_data[len - 1] = '\0';
        }

        if (0 == strncmp(write_data, "highthresh", 10)) {
                strncpy(thresh_list, &write_data[9], OHM_INT_MAX);
                thresh_list[OHM_INT_MAX] = '\0';
                thresh = (int)simple_strtol(thresh_list, NULL, 10);
                if (thresh < 0 || thresh < ohm_thresh_para[type].l_ms)
                  goto input_err;
                ohm_thresh_para[type].h_ms = thresh;
        }else if (0 == strncmp(write_data, "lowthresh", 9)){
                strncpy(thresh_list, &write_data[9], OHM_INT_MAX);
                thresh_list[OHM_INT_MAX] = '\0';
                thresh = (int)simple_strtol(thresh_list, NULL, 10);
                if (thresh < 0 || thresh > ohm_thresh_para[type].h_ms)
                  goto input_err;
                ohm_thresh_para[type].l_ms = thresh;
        }else {
                goto input_err;
        }

        ohm_debug("thresh update success!Now %s h_ms= %dms,l_ms= %dms\n",
                sched_list[type], ohm_thresh_para[type].h_ms, ohm_thresh_para[type].l_ms);
        return len;

input_err:
       ohm_err("input illegal\n");
       return -EFAULT;
}

static ssize_t ohm_thresh_write_iowait(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
      return ohm_thresh_write_common(file, buff, len, ppos, OHM_SCHED_IOWAIT);
}

static ssize_t ohm_thresh_write_sched_latency(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
      return ohm_thresh_write_common(file, buff, len, ppos, OHM_SCHED_SCHEDLATENCY);
}

static ssize_t ohm_thresh_write_fsync(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
      return ohm_thresh_write_common(file, buff, len, ppos, OHM_SCHED_FSYNC);
}

static const struct file_operations proc_iowait_thresh_fops = {
        .read = ohm_thresh_read_iowait,
        .write = ohm_thresh_write_iowait,
};

static const struct file_operations proc_sched_latency_thresh_fops = {
        .read = ohm_thresh_read_sched_latency,
        .write = ohm_thresh_write_sched_latency,
};

static const struct file_operations proc_fsync_thresh_fops = {
        .read = ohm_thresh_read_fsync,
        .write = ohm_thresh_write_fsync,
};
/******  End  ******/
#ifdef OPLUS_FEATURE_UFSPLUS
#if defined(CONFIG_UFSFEATURE)
#if 0
static ssize_t ufsf_para_read(struct file *filp, char __user *buff, size_t count,
             loff_t *off)
{
    char page[1024] = {0};
    int len = 0;

#ifdef CONFIG_UFSHPB
    len += sprintf(&page[len], "hit: %lld\nhit_4k: %lld\nhit_8_32k: %lld\n"
                "miss: %lld\nspan: %lld\nspan_hit: %lld\n"
                "noti: %lld\nnoti_act: %lld\nnoti_inact: %lld\n"
                "rgn_act: %lld\nmap_req: %lld\npre_req: %lld\n"
                "hpb_rgns: %d\n",
                ufsf_para.hit, ufsf_para.hit_4k, ufsf_para.hit_8_32k,
                ufsf_para.miss, ufsf_para.span, ufsf_para.span_hit,
                ufsf_para.noti, ufsf_para.noti_act, ufsf_para.noti_inact,
                ufsf_para.rgn_act, ufsf_para.map_req, ufsf_para.pre_req,
                ufsf_para.hpb_rgns);
#endif

#ifdef CONFIG_UFSTW
    len += sprintf(&page[len],
                "tw_count: %llu\n"
                "tw_error_count: %llu\n"
                "tw_write_secs(512K): %llu\n"
                "total_write_secs(512K): %llu\n"
                "tw_enable: %d\n"
                "tw_lifetime: %x\n"
                "h8_enter_count: %llu\n"
                "h8_all_ms: %llu\n"
                "h8_enter_100ms_count: %llu\n"
                "h8_above100_ms: %llu\n"
                "h8_max_ms: %llu\n"
                "buffer_size: %u\n",
                (ufsf_para.tw_enable_count + ufsf_para.tw_disable_count),
                ufsf_para.tw_setflag_error_count,    /* total error count */
                ufsf_para.tw_write_secs,    /* tw_write_secs(512K) */
                ufsf_para.total_write_secs,
                ufsf_para.tw_enable,
                ufsf_para.tw_lifetime,
                ufsf_para.hibern8_enter_count,
                ufsf_para.hibern8_amount_ms,
                ufsf_para.hibern8_enter_count_100ms,
                ufsf_para.hibern8_amount_ms_100ms,
                ufsf_para.hibern8_max_ms,
                ufsf_para.buffer_size);
#endif


    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }
    if (copy_to_user(buff, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

static const struct file_operations proc_ufsf_para_fops = {
    .read = ufsf_para_read,
};
#endif
#endif
#endif

#define HEALTHINFO_PROC_NODE "oplus_healthinfo"
static struct proc_dir_entry *healthinfo = NULL;
static struct proc_dir_entry *sched_thresh = NULL;

#ifdef CONFIG_KMALLOC_DEBUG
/* operations of vmalloc_debug and kmalloc_debug */

extern int __weak create_kmalloc_debug(struct proc_dir_entry *parent);
#endif
#ifdef CONFIG_VMALLOC_DEBUG
extern int __weak create_vmalloc_debug(struct proc_dir_entry *parent);
#endif
#ifdef CONFIG_VIRTUAL_RESERVE_MEMORY
int create_reserved_area_enable_proc(struct proc_dir_entry *parent);
#endif
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && IS_BUILTIN(CONFIG_OPLUS_FEATURE_ZRAM_OPT)) \
	|| ((LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) && defined(CONFIG_HYBRIDSWAP_SWAPD))
extern int create_swappiness_para_proc(void);
#endif
static int __init healthinfo_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

        ohm_para_init();
        ohm_action_init();
	healthinfo =  proc_mkdir(HEALTHINFO_PROC_NODE, NULL);
	if(!healthinfo) {
		ohm_err("can't create healthinfo proc\n");
		goto ERROR_INIT_VERSION;
	}
/******  ctrl  *****/
	pentry = proc_create("para_update", S_IRUGO | S_IWUGO, healthinfo, &proc_para_fops);
        if(!pentry) {
                ohm_err("create para_update proc failed.\n");
                goto ERROR_INIT_VERSION;
        }

/******  Stat  ******/
	pentry = proc_create("fsync_wait", S_IRUGO, healthinfo, &proc_fsync_wait_fops);
	if(!pentry) {
		ohm_err("create fsync_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("cpu_loading", S_IRUGO, healthinfo, &proc_cpu_load_fops);
	if(!pentry) {
		ohm_err("create cpu_loading proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait", S_IRUGO, healthinfo, &proc_iowait_fops);
	if(!pentry) {
		ohm_err("create iowait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("sched_latency", S_IRUGO, healthinfo, &proc_sched_latency_fops);
	if(!pentry) {
		ohm_err("create sched_latency proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("emcdrv_iowait", S_IRUGO, healthinfo, &proc_emmcio_fops);
	if(!pentry) {
		ohm_err("create emmc_driver_io_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	
	pentry = proc_create("dstate", S_IRUGO, healthinfo, &proc_dstate_fops);
	if(!pentry) {
		ohm_err("create dstate proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait_hung", S_IRUGO, healthinfo, &proc_iowait_hung_fops);
	if(!pentry) {
		ohm_err("create iowait_hung proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	
#ifdef CONFIG_OPLUS_MEM_MONITOR
	pentry = proc_create("alloc_wait", S_IRUGO, healthinfo, &proc_alloc_wait_fops);
	if(!pentry) {
		ohm_err("create alloc_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("ion_wait", S_IRUGO, healthinfo, &proc_ion_wait_fops);
	if(!pentry) {
		ohm_err("create ion_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
#endif /*CONFIG_OPLUS_MEM_MONITOR*/

#ifdef CONFIG_OPLUS_BLK_MONITOR
	pentry = proc_create("blk_q2c_wait", S_IRUGO, healthinfo, &proc_blk_q2c_wait_fops);
	if (!pentry) {
		ohm_err("create blk_q2c_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("blk_q2i_wait", S_IRUGO, healthinfo, &proc_blk_q2i_wait_fops);
	if (!pentry) {
		ohm_err("create blk_q2i_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("blk_i2d_wait", S_IRUGO, healthinfo, &proc_blk_i2d_wait_fops);
	if (!pentry) {
		ohm_err("create blk_i2d_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("blk_d2c_wait", S_IRUGO, healthinfo, &proc_blk_d2c_wait_fops);
	if (!pentry) {
		ohm_err("create blk_d2c_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	blkmon_init();
#endif /* CONFIG_OPLUS_BLK_MONITOR */

    pentry = proc_create("cpu_info", S_IRUGO, healthinfo, &proc_cpu_info_fops);
    if(!pentry) {
        ohm_err("create cpu info proc failed.\n");
        goto ERROR_INIT_VERSION;
    }
#ifdef OPLUS_FEATURE_UFSPLUS
#if defined(CONFIG_UFSFEATURE)
#if 0 
	pentry = proc_create("ufs_feature", S_IRUGO, healthinfo, &proc_ufsf_para_fops);
	if(!pentry) {
		ohm_err("create ufs_feature proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
#endif
#endif
#endif
#ifdef CONFIG_VIRTUAL_RESERVE_MEMORY
	ret = create_reserved_area_enable_proc(healthinfo);
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

#ifdef CONFIG_PROCESS_RECLAIM_ENHANCE
	ret = create_process_reclaim_enable_proc(healthinfo);
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

/****** thresh update ******/
    sched_thresh =  proc_mkdir("sched_thresh", healthinfo);
    if(!healthinfo) {
        ohm_err("can't create healthinfo proc\n");
        goto ERROR_INIT_VERSION;
    }

    pentry = proc_create("iowait_thresh", S_IRUGO | S_IWUGO, sched_thresh, &proc_iowait_thresh_fops);
    if(!pentry) {
        ohm_err("create iowait_thresh proc failed.\n");
        goto ERROR_INIT_VERSION;
    }

    pentry = proc_create("sched_latency_thresh", S_IRUGO | S_IWUGO, sched_thresh, &proc_sched_latency_thresh_fops);
    if(!pentry) {
        ohm_err("create sched_latency_thresh proc failed.\n");
        goto ERROR_INIT_VERSION;
    }

    pentry = proc_create("fsync_thresh", S_IRUGO | S_IWUGO, sched_thresh, &proc_fsync_thresh_fops);
    if(!pentry) {
        ohm_err("create fsync_thresh proc failed.\n");
        goto ERROR_INIT_VERSION;
    }
#ifdef CONFIG_VMALLOC_DEBUG
	/* create the vmalloc_debug file node */
	ret = create_vmalloc_debug(healthinfo);
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

#ifdef CONFIG_KMALLOC_DEBUG
	/* create the kmalloc_debug file node. */
	ret = create_kmalloc_debug(healthinfo);
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

#ifdef CONFIG_KSWAPD_DEBUG_STATISTICS
	ret = kswapd_debug_init(healthinfo);
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && IS_BUILTIN(CONFIG_OPLUS_FEATURE_ZRAM_OPT)) \
	|| ((LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) && defined(CONFIG_HYBRIDSWAP_SWAPD))
	ret = create_swappiness_para_proc();
	if (ret)
		goto ERROR_INIT_VERSION;
#endif

        ohm_debug("Success \n");
	return ret;

ERROR_INIT_VERSION:
	remove_proc_entry(HEALTHINFO_PROC_NODE, NULL);
	return -ENOENT;
}

module_init(healthinfo_init);

module_param_named(ohm_action_ctrl, ohm_action_ctrl, bool, S_IRUGO | S_IWUSR);

MODULE_DESCRIPTION("healthinfo monitor");
MODULE_LICENSE("GPL v2");
