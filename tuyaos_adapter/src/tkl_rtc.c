/*****************************************************************************//**
 * @file tkl_rtc.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------

#include "tuya_error_code.h"
#include "tkl_rtc.h"
#include "tkl_rtc_range.h"
#include "tkl_log.h"

#include "sl_si91x_calendar.h"

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static bool g_rtc_inited = false;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief rtc init
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_rtc_init(void)
{
    sl_status_t status;

    if (g_rtc_inited) {
        return OPRT_OK;
    }

    /* Powers up the NPSS RTC block; must precede any calendar register access. */
    sl_si91x_calendar_init();

    /*
     * RC clock, not RO: the 32 kHz RC oscillator is the one always present on
     * this part. Picking RO here would silently stall the counter on boards
     * that do not fit the external crystal, and the failure looks like "time
     * never advances" rather than an error at init.
     */
    status = sl_si91x_calendar_set_configuration(KHZ_RC_CLK_SEL);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("calendar_set_configuration failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    sl_si91x_calendar_rtc_start();

    g_rtc_inited = true;
    return OPRT_OK;
}

/**
 * @brief rtc deinit
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_rtc_deinit(void)
{
    if (!g_rtc_inited) {
        return OPRT_OK;
    }

    sl_si91x_calendar_rtc_stop();
    sl_si91x_calendar_deinit();

    g_rtc_inited = false;
    return OPRT_OK;
}

/**
 * @brief rtc time set
 *
 * @param[in] time_sec: seconds since the 1970 Unix epoch, UTC
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_rtc_time_set(TIME_T time_sec)
{
    sl_calendar_datetime_config_t datetime;
    sl_status_t                   status;

    if (!g_rtc_inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /*
     * Checked here rather than left to the SDK: TIME_T is 32-bit unsigned, so a
     * caller can hand us a value above the converter's 0x7FFFFFFF cap. See
     * tkl_rtc_range.h for why that cap is the binding one.
     */
    if (!tkl_rtc_unix_in_range((uint32_t)time_sec)) {
        TKL_LOGE("rtc time %lu out of range (max %lu)", (unsigned long)time_sec,
                 (unsigned long)TKL_RTC_UNIX_MAX);
        return OPRT_INVALID_PARM;
    }

    status = sl_si91x_calendar_convert_unix_time_to_calendar_datetime((uint32_t)time_sec, &datetime);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("unix->calendar failed 0x%lx", (unsigned long)status);
        return OPRT_INVALID_PARM;
    }

    status = sl_si91x_calendar_set_date_time(&datetime);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("calendar_set_date_time failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief rtc time get
 *
 * @param[out] time_sec: seconds since the 1970 Unix epoch, UTC
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_rtc_time_get(TIME_T *time_sec)
{
    sl_calendar_datetime_config_t datetime;
    uint32_t                      unix_sec = 0;
    sl_status_t                   status;

    if (time_sec == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (!g_rtc_inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    status = sl_si91x_calendar_get_date_time(&datetime);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("calendar_get_date_time failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_calendar_convert_calendar_datetime_to_unix_time(&datetime, &unix_sec);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("calendar->unix failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    *time_sec = (TIME_T)unix_sec;
    return OPRT_OK;
}
