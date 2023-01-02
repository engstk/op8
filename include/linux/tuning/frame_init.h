#ifndef _OPLUS_FRAME_FORK_H_
#define _OPLUS_FRAME_FORK_H_

static inline void init_task_frame(struct task_struct *p)
{
	INIT_LIST_HEAD(&p->fbg_list);
	rcu_assign_pointer(p->fbg, NULL);
	p->fbg_depth = 0;
}
#endif
