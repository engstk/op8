#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>

#define MAX_CMDLINE_PARAM_LEN 1024
char shutdown_force_panic[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(shutdown_force_panic);

module_param_string(force_panic, shutdown_force_panic, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(force_panic,
"shutdown_speed.force_panic=<force_panic>");

MODULE_LICENSE("GPL v2");
