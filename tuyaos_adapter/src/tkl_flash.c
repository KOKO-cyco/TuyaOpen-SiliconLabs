/*****************************************************************************//**
 * @file tkl_flash.c
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
#include "sl_si91x_driver.h"
#include "sl_status.h"

#include "tuya_error_code.h"
#include "tkl_flash.h"
#include "tkl_log.h"
#include "tuyaopen_license.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

/* TODO: need to consider whether to use locks at the TKL layer*/
#define PARTITION_SIZE (1 << 12) /* 4KB */
#define FLH_BLOCK_SZ   PARTITION_SIZE

// key
#define SIMPLE_FLASH_KEY_ADDR 0x0 /* offset of tuya data partition */

// KV
#define SIMPLE_FLASH_START (SIMPLE_FLASH_KEY_ADDR + PARTITION_SIZE)
#define SIMPLE_FLASH_SIZE  0x8000 // 32K

// UF
#define UF_PARTITION_NUM   1
#define UF_PARTITION_START (SIMPLE_FLASH_START + SIMPLE_FLASH_SIZE)
#define UF_PARTITION_SIZE  0x8000 // 32K 0x18000 // 96K

#if defined(KV_PROTECTED_ENABLE) && (KV_PROTECTED_ENABLE == 1)
#define SIMPLE_FLASH_KV_PROTECTED_START (UF_PARTITION_START + UF_PARTITION_SIZE)
#define SIMPLE_FLASH_KV_PROTECTED_SIZE  0x1000 // 4K
#else
#define SIMPLE_FLASH_KV_PROTECTED_SIZE 0 // 4K
#endif

/*
 * Must cover KEY + KV + UF (+ protect). V1.6 omitted KEY (PARTITION_SIZE), so
 * UF's last 4KB sector lands at flash end (0x8400000) and erase returns
 * SL_STATUS_SI91X_INVALID_CONFIG_RANGE_PROVIDED (0x10063). tal_kv mounts UF.
 */
#define LITTLEFS_MEM_SIZE                                                                                              \
    (PARTITION_SIZE + SIMPLE_FLASH_SIZE + UF_PARTITION_SIZE + SIMPLE_FLASH_KV_PROTECTED_SIZE)

extern char linker_littlefs_begin;

__attribute__((used)) uint8_t littlefs_default_storage[LITTLEFS_MEM_SIZE] __attribute__((section(".ltfs")));
#define FLASH_LITTLEFS_BASE ((uint32_t)&linker_littlefs_begin)

enum {
    FLASH_SECTOR_WRITE = 0,
    FLASH_SECTOR_ERASE = 1,
};

typedef struct {
    char *uuid;
    char *authkey;
} tuya_iot_license_t;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief read flash
 *
 * @param[in] addr: flash address
 * @param[out] dst: pointer of buffer
 * @param[in] size: size of buffer
 *
 * @note This API is used for reading flash.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_read(uint32_t addr, uint8_t *dst, uint32_t size)
{
    uint32_t flash_addr = addr + FLASH_LITTLEFS_BASE;

    if (NULL == dst) {
        return OPRT_INVALID_PARM;
    }

    TKL_LOGV("flash r %08lx, sz %lu", flash_addr, size);
    unsigned char *result = memcpy((uint8_t *)dst, (uint8_t *)flash_addr, size);

    // Check if memcpy was successful
    if (result != dst) {
        TKL_LOGE("flash r error");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief write flash
 *
 * @param[in] addr: flash address
 * @param[in] src: pointer of buffer
 * @param[in] size: size of buffer
 *
 * @note This API is used for writing flash.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_write(uint32_t addr, const uint8_t *src, uint32_t size)
{
    sl_status_t status;
    uint32_t    flash_addr = addr + FLASH_LITTLEFS_BASE;

    if (NULL == src) {
        return OPRT_INVALID_PARM;
    }

    TKL_LOGV("flash w %08lx, sz %lu", flash_addr, size);
    status = (int)sl_si91x_command_to_write_common_flash(flash_addr, src, size, FLASH_SECTOR_WRITE);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("flash w error %lx", status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief erase flash
 *
 * @param[in] addr: flash address
 * @param[in] size: size of flash block
 *
 * @note This API is used for erasing flash.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_erase(uint32_t addr, uint32_t size)
{
    sl_status_t status;
    uint32_t    flash_addr = addr + FLASH_LITTLEFS_BASE;
    uint32_t    size_erase = 0;
    /* WiseConnect erase path ignores payload; keep non-NULL like Silabs LFS HAL */
    uint8_t     dummy_buff[1] = {0};

    if (size % FLASH_SECTOR_SIZE == 0) {
        size_erase = size;
    } else {
        size_erase = (size / FLASH_SECTOR_SIZE + 1) * FLASH_SECTOR_SIZE;
    }

    if (size_erase > 0xFFFFu) {
        TKL_LOGE("flash e size too large %lu", size_erase);
        return OPRT_INVALID_PARM;
    }

    TKL_LOGV("flash e %08lx, size %lu, erase size %lu", flash_addr, size, size_erase);
    status = sl_si91x_command_to_write_common_flash(flash_addr, dummy_buff, (uint16_t)size_erase, FLASH_SECTOR_ERASE);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("flash e error %lx", status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief lock flash
 *
 * @param[in] addr: lock begin addr
 * @param[in] size: lock area size
 *
 * @note This API is used for lock flash.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_lock(uint32_t addr, uint32_t size)
{
    TKL_UNUSED(addr);
    TKL_UNUSED(size);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief unlock flash
 *
 * @param[in] addr: unlock begin addr
 * @param[in] size: unlock area size
 *
 * @note This API is used for unlock flash.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_unlock(uint32_t addr, uint32_t size)
{
    TKL_UNUSED(addr);
    TKL_UNUSED(size);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief get flash information
 *
 * @param[out] info: the description of the flash
 *
 * @note This API is used to get description of storage.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_flash_get_one_type_info(TUYA_FLASH_TYPE_E type, TUYA_FLASH_BASE_INFO_T *info)
{
    if ((type > TUYA_FLASH_TYPE_MAX) || (info == NULL)) {
        return OPRT_INVALID_PARM;
    }

    switch (type) {
    case TUYA_FLASH_TYPE_UF:
        info->partition_num           = 1;
        info->partition[0].block_size = PARTITION_SIZE;
        info->partition[0].start_addr = UF_PARTITION_START;
        info->partition[0].size       = UF_PARTITION_SIZE;
        break;
    case TUYA_FLASH_TYPE_KV_DATA:
        info->partition_num           = 1;
        info->partition[0].block_size = FLH_BLOCK_SZ;
        info->partition[0].start_addr = SIMPLE_FLASH_START;
        info->partition[0].size       = SIMPLE_FLASH_SIZE;
        break;
    case TUYA_FLASH_TYPE_KV_KEY:
        info->partition_num           = 1;
        info->partition[0].block_size = FLH_BLOCK_SZ;
        info->partition[0].start_addr = SIMPLE_FLASH_KEY_ADDR;
        info->partition[0].size       = FLH_BLOCK_SZ;
        break;
#if defined(KV_PROTECTED_ENABLE) && (KV_PROTECTED_ENABLE == 1)
    case TUYA_FLASH_TYPE_KV_PROTECT:
        info->partition_num           = 1;
        info->partition[0].block_size = FLH_BLOCK_SZ;
        info->partition[0].start_addr = SIMPLE_FLASH_KV_PROTECTED_START;
        info->partition[0].size       = SIMPLE_FLASH_KV_PROTECTED_SIZE;
        break;
#endif
    default:
        return OPRT_INVALID_PARM;
        break;
    }

    return OPRT_OK;
}

/**
 * @brief tuya_iot_license_read
 *
 * @param[in] license: iot license struct pointer
 *
 * @note This API is used for read license .
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
int tuya_iot_license_read(tuya_iot_license_t *license)
{
    TKL_UNUSED(license);

    /* SiWx917 has no factory OTP license region. The device credentials come
     * from KV (written by the `auth` CLI command) or from tuya_config.h, both
     * of which the upper layers read on their own -- so there is nothing to
     * hand back here.
     *
     * Do not reintroduce a hard-coded uuid/authkey pair for bring-up. Real
     * credentials in source get committed, published, and reused across
     * boards; use `auth` on the device instead. */
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Read factory/OTP license JSON blob
 * @param[out] data allocated buffer holding license JSON (caller frees with tal_free)
 * @param[out] data_len length of data in bytes
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED if OTP license is unavailable
 * @note SiWx917 has no factory OTP license path yet; use KV auth / tuya_config.h.
 */
OPERATE_RET tuyaopen_license_read(CHAR_T **data, UINT32_T *data_len)
{
    if (data != NULL) {
        *data = NULL;
    }
    if (data_len != NULL) {
        *data_len = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Write factory/OTP license JSON blob
 * @param[in] data license JSON string
 * @param[in] data_len length of data in bytes
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED if OTP write is unavailable
 * @note SiWx917 has no factory OTP license path yet; use `auth` CLI (KV).
 */
OPERATE_RET tuyaopen_license_write(const CHAR_T *data, UINT32_T data_len)
{
    TKL_UNUSED(data);
    TKL_UNUSED(data_len);
    return OPRT_NOT_SUPPORTED;
}
