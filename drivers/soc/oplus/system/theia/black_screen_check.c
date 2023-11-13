// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "powerkey_monitor.h"
#include "theia_kevent_kernel.h"

#define BLACKSCREEN_COUNT_FILE    "/data/oplus/log/bsp/blackscreen_count.txt"
#define BLACK_MAX_WRITE_NUMBER            50
#define BLACK_SLOW_STATUS_TIMEOUT_MS    20000

#define BLACK_DEBUG_PRINTK(a, arg...)\
    do{\
         pr_err("[theia powerkey_monitor_black]: " a, ##arg);\
    }while(0)

static void black_timer_func(struct timer_list *t);

struct black_data g_black_data = {
    .is_panic = 0,
    .status = BLACK_STATUS_INIT,
#ifdef CONFIG_DRM_MSM
    .blank = MSM_DRM_BLANK_UNBLANK,
#else
    .blank = FB_BLANK_UNBLANK,
#endif
    .timeout_ms = BLACK_SLOW_STATUS_TIMEOUT_MS,
    .get_log = 1,
    .error_count = 0,
};

static int bl_start_check_systemid = -1;

int black_screen_timer_restart(void)
{
    BLACK_DEBUG_PRINTK("black_screen_timer_restart:blank = %d,status = %d\n", g_black_data.blank, g_black_data.status);

    if(g_black_data.status != BLACK_STATUS_CHECK_ENABLE && g_black_data.status != BLACK_STATUS_CHECK_DEBUG){
        BLACK_DEBUG_PRINTK("black_screen_timer_restart:g_black_data.status = %d return\n",g_black_data.status);
        return g_black_data.status;
    }
#ifdef CONFIG_DRM_MSM
    if(g_black_data.blank == MSM_DRM_BLANK_POWERDOWN)
#else
    if(g_black_data.blank == FB_BLANK_POWERDOWN)
#endif
    {
        bl_start_check_systemid = get_systemserver_pid();
        BLACK_DEBUG_PRINTK("black_screen_timer_restart: get_systemserver_pid %d\n", bl_start_check_systemid);
        mod_timer(&g_black_data.timer,jiffies + msecs_to_jiffies(g_black_data.timeout_ms));
        BLACK_DEBUG_PRINTK("black_screen_timer_restart: black screen check start %u\n",g_black_data.timeout_ms);
        theia_pwk_stage_start("POWERKEY_START_BL");
        return 0;
    }
    return g_black_data.blank;
}
EXPORT_SYMBOL(black_screen_timer_restart);

//copy mtk_boot_common.h
#define NORMAL_BOOT 0
#define ALARM_BOOT 7
static int get_status(void)
{
#ifdef CONFIG_DRM_MSM
    if(MSM_BOOT_MODE__NORMAL == get_boot_mode())
    {
        return g_black_data.status;
    }
    return BLACK_STATUS_INIT_SUCCEES;
#else
    if((get_boot_mode() == NORMAL_BOOT) || (get_boot_mode() == ALARM_BOOT))
    {
        return g_black_data.status;
    }
    return BLACK_STATUS_INIT_SUCCEES;

#endif
}

static bool get_log_swich(void)
{
    return  (BLACK_STATUS_CHECK_ENABLE == get_status()||BLACK_STATUS_CHECK_DEBUG == get_status())&& g_black_data.get_log;
}

/*
logmap format:
logmap{key1:value1;key2:value2;key3:value3 ...}
*/
static void get_blackscreen_check_dcs_logmap(char* logmap)
{
    char stages[512] = {0};
    int stages_len;

    stages_len = get_pwkey_stages(stages);
	snprintf(logmap, 512, "logmap{logType:%s;error_id:%s;error_count:%u;systemserver_pid:%d;stages:%s;catchlog:%s}", PWKKEY_BLACK_SCREEN_DCS_LOGTYPE,
        g_black_data.error_id, g_black_data.error_count, get_systemserver_pid(), stages, get_log_swich() ? "true" : "false");
}

//if the error id contain current pid, we think is a normal resume
static bool is_normal_resume(void)
{
	char current_pid_str[32];
	sprintf(current_pid_str, "%d", get_systemserver_pid());
	if (!strncmp(g_black_data.error_id, current_pid_str, strlen(current_pid_str))) {
		return true;
	}

    return false;
}

static void get_blackscreen_resume_dcs_logmap(char* logmap)
{
	snprintf(logmap, 512, "logmap{logType:%s;error_id:%s;resume_count:%u;normalReborn:%s;catchlog:false}", PWKKEY_BLACK_SCREEN_DCS_LOGTYPE,
        g_black_data.error_id, g_black_data.error_count, (is_normal_resume() ? "true" : "false"));
}

void send_black_screen_dcs_msg(void)
{
    char logmap[512] = {0};

	BLACK_DEBUG_PRINTK("send_black_screen_dcs_msg\n");
    get_blackscreen_check_dcs_logmap(logmap);
    SendDcsTheiaKevent(PWKKEY_DCS_TAG, PWKKEY_DCS_EVENTID, logmap);
}

static void send_black_screen_resume_dcs_msg(void)
{
    //check the current systemserver pid and the error_id, judge if it is a normal resume or reboot resume
    char resume_logmap[512] = {0};

    BLACK_DEBUG_PRINTK("send_black_screen_resume_dcs_msg\n");
    get_blackscreen_resume_dcs_logmap(resume_logmap);
    SendDcsTheiaKevent(PWKKEY_DCS_TAG, PWKKEY_DCS_EVENTID, resume_logmap);
}

static void delete_timer(char* reason, bool cancel)
{
    BLACK_DEBUG_PRINTK("delete_timer reason:%s", reason);
    del_timer(&g_black_data.timer);

    if (cancel && g_black_data.error_count != 0) {
        send_black_screen_resume_dcs_msg();
        g_black_data.error_count = 0;
        sprintf(g_black_data.error_id, "%s", "null");
    }

    theia_pwk_stage_end(reason);
}

static ssize_t black_screen_cancel_proc_write(struct file *file, const char __user *buf,
        size_t count,loff_t *off)
{
    char buffer[40] = {0};
    char cancel_str[64] = {0};

    if(g_black_data.status == BLACK_STATUS_INIT || g_black_data.status == BLACK_STATUS_INIT_FAIL){
        BLACK_DEBUG_PRINTK("%s init not finish: status = %d\n", __func__, g_black_data.status);
        return count;
    }

    if (count >= 40) {
       count = 39;
    }

    if (copy_from_user(buffer, buf, count)) {
        BLACK_DEBUG_PRINTK("%s: read proc input error.\n", __func__);
        return count;
    }

	snprintf(cancel_str, sizeof(cancel_str), "CANCELED_BL_%s", buffer);
    delete_timer(cancel_str, true);

    return count;
}

static ssize_t black_screen_cancel_proc_read(struct file *file, char __user *buf,
        size_t count,loff_t *off)
{
    return 0;
}

struct file_operations black_screen_cancel_proc_fops = {
    .read = black_screen_cancel_proc_read,
    .write = black_screen_cancel_proc_write,
};

static int black_write_error_count(struct black_data *bla_data)
{
    struct file *fp;
    loff_t pos;
    ssize_t len = 0;
    char buf[256] = {'\0'};
//    static bool have_read_old = false;

    fp = filp_open(BLACKSCREEN_COUNT_FILE, O_RDWR | O_CREAT, 0664);
    if (IS_ERR(fp)) {
        BLACK_DEBUG_PRINTK("create %s file error fp:%p %d \n", BLACKSCREEN_COUNT_FILE, fp,PTR_ERR(fp));
        return -1;
    }

	sprintf(buf, "%u\n", bla_data->error_count);

    pos = 0;
    len = kernel_write(fp, buf, strlen(buf), &pos);
    if (len < 0)
        BLACK_DEBUG_PRINTK("write %s file error\n", BLACKSCREEN_COUNT_FILE);

    pos = 0;
    kernel_read(fp, buf, sizeof(buf), &pos);
    BLACK_DEBUG_PRINTK("black_write_error_count %s\n", buf);

//out:
    filp_close(fp, NULL);

    return len;
}

static void dump_freeze_log(void)
{
	BLACK_DEBUG_PRINTK("dump_freeze_log\n");
    //send kevent dcs msg
    send_black_screen_dcs_msg();
}

static void black_error_happen_work(struct work_struct *work)
{
    struct black_data *bla_data    = container_of(work, struct black_data, error_happen_work);

	BLACK_DEBUG_PRINTK("black_error_happen_work\n");
    if (bla_data->error_count == 0) {
        struct timespec ts;

        BLACK_DEBUG_PRINTK("black_error_happen_work error_count=0\n");
        getnstimeofday(&ts);
        sprintf(g_black_data.error_id, "%d.%ld.%ld", get_systemserver_pid(), ts.tv_sec, ts.tv_nsec);
    }

    if (bla_data->error_count < BLACK_MAX_WRITE_NUMBER) {
		BLACK_DEBUG_PRINTK("black_error_happen_work error_count< BLACK_MAX_WRITE_NUMBER\n");
        bla_data->error_count++;
        dump_freeze_log();
        black_write_error_count(bla_data);
    }
    BLACK_DEBUG_PRINTK("black_error_happen_work error_id = %s, error_count = %d\n",
        bla_data->error_id, bla_data->error_count);

    delete_timer("BR_ERROR_HAPPEN", false);

    if(bla_data->is_panic) {
		BLACK_DEBUG_PRINTK("black_error_happen_work is_panic\n");
        doPanic();
    }
}

static void black_timer_func(struct timer_list *t)
{
    struct black_data *p = from_timer(p, t, timer);

    BLACK_DEBUG_PRINTK("black_timer_func is called\n");
    if (bl_start_check_systemid == get_systemserver_pid()) {
        BLACK_DEBUG_PRINTK("black_timer_func, start work\n");
        schedule_work(&p->error_happen_work);
    } else {
        BLACK_DEBUG_PRINTK("black_timer_func, not valid for check, skip\n");
    }
}

#ifdef CONFIG_DRM_MSM
static int black_fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
    struct msm_drm_notifier *evdata = data;
    int *blank;

     if (event == MSM_DRM_EVENT_BLANK && evdata && evdata->data)
    {
        blank = evdata->data;
        g_black_data.blank = *blank;
        if(g_black_data.status != BLACK_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer,event:%lu status:%d blank:%d\n",
                event, g_black_data.status, g_black_data.blank);
        } else {
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_black_data.status,g_black_data.blank);
        }
    }
    else if (event == MSM_DRM_EARLY_EVENT_BLANK && evdata && evdata->data)
    {
        blank = evdata->data;

        blank = evdata->data;
        g_black_data.blank = *blank;
        if(g_black_data.status != BLACK_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer,event:%lu status:%d blank:%d\n",
                event, g_black_data.status, g_black_data.blank);
        } else {
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_black_data.status,g_black_data.blank);
        }
    } else {
        BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d\n",event,g_black_data.status);
    }
    return 0;
}
#else
static int black_fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;

    if (evdata && evdata->data && event == FB_EVENT_BLANK)
    {
        blank = evdata->data;
        g_black_data.blank = *blank;
        if(g_black_data.status != BLACK_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer,event:%lu status:%d blank:%d\n",
                event, g_black_data.status, g_black_data.blank);
        } else {
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_black_data.status,g_black_data.blank);
        }
    }
    else if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK)
    {
        blank = evdata->data;
        g_black_data.blank = *blank;
        if(g_black_data.status != BLACK_STATUS_CHECK_DEBUG){
            delete_timer("FINISH_FB", true);
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer,event:%lu status:%d blank:%d\n",
                event, g_black_data.status, g_black_data.blank);
        } else {
            BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d blank:%d\n",
                event,g_black_data.status,g_black_data.blank);
        }
    } else {
        BLACK_DEBUG_PRINTK("black_fb_notifier_callback:event = %lu status:%d\n",event,g_black_data.status);
    }
    return 0;
}
#endif /* CONFIG_DRM_MSM */

int black_screen_check_init(void)
{
    int ret = 0;

    sprintf(g_black_data.error_id, "%s", "null");
    g_black_data.fb_notif.notifier_call = black_fb_notifier_callback;
#ifdef CONFIG_DRM_MSM
    msm_drm_register_client(&g_black_data.fb_notif);
#else
    fb_register_client(&g_black_data.fb_notif);
#endif
    if (ret) {
        g_black_data.status = ret;
        printk("block_screen_init register fb client fail\n");
        return ret;
    }

    //the node for cancel black screen check
    proc_create("blackSwitch", S_IRWXUGO, NULL, &black_screen_cancel_proc_fops);

	BLACK_DEBUG_PRINTK("black_screen_check_init INIT_WORK\n");
    INIT_WORK(&g_black_data.error_happen_work, black_error_happen_work);
    BLACK_DEBUG_PRINTK("black_screen_check_init timer_setup\n");
    timer_setup((&g_black_data.timer), (black_timer_func),TIMER_DEFERRABLE);
    g_black_data.status = BLACK_STATUS_CHECK_ENABLE;

    return 0;
}

void black_screen_exit(void)
{
    delete_timer("FINISH_DRIVER_EXIT", true);
    fb_unregister_client(&g_black_data.fb_notif);
}
