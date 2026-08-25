/*****************************************************************************//**
 * @file tkl_thread.c
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
#include <stdio.h>
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "sl_tuya_log.h"
#include "tkl_thread.h"
#include "tuya_error_code.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

typedef struct {
    char    *name;
    uint16_t stack_size;
} thread_info_t;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief Create thread
 *
 * @param[out] thread: thread handle
 * @param[in] name: thread name
 * @param[in] stack_size: stack size of thread
 * @param[in] priority: priority of thread,please ref to tkl thread priority define in tuya_cloud_types.h
 * @param[in] func: the main thread process function
 * @param[in] arg: the args of the func, can be null
 *
 * @note This API is used for creating thread.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_create(TKL_THREAD_HANDLE *thread, const char *name, uint32_t stack_size, uint32_t priority,
                              const THREAD_FUNC_T func, void *const arg)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret         = 0;
    uint32_t   _stack_size = stack_size;

    thread_info_t thread_cfg[] = {
        {.name = "cli", .stack_size = 3072},         //"cli"
        {.name = "wq_", .stack_size = 4096},         //"wq_highpri"
        {.name = "hea", .stack_size = 3072},         //"health_monitor"
        {.name = "TUY", .stack_size = 4096},         //"TUYA_TCPIP"
        {.name = "lan", .stack_size = 4096},         //"lan_sock_loop"
        {.name = "but", .stack_size = 2560},         //"button_scan"
        {.name = "hos", .stack_size = 5112},         //"host_main_thre"
        {.name = "tuy", .stack_size = 5112},         //"tuya_app_main"
        {.name = "sys", .stack_size = 4096},         //"sys_timer"
        {.name = "wq_", .stack_size = 4096},         //"wq_system"
        {.name = "vad", .stack_size = 3072},         //"vad"
        {.name = "ai_", .stack_size = 4096},         //"ai_player"
        {.name = "audio_input", .stack_size = 4096}, //"audio_input"
        {.name = "audio_cloud", .stack_size = 4096}, //"audio_cloud_as"
        {.name = NULL, .stack_size = 0},
    };

    /* Workaround: Override stack size */
    for (thread_info_t *thr = thread_cfg; thr->name != NULL; thr++) {
        if (memcmp(thr->name, name, strlen(thr->name)) == 0) {
            stack_size = thr->stack_size;
            break;
        }
    }

    TKL_LOGI("++ Thread: %s, stack: %ld -> %ld [%d]", name, _stack_size, stack_size, xPortGetFreeHeapSize());
    ret = xTaskCreate(func, name, stack_size / sizeof(portSTACK_TYPE), arg, priority + osPriorityNormal,
                      (TaskHandle_t *const)thread);
    if (ret != pdPASS) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Terminal thread and release thread resources
 *
 * @param[in] thread: thread handle
 *
 * @note This API is used to terminal thread and release thread resources.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_release(const TKL_THREAD_HANDLE thread)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    vTaskDelete((TaskHandle_t)thread);

    return OPRT_OK;
}

/**
 * @brief Get the thread stack's watermark
 *
 * @param[in] thread: thread handle
 * @param[out] watermark: watermark in Bytes
 *
 * @note This API is used to get the thread stack's watermark.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_get_watermark(const TKL_THREAD_HANDLE thread, uint32_t *watermark)
{
    if (NULL == thread || NULL == watermark) {
        return OPRT_INVALID_PARM;
    }
    *watermark = uxTaskGetStackHighWaterMark(thread) * sizeof(StackType_t);
    return OPRT_OK;
}

/**
 * @brief Get the thread thread handle
 *
 * @param[out] thread: thread handle
 *
 * @note This API is used to get the thread handle.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_get_id(TKL_THREAD_HANDLE *thread)
{
    *thread = (TKL_THREAD_HANDLE)xTaskGetCurrentTaskHandle();
    return OPRT_OK;
}

/**
 * @brief Set name of self thread
 *
 * @param[in] name: thread name
 *
 * @note This API is used to set name of self thread.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_set_self_name(const char *name)
{
    if (!name) {
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Check thread is self thread
 *
 * @param[in] thread: thread handle
 * @param[out] is_self: is self thread or not
 *
 * @note This API is used to check thread is self thread.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_is_self(TKL_THREAD_HANDLE thread, BOOL_T *is_self)
{
    if (NULL == thread || NULL == is_self) {
        return OPRT_INVALID_PARM;
    }

    TKL_THREAD_HANDLE self_handle = (TKL_THREAD_HANDLE)xTaskGetCurrentTaskHandle();
    if (NULL == self_handle) {
        return OPRT_OS_ADAPTER_THRD_JUDGE_SELF_FAILED;
    }

    *is_self = (thread == self_handle);

    return OPRT_OK;
}

/**
 * @brief Get thread priority
 *
 * @param[in] thread: thread handle, If NULL indicates the current thread
 * @param[in] priority: thread priority
 *
 * @note This API is used to get thread priority.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_get_priority(TKL_THREAD_HANDLE thread, int *priority)
{
    if (NULL == thread || NULL == priority) {
        return OPRT_INVALID_PARM;
    }

    *priority = (uint32_t)uxTaskPriorityGet((TaskHandle_t)thread);

    return OPRT_OK;
}

/**
 * @brief Set thread priority
 *
 * @param[in] thread: thread handle, If NULL indicates the current thread
 * @param[in] priority: thread priority
 *
 * @note This API is used to Set thread priority.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_set_priority(TKL_THREAD_HANDLE thread, int priority)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    vTaskPrioritySet((TaskHandle_t)thread, priority);

    return OPRT_OK;
}

/**
 * @brief Diagnose the thread(dump task stack, etc.)
 *
 * @param[in] thread: thread handle
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_thread_diagnose(TKL_THREAD_HANDLE thread)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}
