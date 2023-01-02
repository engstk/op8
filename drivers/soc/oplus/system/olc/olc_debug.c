#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <soc/oplus/system/olc.h>

#define  OLC_PROC_ENTRY(name, entry, mode)\
    ({if (!proc_create(#name, S_IFREG | mode, aed_proc_dir, \
        &proc_##entry##_fops)) \
        pr_info("proc_create %s failed\n", #name); })


static struct proc_dir_entry *olc_proc_dir;

static ssize_t proc_generate_exception_read(struct file *file,
            char __user *buf, size_t size, loff_t *ppos)
{
    return 0;
}

static ssize_t proc_generate_exception_write(struct file *file,
            const char __user *buf, size_t size, loff_t *ppos)
{
    char msg[256];
    char *instr = NULL;
    char *token = NULL;
    char *value = NULL;
    struct exception_info exp;

    if ((size < 5) || (size >= sizeof(msg)))
    {
        pr_err("olc: %s size sould be >= 5 and <= %zx bytes.\n", __func__, sizeof(msg));
        return -EFAULT;
    }

    if (!buf)
    {
        pr_err("olc: %s buf = NULL\n", __func__);
        return -EINVAL;
    }

    memset(msg, 0, sizeof(msg));
    if (copy_from_user(msg, buf, size))
    {
        pr_err("olc: %s unable to read message\n", __func__);
        return -EFAULT;
    }

    memset(&exp, 0, sizeof(exp));
    msg[size] = 0;
    instr = &msg[0];
    token = strsep(&instr, ",");
    while (token != NULL)
    {
        if (strncmp(token, "id", strlen("id")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                exp.exceptionId = simple_strtoul(value + 1, NULL, 16);
            }
        }
        else if (strncmp(token, "time", strlen("time")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                exp.time = simple_strtoul(value + 1, NULL, 10);
            }
        }
        else if (strncmp(token, "level", strlen("level")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                exp.level = simple_strtoul(value + 1, NULL, 10);
            }
        }
        else if (strncmp(token, "exceptionType", strlen("exceptionType")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                exp.exceptionType = simple_strtoul(value + 1, NULL, 10);
            }
        }
        else if (strncmp(token, "atomicLogs", strlen("atomicLogs")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                exp.atomicLogs = simple_strtoull(value + 1, NULL, 10);
            }
        }
        else if (strncmp(token, "logParams", strlen("logParams")) == 0)
        {
            value = strchr(token, '=');
            if (value != NULL)
            {
                strncpy(exp.logParams, value + 1, sizeof(exp.logParams) - 1);
            }
        }

        token = strsep(&instr, ",");
    }

    if (exp.exceptionId != 0)
    {
        if (olc_raise_exception(&exp) < 0)
        {
            pr_err("olc: olc_raise_exception failed.\n");
        }
    }

    return size;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops proc_generate_exception_fops = { \
    .proc_read = proc_generate_exception_read, \
    .proc_write = proc_generate_exception_write, \
};
#else
static const struct file_operations proc_generate_exception_fops = { \
    .read = proc_generate_exception_read, \
    .write = proc_generate_exception_write, \
};
#endif

int olc_proc_debug_init(void)
{
    olc_proc_dir = proc_mkdir("olc", NULL);
    if (!olc_proc_dir)
    {
        pr_info("olc proc_mkdir failed\n");
        return -ENOMEM;
    }

    if (!proc_create("generate-exception", S_IFREG | 0600, olc_proc_dir, &proc_generate_exception_fops))
    {
         pr_info("proc_create generate-exception failed\n");
    }

    return 0;
}

int olc_proc_debug_done(void)
{
    remove_proc_entry("generate-exception", olc_proc_dir);
    remove_proc_entry("olc", NULL);
    return 0;
}
