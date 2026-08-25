#ifndef _SL_TUYA_LOG_H_
#define _SL_TUYA_LOG_H_

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LEVEL_OVER_LOGN = 0,
    LEVEL_OVER_LOGE = 1,
    LEVEL_OVER_LOGW,
    LEVEL_OVER_LOGI,
    LEVEL_OVER_LOGD,
    LEVEL_OVER_LOGV,
};

#define DEBUG_STR  "%d, %s: "
#define DEBUG_ARGS __LINE__, __FUNCTION__

#ifndef DEBUG_LEVEL
#define TNLOG_DEBUG_LEVEL LEVEL_OVER_LOGD
#else
#define TNLOG_DEBUG_LEVEL LEVEL_OVER_LOGV // DEBUG_LEVEL
#endif

#define SL_TUYA_PRINTF         sl_tuya_printf
#define TKL_LOG_DATA_INDEX 24
#define TKL_FILE_NAME      (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define TKL_LOG(newline, level, levelstr, ...)                                                                         \
    do {                                                                                                               \
        if (level <= (TNLOG_DEBUG_LEVEL)) {                                                                            \
            char loc_str[384];                                                                                         \
            int  loc_sz = 0;                                                                                           \
            if (newline) {                                                                                             \
                loc_sz = snprintf(loc_str, sizeof(loc_str), "[tuyaos][%s][%s:%d", levelstr, TKL_FILE_NAME, __LINE__);  \
                for (size_t i = loc_sz; i < TKL_LOG_DATA_INDEX - 1; i++) {                                             \
                    loc_str[i] = ' ';                                                                                  \
                    loc_sz++;                                                                                          \
                }                                                                                                      \
            }                                                                                                          \
            loc_str[loc_sz++] = ']';                                                                                   \
            loc_str[loc_sz++] = ' ';                                                                                   \
            snprintf(&loc_str[loc_sz], sizeof(loc_str), __VA_ARGS__);                                                  \
            SL_TUYA_PRINTF("%s\r\n", loc_str);                                                                             \
        }                                                                                                              \
    } while (0)

#define TKL_LOGE(...) TKL_LOG(1, LEVEL_OVER_LOGE, "E", __VA_ARGS__)
#define TKL_LOGW(...) TKL_LOG(1, LEVEL_OVER_LOGW, "W", __VA_ARGS__)
#define TKL_LOGI(...) TKL_LOG(1, LEVEL_OVER_LOGI, "I", __VA_ARGS__)
#define TKL_LOGD(...) TKL_LOG(1, LEVEL_OVER_LOGD, "D", __VA_ARGS__)
#define TKL_LOGV(...) TKL_LOG(1, LEVEL_OVER_LOGV, "P", __VA_ARGS__)

typedef int (*log_output_t)(const char *format, ...);

extern log_output_t sl_tuya_printf;

void log_printhex(char *ss, const uint8_t *buffs, int length);

void log_printhex_no_newline(char *ss, const uint8_t *buffs, int length);

void sl_tuya_log_output_set(log_output_t fn);

#ifdef __cplusplus
}
#endif

#endif /* _SL_TUYA_LOG_H_ */
