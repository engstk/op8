#ifndef _FRAME_BOOST_GROUP_H
#define _FRAME_BOOST_GROUP_H

#include <linux/version.h>

enum DYNAMIC_TRANS_TYPE {
	DYNAMIC_TRANS_BINDER = 0,
	DYNAMIC_TRANS_TYPE_MAX,
};

enum freq_update_flags {
	FRAME_FORCE_UPDATE = (1 << 0),
	FRAME_NORMAL_UPDATE = (1 << 1),
};

struct rq;
struct group_time {
	u64 curr_window_scale;
	u64 curr_window_exec;
	u64 prev_window_scale;
	u64 prev_window_exec;
	unsigned long normalized_util;
};

struct frame_boost_group {
	raw_spinlock_t lock;
	struct list_head tasks;
	struct sched_cluster *preferred_cluster;
	struct group_time time;
	unsigned long freq_update_interval; /*in nanoseconds */
	unsigned long util_invalid_interval; /*in nanoseconds */
	unsigned long util_update_timeout; /*in nanoseconds */
	u64 last_freq_update_time;
	u64 last_util_update_time;
	u64 window_start;
	u64 mark_start;
	u64 prev_window_size;
	int nr_running;
	unsigned int window_size;
	void *private_data;
};

void update_frame_thread(int pid, int tid);
void update_hwui_tasks(int hwtid1, int hwtid2);
int sched_set_frame_boost_group(struct task_struct *p, bool is_add);
struct frame_boost_group *frame_grp(void);
int sched_set_group_window_rollover(void);
void update_group_demand(struct task_struct *p, struct rq *rq, int event, u64 wallclock);
void update_group_nr_running(struct task_struct *p, int event, u64 wallclock);
void sched_set_group_window_size(unsigned int window);
struct frame_boost_group *task_frame_boost_group(struct task_struct *p);
void sched_set_group_normalized_util(unsigned long util, unsigned int flag);
int find_fbg_cpu(struct task_struct *p);
struct cpumask *find_rtg_target(struct task_struct *p);
unsigned long sched_get_group_util(const struct cpumask *query_cpus);
void binder_thread_set_fbg(struct task_struct *thread, struct task_struct *from, bool oneway);
void binder_thread_remove_fbg(struct task_struct *thread, bool oneway);
int schedtune_grp_margin(unsigned long util);
bool isUi(struct task_struct *p);
void record_cr_main(struct task_struct *p);
bool is_cr_main(struct task_struct *p);
int find_cr_main_cpu(struct task_struct *p);
bool is_prime_fits(int cpu);
bool need_frame_boost(struct task_struct *p);
#endif
