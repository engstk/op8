/***************************************************************
** Copyright (C),  2020,  oplus Mobile Comm Corp.,  Ltd
** File : oplus_aod.c
** Description : oplus aod feature
** Version : 1.0
** Date : 2020/09/24
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  liping-m         2020/09/24        1.0           Build this moudle
******************************************************************/
#include <linux/msm_drm_notify.h>

static BLOCKING_NOTIFIER_HEAD(msm_drm_notifier_list);
/**
 * msm_drm_register_client - register a client notifier
 * @nb: notifier block to callback on events
 *
 * This function registers a notifier callback function
 * to msm_drm_notifier_list, which would be called when
 * received unblank/power down event.
 */
int msm_drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&msm_drm_notifier_list, nb);
}
EXPORT_SYMBOL(msm_drm_register_client);

/**
 * msm_drm_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 *
 * This function unregisters the callback function from
 * msm_drm_notifier_list.
 */
int msm_drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&msm_drm_notifier_list, nb);
}
EXPORT_SYMBOL(msm_drm_unregister_client);

/**
 * msm_drm_notifier_call_chain - notify clients of drm_events
 * @val: event MSM_DRM_EARLY_EVENT_BLANK or MSM_DRM_EVENT_BLANK
 * @v: notifier data, inculde display id and display blank
 *     event(unblank or power down).
 */
int msm_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&msm_drm_notifier_list, val, v);
}
EXPORT_SYMBOL(msm_drm_notifier_call_chain);
