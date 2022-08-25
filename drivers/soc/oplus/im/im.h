#ifndef __IM_H__
#define __IM_H__

#include <linux/sched.h>

/* since im_flag is 32bit, don't identify too much */
enum {
	IM_ID_SURFACEFLINGER = 0,  // surfaceflinger
	IM_ID_KWORKER,             // kworker
	IM_ID_LOGD,                // logd
	IM_ID_LOGCAT,              // logcat
	IM_ID_MAIN,                // application main thread
	IM_ID_ENQUEUE,             // qneueue frame task
	IM_ID_GL,                  // open GL tasks
	IM_ID_VK,                  // vulkan tasks
	IM_ID_HWC,                 // hwcomposer
	IM_ID_HWBINDER,            // hw binder
	IM_ID_BINDER,              // binder
	IM_ID_HWUI,                // hwui tasks
	IM_ID_RENDER,              // application render thread
	IM_ID_UNITY_WORKER_THREAD, // unity worker thread
	IM_ID_UNITY_MAIN,          // unity main
	IM_ID_LAUNCHER,            // launcher
	IM_ID_HWUI_EX,             // Hwui task (render enhancement feature)
	IM_ID_BMT,                 // Bitmap thread
	IM_ID_CRENDER,             // Chrome render
#ifdef CONFIG_OPLUS_FEATURE_INPUT_BOOST
	IM_ID_WEBVIEW,             // WEBVIEW
#endif
	IM_ID_MAX
};

#define IM_SURFACEFLINGER (1 << IM_ID_SURFACEFLINGER)
#define IM_KWORKER        (1 << IM_ID_KWORKER)
#define IM_LOGD           (1 << IM_ID_LOGD)
#define IM_LOGCAT         (1 << IM_ID_LOGCAT)
#define IM_MAIN           (1 << IM_ID_MAIN)
#define IM_ENQUEUE        (1 << IM_ID_ENQUEUE)
#define IM_GL             (1 << IM_ID_GL)
#define IM_VK             (1 << IM_ID_VK)
#define IM_HWC            (1 << IM_ID_HWC)
#define IM_HWBINDER       (1 << IM_ID_HWBINDER)
#define IM_BINDER         (1 << IM_ID_BINDER)
#define IM_HWUI           (1 << IM_ID_HWUI)
#define IM_RENDER         (1 << IM_ID_RENDER)
#define IM_UNITY_WORKER_THREAD  (1 << IM_ID_UNITY_WORKER_THREAD)
#define IM_UNITY_MAIN     (1 << IM_ID_UNITY_MAIN)
#define IM_LAUNCHER       (1 << IM_ID_LAUNCHER)
#define IM_HWUI_EX        (1 << IM_ID_HWUI_EX)
#define IM_BMT            (1 << IM_ID_BMT)
#define IM_CRENDER        (1 << IM_ID_CRENDER)
#ifdef CONFIG_OPLUS_FEATURE_INPUT_BOOST
#define IM_WEBVIEW        (1 << IM_ID_WEBVIEW)
#endif

/* ignore list */
enum {
	IM_IG_SF_PROBER = 0,
	IM_IG_SF_APP,
	IM_IG_SF_SF,
	IM_IG_SF_DISPSYNC,
	IM_IG_SF_SCREENSHOTTHRES,
	IM_IG_HWC_DPPS,
	IM_IG_HWC_LTM,
	IM_IG_MAX
};

/* TODO add for group identify */
/* TODO add check for cmdline to cover zygote */

#ifdef CONFIG_OPLUS_FEATURE_IM
static inline bool im_sf(struct task_struct *task)
{
	return task->im_flag & IM_SURFACEFLINGER;
}

static inline bool im_kw(struct task_struct *task)
{
	return task->im_flag & IM_KWORKER;
}

static inline bool im_logd(struct task_struct *task)
{
	return task->im_flag & IM_LOGD;
}

static inline bool im_logcat(struct task_struct *task)
{
	return task->im_flag & IM_LOGCAT;
}

static inline bool im_rendering(struct task_struct *task)
{
/* TODO should handle this part after RATP phased in */
#ifdef CONFIG_RATP
	if (is_ratp_enable() && is_allowmost_enable()) {
		return task->im_flag &
			(IM_MAIN |
			IM_ENQUEUE |
			IM_SURFACEFLINGER |
			IM_GL |
			IM_VK |
			IM_RENDER |
			IM_HWC |
			IM_HWBINDER |
			IM_BINDER |
			IM_BMT |
			IM_CRENDER);
	}
#endif

	return task->im_flag &
		(IM_MAIN |
		IM_ENQUEUE |
		IM_SURFACEFLINGER |
		IM_GL |
		IM_VK |
		IM_HWC |
		IM_RENDER |
		IM_BMT |
        IM_CRENDER);

}

static inline bool im_graphic(struct task_struct *task)
{
	return task->im_flag & (IM_GL | IM_VK | IM_HWUI | IM_HWUI_EX);
}

static inline bool im_main(struct task_struct *task)
{
	return task->im_flag & IM_MAIN;
}

static inline bool im_render(struct task_struct *task)
{
	return task->im_flag & IM_RENDER;
}

static inline bool im_enqueue(struct task_struct *task)
{
	return task->im_flag & IM_ENQUEUE;
}

static inline bool im_gl(struct task_struct *task)
{
	return task->im_flag & IM_GL;
}

static inline bool im_vk(struct task_struct *task)
{
	return task->im_flag & IM_VK;
}

static inline bool im_hwc(struct task_struct *task)
{
	return task->im_flag & IM_HWC;
}

static inline bool im_hwbinder(struct task_struct *task)
{
	return task->im_flag & IM_HWBINDER;
}

static inline bool im_binder(struct task_struct *task)
{
	return task->im_flag & IM_BINDER;
}

static inline bool im_binder_related(struct task_struct *task)
{
	return task->im_flag & (IM_HWBINDER | IM_BINDER);
}

static inline bool im_hwui(struct task_struct *task)
{
	return task->im_flag & IM_HWUI;
}

static inline bool im_unity_worker_thread(struct task_struct *task)
{
	return task->im_flag & (IM_UNITY_WORKER_THREAD);
}

static inline bool im_unity_main(struct task_struct *task)
{
	return task->im_flag & (IM_UNITY_MAIN);
}

static inline bool im_launcher(struct task_struct *task)
{
	return task->im_flag & IM_LAUNCHER;
}

static inline bool im_hwuiEx(struct task_struct *task)
{
	return task->im_flag & IM_HWUI_EX;
}

static inline bool im_crender(struct task_struct *task)
{
	return task->im_flag & IM_CRENDER;
}

static inline bool im_bmt(struct task_struct *task)
{
	return task->im_flag & IM_BMT;
}

extern void im_wmi(struct task_struct *task);
extern void im_set_flag(struct task_struct *task, int flag);
extern void im_unset_flag(struct task_struct *task, int flag);
extern void im_reset_flag(struct task_struct *task);
extern void im_to_str(int flag, char* desc, int size);
extern void im_tsk_init_flag(void *ptr);
#else
static inline bool im_sf(struct task_struct *task) { return false; }
static inline bool im_kw(struct task_struct *task) { return false; }
static inline bool im_logd(struct task_struct *task) { return false; }
static inline bool im_logcat(struct task_struct *task) { return false; }
static inline bool im_rendering(struct task_struct *task) { return false; }
static inline bool im_main(struct task_struct *task) { return false; }
static inline bool im_enqueue(struct task_struct *task) { return false; }
static inline bool im_render(struct task_struct *task) { return false; }
static inline bool im_gl(struct task_struct *task) { return false; }
static inline bool im_vk(struct task_struct *task) { return false; }
static inline bool im_hwc(struct task_struct *task) { return false; }
static inline bool im_hwbinder(struct task_struct *task) { return false; }
static inline bool im_binder(struct task_struct *task) { return false; }
static inline bool im_binder_related(struct task_struct *task) { return false; }
static inline bool im_hwui(struct task_struct *task) { return false; }
static inline bool im_unity_worker_thread(struct task_struct *task) { return false; }
static inline bool im_unity_main(struct task_struct *task) { return false; }
static inline bool im_launcher(struct task_struct *task) { return false; }
static inline bool im_hwuiEx(struct task_struct *task) { return false; }
static inline bool im_crender(struct task_struct *task) { return false; }
static inline bool im_bmt(struct task_struct *task) { return false; }
static inline void im_wmi(struct task_struct *task) {}
static inline void im_set_flag(struct task_struct *task, int flag) {}
static inline void im_unset_flag(struct task_struct *task, int flag) {}
static inline void im_reset_flag(struct task_struct *task) {}
static inline void im_to_str(int flag, char* desc, int size) {}
static inline void im_tsk_init_flag(void *ptr) {}
#endif // CONFIG_OPLUS_FEATURE_IM

#endif // __IM_H__
