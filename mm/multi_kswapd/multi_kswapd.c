// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/sysctl.h>
#include <linux/printk.h>
#include <linux/multi_kswapd.h>
#include <linux/version.h>


/*
* Number of active kswapd threads
*/
#define DEF_KSWAPD_THREADS_PER_NODE  1
int kswapd_threads = DEF_KSWAPD_THREADS_PER_NODE;
int kswapd_threads_current = DEF_KSWAPD_THREADS_PER_NODE;
int max_kswapd_threads = MAX_KSWAPD_THREADS;
#ifdef CONFIG_KSWAPD_UNBIND_MAX_CPU
int kswapd_unbind_cpu = -1;
#endif

static void update_kswapd_threads_node(int nid)
{
	pg_data_t *pgdat;
	int drop, increase;
	int last_idx, start_idx, hid;
	int nr_threads = kswapd_threads_current;

	pgdat = NODE_DATA(nid);
	last_idx = nr_threads - 1;
	if (kswapd_threads < nr_threads) {
		drop = nr_threads - kswapd_threads;
		for (hid = last_idx; hid > (last_idx - drop); hid--) {
			if (pgdat->kswapd[hid]) {
				kthread_stop(pgdat->kswapd[hid]);
				pgdat->kswapd[hid] = NULL;
			}
		}
	} else {
#ifdef CONFIG_KSWAPD_UNBIND_MAX_CPU
		if (kswapd_unbind_cpu == -1)
			upate_kswapd_unbind_cpu();
#endif
		increase = kswapd_threads - nr_threads;
		start_idx = last_idx + 1;
		for (hid = start_idx; hid < (start_idx + increase); hid++) {
			pgdat->kswapd[hid] = kthread_run(kswapd, pgdat,
						"kswapd%d:%d", nid, hid);
			if (IS_ERR(pgdat->kswapd[hid])) {
				pr_err("Failed to start kswapd%d on node %d\n",
					hid, nid);
				pgdat->kswapd[hid] = NULL;
				/*
				 * We are out of resources. Do not start any
				 * more threads.
				 */
				break;
			}
		}
	}
}

void update_kswapd_threads(void)
{
	int nid;

	if (kswapd_threads_current == kswapd_threads)
		return;

	/*
	 * Hold the memory hotplug lock to avoid racing with memory
	 * hotplug initiated updates
	 */
	mem_hotplug_begin();
	for_each_node_state(nid, N_MEMORY)
		update_kswapd_threads_node(nid);

	pr_info("kswapd_thread count changed, old:%d new:%d\n",
		kswapd_threads_current, kswapd_threads);
	kswapd_threads_current = kswapd_threads;
	mem_hotplug_done();
}

int kswapd_threads_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write)
		update_kswapd_threads();

	return 0;
}

/* It's optimal to keep kswapds on the same CPUs as their memory, but
   not required for correctness.  So if the last cpu in a node goes
   away, we get changed to run anywhere: as the first one comes back,
   restore their cpu bindings. */
#if(LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0))
int kswapd_cpu_online_ext(unsigned int cpu)
{
	int nid, hid;
	int nr_threads = kswapd_threads_current;

	for_each_node_state(nid, N_MEMORY) {
		pg_data_t *pgdat = NODE_DATA(nid);
		const struct cpumask *mask;

		mask = cpumask_of_node(pgdat->node_id);

		if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
			for (hid = 0; hid < nr_threads; hid++) {
				/* One of our CPUs online: restore mask */
				set_cpus_allowed_ptr(pgdat->kswapd[hid], mask);
			}
	}
	return 0;
}


#else
int cpu_callback_ext(struct notifier_block *nfb, unsigned long action,
			void *hcpu)
{
	int nid, hid;
	int nr_threads = kswapd_threads_current;

	if (action == CPU_ONLINE || action == CPU_ONLINE_FROZEN) {
		for_each_node_state(nid, N_MEMORY) {
			pg_data_t *pgdat = NODE_DATA(nid);
			const struct cpumask *mask;

			mask = cpumask_of_node(pgdat->node_id);

			if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids) {
			for (hid = 0; hid < nr_threads; hid++) {
				/* One of our CPUs online: restore mask */
				set_cpus_allowed_ptr(pgdat->kswapd[hid], mask);
			}
		}
	}
	}
	return NOTIFY_OK;
}
#endif /*LINUX_VERSION_CODE*/

int kswapd_run_ext(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;
	int hid, nr_threads;

	if (pgdat->kswapd[0])
		return 0;

#ifdef CONFIG_KSWAPD_UNBIND_MAX_CPU
	if (kswapd_unbind_cpu == -1)
		upate_kswapd_unbind_cpu();
#endif
	nr_threads = kswapd_threads;
	for (hid = 0; hid < nr_threads; hid++) {
		pgdat->kswapd[hid] = kthread_run(kswapd, pgdat, "kswapd%d:%d", nid, hid);
			if (IS_ERR(pgdat->kswapd[hid])) {
				/* failure at boot is fatal */
				BUG_ON(system_state < SYSTEM_RUNNING);
				pr_err("Failed to start kswapd%d on node %d\n",
					hid, nid);
				ret = PTR_ERR(pgdat->kswapd[hid]);
				pgdat->kswapd[hid] = NULL;
			}
		}
		kswapd_threads_current = nr_threads;

	return ret;
}

void kswapd_stop_ext(int nid)
{
	struct task_struct *kswapd;
	int hid;
	int nr_threads = kswapd_threads_current;

	for (hid = 0; hid < nr_threads; hid++) {
		kswapd = NODE_DATA(nid)->kswapd[hid];
		if (kswapd) {
			kthread_stop(kswapd);
			NODE_DATA(nid)->kswapd[hid] = NULL;
		}
	}
}

