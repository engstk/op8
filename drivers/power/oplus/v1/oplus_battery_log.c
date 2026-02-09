// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 *
 * oplus_battery_log.c
 * 1.0 : Charging log upload mechanism
 * 2.0 : Save key logs in case of device abnormality
 */
#define pr_fmt(fmt) "[BATTERY_LOG]([%s][%d]): " fmt, __func__, __LINE__

#include <oplus_battery_log.h>
#include <oplus_charger.h>
#include <oplus_chg_module.h>

static struct battery_log_dev *g_battery_log_dev;
static struct battery_log_ops *g_ops_register_fail[BATTERY_LOG_DEVICE_ID_END];
static int register_fail_num = 0;

static const char *const g_battery_log_device_id_table[] = {
	[BATTERY_LOG_DEVICE_ID_COMM_INFO] = "comm_info",
	[BATTERY_LOG_DEVICE_ID_VOOC] = "vooc",
	[BATTERY_LOG_DEVICE_ID_BUCK_IC] = "buck_ic",
	/*[BATTERY_LOG_DEVICE_ID_BQ27541] = "bq27541",*/
};

static struct battery_log_dev *battery_log_get_dev(void)
{
	if (!g_battery_log_dev) {
		chg_err("g_battery_log_dev is null\n");
		return NULL;
	}

	return g_battery_log_dev;
}

static int battery_log_get_device_id(const char *str)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(g_battery_log_device_id_table); i++) {
		chg_info("battery_log_get_device_id i=%d\n", i);
		if (!strncmp(str, g_battery_log_device_id_table[i], strlen(str))) {
			chg_err("battery_log_get_device_id i=%d\n", i);
			return i;
		}
	}

	return -EINVAL;
}

static int battery_log_check_type(int type)
{
	if ((type >= BATTERY_LOG_TYPE_BEGIN) && (type < BATTERY_LOG_TYPE_END))
		return 0;

	chg_err("invalid type=%d\n", type);
	return -EINVAL;
}

static int battery_log_operate_ops(struct battery_log_ops *ops, int type, char *buf, int size)
{
	int ret = BATTERY_LOG_INVAID_OP;

	switch (type) {
	case BATTERY_LOG_DUMP_LOG_HEAD:
		if (!ops->dump_log_head)
			return ret;
		memset(buf, 0, size);
		return ops->dump_log_head(buf, size, ops->dev_data);
	case BATTERY_LOG_DUMP_LOG_CONTENT:
		if (!ops->dump_log_content)
			return ret;
		memset(buf, 0, size);
		return ops->dump_log_content(buf, size, ops->dev_data);
	default:
		return ret;
	}
}

int battery_log_common_operate(int type, char *buf, int size)
{
	int i, ret;
	int buf_size;
	int used = 0;
	int unused;
	struct battery_log_dev *l_dev = NULL;

	if (!buf)
		return -EINVAL;

	ret = battery_log_check_type(type);
	if (ret)
		return -EINVAL;

	l_dev = battery_log_get_dev();
	if (!l_dev)
		return -EINVAL;

	mutex_lock(&l_dev->log_lock);

	for (i = 0; i < BATTERY_LOG_DEVICE_ID_END; i++) {
		if (!l_dev->ops[i])
			continue;

		ret = battery_log_operate_ops(l_dev->ops[i], type, l_dev->log_buf, BATTERY_LOG_MAX_SIZE - 1);
		if (ret) {
			/* if op is invalid, must be skip output */
			if (ret == BATTERY_LOG_INVAID_OP)
				continue;
			chg_err("error type=%d, i=%d, ret=%d\n", type, i, ret);
			break;
		}

		unused = size - BATTERY_LOG_RESERVED_SIZE - used;
		buf_size = strlen(l_dev->log_buf);
		if (unused > buf_size) {
			strncat(buf, l_dev->log_buf, buf_size);
			used += buf_size;
		} else {
			strncat(buf, l_dev->log_buf, unused);
			used += unused;
			break;
		}
	}
	buf_size = strlen("\n\0");
	strncat(buf, "\n\0", buf_size);

	mutex_unlock(&l_dev->log_lock);
	return used + buf_size;
}

int battery_log_ops_register(struct battery_log_ops *ops)
{
	struct battery_log_dev *l_dev = g_battery_log_dev;
	int dev_id;

	if (!ops || !ops->dev_name) {
		chg_err("ops is null\n");
		return -EINVAL;
	}
	if (!l_dev) {
		g_ops_register_fail[register_fail_num++] = ops;
		chg_err("l_dev is not ready, ops register when l_dev is ready\n");
		return -EINVAL;
	}

	dev_id = battery_log_get_device_id(ops->dev_name);
	if (dev_id < 0) {
		chg_err("%s ops register fail\n", ops->dev_name);
		return -EINVAL;
	}
	chg_info("dev_id = %d", dev_id);

	l_dev->ops[dev_id] = ops;
	l_dev->total_ops++;

	chg_info("total_ops=%d %d:%s ops register ok\n", l_dev->total_ops, dev_id, ops->dev_name);
	return 0;
}

static int oplus_battery_log_parse_dt(struct battery_log_dev *l_dev)
{
	struct device_node *node = l_dev->dev->of_node;

	l_dev->battery_log_support = of_property_read_bool(node, "oplus,battery_log_support");
	chg_info("oplus,battery_log_support is %d\n", l_dev->battery_log_support);

	return 0;
}

int oplus_battery_log_support(void)
{
	if (g_battery_log_dev == NULL)
		return -ENODEV;

	return g_battery_log_dev->battery_log_support;
}

static int oplus_battery_log_probe(struct platform_device *pdev)
{
	struct battery_log_dev *l_dev;
	int i;

	l_dev = devm_kzalloc(&pdev->dev, sizeof(struct battery_log_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;
	l_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, l_dev);

	l_dev->total_ops = 0;
	mutex_init(&l_dev->log_lock);
	mutex_init(&l_dev->devid_lock);
	oplus_battery_log_parse_dt(l_dev);
	g_battery_log_dev = l_dev;
	if (register_fail_num != 0) {
		for (i = 0; i < register_fail_num; i++)
			battery_log_ops_register(g_ops_register_fail[i]);
	}

	return 0;
}

static int oplus_battery_log_remove(struct platform_device *pdev)
{
	struct battery_log_dev *l_dev = g_battery_log_dev;

	if (!l_dev)
		return -ENOMEM;

	mutex_destroy(&l_dev->log_lock);
	mutex_destroy(&l_dev->devid_lock);
	devm_kfree(&pdev->dev, l_dev);
	g_battery_log_dev = NULL;

	return 0;
}

static const struct of_device_id oplus_battery_log_match[] = {
	{ .compatible = "oplus,battery_log" },
	{},
};

static struct platform_driver oplus_battery_log_driver = {
	.driver		= {
		.name = "oplus-battery_log",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_battery_log_match),
	},
	.probe		= oplus_battery_log_probe,
	.remove		= oplus_battery_log_remove,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static __init int oplus_battery_log_driver_init(void)
{
	return platform_driver_register(&oplus_battery_log_driver);
}

static __exit void oplus_battery_log_driver_exit(void)
{
	platform_driver_unregister(&oplus_battery_log_driver);
}

oplus_chg_module_register(oplus_battery_log_driver);
#else
module_platform_driver(oplus_battery_log_driver);
#endif

MODULE_DESCRIPTION("battery log driver");
MODULE_LICENSE("GPL v2");
