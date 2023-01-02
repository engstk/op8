#ifndef _OPLUS_FRAME_INFO_H
#define _OPLUS_FRAME_INFO_H

struct frame_info {
	struct frame_boost_group *fbg;

	unsigned int frame_qos;
	unsigned int frame_interval;
	unsigned long frame_vload;
	int vload_margin;
	int max_vload_time;

	//unsigned long prev_fake_load_util;
	unsigned long prev_frame_load_util;
	unsigned long prev_frame_interval;
	u64 prev_frame_exec;
	u64 prev_frame_scale;

	unsigned long frame_util;

	unsigned long status;
	int frame_max_util;
	int frame_min_util;
	int frame_boost_min_util;
};

#define FRAME_START	(1 << 0)
#define FRAME_END	(1 << 1)
#define FRAME_INVALID	(1 << 2)

void set_frame_sched_state(bool enable);
int set_frame_timestamp(unsigned long timestamp);
void sched_update_fbg_tick(struct task_struct *p, u64 wallclock);
void update_frame_state(int pid, bool in_frame);
void set_frame_rate(unsigned int frame_rate);
int set_frame_min_util(int min_util, bool isBoost);
unsigned long calc_frame_load(struct frame_info *frame_info, bool vload);
struct frame_info *fbg_frame_info(struct frame_boost_group *grp);
#endif
