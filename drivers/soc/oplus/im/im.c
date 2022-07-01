#include <linux/im/im.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* default list, empty string means no need to check */
static struct im_target {
	char val[64];
	const char* desc;
} im_target [IM_ID_MAX] = {
	{"surfaceflinger", "sf "},
	{"", "kworker "},
	{"logd", "logd "},
	{"logcat", "logcat "},
	{"", "main "},
	{"", "enqueue "},
	{"", "gl "},
	{"", "vk "},
	{"composer-servic", "hwc "},
	{"HwBinder:", "hwbinder "},
	{"Binder:", "binder "},
	{"hwuiTask", "hwui "},
	{"", "render "},
	{"", "unity_wk"},
	{"UnityMain", "unityM"},
	{"neplus.launcher", "launcher "},
	{"HwuiTask", "HwuiEx "},
	{"CrRendererMain", "crender "},
};

/* ignore list, not set any im_flag */
static char target_ignore_prefix[IM_IG_MAX][64] = {
	"Prober_",
	"DispSync",
	"app",
	"sf",
	"ScreenShotThrea",
	"DPPS_THREAD",
	"LTM_THREAD",
};

void im_to_str(int flag, char* desc, int size)
{
	char *base = desc;
	int i;

	for (i = 0; i < IM_ID_MAX; ++i) {
		if (flag & (1 << i)) {
			size_t len = strlen(im_target[i].desc);

			if (len) {
				if (size <= base - desc + len) {
					pr_warn("im tag desc too long\n");
					return;
				}
				strncpy(base, im_target[i].desc, len);
				base += len;
			}
		}
	}
}

static inline bool im_ignore(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(target_ignore_prefix[idx]);
	if (tlen == 0)
		return false;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);
	if (len < tlen)
		return false;

	if (!strncmp(task->comm, target_ignore_prefix[idx], tlen)) {
		task->im_flag = 0;
		return true;
	}
	return false;
}

static inline void im_tagging(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(im_target[idx].val);
	if (tlen == 0)
		return;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);

	/* non restrict tagging for some prefixed tasks*/
	if (len < tlen)
		return;

	/* prefix cases */
	if (!strncmp(task->comm, im_target[idx].val, tlen)) {
		switch (idx) {
		case IM_ID_HWBINDER:
			task->im_flag |= IM_HWBINDER;
			break;
		case IM_ID_BINDER:
			task->im_flag |= IM_BINDER;
			break;
		case IM_ID_HWUI:
			task->im_flag |= IM_HWUI;
			break;
		case IM_ID_HWUI_EX:
			task->im_flag |= IM_HWUI_EX;
			break;
		}
	}

	/* restrict tagging for specific identical tasks */
	if (len != tlen)
		return;

	if (!strncmp(task->comm, im_target[idx].val, len)) {
		switch (idx) {
		case IM_ID_SURFACEFLINGER:
			task->im_flag |= IM_SURFACEFLINGER;
			break;
		case IM_ID_LOGD:
			task->im_flag |= IM_LOGD;
			break;
		case IM_ID_LOGCAT:
			task->im_flag |= IM_LOGCAT;
			break;
		case IM_ID_HWC:
			task->im_flag |= IM_HWC;
			break;
		case IM_ID_LAUNCHER:
			task->im_flag |= IM_LAUNCHER;
			break;
		case IM_ID_RENDER:
			task->im_flag |= IM_RENDER;
			break;
		case IM_ID_UNITY_MAIN:
			task->im_flag |= IM_UNITY_MAIN;
			break;
		}
	}
}

void im_wmi(struct task_struct *task)
{
	struct task_struct *leader = task, *p;
	int i = 0;

	/* check for ignore */
	for (i = 0; i < IM_IG_MAX; ++i)
		if (im_ignore(task, i))
			return;

	/* do the check and initial */
	task->im_flag = 0;
	for (i = 0; i < IM_ID_MAX; ++i)
		im_tagging(task, i);

	/* check leader part */
	rcu_read_lock();
	if (task != task->group_leader)
		leader = find_task_by_vpid(task->tgid);

	if (leader) {
		/* for hwc cases */
		if (im_hwc(leader)) {
			for_each_thread(task, p) {
				if (im_binder_related(p))
					p->im_flag |= IM_HWC;
			}
		}

		/* for sf cases */
		if (im_sf(leader) && im_binder_related(task))
			task->im_flag |= IM_SURFACEFLINGER;
	}
	rcu_read_unlock();
}

void im_set_flag(struct task_struct *task, int flag)
{
	/* for hwui boost purpose */
	im_tagging(current, IM_ID_HWUI);
	if (current->im_flag & IM_HWUI)
		return;

	im_tagging(current, IM_ID_HWUI_EX);
	if (current->im_flag & IM_HWUI_EX)
		return;

	/* set the flag */
	current->im_flag |= flag;

	/* if task with enqueue operation, then it's leader should be main thread */
	if (flag == IM_ENQUEUE) {
		struct task_struct *leader = current;

		rcu_read_lock();
		/* refetch leader */
		if (current != current->group_leader)
			leader = find_task_by_vpid(current->tgid);
		if (leader)
			leader->im_flag |= IM_MAIN;
		rcu_read_unlock();
	}
}

void im_unset_flag(struct task_struct *task, int flag)
{
	task->im_flag &= ~flag;
}

void im_reset_flag(struct task_struct *task)
{
	task->im_flag = 0;
}

void im_tsk_init_flag(void *ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;

	task->im_flag &= ~(IM_HWUI & IM_HWUI_EX);
}
