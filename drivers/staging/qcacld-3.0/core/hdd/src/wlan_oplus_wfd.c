/******************************************************************************
** Copyright (C), 2019-2029, OPLUS Mobile Comm Corp., Ltd
** VENDOR_EDIT, All rights reserved.
** File: - wlan_oplus_wfd.c
** Description: wlan oplus wfd
**
** Version: 1.0
** Date : 2020/07/27
** TAG: OPLUS_FEATURE_WIFI_CAPCENTER
** ------------------------------- Revision History: ----------------------------
** <author>                                <data>        <version>       <desc>
** ------------------------------------------------------------------------------
 *******************************************************************************/
#ifdef OPLUS_FEATURE_WIFI_OPLUSWFD
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wireless.h>
#include <linux/list.h>
#include "osif_sync.h"
#include <wlan_hdd_includes.h>
#include <net/arp.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/nl80211.h>
#include <linux/spinlock.h>
#include "wlan_hdd_main.h"
#include <net/oplus/oplus_wfd_wlan.h>
#include <linux/preempt.h>
#define LOG_TAG "[oplus_wfd_qcom] %s line:%d "
#define debug(fmt, args...) printk(LOG_TAG fmt, __FUNCTION__, __LINE__, ##args)

#define BIT_SUPPORTED_PHY_NO_HT     1
#define BIT_SUPPORTED_PHY_HT        2
#define BIT_SUPPORTED_PHY_VHT       4
#define BIT_SUPPORTED_PHY_HE        8

#define BAND_2G  0
#define BAND_5G  1

static int is_remove_He_ie_from_prebe_request = 0;
static struct hdd_context *s_hdd_ctx = NULL;

int oplus_wfd_get_remove_He_ie_flag(void);
void oplus_wfd_set_hdd_ctx(struct hdd_context *hdd_ctx);
void oplus_register_oplus_wfd_wlan_ops_qcom(void);

static void remove_he_ie_from_probe_request(int remove);
static int get_dbs_capacity(void);
static int get_phy_capacity(int band);
static void get_supported_channels(int band, int *len, int *freqs, int max_num);
static void get_avoid_channels(int *len, int* freqs, int max_num);

static struct oplus_wfd_wlan_ops_t oplus_wfd_wlan_ops_qcom = {
    .remove_he_ie_from_probe_request = remove_he_ie_from_probe_request,
    .get_dbs_capacity = get_dbs_capacity,
    .get_phy_capacity = get_phy_capacity,
    .get_supported_channels = get_supported_channels,
    .get_avoid_channels = get_avoid_channels,
};

static void remove_he_ie_from_probe_request(int remove) {
    is_remove_He_ie_from_prebe_request = remove;
    debug("set remove he ie =%d", remove);
}

static struct wireless_dev * get_wdev_sta(void)
{
    struct wireless_dev *wdev = NULL;

    if (s_hdd_ctx != NULL && s_hdd_ctx->wiphy && s_hdd_ctx->wiphy->registered) {
        list_for_each_entry(wdev, &s_hdd_ctx->wiphy->wdev_list, list) {
            if (wdev->iftype & NL80211_IFTYPE_STATION) {
                return wdev;
            }
        }
    }
    return NULL;
}
static int get_dbs_capacity(void)
{
    #define DBS_UNKNOWN 0
    #define DBS_NULL 1
    #define DBS_SISO 2
    #define DBS_MIMO 3
    int cap = DBS_UNKNOWN;
    int errno;

    struct osif_vdev_sync *vdev_sync;
    struct wireless_dev *wdev_sta = NULL;

    rtnl_lock();
    wdev_sta = get_wdev_sta();

    if (wdev_sta) {
        errno = osif_vdev_sync_op_start(wdev_sta->netdev, &vdev_sync);
        if (errno) {
            rtnl_unlock();
            return cap;
        }

        if (policy_mgr_is_hw_dbs_capable(s_hdd_ctx->psoc)) {
            if (policy_mgr_is_hw_dbs_2x2_capable(s_hdd_ctx->psoc)) {
                cap = DBS_MIMO;
            } else {
                cap = DBS_SISO;
            }
        } else {
            cap = DBS_NULL;
        }

        osif_vdev_sync_op_stop(vdev_sync);
    } else {
        debug("wdev_sta is null, donothing");
    }

    rtnl_unlock();
    return cap;
}

static int get_phy_capacity(int band)
{
    int phy_bit = 0;
    int errno;
    struct ieee80211_supported_band *band_supported;

    struct osif_vdev_sync *vdev_sync;
    struct wireless_dev *wdev_sta = NULL;

    rtnl_lock();
    wdev_sta = get_wdev_sta();

    if (wdev_sta) {
        errno = osif_vdev_sync_op_start(wdev_sta->netdev, &vdev_sync);
        if (errno) {
            rtnl_unlock();
            return phy_bit;
        }

        if (s_hdd_ctx->wiphy == NULL || s_hdd_ctx->wiphy->bands[NL80211_BAND_2GHZ] == NULL
            || s_hdd_ctx->wiphy->bands[NL80211_BAND_5GHZ] == NULL) {
            osif_vdev_sync_op_stop(vdev_sync);
            rtnl_unlock();
            debug("fail #1");
            return phy_bit;
        }

        if (band == BAND_2G) {
            band_supported = s_hdd_ctx->wiphy->bands[NL80211_BAND_2GHZ];
        } else if (band == BAND_5G) {
            band_supported = s_hdd_ctx->wiphy->bands[NL80211_BAND_5GHZ];
        } else {
            band_supported = NULL;
        }

        if (band_supported != NULL) {
            if (band_supported->iftype_data != NULL && band_supported->iftype_data->he_cap.has_he) {
                phy_bit |= BIT_SUPPORTED_PHY_HE;
                phy_bit |= BIT_SUPPORTED_PHY_VHT;
                phy_bit |= BIT_SUPPORTED_PHY_HT;
            } else if (band_supported->vht_cap.vht_supported) {
                phy_bit |= BIT_SUPPORTED_PHY_VHT;
                phy_bit |= BIT_SUPPORTED_PHY_HT;
            } else if (band_supported->ht_cap.ht_supported) {
                phy_bit |= BIT_SUPPORTED_PHY_HT;
            } else {
                phy_bit |= BIT_SUPPORTED_PHY_NO_HT;
            }
        }
        osif_vdev_sync_op_stop(vdev_sync);
    } else {
        debug("wdev_sta is null, donothing");
    }
    rtnl_unlock();
    return phy_bit;
}

static void get_supported_channels(int band, int* out_len, int* out_freqs, int max_num)
{
    int errno;
    int index;
    int len = 0;
    struct ieee80211_supported_band *band_supported;
    struct ieee80211_channel *chan;

    struct osif_vdev_sync *vdev_sync;
    struct wireless_dev *wdev_sta = NULL;

    *out_len = 0;
    rtnl_lock();
    wdev_sta = get_wdev_sta();

    if (wdev_sta) {
        errno = osif_vdev_sync_op_start(wdev_sta->netdev, &vdev_sync);
        if (errno) {
            rtnl_unlock();
            return;
        }
        if (s_hdd_ctx->wiphy == NULL || s_hdd_ctx->wiphy->bands[NL80211_BAND_2GHZ] == NULL
            || s_hdd_ctx->wiphy->bands[NL80211_BAND_5GHZ] == NULL) {
            osif_vdev_sync_op_stop(vdev_sync);
            rtnl_unlock();
            debug("fail #1");
            return;
        }
        if (band == BAND_2G) {
            band_supported = s_hdd_ctx->wiphy->bands[NL80211_BAND_2GHZ];
        } else if (band == BAND_5G) {
            band_supported = s_hdd_ctx->wiphy->bands[NL80211_BAND_5GHZ];
        } else {
            band_supported = NULL;
        }

        if (band_supported != NULL) {
            for (index = 0; index < band_supported->n_channels; index++) {
                chan = &band_supported->channels[index];
                if (chan->flags & (IEEE80211_CHAN_DISABLED|IEEE80211_CHAN_NO_IR|IEEE80211_CHAN_RADAR)) {
                    continue;
                }
                if (len < max_num) {
                    out_freqs[len++] = chan->center_freq;
                }
            }
            *out_len = len;
        }
        osif_vdev_sync_op_stop(vdev_sync);
    } else {
        debug("wdev_sta is null, donothing");
    }
    rtnl_unlock();
}

static void get_avoid_channels(int* out_len, int* out_freqs, int max_num)
{
    int errno;
    int index;
    int freq;
    int len = 0;
    struct osif_vdev_sync *vdev_sync;
    struct wireless_dev *wdev_sta = NULL;

    *out_len = 0;
    rtnl_lock();
    wdev_sta = get_wdev_sta();

    if (wdev_sta) {
        errno = osif_vdev_sync_op_start(wdev_sta->netdev, &vdev_sync);
        if (errno) {
            rtnl_unlock();
            return;
        }
        if (s_hdd_ctx->wiphy == NULL || s_hdd_ctx->wiphy->bands[NL80211_BAND_2GHZ] == NULL
            || s_hdd_ctx->wiphy->bands[NL80211_BAND_5GHZ] == NULL) {
            osif_vdev_sync_op_stop(vdev_sync);
            rtnl_unlock();
            debug("fail #1");
            return;
        }

        for (index = 0; index < s_hdd_ctx->unsafe_channel_count && index < NUM_CHANNELS; index++) {
            /*
            if (s_hdd_ctx->unsafe_channel_list[index] <= 14) {
            	band = NL80211_BAND_2GHZ;
            } else {
            	band = NL80211_BAND_5GHZ;
            }
            freq = ieee80211_channel_to_frequency(s_hdd_ctx->unsafe_channel_list[index], band);
            if (freq <=0 ) {
            	continue;
            }
            */
            freq = s_hdd_ctx->unsafe_channel_list[index];
            if (len < max_num) {
                out_freqs[len++] = freq;
            }
        }
        *out_len = len;
        osif_vdev_sync_op_stop(vdev_sync);
    } else {
        debug("wdev_sta is null, donothing");
    }
    rtnl_unlock();
}

/*************public begin********************/
int oplus_wfd_get_remove_He_ie_flag(void)
{
    return is_remove_He_ie_from_prebe_request;
}

void oplus_wfd_set_hdd_ctx(struct hdd_context *hdd_ctx)
{
    debug("hdd_ctx =%p", hdd_ctx);
    s_hdd_ctx = hdd_ctx;
}

void oplus_register_oplus_wfd_wlan_ops_qcom(void)
{
    register_oplus_wfd_wlan_ops(&oplus_wfd_wlan_ops_qcom);
}

/*************public end********************/
#endif
