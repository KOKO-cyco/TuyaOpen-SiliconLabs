/*****************************************************************************//**
 * @file tkl_rtc_range.h
 * @brief Range rules for the SiWx917 calendar RTC, split out so they can be
 *        unit-tested on the host without the Silicon Labs SDK.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __TKL_RTC_RANGE_H__
#define __TKL_RTC_RANGE_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * Two independent limits stack up here, and the tighter one wins:
 *
 *  - The hardware struct (RTC_TIME_CONFIG_T) stores Year as 0..99 plus a
 *    2-bit Century, which the SDK encodes as ((years_since_1900 / 100) % 4) + 1.
 *    That alone would allow 1900..2299.
 *  - sl_si91x_calendar_convert_unix_time_to_calendar_datetime() rejects
 *    anything above TIME_UNIX_TIMESTAMP_MAX (0x7FFFFFFF) and treats the input
 *    as seconds since the 1970 Unix epoch.
 *
 * So the usable window is [0, 0x7FFFFFFF] -- 1970-01-01T00:00:00Z through
 * 2038-01-19T03:14:07Z. TIME_T is a 32-bit unsigned int, so values above the
 * cap are representable by the caller and must be rejected here rather than
 * silently wrapping inside the SDK.
 */
#define TKL_RTC_UNIX_MIN 0x00000000u
#define TKL_RTC_UNIX_MAX 0x7FFFFFFFu

/**
 * @brief Whether a Unix timestamp can be represented by this RTC.
 * @param unix_sec seconds since 1970-01-01T00:00:00Z
 * @return true when the value is inside [TKL_RTC_UNIX_MIN, TKL_RTC_UNIX_MAX]
 */
static inline bool tkl_rtc_unix_in_range(uint32_t unix_sec)
{
    return unix_sec <= TKL_RTC_UNIX_MAX;
}

#endif /* __TKL_RTC_RANGE_H__ */
