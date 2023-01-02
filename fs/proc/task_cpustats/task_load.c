#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <../fs/proc/internal.h>
#include <../kernel/sched/sched.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/task_load.h>
#include <linux/sched/signal.h>
#include <linux/sched/cpufreq.h>
#include <linux/timekeeping.h>
#include <linux/arch_topology.h>

#define MAX_PID_NUM 3

static unsigned int monitor_status = 0;
static u64 all_running_time = 0;
static u64 realtime_base = 0;
static u64 normalize_all_running_time = 0;
static int target_pid_num = -1;
static u64 target_pid[MAX_PID_NUM] = {0};

#ifdef CONFIG_SCHED_WALT
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	return (delta * rq->wrq.task_exec_scale) >> 10;
}
#else
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	return delta;
}
#endif /* CONFIG_SCHED_WALT */

void account_normalize_runtime(struct task_struct *p, u64 delta, struct rq *rq)
{
	u64 normalize_delta = 0;

	if (!monitor_status)
		return;

	normalize_delta = scale_exec_time(delta, rq);
	p->signal->sum_runtime += delta;
	all_running_time += delta;
	normalize_all_running_time += normalize_delta;
}

static int proc_target_process_show(struct seq_file *m, void *v)
{
	struct task_struct *p;
	if (!monitor_status)
		return -EFAULT;

	if (target_pid_num == -1 || target_pid_num >=MAX_PID_NUM) {
		task_load_err("get target pid fail\n");
		goto ERROR_INFO;
	}

	p = find_get_task_by_vpid(target_pid[target_pid_num]);

	if (!p) {
		task_load_err("get target task fail\n");
		goto ERROR_INFO;
	}

	seq_printf(m, "%llu\n", p->signal->sum_runtime>>20);
	put_task_struct(p);

	return 0;

ERROR_INFO:
	seq_printf(m, "0\n");
	return 0;
}

static int proc_target_process_open(struct inode* inode, struct file *file)
{
	return single_open(file, proc_target_process_show,inode);
}

static ssize_t proc_target_process_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, target_process_num;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1) {
		count = sizeof(buffer) - 1;
	}

	if (copy_from_user(buffer, buf,count)) {
		return -EFAULT;
	}

	err = kstrtouint(strstrip(buffer), 0, &target_process_num);
	if(err) {
		return err;
	}

	target_pid_num = target_process_num;

	return count;
}


static const struct file_operations proc_target_process_operations = {
	.open		= proc_target_process_open,
	.write		= proc_target_process_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_running_time_show(struct seq_file *m, void *v)
{
	if (!monitor_status)
		return -EFAULT;

	seq_printf(m, "%llu\n", all_running_time>>20);

	return 0;
}

static int proc_running_time_open(struct inode* inode, struct file *file)
{
	return single_open(file, proc_running_time_show,inode);
}

static const struct file_operations proc_running_time_operations = {
	.open		= proc_running_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_normalize_running_time_show(struct seq_file *m, void *v)
{
	if (!monitor_status)
		return -EFAULT;

	seq_printf(m, "%llu\n", normalize_all_running_time>>20);

	return 0;
}

static int proc_normalize_running_time_open(struct inode* inode, struct file *file)
{
	return single_open(file, proc_normalize_running_time_show,inode);
}

static const struct file_operations proc_normalize_running_time_operations = {
	.open		= proc_normalize_running_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_real_time_show(struct seq_file *m, void *v)
{
	struct timespec ts;
	u64 wall_time;

	if (!monitor_status)
		return -EFAULT;

	getnstimeofday(&ts);;
	wall_time = ts.tv_sec * 1e6 + ts.tv_nsec / 1000 - realtime_base;
	seq_printf(m, "%llu\n", wall_time>>10);

	return 0;
}

static int proc_real_time_open(struct inode* inode, struct file *file)
{
	return single_open(file, proc_real_time_show, inode);
}


static const struct file_operations proc_real_time_operations = {
	.open		= proc_real_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_normalize_real_time_show(struct seq_file *m, void *v)
{
	int i;
	struct timespec ts;
	u64 wall_time = 0;
	u64 normalize_wall_time = 0;

	if (!monitor_status)
		return -EFAULT;

	getnstimeofday(&ts);
	wall_time = ts.tv_sec * 1e6 + ts.tv_nsec / 1000 - realtime_base;

	for_each_possible_cpu(i) {
		normalize_wall_time += wall_time * (u64)topology_get_cpu_scale(i);
	}

	seq_printf(m, "%llu\n", normalize_wall_time>>20);

	return 0;
}

static int proc_normalize_real_time_open(struct inode* inode, struct file *file)
{
	return single_open(file, proc_normalize_real_time_show, inode);
}


static const struct file_operations proc_normalize_real_time_operations = {
	.open		= proc_normalize_real_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t proc_monitor_status_write(struct file *file,const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	int err, enable;
	struct timespec ts;
	struct task_struct *g;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1) {
		count = sizeof(buffer) - 1;
	}

	if (copy_from_user(buffer, buf,count)) {
		return -EFAULT;
	}

	err = kstrtouint(strstrip(buffer), 0, &enable);
	if(err) {
		return err;
	}

	getnstimeofday(&ts);
	monitor_status = enable;
	realtime_base = ts.tv_sec * 1e6 + ts.tv_nsec / 1000;

	if(!monitor_status) {
		for_each_process(g){
			g->signal->sum_runtime = 0;
		}

		all_running_time = 0;
		normalize_all_running_time = 0;
	}

	return count;
}

static ssize_t proc_monitor_status_read(struct file* file, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];

	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", monitor_status);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}


static const struct file_operations proc_monitor_status_operations = {
	.write		= proc_monitor_status_write,
	.read		= proc_monitor_status_read,
};

void get_target_process(struct task_struct *task)
{
	struct task_struct *grp;

	if (strstr(task->comm, "om.oplus.camera")) {
		target_pid[camera] = task->pid;
		return;
	}

	grp = task->group_leader;

	if (strstr(grp->comm, "cameraserver")) {
		target_pid[cameraserver] = grp->pid;
		return;
        }
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if (strstr(grp->comm, "provider@2.4-se")) {
		target_pid[cameraprovider] = grp->pid;
		return;
	}
#else
	if (strstr(grp->comm, "camerahalserver")) {
		target_pid[cameraprovider] = grp->pid;
		return;
	}
#endif
}

#define RUNTIME_DIR "task_info"
#define TASK_LOAD_NODE "task_load_info"
#define TASK_LOAD_EXIST_NODE "task_info/task_load_info"
static struct proc_dir_entry *task_load_info = NULL;

static int __init load_info_init(void)
{
	struct proc_dir_entry *proc_entry, *task_runtime;

	task_load_info = proc_mkdir(RUNTIME_DIR, NULL);
	if(!task_load_info) {
		task_runtime = proc_mkdir(TASK_LOAD_EXIST_NODE, NULL);
	}
	else {
		task_runtime = proc_mkdir(TASK_LOAD_NODE, task_load_info);
	}

	if(!task_runtime) {
		task_load_err("create task_load_info fail\n");
		goto ERROR_INIT_VERSION;
	}

	proc_entry = proc_create("monitor_status", S_IRUGO | S_IWUGO, task_runtime, &proc_monitor_status_operations);
	if(!proc_entry) {
		task_load_err("create monitor_status fail\n");
		goto ERROR_INIT_VERSION;
	}

	proc_entry = proc_create("running_time", S_IRUGO | S_IWUGO, task_runtime, &proc_running_time_operations);
	if(!proc_entry) {
		task_load_err("create running_time fail\n");
		goto ERROR_INIT_VERSION;
	}

	proc_entry = proc_create("normalize_running_time", S_IRUGO | S_IWUGO, task_runtime, &proc_normalize_running_time_operations);
	if(!proc_entry) {
		task_load_err("create normalize_running_time fail\n");
		goto ERROR_INIT_VERSION;
	}


	proc_entry = proc_create("real_time", S_IRUGO | S_IWUGO, task_runtime, &proc_real_time_operations);
	if(!proc_entry) {
		task_load_err("create real_time fail\n");
		goto ERROR_INIT_VERSION;
	}

	proc_entry = proc_create("normalize_real_time", S_IRUGO | S_IWUGO, task_runtime, &proc_normalize_real_time_operations);
	if(!proc_entry) {
		task_load_err("create normalize_real_time fail\n");
		goto ERROR_INIT_VERSION;
	}

	proc_entry = proc_create("target_process", S_IRUGO | S_IWUGO, task_runtime, &proc_target_process_operations);
	if(!proc_entry) {
		task_load_err("create target_process fail\n");
		goto ERROR_INIT_VERSION;
	}

	return 0;

ERROR_INIT_VERSION:
	remove_proc_entry(TASK_LOAD_NODE, NULL);
	return -ENOENT;
}
module_init(load_info_init);

MODULE_DESCRIPTION("task_load");
MODULE_LICENSE("GPL v2");
