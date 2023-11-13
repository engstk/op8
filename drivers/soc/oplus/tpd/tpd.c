#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/tpd/tpd.h>
#include <linux/cred.h>
//#include <linux/oem/im.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/healthinfo/fg.h>

/*
 * Task Placement Decision
 *
 * two main rules as below:
 *  1. trigger tpd with tgid and thread's name which will be limited
 *     through online config, framework can tag threads and limit cpu placement
 *     of tagged threads that are also foreground task.
 *     tpd_cmds
 *  2. trigger tpd with tgid and thread_id
 *     control the placement limitation by itself, set tpd_ctl = 1 will limit
 *     cpu placement of tagged threads, but need release by itself to set tpd_enable = 0,
 *     tpd_ctl = 0 and task->tpd = 0
 *     tpd_id
 *  3. control the placement limitation by itself, set tpd_ctl = 1 will limit
 *     cpu placement of tagged threads, but need release by itself to set tpd_enable = 0,
 *     tpd_ctl = 0 and task->tpd = 0
 *     tpd_dynamic
 */

struct tgid_list_entry {
	int pid;
	struct list_head node;
};

struct monitor_gp {
	int decision; /* cpu affinity  */
	spinlock_t tgid_list_lock; /* used to check dynamic tpd task */
	struct list_head tgid_head;/* used to check dynamic tpd task */
	spinlock_t miss_list_lock; /* used to check if need re-tag or not */
	unsigned int miss_list_tgid[MAX_MISS_LIST];/* used to check if need re-tag or not */
	char miss_list[MAX_MISS_LIST][TASK_COMM_LEN];/* used to check if need re-tag or not */
	int not_yet;
	int cur_idx;
};

#define MAX_CLUSTERS 3
static int cluster_total;
struct tpd_cpuinfo {
	int first_cpu;
	cpumask_t related_cpus;
};
static struct tpd_cpuinfo clusters[MAX_CLUSTERS];

/* monitor group for dynamic tpd threads */
static struct monitor_gp mgp[TPD_GROUP_MAX];

static atomic_t tpd_enable_rc = ATOMIC_INIT(0);
static int tpd_enable_rc_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&tpd_enable_rc));
}

static struct kernel_param_ops tpd_enable_rc_ops = {
	.get = tpd_enable_rc_show,
};

module_param_cb(tpd_en_rc, &tpd_enable_rc_ops, NULL, 0664);

static int tpd_log_lv = 2;
module_param_named(log_lv, tpd_log_lv, int, 0664);

static atomic64_t tpd_ctl = ATOMIC64_INIT(0); /* used to ignore fg task checking */

static bool should_update_tpd_enable(int enable) {

	bool ret = true;
	int refcnt = 0;

	if (enable) {
		if (atomic_inc_return(&tpd_enable_rc) > 1)
			ret = false;
	} else {
		refcnt = atomic_read(&tpd_enable_rc);
		if (refcnt < 1) {
			ret = false;
			goto out;
		}

		/* tpd_cmds will always increase but not minus from TpdManager,
		   so we ignore the check with tpd_ctl */
		/* tpd_enable ref count must greater or equal tpd_ctl */
		/*if (refcnt - 1 < atomic64_read(&tpd_ctl)) {
			ret = false;
			goto out;
		}*/

		if (atomic_dec_return(&tpd_enable_rc) > 0) {
			tpd_loge("tpd cannot disable");
			ret = false;
		}
	}

out:
	tpd_logv("should_update_tpd_enable? %d", ret);

	return ret;
}

static int tpd_enable = 0;
static int tpd_enable_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	if (should_update_tpd_enable(val))
		tpd_enable = val;

	return 0;
}

static int tpd_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", tpd_enable);
}

static struct kernel_param_ops tpd_enable_ops = {
	.set = tpd_enable_store,
	.get = tpd_enable_show,
};

module_param_cb(tpd_enable, &tpd_enable_ops, NULL, 0664);

bool is_tpd_enable(void)
{
	return tpd_enable;
}

static int st_tpd_enable = 1; // system thread affinity from im tagging
static int st_tpd_enable_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	st_tpd_enable = val;

	return 0;
}

static int st_tpd_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", st_tpd_enable);
}

static struct kernel_param_ops st_tpd_enable_ops = {
	.set = st_tpd_enable_store,
	.get = st_tpd_enable_show,
};

module_param_cb(st_tpd_enable, &st_tpd_enable_ops, NULL, 0664);

bool is_st_tpd_enable(void)
{
	return st_tpd_enable;
}

static int miss_list_show(char *buf, const struct kernel_param *kp)
{
	int i, j;
	int cnt = 0;

	for (j = TPD_GROUP_MEDIAPROVIDER; j < TPD_GROUP_MAX; ++j) {
		spin_lock(&mgp[j].miss_list_lock);
		if (mgp[j].not_yet > 0) {
			for (i = 0; i < MAX_MISS_LIST; ++i) {
				if(strlen(mgp[j].miss_list[i]) > 0) {
					cnt +=	snprintf(buf + cnt, PAGE_SIZE - cnt, "%s %d\n", mgp[j].miss_list[i], mgp[j].miss_list_tgid[i]);
				}
			}
		}
		spin_unlock(&mgp[j].miss_list_lock);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	}
	return cnt;
}

static struct kernel_param_ops miss_list_ops = {
	.get = miss_list_show,
};

module_param_cb(miss_list, &miss_list_ops, NULL, 0664);

bool is_dynamic_tpd_task(struct task_struct *tsk)
{
	int gid, len = 0, i;
	bool ret = false;
	struct monitor_gp *group;
	struct tgid_list_entry *p;
	struct task_struct *leader;

	/* this is for all tpd tasks reset(dynamic/not dynamic tpd tasks) */
	if (!tpd_enable) {
		tsk->tpd = 0;
		tsk->dtpdg = -1;
		tsk->dtpd = 0;
		return ret;
	}

	/* return if current task is DEAD(ing) or ZOMBIE(ing) */
	if (current->exit_state)
		return ret;

	leader = find_task_by_vpid(tsk->tgid);
	if (leader == NULL)
		return ret;

	gid = leader->dtpdg;
	/* not dynamic tpd task will return */
	if (gid < TPD_GROUP_MEDIAPROVIDER || gid >= TPD_GROUP_MAX)
		return ret;

	group = &mgp[gid];


	switch (gid) {
	case TPD_GROUP_MEDIAPROVIDER:

		spin_lock(&group->tgid_list_lock);

		/* no dynamic tpd task enable of this dynamic tpd group id */
		if (list_empty(&group->tgid_head)) {
			if (tsk->dtpd) {
				tsk->dtpd = 0;
				tsk->tpd = 0;
			}
			spin_unlock(&group->tgid_list_lock);
			return ret;
		}

		list_for_each_entry(p, &group->tgid_head, node) {
			if (leader->pid != p->pid)
				continue;

			/* parent of thread was dynamic tpd task */
			/* dynamic tpd task has already tagged */
			if (tsk->dtpd && tsk->tpd) {
				ret = true;
				break;
			}

			/* start tagging process */
#ifdef CONFIG_IM
			/*binder thread of media provider */
			if (im_binder(tsk)) {
				tsk->dtpd = 1; /* dynamic tpd */
				tsk->tpd = group->decision;
				ret = true;
				break;
			}
#endif

#ifdef CONFIG_ONEPLUS_FG_OPT
			/* fuse related thread of media provider */
			if (tsk->fuse_boost) {
				tsk->dtpd = 1; /* dynamic tpd */
				tsk->tpd = group->decision;
				ret = true;
				break;
			}
#endif
			/* re-tag missed thread, only one name with  one thread */
			spin_lock(&group->miss_list_lock);
			if (group->not_yet > 0) {
				for (i = 0; i < MAX_MISS_LIST; ++i) {
					len = strlen(group->miss_list[i]);
					if (len == 0)
						continue;
					if (group->miss_list_tgid[i] != p->pid)
						continue;
					if (!strncmp(tsk->comm, group->miss_list[i], len)) {
						strcpy(group->miss_list[i], "");
						group->miss_list_tgid[i] = 0;
						tsk->dtpd = 1;
						tsk->tpd = group->decision;
						group->not_yet--;
						ret = true;
						break;
					}
				}

				if (ret) {
					spin_unlock(&group->miss_list_lock);
					break; /* break list_for_each_entry */
				}
			}
			spin_unlock(&group->miss_list_lock);
			/* end tagging process */
		}
		spin_unlock(&group->tgid_list_lock);

		/* reset flag if dynamic tpd task removed (tgid was removed) */
		if (!ret) {
			if (tsk->dtpd) {
				tsk->dtpd = 0;
				tsk->tpd = 0;
			}
		}
		break;
	default:
		break;
	}

	return ret;
}

static void set_tpd_ctl(int force)
{
	if (force)
		atomic64_inc(&tpd_ctl);
	else {
		if (atomic64_read(&tpd_ctl) > 0)
			atomic64_dec(&tpd_ctl);
	}
}

static int tpd_ctl_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	set_tpd_ctl(val);

	return 0;
}

static int tpd_ctl_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%lld\n", atomic64_read(&tpd_ctl));
}

static struct kernel_param_ops tpd_ctl_ops = {
	.set = tpd_ctl_store,
	.get = tpd_ctl_show,
};

module_param_cb(tpd_ctl, &tpd_ctl_ops, NULL, 0664);

static inline void tagging(struct task_struct *tsk, int decision)
{
	if (tsk == NULL) {
		tpd_loge("task cannot set");
		return;
	}

	tpd_logv("%s task: %s pid:%d decision:%d\n", __func__, tsk->comm, tsk->pid, decision);

	tsk->tpd = decision;
}

static inline void tagging_by_name(struct task_struct *tsk, char* name, int decision, int *cnt)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(name);
	if (tlen == 0)
		return;

	len = strlen(tsk->comm);

	if (len != tlen)
		return;

	if (!strncmp(tsk->comm, name, tlen)) {
		tpd_logv("%s task: %s pid:%d decision:%d name=%s\n", __func__, tsk->comm, tsk->pid, decision, name);
		tsk->tpd = decision;
		*cnt = *cnt + 1;
	}
}

static void tag_from_tgid(unsigned int tgid, int decision, char* thread_name, int *cnt)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	p = find_task_by_vpid(tgid);
	if (p) {
		for_each_thread(p, t)
			tagging_by_name(t, thread_name, decision, cnt);
	}
	rcu_read_unlock();

}

static inline void dy_tagging_by_name(struct task_struct *tsk, const char* name, int decision, int dtpd, int *cnt)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(name);
	if (tlen == 0)
		return;

	len = strlen(tsk->comm);

	if (len != tlen)
		return;

	if (!strncmp(tsk->comm, name, tlen)) {
		tpd_logv("%s task: %s pid:%d decision:%d name=%s, dtpd=%d\n", __func__, tsk->comm, tsk->pid, decision, name, dtpd);
		tsk->tpd = decision;
		tsk->dtpd = dtpd;
		*cnt = *cnt + 1;
	}
}

static void dy_tag_from_tgid(unsigned int tgid, int decision, const char* thread_name, int dtpd, int *cnt)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	p = find_task_by_vpid(tgid);
	if (p) {
		for_each_thread(p, t)
			dy_tagging_by_name(t, thread_name, decision, dtpd, cnt);
	}
	rcu_read_unlock();

}

/* set dtpd group id in the main thread */
static void tag_dtpdg(unsigned int tgid, unsigned int dtpdg)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_vpid(tgid);
	if (p) {
		p->dtpdg = dtpdg;
	}
	rcu_read_unlock();

}

static int tpd_cmd_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int tgid = 0;
	int tp_decision = -1;
	char threads[MAX_THREAD_INPUT][TASK_COMM_LEN] = {{0}, {0}, {0}, {0}, {0}, {0}};
	int ret, i, cnt = 0;

	ret = sscanf(buf, "%u %d %s %s %s %s %s %s\n",
		&tgid, &tp_decision,
		threads[0], threads[1], threads[2], threads[3], threads[4], threads[5]);

        tpd_logi("tpd params: %u %d %s %s %s %s %s %s, from %s %d, total=%d\n",
              tgid, tp_decision, threads[0], threads[1], threads[2], threads[3], threads[4], threads[5],
              current->comm, current->pid, ret);

	for (i = 0; i < MAX_THREAD_INPUT; i++) {
		if (strlen(threads[i]) > 0)
			tag_from_tgid(tgid, tp_decision, threads[i], &cnt);
	}

	tpd_logv("tpd tagging count = %d\n", cnt);

	if (cnt > 0)
		set_tpd_ctl(1);

	/* reset tpd decision of top activity thread when top activity change */
	if (tp_decision == 0)
		set_tpd_ctl(0);

	return 0;
}

static struct kernel_param_ops tpd_cmd_ops = {
	.set = tpd_cmd_store,
};
module_param_cb(tpd_cmds, &tpd_cmd_ops, NULL, 0664);

static void tag_from_tid(unsigned int pid, unsigned int tid, int decision)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_vpid(tid);
	if (p) {
		if (p->group_leader && (p->group_leader->pid == pid)) {
			tpd_logv("tpd tagging task pid= %d\n", pid);
			tagging(p, decision);
		}
	} else {
		tpd_loge("cannot find task!!! pid = %d", tid);
	}
	rcu_read_unlock();
}

static int tpd_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int pid = 0;
	unsigned int tid = 0;
	int tpdenable = 0;
	int tp_decision = -1;
	int ret;

	ret = sscanf(buf, "%u,%u,%d,%d\n",
		&pid, &tid, &tpdenable, &tp_decision);

	tpd_logi("tpd param pid:%u tid:%u, tpd_enable:%d decision:%d from %s %d\n",
		pid, tid, tpdenable, tp_decision, current->comm, current->pid);

	if (ret != 4) {
		tpd_loge("Invalid params!!!!!!");
		return 0;
	}

	tag_from_tid(pid, tid, tpdenable ? tp_decision : 0);

	set_tpd_ctl(tpdenable);

	/* update tpd_enable ref cnt*/
	if (should_update_tpd_enable(tpdenable))
		tpd_enable = tpdenable;

	return 0;
}

static struct kernel_param_ops tpd_ops = {
	.set = tpd_store,
};
module_param_cb(tpd_id, &tpd_ops, NULL, 0664);

static void tgid_list_add(struct monitor_gp *_mgp, int pid)
{
	struct tgid_list_entry *p;

	p = kmalloc(sizeof(struct tgid_list_entry), GFP_KERNEL);
	if (p == NULL)
		return;
	p->pid = pid;
	INIT_LIST_HEAD(&p->node);

	spin_lock(&_mgp->tgid_list_lock);
	list_add_tail(&p->node, &_mgp->tgid_head);
	tpd_logv("add main thread: %d", pid);
	spin_unlock(&_mgp->tgid_list_lock);
}

void tpd_tglist_del(struct task_struct *tsk)
{
	struct tgid_list_entry *p, *next;
	struct monitor_gp *group;

	if (!tsk->pid)
		return;

	if (tsk->dtpdg < 0 || tsk->dtpdg >= TPD_GROUP_MAX)
		return;

	group = &mgp[tsk->dtpdg];

	spin_lock(&group->tgid_list_lock);

	if (list_empty(&group->tgid_head))
		goto unlock;

	list_for_each_entry_safe(p, next, &group->tgid_head, node) {
		if (p != NULL && (p->pid == tsk->pid)) {
			list_del_init(&p->node);
			tpd_logv("remove task: %d", tsk->pid);
			kfree(p);
			break;
		}
	}
	/* if no tgid in list, disable tpd_ctl && try to disable tpd */
	if (list_empty(&group->tgid_head)) {
		set_tpd_ctl(0);
		if (should_update_tpd_enable(0))
			tpd_enable = 0;
	}

unlock:
	spin_unlock(&group->tgid_list_lock);
}

/* return true if list size is from one to empty */
static bool tgid_list_del(struct monitor_gp *_mgp, int pid)
{
	struct tgid_list_entry *p, *next;
	bool ret = false;

	spin_lock(&_mgp->tgid_list_lock);
	/* do nothing if list empty */
	if (list_empty(&_mgp->tgid_head))
		goto unlock;

	list_for_each_entry_safe(p, next, &_mgp->tgid_head, node) {
		if (p != NULL && (p->pid == pid)) {
			list_del_init(&p->node);
			tpd_logv("remove main thread: %d", pid);
			kfree(p);
			break;
		}
	}
	/* re-check if list is empty after deletion */
	if (list_empty(&_mgp->tgid_head))
		ret = true;

unlock:
	spin_unlock(&_mgp->tgid_list_lock);
	return ret;
}

static int list_show(char *buf, const struct kernel_param *kp)
{
	struct tgid_list_entry *p;
	int cnt = 0;
	int i;

	for (i = 0; i < TPD_GROUP_MAX; ++i) {
		spin_lock(&mgp[i].tgid_list_lock);

		if (list_empty(&mgp[i].tgid_head))
			goto unlock;

		list_for_each_entry(p, &mgp[i].tgid_head, node) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "%d %d\n", i, p->pid);
		}
unlock:
		spin_unlock(&mgp[i].tgid_list_lock);
	}

	return cnt;
}

static struct kernel_param_ops tgid_list_ops = {
	.get = list_show,
};

module_param_cb(tgid_list, &tgid_list_ops, NULL, 0664);

#define MONITOR_THREAD_NUM 1
static int tpd_process_trigger_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int tgid = 0;
	int tpdenable = 0;
	int tp_decision = -1;
	int tpd_group = -1;
	int ret, i, cnt = 0, j;
	const char *threads[MONITOR_THREAD_NUM] = {"bg"};
	struct monitor_gp *group;

	ret = sscanf(buf, "%d,%u,%d,%d\n",
		&tpd_group, &tgid, &tpdenable, &tp_decision);

	tpd_logi("tpd param group:%d pid:%u tpd_enable:%d decision:%d from %s %d\n",
		tpd_group, tgid, tpdenable, tp_decision, current->comm, current->pid);

	if (ret != 4) {
		tpd_loge("Invalid params!!!!!!");
		return 0;
	}

	if (tpd_group >= TPD_GROUP_MAX || tpd_group < 0) {
		tpd_loge("Invalid group!!!!!!");
		return 0;
	}

	group = &mgp[tpd_group];

	if (!tpdenable) {
		if (tgid_list_del(group, tgid))
			group->decision = 0;

		tag_dtpdg(tgid, -1);
	} else {
		group->decision = tp_decision;
		tgid_list_add(group, tgid);
		tag_dtpdg(tgid, tpd_group);
	}

	set_tpd_ctl(tpdenable);


	/* tagged by name from group: media provider */
	if (tpd_group == TPD_GROUP_MEDIAPROVIDER) {
		for(i = 0; i < MONITOR_THREAD_NUM; i++) {
			cnt = 0;
			dy_tag_from_tgid(tgid, tpdenable ? tp_decision : 0, threads[i], tpdenable, &cnt);
			/* can't find thread to tag/un-tag */
			if (cnt == 0) {
				spin_lock(&group->miss_list_lock);
				if (tpdenable) {
					/* need re-tag, add thread name into miss_list */
					strncpy(group->miss_list[group->cur_idx], threads[i], strlen(threads[i]));
					group->miss_list_tgid[group->cur_idx] = tgid;
					group->cur_idx = (group->cur_idx + 1) % MAX_MISS_LIST;
					group->not_yet++;
				} else {
					/* dynmic tpd disabled when miss_list still have thread need to tag, we clear the miss list */
					for (j = 0; j < MAX_MISS_LIST; ++j) {
						if (group->miss_list_tgid[j] == tgid) {
							strcpy(group->miss_list[j], "");
							group->miss_list_tgid[j] = 0;
							group->not_yet--;
						}
					}
				}
				spin_unlock(&group->miss_list_lock);
			}
		}
	}

	tpd_logv("tagging count = %d, tpd enable set:%d", cnt, tpdenable);

	/* update tpd_enable by ref cnt */
	if (should_update_tpd_enable(tpdenable))
		tpd_enable = tpdenable;

	return 0;
}

static struct kernel_param_ops tpd_pt_ops = {
	.set = tpd_process_trigger_store,
};
module_param_cb(tpd_dynamic, &tpd_pt_ops, NULL, 0664);

//#ifndef task_is_fg
//static __always_inline bool task_is_fg(struct task_struct *task)
//{
//	int cur_uid;
//
//	cur_uid = task_uid(task).val;
//	if (is_fg(cur_uid))
//		return true;
//	return false;
//}
//#endif

int tpd_suggested(struct task_struct* tsk, int request_cluster)
{
	int suggest_cluster = request_cluster;
	int tpd_local;

	if (!(task_is_fg(tsk) || atomic64_read(&tpd_ctl) || (tsk->tpd_st > 0)))
		goto out;

	tpd_local = (tsk->tpd_st > 0) ? tsk->tpd_st : tsk->tpd;

	switch (tpd_local) {
	case TPD_TYPE_S:
	case TPD_TYPE_GS:
	case TPD_TYPE_PS:
	case TPD_TYPE_PGS:
		suggest_cluster = 0;
		break;
	case TPD_TYPE_G:
	case TPD_TYPE_PG:
		suggest_cluster = 1;
		break;
	case TPD_TYPE_P:
		if (cluster_total == MAX_CLUSTERS)
			suggest_cluster = 2;
		else
			suggest_cluster = 1;
		break;
	default:
		tpd_loge("suggest unexpected case: tpd = %d, tpd_st = %d", tsk->tpd, tsk->tpd_st);
		break;
	}
out:
	tpd_logi("pid = %d: comm = %s, tpd = %d, suggest_cpu = %d, task is fg? %d, tpd_ctl = %lld, tpd_st = %d\n", tsk->pid, tsk->comm,
		tsk->tpd, suggest_cluster, task_is_fg(tsk), atomic64_read(&tpd_ctl), tsk->tpd_st);
	return suggest_cluster;
}

int tpd_suggested_cpu(struct task_struct* tsk, int request_cpu)
{
	int suggest_cpu = request_cpu;
	int tpd_local;

	if (!(task_is_fg(tsk) || atomic64_read(&tpd_ctl) || (tsk->tpd_st > 0)))
		goto out;

	tpd_local = (tsk->tpd_st > 0) ? tsk->tpd_st : tsk->tpd;

	switch (tpd_local) {
	case TPD_TYPE_S:
	case TPD_TYPE_GS:
	case TPD_TYPE_PS:
	case TPD_TYPE_PGS:
		suggest_cpu = clusters[0].first_cpu;
		break;
	case TPD_TYPE_G:
	case TPD_TYPE_PG:
		suggest_cpu = clusters[1].first_cpu;
		break;
	case TPD_TYPE_P:
		if (cluster_total == MAX_CLUSTERS)
			suggest_cpu = clusters[2].first_cpu;
		else
			suggest_cpu = clusters[1].first_cpu;
		break;
	default:
		tpd_loge("suggest unexpected case: tpd = %d, tpd_st = %d", tsk->tpd, tsk->tpd_st);
		break;
	}
out:
	tpd_logi("pid = %d: comm = %s, tpd = %d, suggest_cpu = %d, task is fg? %d, tpd_ctl = %lld, tpd_st = %d\n", tsk->pid, tsk->comm,
		tsk->tpd, suggest_cpu, task_is_fg(tsk), atomic64_read(&tpd_ctl), tsk->tpd_st);
	return suggest_cpu;
}

void tpd_mask(struct task_struct* tsk, cpumask_t *request)
{
	int i = 0, j, x;
	int tmp_tpd;

	cpumask_t mask = CPU_MASK_NONE;

	if (!(task_is_fg(tsk) || atomic64_read(&tpd_ctl) || (tsk->tpd_st > 0))) {
		if (!task_is_fg(tsk))
			tpd_logv("task is not fg!!!\n");
		goto out;
	}

	tmp_tpd = (tsk->tpd_st > 0) ? tsk->tpd_st : tsk->tpd;

	while (tmp_tpd > 0) {
		if (tmp_tpd & 1) {
			for_each_cpu(j, &clusters[i].related_cpus)
				cpumask_set_cpu(j, &mask);
			i++;
		}
		tmp_tpd = tmp_tpd >> 1;
	}
	cpumask_copy(request, &mask);
	tpd_logi("tpd_mask: related_cpus = ");
	for_each_cpu(x, request)
		tpd_logi("%d ", x);

out:
	tpd_logi("pid = %d: comm = %s, tpd = %d, task is fg? %d, tpd_ctl = %lld, tpd_st = %d\n", tsk->pid, tsk->comm,
		tsk->tpd, task_is_fg(tsk), atomic64_read(&tpd_ctl), tsk->tpd_st);
}

bool tpd_check(struct task_struct *tsk, int dest_cpu)
{
	bool mismatch = false;
	int tpd_local;

	if (!(task_is_fg(tsk) || atomic64_read(&tpd_ctl) || (tsk->tpd_st > 0)))
		goto out;

	tpd_local = (tsk->tpd_st > 0) ? tsk->tpd_st : tsk->tpd;
	switch (tpd_local) {
	case TPD_TYPE_S:
		if (!cpumask_test_cpu(dest_cpu, &clusters[0].related_cpus))
			mismatch = true;
		break;
	case TPD_TYPE_G:
		if (!cpumask_test_cpu(dest_cpu, &clusters[1].related_cpus))
			mismatch = true;
		break;
	case TPD_TYPE_GS:
		/* if no gold plus cores, mid = max*/
		if (cluster_total == 3 && cpumask_test_cpu(dest_cpu, &clusters[2].related_cpus))
			mismatch = true;
		break;
	case TPD_TYPE_P:
		if (cluster_total == 3 && !cpumask_test_cpu(dest_cpu, &clusters[2].related_cpus))
			mismatch = true;
		break;
	case TPD_TYPE_PS:
		if (cpumask_test_cpu(dest_cpu, &clusters[1].related_cpus))
			mismatch = true;
		break;
	case TPD_TYPE_PG:
		if (cpumask_test_cpu(dest_cpu, &clusters[0].related_cpus))
			mismatch = true;
		break;
	default:
		tpd_loge("check unexpected case: tpd = %d, tpd_st = %d", tsk->tpd, tsk->tpd_st);
		break;
	}

out:
	tpd_logi("task:%d comm:%s dst: %d should migrate = %d, task is fg? %d, tpd = %d, tpd_st = %d, tpd_ctl = %lld\n",
		tsk->pid, tsk->comm, dest_cpu, !mismatch, task_is_fg(tsk), tsk->tpd, tsk->tpd_st, atomic64_read(&tpd_ctl));

	return mismatch;
}

void tpd_init_policy(struct cpufreq_policy *policy)
{
	struct tpd_cpuinfo *cpu_info;
	int i;

	cpu_info = &clusters[cluster_total];
	cpu_info->first_cpu = policy->cpu;
	cpumask_copy(&cpu_info->related_cpus, policy->related_cpus);
	cluster_total++;

	tpd_logi("policy->cpu = %d, related_cpus = ", policy->cpu);
	for_each_cpu(i, policy->related_cpus)
		tpd_logi("%d ", i);
}

static void tpd_mgp_init(void)
{
	int i, j;

	for (i = 0; i < TPD_GROUP_MAX; ++i) {
		mgp[i].decision = 0;
		spin_lock_init(&mgp[i].tgid_list_lock);
		INIT_LIST_HEAD(&mgp[i].tgid_head);
		spin_lock_init(&mgp[i].miss_list_lock);
		for (j = 0; j < MAX_MISS_LIST; j++) {
			mgp[i].miss_list_tgid[j] = 0;
			strcpy(mgp[i].miss_list[j], "");
		}
		mgp[i].not_yet = 0;
		mgp[i].cur_idx = 0;
	}
}

static int tpd_init(void)
{
        tpd_mgp_init();
        tpd_logv("tpd init\n");
        return 0;
}

pure_initcall(tpd_init);
