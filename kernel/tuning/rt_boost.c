#include <linux/sched.h>
#include <../kernel/sched/sched.h>
#ifdef CONFIG_OPLUS_FEATURE_IM
#include <linux/im/im.h>
#endif
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <linux/sched_assist/sched_assist_common.h>
#endif
#include "rt_boost.h"

#define ANIM_BOOST_UTIL 300

unsigned long sysctl_anim_boost_util_mid = ANIM_BOOST_UTIL;
unsigned long sysctl_anim_boost_util_max = ANIM_BOOST_UTIL;

static bool is_anim(void)
{
	bool is_anim = false;
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	is_anim = sched_assist_scene(SA_ANIM);
#endif
	return is_anim;
}

static bool is_anim_tasks(struct task_struct *p)
{
#ifdef CONFIG_OPLUS_FEATURE_IM
	if (im_hwc(p) || im_hwbinder(p))
		return true;
#endif

#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	if (task_is_sf_group(p))
		return true;
#endif
	return false;
}

bool should_honor_rt_sync(struct rq *rq, struct task_struct *p,
					     bool sync)
{
	if (!is_anim())
		return false;

	return sync &&
		p->prio <= rq->rt.highest_prio.next &&
		rq->rt.rt_nr_running <= 2;
}


bool task_need_anim_boost(struct task_struct *p, int src_cpu, int dest_cpu)
{
	if (!is_anim() || p == NULL || src_cpu < 0 || dest_cpu < 0)
		return false;

	if (is_anim_tasks(p) && is_min_capacity_cpu(dest_cpu)
		&& !is_min_capacity_cpu(src_cpu)) {
		return true;
	}
	return false;
}

bool anim_boost_waker(struct task_struct *p)
{
	if (!is_anim() || p == NULL)
		return false;

#ifdef CONFIG_OPLUS_SF_BOOST
	if (p->compensate_need == 2) {
		return true;
	}
#endif
	return false;
}

unsigned long cpu_anim_util(int cpu)
{
	if (cpu < 0)
		return 0;

	if (!is_anim() || is_min_capacity_cpu(cpu))
		return 0;

	if (is_max_capacity_cpu(cpu)) {
		return sysctl_anim_boost_util_max;
	} else {
		return sysctl_anim_boost_util_mid;
	}
}
