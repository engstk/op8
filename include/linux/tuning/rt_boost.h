#ifndef _RT_BOOST__H
#define _RT_BOOST__H
bool should_honor_rt_sync(struct rq *rq, struct task_struct *p, bool sync);
bool task_need_anim_boost(struct task_struct *p, int src_cpu, int dest_cpu);
bool anim_boost_waker(struct task_struct *p);
unsigned long cpu_anim_util(int cpu);
#endif
