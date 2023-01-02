#include <linux/sched.h>
#ifdef CONFIG_OPLUS_FEATURE_IM
#include <linux/im/im.h>
#endif

#include <../kernel/sched/sched.h>
#include <../kernel/sched/walt.h>
#include "webview_boost.h"

#define TOPAPP 4
#define MIN_BOOST_VLAUE 15
#define DEBUG 0
#define WEB_ID_MAX 8
#define CHILD_MAX 3
#define ERROR_CODE -1

static struct web_target {
       char val[16];
       char *desc[CHILD_MAX];
} web_target[WEB_ID_MAX] = {
       {"cent.mm:toolsmp", {"Chrome_InProc", "Compositor"} },
       {"m.taobao.taobao", {"Chrome_IOThread", "Chrome_ChildIOT", "Chrome_InProc"} },
       {"vilege_process0", {"Chrome_ChildIOT", "Compositor"} },
};

bool is_top(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	if (p == NULL)
		return false;

	rcu_read_lock();
	css = task_css(p, schedtune_cgrp_id);
	if (!css) {
		rcu_read_unlock();
		return false;
	}
	rcu_read_unlock();

	return css->id == TOPAPP;
}

void task_rename_hook(struct task_struct *p)
{
	struct task_struct *leader = p->group_leader;
	int im_flag = IM_WEBVIEW;
	char *buf = p->comm;
	int i = 0, j = 0;
	size_t tlen = 0;

	for (i = 0; i < WEB_ID_MAX; ++i) {
		tlen = strlen(web_target[i].val);
		if (tlen == 0)
			break;

		if (strnstr(leader->comm, web_target[i].val, strlen(leader->comm))) {
#ifdef OPLUS_FEATURE_SCHED_ASSIST
			for (j = 0; j < CHILD_MAX; ++j) {
				char *temp = web_target[i].desc[j];
				if (!temp)
					break;

				if ((p->prio < DEFAULT_PRIO) && strnstr(buf, temp, strlen(buf))) {
					oplus_set_im_flag(p, im_flag);
					if (DEBUG) {
						pr_info("record webview: pid=%d comm=%s prio=%d leader_pid=%d leader_comm=%s\n",
						p->pid, buf, p->prio, leader->pid, leader->comm);
					}
					break;
				} else if (oplus_get_im_flag(p) == IM_WEBVIEW) {
					oplus_unset_im_flag(p, IM_WEBVIEW);
				}
			}
#endif /* OPLUS_FEATURE_SCHED_ASSIST */
			break;
		}
	}

}

bool is_webview(struct task_struct *p)
{
	if (!is_top(p))
		return false;

	if (oplus_get_im_flag(p) == IM_WEBVIEW) {
		return true;
	}

	return false;
}

bool is_webview_boost(struct task_struct *p)
{
	if (is_webview(p) && task_util(p) >= MIN_BOOST_VLAUE)
		return true;

	return false;
}

extern capacity_spare_without(int cpu, struct task_struct *p);
int find_webview_cpu(struct task_struct *p)
{
	int i;
	int max_spare_cap_cpu = -1;
	int active_cpu = -1;
	unsigned long spare_cap = 0, max_spare_cap = 0;
	int index = (num_sched_clusters > 1) ? 1 : 0;
	struct cpumask *preferred_cpus = NULL;
	struct task_struct *curr = NULL;
	struct rq *rq = NULL;
	cpumask_t search_cpus = CPU_MASK_NONE;
	struct sched_cluster *preferred_cluster = sched_cluster[index];
	if (!preferred_cluster)
		return ERROR_CODE;

	cpumask_and(&search_cpus, &p->cpus_allowed, cpu_active_mask);
	preferred_cpus = &preferred_cluster->cpus;

	for_each_cpu_and(i, &search_cpus, preferred_cpus) {
		if (active_cpu == -1)
			active_cpu = i;

		if (is_reserved(i))
			continue;

		rq = cpu_rq(i);
		curr = rq->curr;
		if ((curr->prio < MAX_RT_PRIO) || is_webview(curr))
			continue;

		if (available_idle_cpu(i) || (i == task_cpu(p) && p->state == TASK_RUNNING)) {
			return i;
		}

		spare_cap = capacity_spare_without(i, p);
		if (spare_cap > max_spare_cap) {
			max_spare_cap = spare_cap;
			max_spare_cap_cpu = i;
		}
	}
	if (max_spare_cap_cpu == -1)
		max_spare_cap_cpu = active_cpu;

	return max_spare_cap_cpu;
}
