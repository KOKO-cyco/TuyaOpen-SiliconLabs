/*****************************************************************************//**
 * @file tkl_dac.c
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

#include <string.h>

#include "tuya_error_code.h"
#include "tkl_dac.h"
#include "tkl_dac_calc.h"
#include "tkl_log.h"

#include "sl_si91x_dac.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

/*
 * One AUX DAC block, so only TUYA_DAC_NUM_0 exists.
 *
 * Static mode, not FIFO: the TKL surface has no way to hand over a streaming
 * buffer with timing (tkl_dac_controller_config takes a void * and one cmd),
 * and static mode is the one where a written sample simply holds on the pin,
 * which is what a "set this output level" API means. FIFO mode would also want
 * a sample clock nothing in the config can express.
 *
 * The output pad is whatever sl_si91x_dac_config.h selects (ULP GPIO 4 by
 * default on this SDK); the DAC driver muxes it internally, so as with the ADC
 * there is no RTE pin lookup here.
 */
#define TKL_DAC_PORT_MAX 1u

typedef struct {
    bool                inited;
    bool                running;
    TUYA_DAC_BASE_CFG_T cfg;
} tkl_dac_ctx_t;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static tkl_dac_ctx_t g_dac;

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

/* Registered because the SDK wants one before start(); static mode reports
 * nothing through it. */
static void _dac_event_cb(uint8_t event)
{
    (void)event;
}

static OPERATE_RET _dac_write(const TKL_DAC_WRITE_T *w)
{
    /* Static mode converts a single sample and holds it, so only the first is
     * meaningful; a caller passing more is not an error, the rest is just not
     * a waveform this mode can play. */
    int16_t     sample;
    sl_status_t status;

    if (w == NULL || w->data == NULL || w->len == 0u) {
        return OPRT_INVALID_PARM;
    }

    sample = tkl_dac_clamp(w->data[0]);
    status = sl_si91x_dac_write_data(&sample, 1u);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("dac write failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

OPERATE_RET tkl_dac_init(TUYA_DAC_NUM_E port_num)
{
    sl_dac_clock_config_t clock = {0};
    sl_status_t           status;

    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (g_dac.inited) {
        return OPRT_OK;
    }

    memset(&g_dac, 0, sizeof(g_dac));
    /* Defaults the DAC config header already carries; the block derives its
     * own clock from the SoC PLL that app_tuya.c brought up. */
    g_dac.cfg.width = TKL_DAC_WIDTH_BITS;
    g_dac.cfg.freq  = 5000000u;

    status = sl_si91x_dac_init(&clock);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("dac init failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_dac_register_event_callback(_dac_event_cb);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("dac callback register failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    g_dac.inited = true;
    return OPRT_OK;
}

OPERATE_RET tkl_dac_deinit(TUYA_DAC_NUM_E port_num)
{
    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!g_dac.inited) {
        return OPRT_OK;
    }

    if (g_dac.running) {
        sl_si91x_dac_stop();
        g_dac.running = false;
    }
    sl_si91x_dac_deinit();
    g_dac.inited = false;
    return OPRT_OK;
}

OPERATE_RET tkl_dac_controller_config(TUYA_DAC_NUM_E port_num, TUYA_DAC_CMD_E cmd, void *argu)
{
    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (argu == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!g_dac.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    switch (cmd) {
    case TUYA_DAC_SET_BASE_CFG: {
        const TUYA_DAC_BASE_CFG_T *cfg = (const TUYA_DAC_BASE_CFG_T *)argu;
        sl_dac_config_t            dac_cfg = {0};
        sl_status_t                status;

        /* width is reported, not chosen: the converter is 10-bit and a caller
         * asking for more would silently get its low bits used. */
        if (cfg->width != 0u && cfg->width != TKL_DAC_WIDTH_BITS) {
            TKL_LOGE("dac width %u unsupported, converter is %u-bit",
                     cfg->width, TKL_DAC_WIDTH_BITS);
            return OPRT_INVALID_PARM;
        }

        dac_cfg.operating_mode  = SL_DAC_STATIC_MODE;
        dac_cfg.dac_sample_rate = (cfg->freq != 0u) ? cfg->freq : 5000000u;

        status = sl_si91x_dac_set_configuration(dac_cfg, (float)TKL_DAC_DEFAULT_VREF_MV / 1000.0f);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("dac set_configuration failed 0x%lx", (unsigned long)status);
            return OPRT_COM_ERROR;
        }

        g_dac.cfg       = *cfg;
        g_dac.cfg.width = TKL_DAC_WIDTH_BITS;
        return OPRT_OK;
    }

    case TUYA_DAC_WRITE_FIFO:
        /* See TKL_DAC_WRITE_T in tkl_dac_calc.h: the TKL API leaves argu
         * untyped and no in-tree caller pins it down, so this shape is defined
         * by the platform. */
        return _dac_write((const TKL_DAC_WRITE_T *)argu);

    default:
        return OPRT_NOT_SUPPORTED;
    }
}

OPERATE_RET tkl_dac_base_cfg_get(TUYA_DAC_NUM_E port_num, TUYA_DAC_BASE_CFG_T *cfg)
{
    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    *cfg = g_dac.cfg;
    /* Reported even before init so a caller can size buffers up front. */
    cfg->width = TKL_DAC_WIDTH_BITS;
    return OPRT_OK;
}

OPERATE_RET tkl_dac_start(TUYA_DAC_NUM_E port_num)
{
    sl_status_t status;

    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!g_dac.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    status = sl_si91x_dac_start();
    if (status != SL_STATUS_OK) {
        TKL_LOGE("dac start failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    g_dac.running = true;
    return OPRT_OK;
}

OPERATE_RET tkl_dac_stop(TUYA_DAC_NUM_E port_num)
{
    sl_status_t status;

    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!g_dac.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    status = sl_si91x_dac_stop();
    if (status != SL_STATUS_OK) {
        TKL_LOGE("dac stop failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    g_dac.running = false;
    return OPRT_OK;
}

OPERATE_RET tkl_dac_fifo_reset(TUYA_DAC_NUM_E port_num)
{
    if ((uint32_t)port_num >= TKL_DAC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!g_dac.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /*
     * There is no FIFO to reset in static mode -- the block holds one sample,
     * and the SDK exposes no flush for it. Reported as unsupported rather than
     * returning OK, which would tell a caller a buffer had been cleared when
     * no such buffer exists.
     */
    return OPRT_NOT_SUPPORTED;
}
