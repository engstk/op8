#ifndef _FG_H_
#define _FG_H_

#include <linux/cred.h>
#include "../fs/fg_uid/fg_uid.h"

#ifdef CONFIG_FG_TASK_UID
static inline int current_is_fg(void)
{
	int cur_uid;
	cur_uid = current_uid().val;
	if (is_fg(cur_uid))
		return 1;
	return 0;
}

static inline int task_is_fg(struct task_struct *tsk)
{
	int cur_uid;
	cur_uid = task_uid(tsk).val;
	if (is_fg(cur_uid))
		return 1;
	return 0;
}
#else
static inline int current_is_fg(void)
{
	return 0;
}

static inline int task_is_fg(struct task_struct *tsk)
{
	return 0;
}

static inline bool is_fg(int uid)
{
	return false;
}
#endif
#endif /*_FG_H_*/
