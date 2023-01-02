#include <../kernel/sched/sched.h>
#include <trace/events/sched.h>
#include "frame_info.h"
#include "frame_boost_group.h"

#define DEFAULT_FRAME_QOS		60
#define DEFAULT_VLOAD_MARGIN		0
#define FRAME_DEFAULT_MAX_UTIL		1024
#define FRAME_MAX_VLOAD			1024
#define FRAME_MAX_LOAD			1024
#define FRAME_UTIL_INVALID_FACTOR	4

struct frame_info global_frame_info;
unsigned long prev_real_util;
extern atomic_t start_frame;
bool isHighFps;
bool use_vload;
bool up_migrate;
extern int stune_boost;
unsigned int max_exec_time = 5000000;
unsigned int timeout_load = 1024;
u64 last_migrate_time;

struct frame_info *fbg_frame_info(struct frame_boost_group *grp)
{
	return (struct frame_info *) grp->private_data;
}
static inline struct group_time *frame_info_fbg_load(struct frame_info *frame_info)
{
	return &(frame_info->fbg->time);
}

void set_frame_rate(unsigned int frame_rate)
{
	struct frame_info *frame_info = NULL;
	struct frame_boost_group *fbg = frame_grp();
	if (!fbg)
		return;

	frame_info = fbg_frame_info(fbg);
	if (!frame_info)
		return;

	if (frame_rate > 60) {
		isHighFps = true;
		if (frame_rate == 120) {
			max_exec_time = 5000000;
			timeout_load = 205;
		} else {
			max_exec_time = 7000000;
			timeout_load = 240;
		}
	} else {
		isHighFps = false;
		max_exec_time = 12000000;
		timeout_load = 1024;
	}

	stune_boost = 20;
	frame_info->frame_qos = frame_rate;
	frame_info->frame_interval = NSEC_PER_SEC / frame_rate;
	frame_info->vload_margin = 0;
	frame_info->max_vload_time = (frame_info->frame_interval / NSEC_PER_MSEC) + frame_info->vload_margin;
}
static inline void frame_boost(struct frame_info *frame_info)
{
	if (frame_info->frame_util < frame_info->frame_boost_min_util)
		frame_info->frame_util = frame_info->frame_boost_min_util;
}

int set_frame_min_util(int min_util, bool isBoost)
{
	struct frame_info *frame_info = NULL;
	struct frame_boost_group *fbg = frame_grp();

	if (!fbg)
		return -EIO;

	if (min_util < 0 || min_util > SCHED_CAPACITY_SCALE) {
		pr_err("[%s] [FRAME_INFO] invalid min_util value", __func__);
		return -EINVAL;
	}

	frame_info = fbg_frame_info(fbg);
	if (!frame_info)
		return -EIO;

	if (isBoost) {
		frame_info->frame_boost_min_util = min_util;
		frame_boost(frame_info);
		sched_set_group_normalized_util(frame_info->frame_util, FRAME_FORCE_UPDATE);
	} else {
		frame_info->frame_min_util = min_util;

	}

	return 0;
}

static  unsigned long calc_prev_frame_load_util(struct frame_info *frame_info, bool fake)
{
	u64 prev_frame_scale = frame_info->prev_frame_scale;
	u64 prev_frame_interval = frame_info->frame_interval;
	unsigned long  frame_util = 0;

	if (fake) {
		prev_frame_interval = max_t(unsigned long, frame_info->prev_frame_interval, prev_frame_interval);
	}

	if (prev_frame_interval > 0) {
		frame_util = div64_u64((prev_frame_scale << SCHED_CAPACITY_SHIFT), prev_frame_interval);
	}

	if (frame_util > FRAME_MAX_LOAD)
		frame_util = FRAME_MAX_LOAD;

	return frame_util;
}

/* last frame load tracking */
static inline void update_frame_prev_load(struct frame_info *frame_info, bool fake)
{
	frame_info->prev_frame_exec = frame_info_fbg_load(frame_info)->prev_window_exec;
	frame_info->prev_frame_scale = frame_info_fbg_load(frame_info)->prev_window_scale;
	frame_info->prev_frame_interval = frame_info->fbg->prev_window_size;

	frame_info->prev_frame_load_util = calc_prev_frame_load_util(frame_info, fake);
}

static inline void set_frame_start(struct frame_info *frame_info)
{
	unsigned long util;
	bool fake = false;

	if (likely(frame_info->status == FRAME_START)) {
		/*
		 * START -=> START -=> ......
		 * FRMAE_START is
		 * the end of last frame
		 * the start of the current frame
		 */
		update_frame_prev_load(frame_info, fake);
	} else if (frame_info->status == FRAME_END ||
		   frame_info->status ==  FRAME_INVALID) {
		/* START -=> END -=> [START]
		 * FRAME_START is
		 * only the start of current frame
		 * we shoudn't tracking the last fbg-window
		 * [FRAME_END, FRAME_START]
		 * it's not an available frame window
		 */
		fake = true;
		update_frame_prev_load(frame_info, fake);
		frame_info->status = FRAME_START;
	}

	/* new_frame start */
	frame_info->frame_vload = 0;
	frame_info->frame_boost_min_util = 0;
	/* FRAME_START, we don't use fake load */
	if (fake) {
		util = max_t(unsigned long, prev_real_util, frame_info->frame_min_util);
	} else {
		util = max_t(unsigned long, frame_info->prev_frame_load_util, frame_info->frame_min_util);
	}

	frame_info->frame_util = min_t(unsigned long, frame_info->frame_max_util, util);
}

//static inline void do_frame_end(struct frame_info *frame_info)
//{
//	frame_info->status = FRAME_END;
//
//	/* last frame load tracking */
//	update_frame_prev_load(frame_info, false);
//
//	/* reset frame_info */
//	frame_info->frame_vload = 0;
//	frame_info->frame_util = min_t(unsigned long, frame_info->frame_max_util, frame_info->prev_frame_load_util);
//}

static inline void set_frame_end(struct frame_info *frame_info, bool fake)
{
	frame_info->status = FRAME_END;

	/* last frame load tracking */
	update_frame_prev_load(frame_info, fake);

	if (!fake) {
		prev_real_util = frame_info->prev_frame_load_util;
	}

	/* reset frame_info */
	frame_info->frame_vload = 0;
	frame_info->frame_boost_min_util = 0;

	frame_info->frame_util = min_t(unsigned long,
			frame_info->frame_max_util,
			frame_info->prev_frame_load_util);
}

int set_frame_status(unsigned long status)
{
	int ret = -1;
	struct frame_boost_group *grp = NULL;
	struct frame_info *frame_info = NULL;

	if (atomic_read(&start_frame) == 0)
		return ret;

	grp = frame_grp();
	if (!grp)
		return ret;

	frame_info = fbg_frame_info(grp);

	if (!frame_info)
		return ret;

	switch (status) {
	case FRAME_START:
		/* collect frame_info when frame_end timestamp comming */
		set_frame_start(frame_info);
		break;
	case FRAME_END:
		/* FRAME_END  status should only set and update freq once */
		if (unlikely(frame_info->status == FRAME_END))
			return 0;
		set_frame_end(frame_info, false);
		break;
	}


	frame_boost(frame_info);
	sched_set_group_normalized_util(frame_info->frame_util, FRAME_FORCE_UPDATE);
	trace_frame_status(frame_info->frame_util);
	return 0;
}

int set_frame_timestamp(unsigned long timestamp)
{
	int ret = 0;

	if (atomic_read(&start_frame) == 0)
		return ret;

	ret = sched_set_group_window_rollover();

	if (!ret)
		ret = set_frame_status(timestamp);

	return ret;
}

void set_frame_sched_state(bool enable)
{
	struct frame_boost_group *grp = NULL;
	struct frame_info *frame_info = NULL;

	grp = frame_grp();
	if (!grp)
		return;


	frame_info = fbg_frame_info(grp);

	if (!frame_info)
		return;

	if (enable) {
		if (atomic_read(&start_frame) == 1)
			return;

		atomic_set(&start_frame, 1);
		frame_info->prev_frame_load_util = 0;
		frame_info->frame_vload = 0;
	} else {
		if (atomic_read(&start_frame) == 0)
			return;

		atomic_set(&start_frame, 0);
		//(void)sched_set_group_normalized_util(0);
		frame_info->status = FRAME_END;
	}

}

void update_frame_state(int pid, bool in_frame)
{
	if (in_frame) {
		set_frame_sched_state(true);
		set_frame_timestamp(FRAME_START);
	} else {
		set_frame_timestamp(FRAME_END);
	}
}

/*
 * frame_vload [0~1024]
 * vtime = now - timestamp
 * max_time = frame_info->qos_frame_interval+ vload_margin
 * load = F(vtime)
 *	= vtime ^ 2 - vtime * max_time + FRAME_MAX_VLOAD * vtime / max_time;
 *	= vtime * (vtime + FRAME_MAX_VLOAD / max_time - max_time);
 * [0, 0] -=> [max_time, FRAME_MAX_VLOAD]
 *
 */
/* frame_load : caculate frame load using vtime, vload for FRMAE_START */
unsigned long calc_frame_vload(struct frame_info *frame_info, u64 timeline)
{
	unsigned long vload = 0;
	int factor = 0;
	int vtime = div_u64(timeline, NSEC_PER_MSEC);
	int max_time = frame_info->max_vload_time;

	if (max_time <= 0 || vtime > max_time)
		return FRAME_MAX_VLOAD;

	factor = vtime + FRAME_MAX_VLOAD / max_time;

	/* margin maybe negative */
	if (vtime <= 0 || factor <= max_time)
		return 0;

	vload = vtime * (factor - max_time);

	return vload;
}

unsigned long calc_frame_load(struct frame_info *frame_info, bool vload)
{
	unsigned long load = 0;

	if (frame_info->frame_interval > 0) {
		if (vload) {
			/* frame_load : caculate frame load using group exec time, vload for FRMAE_END or FRAME_INVALID */
			load = div_u64((frame_info_fbg_load(frame_info)->curr_window_exec
				<< SCHED_CAPACITY_SHIFT), frame_info->frame_interval);
		} else {
			/* frame_load : caculate frame load using group scale time, rload for FRMAE_START */
			load = div_u64((frame_info_fbg_load(frame_info)->curr_window_scale
				<< SCHED_CAPACITY_SHIFT), frame_info->frame_interval);
		}
	}

	return load;
}

/* real_util = max(last_util, frame_vload, frame_min_util) */
static inline u64 calc_frame_util(struct frame_info *frame_info)
{
	unsigned long load_util;

	load_util = frame_info->prev_frame_load_util;

	load_util = max_t(unsigned long, load_util, frame_info->frame_min_util);

	return min_t(unsigned long, max_t(unsigned long, load_util,
		frame_info->frame_vload),
		READ_ONCE(frame_info->frame_max_util));
}


static inline bool check_frame_util_invalid(struct frame_info *frame_info, u64 timeline)
{
	return ((frame_info->fbg)->util_invalid_interval <= timeline);
}

void update_frame_info_tick(struct frame_boost_group *grp, u64 wallclock,  struct task_struct *p)
{
	u64 timeline = 0;
	u64 exec = 0;
	u64 critical_time = 0;
	struct frame_info *frame_info = NULL;
	unsigned long curr_load = 0;
	unsigned long vload = 0;
	unsigned long critical_load = 0;
	int prev_cpu = task_cpu(p);
	up_migrate = false;

	if (atomic_read(&start_frame) == 0)
		return;

	rcu_read_lock();
	frame_info = fbg_frame_info(grp);
	rcu_read_unlock();

	if (!frame_info)
		return;

	timeline = wallclock - grp->window_start;
	exec = wallclock - p->last_wake_ts;

	switch (frame_info->status) {
	case FRAME_INVALID:
	case FRAME_END:
		if (timeline >= frame_info->frame_interval) {
			/*
			 * fake FRAME_END here to rollover frame_window.
			 * set_frame_timestamp(FRAME_END);
			 */
			sched_set_group_window_rollover();
			set_frame_end(frame_info, true);
		} else {
			frame_info->frame_vload = calc_frame_load(frame_info, true);
			frame_info->frame_util = calc_frame_util(frame_info);
		}
		break;
	case FRAME_START:
		/* check frame_util invalid */
		if (!check_frame_util_invalid(frame_info, timeline)) {
			int interval_time = frame_info->frame_interval / NSEC_PER_MSEC;
			frame_info->frame_vload = calc_frame_vload(frame_info, timeline);
			vload = frame_info->frame_vload;

			/* add for continuous execution*/
			if (!use_vload && exec >= max_exec_time && timeline >= max_exec_time) {
				use_vload = true;

				/* if task migrates from silver cluster to gold cluster, update cpufreq after next tick  */
				if (prev_cpu < cpumask_first(&sched_cluster[1]->cpus)) {
					up_migrate = true;
					last_migrate_time = wallclock;
				}
			}

			if (frame_info->max_vload_time >= interval_time) {
				curr_load = calc_frame_load(frame_info, false);
				critical_time = max_exec_time + (max_exec_time >> 1);
				critical_load = (vload >> 1) + (vload >> 2);

				/* add for discontinuous execution*/
				if (!use_vload && timeline >= critical_time && curr_load >= 358) {
					use_vload = true;
				}

				if ((curr_load <= critical_load) && !use_vload) {
					frame_info->frame_vload = curr_load;
				}

				frame_info->frame_vload = max(curr_load, frame_info->frame_vload);
			}
			frame_info->frame_util = calc_frame_util(frame_info);
		} else {
			frame_info->status = FRAME_INVALID;
			/*
			 * trigger FRAME_END to rollover frame window,
			 * we treat FRAME_INVALID as FRAME_END.
			 */
			sched_set_group_window_rollover();
			set_frame_end(frame_info, false);
		}
		break;
	default:
		return;
	}

	frame_boost(frame_info);
	sched_set_group_normalized_util(frame_info->frame_util, FRAME_NORMAL_UPDATE);
	trace_frame_info_tick(p, timeline, exec, curr_load, vload, use_vload, frame_info->frame_qos, frame_info->frame_util);
}

void sched_update_fbg_tick(struct task_struct *p, u64 wallclock)
{
	struct frame_boost_group *grp = NULL;
	trace_fbg_tick_task(p, p->fbg_depth);

	rcu_read_lock();
	grp = task_frame_boost_group(p);
	if (!grp || list_empty(&grp->tasks)) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	update_frame_info_tick(grp, wallclock, p);
}

static int __init init_frame_info(void)
{
	struct frame_boost_group *grp = NULL;
	struct frame_info *frame_info = NULL;
	unsigned long flags;

	frame_info = &global_frame_info;
	memset(frame_info, 0, sizeof(struct frame_info));

	frame_info->frame_qos = DEFAULT_FRAME_QOS;
	frame_info->frame_interval =  NSEC_PER_SEC / frame_info->frame_qos;
	frame_info->vload_margin = DEFAULT_VLOAD_MARGIN;
	frame_info->max_vload_time = (frame_info->frame_interval / NSEC_PER_MSEC) + frame_info->vload_margin;

	frame_info->frame_max_util = 600;
	frame_info->frame_min_util = 0;
	frame_info->frame_boost_min_util = 0;

	frame_info->status = FRAME_END;
	grp = frame_grp();

	raw_spin_lock_irqsave(&grp->lock, flags);
	grp->private_data = frame_info;
	raw_spin_unlock_irqrestore(&grp->lock, flags);

	frame_info->fbg = grp;
	return 0;
}
late_initcall(init_frame_info);
