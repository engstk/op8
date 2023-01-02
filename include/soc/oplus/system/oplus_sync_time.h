/***********************************************************
** Copyright (C), 2008-2019, Oplus Mobile Comm Corp., Ltd.
**
** File: oplus_sync_time.h
** Description: Add for Sync App and Kernel time
**
** Version: 1.0
** Date : 2020/07/22
**
** ------------------ Revision History:------------------------
** <author>      <data>      <version >       <desc>
** Zhiming.Chen    2020/08/06      1.0       OPLUS_FEATURE_LOGKIT
****************************************************************/

#ifndef _OPLUS_SYNC_TIME_H
#define _OPLUS_SYNC_TIME_H
#include <linux/rtc.h>

static ssize_t watchdog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	s32 value;
	struct timespec ts;
	struct rtc_time tm;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else if (count <= 11) { /* ASCII perhaps? */
		char ascii_value[11];
		unsigned long int ulval;
		int ret;

		if (copy_from_user(ascii_value, buf, count))
			return -EFAULT;

		if (count > 10) {
			if (ascii_value[10] == '\n')
				ascii_value[10] = '\0';
			else
				return -EINVAL;
		} else {
			ascii_value[count] = '\0';
		}
		ret = kstrtoul(ascii_value, 16, &ulval);
		if (ret) {
			pr_debug("%s, 0x%lx, 0x%x\n", ascii_value, ulval, ret);
			return -EINVAL;
		}
		value = (s32)lower_32_bits(ulval);
	} else {
		return -EINVAL;
	}

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_warn("!@WatchDog_%d; %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		value, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);

	return count;
}
#endif /*_OPLUS_SYNC_TIME_H */
