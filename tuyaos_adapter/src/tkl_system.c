/*****************************************************************************//**
 * @file tkl_system.c
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
#include <stdlib.h>

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include "tuya_error_code.h"
#include "tkl_system.h"
#include "sl_tuya_system.h"
#include "tkl_timer.h"
#include "sl_tuya_log.h"
#include "sl_core.h"
#include "sl_si91x_hal_soc_soft_reset.h"

#if TKL_ULP_TIMER_SYSTICK_ENABLE

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define SL_SYSTICK_MS_TIMER       TUYA_TIMER_NUM_0
#define SL_SYSTICK_MS_TIMER_COUNT 100000000 /* 100000ms (100s) */
#define SL_SYSTICK_MS_STEP        100000    /* 100000ms (100s) */

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

volatile SYS_TIME_T g_mstick = 0;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

RAMFUNC static void sl_systick_handle(void *args)
{
    TKL_UNUSED(args);

    g_mstick += SL_SYSTICK_MS_STEP;
}

void sl_tuya_system_timer_init(void)
{
    TUYA_TIMER_BASE_CFG_T sg_timer_cfg = {.mode = TUYA_TIMER_MODE_PERIOD, .args = NULL, .cb = sl_systick_handle};

    tkl_timer_init(SL_SYSTICK_MS_TIMER, &sg_timer_cfg);
    tkl_timer_start(SL_SYSTICK_MS_TIMER, SL_SYSTICK_MS_TIMER_COUNT);
}

#endif /* TKL_ULP_TIMER_SYSTICK_ENABLE */

/**
 * @brief system reset
 *
 * @param none
 *
 * @return none
 */
void tkl_system_reset(void)
{
    sl_si91x_soc_nvic_reset();
}

/**
 * @brief Get system tick count
 *
 * @param none
 *
 * @return system tick count
 */
SYS_TICK_T tkl_system_get_tick_count(void)
{
    return (SYS_TICK_T)xTaskGetTickCount();
}

/**
 * @brief Get system millisecond
 *
 * @param none
 *
 * @return system millisecond
 */
SYS_TIME_T tkl_system_get_millisecond(void)
{
#if TKL_ULP_TIMER_SYSTICK_ENABLE
    uint32_t usec = 0;

    tkl_timer_get(SL_SYSTICK_MS_TIMER, &usec);

    return g_mstick + (usec / 1000);
#else
    return (SYS_TIME_T)pdTICKS_TO_MS(tkl_system_get_tick_count());
#endif
}

/* Strong runtime stats counter for FreeRTOS: returns milliseconds */
uint32_t ulGetRunTimeCounterValue(void)
{
    return (uint32_t)tkl_system_get_millisecond();
}

/**
 * @brief Get system random data
 *
 * @param[in] range: random from 0  to range
 *
 * @return random value
 */
int tkl_system_get_random(uint32_t range)
{
    if (range == 0) {
        return 0;
    }

    return rand() % range;
}

/**
 * @brief Get system reset reason
 *
 * @param[in] describe: point to reset reason describe
 *
 * @return reset reason
 */
TUYA_RESET_REASON_E tkl_system_get_reset_reason(char **describe)
{
    TKL_UNUSED(describe);

    return 0;
}

/**
 * @brief  system sleep
 *
 * @param[in] describe: num ms
 *
 * @return none
 */
void tkl_system_sleep(uint32_t num_ms)
{
    uint32_t xTicksToDelay;

    if (num_ms < 10) {
        num_ms = 10;
    }

    xTicksToDelay = pdMS_TO_TICKS(num_ms);
    vTaskDelay(xTicksToDelay);
}

/**
 * @brief system delay
 *
 * @param[in] msTime: time in MS
 *
 * @note This API is used for system sleep.
 *
 * @return void
 */
void tkl_system_delay(uint32_t num_ms)
{
    tkl_system_sleep(num_ms);
}

/**
 * @brief get system cpu info
 *
 * @param[in] cpu_ary: info of cpus
 * @param[in] cpu_cnt: num of cpu
 * @note This API is used for system cpu info get.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_system_get_cpu_info(TUYA_CPU_INFO_T **cpu_ary, int *cpu_cnt)
{
    TKL_UNUSED(cpu_ary);
    TKL_UNUSED(cpu_cnt);

    return OPRT_NOT_SUPPORTED;
}

void sl_tuya_system_print_task_stats(void)
{
    static TaskStatus_t *s_prev       = NULL;
    static UBaseType_t   s_prev_count = 0;
    static uint32_t      s_prev_total = 0;

    TaskStatus_t *curr_tasks;
    UBaseType_t   curr_count;
    uint32_t      curr_total;

    char status_char;

    /* Get current number of tasks */
    curr_count = uxTaskGetNumberOfTasks();
    curr_tasks = pvPortMalloc(curr_count * sizeof(TaskStatus_t));
    if (curr_tasks == NULL) {
        return;
    }

    /* Get information of all tasks */
    curr_count = uxTaskGetSystemState(curr_tasks, curr_count, &curr_total);

    /* First time: only save snapshot, do not print */
    if (s_prev == NULL) {
        s_prev       = curr_tasks;
        s_prev_count = curr_count;
        s_prev_total = curr_total;
        return;
    }

    /* From second time onwards: print by time interval (delta) */
    if (curr_total > 0 && curr_total >= s_prev_total) {
        uint32_t total_delta = curr_total - s_prev_total;
        if (total_delta == 0) {
            /* No useful delta */
            vPortFree(s_prev);
            s_prev       = curr_tasks;
            s_prev_count = curr_count;
            s_prev_total = curr_total;
            return;
        }

        TKL_LOGI("==== Task CPU Usage (Last ~%lu ms) =====", (unsigned long)total_delta);
        TKL_LOGI("Task Name       State  Priority  Stack  CPU(z)  CPUΔ(ms)");
        TKL_LOGI("-------------------------------------------------------------");

        for (UBaseType_t i = 0; i < curr_count; i++) {
            /* Calculate character status */
            switch (curr_tasks[i].eCurrentState) {
            case eRunning:
                status_char = 'X';
                break;
            case eReady:
                status_char = 'R';
                break;
            case eBlocked:
                status_char = 'B';
                break;
            case eSuspended:
                status_char = 'S';
                break;
            case eDeleted:
                status_char = 'D';
                break;
            default:
                status_char = '?';
                break;
            }

            /* Find previous runtime by handle */
            uint32_t prev_run = 0;
            for (UBaseType_t j = 0; j < s_prev_count; j++) {
                if (curr_tasks[i].xHandle == s_prev[j].xHandle) {
                    prev_run = s_prev[j].ulRunTimeCounter;
                    break;
                }
            }

            uint32_t delta_run =
                (curr_tasks[i].ulRunTimeCounter >= prev_run) ? (curr_tasks[i].ulRunTimeCounter - prev_run) : 0;
            uint32_t cpu_pct = (delta_run * 100UL) / total_delta;

            TKL_LOGI("%-16s %c     %2u        %5u   %3u     %lu", curr_tasks[i].pcTaskName, status_char,
                     (unsigned int)curr_tasks[i].uxCurrentPriority, (unsigned int)curr_tasks[i].usStackHighWaterMark,
                     (unsigned int)cpu_pct, (unsigned long)delta_run);
        }

        TKL_LOGI("-------------------------------------------------------------");
        TKL_LOGI("Total Runtime (Δ): %lu ms", (unsigned long)total_delta);
    }

    /* Update snapshot for next call */
    if (s_prev) {
        vPortFree(s_prev);
    }
    s_prev       = curr_tasks;
    s_prev_count = curr_count;
    s_prev_total = curr_total;
}
