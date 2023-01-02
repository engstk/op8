#ifndef _WEBVIEW_BOOST__H
#define _WEBVIEW_BOOST__H

void task_rename_hook(struct task_struct *tsk);
int find_webview_cpu(struct task_struct *p);
bool is_webview_boost(struct task_struct *p);
#ifdef CONFIG_OPLUS_FEATURE_IM
static inline void oplus_set_im_flag(struct task_struct *task, int flag)
{
	task->im_flag |= flag;
}
static inline void oplus_unset_im_flag(struct task_struct *task, int flag)
{
	task->im_flag &= ~flag;
}
static inline int oplus_get_im_flag(struct task_struct *task)
{
	return task->im_flag;
}
#else
static inline void oplus_set_im_flag(struct task_struct *task, int flag)
{
}
static inline void oplus_unset_im_flag(struct task_struct *task, int flag)
{
}
static inline int oplus_get_im_flag(struct task_struct *task)
{
	return 0;
}
#endif
#endif
