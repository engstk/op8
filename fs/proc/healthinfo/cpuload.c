#include <linux/delay.h>
#include <soc/oplus/healthinfo.h>
#include <linux/kernel_stat.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
extern  u64 get_idle_time(int cpu);
extern  u64 get_iowait_time(int cpu);
#else
extern  u64 get_idle_time(struct kernel_cpustat *kcs, int cpu);
extern  u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu);
#endif

struct cpu_load_stat {
        u64 t_user;
        u64 t_system;
        u64 t_idle;
        u64 t_iowait;
        u64 t_irq;
        u64 t_softirq;
};

#define OPLUS_CPUTIME
int ohm_get_cur_cpuload(bool ctrl)
{
	int i;
	struct cpu_load_stat cpu_load = { 0, 0, 0, 0, 0, 0};
        struct cpu_load_stat cpu_load_temp = { 0, 0, 0, 0, 0, 0};
        clock_t ct_user, ct_system, ct_idle, ct_iowait, ct_irq, ct_softirq, load, sum = 0;
        if (!ctrl)
                return -1;

	for_each_online_cpu(i) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		struct kernel_cpustat *kcs = &kcpustat_cpu(i);
#endif
		cpu_load_temp.t_user += kcpustat_cpu(i).cpustat[CPUTIME_USER];
		cpu_load_temp.t_system += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
		cpu_load_temp.t_idle += get_idle_time(i);
		cpu_load_temp.t_iowait += get_iowait_time(i);
#else
		cpu_load_temp.t_idle += get_idle_time(kcs, i);
		cpu_load_temp.t_iowait += get_iowait_time(kcs, i);
#endif
		cpu_load_temp.t_irq += kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		cpu_load_temp.t_softirq += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
	}
        msleep(25);
	for_each_online_cpu(i) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		struct kernel_cpustat *kcs = &kcpustat_cpu(i);
#endif
		cpu_load.t_user += kcpustat_cpu(i).cpustat[CPUTIME_USER];
		cpu_load.t_system += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
		cpu_load.t_idle += get_idle_time(i);
		cpu_load.t_iowait += get_iowait_time(i);
#else
		cpu_load.t_idle += get_idle_time(kcs,i);
		cpu_load.t_iowait += get_iowait_time(kcs,i);
#endif
		cpu_load.t_irq += kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		cpu_load.t_softirq += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
	}

        ct_user = nsec_to_clock_t(cpu_load.t_user) - nsec_to_clock_t(cpu_load_temp.t_user);
        ct_system = nsec_to_clock_t(cpu_load.t_system) - nsec_to_clock_t(cpu_load_temp.t_system);
        ct_idle = nsec_to_clock_t(cpu_load.t_idle) - nsec_to_clock_t(cpu_load_temp.t_idle);
        ct_iowait = nsec_to_clock_t(cpu_load.t_iowait) - nsec_to_clock_t(cpu_load_temp.t_iowait);
        ct_irq = nsec_to_clock_t(cpu_load.t_irq) - nsec_to_clock_t(cpu_load_temp.t_irq);
        ct_softirq = nsec_to_clock_t(cpu_load.t_softirq) - nsec_to_clock_t(cpu_load_temp.t_softirq);

	sum = ct_user + ct_system + ct_idle + ct_iowait + ct_irq + ct_softirq;
        load = ct_user + ct_system + ct_iowait + ct_irq + ct_softirq;

	if (sum == 0)
		return -1;

	return 100 * load / sum;
}
EXPORT_SYMBOL(ohm_get_cur_cpuload);
