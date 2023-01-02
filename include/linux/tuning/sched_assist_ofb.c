#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/compat.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_OPLUS_FEATURE_IM
#include <linux/im/im.h>
#endif

#include "sched_assist_ofb.h"
#include "frame_boost_group.h"
#include "frame_info.h"

#define ofb_debug(fmt, ...) \
		printk_deferred(KERN_INFO "[frame_boost][%s]"fmt, __func__, ##__VA_ARGS__)

static struct proc_dir_entry *frame_boost_proc;
static atomic_t fbg_pid = ATOMIC_INIT(-1);
static atomic_t fbg_tid = ATOMIC_INIT(-1);
extern atomic_t start_frame;
extern int isHighFps;
extern struct frame_boost_group default_frame_boost_group;
extern int stune_boost;
extern unsigned int timeout_load;
#define DEBUG 0

static void ctrl_set_fbg(int pid, int tid, int hwtid1, int hwtid2)
{
	if (pid != atomic_read(&fbg_pid))
		atomic_set(&fbg_pid, pid);

	if (tid != atomic_read(&fbg_tid))
		atomic_set(&fbg_tid, tid);

	update_frame_thread(pid, tid);
	//update_hwui_tasks(hwtid1, hwtid2);
	set_frame_timestamp(FRAME_END);
	if (DEBUG) {
		unsigned long irqflag;
		struct task_struct *p = NULL;
		struct list_head *pos, *n;
		struct frame_boost_group *grp = &default_frame_boost_group;

		raw_spin_lock_irqsave(&grp->lock, irqflag);
		list_for_each_safe(pos, n, &grp->tasks) {
			p = list_entry(pos, struct task_struct, fbg_list);
			ofb_debug("grp_thread pid:%d comm:%s depth:%d", p->pid, p->comm, p->fbg_depth);
		}
		raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	}
}

static void ctrl_frame_state(int pid, bool is_enter)
{
	if ((pid != current->pid) || (pid != atomic_read(&fbg_pid)))
		return;

	update_frame_state(pid, is_enter);
}

unsigned int max_rate;
extern raw_spinlock_t fbg_lock;
extern struct frame_info global_frame_info;
char sf[] = "surfaceflinger";
char lc[] = "ndroid.launcher";
static void crtl_update_refresh_rate(int pid, int64_t vsyncNs)
{
	unsigned long flags;
	unsigned int frame_rate =  NSEC_PER_SEC / (unsigned int)(vsyncNs);
	bool is_sf = false;
	struct task_struct *leader = NULL;

#ifdef CONFIG_OPLUS_FEATURE_IM
	if (im_sf(current) || strnstr(current->comm, sf, strlen(current->comm))) {
		is_sf = true;
	}
#endif
	leader = current->group_leader;
	if (pid == atomic_read(&fbg_pid) && leader && strnstr(leader->comm, lc, strlen(leader->comm))) {
		stune_boost = 30;
	}

	if (!is_sf && frame_rate == global_frame_info.frame_qos)
		return;

	raw_spin_lock_irqsave(&fbg_lock, flags);
	if (is_sf) {
		max_rate = frame_rate;
	} else if (pid != atomic_read(&fbg_pid) || frame_rate > max_rate) {
		raw_spin_unlock_irqrestore(&fbg_lock, flags);
		return;
	}

	set_frame_rate(frame_rate);
	sched_set_group_window_size(vsyncNs);
	raw_spin_unlock_irqrestore(&fbg_lock, flags);
}

static void ctrl_frame_obt(int pid)
{
	if (atomic_read(&start_frame) == 0)
		return;

	if ((pid != current->pid) || pid != atomic_read(&fbg_pid))
		return;

	if (!isHighFps)
		return;

	set_frame_min_util(600, true);
}

static void ctrl_set_render(int pid, int tid)
{
	if ((pid != current->pid) || pid != atomic_read(&fbg_pid))
		return;

	update_frame_thread(pid, tid);
}

void ctrl_set_hwuitasks(int pid, int hwtid1, int hwtid2)
{
	if (pid != atomic_read(&fbg_pid))
		return;

	update_hwui_tasks(hwtid1, hwtid2);
}
static void ctrl_set_timeout(int pid)
{
	unsigned long curr_load = 0;
	struct frame_info *frame_info = NULL;
	struct frame_boost_group *grp = NULL;

	if (atomic_read(&start_frame) == 0)
		return;

	if ((pid != current->pid) || pid != atomic_read(&fbg_pid))
		return;

	if (!isHighFps)
		return;

	grp = frame_grp();
	rcu_read_lock();
	frame_info = fbg_frame_info(grp);
	rcu_read_unlock();

	if (!frame_info)
		return;

	curr_load = calc_frame_load(frame_info, false);
	if (curr_load > timeout_load) {
		set_frame_min_util(500, true);
	}
}

static long ofb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -ENOTTY;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_debug("invalid address");
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_SET_FPS:
		crtl_update_refresh_rate(data.pid, data.vsyncNs);
		break;
	case CMD_ID_BOOST_HIT:
		//if (data.stage == BOOST_MOVE_FG) {
		//	ctrl_set_fbg(data.pid, data.tid, data.hwtid1, data.hwtid2);
		//}
		if (data.stage == BOOST_FRAME_START) {
			ctrl_frame_state(data.pid, true);
		}
		if (data.stage == BOOST_OBTAIN_VIEW) {
			ctrl_frame_obt(data.pid);
		}
		//if (data.stage == BOOST_SET_HWUITASK) {
		//	ctrl_set_hwuitasks(data.pid, data.hwtid1, data.hwtid2);
		//}
		if (data.stage == BOOST_SET_RENDER_THREAD) {
			ctrl_set_render(data.pid, data.tid);
		}
		if (data.stage == BOOST_FRAME_TIMEOUT) {
			ctrl_set_timeout(data.pid);
		}
		if (data.stage == FRAME_BOOST_END) {
			ctrl_frame_state(data.pid, false);
		}
		break;
	case CMD_ID_END_FRAME:
		//ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid);
		break;
	case CMD_ID_SF_FRAME_MISSED:
		//ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid);
		break;
	case CMD_ID_SF_COMPOSE_HINT:
		//ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid);
		break;
	case CMD_ID_IS_HWUI_RT:
		//ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid);
		break;
	case CMD_ID_SET_TASK_TAGGING:
		//ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static long sys_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -ENOTTY;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_debug("invalid address");
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_BOOST_HIT:
		if (data.stage == BOOST_MOVE_FG) {
			ctrl_set_fbg(data.pid, data.tid, data.hwtid1, data.hwtid2);
		}

		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ofb_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}

static long sys_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return sys_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}
#endif

static int ofb_ctrl_open(struct inode *inode, struct file *file)
{
   return 0;
}

static int ofb_ctrl_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations ofb_ctrl_fops = {
	.owner        = THIS_MODULE,
	.unlocked_ioctl    = ofb_ioctl,
	.open		 = ofb_ctrl_open,
	.release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ofb_ctrl_compat_ioctl,
#endif
};

static const struct file_operations sys_ctrl_fops = {
	.owner        = THIS_MODULE,
	.unlocked_ioctl    = sys_ioctl,
	.open		 = ofb_ctrl_open,
	.release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = sys_ctrl_compat_ioctl,
#endif
};

static int __init frame_boost_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;
	frame_boost_proc = proc_mkdir(FRAMEBOOST_PROC_NODE, NULL);

	pentry = proc_create("ctrl", S_IRWXUGO, frame_boost_proc, &ofb_ctrl_fops);
	if (!pentry) {
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("sys_ctrl", (S_IRWXU|S_IRWXG), frame_boost_proc, &sys_ctrl_fops);
	if (!pentry) {
		goto ERROR_INIT_VERSION;
	}
	return ret;

ERROR_INIT_VERSION:
	remove_proc_entry(FRAMEBOOST_PROC_NODE, NULL);
	return -ENOENT;

}

module_init(frame_boost_init);

MODULE_DESCRIPTION("oplus frame boost");
MODULE_LICENSE("GPL v2");
