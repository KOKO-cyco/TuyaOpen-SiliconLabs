/*****************************************************************************//**
 * @file tkl_vad.c
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

#include "tkl_vad.h"
#include "tuya_cloud_types.h"

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

OPERATE_RET tkl_vad_init(TKL_VAD_CONFIG_T *config)
{
    TKL_UNUSED(config);

    return OPRT_OK;
}

OPERATE_RET tkl_vad_feed(uint8_t *data, uint32_t len)
{
    TKL_UNUSED(data);
    TKL_UNUSED(len);

    return OPRT_NOT_SUPPORTED;
}

TKL_VAD_STATUS_T tkl_vad_get_status(void)
{
    return TKL_VAD_STATUS_NONE;
}

OPERATE_RET tkl_vad_start(void)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_vad_stop(void)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_vad_deinit(void)
{
    return OPRT_NOT_SUPPORTED;
}
