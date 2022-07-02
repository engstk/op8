// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "wakelock_profile: " fmt

#include "oplus_wakelock_profiler_qcom.h"
#include "qcom_platform_atoll.h"


int wakeup_reasons_statics(const char *irq_name, int choose_flag)
{
	int i, j;
	struct wakeup_count_desc_t *desc;
	struct ws_desc_t *ws_desc;

	if (irq_name == NULL) {
		return false;
	}
	pr_info("Enter: %s, irq_name=%s, choose_flag=0x%x", __func__,
		irq_name, choose_flag);

	for (i = 0; (desc = all_modules[i]) != NULL; i++) {
		if (desc->module_mask & choose_flag) {
			for (j = 0; j < desc->ws_number; j++) {
				ws_desc = &desc->ws_desc[j];
				if (!strncmp(irq_name, ws_desc->name, strlen(ws_desc->name))) {
					ws_desc->count++;
					desc->module_all_count++;
				}
			}
		}
	}

	return true;
}


void wakeup_reasons_clear(int choose_flag)
{
	int i, j;
	struct wakeup_count_desc_t *desc;
	struct ws_desc_t *ws_desc;

	pr_info("Enter: %s, choose_flag=0x%x", __func__, choose_flag);
	for (i = 0; (desc = all_modules[i]) != NULL; i++) {
		if (desc->module_mask & choose_flag) {
			for (j = 0; j < desc->ws_number; j++) {
				ws_desc = &desc->ws_desc[j];
				ws_desc->count = 0;
			}
			desc->module_all_count = 0;
		}
	}
}

void wakeup_reasons_print(int choose_flag, int datil)
{
	int i, j;
	struct wakeup_count_desc_t *desc;

	for (i = 0; (desc = all_modules[i]) != NULL; i++) {
		if (desc->module_mask & choose_flag) {
			pr_info("%s %lld times,\n", desc->module_name, desc->module_all_count);
			if (datil) {
				for (j = 0; j < desc->ws_number; j++) {
					if (desc->ws_desc[j].prop & (IRQ_PROP_REAL|IRQ_PROP_EXCHANGE)) {
						pr_info("%s wakeup %lld times\n", desc->ws_desc[j].name, desc->ws_desc[j].count);
					}
				}
			}
		}
	}
}

/*----------------------------- alarmtimer wakeup statics --------------------------*/

extern enum alarmtimer_restart	(*net_alarm_func)(struct alarm *, ktime_t now);

struct alarm_wakeup_t {
	atomic_t suspend_flag;
	atomic_t busy_flag;
	uint64_t alarm_count;
	uint64_t alarm_wakeup_count;
};
static struct alarm_wakeup_t awuc = {
	.suspend_flag = ATOMIC_INIT(0),
	.busy_flag = ATOMIC_INIT(0),
	.alarm_count = 0,
};

static int alarmtimer_suspend_flag_get(void)
{
	return atomic_read(&awuc.suspend_flag);
}
void alarmtimer_suspend_flag_set(void)
{
	atomic_set(&awuc.suspend_flag, 1);
}
void alarmtimer_suspend_flag_clear(void)
{
	atomic_set(&awuc.suspend_flag, 0);
}
static int alarmtimer_busy_flag_get(void)
{
	return atomic_read(&awuc.busy_flag);
}
void alarmtimer_busy_flag_set(void)
{
	atomic_set(&awuc.busy_flag, 1);
}
void alarmtimer_busy_flag_clear(void)
{
	atomic_set(&awuc.busy_flag, 0);
}

void alarmtimer_wakeup_count(struct alarm *alarm)
{
	if (alarm->type == ALARM_REALTIME || alarm->type == ALARM_BOOTTIME) {
		if(!(alarm->function && alarm->function == net_alarm_func))
			awuc.alarm_count++;
		if(alarmtimer_suspend_flag_get() || alarmtimer_busy_flag_get()) {
			if(!(alarm->function && alarm->function == net_alarm_func)) {
				awuc.alarm_wakeup_count++;
				wakeup_reasons_statics(IRQ_NAME_ALARM, WS_CNT_ALARM);
			}
			if(alarmtimer_busy_flag_get())
				alarmtimer_busy_flag_clear();
			if (alarm->function)
				pr_info("%s: alarm_type=%d, not_netalarm_count=%lld, not_netalarm_wakeup_count=%lld, alarm_func=%pf\n",
					__func__, alarm->type, awuc.alarm_count, awuc.alarm_wakeup_count, alarm->function);
		}
	}
}

static ssize_t ap_resume_reason_stastics_show (
	struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i, buf_offset = 0;
	struct wakeup_count_desc_t *desc;

	for (i = 0; (desc = all_modules[i]) != NULL && buf_offset < PAGE_SIZE; i++) {
		buf_offset += sprintf(buf + buf_offset, "%s: %lld\n", desc->module_name, desc->module_all_count);
		pr_info("%s: %lld times.\n", desc->module_name, desc->module_all_count);
	}

	return buf_offset;
}

static ssize_t wakeup_stastisc_reset_store (
	struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	const char *pstring = "reset";

	if(!(count == strlen(pstring) || (count == strlen(pstring) + 1 && buf[count-1] == '\n'))) {
		return count;
	}
	if (strncmp(buf, pstring, strlen(pstring))) {
		return count;
	}
	wakeup_reasons_clear(WS_CNT_ALL);

	return count;
}

/* Keep the same sysfs name with the old project. */
static ssize_t wakeup_stastisc_reset_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf) {
	const char *pstring = "reset";
	return sprintf(buf, "%s\n", pstring);
}

static struct kobj_attribute ap_resume_reason_stastics = __ATTR_RO(ap_resume_reason_stastics);
static struct kobj_attribute wakeup_stastisc_reset = 	__ATTR_RW(wakeup_stastisc_reset);


static ssize_t modem_resume_reason_stastics_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i, j, tmp_value = 0, index = 0;
	struct wakeup_count_desc_t *desc;
	const char *report_name;

	for (i = 0; (desc = all_modules[i]) != NULL; i++) {
		if (desc->module_mask & WS_CNT_MODEM) {
			for (j = 0; j < desc->ws_number; j++) {
				pr_info("%s wakeup %d times, modem total wakeup %d times.\n",
					desc->ws_desc[j].name, desc->ws_desc[j].count, desc->module_all_count);
				if (desc->ws_desc[j].count > tmp_value) {
					tmp_value = desc->ws_desc[j].count;
					index = j;
				}
			}
			break;
		}
	}

	if (desc) {
		/* The upper layer requirement is this name */
		if (!strcmp(desc->ws_desc[index].name, IRQ_NAME_MODEM_IPA)) {
			report_name = "IPA_WS";
		} else if (!strcmp(desc->ws_desc[index].name, IRQ_NAME_MODEM_QMI)) {
			report_name = "QMI_WS";
		} else {
			report_name = desc->ws_desc[index].name;
		}
		/* Just return the maximum modem wakeup count and modem total wakeup count. */
		return sprintf(buf, "%s:%llu:%llu\n", desc->ws_desc[index].name, desc->ws_desc[index].count,
			desc->module_all_count);
	} else {
		pr_info("Modem module not found!\n");
		return -EINVAL;
	}
}

static struct kobj_attribute modem_resume_reason_stastics = __ATTR_RO(modem_resume_reason_stastics);


/*---------------------------------- sysfs: kernel_time ---------------------------------------------*/

extern bool ws_all_release(void);

struct ws_time_statistics {
	ktime_t	start;
	ktime_t	end;
	ktime_t hold;
	ktime_t reset;

	bool all_ws_released;
};
static DEFINE_SPINLOCK(ws_lock);
static struct ws_time_statistics wsts;

void wakeup_get_start_time(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ws_lock, flags);
	if (wsts.all_ws_released) {
		wsts.all_ws_released = false;
		wsts.start = ktime_get();
	}
	spin_unlock_irqrestore(&ws_lock, flags);
}

void wakeup_get_end_hold_time(void)
{
	unsigned long flags;
	ktime_t ws_hold;

	spin_lock_irqsave(&ws_lock, flags);
	wsts.all_ws_released = true;
	wsts.end = ktime_get();
	ws_hold = ktime_sub(wsts.end, wsts.start);
	wsts.hold = ktime_add(wsts.hold, ws_hold);
	spin_unlock_irqrestore(&ws_lock, flags);
}

static void kernel_time_reset(void)
{
	if (!ws_all_release()) {
		ktime_t offset_hold_time;
		ktime_t now = ktime_get();
		spin_lock_bh(&ws_lock);
		offset_hold_time = ktime_sub(now, wsts.start);
		wsts.reset = ktime_add(wsts.hold, offset_hold_time);
		spin_unlock_bh(&ws_lock);
	} else {
		spin_lock_bh(&ws_lock);
		wsts.reset = wsts.hold;
		spin_unlock_bh(&ws_lock);
	}
}

static ssize_t kernel_time_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	const char *pstring = "reset";

	if(!(count == strlen(pstring) || (count == strlen(pstring) + 1 && buf[count-1] == '\n')))
		return count;
	if (strncmp(buf, pstring, strlen(pstring)))
		return count;
	kernel_time_reset();

	return count;
}

static ssize_t kernel_time_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int buf_offset = 0;
	ktime_t newest_hold_time;

	if(!ws_all_release()) {
		ktime_t offset_hold_time;
		ktime_t now = ktime_get();
		spin_lock_bh(&ws_lock);
		offset_hold_time = ktime_sub(now, wsts.start);
		newest_hold_time = ktime_add(wsts.hold, offset_hold_time);
	} else {
		spin_lock_bh(&ws_lock);
		newest_hold_time = wsts.hold;
	}
	newest_hold_time = ktime_sub(newest_hold_time, wsts.reset);
	spin_unlock_bh(&ws_lock);
	buf_offset += sprintf(buf + buf_offset, "%lld\n", ktime_to_ms(newest_hold_time));

	return buf_offset;
}

static struct kobj_attribute kernel_time = __ATTR_RW(kernel_time);


/*------------------------------ sysfs: active_max -------------------------------------*/

#define STATICS_NUMBER 4
#define NAME_SIZE 40

extern void get_ws_listhead(struct list_head **ws);
extern void wakeup_srcu_read_lock(int *srcuidx);
extern void wakeup_srcu_read_unlock(int srcuidx);

struct wakelock_desc {
	struct wakeup_source *ws;
	ktime_t max_ws_time;
	char max_ws_name[NAME_SIZE];
};

static ktime_t active_max_reset_time;
static struct wakelock_desc max_wakelock_list[STATICS_NUMBER];
static DEFINE_MUTEX(max_wakelock_list_lock);

static void active_max_reset(void)
{
	int srcuidx;
	unsigned long flags;
	ktime_t total_time;
	struct wakeup_source *ws;
	struct list_head *ws_listhead;

	get_ws_listhead(&ws_listhead);
	mutex_lock(&max_wakelock_list_lock);
	active_max_reset_time = ktime_get();
	mutex_unlock(&max_wakelock_list_lock);
	wakeup_srcu_read_lock(&srcuidx);
	list_for_each_entry_rcu(ws, ws_listhead, entry) {
		spin_lock_irqsave(&ws->lock, flags);
		total_time = ws->total_time;
		if(ws->active) {
			ktime_t active_time;
			ktime_t now = ktime_get();
			active_time = ktime_sub(now, ws->last_time);
			total_time = ktime_add(ws->total_time, active_time);
		}
		ws->total_time_backup = total_time;
		spin_unlock_irqrestore(&ws->lock, flags);
	}
	wakeup_srcu_read_unlock(srcuidx);
}

static ssize_t active_max_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int srcuidx, i, j;
	int max_ws_rate;
	ktime_t cur_ws_total_time, cur_ws_total_time_backup;
	ktime_t wall_delta;
	int buf_offset = 0;
	unsigned long flags;
	struct wakeup_source *ws;
	struct list_head *ws_listhead;

	get_ws_listhead(&ws_listhead);
	mutex_lock(&max_wakelock_list_lock);
	memset(max_wakelock_list, 0, sizeof(max_wakelock_list));
	wakeup_srcu_read_lock(&srcuidx);
	list_for_each_entry_rcu(ws, ws_listhead, entry) {
		ktime_t active_time;
		ktime_t now = ktime_get();
		spin_lock_irqsave(&ws->lock, flags);
		cur_ws_total_time = ws->total_time;
		if(ws->active) {
			active_time = ktime_sub(now, ws->last_time);
			cur_ws_total_time = ktime_add(ws->total_time, active_time);
		}
		cur_ws_total_time_backup = ws->total_time_backup;
		spin_unlock_irqrestore(&ws->lock, flags);
		if(ktime_compare(cur_ws_total_time, cur_ws_total_time_backup) >= 0) {
			for(i = 0; i < STATICS_NUMBER; i++) {
				if(ktime_compare(ktime_sub(cur_ws_total_time, cur_ws_total_time_backup), max_wakelock_list[i].max_ws_time) > 0) {
					for(j = STATICS_NUMBER -1 ; j >= i + 1; j--) {
						max_wakelock_list[j].ws = max_wakelock_list[j-1].ws;
						max_wakelock_list[j].max_ws_time = max_wakelock_list[j-1].max_ws_time;
					}
					max_wakelock_list[i].ws = ws;
					max_wakelock_list[i].max_ws_time = ktime_sub(cur_ws_total_time, cur_ws_total_time_backup);
					break;
				}
			}
		}
	}
	for(i = 0; i < STATICS_NUMBER; i++) {
		if((max_wakelock_list[i].ws != NULL) && (max_wakelock_list[i].ws->name != NULL)) {
			scnprintf(max_wakelock_list[i].max_ws_name, NAME_SIZE - 1, "%s", max_wakelock_list[i].ws->name);
		}
	}
	wakeup_srcu_read_unlock(srcuidx);

	wall_delta = ktime_sub(ktime_get(), active_max_reset_time);
	buf_offset += sprintf(buf + buf_offset, "Name\tTime(mS)\tRate(%%)\n");
	for(i = STATICS_NUMBER -1; i >= 0; i--) {
		if((max_wakelock_list[i].ws != NULL) && (max_wakelock_list[i].max_ws_name[0] != 0)) {
			max_ws_rate = ktime_compare(wall_delta, max_wakelock_list[i].max_ws_time) >= 0 ?
				ktime_to_ms(max_wakelock_list[i].max_ws_time)*100/ktime_to_ms(wall_delta) : 100;
			buf_offset += scnprintf(buf + buf_offset, 200, "%s\t%lld\t%d\n",
				max_wakelock_list[i].max_ws_name, ktime_to_ms(max_wakelock_list[i].max_ws_time), max_ws_rate);
		}
	}
	mutex_unlock(&max_wakelock_list_lock);

	return buf_offset;
}

static ssize_t active_max_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	char reset_string[]="reset";

	if(!((count == strlen(reset_string)) || ((count == strlen(reset_string) + 1) && (buf[count-1] == '\n')))) {
		return count;
	}

	if (strncmp(buf, reset_string, strlen(reset_string)) != 0) {
		return count;
	}
	active_max_reset();

	return count;
}

static struct kobj_attribute active_max = __ATTR_RW(active_max);


static struct attribute *ws_attrs[] = {
	&ap_resume_reason_stastics.attr,
	&wakeup_stastisc_reset.attr,
	&modem_resume_reason_stastics.attr,
	&kernel_time.attr,
	&active_max.attr,
	NULL,
};

static struct attribute_group ws_attr_group = {
	.attrs = ws_attrs,
};

static int ws_fb_notify_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;

	if (evdata && evdata->data) {
		blank = evdata->data;
		pr_info("[%s], val=%ld, blank=%d\n", __func__, event, *blank);

		if (*blank == MSM_DRM_BLANK_POWERDOWN) { /*suspend*/
			if (event == MSM_DRM_EARLY_EVENT_BLANK) { /*early event*/
				pr_info("[%s], POWERDOWN.\n", __func__);
			}
		} else if (*blank == MSM_DRM_BLANK_UNBLANK) { /*resume*/
			if (event == MSM_DRM_EVENT_BLANK) { /*event*/
				pr_info("[%s], UNBLANK\n", __func__);
			}
		}

		if (event == MSM_DRM_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_UNBLANK) {
				wakeup_reasons_print(WS_CNT_ALL, MODULE_DETAIL_PRINT);/*print all wakeup source*/
			}
		} else if (event == MSM_DRM_EARLY_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_POWERDOWN) {
				/* But the real clean up operation is done by the upper layer */
				pr_info("[%s] clean all wakeup counts.\n", __func__);
			}
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block ws_fb_notify_block = {
	.notifier_call =  ws_fb_notify_callback,
};

static struct kobject *wakelock_profiler;

static int __init wakelock_statistics_function_init(void)
{
	int retval;

	active_max_reset_time = ktime_set(0, 0);
	wakelock_profiler = kobject_create_and_add("wakelock_profiler", kernel_kobj);
	if (!wakelock_profiler) {
		pr_info("[%s] failed to create a sysfs kobject\n", __func__);
		return -ENOMEM;
	}
	retval = sysfs_create_group(wakelock_profiler, &ws_attr_group);
	if (retval) {
		kobject_put(wakelock_profiler);
		pr_info("[%s] failed to create a sysfs group %d\n", __func__, retval);
	}

	retval = msm_drm_register_client(&ws_fb_notify_block);
	if (retval) {
		pr_info("[%s] register drm notifier failed.\n", __func__);
	}

	return 0;
}


/* Function: When suspend prepare failed, printk active wakeup source every 60ms */

#define wakelock_printk_interval_ms  (60*1000)
static int wakelock_printk_repeat = 0;

static int __pm_print_active_wakeup_sources(void)
{
	struct wakeup_source *ws;
	int srcuidx, active = 0;
	struct wakeup_source *last_activity_ws = NULL;
	struct list_head *ws_listhead;

	get_ws_listhead(&ws_listhead);
	wakeup_srcu_read_lock(&srcuidx);
	list_for_each_entry_rcu(ws, ws_listhead, entry) {
		if (ws->active) {
			pr_info("active wakeup source: %s, %ld, %ld\n", ws->name,
				ws->active_count, ktime_to_ms(ws->total_time));
			active = 1;
		} else if (!active &&
			   (!last_activity_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_activity_ws->last_time))) {
			last_activity_ws = ws;
		}
	}

	if (!active && last_activity_ws)
		pr_info("last active wakeup source: %s, %ld, %ld\n", last_activity_ws->name,
			last_activity_ws->active_count, ktime_to_ms(last_activity_ws->total_time));
	wakeup_srcu_read_unlock(srcuidx);

	return 0;
}

static void wakelock_printk(struct work_struct *w)
{
	struct delayed_work *dwork = container_of(w, struct delayed_work, work);

	if (__pm_print_active_wakeup_sources())
		return ;

	if (wakelock_printk_repeat)
		schedule_delayed_work(dwork, msecs_to_jiffies(wakelock_printk_interval_ms));
}
static DECLARE_DELAYED_WORK(wakelock_printk_work, wakelock_printk);

static void wakelock_printk_control(int on)
{
	if (on) {
		wakelock_printk_repeat = 1;
		schedule_delayed_work(&wakelock_printk_work,
			msecs_to_jiffies(wakelock_printk_interval_ms));
	} else {
		wakelock_printk_repeat = 0;
		cancel_delayed_work_sync(&wakelock_printk_work);
	}
}

static int wakelock_printk_notifier(struct notifier_block *nb,
	unsigned long event, void *unused)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		wakelock_printk_control(0);
		break;
	case PM_POST_SUSPEND:
		wakelock_printk_control(1);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakelock_printk_pm_nb = {
	.notifier_call = wakelock_printk_notifier,
	.priority = INT_MAX,
};

static int __init wakelock_printk_function_init(void)
{
	int ret;

	ret = register_pm_notifier(&wakelock_printk_pm_nb);
	if (ret) {
		pr_info("%s wakelock_printk_pm_nb error %d\n", __func__, ret);
		return -1;
	}
	wakelock_printk_control(1);

	return 0;
}

static int __init wakelock_profile_init(void) {
	wakelock_statistics_function_init();
	wakelock_printk_function_init();
	pr_info("wakelock_profile probed!\n");

	return 0;
}

postcore_initcall(wakelock_profile_init);
