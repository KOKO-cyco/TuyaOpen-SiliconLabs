/*****************************************************************************//**
 * @file tkl_adc.c
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
#include "tkl_adc.h"
#include "tkl_adc_calc.h"
#include "tkl_log.h"

#include "sl_si91x_adc.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

/*
 * One AUX ADC block, so only TUYA_ADC_NUM_0 exists. Its channels are what
 * cfg->ch_list selects, and a channel's analog pad comes from pos_inp_sel,
 * which the SDK muxes itself (ADC_PinMux in rsi_adc.c) -- unlike PWM, nothing
 * here has to be looked up in the RTE pin table.
 *
 * Static mode is used rather than FIFO: tkl_adc_read_single_channel() is a
 * one-shot read, which is what every in-tree caller does (tdd_power_soc.c
 * samples in a loop, tdd_joystick.c polls), and FIFO mode would mean carrying
 * ping/pong DMA buffers for no gain.
 */
#define TKL_ADC_PORT_MAX 1u

typedef struct {
    bool                    inited;
    uint32_t                vref_mv;
    uint32_t                ch_list;
    sl_adc_config_t         cfg;
    sl_adc_channel_config_t ch_cfg;
} tkl_adc_ctx_t;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static tkl_adc_ctx_t g_adc;

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

/* The SDK requires a callback to be registered before start(); nothing in the
 * static one-shot path reports through it, so this exists only to satisfy that. */
static void _adc_event_cb(uint8_t channel, uint8_t event)
{
    (void)channel;
    (void)event;
}

/*
 * Point the one configured slot at an analog input and make the hardware
 * follow. See _adc_configure_slot0() for why this is not just a field write.
 */
static OPERATE_RET _adc_select_input(uint8_t input_sel)
{
    sl_status_t status;

    if (g_adc.ch_cfg.pos_inp_sel[0] == input_sel) {
        return OPRT_OK;
    }

    g_adc.ch_cfg.pos_inp_sel[0] = input_sel;

    /* ADC_PinMux() runs from inside this call, so the pad only moves when the
     * configuration is pushed again -- assigning the field alone would read
     * the previous pin while reporting success. */
    status = sl_si91x_adc_set_channel_configuration(g_adc.ch_cfg, g_adc.cfg);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc input %u select failed 0x%lx", input_sel, (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static OPERATE_RET _adc_read_raw(uint8_t ch_id, int32_t *out)
{
    uint16_t    value = 0;
    sl_status_t status;
    OPERATE_RET rt;

    rt = _adc_select_input(ch_id);
    if (rt != OPRT_OK) {
        return rt;
    }

    status = sl_si91x_adc_read_data_static(g_adc.ch_cfg, g_adc.cfg, &value);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc input %u read failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    *out = (int32_t)value;
    return OPRT_OK;
}

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

OPERATE_RET tkl_adc_init(TUYA_ADC_NUM_E port_num, TUYA_ADC_BASE_CFG_T *cfg)
{
    sl_status_t status;
    uint8_t     first_ch;
    float       vref_volts;

    if ((uint32_t)port_num >= TKL_ADC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (cfg->ch_list.data == 0u) {
        TKL_LOGE("adc ch_list is empty");
        return OPRT_INVALID_PARM;
    }

    first_ch = tkl_adc_nth_channel(cfg->ch_list.data, 0);
    if (!tkl_adc_input_sel_valid(first_ch)) {
        TKL_LOGE("adc ch_list 0x%lx selects no usable input", (unsigned long)cfg->ch_list.data);
        return OPRT_INVALID_PARM;
    }

    memset(&g_adc, 0, sizeof(g_adc));
    g_adc.vref_mv = (cfg->ref_vol != 0u) ? cfg->ref_vol : TKL_ADC_DEFAULT_VREF_MV;
    g_adc.ch_list = cfg->ch_list.data;

    /*
     * One slot, and only one.
     *
     * The SDK's "channel" is a SLOT index, not an analog input: it installs
     * slots 0..num_of_channel_enable-1 and picks each slot's pad from
     * pos_inp_sel[slot]. sl_si91x_adc_read_data_static() then walks the slots
     * itself using a file-static counter and ignores the channel field it was
     * handed, so there is no way to say "read input 5 now" -- the only lever
     * is which pad the slot points at.
     *
     * validate_adc_channel_parameters() enforces this: it counts the non-zero
     * entries of num_of_samples[] as the installed slots and rejects the whole
     * configuration when that count disagrees with num_of_channel_enable.
     * Filling all 16 entries while declaring one channel is what made init
     * return SL_STATUS_INVALID_CONFIGURATION (0x23) on the board.
     */
    g_adc.cfg.operation_mode        = SL_ADC_STATIC_MODE;
    g_adc.cfg.num_of_channel_enable = 1;

    g_adc.ch_cfg.channel = 0;
    /* Single-ended: the differential path would need a negative input the TKL
     * config has no way to express, and would halve the usable range. */
    g_adc.ch_cfg.input_type[0]     = SL_ADC_SINGLE_ENDED;
    g_adc.ch_cfg.pos_inp_sel[0]    = first_ch;
    g_adc.ch_cfg.num_of_samples[0] = 1;
    g_adc.ch_cfg.sampling_rate[0]  = (cfg->freq != 0u) ? cfg->freq : 100000u;
    /* Slots 1..15 stay at zero from the memset above; a non-zero
     * num_of_samples there would be read as another installed slot. */

    vref_volts = (float)g_adc.vref_mv / 1000.0f;

    /* Note: sl_si91x_adc_init() trims the efuse and in doing so changes the
     * clock the debug UART was set up with; the SDK example re-inits its
     * console right after for that reason. Nothing here prints during init. */
    status = sl_si91x_adc_init(g_adc.ch_cfg, g_adc.cfg, vref_volts);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc init failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_adc_set_channel_configuration(g_adc.ch_cfg, g_adc.cfg);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc channel config failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_adc_register_event_callback(_adc_event_cb);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc callback register failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_adc_start(g_adc.cfg);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("adc start failed 0x%lx", (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    g_adc.inited = true;
    return OPRT_OK;
}

OPERATE_RET tkl_adc_deinit(TUYA_ADC_NUM_E port_num)
{
    if ((uint32_t)port_num >= TKL_ADC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!g_adc.inited) {
        return OPRT_OK;
    }

    sl_si91x_adc_stop(g_adc.cfg);
    sl_si91x_adc_deinit(g_adc.cfg);
    g_adc.inited = false;
    return OPRT_OK;
}

uint8_t tkl_adc_width_get(TUYA_ADC_NUM_E port_num)
{
    (void)port_num;
    return TKL_ADC_WIDTH_BITS;
}

uint32_t tkl_adc_ref_voltage_get(TUYA_ADC_NUM_E port_num)
{
    (void)port_num;
    /* Before init the configured value is unknown, so report the board default
     * rather than 0, which a caller would divide by. */
    return g_adc.inited ? g_adc.vref_mv : TKL_ADC_DEFAULT_VREF_MV;
}

int32_t tkl_adc_temperature_get(void)
{
    /*
     * The die temperature sits behind a separate block on this part
     * (sl_si91x_bjt_temperature_sensor), not the AUX ADC, and it needs its own
     * SLC component. Reporting a made-up number here would be worse than
     * saying no.
     */
    return (int32_t)OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_adc_read_single_channel(TUYA_ADC_NUM_E port_num, uint8_t ch_id, int32_t *data)
{
    if ((uint32_t)port_num >= TKL_ADC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (data == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!g_adc.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }
    if (!tkl_adc_input_sel_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    return _adc_read_raw(ch_id, data);
}

OPERATE_RET tkl_adc_read_data(TUYA_ADC_NUM_E port_num, int32_t *buff, uint16_t len)
{
    if ((uint32_t)port_num >= TKL_ADC_PORT_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (buff == NULL || len == 0u) {
        return OPRT_INVALID_PARM;
    }
    if (!g_adc.inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /* One sample per enabled channel, in ch_list order, until the caller's
     * buffer is full or the list runs out -- whichever comes first. */
    for (uint16_t i = 0; i < len; i++) {
        uint8_t     ch = tkl_adc_nth_channel(g_adc.ch_list, (uint8_t)i);
        OPERATE_RET rt;

        if (!tkl_adc_input_sel_valid(ch)) {
            /* Fewer channels enabled than the caller asked for; the remainder
             * is left untouched rather than filled with a fabricated reading. */
            return (i == 0u) ? OPRT_INVALID_PARM : OPRT_OK;
        }
        rt = _adc_read_raw(ch, &buff[i]);
        if (rt != OPRT_OK) {
            return rt;
        }
    }
    return OPRT_OK;
}

OPERATE_RET tkl_adc_read_voltage(TUYA_ADC_NUM_E port_num, int32_t *buff, uint16_t len)
{
    OPERATE_RET rt = tkl_adc_read_data(port_num, buff, len);

    if (rt != OPRT_OK) {
        return rt;
    }

    for (uint16_t i = 0; i < len; i++) {
        uint8_t ch = tkl_adc_nth_channel(g_adc.ch_list, (uint8_t)i);

        if (!tkl_adc_input_sel_valid(ch)) {
            break; /* same short list read_data stopped at */
        }
        buff[i] = tkl_adc_code_to_mv(buff[i], g_adc.vref_mv);
    }
    return OPRT_OK;
}
