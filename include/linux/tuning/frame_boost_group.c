#include <linux/sched.h>
#include <linux/sched/cpufreq.h>
#include <../kernel/sched/sched.h>
#include <../kernel/sched/walt.h>
#include <trace/events/sched.h>
#include <linux/reciprocal_div.h>

#include "frame_boost_group.h"
#include "frame_info.h"

#ifdef OPLUS_FEATURE_SCHED_ASSIST
#include <linux/sched_assist/sched_assist_common.h>
#endif /* OPLUS_FEATURE_SCHED_ASSIST */

struct task_struct *ui;
struct task_struct *render;
struct task_struct *hwtask1;
struct task_struct *hwtask2;
struct frame_boost_group default_frame_boost_group;
atomic_t start_frame = ATOMIC_INIT(1);
int stune_boost;
DEFINE_RAW_SPINLOCK(fbg_lock);
extern bool isHighFps;
extern bool use_vload;
extern capacity_spare_without(int cpu, struct task_struct *p);
extern struct sched_cluster *sched_cluster[];
extern int num_sched_clusters;
extern int sysctl_slide_boost_enabled;
extern int sysctl_input_boost_enabled;
extern bool up_migrate;
extern u64 last_migrate_time;
#define DEFAULT_ROLL_OVER_INTERVAL 1000000 /* ns */
#define DEFAULT_FREQ_UPDATE_INTERVAL 2000000  /* ns */
#define DEFAULT_UTIL_INVALID_INTERVAL 48000000 /* ns */
#define DEFAULT_UTIL_UPDATE_TIMEOUT 20000000  /* ns */
#define DEFAULT_GROUP_RATE 60 /* 60FPS */
#define ACTIVE_TIME  5000000000/* ns */

#define DEFAULT_TRANS_DEPTH (2)
#define DEFAULT_MAX_THREADS (6)
#define STATIC_FBG_DEPTH (-1)
#define ERROR_CODE (-1)

struct frame_boost_group *frame_grp(void)
{
	return &default_frame_boost_group;
}

/*
* frame boost group load tracking
*/

void sched_set_group_window_size(unsigned int window)
{
	struct frame_boost_group *grp = &default_frame_boost_group;
	unsigned long flag;

	if (!grp) {
		pr_err("set window size for group %d fail\n");
		return;
	}

	raw_spin_lock_irqsave(&grp->lock, flag);

	grp->window_size = window;

	raw_spin_unlock_irqrestore(&grp->lock, flag);
}

struct frame_boost_group *task_frame_boost_group(struct task_struct *p)
{
	return rcu_dereference(p->fbg);
}

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)

static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	u64 task_exec_scale;
	int cpu = cpu_of(rq);
	unsigned long curr_cap = arch_scale_freq_capacity(cpu);
	unsigned int curr_freq = (curr_cap * (u64) rq->cluster->max_possible_freq) >>
		SCHED_CAPACITY_SHIFT;

	task_exec_scale = DIV64_U64_ROUNDUP(curr_freq *
				arch_scale_cpu_capacity(NULL, cpu),
				rq->cluster->max_possible_freq);
	trace_task_exec_scale(cpu, task_exec_scale, curr_freq, arch_scale_cpu_capacity(NULL, cpu), rq->cluster->max_possible_freq);
	return (delta * task_exec_scale) >> 10;
}

static int account_busy_for_group_demand(struct task_struct *p, int event, struct rq *rq, struct frame_boost_group *grp)
{
	/* No need to bother updating task demand for the idle task. */
	if (is_idle_task(p))
		return 0;

	if (event == TASK_WAKE && (grp->nr_running == 0))
		return 0;

	/*
	 * The idle exit time is not accounted for the first task _picked_ up to
	 * run on the idle CPU.
	 */
	if (event == PICK_NEXT_TASK && rq->curr == rq->idle)
		return 0;

	/*
	 * TASK_UPDATE can be called on sleeping task, when its moved between
	 * related groups
	 */
	if (event == TASK_UPDATE) {
		if (rq->curr == p)
			return 1;
		return p->on_rq;
	}

	return 1;
}

void update_group_nr_running(struct task_struct *p, int event, u64 wallclock)
{
	struct frame_boost_group *grp = NULL;

	if (event == IRQ_UPDATE)
		return;

	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (!grp) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock(&grp->lock);

	if (event == PICK_NEXT_TASK)
		grp->nr_running++;
	else if (event == PUT_PREV_TASK)
		grp->nr_running--;

	raw_spin_unlock(&grp->lock);
	rcu_read_unlock();
}

static void add_to_group_task_time(struct frame_boost_group *grp, struct rq *rq, struct task_struct *p, u64 wallclock)
{
	u64 delta_exec, delta_scale;
	u64 mark_start = p->ravg.mark_start;
	u64 window_start = grp->window_start;

	if (unlikely(wallclock <= mark_start))
		return;

	/* per task load tracking in FBG */
	if (likely(mark_start >= window_start)) {
		/*
		*    ws   ms   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/
		delta_exec = wallclock - mark_start;
		p->ravg.curr_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		p->ravg.curr_window_scale += delta_scale;
	} else {
		/*
		*    ms   ws   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/

		/* prev window task statistic */
		delta_exec = window_start - mark_start;
		p->ravg.prev_window_exec += delta_exec;
		delta_scale = scale_exec_time(delta_exec, rq);
		p->ravg.prev_window_scale += delta_scale;

		/* curr window task statistic */
		delta_exec = wallclock - window_start;
		p->ravg.curr_window_exec += delta_exec;
		delta_scale = scale_exec_time(delta_exec, rq);
		p->ravg.curr_window_scale += delta_scale;
	}
}
static void add_to_group_time(struct frame_boost_group *grp, struct rq *rq, u64 wallclock)
{
	u64 delta_exec, delta_scale;
	u64 mark_start = grp->mark_start;
	u64 window_start = grp->window_start;

	if (unlikely(wallclock <= mark_start))
		return;

	/* per group load tracking in FBG */
	if (likely(mark_start >= window_start)) {
		/*
		*    ws   ms   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/
		delta_exec = wallclock - mark_start;
		grp->time.curr_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.curr_window_scale += delta_scale;
	} else {
		/*
		*    ms   ws   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/

		/* prev window group statistic */
		delta_exec = window_start - mark_start;
		grp->time.prev_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.prev_window_scale += delta_scale;

		/* curr window group statistic */
		delta_exec = wallclock - window_start;
		grp->time.curr_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.curr_window_scale += delta_scale;
	}
	trace_add_group_delta(wallclock, mark_start, window_start, delta_exec, delta_scale, rq->task_exec_scale);
}

static inline void add_to_group_demand(struct frame_boost_group *grp, struct rq *rq, struct task_struct *p, u64 wallclock)
{
	if (unlikely(wallclock <= grp->window_start))
		return;

	add_to_group_task_time(grp, rq, p, wallclock);
	add_to_group_time(grp, rq, wallclock);
}

void update_group_demand(struct task_struct *p, struct rq *rq, int event, u64 wallclock)
{
	struct frame_boost_group *grp;

	if (event == IRQ_UPDATE)
		return;

	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (!grp) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock(&grp->lock);

	/* we must update mark_start whether group demand is busy or not */
	if (!account_busy_for_group_demand(p, event, rq, grp)) {
		goto done;
	}


	if (grp->nr_running == 1)
		grp->mark_start = max(grp->mark_start, p->ravg.mark_start);

	add_to_group_demand(grp, rq, p, wallclock);

done:
	grp->mark_start = wallclock;
	raw_spin_unlock(&grp->lock);
	rcu_read_unlock();
	trace_add_group_demand(p, event, grp->time.curr_window_scale, rq->task_exec_scale, grp->nr_running);
}

void group_time_rollover(struct group_time *time)
{
	time->prev_window_scale = time->curr_window_scale;
	time->curr_window_scale = 0;
	time->prev_window_exec = time->curr_window_exec;
	time->curr_window_exec = 0;
}

int sched_set_group_window_rollover(void)
{
	u64 wallclock;
	unsigned long flag;
	struct task_struct *p = NULL;
	u64 window_size = 0;
	struct frame_boost_group *grp = &default_frame_boost_group;

	raw_spin_lock_irqsave(&grp->lock, flag);

	wallclock = sched_ktime_clock();
	window_size = wallclock - grp->window_start;
	if (window_size < DEFAULT_ROLL_OVER_INTERVAL) {
		raw_spin_unlock_irqrestore(&grp->lock, flag);
		return 0;
	}

	grp->prev_window_size = window_size;
	grp->window_start = wallclock;

	list_for_each_entry(p, &grp->tasks, fbg_list) {
		p->ravg.prev_window_scale = p->ravg.curr_window_scale;
		p->ravg.curr_window_scale = 0;
		p->ravg.prev_window_exec = p->ravg.curr_window_exec;
		p->ravg.curr_window_exec = 0;
	}

	group_time_rollover(&grp->time);

	raw_spin_unlock_irqrestore(&grp->lock, flag);
	if (use_vload) {
		use_vload = false;
	}
	if (up_migrate) {
		up_migrate = false;
	}

	return 0;
}

static inline int per_task_boost(struct task_struct *p)
{
	if (p->boost_period) {
		if (sched_clock() > p->boost_expires) {
			p->boost_period = 0;
			p->boost_expires = 0;
			p->boost = 0;
		}
	}
	return p->boost;
}

static inline unsigned long boosted_task_util(struct task_struct *task)
{
	unsigned long util = task_util_est(task);
	long margin = schedtune_grp_margin(util);
	trace_sched_boost_task(task, util, margin);
	return util + margin;
}

static inline bool task_demand_fits(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long boosted_util = boosted_task_util(p);
	unsigned int margin = 1024;

	return (capacity << SCHED_CAPACITY_SHIFT) >=
			(boosted_util * margin);
}

static inline bool task_fits_max(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = cpu_rq(cpu)->rd->max_cpu_capacity.val;
	unsigned long task_boost = per_task_boost(p);
	cpumask_t allowed_cpus;
	int allowed_cpu;

	if (capacity == max_capacity)
		return true;

	if (task_boost > TASK_BOOST_ON_MID)
		return false;

	if (task_demand_fits(p, cpu)) {
		return true;
	}

	/* Now task does not fit. Check if there's a better one. */
	cpumask_and(&allowed_cpus, &p->cpus_allowed, cpu_online_mask);
	for_each_cpu(allowed_cpu, &allowed_cpus) {
	if (capacity_orig_of(allowed_cpu) > capacity)
		return false; /* Misfit */
	}

	/* Already largest capacity in allowed cpus. */
	return true;
}

struct cpumask *find_rtg_target(struct task_struct *p)
{
	struct frame_boost_group *grp = NULL;
	struct sched_cluster *preferred_cluster = NULL;
	struct cpumask *rtg_target = NULL;
	struct sched_cluster *prime_cluster = sched_cluster[num_sched_clusters - 1];
	int prime_cpu = -1;

	rcu_read_lock();
	grp = task_frame_boost_group(p);
	rcu_read_unlock();

	if (!grp)
		return NULL;

	preferred_cluster = grp->preferred_cluster;
	if (!preferred_cluster)
		return NULL;

	rtg_target = &preferred_cluster->cpus;
	if (!task_fits_max(p, cpumask_first(rtg_target))) {
		rtg_target = NULL;
	}
	if (!rtg_target && prime_cluster && (preferred_cluster->id == 1)) {
		prime_cpu = cpumask_first(&prime_cluster->cpus);
		if (task_fits_max(p, prime_cpu))
			rtg_target = &prime_cluster->cpus;
		else
			rtg_target = &preferred_cluster->cpus;
	}

	return rtg_target;
}

bool is_prime_fits(int cpu)
{
	struct task_struct *curr = NULL;
	struct rq *rq = NULL;

	if (cpu < 0 || !cpu_active(cpu) || cpu_isolated(cpu))
		return false;

	rq = cpu_rq(cpu);
	curr = rq->curr;
	if (!curr)
		return false;

	return (curr->fbg_depth == 0);
}

bool need_frame_boost(struct task_struct *p)
{
	bool is_launch = false;
	if (!p)
		return false;
#ifdef OPLUS_FEATURE_SCHED_ASSIST
	is_launch = sched_assist_scene(SA_LAUNCH);
#endif /* OPLUS_FEATURE_SCHED_ASSIST */

	return (p->fbg_depth != 0) &&
	(!is_launch || p->ravg.active_time > ACTIVE_TIME);
}

int find_fbg_cpu(struct task_struct *p)
{
	int max_spare_cap_cpu = -1;
	int active_cpu = -1;
	struct frame_boost_group *grp = NULL;
	struct sched_cluster *preferred_cluster = NULL;
	struct task_struct *curr = NULL;
	struct rq *rq = NULL;

	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (grp) {
		int i;
		unsigned long spare_cap = 0, max_spare_cap = 0;
		struct cpumask *preferred_cpus = NULL;
		cpumask_t search_cpus = CPU_MASK_NONE;
		preferred_cluster = grp->preferred_cluster;

		/* full boost, if prime cluster has too many frame task,
		we will step in the function, so select gold cluster */
		if (is_full_throttle_boost())
			preferred_cluster = sched_cluster[num_sched_clusters - 2];

		if (!preferred_cluster) {
			rcu_read_unlock();
			return ERROR_CODE;
		}

		preferred_cpus = &preferred_cluster->cpus;

		cpumask_and(&search_cpus, &p->cpus_allowed, cpu_online_mask);
		cpumask_andnot(&search_cpus, &search_cpus, cpu_isolated_mask);

		for_each_cpu_and(i, &search_cpus, preferred_cpus) {
			rq = cpu_rq(i);
			curr = rq->curr;
			if (curr) {
				if (curr->fbg_depth != 0)
					continue;
			}

			if (active_cpu == -1)
				active_cpu = i;

			if (is_reserved(i))
				continue;

			if (idle_cpu(i) || (i == task_cpu(p) && p->state == TASK_RUNNING)) {
				rcu_read_unlock();
				return i;
			}

			spare_cap = capacity_spare_without(i, p);
			if (spare_cap > max_spare_cap) {
				max_spare_cap = spare_cap;
				max_spare_cap_cpu = i;
			}
		}
	}
	rcu_read_unlock();
	if (max_spare_cap_cpu == -1)
		max_spare_cap_cpu = active_cpu;

	return max_spare_cap_cpu;
}

extern struct reciprocal_value reciprocal_value(u32 d);
struct reciprocal_value schedtune_spc_rdiv_v1;
static long schedtune_margin(unsigned long signal, long boost)
{
	long long margin = 0;

	/*
	 * Signal proportional compensation (SPC)
	 *
	 * The Boost (B) value is used to compute a Margin (M) which is
	 * proportional to the complement of the original Signal (S):
	 *   M = B * (SCHED_CAPACITY_SCALE - S)
	 * The obtained M could be used by the caller to "boost" S.
	 */
	if (boost >= 0) {
		margin  = SCHED_CAPACITY_SCALE - signal;
		margin *= boost;
	} else
		margin = -signal * boost;

	margin  = reciprocal_divide(margin, schedtune_spc_rdiv_v1);

	if (boost < 0)
		margin *= -1;
	return margin;
}

int schedtune_grp_margin(unsigned long util)
{
	if (stune_boost == 0)
		return 0;

	if ((!isHighFps) && !(sysctl_slide_boost_enabled || sysctl_input_boost_enabled))
		return 0;

	return schedtune_margin(util, stune_boost);
}

static struct sched_cluster *best_cluster(struct frame_boost_group *grp)
{
	int cpu;
	unsigned long max_cap = 0, cap = 0;
	struct sched_cluster *cluster = NULL, *max_cluster = NULL;
	unsigned long util = grp->time.normalized_util;
	unsigned long boosted_grp_util = util + schedtune_grp_margin(util);

	/* if sched_boost == 1, select prime cluster*/
	if (is_full_throttle_boost()) {
		return sched_cluster[num_sched_clusters - 1];
	}

	for_each_sched_cluster(cluster) {
		cpu = cpumask_first(&cluster->cpus);
		cap = capacity_orig_of(cpu);
		if (cap > max_cap) {
			max_cap = cap;
			max_cluster = cluster;
		}
		if (boosted_grp_util <= cap)
			return cluster;
	}

	return max_cluster;
}

static inline bool
group_should_invalid_util(struct frame_boost_group *grp, u64 now)
{
	return (now - grp->last_util_update_time >= grp->util_invalid_interval);
}

bool valid_normalized_util(struct frame_boost_group *grp)
{
	struct task_struct *p = NULL;
	cpumask_t fbg_cpus = CPU_MASK_NONE;
	int cpu;
	struct rq *rq;

	if (grp->nr_running) {
		list_for_each_entry(p, &grp->tasks, fbg_list) {
			cpu = task_cpu(p);
			rq = cpu_rq(cpu);
			if (task_running(rq, p))
				cpumask_set_cpu(task_cpu(p), &fbg_cpus);
		}
	}

	/* just up migrate, this is no need to update cpufreq*/
	if (use_vload && up_migrate) {
		return false;
	}
	return cpumask_intersects(&fbg_cpus, &grp->preferred_cluster->cpus);
}

unsigned long sched_get_group_util(const struct cpumask *query_cpus)
{
	unsigned long flag;
	unsigned long grp_util = 0;
	struct frame_boost_group *grp = &default_frame_boost_group;

	u64 now = sched_ktime_clock();
	raw_spin_lock_irqsave(&grp->lock, flag);

	if (!list_empty(&grp->tasks) && grp->preferred_cluster &&
		cpumask_intersects(query_cpus, &grp->preferred_cluster->cpus) &&
		(!group_should_invalid_util(grp, now)) &&
		valid_normalized_util(grp)) {
		grp_util = grp->time.normalized_util;
	}

	raw_spin_unlock_irqrestore(&grp->lock, flag);

	return grp_util;
}

static bool group_should_update_freq(struct frame_boost_group *grp,
				     int cpu, unsigned int flag, u64 now)
{
	if (flag & FRAME_FORCE_UPDATE) {
		return true;
	} else if (flag & FRAME_NORMAL_UPDATE) {
		if (now - grp->last_freq_update_time >= grp->freq_update_interval) {
			return true;
		}
	}

	return false;
}

void sched_set_group_normalized_util(unsigned long util, unsigned int flag)
{
	u64 now;
	int prev_cpu, next_cpu;
	unsigned long flags;
	bool need_update_prev_freq = false;
	bool need_update_next_freq = false;
	struct sched_cluster *preferred_cluster = NULL;
	struct sched_cluster *prev_cluster = NULL;
	struct frame_boost_group *grp = &default_frame_boost_group;

	raw_spin_lock_irqsave(&grp->lock, flags);

	if (list_empty(&grp->tasks)) {
		raw_spin_unlock_irqrestore(&grp->lock, flags);
		return;
	}

	grp->time.normalized_util = util;
	preferred_cluster = best_cluster(grp);

	if (!grp->preferred_cluster)
		grp->preferred_cluster = preferred_cluster;
	else if (grp->preferred_cluster != preferred_cluster) {
		prev_cluster = grp->preferred_cluster;
		prev_cpu = cpumask_first(&prev_cluster->cpus);
		need_update_prev_freq = true;

		grp->preferred_cluster = preferred_cluster;
	}

	if (grp->preferred_cluster)
		next_cpu = cpumask_first(&grp->preferred_cluster->cpus);
	else
		next_cpu = 0;

	now = sched_ktime_clock();

	grp->last_util_update_time = now;
	if (up_migrate && (now - last_migrate_time >= DEFAULT_UTIL_UPDATE_TIMEOUT))
		up_migrate = false;

	need_update_next_freq = group_should_update_freq(grp, next_cpu, flag, now);
	if (need_update_next_freq) {
		grp->last_freq_update_time = now;
	}

	raw_spin_unlock_irqrestore(&grp->lock, flags);

	if (need_update_prev_freq) {
		cpufreq_update_util(cpu_rq(prev_cpu), (SCHED_CPUFREQ_WALT | SCHED_INPUT_BOOST));
	}

	if (need_update_next_freq) {
		cpufreq_update_util(cpu_rq(next_cpu), (SCHED_CPUFREQ_WALT | SCHED_INPUT_BOOST));
	}
}

static void remove_from_frame_boost_group(struct task_struct *p)
{
	struct frame_boost_group *grp = p->fbg;
	struct rq *rq;
	struct rq_flags flag;
	unsigned long irqflag;

	rq = __task_rq_lock(p, &flag);
	raw_spin_lock_irqsave(&grp->lock, irqflag);

	list_del_init(&p->fbg_list);
	rcu_assign_pointer(p->fbg, NULL);

	if (p->on_cpu)
		grp->nr_running--;

	if (grp->nr_running < 0) {
		WARN_ON(1);
		grp->nr_running = 0;
	}

	if (list_empty(&grp->tasks)) {
		grp->preferred_cluster = NULL;
		grp->time.normalized_util = 0;
	}

	raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	__task_rq_unlock(rq, &flag);
}

static void add_to_frame_boost_group(struct task_struct *p, struct frame_boost_group *grp)
{
	struct rq *rq;
	struct rq_flags flag;
	unsigned long irqflag;

	rq = __task_rq_lock(p, &flag);
	raw_spin_lock_irqsave(&grp->lock, irqflag);

	list_add(&p->fbg_list, &grp->tasks);
	rcu_assign_pointer(p->fbg, grp);

	if (p->on_cpu) {
		grp->nr_running++;
		if (grp->nr_running == 1)
			grp->mark_start = max(grp->mark_start, sched_ktime_clock());
	}

	raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	__task_rq_unlock(rq, &flag);
}

int sched_set_frame_boost_group(struct task_struct *p, bool is_add)
{
	int rc = 0;
	unsigned long flags;
	struct frame_boost_group *grp = NULL;

	raw_spin_lock_irqsave(&p->pi_lock, flags);

	if ((!p->fbg && !is_add) || (p->fbg && is_add) || (is_add && (p->flags & PF_EXITING)))
		goto done;

	if (!is_add) {
		remove_from_frame_boost_group(p);
		goto done;
	}

	grp = &default_frame_boost_group;

	add_to_frame_boost_group(p, grp);

done:
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

	return rc;
}

int set_fbg_sched(struct task_struct *task, bool is_add)
{
	int err = -1;

	if (!task)
		return err;

	if (is_add && (task->sched_class != &fair_sched_class &&
			task->sched_class != &rt_sched_class))
		return err;

	if (in_interrupt()) {
		pr_err("[AWARE_RTG]: %s is in interrupt\n", __func__);
		return err;
	}

	err = sched_set_frame_boost_group(task, is_add);

	return err;
}

void set_fbg_thread(struct task_struct *task, bool is_add)
{
	int err = 0;

	if (!task)
		return;

	err = set_fbg_sched(task, is_add);
	if (err < 0) {
		pr_err("[AWARE_RTG]: %s task:%d set_group err:%d\n",
		__func__, task->pid, err);
		return;
	}

	if (is_add) {
		task->fbg_depth = STATIC_FBG_DEPTH;
	} else {
		task->fbg_depth = 0;
	}
}

struct task_struct *do_update_thread(int pid, struct task_struct *old)
{
	struct task_struct *new = NULL;

	if (pid > 0) {

		if (old && (pid == old->pid))
			return old;

		rcu_read_lock();
		new = find_task_by_vpid(pid);
		if (new) {
			get_task_struct(new);
		}
		rcu_read_unlock();
	}

	if (atomic_read(&start_frame) == 1) {
		set_fbg_thread(old, false);
		set_fbg_thread(new, true);
	}

	if (old) {
		put_task_struct(old);
	}

	return new;
}

void update_hwui_tasks(int hwtid1, int hwtid2)
{
	hwtask1 = do_update_thread(hwtid1, hwtask1);
	hwtask2 = do_update_thread(hwtid2, hwtask2);
}

void update_frame_thread(int pid, int tid)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&fbg_lock, flags);

	ui = do_update_thread(pid, ui);
	render = do_update_thread(tid, render);

	raw_spin_unlock_irqrestore(&fbg_lock, flags);
}

static int max_trans_depth = DEFAULT_TRANS_DEPTH;
static int max_trans_num = DEFAULT_MAX_THREADS;
static atomic_t fbg_trans_num = ATOMIC_INIT(0);

bool is_frame_task(struct task_struct *task)
{
	struct frame_boost_group *grp = NULL;

	if (task == NULL) {
		return false;
	}

	rcu_read_lock();
	grp = task_frame_boost_group(task);
	rcu_read_unlock();

	return grp;
}

void add_trans_thread(struct task_struct *target, struct task_struct *from)
{
	int ret;
	if (target == NULL || from == NULL) {
		return;
	}

	if (is_frame_task(target) || !is_frame_task(from)) {
		return;
	}

	if (atomic_read(&fbg_trans_num) >= max_trans_num) {
		return;
	}

	if (from->fbg_depth != STATIC_FBG_DEPTH &&
		from->fbg_depth >= max_trans_depth) {
		return;
	}

	get_task_struct(target);
	ret = set_fbg_sched(target, true);
	if (ret < 0) {
		put_task_struct(target);
		return;
	}

	if (from->fbg_depth == STATIC_FBG_DEPTH)
		target->fbg_depth = 1;
	else
		target->fbg_depth = from->fbg_depth + 1;

	atomic_inc(&fbg_trans_num);
	put_task_struct(target);
}

void remove_trans_thread(struct task_struct *target)
{
	int ret;
	if (target == NULL)
		return;

	if (!is_frame_task(target))
		return;

	get_task_struct(target);
	if (target->fbg_depth == STATIC_FBG_DEPTH) {
		put_task_struct(target);
		return;
	}

	ret = set_fbg_sched(target, false);
	if (ret < 0) {
		put_task_struct(target);
		return;
	}

	target->fbg_depth = 0;
	if (atomic_read(&fbg_trans_num) > 0) {
		atomic_dec(&fbg_trans_num);
	}
	put_task_struct(target);
}

void binder_thread_set_fbg(struct task_struct *thread, struct task_struct *from, bool oneway)
{
	if (!oneway && from && thread) {
		add_trans_thread(thread, from);
	}
}

void binder_thread_remove_fbg(struct task_struct *thread, bool oneway)
{
	if (!oneway && thread) {
		remove_trans_thread(thread);
	}
}

static int __init init_frame_boost_group(void)
{
	struct frame_boost_group *grp = &default_frame_boost_group;

	grp->freq_update_interval = DEFAULT_FREQ_UPDATE_INTERVAL;
	grp->util_invalid_interval = DEFAULT_UTIL_INVALID_INTERVAL;
	grp->util_update_timeout = DEFAULT_UTIL_UPDATE_TIMEOUT;
	grp->window_size = NSEC_PER_SEC / DEFAULT_GROUP_RATE;
	grp->preferred_cluster = NULL;

	INIT_LIST_HEAD(&grp->tasks);
	raw_spin_lock_init(&grp->lock);
	schedtune_spc_rdiv_v1 = reciprocal_value(100);

	return 0;
}
late_initcall(init_frame_boost_group);
