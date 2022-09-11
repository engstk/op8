// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include "powerkey_monitor.h"
#include "theia_kevent_kernel.h"

#define BRIGHTSCREEN_COUNT_FILE    "/data/oplus/log/bsp/brightscreen_count.txt"
#define BRIGHT_MAX_WRITE_NUMBER            50
#define BRIGHT_SLOW_TIMEOUT_MS            20000

#define BRIGHT_DEBUG_PRINTK(a, arg...)\
    do{\
         pr_err("[theia powerkey_monitor_bright]: " a, ##arg);\
    }while(0)

struct bright_data g_bright_data = {
    .is_panic = 0,
    .status = BRIGHT_STATUS_INIT,
#ifdef CONFIG_DRM_MSM
    .blank = MSM_DRM_BLANK_UNBLANK,
#else
    .blank = FB_BLANK_UNBLANK,
#endif
    .timeout_ms = BRIGHT_SLOW_TIMEOUT_MS,
    .get_log = 1,
    .error_count = 0,
    //.timer.function = bright_timer_func,
};

/* if last stage in this array, skip */
static char bright_last_skip_block_stages[][64] = {
	{ "POWERKEY_interceptKeyBeforeQueueing" }, /* framework policy may not goto sleep when bright check, skip */
};

/* if contain stage in this array, skip */
static char bright_skip_stages[][64] = {
	{ "POWER_wakeUpInternal" }, /* quick press powerkey, power decide wakeup when bright check, skip */
	{ "POWERKEY_wakeUpFromPowerKey" }, /* quick press powerkey, power decide wakeup when bright check, skip */
	{ "CANCELED_" }, /* if CANCELED_ event write in bright check stage, skip */
};

static void bright_timer_func(struct timer_list *t);

static int br_start_check_systemid = -1;

int bright_screen_timer_restart(void)
{
    BRIGHT_DEBUG_PRINTK("bright_screen_timer_restart:blank = %d,status = %d\n", g_bright_data.blank, g_bright_data.status);

    if(g_bright_data.status != BRIGHT_STATUS_CHECK_ENABLE && g_bright_data.status != BRIGHT_STATUS_CHECK_DEBUG){
        BRIGHT_DEBUG_PRINTK("bright_screen_timer_restart:g_bright_data.status = %d return\n",g_bright_data.status);
        return g_bright_data.status;
    }

#ifdef CONFIG_DRM_MSM
    if(g_bright_data.blank == MSM_DRM_BLANK_UNBLANK)    //MSM_DRM_BLANK_POWERDOWN
#else
    if(g_bright_data.blank == FB_BLANK_UNBLANK)        //FB_BLANK_POWERDOWN
#endif
    {
        br_start_check_systemid = get_systemserver_pid();
        mod_timer(&g_bright_data.timer,jiffies + msecs_to_jiffies(g_bright_data.timeout_ms));
        BRIGHT_DEBUG_PRINTK("bright_screen_timer_restart: bright screen check start %u\n",g_bright_data.timeout_ms);
        theia_pwk_stage_start("POWERKEY_START_BR");
        return 0;
    }
    return g_bright_data.blank;
}
EXPORT_SYMBOL(bright_screen_timer_restart);

static int bright_write_error_count(struct bright_data *bri_data)
{
    struct file *fp;
    loff_t pos;
    ssize_t len = 0;
    char buf[256] = {'\0'};
//    static bool have_read_old = false;

    fp = filp_open(BRIGHTSCREEN_COUNT_FILE, O_RDWR | O_CREAT, 0664);
    if (IS_ERR(fp)) {
        BRIGHT_DEBUG_PRINTK("create %s file error fp:%p\n", BRIGHTSCREEN_COUNT_FILE, fp);
        return -1;
    }

    sprintf(buf, "%d\n", bri_data->error_count);

    pos = 0;
    len = kernel_write(fp, buf, strlen(buf), &pos);
    if (len < 0)
        BRIGHT_DEBUG_PRINTK("write %s file error\n", BRIGHTSCREEN_COUNT_FILE);

    pos = 0;
    kernel_read(fp, buf, sizeof(buf), &pos);
    BRIGHT_DEBUG_PRINTK("bright_write_error_count %s\n", buf);

//out:
    filp_close(fp, NULL);

    return len;
}

//copy mtk_boot_common.h
#define NORMAL_BOOT 0
#define ALARM_BOOT 7

static int get_status(void)
{
#ifdef CONFIG_DRM_MSM
    if(MSM_BOOT_MODE__NORMAL == get_boot_mode())
    {
        return g_bright_data.status;
    }
    return BRIGHT_STATUS_INIT_SUCCEES;
#else
    if((get_boot_mode() == NORMAL_BOOT) || (get_boot_mode() == ALARM_BOOT))
    {
        return g_bright_data.status;
    }
    return BRIGHT_STATUS_INIT_SUCCEES;

#endif
}

static bool get_log_swich(void)
{
    return  (BRIGHT_STATUS_CHECK_ENABLE == get_status()||BRIGHT_STATUS_CHECK_DEBUG == get_status())&& g_bright_data.get_log;
}

/*
logmap format:
logmap{key1:value1;key2:value2;key3:value3 ...}
*/
static void get_brightscreen_check_dcs_logmap(char* logmap)
{
    char stages[512] = {0};
    int stages_len;

    stages_len = get_pwkey_stages(stages);
    snprintf(logmap, 512, "logmap{logType:%s;error_id:%s;error_count:%d;systemserver_pid:%d;stages:%s;catchlog:%s}", PWKKEY_BRIGHT_SCREEN_DCS_LOGTYPE,
        g_bright_data.error_id, g_bright_data.error_count, get_systemserver_pid(), stages, get_log_swich() ? "true" : "false");
}

void send_bright_screen_dcs_msg(void)
{
    char logmap[512] = {0};
    get_brightscreen_check_dcs_logmap(logmap);
    SendDcsTheiaKevent(PWKKEY_DCS_TAG, PWKKEY_DCS_EVENTID, logmap);
}

static void dump_freeze_log(void)
{
    //send kevent dcs msg
    send_bright_screen_dcs_msg();
}

static bool is_bright_last_stage_skip(void)
{
	int i = 0, nLen;
	char stage[64] = {0};;
	get_last_pwkey_stage(stage);

	nLen = sizeof(bright_last_skip_block_stages)/sizeof(bright_last_skip_block_stages[0]);

	/* BRIGHT_DEBUG_PRINTK("is_bright_last_stage_skip stage:%s nLen:%d", stage, nLen); */
	for (i = 0; i < nLen; i++) {
		/* BRIGHT_DEBUG_PRINTK("is_bright_last_stage_skip stage:%s i:%d nLen:%d bright_last_skip_block_stages[i]:%s",
			stage, i, nLen, bright_last_skip_block_stages[i]); */
        if (!strcmp(stage, bright_last_skip_block_stages[i])) {
			BRIGHT_DEBUG_PRINTK("is_bright_last_stage_skip return true, stage:%s", stage);
			return true;
		}
	}

	return false;
}

static bool is_bright_contain_skip_stage(void)
{
	char stages[512] = {0};
	int i = 0, nArrayLen;
	get_pwkey_stages(stages);

	nArrayLen = sizeof(bright_skip_stages)/sizeof(bright_skip_stages[0]);
	for (i = 0; i < nArrayLen; i++) {
		if (strstr(stages, bright_skip_stages[i]) != NULL) {
			BRIGHT_DEBUG_PRINTK("is_bright_contain_skip_stage return true, stages:%s", stages);
			return true;
		}
	}

	return false;
}

static bool is_need_skip(void)
{
	if (is_bright_last_stage_skip()) {
		return true;
	}

	if (is_bright_contain_skip_stage()) {
		return true;
	}

	return false;
}

//if the error id contain current pid, we think is a normal resume
static bool is_normal_resume(void)
{
	char current_pid_str[32];
	sprintf(current_pid_str, "%d", get_systemserver_pid());
	if (!strncmp(g_bright_data.error_id, current_pid_str, strlen(current_pid_str))) {
        return true;
	}

    return false;
}

static void get_bright_resume_dcs_logmap(char* logmap)
{
    snprintf(logmap, 512, "logmap{logType:%s;error_id:%s;resume_count:%d;normalReborn:%s;catchlog:false}", PWKKEY_BRIGHT_SCREEN_DCS_LOGTYPE,
        g_bright_data.error_id, g_bright_data.error_count, (is_normal_resume() ? "true" : "false"));
}

static void send_bright_screen_resume_dcs_msg(void)
{
    //check the current systemserver pid and the error_id, judge if it is a normal resume or reboot resume
    char resume_logmap[512] = {0};
    get_bright_resume_dcs_logmap(resume_logmap);
    SendDcsTheiaKevent(PWKKEY_DCS_TAG, PWKKEY_DCS_EVENTID, resume_logmap);
}

static void delete_timer(char* reason, bool cancel) {
    //BRIGHT_DEBUG_PRINTK("delete_timer reason:%s", reason);
    del_timer(&g_bright_data.timer);

    if (cancel && g_bright_data.error_count != 0) {
        send_bright_screen_resume_dcs_msg();
        g_bright_data.error_count = 0;
        sprintf(g_bright_data.error_id, "%s", "null");
    }

    theia_pwk_stage_end(reason);
}

static void bright_error_happen_work(struct work_struct *work)
{
    struct bright_data *bri_data = container_of(work, struct bright_data, error_happen_work);

	/* for bright screen check, check if need skip, we direct return */
	if (is_need_skip()) {
        return;
    }

    if (bri_data->error_count == 0) {
        struct timespec ts;
        getnstimeofday(&ts);
        sprintf(g_bright_data.error_id, "%d.%ld.%ld", get_systemserver_pid(), ts.tv_sec, ts.tv_nsec);
    }

    if (bri_data->error_count < BRIGHT_MAX_WRITE_NUMBER) {
        bri_data->error_count++;
        dump_freeze_log();
        bright_write_error_count(bri_data);
    }
    BRIGHT_DEBUG_PRINTK("bright_error_happen_work error_id = %s, error_count = %d\n",
        bri_data->error_id, bri_data->error_count);

    delete_timer("BL_ERROR_HAPPEN", false);

    if(bri_data->is_panic) {
        doPanic();
    }
}

static void bright_timer_func(struct timer_list *t)
{
//    struct bright_data * p = (struct bright_data *)data;
    struct bright_data * p = from_timer(p, t, timer);


    BRIGHT_DEBUG_PRINTK("bright_timer_func is called\n");

    if (br_start_check_systemid == get_systemserver_pid()) {
        schedule_work(&p->error_happen_work);
    } else {
        BRIGHT_DEBUG_PRINTK("bright_timer_func, not valid for check, skip\n");
    }
}

#ifdef CONFIG_DRM_MSM
static int bright_fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
    struct msm_drm_notifier *evdata = data;
    int *blank;

     if (event == MSM_DRM_EVENT_BLANK && evdata && evdata->data)
    {
        blank = evdata->data;
        g_bright_data.blank = *blank;
        if(g_bright_data.status != BRIGHT_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback: del timer,event:%lu status:%d blank:%d\n",
                event, g_bright_data.status, g_bright_data.blank);
        } else {
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_bright_data.status,g_bright_data.blank);
        }
    }
    else if (event == MSM_DRM_EARLY_EVENT_BLANK && evdata && evdata->data)
    {
        blank = evdata->data;

        blank = evdata->data;
        g_bright_data.blank = *blank;
        if(g_bright_data.status != BRIGHT_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback: del timer event:%lu status:%d blank:%d\n",
                event, g_bright_data.status, g_bright_data.blank);
        } else {
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_bright_data.status,g_bright_data.blank);
        }
    } else {
        BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu status:%d\n",event,g_bright_data.status);
    }

    return 0;
}
#else
static int bright_fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;

    if (evdata && evdata->data && event == FB_EVENT_BLANK)
    {
        blank = evdata->data;
        g_bright_data.blank = *blank;
        if(g_bright_data.status != BRIGHT_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BRIGHT_DEBUG_PRINTK("bright fb_notifier_callback: del timer,event:%d,status:%d,blank:%d\n",
                event, g_bright_data.status, g_bright_data.blank);
        } else {
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu,status:%d,blank:%d\n",
                event,g_bright_data.status,g_bright_data.blank = *blank);
        }
    }
    else if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK)
    {
        blank = evdata->data;
        g_bright_data.blank = *blank;
        if(g_bright_data.status != BRIGHT_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BRIGHT_DEBUG_PRINTK("bright fb_notifier_callback: del timer,event:%d,status:%d,blank:%d\n",
                event, g_bright_data.status, g_bright_data.blank);
        } else {
            BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu,status:%d,blank:%d\n",
                event,g_bright_data.status,g_bright_data.blank = *blank);
        }
    } else {
        BRIGHT_DEBUG_PRINTK("bright_fb_notifier_callback:event = %lu,status:%d\n",event,g_bright_data.status);
    }
    return 0;
}
#endif /* CONFIG_DRM_MSM */

static ssize_t bright_screen_cancel_proc_write(struct file *file, const char __user *buf,
        size_t count,loff_t *off)
{
    char buffer[40] = {0};
    char cancel_str[64] = {0};

    if(g_bright_data.status == BRIGHT_STATUS_INIT || g_bright_data.status == BRIGHT_STATUS_INIT_FAIL){
        BRIGHT_DEBUG_PRINTK("%s init not finish: status = %d\n", __func__, g_bright_data.status);
        return count;
    }

    if (count >= 40) {
       count = 39;
    }

    if (copy_from_user(buffer, buf, count)) {
        BRIGHT_DEBUG_PRINTK("%s: read proc input error.\n", __func__);
        return count;
    }

	snprintf(cancel_str, sizeof(cancel_str), "CANCELED_BR_%s", buffer);
    delete_timer(cancel_str, true);

    return count;
}
static ssize_t bright_screen_cancel_proc_read(struct file *file, char __user *buf,
        size_t count,loff_t *off)
{
    return 0;
}

struct file_operations bright_screen_cancel_proc_fops = {
    .read = bright_screen_cancel_proc_read,
    .write = bright_screen_cancel_proc_write,
};

int bright_screen_check_init(void)
{
    int ret = 0;

    sprintf(g_bright_data.error_id, "%s", "null");
    g_bright_data.fb_notif.notifier_call = bright_fb_notifier_callback;
#ifdef CONFIG_DRM_MSM
    msm_drm_register_client(&g_bright_data.fb_notif);
#else
    fb_register_client(&g_bright_data.fb_notif);
#endif
    if (ret) {
        g_bright_data.status = ret;
        printk("bright block_screen_init register fb client fail\n");
        return ret;
    }

    //the node for cancel bright screen check
    proc_create("brightSwitch", S_IRWXUGO, NULL, &bright_screen_cancel_proc_fops);

    INIT_WORK(&g_bright_data.error_happen_work, bright_error_happen_work);
    timer_setup((&g_bright_data.timer), (bright_timer_func),TIMER_DEFERRABLE);
    g_bright_data.status = BRIGHT_STATUS_CHECK_ENABLE;
    return 0;
}

void bright_screen_exit(void)
{
    delete_timer("FINISH_DRIVER_EXIT", true);
    fb_unregister_client(&g_bright_data.fb_notif);
}
