/*****************************************************************************//**
 * @file tkl_mutex.c
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
#include "tkl_mutex.h"
#include "tuya_error_code.h"
#include "cmsis_os2.h"

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief Create mutex
 *
 * @param[out] pMutexHandle: mutex handle
 *
 * @note This API is used to create and init mutex.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_mutex_create_init(TKL_MUTEX_HANDLE *handle)
{
    static const osMutexAttr_t attr = {
        .attr_bits = osMutexRecursive,
    };

    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    *handle = (TKL_MUTEX_HANDLE)osMutexNew(&attr);
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Lock mutex
 *
 * @param[in] mutexHandle: mutex handle
 *
 * @note This API is used to lock mutex.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_mutex_lock(const TKL_MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    osStatus_t status = osMutexAcquire((osMutexId_t)handle, osWaitForever);
    if (status != osOK) {
        return OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Try Lock mutex
 *
 * @param[in] mutexHandle: mutex handle
 *
 * @note This API is used to try lock mutex.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_mutex_trylock(const TKL_MUTEX_HANDLE handle)
{
    if (NULL == handle) {
        return OPRT_INVALID_PARM;
    }

    osStatus_t status = osMutexAcquire((osMutexId_t)handle, osWaitForever);
    if (status != osOK) {
        return OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Unlock mutex
 *
 * @param[in] mutexHandle: mutex handle
 *
 * @note This API is used to unlock mutex.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_mutex_unlock(const TKL_MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    osStatus_t status = osMutexRelease((osMutexId_t)handle);
    if (status != osOK) {
        return OPRT_OS_ADAPTER_MUTEX_UNLOCK_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Release mutex
 *
 * @param[in] mutexHandle: mutex handle
 *
 * @note This API is used to release mutex.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_mutex_release(const TKL_MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    osMutexDelete((osMutexId_t)handle);

    return OPRT_OK;
}
