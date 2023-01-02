// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/input/qpnp-power-on.h>
#include <linux/of_address.h>
#include <asm/cacheflush.h>
#include <asm/system_misc.h>
#include <asm/memory.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/restart.h>
#include <soc/qcom/watchdog.h>
#include <soc/qcom/minidump.h>

#include <linux/uaccess.h>
#include <asm-generic/irq_regs.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <soc/qcom/memory_dump.h>
#include <linux/sched/debug.h>
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/system/qcom_minidump_enhance.h>

#define CPUCTX_VERSION 		1
#define CPUCTX_MAIGC1 		0x4D494E49
#define CPUCTX_MAIGC2 		(CPUCTX_MAIGC1 + CPUCTX_VERSION)

struct cpudatas{
	struct pt_regs 			pt;
	unsigned int 			regs[32][512];//X0-X30 pc
	unsigned int 			sps[1024];
	unsigned int 			ti[16];//thread_info
	unsigned int			task_struct[1024];
};//16 byte alignment

struct cpuctx{
	unsigned int magic_nubmer1;
	unsigned int magic_nubmer2;
	unsigned int dump_cpus;
	unsigned int reserve;
	struct cpudatas datas[0];
};

static struct cpuctx *Cpucontex_buf = NULL;
extern struct pt_regs * get_arm64_cpuregs(struct pt_regs *regs);




/*maybe system crashed at early boot time .at that time maybe pm_power_off and 
  *arm_pm_restart are not defined
  */
#define PS_HOLD_ADDR 0x10ac000 //sdm660.dts
#define SCM_IO_DEASSERT_PS_HOLD		2 //msm-poweroff.c
static void pull_down_pshold(void)
{
	struct scm_desc desc = {
		.args[0] = 0,
		.arginfo = SCM_ARGS(1),
	};
	void __iomem *msm_ps_hold = ioremap(PS_HOLD_ADDR , 4);;

	printk("user do_msm_restart_early to reboot\n");
	if (scm_is_call_available(SCM_SVC_PWR, SCM_IO_DEASSERT_PS_HOLD) > 0)
	{
		/* This call will be available on ARMv8 only */
		scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_PWR,
				 SCM_IO_DEASSERT_PS_HOLD), &desc);

	}
	/* Fall-through to the direct write in case the scm_call "returns" */
	if(msm_ps_hold)
		__raw_writel(0, msm_ps_hold);

	mdelay(10000);

}

void do_restart_early(enum reboot_mode reboot_mode, const char *cmd)
{
	pull_down_pshold();
}

void do_poweroff_early(void)
{
	pull_down_pshold();
}

void dumpcpuregs(struct pt_regs *pt_regs)
{
        return;
}
EXPORT_SYMBOL(dumpcpuregs);

void register_cpu_contex(void)
{
	int ret;
	struct msm_dump_entry dump_entry;
	struct msm_dump_data *dump_data;

	if (MSM_DUMP_MAJOR(msm_dump_table_version()) > 1) {
		dump_data = kzalloc(sizeof(struct msm_dump_data), GFP_KERNEL);
		if (!dump_data)
			return;
		Cpucontex_buf = ( struct cpuctx *)kzalloc(sizeof(struct cpuctx) + 
		 		sizeof(struct cpudatas)* num_present_cpus(),GFP_KERNEL);

		if (!Cpucontex_buf)
			goto err0;
		//init magic number
		Cpucontex_buf->magic_nubmer1 = CPUCTX_MAIGC1;
		Cpucontex_buf->magic_nubmer2 = CPUCTX_MAIGC2;//version
		Cpucontex_buf->dump_cpus = 0;//version

		strlcpy(dump_data->name, "cpucontex", sizeof(dump_data->name));
		dump_data->addr = virt_to_phys(Cpucontex_buf);
		dump_data->len = sizeof(struct cpuctx) + sizeof(struct cpudatas)* num_present_cpus();
		dump_entry.id = 0;
		dump_entry.addr = virt_to_phys(dump_data);
		ret = msm_dump_data_register(MSM_DUMP_TABLE_APPS, &dump_entry);
		if (ret) {
			pr_err("Registering cpu contex dump region failed\n");
			goto err1;
		}
		return;
err1:
		kfree(Cpucontex_buf);
		Cpucontex_buf=NULL;
err0:
		kfree(dump_data);
	}
}

