// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/sysctl.h>
#include <linux/tick.h>
#include <linux/kernel_stat.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>

#include "../../../fs/proc/internal.h"
#include "jank_cpuloadmonitor.h"
#include "jank_netlink.h"

#define CPU_LOAD_TIMER_RATE 5
#define CPU_HIGH_LOAD_THRESHOLD 4
#define CPU_LOW_LOAD_THRESHOLD 0

#define DETECT_PERIOD 1000000

#define CPU_LOAD_BIG_HIGHCYCLE 6
#define CPU_LOAD_BIG_LOWCYCLE 3

#define CPUS_PROC_DURING 4000000
#define CPUS_PROC_PERIOD 400000

#define PID_LIST_MAX 20
#define HIGH_LOAD_MAX_PIDS 20
#define HIGH_LOAD_MAX_TIDS 8

#define HIGH_LOAD_PERCENT 100
#define PERCENT_HUNDRED 100

#define PROC_STATIC_MAX 1024
#define PROC_STATIC_CPUSET_LBG 4			/* FIXME */
#define PROC_STATIC_CPUSET_BG 5
#define PROC_STATIC_CPUSET_HBG 4
#define PROC_STATIC_CPUSET_HFG 5
#define PROC_STATIC_CLUSTER_BIG 5

#define MAX_LAST_RQCLOCK 5000000
#define MAX_BUF_LEN 10
#define MAX_THRESHOLD 100

enum {
	DEFAULT = 0,
	LOW_LOAD = 1,
	HIGH_LOAD = 2,
};

enum {
	CPUSET_LBG = 0,				/* cpu2,3 */
	CPUSET_BG = 1,				/* cpu0,1,2,3 */
	CPUSET_HBG = 2,				/* cpu0,1,2,3,4 */
	CPUSET_HFG = 3,				/* cpu4,5,6,7 */
	CPUSET_ALL = 4,					/* all cpus */
	CLUSTER_BIG = 5,			/* diacard */
	HIGH_LOAD_MAX_TYPE
};

#define MASK_LBG			0x0c
#define MASK_BG				0x0f
#define MASK_HBG			0x1f
#define MASK_HFG			0xf0


enum {
	LOAD_SWITCH_LBG = 0,
	LOAD_SWITCH_BG = 1,
	LOAD_SWITCH_HBG = 2,
	LOAD_SWITCH_HFG = 3,
	LOAD_SWITCH_DEFAULT = 4,
	LOAD_SWITCH_BIGCORE = 5,
};

struct task_id {
	/* FIXME: uid & tgid only */
	/* int pid; */
	int uid;
	int tgid;
};

struct high_load_data {
	int cmd;
	struct task_id id[HIGH_LOAD_MAX_PIDS];
};

struct action_ctl {
	u64 bits_high;
	u64 bits_type;
};

static int check_intervals;
static int check_proc_static;
static int high_load_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static int normal_load_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static int last_status[HIGH_LOAD_MAX_TYPE] = { 0 };
static int cycle_big_high_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static int cycle_big_normal_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static int cycle_big_cycles[HIGH_LOAD_MAX_TYPE] = { 0 };

#ifdef JANK_DEBUG
static unsigned long high_load_switch = 0x0F;
#else
static unsigned long high_load_switch;
#endif
static struct delayed_work high_load_work;
static struct delayed_work cpus_proc_static_work;

static unsigned long cpumask_lbg = MASK_LBG;
static unsigned long cpumask_bg = MASK_BG;
static unsigned long cpumask_hbg = MASK_HBG;
static __maybe_unused unsigned long cpumask_hfg = MASK_HFG;

static unsigned long cpumask_big = 0x000000c0;

static struct cpufreq_policy *policy;
static unsigned int *freqs_weight;
static u64 *freqs_time;
static u64 *freqs_time_last;
static int freqs_len;
static unsigned int fg_freqs_threshold = 75;

static struct action_ctl action_ctl_bits = { 0 };

static unsigned int highload_threshold = 98;			/* all cpus */
static unsigned int highload_cpuset_lbg = 95;
static unsigned int highload_cpuset_bg = 95;
static unsigned int highload_cpuset_hbg = 95;
static unsigned int highload_cpuset_hfg = 95;
static unsigned int lowload_cpuset_lbg = 60;
static unsigned int lowload_cpuset_bg = 60;
static unsigned int lowload_cpuset_hbg = 60;
static unsigned int lowload_cpuset_hfg = 60;


struct pid_stat_node {
	struct rb_node node;
	pid_t key_pid;
	uid_t uid;
	pid_t tgid;
	int count;
};

struct pid_stat_mgr {
	int index_curr;
	int max_count;
	struct pid_stat_node *head;
	struct rb_root rb_root;
	struct high_load_data data;
};

struct pid_stat_mgr g_pid_stat_mgt[HIGH_LOAD_MAX_TYPE] = { 0 };

void cpuset_bg_cpumask(unsigned long bits)
{
	pr_info("cpuload:bg cpumask bits:0x%x", bits);
	cpumask_bg = bits;
}

static inline void set_action_ctl(u32 type, bool high_load)
{
	action_ctl_bits.bits_type |= (1 << type);

	if (high_load)
		action_ctl_bits.bits_high |= (1 << type);
}

static void send_to_user_high(u32 type, int size, int *data)
{
	int i = 0;
	int idx;
	struct high_load_data high_data;

	/*
	 * Modify BYHP
	 * CPUSET_HBG, CPUSET_BG, and CPUSET_LBG groups use the same CMD
	 */
	if (type >= CPUSET_LBG && type <= CPUSET_HBG) {
		type = CPUSET_BG;
	}

	memset(&high_data, 0, sizeof(high_data));
	high_data.cmd = (type << 1) - 1;

	/* Modify BYHP */
	for (i = 0; i < size; i++) {
		idx = (sizeof(struct task_id)/sizeof(int)) * i;

		/* FIXME: uid & tgid only */
		/* high_data.id[i].pid = data[idx]; */
		high_data.id[i].uid = data[idx];
		high_data.id[i].tgid = data[idx+1];
	}

	send_to_user(PROC_LOAD, sizeof(high_data) / sizeof(int),
		(int *)&high_data);
}

static __maybe_unused void send_to_user_low(u32 type)
{
	struct high_load_data high_data;

	/*
	 * Modify BYHP
	 * CPUSET_HBG, CPUSET_BG, and CPUSET_LBG groups use the same CMD
	 */
	if (type >= CPUSET_LBG && type <= CPUSET_HBG) {
		type = CPUSET_BG;
	}

	memset(&high_data, 0, sizeof(high_data));
	high_data.cmd = (type << 1);

	/* FIXME: uid & pid only */
	/* high_data.id[0].pid = 0; */
	high_data.id[0].uid = 0;
	high_data.id[0].tgid = 0;
	send_to_user(PROC_LOAD, sizeof(high_data) / sizeof(int),
		(int *)&high_data);
}

static int pidstat_init(void)
{
	int i = 0;

	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++) {
		g_pid_stat_mgt[i].index_curr = 0;
		g_pid_stat_mgt[i].max_count =
			(CPUS_PROC_DURING / CPUS_PROC_PERIOD) * CPU_NUMS;
		g_pid_stat_mgt[i].head =
			kmalloc_array(g_pid_stat_mgt[i].max_count,
				sizeof(*(g_pid_stat_mgt[i].head)), GFP_KERNEL);

		if (g_pid_stat_mgt[i].head == NULL)
			return -ENOMEM;

		g_pid_stat_mgt[i].rb_root = RB_ROOT;
		memset(g_pid_stat_mgt[i].head, 0, g_pid_stat_mgt[i].max_count *
			sizeof(*(g_pid_stat_mgt[i].head)));
		memset(&g_pid_stat_mgt[i].data, 0,
			sizeof(g_pid_stat_mgt[i].data));
	}

	pr_info("cpuload: pidstat init ok!");
	return 0;
}

static struct pid_stat_node *pid_stat_getnode(u32 type)
{
	if (g_pid_stat_mgt[type].index_curr >=
		(g_pid_stat_mgt[type].max_count - 1))
		return NULL;

	g_pid_stat_mgt[type].index_curr++;
	return g_pid_stat_mgt[type].head + g_pid_stat_mgt[type].index_curr - 1;
}

static void pid_stat_reset(u32 type)
{
	if (type >= HIGH_LOAD_MAX_TYPE)
		return;

	memset(g_pid_stat_mgt[type].head,
		0,
		g_pid_stat_mgt[type].index_curr *
		sizeof(*(g_pid_stat_mgt[type].head)));
	memset(&g_pid_stat_mgt[type].data, 0, sizeof(g_pid_stat_mgt[type].data));
	g_pid_stat_mgt[type].index_curr = 0;
	g_pid_stat_mgt[type].rb_root = RB_ROOT;
}

static void pid_stat_clear(void)
{
	int i = 0;

	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++) {
		if (g_pid_stat_mgt[i].head != NULL)
			kfree(g_pid_stat_mgt[i].head);

		memset(&g_pid_stat_mgt[i], 0, sizeof(g_pid_stat_mgt[i]));
	}
}

static void pid_stat_search_insert(pid_t key_pid, pid_t pid, uid_t uid, pid_t tgid, u32 type)
{
	struct rb_node **curr = &(g_pid_stat_mgt[type].rb_root.rb_node);
	struct rb_node *parent = NULL;
	struct pid_stat_node *this = NULL;
	pid_t ret_pid;
	struct pid_stat_node *new_node = NULL;

	while (*curr) {
		this = container_of(*curr, struct pid_stat_node, node);
		parent = *curr;
		ret_pid = key_pid - this->key_pid;

		if (ret_pid < 0) {
			curr = &((*curr)->rb_left);
		} else if (ret_pid > 0) {
			curr = &((*curr)->rb_right);
		} else {
			this->count++;
			return;
		}
	}

	/* add new node and rebalance tree. */
	new_node = pid_stat_getnode(type);
	if (new_node == NULL)
		return;

	new_node->key_pid = key_pid;
	new_node->uid = uid;
	new_node->tgid = tgid;
	new_node->count = 1;
	rb_link_node(&new_node->node, parent, curr);
	rb_insert_color(&new_node->node, &g_pid_stat_mgt[type].rb_root);
}

int send_to_user_netlink(int data)
{
	int dt[] = { data };

	return send_to_user(CPU_HIGH_LOAD, 1, dt);
}

int send_to_user_with_usage(int sock_no, int *data, int cnt)
{
	u32 i;
	u32 dt[CPU_NUMS+1] = {0};

	dt[0] = sock_no;
	for (i = 0; i < cnt; i++)
		dt[i+1] = data[i];

	return send_to_user(CPU_HIGH_LOAD, cnt+1, dt);
}


/*lint -save -e501 -e64 -e507 -e644 -e64 -e409*/
static u64 get_idle_time_jk(int cpu)
{
	u64 idle;
	u64 idle_time = -1ULL;

	if (cpu_online(cpu))
		idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	else
		idle = idle_time * NSEC_PER_USEC;

	return idle;
}

static u64 get_iowait_time_jk(int cpu)
{
	u64 iowait;
	u64 iowait_time = -1ULL;

	if (cpu_online(cpu))
		iowait_time = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_time == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
	else
		iowait = iowait_time * NSEC_PER_USEC;

	return iowait;
}

static u64 cpu_total_time[CPU_NUMS];
static u64 cpu_busy_time[CPU_NUMS];
static int cpu_online[CPU_NUMS];

static void get_cpu_time(void)
{
	int i = 0;
	u64 total_time;
	u64 busy_time;

	memset(cpu_online, 0, sizeof(cpu_online));
	for_each_online_cpu(i) {
		if (i >= CPU_NUMS || i < 0)
			break;

		busy_time = kcpustat_cpu(i).cpustat[CPUTIME_USER];
		busy_time += kcpustat_cpu(i).cpustat[CPUTIME_NICE];
		busy_time += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];

		total_time = busy_time;
		total_time += get_idle_time_jk(i);
		total_time += get_iowait_time_jk(i);
		total_time += kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		total_time += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
		total_time += kcpustat_cpu(i).cpustat[CPUTIME_STEAL];
		total_time += kcpustat_cpu(i).cpustat[CPUTIME_GUEST];
		total_time += kcpustat_cpu(i).cpustat[CPUTIME_GUEST_NICE];
		cpu_total_time[i] = total_time;
		cpu_busy_time[i] = busy_time;
		cpu_online[i] = 1;
	}
}

/*
 * This function recoreds the cpustat information
 */
static void get_cpu_load(u64 *total_time, u64 *busy_time,
	unsigned long cpumask)
{
	u32 i = 0;

	for (i = 0; i < CPU_NUMS; i++) {
		if (!cpu_online[i])
			continue;

		if (((1 << i) & cpumask) == 0)
			continue;

		*total_time += cpu_total_time[i];
		*busy_time += cpu_busy_time[i];
	}
}

static void update_cputime(u64 *total_time, u64 *busy_time)
{
	u32 i = 0;

	for (i = 0; i < CPU_NUMS; i++) {
		if (!cpu_online[i]) {
			total_time[i] = 0;
			busy_time[i] = 0;
		} else {
			total_time[i] = cpu_total_time[i];
			busy_time[i] = cpu_busy_time[i];
		}
	}
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 */

bool high_load_tick(void);
bool high_load_tick_mask(unsigned long bits, u32 type,
	int threhold_up, int threhold_down);

static unsigned long get_mask_bytype(u32 type)
{
	switch (type) {
	case CPUSET_LBG:
		return cpumask_lbg;
	case CPUSET_BG:
		return cpumask_bg;
	case CPUSET_HBG:
		return cpumask_hbg;
	case CPUSET_HFG:
		return 0xff;				/* cpumask_hfg */
	case CLUSTER_BIG:
		return cpumask_big;
	default:
		return 0;
	}
}

static int get_threhold_bytype(u32 type)
{
	switch (type) {
	case CPUSET_LBG:
		return PROC_STATIC_CPUSET_LBG;
	case CPUSET_BG:
		return PROC_STATIC_CPUSET_BG;
	case CPUSET_HBG:
		return PROC_STATIC_CPUSET_HBG;
	case CPUSET_HFG:
		return PROC_STATIC_CPUSET_HFG;
	case CLUSTER_BIG:
		return PROC_STATIC_CLUSTER_BIG;
	default:
		return PROC_STATIC_MAX;
	}
}

static void high_load_tickfn(struct work_struct *work);
static void cpus_proc_static_tickfn(struct work_struct *work);

#ifdef CONFIG_HISI_FREQ_STATS_COUNTING_IDLE
static void high_freqs_load_tick(void);
#endif

static void high_load_count_reset(void)
{
	check_intervals = 0;

	high_load_cnt[CPUSET_ALL] = 0;
	high_load_cnt[CPUSET_LBG] = 0;
	high_load_cnt[CPUSET_BG] = 0;
	high_load_cnt[CPUSET_HBG] = 0;
	high_load_cnt[CPUSET_HFG] = 0;
	high_load_cnt[CLUSTER_BIG] = 0;

	normal_load_cnt[CPUSET_LBG] = 0;
	normal_load_cnt[CPUSET_BG] = 0;
	normal_load_cnt[CPUSET_HBG] = 0;
	normal_load_cnt[CPUSET_HFG] = 0;
	normal_load_cnt[CLUSTER_BIG] = 0;
}

void highload_report(void)
{
	u32 type, state;
	u32 i = 0;
	u32 data[HIGH_LOAD_MAX_TYPE*2] = {-1};
#ifdef JANK_DEBUG
	u32 j;
#endif

	if (!action_ctl_bits.bits_type)
		return;

	for (type = CPUSET_LBG; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(high_load_switch & (1 << type)))
			continue;

		if (action_ctl_bits.bits_type & (1 << type)) {
			if (action_ctl_bits.bits_high & (1 << type))
				state = HIGH_LOAD;
			else
				state = LOW_LOAD;
		} else {
			if (last_status[type] == HIGH_LOAD)
				state = HIGH_LOAD;
			else
				state = DEFAULT;
		}

		data[i++] = 1 << type;
		data[i++] = state;
	}

#ifdef JANK_DEBUG
	for (j = 0; j < i; j++) {
		pr_info("cpuload: [highload_report:%d] data[%d]=%d\n", i, j, data[j]);
	}
#endif

	send_to_user(CPU_HIGH_LOAD, i, data);
}

static void high_load_tickfn(struct work_struct *work)
{
	++check_intervals;

	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		action_ctl_bits.bits_type = 0;
		action_ctl_bits.bits_high = 0;
	}

	get_cpu_time();

	if ((high_load_switch & (1 << LOAD_SWITCH_DEFAULT)) != 0) {
		if (high_load_tick())
			goto HIGHLOAD_END;
	}

	/* GRP_LBG: cpu2,3 */
	if ((high_load_switch & (1 << LOAD_SWITCH_LBG)) != 0)
		high_load_tick_mask(MASK_LBG, CPUSET_LBG,
			highload_cpuset_lbg, lowload_cpuset_lbg);

	/* GRP_BG: cpu4,5,6,7 */
	if ((high_load_switch & (1 << LOAD_SWITCH_BG)) != 0)
		high_load_tick_mask(MASK_BG, CPUSET_BG,
			highload_cpuset_bg, lowload_cpuset_bg);

	/* GRP_HBG: cpu0,1,2,3,4 */
	if ((high_load_switch & (1 << LOAD_SWITCH_HBG)) != 0)
		high_load_tick_mask(MASK_HBG, CPUSET_HBG,
			highload_cpuset_hbg, lowload_cpuset_hbg);

	/* GRP_HFG: cpu4,5,6,7 */
	if ((high_load_switch & (1 << LOAD_SWITCH_HFG)) != 0)
		high_load_tick_mask(MASK_HFG, CPUSET_HFG,
			highload_cpuset_hfg, lowload_cpuset_hfg);

	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
#ifdef CONFIG_HISI_FREQ_STATS_COUNTING_IDLE
		if ((high_load_switch & (1 << LOAD_SWITCH_BIGCORE)) != 0)
			high_freqs_load_tick();
#endif
		if (action_ctl_bits.bits_type) {
			highload_report();
			schedule_delayed_work_on(0, &cpus_proc_static_work,
				usecs_to_jiffies(CPUS_PROC_PERIOD));
		}
	}

HIGHLOAD_END:
	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		high_load_count_reset();
	}

	schedule_delayed_work(&high_load_work, usecs_to_jiffies(DETECT_PERIOD));
}

static struct task_id threhold_pid_list[HIGH_LOAD_MAX_TYPE][PID_LIST_MAX] = { 0 };
static int threhold_pid_size[HIGH_LOAD_MAX_TYPE] = { 0 };

static void cpus_procstatic_low(void)
{
	u32 type;
	int i;

	for (type = 1; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;

		if (action_ctl_bits.bits_high & (1 << type))
			continue;

		pr_info("cpuload: delete size:%d type:%u",
			threhold_pid_size[type], type);

		/* 
		 * Modify BYHP
		 * Do NOT reported when the load is low
		 */
		/* send_to_user_low(type); */
		for (i = 0; i < threhold_pid_size[type]; i++) {
			/* Modify BYHP */
			/* threhold_pid_list[type][i].pid = 0; */
			threhold_pid_list[type][i].uid = 0;
			threhold_pid_list[type][i].tgid = 0;
		}

		pid_stat_reset(type);
		threhold_pid_size[type] = 0;
	}
}

static void cpus_proc_static_high(u32 type)
{
	int i;
	struct pid_stat_node *curr = NULL;
	int index;

	for (i = 0, curr = g_pid_stat_mgt[type].head;
		i < g_pid_stat_mgt[type].index_curr;
		i++, curr++) {
		if (curr->count > get_threhold_bytype(type)) {
			pr_info("cpuload: key_pid:%d,count:%d,type:%u",
				curr->key_pid, curr->count, type);

			if (threhold_pid_size[type] < PID_LIST_MAX) {
				index = threhold_pid_size[type];

				/* Modify BYHP */
				/* threhold_pid_list[type][index].pid = curr->key_pid; */
				threhold_pid_list[type][index].uid = curr->uid;
				threhold_pid_list[type][index].tgid = curr->tgid;

				threhold_pid_size[type]++;
			}
		}
	}

	pr_info("cpuload: threhold_pid_size:%d,type:%u", threhold_pid_size[type], type);
	if (threhold_pid_size[type])
		send_to_user_high(type, threhold_pid_size[type], (int *)threhold_pid_list[type]);
	pid_stat_reset(type);
	for (i = 0; i < threhold_pid_size[type]; i++) {
		/* threhold_pid_list[type][i].pid = 0; */
		threhold_pid_list[type][i].uid = 0;
		threhold_pid_list[type][i].tgid = 0;
	}
	threhold_pid_size[type] = 0;
}

bool high_load_tick(void)
{
	bool ret = false;
	static int last_report_reason = LOW_LOAD;
	static u64 last_total_time;
	static u64 last_busy_time;
	u64 total_time = 0;
	u64 busy_time = 0;
	u64 total_delta_time;

	static u64 last_total_time_per[CPU_NUMS];
	static u64 last_busy_time_per[CPU_NUMS];
	u64 total_time_per[CPU_NUMS] = {0};
	u64 busy_time_per[CPU_NUMS] = {0};
	u32 usage_per[CPU_NUMS] = {0};
	u32 i;

	get_cpu_load(&total_time, &busy_time, 0xffffffff);
	total_delta_time = total_time - last_total_time;

	if (check_intervals == 1)
		update_cputime(last_total_time_per, last_busy_time_per);

	if (total_delta_time != 0) {
		if ((busy_time - last_busy_time) * HIGH_LOAD_PERCENT >=
			highload_threshold * total_delta_time)
			high_load_cnt[CPUSET_ALL]++;
	}

	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		if (high_load_cnt[CPUSET_ALL] >= CPU_HIGH_LOAD_THRESHOLD
			&& last_report_reason != HIGH_LOAD) {
			update_cputime(total_time_per, busy_time_per);
			for (i = 0; i < CPU_NUMS; i++) {
				total_delta_time = total_time_per[i] - last_total_time_per[i];
				if (!total_delta_time)
					usage_per[i] = 0;
				else
					usage_per[i] = ((busy_time_per[i] - last_busy_time_per[i])\
						* HIGH_LOAD_PERCENT) / total_delta_time;
			}

			send_to_user_with_usage(HIGH_LOAD, usage_per, CPU_NUMS);
			last_report_reason = HIGH_LOAD;
			ret = true;
			pr_info("cpuload: cpuload HIGH_LOAD!");
		} else if (high_load_cnt[CPUSET_ALL] == CPU_LOW_LOAD_THRESHOLD
			&& last_report_reason != LOW_LOAD) {
			send_to_user_netlink(LOW_LOAD);
			last_report_reason = LOW_LOAD;
			ret = true;
			pr_info("cpuload: cpuload LOW_LOAD!");
		}
	}

	last_total_time = total_time;
	last_busy_time = busy_time;
	return ret;
}

static void cycle_big_count_reset(u32 type)
{
	cycle_big_high_cnt[type] = 0;
	cycle_big_normal_cnt[type] = 0;
	cycle_big_cycles[type] = 0;
}

static bool cycle_big_highcycle(u32 type)
{
	bool ret = false;

	if (last_status[type] == HIGH_LOAD) {
		if (cycle_big_high_cnt[type] >
			(CPU_LOAD_BIG_HIGHCYCLE * CPU_HIGH_LOAD_THRESHOLD)) {
			pr_info("cpuload: type=%d, cycle big HIGH_LOAD!", type);
			set_action_ctl(type, true);
			ret = true;
		}
	}

	cycle_big_count_reset(type);
	return ret;
}

static bool cycle_big_lowcycle(u32 type)
{
	if (last_status[type] == HIGH_LOAD) {
		if (cycle_big_normal_cnt[type] >
			(CPU_LOAD_BIG_LOWCYCLE * CPU_HIGH_LOAD_THRESHOLD)) {
			pr_info("cpuload: type=%d, cycle big LOW_LOAD!", type);
			last_status[type] = LOW_LOAD;
			set_action_ctl(type, false);
			cycle_big_count_reset(type);
			return true;
		}
	}

	return false;
}

bool high_load_tick_mask(unsigned long bits, u32 type,
	int threhold_up, int threhold_down)
{
	static u64 last_total_time[HIGH_LOAD_MAX_TYPE] = { 0 };
	static u64 last_busy_time[HIGH_LOAD_MAX_TYPE] = { 0 };
	u64 total_time = 0;
	u64 busy_time = 0;
	u64 total_delta_time;
	u64 busy_precent = 0;

	get_cpu_load(&total_time, &busy_time, bits);

	total_delta_time = total_time - last_total_time[type];
	if (total_delta_time != 0) {
		busy_precent =
			(busy_time - last_busy_time[type]) * HIGH_LOAD_PERCENT;
		if (busy_precent >= (threhold_up * total_delta_time)) {
			high_load_cnt[type]++;
			cycle_big_high_cnt[type]++;
		}

		if (busy_precent < (threhold_down * total_delta_time)) {
			normal_load_cnt[type]++;
			cycle_big_normal_cnt[type]++;
		}
	}

	last_total_time[type] = total_time;
	last_busy_time[type] = busy_time;

	if (check_intervals < CPU_LOAD_TIMER_RATE)
		return false;

	cycle_big_cycles[type]++;

	if (high_load_cnt[type] >= CPU_HIGH_LOAD_THRESHOLD &&
		last_status[type] != HIGH_LOAD) {
		pr_info("cpuload: cpuload mask HIGH_LOAD\n");
		last_status[type] = HIGH_LOAD;
		set_action_ctl(type, true);
		cycle_big_count_reset(type);
		return true;
	}

	if ((cycle_big_cycles[type] % CPU_LOAD_BIG_LOWCYCLE) == 0) {
		if (cycle_big_lowcycle(type))
			return true;
	}

	if (cycle_big_cycles[type] >= CPU_LOAD_BIG_HIGHCYCLE) {
		if (cycle_big_highcycle(type))
			return true;
	}

	return false;
}

#ifdef CONFIG_HISI_FREQ_STATS_COUNTING_IDLE
int hisi_time_in_freq_get(int cpu, u64 *freqs, int freqs_len)
{
	/* TODO */
	return 0;
}

static void high_freqs_load_tick(void)
{
	u64 delta_time;
	u64 delta_freqs_time;
	ktime_t now;
	static ktime_t last;
#ifdef CONFIG_HISI_FREQ_STATS_COUNTING_IDLE
	int i;
	int ret_err;
#endif
	if ((!policy) || (!freqs_weight))
		return;

	now = ktime_get();
	delta_time = ktime_us_delta(now, last);
	last = now;

#ifdef CONFIG_HISI_FREQ_STATS_COUNTING_IDLE
	ret_err = hisi_time_in_freq_get(CPU_NUMS - 1,
		freqs_time, freqs_len);
	if (ret_err)
		return;

	delta_freqs_time = 0;
	for (i = 0; i < freqs_len; i++) {
		if (freqs_weight[i] < fg_freqs_threshold)
			continue;
		delta_freqs_time += (freqs_time[i] - freqs_time_last[i]) *
			freqs_weight[i];
		freqs_time_last[i] = freqs_time[i];
	}

	if (delta_freqs_time > (delta_time * fg_freqs_threshold)) {
		pr_info("cpuload: high freqs load");
		set_action_ctl(CLUSTER_BIG, true);
		last_status[CLUSTER_BIG] = HIGH_LOAD;
	} else if (last_status[CLUSTER_BIG] != LOW_LOAD) {
		set_action_ctl(CLUSTER_BIG, false);
		last_status[CLUSTER_BIG] = LOW_LOAD;
	}
#endif
}
#endif

static void get_current_task_mask(u32 type)
{
	u32 cpu = 0;
	pid_t curr_pid;
	pid_t local_pid = current->pid;
	uid_t uid;
	pid_t tgid;
	unsigned long cpumask = get_mask_bytype(type);

	for (cpu = 0; cpu < CPU_NUMS; cpu++) {
		struct rq *rq_cur = NULL;
		struct task_struct *p = NULL;

		if (((1 << cpu) & cpumask) == 0)
			continue;

		rq_cur = cpu_rq(cpu);
		if (!rq_cur) {
			pr_info("cpuload: cpu:%u rq_cur is NULL!", cpu);
			continue;
		}

		p = rq_cur->curr;
		if (!p) {
			pr_info("cpuload: cpu:%u p is NULL!", cpu);
			continue;
		}

		get_task_struct(p);
		curr_pid = p->pid;
		uid = task_uid(p).val;
		tgid = p->tgid;
		put_task_struct(p);
		pr_info("cpuload: pid=%d, uid=%d, tgid=%d\n", curr_pid, uid, tgid);

		if (curr_pid == 0 || curr_pid == local_pid)
			continue;

		if (curr_pid != 0)
			pid_stat_search_insert(tgid, curr_pid, uid, tgid, type);
	}
}

static void get_current_thread_mask(u32 type)
{
	u32 cpu = 0;
	pid_t curr_pid;
	pid_t local_pid = current->pid;
	uid_t uid;
	pid_t tgid;
	unsigned long cpumask = get_mask_bytype(type);

	for (cpu = 0; cpu < CPU_NUMS; cpu++) {
		struct rq *rq_cur = NULL;
		struct task_struct *p = NULL;

		if (((1 << cpu) & cpumask) == 0)
			continue;

		rq_cur = cpu_rq(cpu);
		if (!rq_cur) {
			pr_info("cpuload: cpu:%u rq_cur is NULL!", cpu);
			continue;
		}

		p = rq_cur->curr;

		if (!p) {
			pr_info("cpuload: cpu:%u p is NULL!", cpu);
			continue;
		}

		get_task_struct(p);
		curr_pid = p->pid;
		uid = task_uid(p).val;
		tgid = p->tgid;
		put_task_struct(p);
		pr_info("cpuload: pid=%d, uid=%d, tgid=%d\n", curr_pid, uid, tgid);

		if (curr_pid == 0 || curr_pid == local_pid)
			continue;

		if (curr_pid != 0)
			pid_stat_search_insert(tgid, curr_pid, uid, tgid, type);
	}
}

/* This function is executed every 400ms */
static void cpus_proc_static_tickfn(struct work_struct *work)
{
	int type;

	if (check_proc_static == 0)
		cpus_procstatic_low();

	check_proc_static += CPUS_PROC_PERIOD;

	for (type = CPUSET_HFG; type >= CPUSET_LBG; type--) {
		if (!(high_load_switch & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_high & (1 << type)))
			continue;

#ifdef JANK_DEBUG
		pr_info("cpuload: [cpus_proc_static_tickfn: type=%d]\n", type);
#endif

		/* Samples were taken every 400ms */
		/* Sample only and insert the red-black tree */
		get_current_task_mask(type);

		/* Report in four seconds */
		if (check_proc_static >= CPUS_PROC_DURING)
			cpus_proc_static_high(type);

		if (type != CPUSET_HFG)
			break;
	}

	if (check_proc_static >= CPUS_PROC_DURING) {
		check_proc_static = 0;
	} else {
		schedule_delayed_work_on(0, &cpus_proc_static_work,
			usecs_to_jiffies(CPUS_PROC_PERIOD));
	}
}


/* This function is executed every 400ms */
static __maybe_unused void cpus_proc_static_tickfn_unused(struct work_struct *work)
{
	u32 type;

	if (check_proc_static == 0)
		cpus_procstatic_low();

	check_proc_static += CPUS_PROC_PERIOD;

	for (type = 1; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_high & (1 << type)))
			continue;

		/* get thread or task statistic */
		if (type == CLUSTER_BIG)
			get_current_thread_mask(type);
		else
			get_current_task_mask(type);

		if (check_proc_static >= CPUS_PROC_DURING)
			cpus_proc_static_high(type);
	}

	if (check_proc_static >= CPUS_PROC_DURING) {
		check_proc_static = 0;
	} else {
		schedule_delayed_work_on(0, &cpus_proc_static_work,
			usecs_to_jiffies(CPUS_PROC_PERIOD));
	}
}

static void init_freqs_data(const struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *pos = NULL;
	unsigned int len = 0;
	unsigned int last;
	int i;
	int mem_size;

	cpufreq_for_each_valid_entry(pos, policy->freq_table)
		len++;

	if (len == 0)
		return;
	freqs_len = len;

	last = policy->freq_table[len - 1].frequency / PERCENT_HUNDRED;
	if (last == 0)
		return;

	/*
	 * request memory for three arrays:
	 * freqs_weight, freqs_time, freqs_time_last
	 */
	mem_size = len * (sizeof(unsigned int) + sizeof(u64) + sizeof(u64));
	freqs_weight = kzalloc(mem_size, GFP_KERNEL);
	if (!freqs_weight)
		return;
	freqs_time = (u64 *)(freqs_weight + len);
	freqs_time_last = freqs_time + len;

	i = 0;
	cpufreq_for_each_valid_entry(pos, policy->freq_table)
		freqs_weight[i++] = (pos->frequency / last) + 1;
	freqs_weight[len - 1] = PERCENT_HUNDRED;
}

void jank_calcload_init(void)
{
	int i = 0;

	check_intervals = 0;
	check_proc_static = 0;

	if (pidstat_init() != 0) {
		pr_info("cpuloadmonitor init failed!\n");
		return;
	}

	policy = cpufreq_cpu_get(CPU_NUMS - 1);
	if (policy)
		init_freqs_data(policy);

	INIT_DEFERRABLE_WORK(&high_load_work, high_load_tickfn);
	INIT_DEFERRABLE_WORK(&cpus_proc_static_work, cpus_proc_static_tickfn);
	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++)
		last_status[i] = LOW_LOAD;

	create_cpu_netlink(NETLINK_HW_IAWARE_CPU);
}

void jank_calcload_exit(void)
{
	if (high_load_switch) {
		high_load_count_reset();
		cancel_delayed_work_sync(&high_load_work);
	}
	high_load_switch = 0;

	kfree(freqs_weight);
	freqs_weight = NULL;
	freqs_time = NULL;
	freqs_time_last = NULL;
	freqs_len = 0;
	pid_stat_clear();

	destroy_cpu_netlink();
}

static ssize_t proc_clm_enable_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", high_load_switch);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_enable_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, enable;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &enable);
	if (err)
		return err;

	if (enable != 0) {
		if (!high_load_switch) {
			schedule_delayed_work(&high_load_work,
				usecs_to_jiffies(DETECT_PERIOD));
		}
	} else {
		if (high_load_switch) {
			high_load_count_reset();
			cancel_delayed_work_sync(&high_load_work);
		}
	}

	high_load_switch = enable;

	return count;
}

static ssize_t proc_fg_freqs_threshold_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", fg_freqs_threshold);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_fg_freqs_threshold_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	fg_freqs_threshold = threshold;

	return count;
}

static ssize_t proc_clm_highload_all_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", highload_threshold);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_highload_all_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	highload_threshold = threshold;

	return count;
}

static ssize_t proc_clm_highload_grp_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", highload_cpuset_lbg);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_highload_grp_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	highload_cpuset_lbg = threshold;
	highload_cpuset_bg = threshold;
	highload_cpuset_hbg = threshold;
	highload_cpuset_hfg = threshold;

	return count;
}

static ssize_t proc_clm_lowload_grp_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", lowload_cpuset_lbg);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_lowload_grp_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	lowload_cpuset_lbg = threshold;
	lowload_cpuset_bg = threshold;
	lowload_cpuset_hbg = threshold;
	lowload_cpuset_hfg = threshold;

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations proc_clm_enable_operations = {
	.read = proc_clm_enable_read,
	.write = proc_clm_enable_write,
};

static const struct file_operations proc_fg_freqs_threshold_operations = {
	.read = proc_fg_freqs_threshold_read,
	.write = proc_fg_freqs_threshold_write,
};

static const struct file_operations proc_clm_highload_all_operations = {
	.read = proc_clm_highload_all_read,
	.write = proc_clm_highload_all_write,
};

static const struct file_operations proc_clm_highload_grp_operations = {
	.read = proc_clm_highload_grp_read,
	.write = proc_clm_highload_grp_write,
};

static const struct file_operations proc_clm_lowload_grp_operations = {
	.read = proc_clm_lowload_grp_read,
	.write = proc_clm_lowload_grp_write,
};
#else
static const struct proc_ops proc_clm_enable_operations = {
	.proc_read = proc_clm_enable_read,
	.proc_write = proc_clm_enable_write,
};

static const struct proc_ops proc_fg_freqs_threshold_operations = {
	.proc_read = proc_fg_freqs_threshold_read,
	.proc_write = proc_fg_freqs_threshold_write,
};

static const struct proc_ops proc_clm_highload_all_operations = {
	.proc_read = proc_clm_highload_all_read,
	.proc_write = proc_clm_highload_all_write,
};

static const struct proc_ops proc_clm_highload_grp_operations = {
	.proc_read = proc_clm_highload_grp_read,
	.proc_write = proc_clm_highload_grp_write,
};

static const struct proc_ops proc_clm_lowload_grp_operations = {
	.proc_read = proc_clm_lowload_grp_read,
	.proc_write = proc_clm_lowload_grp_write,
};
#endif

struct proc_dir_entry *jank_calcload_proc_init(
			struct proc_dir_entry *pde)
{
	struct proc_dir_entry *entry = NULL;


	entry = proc_create("clm_enable", S_IRUGO | S_IWUGO,
				pde, &proc_clm_enable_operations);
	if (!entry) {
		pr_err("create clm_enable fail\n");
		goto err_clm_enable;
	}

	entry = proc_create("fg_freqs_threshold", S_IRUGO | S_IWUGO,
				pde, &proc_fg_freqs_threshold_operations);
	if (!entry) {
		pr_err("create fg_freqs_threshold fail\n");
		goto err_fg_freqs_threshold;
	}

	entry = proc_create("clm_highload_all", S_IRUGO | S_IWUGO,
				pde, &proc_clm_highload_all_operations);
	if (!entry) {
		pr_err("create clm_highload_all fail\n");
		goto err_clm_highload_all;
	}

	entry = proc_create("clm_highload_grp", S_IRUGO | S_IWUGO,
				pde, &proc_clm_highload_grp_operations);
	if (!entry) {
		pr_err("create clm_highload_grp fail\n");
		goto err_clm_highload_grp;
	}

	entry = proc_create("clm_lowload_grp", S_IRUGO | S_IWUGO,
				pde, &proc_clm_lowload_grp_operations);
	if (!entry) {
		pr_err("create clm_lowload_grp fail\n");
		goto err_clm_lowload_grp;
	}

	return entry;

err_clm_lowload_grp:
	remove_proc_entry("clm_highload_grp", pde);

err_clm_highload_grp:
	remove_proc_entry("clm_highload_all", pde);

err_clm_highload_all:
	remove_proc_entry("fg_freqs_threshold", pde);

err_fg_freqs_threshold:
	remove_proc_entry("clm_enable", pde);

err_clm_enable:
	return NULL;
}

void jank_calcload_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("clm_lowload_grp", pde);
	remove_proc_entry("clm_highload_grp", pde);
	remove_proc_entry("clm_highload_all", pde);
	remove_proc_entry("fg_freqs_threshold", pde);
	remove_proc_entry("clm_enable", pde);
}

