// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/timekeeping.h>
#include <linux/rtc.h>
#include "input-compat.h"
#include <soc/oplus/system/boot_mode.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define SAUPWK_KEY_DOWN 1

#if IS_MODULE(CONFIG_OPLUS_FEATURE_SAUPWK)
extern char saupwk_enable[];
#endif
bool __read_mostly saupwk_en = false;
static int sau_pwknum = 0;
static int saupwk_cache_obtained = 0;

bool is_silent_reboot_or_sau(void) {
    int boot_mode = get_boot_mode();
    return  MSM_BOOT_MODE__SILENCE == boot_mode || MSM_BOOT_MODE__SAU == boot_mode;
}

inline void oplus_sync_saupwk_event(unsigned int type, unsigned int code, int value)
{
    if (saupwk_en && is_silent_reboot_or_sau() && (!saupwk_cache_obtained) &&
	type == EV_KEY && code == KEY_POWER && value == SAUPWK_KEY_DOWN){
	sau_pwknum += 1;
	pr_debug("[SAUPWK]:power key pressed\n");
    }
}

static int sau_pwknum_seq_show(struct seq_file *seq, void*offset)
{
    seq_printf(seq, "%d", sau_pwknum);
    saupwk_cache_obtained = 1;
    return 0;
}

extern void* PDE_DATA(const struct inode*inode);
static int sau_pwknum_open(struct inode *inode, struct file* file)
{
    return single_open(file, sau_pwknum_seq_show, PDE_DATA(inode));
}


static const struct file_operations oplus_sau_pwknum_fops = {
    .owner   = THIS_MODULE,
    .open    = sau_pwknum_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static int __init get_saupwk_enable(char* str)
{
    if(!strncmp("1", str, sizeof("1"))){
	saupwk_en = 1;
    }
    return 1;
}
#if IS_BUILTIN(CONFIG_OPLUS_FEATURE_SAUPWK)
__setup("saupwk.en=",get_saupwk_enable);
#endif

static int __init
oplus_saupwk_during_init(void)
{
    struct proc_dir_entry * pentry;
#if IS_MODULE(CONFIG_OPLUS_FEATURE_SAUPWK)
    if(saupwk_enable != NULL){
        get_saupwk_enable(saupwk_enable);
    }else{
        get_saupwk_enable("1");
    }
#endif
    if (!saupwk_en){
		return 0;
    }

    pentry = proc_create("sau_pwknum", 0, NULL, &oplus_sau_pwknum_fops);
    if(!pentry){
	pr_err("sau_pwknum proc entory create failure.\n");
	return -ENOENT;
    }
    pr_warn("[SAUPWK]:sau_pwknum proc entry create success.\n");
    return 0;
}


module_init(oplus_saupwk_during_init);

MODULE_LICENSE("GPL v2");
