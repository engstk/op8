
#ifndef _OPLUS_TASK_LOAD_H
#define _OPLUS_TASK_LOAD_H

#define task_load_err(fmt, ...) \
        printk(KERN_ERR "[TASK_LOAD_INFO_ERR][%s]"fmt, __func__, ##__VA_ARGS__)

enum {
	camera = 0,
	cameraserver,
	cameraprovider,
};


extern void account_normalize_runtime(struct task_struct *p, u64 delta, struct rq *rq);
extern void get_target_process(struct task_struct *task);


#endif
