// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/sched.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <trace/events/sched.h>
#include <../kernel/sched/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <../fs/proc/internal.h>
#include <linux/thread_info.h>

#include "sched_assist_common.h"
#include "sched_assist_slide.h"
#ifdef CONFIG_MMAP_LOCK_OPT
#include <linux/mm.h>
#include <linux/rwsem.h>
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#ifndef CONFIG_ARCH_HOLI
#include <../kernel/sched/walt/walt.h>
#endif
#else
#include <../kernel/sched/walt.h>
#endif
#if defined(CONFIG_OPLUS_FEATURE_INPUT_BOOST) && defined(CONFIG_OPLUS_FEATURE_IM)
extern bool is_webview(struct task_struct *p);
#endif

#define CREATE_TRACE_POINTS
#include <sched_assist_trace.h>

int ux_min_sched_delay_granularity;
int ux_max_inherit_exist = 1000;
int ux_max_inherit_granularity = 32;
int ux_min_migration_delay = 10;
int ux_max_over_thresh = 2000;
/* for slide boost */
int sysctl_animation_type = 0;
int sysctl_input_boost_enabled = 0;
int sysctl_sched_assist_ib_duration_coedecay = 1;
u64 sched_assist_input_boost_duration = 0;
int sched_assist_ib_duration_coedecay = 1;
#define S2NS_T 1000000
#define BGAPP 3

static int param_ux_debug = 0;
module_param_named(debug, param_ux_debug, uint, 0644);

struct ux_util_record sf_target[SF_GROUP_COUNT] = {
{"surfaceflinger", 0, 0},
{"RenderEngine", 0, 0},
};

pid_t sf_pid = 0;
pid_t re_pid = 0;

bool slide_scene(void)
{
	return sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_ANIM) || sched_assist_scene(SA_INPUT);
}

/* record important process tgid */
pid_t save_audio_tgid;
pid_t save_top_app_tgid;
unsigned int top_app_type;
struct cpumask nr_mask;

struct ux_sched_cputopo ux_sched_cputopo;

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
DEFINE_PER_CPU(struct task_count_rq, task_lb_count);
void init_rq_cpu(int cpu)
{
	per_cpu(task_lb_count, cpu).ux_low = 0;
	per_cpu(task_lb_count, cpu).ux_high = 0;
	per_cpu(task_lb_count, cpu).top_low = 0;
	per_cpu(task_lb_count, cpu).top_high = 0;
	per_cpu(task_lb_count, cpu).foreground_low = 0;
	per_cpu(task_lb_count, cpu).foreground_high = 0;
	per_cpu(task_lb_count, cpu).background_low = 0;
	per_cpu(task_lb_count, cpu).background_high = 0;
}
#endif /* CONFIG_OPLUS_FEATURE_SCHED_SPREAD */

static inline void sched_init_ux_cputopo(void)
{
	int i = 0;

	ux_sched_cputopo.cls_nr = 0;
	for (; i < NR_CPUS; ++i) {
		cpumask_clear(&ux_sched_cputopo.sched_cls[i].cpus);
		ux_sched_cputopo.sched_cls[i].capacity = ULONG_MAX;
	}
}

void update_ux_sched_cputopo(void)
{
	unsigned long prev_cap = 0;
	unsigned long cpu_cap = 0;
	unsigned int cpu = 0;
	int i, insert_idx, cls_nr;
	struct ux_sched_cluster sched_cls;

	/* reset prev cpu topo info */
	sched_init_ux_cputopo();

	/* update new cpu topo info */
	for_each_possible_cpu(cpu) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		cpu_cap = arch_scale_cpu_capacity(cpu);
#else
		cpu_cap = arch_scale_cpu_capacity(NULL, cpu);
#endif

		/* add cpu with same capacity into target sched_cls */
		if (cpu_cap == prev_cap) {
			for (i = 0; i < ux_sched_cputopo.cls_nr; ++i) {
				if (cpu_cap == ux_sched_cputopo.sched_cls[i].capacity) {
					cpumask_set_cpu(cpu, &ux_sched_cputopo.sched_cls[i].cpus);
					break;
				}
			}

			continue;
		}

		cpumask_clear(&sched_cls.cpus);
		cpumask_set_cpu(cpu, &sched_cls.cpus);
		sched_cls.capacity = cpu_cap;
		cls_nr = ux_sched_cputopo.cls_nr;

		if (!cls_nr) {
			ux_sched_cputopo.sched_cls[cls_nr] = sched_cls;
		} else {
			for (i = 0; i <= ux_sched_cputopo.cls_nr; ++i) {
				if (sched_cls.capacity < ux_sched_cputopo.sched_cls[i].capacity) {
					insert_idx = i;
					break;
				}
			}
			if (insert_idx == ux_sched_cputopo.cls_nr) {
				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			} else {
				for (; cls_nr > insert_idx; cls_nr--) {
					ux_sched_cputopo.sched_cls[cls_nr] = ux_sched_cputopo.sched_cls[cls_nr-1];
				}
				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			}
		}
		ux_sched_cputopo.cls_nr++;

		prev_cap = cpu_cap;
	}

	for (i = 0; i < ux_sched_cputopo.cls_nr; i++)
		ux_debug("update ux sched cpu topology [cls_nr:%d cpus:%*pbl cap:%lu]",
			i, cpumask_pr_args(&ux_sched_cputopo.sched_cls[i].cpus), ux_sched_cputopo.sched_cls[i].capacity);
}

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static int entity_before(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static int entity_over(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) > (s64)ux_max_over_thresh * S2NS_T;
}

extern const struct sched_class fair_sched_class;

/* identify ux only opt in some case, but always keep it's id_type, and wont do inherit through test_task_ux() */
bool test_task_identify_ux(struct task_struct *task, int id_type_ux)
{
	if (id_type_ux == SA_TYPE_ID_CAMERA_PROVIDER) {
		struct task_struct *grp_leader = task->group_leader;
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
		if (save_audio_tgid != 0)
			return false;
#endif
		/* consider provider's HwBinder in configstream */
		if ((task->ux_state & SA_TYPE_LISTPICK) && (grp_leader->ux_state & SA_TYPE_ID_CAMERA_PROVIDER))
			return true;
		return (task->ux_state & SA_TYPE_ID_CAMERA_PROVIDER) && (sysctl_sched_assist_scene & SA_CAMERA);
	} else if (id_type_ux == SA_TYPE_ID_ALLOCATOR_SER) {
		if (task && (task->ux_state & SA_TYPE_ID_ALLOCATOR_SER) && (sysctl_sched_assist_scene & SA_CAMERA))
			return true;
	}

	return false;
}

inline bool test_task_ux(struct task_struct *task)
{
	if (unlikely(!sysctl_sched_assist_enabled))
		return false;

	if (!task)
		return false;

	if (task->sched_class != &fair_sched_class)
		return false;

	if (task->ux_state & (SA_TYPE_HEAVY | SA_TYPE_LIGHT | SA_TYPE_ANIMATOR | SA_TYPE_LISTPICK))
		return true;

	return false;
}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
#define NR_IMBALANCE_THRESHOLD (24)
void update_rq_nr_imbalance(int cpu)
{
	int total_nr = 0;
	int i = -1;
	int threshold = NR_IMBALANCE_THRESHOLD;

	/* Note: check without holding rq lock */
	for_each_cpu(i, cpu_active_mask) {
		total_nr += cpu_rq(i)->cfs.nr_running;
		if (idle_cpu(i))
			cpumask_clear_cpu(i, &nr_mask);
	}

	if (!idle_cpu(cpu) && (total_nr >= threshold)) {
		cpumask_set_cpu(cpu, &nr_mask);
	} else {
		cpumask_clear_cpu(cpu, &nr_mask);
	}
}

bool should_force_spread_tasks(void) {
	return !cpumask_empty(&nr_mask);
}

#ifdef CONFIG_CGROUP_SCHED
static inline int task_cgroup_id(struct task_struct *task)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	struct cgroup_subsys_state *css = task_css(task, cpu_cgrp_id);
#else
	struct cgroup_subsys_state *css = task_css(task, schedtune_cgrp_id);
#endif

	return css ? css->id : -1;
}
#else
static inline int task_cgroup_id(struct task_struct *task)
{
	return -1;
}
#endif

int task_lb_sched_type(struct task_struct *tsk)
{
	int cgroup_type = task_cgroup_id(tsk);

	if (test_task_ux(tsk))
		return SA_UX;
	else if (cgroup_type == SA_CGROUP_TOP_APP)
		return SA_TOP;
	else if (cgroup_type == SA_CGROUP_FOREGROUND || cgroup_type == SA_CGROUP_DEFAULT)
		return SA_FG;
	else if (cgroup_type == SA_CGROUP_BACKGROUND)
		return SA_BG;

	return -1;
}

#ifdef CONFIG_SCHED_WALT
bool task_high_load(struct task_struct *tsk)
{
	unsigned int cpu = 0;
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = cpu_rq(cpu)->rd->max_cpu_capacity.val;
	unsigned int margin;
	int sched_type = task_lb_sched_type(tsk);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	unsigned long load = tsk->wts.demand_scaled;
#else
	unsigned long load = tsk->ravg.demand_scaled;
#endif

	if (sched_type == SA_BG)
		load = min(load, tsk->se.avg.util_avg);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	load = clamp(load,
		     uclamp_eff_value(tsk, UCLAMP_MIN),
		     uclamp_eff_value(tsk, UCLAMP_MAX));
#endif

	if (capacity == max_capacity)
		return true;

	if (capacity_orig_of(task_cpu(tsk)) > capacity_orig_of(cpu))
		margin = sched_capacity_margin_down[cpu];
	else
		margin = sched_capacity_margin_up[task_cpu(tsk)];

	return capacity * 1024 < load * margin;
}
#endif
void dec_task_lb(struct task_struct *tsk, struct rq *rq,
	int high_load, int task_type)
{
	int cpu = cpu_of(rq);

	if (high_load == SA_HIGH_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_high--;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_high--;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_high--;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_high--;
			break;
		}
	} else if (high_load == SA_LOW_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_low--;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_low--;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_low--;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_low--;
			break;
		}
	}
}

void inc_task_lb(struct task_struct *tsk, struct rq *rq,
	int high_load, int task_type)
{
	int cpu = cpu_of(rq);

	if (high_load == SA_HIGH_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_high++;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_high++;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_high++;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_high++;
			break;
		}
	} else if (high_load == SA_LOW_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_low++;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_low++;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_low++;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_low++;
			break;
		}
	}
}

void update_load_flag(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = task_high_load(tsk);
	int curr_task_type = task_lb_sched_type(tsk);

	if (tsk->lb_state != 0) {
		int prev_high_load = tsk->lb_state & 0x1;
		int prev_task_type = (tsk->lb_state >> 1) & 0x7;

		if (prev_high_load == curr_high_load && prev_task_type == curr_task_type)
			return;
		else
			dec_task_lb(tsk, rq, prev_high_load, prev_task_type);
	}
	inc_task_lb(tsk, rq, curr_high_load, curr_task_type);
	tsk->lb_state = (curr_task_type << 1) | curr_high_load;
}

void inc_ld_stats(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = tsk->lb_state & 0x1;
	int curr_task_type = (tsk->lb_state >> 1) & 0x7;

	inc_task_lb(tsk, rq, curr_high_load, curr_task_type);
	tsk->ld_flag = 1;
}

void dec_ld_stats(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = tsk->lb_state & 0x1;
	int curr_task_type = (tsk->lb_state >> 1) & 0x7;

	tsk->ld_flag = 0;
	dec_task_lb(tsk, rq, curr_high_load, curr_task_type);
}

#define MAX_ADJ_NICE (5)
#define MAX_ADJ_PRIO (DEFAULT_PRIO + MAX_ADJ_NICE - 1)
const int sched_prio_to_weight_bg[MAX_ADJ_NICE] = { 716, 601, 510, 438, 380, };
const int sched_prio_to_wmult_bg[MAX_ADJ_NICE] = { 5998558, 7146368, 8421505, 9805861, 11302546, };

/* keep same as defined in sched/fair.c */
#define OPLUS_WMULT_CONST	(~0U)
#define OPLUS_WMULT_SHIFT	32

/* keep same as defined in sched/fair.c */
static void __oplus_update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;

	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= OPLUS_WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = OPLUS_WMULT_CONST;
	else
		lw->inv_weight = OPLUS_WMULT_CONST / w;
}

static struct load_weight sa_new_weight(struct sched_entity *se) {
	struct task_struct *task = NULL;
	struct load_weight lw;
	int sched_type = -1;
	int idx = -1;

	if (!is_spread_task_enabled() || !entity_is_task(se)) {
		return se->load;
	}

	task = container_of(se, struct task_struct, se);
	if ((task->static_prio < DEFAULT_PRIO) || (task->static_prio > MAX_ADJ_PRIO) || (task_util(task) < 51))
		return se->load;

	/* skip important process such as audio proc */
	if (save_audio_tgid && (task->tgid == save_audio_tgid))
		return se->load;

	/* we only adjust background group's load weight */
	sched_type = task_lb_sched_type(task);
	if (sched_type != SA_BG)
		return se->load;

	idx = task->static_prio - DEFAULT_PRIO;
	lw.weight = scale_load(sched_prio_to_weight_bg[idx]);
	lw.inv_weight = sched_prio_to_wmult_bg[idx];

	return lw;
}

u64 sa_calc_delta(struct sched_entity *se, u64 delta_exec, unsigned long weight, struct load_weight *lw, bool calc_fair)
{
	u64 fact = 0;
	u32 inv_weight = 0;
	int shift = OPLUS_WMULT_SHIFT;
	struct load_weight sa_lw = sa_new_weight(se);

	if (calc_fair && (sa_lw.weight == lw->weight) && (lw->weight == NICE_0_LOAD))
		return delta_exec;

	__oplus_update_inv_weight(lw);

	if (calc_fair) { /* __calc_delta(delta, NICE_0_LOAD, &se->load) */
		fact = scale_load_down(weight);
		inv_weight = sa_lw.inv_weight;
	} else { /* __calc_delta(slice, sa_lw.weight, load) */
		fact = scale_load_down(sa_lw.weight);
		inv_weight = lw->inv_weight;
	}

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}

bool should_force_adjust_vruntime(struct sched_entity *se)
{
	struct task_struct *se_task = NULL;

	if (!is_spread_task_enabled())
		return false;

	if (!entity_is_task(se))
		return false;

	se_task = task_of(se);
	/* requeue runnable inherit ux task should be adjusted */
	if (se_task && (se_task->ux_state & SA_TYPE_INHERIT))
		return true;

	return false;
}

void printf_cpu_spread_nr_info(unsigned int cpu, char *nr_info, int info_size)
{
	int rq_ux_nr = 0, rq_top_nr = 0, rq_fg_nr = 0, rq_bg_nr = 0;

	if (cpu >= 0 && cpu < NR_CPUS) {
		rq_ux_nr = per_cpu(task_lb_count, cpu).ux_low +
			per_cpu(task_lb_count, cpu).ux_high;
		rq_top_nr = per_cpu(task_lb_count, cpu).top_low +
			per_cpu(task_lb_count, cpu).top_high;
		rq_fg_nr = per_cpu(task_lb_count, cpu).foreground_low +
			per_cpu(task_lb_count, cpu).foreground_high;
		rq_bg_nr = per_cpu(task_lb_count, cpu).background_low +
			per_cpu(task_lb_count, cpu).background_high;

		scnprintf(nr_info, info_size, "(%d:%d:%d:%d:%d)",
			cpu_rq(cpu)->nr_running, rq_ux_nr, rq_top_nr, rq_fg_nr, rq_bg_nr);
	}
}

static void find_spread_lowest_nr_cpu(struct task_struct *p, cpumask_t *visit_cpus_t, int sched_type, int prev_cpu, int skip_cpu,
	int *lowest_nr, int *lowest_nr_load, int *lowest_nr_cpu)
{
	int i = 0;

	for_each_cpu(i, visit_cpus_t) {
		int ux_nr = 0;
		int top_nr = 0;
		int fg_nr = 0;
		int bg_nr = 0;
		int rq_nr = 0;
		int rq_nr_load = 0;

		if (!cpu_active(i) || cpu_isolated(i))
			continue;

		if (is_reserved(i))
			continue;

		if (sched_cpu_high_irqload(i))
			continue;

		if (skip_cpu == i)
			continue;

		ux_nr = per_cpu(task_lb_count, i).ux_high + per_cpu(task_lb_count, i).ux_low;
		top_nr = per_cpu(task_lb_count, i).top_high + per_cpu(task_lb_count, i).top_low;
		fg_nr = per_cpu(task_lb_count, i).foreground_high + per_cpu(task_lb_count, i).foreground_low;
		bg_nr = per_cpu(task_lb_count, i).background_high + per_cpu(task_lb_count, i).background_low;

		if (sched_type == SA_UX) {
			rq_nr = ux_nr;
		} else if (sched_type == SA_TOP) {
			rq_nr = ux_nr + top_nr;
		} else if (sched_type == SA_FG) {
			rq_nr = ux_nr + top_nr + fg_nr;
		} else if (sched_type == SA_BG) {
			rq_nr = ux_nr + top_nr + fg_nr + bg_nr;
		}
		rq_nr_load = 1000 * ux_nr + 100 * top_nr + 10 * fg_nr + bg_nr;

		if (rq_nr > *lowest_nr) {
			continue;
		}

		if (rq_nr == *lowest_nr) {
			if (rq_nr_load < *lowest_nr_load)
				goto find;
			if (rq_nr_load == *lowest_nr_load && i == prev_cpu)
				goto find;

			continue;
		}

find:
		*lowest_nr = rq_nr;
		*lowest_nr_load = rq_nr_load;
		*lowest_nr_cpu = i;
	}
}

#if !defined(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM) || (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
void sched_assist_spread_tasks(struct task_struct *p, cpumask_t new_allowed_cpus,
		int start_cpu, int skip_cpu, cpumask_t *cpus, bool strict)
{
	int sched_type = task_lb_sched_type(p);
	int cluster;
	cpumask_t visit_cpus;
	int lowest_nr = INT_MAX;
	int lowest_nr_load = INT_MAX;
	int lowest_nr_cpu = -1;
	int prev_cpu = task_cpu(p);
	bool force_spread = false;
	int tmp_idx = 0;
	int order_index = 0;
	int end_index = 0;
	struct ux_sched_cputopo cputopo = ux_sched_cputopo;

	if (!is_spread_task_enabled())
		return;

	if (cputopo.cls_nr <= 1 || sched_type == -1)
		return;

	for (cluster = 0; cluster < cputopo.cls_nr; cluster++) {
		if (start_cpu == cpumask_first(&cputopo.sched_cls[cluster].cpus)) {
			order_index = cluster;
			break;
		}
	}

	/* add for launch boost */
	if (sched_assist_scene(SA_LAUNCH) && is_heavy_ux_task(p)) {
		strict = true;
		order_index = cputopo.cls_nr - 1;
		end_index = (p->pid == p->tgid) ? 0 : 1;
	}

	/* in launch or animation scene, force scheduler to spread tasks */
	if (should_force_spread_tasks())
		force_spread = true;
	else
		return;

	tmp_idx = order_index;
	for (cluster = 0; cluster < cputopo.cls_nr; cluster++) {
		cpumask_and(&visit_cpus, &new_allowed_cpus,
				&cputopo.sched_cls[tmp_idx].cpus);

		find_spread_lowest_nr_cpu(p, &visit_cpus, sched_type, prev_cpu, skip_cpu,
			&lowest_nr, &lowest_nr_load, &lowest_nr_cpu);

		/*
		 * order_index <= 1, order is cluster(0-1-2 or 1-2-0)
		 * order_index > 1,  order is cluster(2-1-0)
		*/
		if (order_index <= 1) {
			tmp_idx++;
		} else {
			tmp_idx--;
		}
		if (tmp_idx >= cputopo.cls_nr)
			tmp_idx = 0;
		if (tmp_idx < 0)
			tmp_idx = cputopo.cls_nr - 1;

		/* should we visit next cluster? */
		if (strict && cluster >= end_index) {
			break;
		}

		if (force_spread)
			continue;

		break;
	}

	if (lowest_nr_cpu != -1) {
		cpumask_set_cpu(lowest_nr_cpu, cpus);
		trace_sched_assist_spread_tasks(p, sched_type, lowest_nr_cpu);
	}
}
#else
void sched_assist_spread_tasks(struct task_struct *p, cpumask_t new_allowed_cpus,
		int order_index, int end_index, int skip_cpu, cpumask_t *cpus, bool strict)
{
	int sched_type = task_lb_sched_type(p);
	int cluster;
	cpumask_t visit_cpus;
	int lowest_nr = INT_MAX;
	int lowest_nr_load = INT_MAX;
	int lowest_nr_cpu = -1;
	int prev_cpu = task_cpu(p);
	bool force_spread = false;

	if (!is_spread_task_enabled())
		return;

	if (num_sched_clusters <= 1 || sched_type == -1)
		return;

	/* add for launch boost */
	if (sched_assist_scene(SA_LAUNCH) && is_heavy_ux_task(p)) {
		strict = true;
		order_index = num_sched_clusters - 1;
		end_index = (p->pid == p->tgid) ? 0 : 1;
	}

	/* in launch or animation scene, force scheduler to spread tasks */
	if (should_force_spread_tasks())
		force_spread = true;
	else
		return;

	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		cpumask_and(&visit_cpus, &new_allowed_cpus,
				&cpu_array[order_index][cluster]);

		find_spread_lowest_nr_cpu(p, &visit_cpus, sched_type, prev_cpu, skip_cpu,
			&lowest_nr, &lowest_nr_load, &lowest_nr_cpu);

		/* should we visit next cluster? */
		if (strict && cluster >= end_index) {
			break;
		}

		if (force_spread)
			continue;

		break;
	}

	if (lowest_nr_cpu != -1) {
		cpumask_set_cpu(lowest_nr_cpu, cpus);
		trace_sched_assist_spread_tasks(p, sched_type, lowest_nr_cpu);
	}
}
#endif
#endif /* CONFIG_OPLUS_FEATURE_SCHED_SPREAD */

inline int get_ux_state_type(struct task_struct *task)
{
	if (!task) {
		return UX_STATE_INVALID;
	}

	if (task->ux_state & SA_TYPE_INHERIT)
		return UX_STATE_INHERIT;

	if (task->ux_state & (SA_TYPE_HEAVY | SA_TYPE_LIGHT | SA_TYPE_ANIMATOR | SA_TYPE_LISTPICK))
		return UX_STATE_SCHED_ASSIST;

	return UX_STATE_NONE;
}

inline bool test_list_pick_ux(struct task_struct *task)
{
#if defined(CONFIG_OPLUS_FEATURE_INPUT_BOOST) && defined(CONFIG_OPLUS_FEATURE_IM)
	if (is_webview(task))
		return true;
#endif
	return (task->ux_state & SA_TYPE_LISTPICK) || (task->ux_state & SA_TYPE_ONCE_UX) ||
		test_task_identify_ux(task, SA_TYPE_ID_ALLOCATOR_SER);
}

void enqueue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;
	bool exist = false;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!rq || !p || !list_empty(&p->ux_entry)) {
		return;
	}

	p->enqueue_time = rq->clock;
	if (test_list_pick_ux(p)) {
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				exist = true;
				break;
			}
		}
		if (!exist) {
			list_add_tail(&p->ux_entry, &rq->ux_thread_list);
			get_task_struct(p);
		}
	}
}

void dequeue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;

	if (!rq || !p) {
		return;
	}
	p->enqueue_time = 0;
	if (!list_empty(&p->ux_entry)) {
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				list_del_init(&p->ux_entry);
				if (p->ux_state & SA_TYPE_ONCE_UX) {
					p->ux_state &= ~SA_TYPE_ONCE_UX;
				}
				put_task_struct(p);
				return;
			}
		}
	}
}

static struct task_struct *pick_first_ux_thread(struct rq *rq)
{
	struct list_head *ux_thread_list = &rq->ux_thread_list;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct task_struct *temp = NULL;
	struct task_struct *leftmost_task = NULL;
	list_for_each_safe(pos, n, ux_thread_list) {
		temp = list_entry(pos, struct task_struct, ux_entry);
		/*ensure ux task in current rq cpu otherwise delete it*/
		if (unlikely(task_cpu(temp) != rq->cpu)) {
			list_del_init(&temp->ux_entry);
			continue;
		}

		if (leftmost_task == NULL) {
			leftmost_task = temp;
		} else if (entity_before(&temp->se, &leftmost_task->se)) {
			leftmost_task = temp;
		}
	}

	return leftmost_task;
}

void pick_ux_thread(struct rq *rq, struct task_struct **p, struct sched_entity **se)
{
	struct task_struct *ori_p = *p;
	struct task_struct *key_task;
	struct sched_entity *key_se;

	if (!rq || !ori_p || !se) {
		return;
	}

#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	if (ori_p->oplus_task_info.im_small)
		return;
#endif

	if ((ori_p->ux_state & SA_TYPE_ANIMATOR) || test_list_pick_ux(ori_p))
		return;

	if (!list_empty(&rq->ux_thread_list)) {
		key_task = pick_first_ux_thread(rq);
		/* in case that ux thread keep running too long */
		if (key_task && entity_over(&key_task->se, &ori_p->se))
			return;

		if (key_task) {
			key_se = &key_task->se;
			if (key_se && (rq->clock >= key_task->enqueue_time) &&
			rq->clock - key_task->enqueue_time >= ((u64)ux_min_sched_delay_granularity * S2NS_T)) {
				*p = key_task;
				*se = key_se;
			}
		}
	}
}

#define INHERIT_UX_SEC_WIDTH   8
#define INHERIT_UX_MASK_BASE   0x00000000ff

#define inherit_ux_offset_of(type) (type * INHERIT_UX_SEC_WIDTH)
#define inherit_ux_mask_of(type) ((u64)(INHERIT_UX_MASK_BASE) << (inherit_ux_offset_of(type)))
#define inherit_ux_get_bits(value, type) ((value & inherit_ux_mask_of(type)) >> inherit_ux_offset_of(type))
#define inherit_ux_value(type, value) ((u64)value << inherit_ux_offset_of(type))


bool test_inherit_ux(struct task_struct *task, int type)
{
	u64 inherit_ux;
	if (!task) {
		return false;
	}
	inherit_ux = atomic64_read(&task->inherit_ux);
	return inherit_ux_get_bits(inherit_ux, type) > 0;
}

static bool test_task_exist(struct task_struct *task, struct list_head *head)
{
	struct list_head *pos, *n;
	list_for_each_safe(pos, n, head) {
		if (pos == &task->ux_entry) {
			return true;
		}
	}
	return false;
}

inline void inherit_ux_inc(struct task_struct *task, int type)
{
	atomic64_add(inherit_ux_value(type, 1), &task->inherit_ux);
}

inline void inherit_ux_sub(struct task_struct *task, int type, int value)
{
	atomic64_sub(inherit_ux_value(type, value), &task->inherit_ux);
}

static void __inherit_ux_dequeue(struct task_struct *task, int type, int value)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	bool exist = false;
	struct rq *rq = NULL;
	u64 inherit_ux = 0;

	rq = task_rq_lock(task, &flags);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux <= 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	inherit_ux_sub(task, type, value);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	task->ux_depth = 0;

	exist = test_task_exist(task, &rq->ux_thread_list);
	if (exist) {
		list_del_init(&task->ux_entry);
		put_task_struct(task);
	}
	task_rq_unlock(rq, task, &flags);
}

void inherit_ux_dequeue(struct task_struct *task, int type)
{
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
	__inherit_ux_dequeue(task, type, 1);
}
void inherit_ux_dequeue_refs(struct task_struct *task, int type, int value)
{
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
	__inherit_ux_dequeue(task, type, value);
}

static void __inherit_ux_enqueue(struct task_struct *task, int type, int depth)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	bool exist = false;
	struct rq *rq = NULL;

	rq = task_rq_lock(task, &flags);

	if (unlikely(!list_empty(&task->ux_entry))) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	inherit_ux_inc(task, type);
	task->inherit_ux_start = jiffies_to_nsecs(jiffies);
	task->ux_depth = task->ux_depth > depth + 1 ? task->ux_depth : depth + 1;
	if (task->state == TASK_RUNNING && task->sched_class == &fair_sched_class) {
		exist = test_task_exist(task, &rq->ux_thread_list);
		if (!exist) {
			get_task_struct(task);
			list_add_tail(&task->ux_entry, &rq->ux_thread_list);
		}
	}
	task_rq_unlock(rq, task, &flags);
}

void inherit_ux_enqueue(struct task_struct *task, int type, int depth)
{
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
	__inherit_ux_enqueue(task, type, depth);
}

inline bool test_task_ux_depth(int ux_depth)
{
	return ux_depth < UX_DEPTH_MAX;
}

inline bool test_set_inherit_ux(struct task_struct *tsk)
{
	return tsk && test_task_ux(tsk) && test_task_ux_depth(tsk->ux_depth);
}

void ux_init_rq_data(struct rq *rq)
{
	if (!rq) {
		return;
	}

	INIT_LIST_HEAD(&rq->ux_thread_list);
#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	cpumask_clear(&nr_mask);

	per_cpu(task_lb_count, cpu_of(rq)).ux_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).ux_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).top_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).top_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).foreground_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).foreground_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).background_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).background_high = 0;
#endif
}

int ux_prefer_cpu[NR_CPUS] = {0};
void ux_init_cpu_data(void) {
	int i = 0;
	int min_cpu = 0, ux_cpu = 0;

	for (; i < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = -1;
	}

	ux_cpu = cpumask_weight(topology_core_cpumask(min_cpu));
	if (ux_cpu == 0) {
		ux_warn("failed to init ux cpu data\n");
		return;
	}

	for (i = 0; i < nr_cpu_ids && ux_cpu < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = ux_cpu++;
	}
}

bool test_ux_task_cpu(int cpu) {
	return (cpu >= ux_prefer_cpu[0]);
}

bool test_ux_prefer_cpu(struct task_struct *tsk, int cpu) {
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	if (cpu < 0)
		return false;

	if (tsk->pid == tsk->tgid) {
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		return cpu >= rd->wrd.max_cap_orig_cpu;
#else
		return cpu >= rd->max_cap_orig_cpu;
#endif
#else
		return capacity_orig_of(cpu) >= rd->max_cpu_capacity.val;
#endif /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/
	}

	return (cpu >= ux_prefer_cpu[0]);
}

void find_ux_task_cpu(struct task_struct *tsk, int *target_cpu) {
	int i = 0;
	int temp_cpu = 0;
	struct rq *rq = NULL;
	for (i = (nr_cpu_ids - 1); i >= 0; --i) {
		temp_cpu = ux_prefer_cpu[i];
		if (temp_cpu <= 0 || temp_cpu >= nr_cpu_ids)
			continue;

		rq = cpu_rq(temp_cpu);
		if (!rq)
			continue;

		if (rq->curr->prio <= MAX_RT_PRIO)
			continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) && !cpu_isolated(temp_cpu)
			&& cpumask_test_cpu(temp_cpu, tsk->cpus_ptr)) {
#else
		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) && !cpu_isolated(temp_cpu)
			&& cpumask_test_cpu(temp_cpu, &tsk->cpus_allowed)) {
#endif
			*target_cpu = temp_cpu;
			return;
		}
	}
	return;
}

static inline bool oplus_is_min_capacity_cpu(int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if (unlikely(cls_nr <= 0))
		return false;

	return capacity_orig_of(cpu) == ux_cputopo.sched_cls[0].capacity;
}

#ifdef CONFIG_SCHED_WALT
/* 2ms default for 20ms window size scaled to 1024 */
#define THRESHOLD_BOOST (102)
bool sched_assist_task_misfit(struct task_struct *task, int cpu, int flag)
{
	bool sum_over = false;
	bool demand_over = false;

	if (unlikely(!sysctl_sched_assist_enabled))
		return false;

	if (!test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER))
		return false;

	/* for SA_TYPE_ID_CAMERA_PROVIDER */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	sum_over = scale_demand(task->wts.sum) >= THRESHOLD_BOOST;
#else
	sum_over = scale_demand(task->ravg.sum) >= THRESHOLD_BOOST;
#endif

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	demand_over = task_util(task) >= THRESHOLD_BOOST;
#else
	demand_over = false;
#endif

	if ((sum_over || demand_over) && oplus_is_min_capacity_cpu(cpu)) {
		return true;
	}

	return false;
}
#endif

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
extern unsigned long capacity_curr_of(int cpu);
#endif
/*
 * taget cpu is
 *  unboost: the one in all domain, with lowest prio running task
 *  boost:   the one in power domain, with lowest prio running task which is not ux
 *  !!this func will ignore task's start cpu
*/
int set_ux_task_cpu_common_by_prio(struct task_struct *task, int *target_cpu, bool boost, bool prefer_idle, unsigned int type)
{
	int i;
	int lowest_prio = INT_MIN;
	unsigned long lowest_prio_max_cap = 0;
	int ret = -1;

	if (unlikely(!sysctl_sched_assist_enabled))
		return -1;

	if (!(task->ux_state & SA_TYPE_ANIMATOR) && !test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER))
		return -1;

	if ((*target_cpu < 0) || (*target_cpu >= NR_CPUS))
		return -1;

#ifdef CONFIG_SCHED_WALT
	if (test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		boost = ((scale_demand(task->wts.sum) >= THRESHOLD_BOOST) ||
			(task_util(task) >= THRESHOLD_BOOST)) ? true : false;
#else /* KERNEL-5-4 */

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		boost = ((scale_demand(task->ravg.sum) >= THRESHOLD_BOOST) ||
			(task_util(task) >= THRESHOLD_BOOST)) ? true : false;
#else
		boost = (scale_demand(task->ravg.sum) >= THRESHOLD_BOOST);
#endif

#endif /* KERNEL-5-4 */
	}
#endif /* CONFIG_SCHED_WALT */

	for_each_cpu(i, cpu_active_mask) {
		unsigned long capacity_curr;
		struct task_struct *curr;
		bool curr_ux = false;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (!cpumask_test_cpu(i, task->cpus_ptr) || cpu_isolated(i))
#else
		if (!cpumask_test_cpu(i, &task->cpus_allowed) || cpu_isolated(i))
#endif
			continue;

		/* avoid placing task into high power cpu and break it's idle state if !prefer_idle */
		if (prefer_idle && idle_cpu(i)) {
			*target_cpu = i;
			return 0;
		}

		curr = cpu_rq(i)->curr;
		/* avoid placing task into cpu with rt */
		if (!curr || !(curr->sched_class == &fair_sched_class))
			continue;

		curr_ux = test_task_ux(curr) || test_task_identify_ux(curr, SA_TYPE_ID_CAMERA_PROVIDER);
		if (curr_ux)
			continue;

		capacity_curr = capacity_curr_of(i);
		if ((curr->prio > lowest_prio) || (boost && (capacity_curr > lowest_prio_max_cap))) {
			lowest_prio = curr->prio;
			lowest_prio_max_cap = capacity_curr;
			*target_cpu = i;
			ret = 0;
		}
	}

	return ret;
}

bool is_sf(struct task_struct *p)
{
	char sf_name[] = "surfaceflinger";
	return (strcmp(p->comm, sf_name) == 0) && (p->pid == p->tgid);
}

void drop_ux_task_cpus(struct task_struct *p, struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);
#ifdef CONFIG_SCHED_WALT
	bool sf = false;
#endif

	while (cpu < nr_cpu_ids) {
		/* unlocked access */
		struct task_struct *task = READ_ONCE(cpu_rq(cpu)->curr);

		if ((sysctl_sched_assist_scene & SA_LAUNCH) && (task->ux_state & SA_TYPE_HEAVY))
			cpumask_clear_cpu(cpu, lowest_mask);

		if (test_task_ux(task) || !list_empty(&task->ux_entry) ||
			(test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER) && oplus_is_min_capacity_cpu(cpu))) {
			cpumask_clear_cpu(cpu, lowest_mask);
		}

#ifdef CONFIG_SCHED_WALT
		if (slide_scene()) {
			sf = is_sf(p);
			if (sf && (cpu < ux_prefer_cpu[0] || (task->ux_state & SA_TYPE_HEAVY)))
				cpumask_clear_cpu(cpu, lowest_mask);
		}
#endif

		cpu = cpumask_next(cpu, lowest_mask);
	}
}

static inline bool test_sched_assist_ux_type(struct task_struct *task, unsigned int sa_ux_type)
{
	return task->ux_state & sa_ux_type;
}

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

#define MALI_THREAD_NAME "mali-cmar-backe"
#define LAUNCHER_THREAD_NAME "ndroid.launcher"
#define ALLOCATOR_THREAD_NAME "allocator-servi"
#define CAMERA_PROVIDER_NAME "provider@2.4-se"

#define CAMERA_MAINTHREAD_NAME "com.oplus.camera"
#define OPLUS_CAMERA_MAINTHREAD_NAME "om.oplus.camera"
#define CAMERA_PREMR_NAME "previewManagerR"
#define CAMERA_PREPT_NAME "PreviewProcessT"
#define CAMERA_HALCONT_NAME "Camera Hal Cont"
#define CAMERA_IMAGEPROC_NAME "ImageProcessThr"
extern pid_t sf_pid;
extern pid_t re_pid;
void sched_assist_target_comm(struct task_struct *task)
{
	struct task_struct *grp_leader = task->group_leader;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!grp_leader || (get_ux_state_type(task) != UX_STATE_NONE))
		return;
	if (strstr(task->comm, "surfaceflinger") && strstr(grp_leader->comm, "surfaceflinger")) {
			sf_pid = task->pid;
			return;
	}

	if (strstr(task->comm, "RenderEngine") && strstr(grp_leader->comm, "surfaceflinger")) {
			re_pid = task->pid;
			return;
	}
#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	/* mali thread only exist in mtk platform */
	if (strstr(grp_leader->comm, LAUNCHER_THREAD_NAME) && strstr(task->comm, MALI_THREAD_NAME)) {
		task->ux_state |= SA_TYPE_ANIMATOR;
		return;
	}
#endif

	if (strstr(grp_leader->comm, CAMERA_PROVIDER_NAME) && strstr(task->comm, CAMERA_PROVIDER_NAME)) {
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
		return;
	}

	if ((strstr(grp_leader->comm, CAMERA_MAINTHREAD_NAME) || strstr(grp_leader->comm, OPLUS_CAMERA_MAINTHREAD_NAME)) && (strstr(task->comm, CAMERA_PREMR_NAME)
		|| strstr(task->comm, CAMERA_PREPT_NAME)
		|| strstr(task->comm, CAMERA_HALCONT_NAME)
		|| strstr(task->comm, CAMERA_IMAGEPROC_NAME))) {
		task->ux_state |= SA_TYPE_LIGHT;
		return;
	}

	if (!strncmp(grp_leader->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN) || !strncmp(task->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN)) {
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
	}

	return;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/* An entity is a task if it doesn't "own" a runqueue */
#define oplus_entity_is_task(se)	(!se->my_q)
#else
#define oplus_entity_is_task(se)	(1)
#endif

void place_entity_adjust_ux_task(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;
	unsigned long thresh = sysctl_sched_latency;
	unsigned long launch_adjust = 0;
	struct task_struct *se_task = NULL;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!oplus_entity_is_task(se) || initial)
		return;

	if (sysctl_sched_assist_scene & SA_LAUNCH)
		launch_adjust = sysctl_sched_latency;

	se_task = task_of(se);

#ifdef CONFIG_MMAP_LOCK_OPT
	if (se_task->ux_once) {
		vruntime -= 3 * thresh;
		se->vruntime = vruntime;
		se_task->ux_once = 0;
		return;
	}
#endif

	if (test_sched_assist_ux_type(se_task, SA_TYPE_ANIMATOR)) {
		vruntime -= 3 * thresh + (thresh >> 1);
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}

	if (test_sched_assist_ux_type(se_task, SA_TYPE_LIGHT | SA_TYPE_HEAVY)) {
		vruntime -= 2 * thresh;
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}

	if (test_task_identify_ux(se_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		vruntime -= 2 * thresh + (thresh >> 1);
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	if (should_force_adjust_vruntime(se)) {
		se->vruntime = vruntime - (thresh + (thresh >> 1));
		return;
	}
#endif
}

bool should_ux_preempt_wakeup(struct task_struct *wake_task, struct task_struct *curr_task)
{
	bool wake_ux = false;
	bool curr_ux = false;
#if defined(CONFIG_OPLUS_FEATURE_INPUT_BOOST) && defined(CONFIG_OPLUS_FEATURE_IM)
	bool wake_web = false;
	bool curr_web = false;
#endif

	if (!sysctl_sched_assist_enabled)
		return false;

#if defined(CONFIG_OPLUS_FEATURE_INPUT_BOOST) && defined(CONFIG_OPLUS_FEATURE_IM)
	wake_web = is_webview(wake_task);
	curr_web = is_webview(curr_task);
	if (wake_web && !curr_web)
		return true;
	if (!wake_web && curr_web)
		return false;
#endif
	wake_ux = test_task_ux(wake_task) || test_list_pick_ux(wake_task) || test_task_identify_ux(wake_task, SA_TYPE_ID_CAMERA_PROVIDER);
	curr_ux = test_task_ux(curr_task) || test_list_pick_ux(curr_task) || test_task_identify_ux(curr_task, SA_TYPE_ID_CAMERA_PROVIDER);

	/* ux can preemt cfs */
	if (wake_ux && !curr_ux)
		return true;

	/* animator ux can preemt un-animator */
	if ((wake_task->ux_state & SA_TYPE_ANIMATOR) && !(curr_task->ux_state & SA_TYPE_ANIMATOR))
		return true;

	/* heavy type can be preemt by other type */
	if (wake_ux && (curr_task->ux_state & SA_TYPE_HEAVY)) {
		bool preempt = !(wake_task->ux_state & SA_TYPE_HEAVY) && !test_task_identify_ux(wake_task, SA_TYPE_ID_CAMERA_PROVIDER);
		return preempt;
	}

	return false;
}

bool should_ux_task_skip_further_check(struct sched_entity *se)
{
	return oplus_entity_is_task(se) && test_sched_assist_ux_type(task_of(se), SA_TYPE_ANIMATOR);
}

static inline bool is_ux_task_prefer_cpu(struct task_struct *task, int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if(cpu < 0)
        	return false;

	/* only one cluster or init failed */
	if (unlikely(cls_nr <= 0))
		return true;

	if (cpu_rq(cpu)->curr && test_sched_assist_ux_type(cpu_rq(cpu)->curr, SA_TYPE_HEAVY))
		return false;

	if (test_sched_assist_ux_type(task, SA_TYPE_HEAVY)) {
		return capacity_orig_of(cpu) >= ux_cputopo.sched_cls[cls_nr].capacity;
	}

	return true;
}

bool should_ux_task_skip_cpu(struct task_struct *task, unsigned int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	bool is_skip_rt_cpu = true;
	int silver_core_nr = 0;

	if (!sysctl_sched_assist_enabled)
		return false;

	if (test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		if (cpu_rq(cpu)->curr && test_task_ux(cpu_rq(cpu)->curr)) {
			return true;
		}
	}

	if (!test_task_ux(task))
		return false;

	if ((sysctl_sched_assist_scene & SA_LAUNCH) && !is_ux_task_prefer_cpu(task, cpu))
		return true;

	if (cls_nr > 0)
		silver_core_nr = cpumask_weight(&ux_cputopo.sched_cls[0].cpus);

#define SILVER_NR_6 (6)
	/* avoid ux being squeezed to small core by rt in 6+2 architecture*/
	if ((sysctl_sched_assist_scene & SA_ANIM) && (silver_core_nr == SILVER_NR_6)
		&& !oplus_is_min_capacity_cpu(cpu))
		is_skip_rt_cpu = false;

	if (!(sysctl_sched_assist_scene & SA_LAUNCH) || !test_sched_assist_ux_type(task, SA_TYPE_HEAVY)) {
		if (is_skip_rt_cpu && cpu_rq(cpu)->rt.rt_nr_running)
			return true;

		/* avoid placing turbo ux into cpu which has animator ux or list ux */
		if (cpu_rq(cpu)->curr && (test_sched_assist_ux_type(cpu_rq(cpu)->curr, SA_TYPE_ANIMATOR)
			|| !list_empty(&cpu_rq(cpu)->ux_thread_list)))
			return true;
	}

	return false;
}

void set_ux_task_to_prefer_cpu_v1(struct task_struct *task, int *orig_target_cpu, bool *cond) {
	struct rq *rq = NULL;
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	int cpu = 0;

	if (!sysctl_sched_assist_enabled || !(sysctl_sched_assist_scene & SA_LAUNCH))
		return;

	if (unlikely(cls_nr <= 0))
		return;

	if (is_ux_task_prefer_cpu(task, *orig_target_cpu))
		return;
	*cond = true;
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (test_sched_assist_ux_type(rq->curr, SA_TYPE_HEAVY))
			continue;

		if (rq->curr->prio < MAX_RT_PRIO)
			continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
#else
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, &task->cpus_allowed)) {
#endif
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr - 1;
	if (cls_nr > 0)
		goto retry;

	return;
}

void set_ux_task_to_prefer_cpu(struct task_struct *task, int *orig_target_cpu) {
	struct rq *rq = NULL;
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	int cpu = 0;

	if (!sysctl_sched_assist_enabled || !(sysctl_sched_assist_scene & SA_LAUNCH))
		return;

	if (unlikely(cls_nr <= 0))
		return;

	if (is_ux_task_prefer_cpu(task, *orig_target_cpu))
		return;
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (test_sched_assist_ux_type(rq->curr, SA_TYPE_HEAVY))
			continue;

		if (rq->curr->prio < MAX_RT_PRIO)
			continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
#else
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, &task->cpus_allowed)) {
#endif
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr - 1;
	if (cls_nr > 0)
		goto retry;

	return;
}

static int boost_kill = 1;
module_param_named(boost_kill, boost_kill, uint, 0644);
int get_grp(struct task_struct *p)
{
	struct cgroup_subsys_state *css;

	if (p == NULL)
		return false;
	rcu_read_lock();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	css = task_css(p, cpu_cgrp_id);
#else
	css = task_css(p, schedtune_cgrp_id);
#endif
	if (!css) {
		rcu_read_unlock();
		return false;
	}
	rcu_read_unlock();

	return css->id;
}

void oplus_boost_kill_signal(int sig, struct task_struct *cur, struct task_struct *p)
{
	struct task_struct *tmp;
	struct cpumask *new_mask = (struct cpumask *)cpu_possible_mask;

	if (p == NULL)
		return;
	if (sig == SIGKILL && boost_kill && get_grp(p) == BGAPP &&
			p->group_leader && p->group_leader->pid == p->pid) {
		tmp = p;
		rcu_read_lock();
		/*walk all threads for each process */
		do {
			set_user_nice(tmp, -20);
			if (!cpumask_empty(new_mask)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
				cpumask_copy(&tmp->cpus_allowed, new_mask);
				cpumask_copy(&tmp->cpus_requested, new_mask);
#else
				cpumask_copy(&tmp->cpus_mask, new_mask);
				tmp->cpus_ptr = new_mask;
#endif
				tmp->nr_cpus_allowed = cpumask_weight(new_mask);
			}
		}while_each_thread(p, tmp);
		rcu_read_unlock();
	}
}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
static void requeue_runnable_task(struct task_struct *p)
{
	bool queued, running;
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(p, &rf);
	queued = task_on_rq_queued(p);
	running = task_current(rq, p);

	if (!queued || running) {
		task_rq_unlock(rq, p, &rf);
		return;
	}

	update_rq_clock(rq);
	deactivate_task(rq, p, DEQUEUE_NOCLOCK);
	activate_task(rq, p, ENQUEUE_NOCLOCK);
	resched_curr(rq);

	task_rq_unlock(rq, p, &rf);
}
#endif

void set_inherit_ux(struct task_struct *task, int type, int depth, int inherit_val)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq = NULL;
	int old_state = 0;

	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}

	rq = task_rq_lock(task, &flags);

	if (task->sched_class != &fair_sched_class) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	inherit_ux_inc(task, type);
	task->ux_depth = depth + 1;
	old_state = task->ux_state;
	task->ux_state = (inherit_val & SCHED_ASSIST_UX_MASK) | SA_TYPE_INHERIT;
	/* identify type like allocator ux, keep it, but can not inherit  */
	if (old_state & SA_TYPE_ID_ALLOCATOR_SER)
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
	if (old_state & SA_TYPE_ID_CAMERA_PROVIDER)
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
	task->inherit_ux_start = jiffies_to_nsecs(jiffies);

	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	task_rq_unlock(rq, task, &flags);

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	/* requeue runnable task to ensure vruntime adjust */
	requeue_runnable_task(task);
#endif
}

void reset_inherit_ux(struct task_struct *inherit_task, struct task_struct *ux_task, int reset_type)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;
	int reset_depth = 0;
	int reset_inherit = 0;

	if (!inherit_task || !ux_task || reset_type >= INHERIT_UX_MAX) {
		return;
	}

	reset_inherit = ux_task->ux_state;
	reset_depth = ux_task->ux_depth;
	/* animator ux is important, so we just reset in this type */
	if (!test_inherit_ux(inherit_task, reset_type) || !test_sched_assist_ux_type(ux_task, SA_TYPE_ANIMATOR))
		return;

	rq = task_rq_lock(inherit_task, &flags);

	inherit_task->ux_depth = reset_depth + 1;
	/* identify type like allocator ux, keep it, but can not inherit  */
	if (reset_inherit & SA_TYPE_ID_ALLOCATOR_SER)
		reset_inherit &= ~SA_TYPE_ID_ALLOCATOR_SER;
	if (reset_inherit & SA_TYPE_ID_CAMERA_PROVIDER)
		reset_inherit &= ~SA_TYPE_ID_CAMERA_PROVIDER;
	inherit_task->ux_state = (inherit_task->ux_state & ~SCHED_ASSIST_UX_MASK) | reset_inherit;

	sched_assist_systrace_pid(inherit_task->tgid, inherit_task->ux_state, "ux_state %d", inherit_task->pid);

	task_rq_unlock(rq, inherit_task, &flags);
}

void unset_inherit_ux_value(struct task_struct *task, int type, int value)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;
	s64 inherit_ux;

	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}

	rq = task_rq_lock(task, &flags);

	inherit_ux_sub(task, type, value);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	if (inherit_ux < 0) {
		atomic64_set(&(task->inherit_ux), 0);
	}
	task->ux_depth = 0;
	/* identify type like allocator ux, keep it, but can not inherit  */
	task->ux_state &= SA_TYPE_ID_ALLOCATOR_SER | SA_TYPE_ID_CAMERA_PROVIDER;

	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	task_rq_unlock(rq, task, &flags);
}

void unset_inherit_ux(struct task_struct *task, int type)
{
	unset_inherit_ux_value(task, type, 1);
}

void inc_inherit_ux_refs(struct task_struct *task, int type) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;

	rq = task_rq_lock(task, &flags);
	inherit_ux_inc(task, type);
	task_rq_unlock(rq, task, &flags);
}

bool task_is_sf_group(struct task_struct *p)
{
	return (p->pid == sf_pid) || (p->pid == re_pid);
}

void sf_task_util_record(struct task_struct *p)
{
	char comm_now[TASK_COMM_LEN];
	unsigned long walt_util;
	int i = 0;
	int len = 0;
	memset(comm_now, '\0', sizeof(comm_now));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	walt_util = p->wts.demand_scaled;
#else
	walt_util = p->ravg.demand_scaled;
#endif
	if (unlikely(task_is_sf_group(p))) {
		strcpy(comm_now, p->comm);
		len = strlen(comm_now);
		for(i = 0; i < SF_GROUP_COUNT ;i++) {
			if (!strncmp(comm_now, sf_target[i].val, len))
				sf_target[i].util = walt_util;
		}
	}
}

bool sf_task_misfit(struct task_struct *p)
{
	unsigned long  util = 0;
	int i;
	if (task_is_sf_group(p)) {
		for (i = 0; i < SF_GROUP_COUNT; i++) {
			util += sf_target[i].util;
		}
		if ((util > sysctl_boost_task_threshold) && slide_scene())
			return true;
		else
			return false;
	}
	return false;
}

bool oplus_task_misfit(struct task_struct *p, int cpu)
{
#ifdef CONFIG_SCHED_WALT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
	if ((scale_demand(p->wts.sum) >= sysctl_boost_task_threshold ||
		task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
	return true;
	#endif
#else
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if ((scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold ||
		task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
#else
	if ((scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold ||
                READ_ONCE(p->se.avg.util_avg) > sysctl_boost_task_threshold) && cpu < num_mincpu)
#endif
		return true;
#endif
#endif
	return false;
}
void kick_min_cpu_from_mask(struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);
	while(cpu < nr_cpu_ids) {
		if (cpu < ux_prefer_cpu[0]) {
			cpumask_clear_cpu(cpu, lowest_mask);
		}
		cpu = cpumask_next(cpu, lowest_mask);
	}
}

bool ux_skip_sync_wakeup(struct task_struct *task, int *sync)
{
	bool ret = false;

	if (test_sched_assist_ux_type(task, SA_TYPE_ANIMATOR)) {
		*sync = 0;
		ret = true;
	}

	return ret;
}

#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
#define FIT_SMALL_THREASH 3
#define RUNNABLE_MIN_THREASH 20000000
#define RUNNING_MIN_THREASH 3000000

#define TASK_SMALL (0)

static inline bool trace_state_group(struct task_struct *tsk)
{
	return (save_audio_tgid && tsk->tgid == save_audio_tgid);
}

bool sched_assist_pick_next_entity(struct cfs_rq *cfs_rq, struct sched_entity **se)
{
	if (cfs_rq->next && oplus_entity_is_task(cfs_rq->next)) {
		struct task_struct *tsk = task_of(cfs_rq->next);

		if (tsk->oplus_task_info.im_small) {
			/* sched_assist_im_systrace_c(tsk, -1); */
			*se = cfs_rq->next;
			return true;
		}
	}

	return false;
}

void update_task_sched_stat_common(struct task_struct *tsk, u64 delta_ns, int stats_type)
{
	int i = TASK_INFO_SAMPLE -1;
	while (i > 0) {
		tsk->oplus_task_info.sa_info[stats_type][i] = tsk->oplus_task_info.sa_info[stats_type][i-1];
		i--;
	}
	tsk->oplus_task_info.sa_info[stats_type][0] = delta_ns;
}

u64 task_info_sum(struct task_struct *task, int type)
{
	int i = 0;
	u64 sum = 0;

	for (i = 0; i < TASK_INFO_SAMPLE; i++) {
		sum += task->oplus_task_info.sa_info[type][i];
	}

	return sum;
}

void try_to_mark_task_type(struct task_struct *tsk, int type)
{
	int i = 0;
	int fit_small = 0;
	u64 sum_running = 0;

	switch (type) {
	case TASK_SMALL:
		if ((tsk->oplus_task_info.sa_info[TST_EXEC][TASK_INFO_SAMPLE-1]!= 0) && (tsk->oplus_task_info.sa_info[TST_SLEEP][TASK_INFO_SAMPLE-1]!= 0)) {
			sum_running = task_info_sum(tsk, TST_EXEC);
			while (i < TASK_INFO_SAMPLE) {
				if ((tsk->oplus_task_info.sa_info[TST_EXEC][i] < RUNNING_MIN_THREASH) &&
					((tsk->oplus_task_info.sa_info[TST_EXEC][i] * 5) < tsk->oplus_task_info.sa_info[TST_SLEEP][i])) {
					fit_small++;
				}

				if (fit_small >= FIT_SMALL_THREASH) {
					tsk->oplus_task_info.im_small = true;
					goto out;
				}
					i++;
			}
		}
		tsk->oplus_task_info.im_small = false;
		break;
	default:
		break;
	}
out:
	return;
}

void update_sa_task_stats(struct task_struct *tsk, u64 delta_ns, int stats_type)
{
	if (unlikely(!tsk) || unlikely(!sysctl_sched_assist_enabled))
		return;

	if (tsk->oplus_task_info.im_small && (!trace_state_group(tsk) || !sched_assist_scene(SA_CAMERA))) {
		memset(&tsk->oplus_task_info, 0, sizeof(struct task_info));
		return;
	}

	if (!sched_assist_scene(SA_CAMERA))
		return;

	update_task_sched_stat_common(tsk, delta_ns, stats_type);

	try_to_mark_task_type(tsk, TASK_SMALL);
}

void sched_assist_update_record(struct task_struct *p, u64 delta_ns, int stats_type)
{
	update_sa_task_stats(p, delta_ns, stats_type);
	/* sched_assist_im_systrace_c(p, stats_type + 1); */
}
#endif /* CONFIG_OPLUS_FEATURE_AUDIO_OPT */

/*
 * add for create proc node: proc/pid/task/pid/ux_state
*/
bool is_special_entry(struct dentry *dentry, const char *special_proc)
{
	const unsigned char *name;
	if (NULL == dentry || NULL == special_proc)
		return false;

	name = dentry->d_name.name;
	if (NULL != name && !strncmp(special_proc, name, 32))
		return true;
	else
		return false;
}

static unsigned long __read_mostly mark_addr;

static int _sched_assist_update_tracemark(void)
{
	if (mark_addr)
		return 1;

	mark_addr = kallsyms_lookup_name("tracing_mark_write");

	if (unlikely(!mark_addr))
		return 0;

	return 1;
}

void sched_assist_systrace_pid(pid_t pid, int val, const char *fmt, ...)
{
	char log[256];
	va_list args;
	int len;

	if (likely(!param_ux_debug))
		return;

	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	memset(log, ' ', sizeof(log));
	va_start(args, fmt);
	len = vsnprintf(log, sizeof(log), fmt, args);
	va_end(args);

	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		log[255] = '\0';

	preempt_disable();
	event_trace_printk(mark_addr, "C|%d|%s|%d\n", pid, log, val);
	preempt_enable();
}

#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
void sched_assist_im_systrace_c(struct task_struct *tsk, int tst_type)
{
	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	if (likely(!param_ux_debug))
		return;

	if (!trace_state_group(tsk))
		return;

	preempt_disable();
	if (tst_type != -1) {
		event_trace_printk(mark_addr, "C|10001|short_run_target_%d|%d\n", tsk->pid, tsk->oplus_task_info.im_small ? 1 : 0);
	} else if (tst_type == -1) {
		event_trace_printk(mark_addr, "C|10001|short_run_target_buddy_%d|%d\n", tsk->pid, 1);
		event_trace_printk(mark_addr, "C|10001|short_run_target_buddy_%d|%d\n", tsk->pid, 0);
	}
	preempt_enable();
}
#endif /* CONFIG_OPLUS_FEATURE_AUDIO_OPT */

static int proc_ux_state_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p;
	p = get_proc_task(inode);
	if (!p) {
		return -ESRCH;
	}
	task_lock(p);
	seq_printf(m, "%d\n", p->ux_state);
	task_unlock(p);
	put_task_struct(p);
	return 0;
}

static int proc_ux_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_ux_state_show, inode);
}

static ssize_t proc_ux_state_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int err, ux_state;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	err = kstrtoint(strstrip(buffer), 0, &ux_state);
	if(err) {
		return err;
	}
	task = get_proc_task(file_inode(file));
	if (!task) {
		return -ESRCH;
	}

	if (ux_state < 0) {
		put_task_struct(task);
		return -EINVAL;
	}

	if (ux_state == SA_OPT_CLEAR) { /* clear all ux type but animator */
		task->ux_state &= ~(SA_TYPE_LISTPICK | SA_TYPE_HEAVY | SA_TYPE_LIGHT);
	} else if (ux_state & SA_OPT_SET) { /* set target ux type and clear set opt */
		task->ux_state |= ux_state & (~SA_OPT_SET);
	} else if (task->ux_state & ux_state) { /* reset target ux type */
		task->ux_state &= ~ux_state;
	}
	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	put_task_struct(task);
	return count;
}

static ssize_t proc_ux_state_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[256];
	struct task_struct *task = NULL;
	int ux_state = -1;
	size_t len = 0;
	int grp_id = 0;

	task = get_proc_task(file_inode(file));
	if (!task) {
		return -ESRCH;
	}
	ux_state = task->ux_state;
#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	grp_id = task_cgroup_id(task);
#endif
	put_task_struct(task);

#ifdef CONFIG_OPLUS_UX_IM_FLAG
	len = snprintf(buffer, sizeof(buffer),
		"pid=%d ux_state=%d inherit=%llx(fu:%d mu:%d rw:%d bi:%d) grp_id=%d ux_im_flag=%d\n",
		task->pid, ux_state, task->inherit_ux,
		test_inherit_ux(task, INHERIT_UX_FUTEX), test_inherit_ux(task, INHERIT_UX_MUTEX),
		test_inherit_ux(task, INHERIT_UX_RWSEM), test_inherit_ux(task, INHERIT_UX_BINDER),
		grp_id, task->ux_im_flag);
#else
	len = snprintf(buffer, sizeof(buffer),
		"pid=%d ux_state=%d inherit=%llx(fu:%d mu:%d rw:%d bi:%d) grp_id=%d\n",
		task->pid, ux_state, task->inherit_ux,
		test_inherit_ux(task, INHERIT_UX_FUTEX), test_inherit_ux(task, INHERIT_UX_MUTEX),
		test_inherit_ux(task, INHERIT_UX_RWSEM), test_inherit_ux(task, INHERIT_UX_BINDER),
		grp_id);
#endif

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations proc_ux_state_operations = {
	.open		= proc_ux_state_open,
	.write		= proc_ux_state_write,
	.read		= proc_ux_state_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * add for proc node: proc/sys/kernel/sched_assist_scene
*/
int sysctl_sched_assist_scene_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int result, save_sa;
	static DEFINE_MUTEX(sa_scene_mutex);

	mutex_lock(&sa_scene_mutex);

	save_sa = sysctl_sched_assist_scene;
	result = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!write)
		goto out;

	if (sysctl_sched_assist_scene == SA_SCENE_OPT_CLEAR) {
		goto out;
	}

	if (sysctl_sched_assist_scene & SA_SCENE_OPT_SET) {
		save_sa |= sysctl_sched_assist_scene & (~SA_SCENE_OPT_SET);
	} else if (save_sa & sysctl_sched_assist_scene) {
		save_sa &= ~sysctl_sched_assist_scene;
	}

	sysctl_sched_assist_scene = save_sa;
	sched_assist_systrace(sysctl_sched_assist_scene, "scene");
out:
	mutex_unlock(&sa_scene_mutex);

	return result;
}
#ifdef CONFIG_MMAP_LOCK_OPT
void uxchain_rwsem_wake(struct task_struct *tsk, struct rw_semaphore *sem)
{
	int set_ux_once;

	if (current->mm) {
		set_ux_once = (sem == &(current->mm->mmap_sem));
		if (set_ux_once && sysctl_uxchain_v2)
			tsk->ux_once = 1;
	}
}
void uxchain_rwsem_down(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) && sysctl_uxchain_v2) {
		current->get_mmlock = 1;
		current->get_mmlock_ts = sched_clock();
	}
}

void uxchain_rwsem_up(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) &&
		current->get_mmlock == 1 && sysctl_uxchain_v2) {
		current->get_mmlock = 0;
	}
}
#endif

#define TOP_APP_GROUP_ID	4
bool im_mali(struct task_struct *p)
{
	return strstr(p->comm, "mali-cmar-backe");
}
bool cgroup_check_set_sched_assist_boost(struct task_struct *p)
{
	return im_mali(p);
}
int get_st_group_id(struct task_struct *task)
{
#if IS_ENABLED(CONFIG_SCHED_TUNE)
	const int subsys_id = schedtune_cgrp_id;
	struct cgroup *grp;

	rcu_read_lock();
	grp = task_cgroup(task, subsys_id);
	rcu_read_unlock();
	return grp->id;
#else
	return 0;
#endif
}
void cgroup_set_sched_assist_boost_task(struct task_struct *p)
{
	if(cgroup_check_set_sched_assist_boost(p)) {
		if (get_st_group_id(p) == TOP_APP_GROUP_ID) {
			p->ux_state |= SA_TYPE_HEAVY;
		} else
			p->ux_state &= ~SA_TYPE_HEAVY;
	} else {
		return;
	}
}

static ssize_t proc_sched_impt_task_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char temp_buf[32];
	char *temp_str, *token;
	char in_str[2][16];
	int cnt, err, pid;

	static DEFINE_MUTEX(impt_thd_mutex);

	mutex_lock(&impt_thd_mutex);

	memset(temp_buf, 0, sizeof(temp_buf));

	if (count > sizeof(temp_buf) - 1) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	if (copy_from_user(temp_buf, buf, count)) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	cnt = 0;
	temp_str = strstrip(temp_buf);
	while ((token = strsep(&temp_str, " ")) && *token && (cnt < 2)) {
		strncpy(in_str[cnt], token, sizeof(in_str[cnt]));
		cnt += 1;
	}

	if (cnt != 2) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	err = kstrtoint(strstrip(in_str[1]), 0, &pid);
	if (err) {
		mutex_unlock(&impt_thd_mutex);
		return err;
	}

	if (pid < 0 || pid > PID_MAX_DEFAULT) {
		mutex_unlock(&impt_thd_mutex);
		return -EINVAL;
	}

	/* set top app */
	if (!strncmp(in_str[0], "fg", 2)) {
		save_top_app_tgid = pid;
		top_app_type = 0;
		if (!strncmp(in_str[0], "fgLauncher", 10))
			top_app_type = 1; /* 1 is launcher */
		goto out;
	}

	/* set audio app */
	if (!strncmp(in_str[0], "au", 2)) {
		save_audio_tgid = pid;
		goto out;
	}

out:
	mutex_unlock(&impt_thd_mutex);

	return count;
}

static ssize_t proc_sched_impt_task_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[32];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "top(%d %u) au(%d)\n", save_top_app_tgid, top_app_type, save_audio_tgid);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct file_operations proc_sched_impt_task_fops = {
	.write		= proc_sched_impt_task_write,
	.read		= proc_sched_impt_task_read,
};


#ifdef CONFIG_OPLUS_UX_IM_FLAG
enum {
	OPT_STR_TYPE = 0,
	OPT_STR_PID,
	OPT_STR_VAL,
	OPT_STR_MAX = 3,
};
#define MAX_SET 128
static pid_t global_im_flag_pid = -1;

static int im_flag_set_handle(struct task_struct *task, int im_flag)
{
	task->ux_im_flag = im_flag;

	switch (task->ux_im_flag) {
	case IM_FLAG_LAUNCHER_NON_UX_RENDER:
		task->ux_state |= SA_TYPE_HEAVY;
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t proc_im_flag_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char opt_str[OPT_STR_MAX][16];
	int cnt = 0;
	int pid = 0;
	int im_flag = 0;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < OPT_STR_MAX)) {
		strlcpy(opt_str[cnt], token, sizeof(opt_str[cnt]));
		cnt += 1;
	}

	if ((cnt != OPT_STR_MAX) && (cnt != OPT_STR_MAX-1))
		return -EFAULT;

	if (cnt != OPT_STR_MAX) {
		if (cnt == (OPT_STR_MAX - 1) && !strncmp(opt_str[OPT_STR_TYPE], "r", 1)) {
			err = kstrtoint(strstrip(opt_str[OPT_STR_PID]), 10, &pid);
			if (err)
				return err;

			if (pid > 0 && pid <= PID_MAX_DEFAULT)
				global_im_flag_pid = pid;
		}
		return count;
	}

	err = kstrtoint(strstrip(opt_str[OPT_STR_PID]), 10, &pid);
	if (err)
		return err;

	err = kstrtoint(strstrip(opt_str[OPT_STR_VAL]), 10, &im_flag);
	if (err)
		return err;

	if (!strncmp(opt_str[OPT_STR_TYPE], "p", 1)) {
		struct task_struct *task = NULL;

		if (pid > 0 && pid <= PID_MAX_DEFAULT) {
			rcu_read_lock();
			task = find_task_by_vpid(pid);
			if (task) {
				get_task_struct(task);
				im_flag_set_handle(task, im_flag);
				put_task_struct(task);
			} else {
				ux_debug("Can not find task with pid=%d", pid);
			}
			rcu_read_unlock();
		}
	}

	return count;
}

static ssize_t proc_im_flag_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[256];
	size_t len = 0;
	struct task_struct *task = NULL;

	rcu_read_lock();
	task = find_task_by_vpid(global_im_flag_pid);
	if (task) {
		get_task_struct(task);
		len = snprintf(buffer, sizeof(buffer), "comm=%s pid=%d tgid=%d ux_im_flag=%d\n",
			task->comm, task->pid, task->tgid, task->ux_im_flag);
		put_task_struct(task);
	} else
		len = snprintf(buffer, sizeof(buffer), "Can not find task\n");
	rcu_read_unlock();

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct file_operations proc_im_flag_fops = {
	.write		= proc_im_flag_write,
	.read		= proc_im_flag_read,
};
#endif /* CONFIG_OPLUS_UX_IM_FLAG */

#define OPLUS_SCHEDULER_PROC_DIR	"oplus_scheduler"
#define OPLUS_SCHEDASSIST_PROC_DIR	"sched_assist"
struct proc_dir_entry *d_oplus_scheduler = NULL;
struct proc_dir_entry *d_sched_assist = NULL;
static int __init oplus_sched_assist_init(void)
{
	struct proc_dir_entry *proc_node;

	d_oplus_scheduler = proc_mkdir(OPLUS_SCHEDULER_PROC_DIR, NULL);
	if(!d_oplus_scheduler) {
		ux_err("failed to create proc dir oplus_scheduler\n");
		goto err_dir_scheduler;
	}

	d_sched_assist = proc_mkdir(OPLUS_SCHEDASSIST_PROC_DIR, d_oplus_scheduler);
	if(!d_sched_assist) {
		ux_err("failed to create proc dir sched_assist\n");
		goto err_dir_sa;
	}

	proc_node = proc_create("sched_impt_task", 0666, d_sched_assist, &proc_sched_impt_task_fops);
	if(!proc_node) {
		ux_err("failed to create proc node sched_impt_task\n");
#ifdef CONFIG_OPLUS_UX_IM_FLAG
		goto err_node_assist;
#else
		goto err_node_impt;
#endif
	}

#ifdef CONFIG_OPLUS_UX_IM_FLAG
	proc_node = proc_create("im_flag", 0666, d_sched_assist, &proc_im_flag_fops);
	if (!proc_node) {
		ux_err("failed to create proc node im_flag\n");
		goto err_node_assist;
	}
#endif

	return 0;

#ifdef CONFIG_OPLUS_UX_IM_FLAG
err_node_assist:
#else
err_node_impt:
#endif
	remove_proc_entry(OPLUS_SCHEDASSIST_PROC_DIR, NULL);

err_dir_sa:
	remove_proc_entry(OPLUS_SCHEDULER_PROC_DIR, NULL);

err_dir_scheduler:
	return -ENOENT;
}

device_initcall(oplus_sched_assist_init);
