#ifndef _TKL_LOG_H_
#define _TKL_LOG_H_

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

#define TKL_PRINTF         tkl_printf
#define TKL_LOG_DATA_INDEX 24
#define TKL_FILE_NAME      (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/*
 * loc_sz tracks how much of loc_str is already used, so every later write has
 * to be bounded by what is LEFT, not by sizeof(loc_str): the payload snprintf
 * used to be handed the full buffer size while writing at offset loc_sz, which
 * let a long line run up to ~24 bytes past the end of this stack buffer.
 * snprintf also returns the length it WOULD have written, so loc_sz has to be
 * clamped before it is used as an index.
 */
#define TKL_LOG(newline, level, levelstr, ...)                                                                        \
    do {                                                                                                              \
        if (level <= (TNLOG_DEBUG_LEVEL)) {                                                                           \
            char loc_str[384];                                                                                        \
            int  loc_sz = 0;                                                                                          \
            if (newline) {                                                                                            \
                loc_sz = snprintf(loc_str, sizeof(loc_str), "[tuyaos][%s][%s:%d", levelstr, TKL_FILE_NAME, __LINE__); \
                if (loc_sz < 0 || loc_sz > (int)sizeof(loc_str) - 1) {                                                \
                    loc_sz = (int)sizeof(loc_str) - 1;                                                                \
                }                                                                                                     \
                for (int i = loc_sz; i < TKL_LOG_DATA_INDEX - 1; i++) {                                               \
                    loc_str[i] = ' ';                                                                                 \
                    loc_sz++;                                                                                         \
                }                                                                                                     \
            }                                                                                                         \
            if (loc_sz <= (int)sizeof(loc_str) - 3) {                                                                 \
                loc_str[loc_sz++] = ']';                                                                              \
                loc_str[loc_sz++] = ' ';                                                                              \
            }                                                                                                         \
            loc_str[loc_sz] = '\0';                                                                                   \
            snprintf(&loc_str[loc_sz], sizeof(loc_str) - (size_t)loc_sz, __VA_ARGS__);                                \
            TKL_PRINTF("%s\r\n", loc_str);                                                                            \
        }                                                                                                             \
    } while (0)

#define TKL_LOGE(...) TKL_LOG(1, LEVEL_OVER_LOGE, "E", __VA_ARGS__)
#define TKL_LOGW(...) TKL_LOG(1, LEVEL_OVER_LOGW, "W", __VA_ARGS__)
#define TKL_LOGI(...) TKL_LOG(1, LEVEL_OVER_LOGI, "I", __VA_ARGS__)
#define TKL_LOGD(...) TKL_LOG(1, LEVEL_OVER_LOGD, "D", __VA_ARGS__)
#define TKL_LOGV(...) TKL_LOG(1, LEVEL_OVER_LOGV, "P", __VA_ARGS__)

typedef int (*log_output_t)(const char *format, ...);

extern log_output_t tkl_printf;

void log_printhex(char *ss, const uint8_t *buffs, int length);

void log_printhex_no_newline(char *ss, const uint8_t *buffs, int length);

void tkl_log_output_set(log_output_t fn);

#ifdef __cplusplus
}
#endif

#endif /* _TKL_LOG_H_ */
