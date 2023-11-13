// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/vmalloc.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/im/im.h>
#include <linux/tpp/tpp.h>

/*
 * log output
 * lv == 0 -> verbose info
 * lv == 1 -> some infomation
 * lv == 2 -> warning
 * lv == 3 -> error
 */

static int tpp_log_lv = 1;

// Set tpp_on 1 to open tpp features
static int tpp_on = 0;

static int tpp_task_record_on = 0;

static int tpp_strategy = TPP_UNITY_WORKER_THREAD_MIDDLE_CORE;

static struct tpp_task_monitor_struct tpp_task_monitor = {.buf = NULL};
static int get_tpp_thread_cnt(int flag, int cpu);

static struct tpp_cpu_select_monitor_struct tpp_cpu_select_monitor = {
	.buf = NULL,
	.index = ATOMIC_INIT(-1)
};

// Need to align tpp.h TPP_MINITOR_SIZE

static const char *tpp_task_monitor_case[TPP_TASK_MONITOR_SIZE] = {
	"normal",
	"worker_thread",
};

static const char *tpp_strategy_str[] = {
	"orig",
	"worker_thread_middle_core"
};
#define IM_FLAG(task) (task->im_flag)
#define TPP_FLAG(task) (task->tpp_flag)

void tpp_clear_tpp_flag(struct task_struct *p)
{
	TPP_FLAG(p) = 0;
}

bool tpp_task(struct task_struct *p)
{
	return tpp_on && TPP_FLAG(p);
}

static inline bool tpp_worker_thread(struct task_struct *p)
{
	return (bool)(TPP_FLAG(p) & TPP_UNITY_WORKER_THREAD);
}

static inline bool cpu_available(int cpu)
{
	return cpu_online(cpu) && !cpu_isolated(cpu);
}

static inline int get_tpp_task_id(struct task_struct *p)
{
	return tpp_worker_thread(p) ? TPP_UNITY_WORKER_THREAD_ID : TPP_OTHER_THREAD_ID;
}

static inline bool im_tpp(struct task_struct *p)
{
	// Now only worker thread will be concerned
	return tpp_worker_thread(p);
}

static void  __tpp_task_record(int tpp_task_id, int cpu_orig, int cpu) {
	int index;
	int tag_index = tpp_task_id;
	int offset;

	if (tpp_task_record_on && (!tpp_task_monitor.buf)) {
		tpp_loge("init sample buffer failed, set enabled state to 0\n");
		tpp_task_record_on = 0;
	}
	if (!tpp_task_record_on || tag_index >= TPP_TASK_MONITOR_SIZE) {
		return;
	}

	index = atomic_inc_return(&tpp_task_monitor.index[tag_index]);
	if (index >= TPP_TASK_REPORT_SIZE)
		return;
	offset = tag_index * TPP_TASK_COLUMN;
	tpp_task_monitor.buf->data[index][offset + TPP_TASK_TAG_CPU_ORIG] = cpu_orig;
	tpp_task_monitor.buf->data[index][offset + TPP_TASK_TAG_CPU] = cpu;
	if (cpu == -1)
		cpu = cpu_orig;
	tpp_task_monitor.buf->data[index][offset + TPP_TASK_TAG_WORK_THREAD] = get_tpp_thread_cnt(TPP_UNITY_WORKER_THREAD_ID, cpu);
}

static void tpp_task_record(struct task_struct *p, int cpu_orig, int cpu)
{
	int tpp_task_id = get_tpp_task_id(p);

	if (tpp_task_id > -1 && tpp_task_id < TPP_TASK_MONITOR_SIZE) {
		__tpp_task_record(tpp_task_id, cpu_orig, cpu);
	}
}


static inline void tpp_cpu_select_index_clear(void)
{
	if (!tpp_cpu_select_monitor.buf)
		return;
	atomic_set(&(tpp_cpu_select_monitor.index), -1);
}

static inline void tpp_task_index_clear(void)
{
	int i = 0;
	if (!tpp_task_monitor.buf)
		return;
	for (i = 0; i < TPP_TASK_MONITOR_SIZE; i ++)
		atomic_set(&(tpp_task_monitor.index[i]), -1);
}

static int tpp_task_record_on_set(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) <= 0) {
		tpp_loge("error setting argument. argument should be 1 or 0\n");
		return -EINVAL;
	}

	tpp_task_record_on = !!val;
	if (tpp_task_record_on) {
		if (!tpp_task_monitor.buf)
			tpp_task_monitor.buf = (struct tpp_task_sample*)vzalloc(sizeof(struct tpp_task_sample));
		if (!tpp_cpu_select_monitor.buf)
			tpp_cpu_select_monitor.buf = (struct tpp_cpu_select_sample*)vzalloc(sizeof(struct tpp_cpu_select_sample));
	} else if (!tpp_task_record_on && tpp_task_monitor.buf && tpp_cpu_select_monitor.buf) {
		tpp_task_index_clear();
		tpp_cpu_select_index_clear();
	}
	return 0;
}

static inline int tpp_task_record_on_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", tpp_task_record_on);
}

static atomic_t tpp_thread_cnt[TPP_TASK_MONITOR_SIZE][TPP_NR_CPUS];

static inline int get_tpp_thread_cnt(int flag, int cpu)
{
	return flag != -1 ? atomic_read(&tpp_thread_cnt[flag][cpu]) : 0;
}

static inline int wk_thrd_ops_show(char *buf, const struct kernel_param *kp)
{
	int i;
	unsigned int l = 0;
	for (i = 0; i < TPP_NR_CPUS; ++i) {
		l += snprintf(buf + l, PAGE_SIZE - l,"%d ", get_tpp_thread_cnt(TPP_UNITY_WORKER_THREAD_ID, i));
	}
	return l;
}

static inline void tpp_thread_cnt_inc(int flag, int cpu) {
	atomic_inc(&tpp_thread_cnt[flag][cpu]);
}

static inline void tpp_thread_cnt_dec(int flag, int cpu) {
	atomic_dec(&tpp_thread_cnt[flag][cpu]);
}

static int strategy_names_show(char *buf, const struct kernel_param *kp)
{
	size_t len = 0;
	int i;

	for (i = 0; i < TPP_STRATEGY_SIZE; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d: %s\n", i, tpp_strategy_str[i]);
	return len;
}

static inline void clear_tpp_thread_cnt(void) {
	int cpu, flag;

	for_each_possible_cpu(cpu) {
		for (flag = 0; flag < TPP_TASK_MONITOR_SIZE; flag++) {
			atomic_set(&tpp_thread_cnt[flag][cpu], 0);
		}
	}
}

static int tpp_clear_threads_cnt(char const *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) >= 0 && val != 0) {
		clear_tpp_thread_cnt();
		return 0;
	} else {
		tpp_logi(" not clear\n");
		return -1;
	}
}

static int tpp_cpu_select_index_set(const char *buf, const struct kernel_param *kp)
{
	int val;
	size_t len = sscanf(buf, "%d\n", &val);

	if (tpp_cpu_select_monitor.buf && len > 0 && val < TPP_CPU_SELECT_MAX_REPORT_SIZE && val >= -1) {
		tpp_logi("Set index to %d\n", val);
		atomic_set(&(tpp_cpu_select_monitor.index), val);
		return 0;
	} else {
		tpp_loge("Error setting argument."
				"Argument should be integer between %d and %d.\n",
				TPP_CPU_SELECT_MAX_REPORT_SIZE, -1);
		return -EINVAL;
	}
}

static int tpp_task_index_set(const char *buf, const struct kernel_param *kp)
{
	int val[TPP_TASK_MONITOR_SIZE];
	int i;
	size_t len = 0;

	for (i = 0; i < TPP_TASK_MONITOR_SIZE; i++) {
		len += sscanf(buf+ len, " %d", &val[i]);
	}

	for (i = 0; i < TPP_TASK_MONITOR_SIZE; i++) {
		if (len > 0 && val[i] < TPP_TASK_REPORT_SIZE && val[i] >= -1) {
			tpp_logi("Set index to %d\n", val[i]);
			atomic_set(&(tpp_task_monitor.index[i]), val[i]);
		} else {
			tpp_loge("Error setting argument."
					"Argument should be integer between %d and %d.\n",
					TPP_TASK_REPORT_SIZE, -1);
			return -EINVAL;
		}
	}
	return 0;
}

static int tpp_task_index_show(char *buf, const struct kernel_param *kp)
{
	size_t len = 0;
	int i;
	if (!tpp_task_monitor.buf)
		return 0;
	for (i = 0; i < TPP_TASK_MONITOR_SIZE; i++) {
		len += snprintf(buf + len, PAGE_SIZE, "%d ", atomic_read(&tpp_task_monitor.index[i]));
	}
	len += snprintf(buf + len, PAGE_SIZE, "\n");
	return len;
}

static int tpp_cpu_select_index_show(char *buf, const struct kernel_param *kp)
{
	if (!tpp_cpu_select_monitor.buf)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&tpp_cpu_select_monitor.index));
}

static const char *tpp_task_tags[TPP_TASK_COLUMN] =  {
	"cpu_orig",
	"cpu",
	"worker_thread",
};

static inline void print_tpp_task_tag(struct seq_file *m)
{
	unsigned int i, j;
	for (i = 0; i < TPP_TASK_MONITOR_SIZE; ++i) {
		for (j = 0; j < TPP_TASK_COLUMN; ++j) {
			seq_printf(m, "%s_%s,", tpp_task_monitor_case[i], tpp_task_tags[j]);
		}
	}
	seq_printf(m, "\n");
}

static const char *tpp_cpu_select_tags[TPP_CPU_SELECT_MONITOR_SIZE] = {
	"count",
	"cpu",
};

static inline void print_tpp_cpu_select_tag(struct seq_file *m)
{
	unsigned int i;

	for (i = 0; i < TPP_CPU_SELECT_MONITOR_SIZE; ++i) {
		seq_printf(m, "%s,", tpp_cpu_select_tags[i]);
	}
	seq_printf(m, "\n");
}


static inline void print_tpp_task_data(struct seq_file *m)
{
	unsigned int i, j, k;
	unsigned int report_size[TPP_TASK_MONITOR_SIZE];
	if (!tpp_task_monitor.buf)
		return;
	for (i = 0; i < TPP_TASK_MONITOR_SIZE; ++i) {
		report_size[i] = atomic_read(&(tpp_task_monitor.index[i])) + 1;
		report_size[i] = min(report_size[i], (unsigned int) TPP_TASK_REPORT_SIZE);
	}

	for (i = 0; i < TPP_TASK_REPORT_SIZE; ++i) {
		for (j = 0; j < TPP_TASK_MONITOR_SIZE; j++) {
			for (k = 0; k < TPP_TASK_COLUMN && i < report_size[j]; k++)
				seq_printf(m, "%d,",
						tpp_task_monitor.buf->data[i][TPP_TASK_COLUMN * j + k]);
			for (k = 0; k < TPP_TASK_COLUMN && i >= report_size[j]; k++)
				seq_printf(m, ",");
		}
		seq_printf(m, "\n");
	}
}
static inline void print_tpp_cpu_select_data(struct seq_file *m)
{
	unsigned int i, j;
	unsigned int report_size;

	if (!tpp_cpu_select_monitor.buf)
		return;

	report_size = atomic_read(&tpp_cpu_select_monitor.index) + 1;
	report_size = min(report_size, (unsigned int) TPP_CPU_SELECT_MAX_REPORT_SIZE);

	for (i = 0; i < report_size; ++i) {
		for (j = 0; j < TPP_CPU_SELECT_MONITOR_SIZE; ++j)
			seq_printf(m, "%d," , tpp_cpu_select_monitor.buf->data[i][j]);
		seq_printf(m, "\n");
	}
}

static int tpp_cpu_select_show_report(struct seq_file *m, void *v)
{
	if (tpp_task_record_on) {
		print_tpp_cpu_select_tag(m);
		print_tpp_cpu_select_data(m);
	} else {
		seq_printf(m, "Not recorded\n");
	}
	return 0;
}

static int tpp_tagged_list_show(struct seq_file *m, void *v)
{
	struct task_struct *p, *t;

	for_each_process_thread(p, t) {
		if (im_tpp(t)) {
			seq_printf(m, "pid: %u, tgid: %u im_flag: %u tpp_flag: %u\n",
					t->pid, t->tgid, IM_FLAG(t), TPP_FLAG(t));
		}
	}
	return 0;
}

static int tpp_task_show_report(struct seq_file *m, void *v)
{
	if (tpp_task_record_on) {
		print_tpp_task_tag(m);
		print_tpp_task_data(m);
	} else {
		seq_printf(m, "Not recorded\n");
	}
	return 0;
}

static int tpp_task_open_report(struct inode *inode, struct file *file)
{
	return single_open(file, tpp_task_show_report, NULL);
}


static int tpp_cpu_select_open_report(struct inode *inode, struct file *file)
{
	return single_open(file, tpp_cpu_select_show_report, NULL);
}

static int tpp_tagged_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, tpp_tagged_list_show, NULL);
}

static const struct file_operations tpp_cpu_select_report_proc_fops = {
	.open = tpp_cpu_select_open_report,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations tpp_tagged_list_proc_fops = {
	.open = tpp_tagged_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations tpp_task_report_proc_fops = {
	.open = tpp_task_open_report,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static inline void tpp_cpu_select_record(int tpp_task_id, int cpu)
{
	int index;
	if (!tpp_task_record_on || !tpp_cpu_select_monitor.buf)
		return;
	index = atomic_inc_return(&tpp_cpu_select_monitor.index);
	if (index < TPP_CPU_SELECT_MAX_REPORT_SIZE) {
		tpp_cpu_select_monitor.buf->data[index][TPP_CPU_SELECT_TPP_TASK_ID] = tpp_task_id;
		tpp_cpu_select_monitor.buf->data[index][TPP_CPU_SELECT_CPU] = cpu;
	}
}

static inline void tpp_thread_enqueue(int cpu, struct task_struct *p)
{
	int tpp_task_id = get_tpp_task_id(p);

	if (tpp_task_id >= TPP_TASK_MONITOR_SIZE || tpp_task_id < 0)
		return;
	else {
		tpp_thread_cnt_inc(tpp_task_id, cpu);
		tpp_cpu_select_record(tpp_task_id, cpu);
	}
}


inline void tpp_enqueue(int cpu, struct task_struct *p)
{
	if (tpp_on && im_tpp(p)) {
		tpp_thread_enqueue(cpu, p);
		TPP_FLAG(p) |= TPP_CFS_RQ;
	}
}

static inline void tpp_thread_dequeue(int cpu, struct task_struct *p)
{
	int tpp_task_id = get_tpp_task_id(p);

	if (tpp_task_id >= TPP_TASK_MONITOR_SIZE || tpp_task_id < 0)
		return;
	else {
		tpp_thread_cnt_dec(tpp_task_id, cpu);
	}
}

static int unity_tgid = -1;
inline void tpp_tagging(struct task_struct *p)
{
	if (im_unity_main(p)) {
		tpp_logv("unity_main tgid %d, pid %d", p->tgid, p->pid);
		unity_tgid = p->tgid;
	}
	/* Let only the worker threads which belong to Unity will be tagged*/
	if (p->tgid == unity_tgid) {
		size_t len = 0;

		if (TPP_FLAG(p) & TPP_UNITY_WORKER_THREAD) {
			IM_FLAG(p) |= IM_UNITY_WORKER_THREAD;
			return;
		}

		len = strlen(p->comm);
		if ((len >= 6 && !strncmp(p->comm, "Worker", 6)) ||
				(len >= 10 && !strncmp(p->comm, "Job.Worker", 10))) {
			IM_FLAG(p) |= IM_UNITY_WORKER_THREAD;
			TPP_FLAG(p) |= TPP_UNITY_WORKER_THREAD;
		}
	}
}

inline void tpp_dequeue(int cpu, struct task_struct *p)
{
	if (TPP_FLAG(p) & TPP_CFS_RQ) {
		tpp_thread_dequeue(cpu, p);
		TPP_FLAG(p) &= ~(TPP_CFS_RQ);
	}
}

static unsigned int tpp_cluster_size = 0;

struct tpp_cpu_cluster {
	unsigned int first;
	unsigned int last;
	unsigned int max_freq;
	unsigned int amount;
};

static struct tpp_cpu_cluster tpp_cpu_clusters[TPP_MAX_CLUSTER_SIZE];

int tpp_cluster_amount(void)
{
	return tpp_cluster_size;
}

int tpp_cpu_amount_of_cluster(int cluster)
{
	if (cluster >= 0 && cluster < tpp_cluster_size)
		return tpp_cpu_clusters[cluster].amount;
	return 0;
}

int tpp_max_freq_of_cluster(int cluster)
{
	if (cluster >= 0 && cluster < tpp_cluster_size)
		return tpp_cpu_clusters[cluster].max_freq;
	return 0;
}

int tpp_first_cpu_of_cluster(int cluster)
{
	if (cluster >= 0 && cluster < tpp_cluster_size)
		return tpp_cpu_clusters[cluster].first;
	return 0;
}

static inline int worker_thread_middle_core(struct task_struct *p, int cpu_orig)
{
	int start_cpu;
	int cpu;
	int max_cnt_on_rq = -1;
	int candicate_cpu = -1;
	
	if (TPP_BIG_CLUSTER != tpp_cluster_size - 1)
		return cpu_orig;
	start_cpu = tpp_cpu_clusters[TPP_MIDDLE_CLUSTER].first;
	if (!tpp_worker_thread(p))
		return cpu_orig;
	for (cpu = start_cpu; cpu <= tpp_cpu_clusters[TPP_BIG_CLUSTER].last; cpu ++) {
		if (cpu_available(cpu)) {
			int cnt_on_rq = get_tpp_thread_cnt(TPP_UNITY_WORKER_THREAD_ID, cpu);

			if (max_cnt_on_rq < cnt_on_rq) {
				candicate_cpu = cpu;
				max_cnt_on_rq = cnt_on_rq;
			}
		}
	}

	return candicate_cpu;
}
void tpp_find_cpu(int *cpu_orig, struct task_struct *p)
{
	int cpu = *cpu_orig;
	if (!tpp_on)
		return;

	switch (tpp_strategy) {
		case TPP_UNITY_WORKER_THREAD_MIDDLE_CORE:
			cpu = worker_thread_middle_core(p, cpu);
			break;
		case TPP_STRATEGY_ORIG:
		default:
			cpu = *cpu_orig;
	}
	if (tpp_task_record_on)
		tpp_task_record(p, *cpu_orig, cpu);
	if (cpu > -1 && cpu < TPP_NR_CPUS)
		*cpu_orig = cpu;
}

static struct kernel_param_ops tpp_task_monitor_index_ops = {
	.set = tpp_task_index_set,
	.get = tpp_task_index_show,
};

static struct kernel_param_ops tpp_cpu_select_monitor_index_ops = {
	.set = tpp_cpu_select_index_set,
	.get = tpp_cpu_select_index_show,
};

static struct kernel_param_ops clear_ops = {
	.set = tpp_clear_threads_cnt,
};

static struct kernel_param_ops strategy_names_ops = {
	.get = strategy_names_show,
};

static struct kernel_param_ops tpp_task_record_on_ops = {
	.set = tpp_task_record_on_set,
	.get = tpp_task_record_on_show,
};

static struct kernel_param_ops wk_thrd_cnt_ops = {
	.get = wk_thrd_ops_show,
};

module_param_named(log_lv, tpp_log_lv, int, 0664);
module_param_named(strategy, tpp_strategy, int, 0664);

module_param_named(tpp_on, tpp_on, int, 0664);
module_param_cb(tpp_task_record_on, &tpp_task_record_on_ops, NULL, 0664);
module_param_cb(tpp_task_monitor_index, &tpp_task_monitor_index_ops,
		NULL, 0664);
module_param_cb(tpp_cpu_select_monitor_index, &tpp_cpu_select_monitor_index_ops,
		NULL, 0664);
module_param_cb(strategy_names, &strategy_names_ops, NULL, 0664);
module_param_cb(wk_thrd_cnt, &wk_thrd_cnt_ops, NULL, 0664);

module_param_cb(clear_threads_cnt, &clear_ops, NULL, 0664);

static unsigned int save_cpu_cluster_and_ret_last_cpu(struct cpufreq_policy * policy)
{
	struct cpumask *cpumask_ptr;
	unsigned int max_freq;
	unsigned int cluster_id = tpp_cluster_size;
	unsigned int first, last;

	if (tpp_cluster_size >= TPP_MAX_CLUSTER_SIZE) {
		tpp_loge("tpp cluster size overflow");
		tpp_on = 0;
		return NR_CPUS;
	}

	cpumask_ptr = policy->related_cpus;
	if (cpumask_ptr == NULL) {
		tpp_loge("Can't get cpumask_ptr");
		tpp_on = 0;
		return NR_CPUS;
	}

	first = cpumask_first(cpumask_ptr);
	last = cpumask_last(cpumask_ptr);
	max_freq = policy->cpuinfo.max_freq;

	tpp_cpu_clusters[cluster_id].first = first;
	tpp_cpu_clusters[cluster_id].last = last;
	tpp_cpu_clusters[cluster_id].max_freq = max_freq;
	tpp_cpu_clusters[cluster_id].amount = last - first + 1;
	tpp_cluster_size += 1;
	return last;
}

static void record_system_cpu_state(void)
{
	struct cpufreq_policy * policy;
	unsigned int cpu = 0;
	while (cpu < NR_CPUS) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (policy == NULL) {
			tpp_loge("Can't get cpu policy in cpu %u", cpu);
			tpp_loge("NR_CPUS: %u", NR_CPUS);
			tpp_on = 0;
			return;
		}
		cpu = save_cpu_cluster_and_ret_last_cpu(policy);
		cpu += 1;
	}
}

#define register_hooks(_register, _cb, ret, tag) \
	do { \
		int ret = _register(_cb, NULL); \
		if (!ret) { \
			pr_info("%s: %pF register succesful\n",tag, _cb); \
		} else { \
			pr_warn("%s: %pF register failed, ret %d\n",tag, _cb, ret); \
			goto failed; \
		} \
	} while (0);

static int __init tpp_init(void)
{
	tpp_logi("INIT TPP\n");
	proc_create("tpp_task_report", S_IFREG | 0444, NULL,
			&tpp_task_report_proc_fops);
	proc_create("tpp_tagged_list", S_IFREG | 0444, NULL,
			&tpp_tagged_list_proc_fops);
	proc_create("tpp_cpu_select_report", S_IFREG | 0444, NULL,
			&tpp_cpu_select_report_proc_fops);

	record_system_cpu_state();
	return 0;
}

static void __exit tpp_exit(void)
{
	if (tpp_task_monitor.buf) {
		vfree(tpp_task_monitor.buf);
		tpp_task_monitor.buf = NULL;
	}
	if (tpp_cpu_select_monitor.buf) {
		vfree(tpp_cpu_select_monitor.buf);
		tpp_cpu_select_monitor.buf = NULL;
	}
	tpp_logi("EXIT TPP\n");
}

pure_initcall(tpp_init);
module_exit(tpp_exit);

MODULE_AUTHOR("huayi.ho");
MODULE_DESCRIPTION("tpp");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
