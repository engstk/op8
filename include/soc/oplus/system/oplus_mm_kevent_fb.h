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

#define MAX_PAYLOAD_DATASIZE (512)
#define MM_KEVENT_MAX_PAYLOAD_SIZE MAX_PAYLOAD_DATASIZE
#define MAX_FUNC_LINE_SIZE (128)

enum {
	MM_FB_KEY_RATELIMIT_NONE = 0,
	MM_FB_KEY_RATELIMIT_1MIN = 60 * 1000,
	MM_FB_KEY_RATELIMIT_5MIN = 60 * 5 * 1000,
	MM_FB_KEY_RATELIMIT_30MIN = 60 * 30 * 1000,
	MM_FB_KEY_RATELIMIT_1H = MM_FB_KEY_RATELIMIT_30MIN * 2,
	MM_FB_KEY_RATELIMIT_1DAY = MM_FB_KEY_RATELIMIT_1H * 24,
};
#define FEEDBACK_DELAY_60S 60
#define FEEDBACK_DELAY_MAX_3600S 3600

#define OPLUS_FB_ADSP_CRASH_RATELIMIT (60 * 5 * 1000) /*ms, for mtk*/

enum OPLUS_MM_DIRVER_FB_EVENT_MODULE {
	OPLUS_MM_DIRVER_FB_EVENT_DISPLAY = 0,
	OPLUS_MM_DIRVER_FB_EVENT_AUDIO
};

/*------- multimedia bigdata feedback event id, start ------------ */
#define OPLUS_AUDIO_EVENTID_ADSP_CRASH (10001)
#define OPLUS_AUDIO_EVENTID_HEADSET_DET (10009)
#define OPLUS_AUDIO_EVENTID_ADSP_RECOVERY_FAIL (10045)

#define OPLUS_DISPLAY_EVENTID_DRIVER_ERR (12002)

#define OPLUS_DISPLAY_EVENTID_GPU_FAULT (12005)

/*this id just for test or debug */
#define OPLUS_MM_EVENTID_TEST_OR_DEBUG (30000)

#define OPLUS_MM_EVENTID_MAX (39999)
/*------- multimedia bigdata feedback event id, end ------------*/

/* feedback level */
#define FB_NONE 0 /* for none exception feedback */
#define FB_LOW 1
#define FB_HIGH 2
#define FB_ERROR 3 /* default is error */
#define FB_FATAL 4 /* fatal level must feedback */
#define FB_TEST 5 /* test level feedback not delay */
#define FB_LEVEL_MAX 6

typedef struct mm_fb_param_t {
	enum OPLUS_MM_DIRVER_FB_EVENT_MODULE module;
	unsigned int event_id;
	unsigned int level;
	unsigned int delay_s;
	unsigned int limit_ms;
	char fn_ln[MAX_FUNC_LINE_SIZE];
	char payload[MAX_PAYLOAD_DATASIZE];
} mm_fb_param;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
int upload_mm_fb_kevent_to_atlas_limit(unsigned int event_id,
				       unsigned char *payload, int limit_ms);
int upload_mm_fb_kevent_limit(mm_fb_param *param);

#define mm_fb_audio(id, rate_limit_ms, delay, lv, fmt, ...)                    \
	do {                                                                   \
		mm_fb_param param = {                                          \
			.module = OPLUS_MM_DIRVER_FB_EVENT_AUDIO,              \
			.event_id = id,                                        \
			.limit_ms = rate_limit_ms,                             \
			.delay_s = delay,                                      \
			.level = lv,                                           \
		};                                                             \
		scnprintf(param.fn_ln, MAX_FUNC_LINE_SIZE, "%s(%d)", __func__, \
			  __LINE__);                                           \
		scnprintf(param.payload, sizeof(param.payload), fmt,           \
			  ##__VA_ARGS__);                                      \
		upload_mm_fb_kevent_limit(&param);                             \
	} while (0)

#define mm_fb_audio_fatal(id, rate_limit_ms, fmt, ...) \
	mm_fb_audio(id, rate_limit_ms, 0, FB_FATAL, fmt, ##__VA_ARGS__);

#define mm_fb_audio_fatal_delay(id, rate_limit_ms, delay, fmt, ...) \
	mm_fb_audio(id, rate_limit_ms, delay, FB_FATAL, fmt, ##__VA_ARGS__);

#define mm_fb_audio_kevent_named_delay(id, rate_limit_ms, delay, fmt, ...) \
	mm_fb_audio(id, rate_limit_ms, delay, FB_ERROR, fmt, ##__VA_ARGS__);

#define mm_fb_kevent(m, id, name, rate_limit_ms, fmt, ...)               \
	do {                                                             \
		mm_fb_param param = {                                    \
			.module = m,                                     \
			.event_id = id,                                  \
			.limit_ms = rate_limit_ms,                       \
			.delay_s = 0,                                    \
			.level = FB_ERROR,                               \
		};                                                       \
		scnprintf(param.fn_ln, sizeof(param.fn_ln), "%s", name); \
		scnprintf(param.payload, sizeof(param.payload), fmt,     \
			  ##__VA_ARGS__);                                \
		upload_mm_fb_kevent_limit(&param);                       \
	} while (0)

#define mm_fb_display_kevent(name, rate_limit_ms, fmt, ...)                 \
	mm_fb_kevent(OPLUS_MM_DIRVER_FB_EVENT_DISPLAY,                      \
		     OPLUS_DISPLAY_EVENTID_DRIVER_ERR, name, rate_limit_ms, \
		     fmt, ##__VA_ARGS__)

#define mm_fb_display_kevent_named(rate_limit_ms, fmt, ...)                    \
	do {                                                                   \
		char name[MAX_FUNC_LINE_SIZE];                                 \
		scnprintf(name, sizeof(name), "%s:%d", __func__, __LINE__);    \
		mm_fb_display_kevent(name, rate_limit_ms, fmt, ##__VA_ARGS__); \
	} while (0)

#define mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt, ...)  \
	mm_fb_kevent(OPLUS_MM_DIRVER_FB_EVENT_AUDIO, event_id, name, \
		     rate_limit_ms, fmt, ##__VA_ARGS__)

#define mm_fb_audio_kevent_named(event_id, rate_limit_ms, fmt, ...)         \
	do {                                                                \
		char name[MAX_FUNC_LINE_SIZE];                              \
		scnprintf(name, sizeof(name), "%s:%d", __func__, __LINE__); \
		mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt,      \
				   ##__VA_ARGS__);                          \
	} while (0)

int mm_fb_kevent_init(void);
void mm_fb_kevent_deinit(void);

#else /*CONFIG_OPLUS_FEATURE_MM_FEEDBACK*/
#define upload_mm_fb_kevent_to_atlas_limit(event_id, payload, limit_ms) (0)
#define upload_mm_fb_kevent_limit(param) (0)
#define mm_fb_audio(id, rate_limit_ms, delay, lv, fmt, ...) \
	do {                                                \
	} while (0)
#define mm_fb_audio_fatal(id, rate_limit_ms, fmt, ...) \
	do {                                           \
	} while (0)
#define mm_fb_audio_fatal_delay(id, rate_limit_ms, delay, fmt, ...) \
	do {                                                        \
	} while (0)
#define audio_feedback_test(id, version_str, fmt, ...) \
	do {                                           \
	} while (0)
#define mm_fb_audio_kevent_named_delay(id, rate_limit_ms, delay, fmt, ...) \
	do {                                                               \
	} while (0)
#define mm_fb_kevent(m, name, rate_limit_ms, fmt, ...) \
	do {                                           \
	} while (0)
#define mm_fb_display_kevent(name, rate_limit_ms, fmt, ...) \
	do {                                                \
	} while (0)
#define mm_fb_display_kevent_named(rate_limit_ms, fmt, ...) \
	do {                                                \
	} while (0)
#define mm_fb_audio_kevent(event_id, name, rate_limit_ms, fmt, ...) \
	do {                                                        \
	} while (0)
#define mm_fb_audio_kevent_named(event_id, rate_limit_ms, fmt, ...) \
	do {                                                        \
	} while (0)
#endif /*CONFIG_OPLUS_FEATURE_MM_FEEDBACK*/

#endif /* _OPLUS_MM_KEVENT_FB_ */
