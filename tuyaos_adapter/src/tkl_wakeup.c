/**
 * @file tkl_wakeup.c
 * @brief Wakeup source record for SiWx917 deep sleep (Phase 3 slice 1).
 *
 * Validates and records sources; real NPSS/power-manager arming lands
 * with the PS0 integration. Until then DEEP_SLEEP entry stays refused
 * in tkl_cpu_sleep_mode_set, so recording is inert and safe.
 */

// --- BEGIN: user defines and implements ---
#include "tkl_wakeup.h"
#include "tuya_error_code.h"

static TUYA_WAKEUP_SOURCE_BASE_CFG_T s_records[TKL_WAKEUP_MAX_RECORDS];
static uint8_t                       s_record_cnt = 0;

static BOOL_T __wakeup_gpio_ok(TUYA_GPIO_NUM_E gpio_num)
{
    return (gpio_num <= TUYA_GPIO_NUM_4) ? TRUE : FALSE;
}

static BOOL_T __wakeup_same(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *a,
                            const TUYA_WAKEUP_SOURCE_BASE_CFG_T *b)
{
    uint8_t i;

    if (a->source != b->source) {
        return FALSE;
    }
    if (TUYA_WAKEUP_SOURCE_GPIO == a->source) {
        return (a->wakeup_para.gpio_param.gpio_num == b->wakeup_para.gpio_param.gpio_num) ? TRUE : FALSE;
    }
    if (TUYA_WAKEUP_SOURCE_TIMER == a->source) {
        return (a->wakeup_para.timer_param.timer_num == b->wakeup_para.timer_param.timer_num) ? TRUE : FALSE;
    }
    if (TUYA_WAKEUP_SOURCE_RTC == a->source) {
        return (a->wakeup_para.rtc_param.RTC_num == b->wakeup_para.rtc_param.RTC_num) ? TRUE : FALSE;
    }
    return FALSE;
}
// --- END: user defines and implements ---

/**
 * @brief record a wakeup source for deep sleep
 *
 * @param[in] param: wakeup source config
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wakeup_source_set(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param)
{
    uint8_t i;

    // --- BEGIN: user implements ---
    if (NULL == param) {
        return OPRT_INVALID_PARM;
    }
    if (TUYA_WAKEUP_SOURCE_GPIO == param->source) {
        if (!__wakeup_gpio_ok(param->wakeup_para.gpio_param.gpio_num)) {
            return OPRT_INVALID_PARM;
        }
    } else if (TUYA_WAKEUP_SOURCE_TIMER == param->source) {
        if (0 == param->wakeup_para.timer_param.ms) {
            return OPRT_INVALID_PARM;
        }
    } else if (TUYA_WAKEUP_SOURCE_RTC != param->source) {
        return OPRT_INVALID_PARM;
    }
    for (i = 0; i < s_record_cnt; i++) {
        if (__wakeup_same(&s_records[i], param)) {
            s_records[i] = *param;
            return OPRT_OK;
        }
    }
    if (s_record_cnt >= TKL_WAKEUP_MAX_RECORDS) {
        return OPRT_EXCEED_UPPER_LIMIT;
    }
    s_records[s_record_cnt++] = *param;
    return OPRT_OK;
    // --- END: user implements ---
}

/**
 * @brief clear a recorded wakeup source
 *
 * @param[in] param: wakeup source config
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wakeup_source_clear(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param)
{
    uint8_t i;
    uint8_t j;

    // --- BEGIN: user implements ---
    if (NULL == param) {
        return OPRT_INVALID_PARM;
    }
    for (i = 0; i < s_record_cnt; i++) {
        if (__wakeup_same(&s_records[i], param)) {
            for (j = i; j + 1 < s_record_cnt; j++) {
                s_records[j] = s_records[j + 1];
            }
            s_record_cnt--;
            return OPRT_OK;
        }
    }
    return OPRT_NOT_FOUND;
    // --- END: user implements ---
}

/**
 * @brief count recorded wakeup sources
 *
 * @param[in] none
 *
 * @return number of recorded sources
 */
uint8_t tkl_wakeup_record_count(void)
{
    // --- BEGIN: user implements ---
    return s_record_cnt;
    // --- END: user implements ---
}
