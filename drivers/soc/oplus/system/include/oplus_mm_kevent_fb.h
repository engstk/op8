/***************************************************************
** Copyright (C),  2018,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_mm_kevent_fb.h
** Description : MM kevent fb data
** Version : 1.0
** Date : 2018/12/03
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Guo.Ling          2018/12/03        1.0           Build this moudle
**   LiPing-M          2019/01/29        1.1           Add SMMU for QCOM
******************************************************************/
#ifndef _OPLUS_MM_KEVENT_FB_
#define _OPLUS_MM_KEVENT_FB_

#define MAX_PAYLOAD_DATASIZE		 (512)
#define MM_KEVENT_MAX_PAYLOAD_SIZE	 MAX_PAYLOAD_DATASIZE
#define MAX_FUNC_LINE_SIZE	         (128)

enum {
	MM_FB_KEY_RATELIMIT_NONE = 0,
	MM_FB_KEY_RATELIMIT_1MIN = 60 * 1000,
	MM_FB_KEY_RATELIMIT_5MIN = 60 * 5 * 1000,
	MM_FB_KEY_RATELIMIT_30MIN = 60 * 30 * 1000,
	MM_FB_KEY_RATELIMIT_1H = MM_FB_KEY_RATELIMIT_30MIN * 2,
	MM_FB_KEY_RATELIMIT_1DAY = MM_FB_KEY_RATELIMIT_1H * 24,
};
#define FEEDBACK_DELAY_60S                       60

#define OPLUS_FB_ADSP_CRASH_RATELIMIT    (60 * 5 * 1000) /*ms, for mtk*/

enum OPLUS_MM_DIRVER_FB_EVENT_MODULE {
	OPLUS_MM_DIRVER_FB_EVENT_DISPLAY = 0,
	OPLUS_MM_DIRVER_FB_EVENT_AUDIO
};

/*------- multimedia bigdata feedback event id, start ------------ */
#define OPLUS_AUDIO_EVENTID_ADSP_CRASH           (10001)
#define OPLUS_AUDIO_EVENTID_HEADSET_DET          (10009)
#define OPLUS_AUDIO_EVENTID_ADSP_RECOVERY_FAIL   (10045)

#define OPLUS_DISPLAY_EVENTID_DRIVER_ERR         (12002)

#define OPLUS_DISPLAY_EVENTID_GPU_FAULT          (12005)

/*this id just for test or debug */
#define OPLUS_MM_EVENTID_TEST_OR_DEBUG           (30000)
/*------- multimedia bigdata feedback event id, end ------------*/

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
int upload_mm_fb_kevent_to_atlas_limit(unsigned int event_id, unsigned char *payload, int limit_ms);

int upload_mm_fb_kevent_limit(enum OPLUS_MM_DIRVER_FB_EVENT_MODULE module,  unsigned int event_id,
			 const char *name, int rate_limit_ms, unsigned int delay_s, char *payload);

#define mm_fb_audio_kevent_named_delay(event_id, rate_limit_ms, delay_s, fmt, ...) \
do { \
	char name[MAX_PAYLOAD_DATASIZE] = ""; \
	char kv_data[MAX_PAYLOAD_DATASIZE] = ""; \
	scnprintf(name, sizeof(name), "%s:%d", __func__, __LINE__); \
	scnprintf(kv_data, sizeof(kv_data), fmt, ##__VA_ARGS__); \
	upload_mm_fb_kevent_limit(OPLUS_MM_DIRVER_FB_EVENT_AUDIO, event_id, name, rate_limit_ms, delay_s, kv_data); \
} while (0)

#define mm_fb_kevent(m, id, name, rate_limit_ms, fmt, ...) \
	do { \
		char kv_data[MAX_PAYLOAD_DATASIZE] = ""; \
		scnprintf(kv_data, sizeof(kv_data), fmt, ##__VA_ARGS__); \
		upload_mm_fb_kevent_limit(m, id, name, rate_limit_ms, 0, kv_data); \
	} while (0)

#define mm_fb_display_kevent(name, rate_limit_ms, fmt, ...) \
		mm_fb_kevent(OPLUS_MM_DIRVER_FB_EVENT_DISPLAY, OPLUS_DISPLAY_EVENTID_DRIVER_ERR, name, rate_limit_ms, fmt, ##__VA_ARGS__)

#define mm_fb_display_kevent_named(rate_limit_ms, fmt, ...) \
	do { \
		char name[MAX_FUNC_LINE_SIZE]; \
		scnprintf(name, sizeof(name), "%s:%d", __func__, __LINE__); \
		mm_fb_display_kevent(name, rate_limit_ms, fmt, ##__VA_ARGS__); \
	} while (0)

#define mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt, ...) \
		mm_fb_kevent(OPLUS_MM_DIRVER_FB_EVENT_AUDIO, event_id, name, rate_limit_ms, fmt, ##__VA_ARGS__)

#define mm_fb_audio_kevent_named(event_id, rate_limit_ms, fmt, ...) \
	do { \
		char name[MAX_FUNC_LINE_SIZE]; \
		scnprintf(name, sizeof(name), "%s:%d", __func__, __LINE__); \
		mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt, ##__VA_ARGS__); \
	} while (0)

int mm_fb_kevent_init(void);
void mm_fb_kevent_deinit(void);

#else /*CONFIG_OPLUS_FEATURE_MM_FEEDBACK*/
#define upload_mm_fb_kevent_to_atlas_limit(event_id, payload, limit_ms) (0)
#define mm_fb_kevent(m, name, rate_limit_ms, fmt, ...)  do {} while (0)
#define mm_fb_display_kevent(name, rate_limit_ms, fmt, ...)  do {} while (0)
#define mm_fb_display_kevent_named(rate_limit_ms, fmt, ...)  do {} while (0)
#define mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt, ...)  do {} while (0)
#define mm_fb_audio_kevent_named(event_id, rate_limit_ms, fmt, ...)  do {} while (0)
#endif /*CONFIG_OPLUS_FEATURE_MM_FEEDBACK*/

#endif /* _OPLUS_MM_KEVENT_FB_ */

