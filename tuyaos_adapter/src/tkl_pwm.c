/*****************************************************************************//**
 * @file tkl_pwm.c
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
#include "tkl_pwm.h"
#include "tkl_pwm_calc.h"
#include "tkl_log.h"

/* em_device.h for the HP/ULP port constants, which RTE_Device_917.h defines and
 * which the RTE PWM entries are expressed in; tkl_gpio.c takes the same route. */
#include "em_device.h"
#include "sl_si91x_pwm.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

/*
 * The MCPWM has four channels. Each drives a complementary output PAIR (a low
 * and a high side pad), but one duty and one period register per CHANNEL --
 * sl_si91x_pwm_set_duty_cycle() and _set_time_period() both take a channel,
 * not an output. So the part offers four independently controllable PWMs, not
 * eight, and TUYA_PWM_NUM_4/5 have nowhere to go.
 *
 * Both pads of a pair are muxed at init. On the AI dev kit the expansion
 * header brings out 1L/1H/2H/3L/3H/4L but not 2L or 4H, so muxing only one
 * side by convention would leave channel 2 unusable on this board; muxing both
 * lets whichever pad a board actually routes carry the signal.
 *
 * Pin, mux and pad values are the ones RTE_Device_917.h assigns to the PWM
 * signals -- pin and pad are different numbering schemes and sl_pwm_init_t
 * wants both. script/gen/extract_rte_pinmux.py prints the table these come
 * from, and tests/siwx917 golden-checks it against the vendored header.
 */
#define TKL_PWM_CHANNELS 4u

typedef struct {
    uint8_t pin_l, mux_l, pad_l;
    uint8_t pin_h, mux_h, pad_h;
} tkl_pwm_pins_t;

typedef struct {
    bool                inited;
    bool                running;
    TUYA_PWM_BASE_CFG_T cfg;    /* as handed in; duty_set/frequency_set each get
                                   only one value and must recompute the other */
    tkl_pwm_timing_t    timing; /* solved prescale/period for cfg.frequency */
} tkl_pwm_ctx_t;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static const tkl_pwm_pins_t g_pwm_pins[TKL_PWM_CHANNELS] = {
    /* PWM_1L / PWM_1H */ {6, 10, 1, 7, 10, 2},
    /* PWM_2L / PWM_2H */ {8, 10, 3, 9, 10, 4},
    /* PWM_3L / PWM_3H */ {10, 10, 5, 11, 10, 6},
    /* PWM_4L / PWM_4H */ {12, 10, 7, 15, 10, 8},
};

static tkl_pwm_ctx_t g_pwm[TKL_PWM_CHANNELS];

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

static tkl_pwm_ctx_t *_pwm_ctx(TUYA_PWM_NUM_E ch_id)
{
    if ((uint32_t)ch_id >= TKL_PWM_CHANNELS) {
        return NULL;
    }
    return &g_pwm[ch_id];
}

/* TUYA_PWM_NUM_0..3 line up with SL_CHANNEL_1..4. */
static sl_pwm_channel_t _pwm_channel(TUYA_PWM_NUM_E ch_id)
{
    return (sl_pwm_channel_t)(SL_CHANNEL_1 + (uint32_t)ch_id);
}

/* Any channel still up keeps the block powered: sl_si91x_pwm_deinit() takes no
 * channel argument and tears down the whole MCPWM, so deinit-ing one channel
 * must not switch off the others. */
static bool _pwm_any_inited(void)
{
    for (uint32_t i = 0; i < TKL_PWM_CHANNELS; i++) {
        if (g_pwm[i].inited) {
            return true;
        }
    }
    return false;
}

/* Push a solved period + duty into the hardware for one channel. */
static OPERATE_RET _pwm_apply(TUYA_PWM_NUM_E ch_id, tkl_pwm_ctx_t *ctx)
{
    sl_pwm_channel_t ch = _pwm_channel(ch_id);
    uint16_t         duty_ticks;
    sl_status_t      status;

    status = sl_si91x_pwm_control_period(SL_TIME_PERIOD_POSTSCALE_1_1,
                                         (sl_pwm_pre_t)ctx->timing.prescale_sel, ch);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d control_period failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_pwm_set_time_period(ch, ctx->timing.period, 0);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d set_time_period failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    /*
     * Duty is computed here rather than through sl_si91x_pwm_set_configuration()
     * because that helper takes an integer PERCENT -- 101 steps. Tuya's duty is
     * parts of cfg.cycle (10000 by convention), and collapsing it to percent
     * makes an LED fade visibly stepped.
     */
    duty_ticks = tkl_pwm_duty_to_ticks(ctx->timing.period, ctx->cfg.duty, ctx->cfg.cycle);
    status     = sl_si91x_pwm_set_duty_cycle(duty_ticks, ch);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d set_duty_cycle failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

OPERATE_RET tkl_pwm_init(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_BASE_CFG_T *cfg)
{
    tkl_pwm_ctx_t   *ctx = _pwm_ctx(ch_id);
    const tkl_pwm_pins_t *pins;
    sl_pwm_init_t    init;
    sl_status_t      status;
    OPERATE_RET      rt;

    if (ctx == NULL) {
        /* TUYA_PWM_NUM_4/5: the part has four channels. */
        return OPRT_NOT_SUPPORTED;
    }
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!tkl_pwm_solve_timing(cfg->frequency, &ctx->timing)) {
        TKL_LOGE("pwm%d frequency %lu Hz unreachable", ch_id, (unsigned long)cfg->frequency);
        return OPRT_INVALID_PARM;
    }

    pins = &g_pwm_pins[ch_id];
    /* HP (== 0) is the port every PWM signal sits on per RTE_Device_917.h. */
    init = (sl_pwm_init_t){
        .port_l = HP, .pin_l = pins->pin_l, .mux_l = pins->mux_l, .pad_l = pins->pad_l,
        .port_h = HP, .pin_h = pins->pin_h, .mux_h = pins->mux_h, .pad_h = pins->pad_h,
    };

    status = sl_si91x_pwm_init(&init);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d init failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    /*
     * Independent, not complementary: a caller asking for one PWM output does
     * not expect the paired pad to be driven with the inverse, which on a board
     * that routes both would fight whatever else is on it.
     */
    status = sl_si91x_pwm_set_output_mode(SL_MODE_INDEPENDENT, _pwm_channel(ch_id));
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d set_output_mode failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    /*
     * Per-channel timers, so channels can run at different frequencies.
     * cfg->count_mode is not consulted: TUYA_PWM_CNT_UP_AND_DOWN exists for
     * duplex complementary output, which this adapter deliberately does not
     * use (see SL_MODE_INDEPENDENT above), so free-run up-count is the only
     * mode that matches what the pads actually do.
     */
    status = sl_si91x_pwm_set_base_timer_mode(SL_FREE_RUN_MODE, _pwm_channel(ch_id));
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d set_base_timer_mode failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    ctx->cfg = *cfg;
    rt       = _pwm_apply(ch_id, ctx);
    if (rt != OPRT_OK) {
        /*
         * sl_si91x_pwm_init() above already powered the block. Leaving now
         * without inited set would strand it: every later tkl_pwm_deinit()
         * short-circuits on !inited and the block never powers down.
         */
        if (!_pwm_any_inited()) {
            sl_si91x_pwm_deinit();
        }
        return rt;
    }

    ctx->inited  = true;
    ctx->running = false;
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_deinit(TUYA_PWM_NUM_E ch_id)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_OK;
    }

    sl_si91x_pwm_stop(_pwm_channel(ch_id));
    ctx->running = false;
    ctx->inited  = false;

    /* Only the last channel down may power off the shared block. */
    if (!_pwm_any_inited()) {
        sl_si91x_pwm_deinit();
    }
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_start(TUYA_PWM_NUM_E ch_id)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);
    sl_status_t    status;

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    status = sl_si91x_pwm_start(_pwm_channel(ch_id));
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d start failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    ctx->running = true;
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_stop(TUYA_PWM_NUM_E ch_id)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);
    sl_status_t    status;

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    status = sl_si91x_pwm_stop(_pwm_channel(ch_id));
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d stop failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }
    ctx->running = false;
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_duty_set(TUYA_PWM_NUM_E ch_id, uint32_t duty)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);
    uint16_t       ticks;
    sl_status_t    status;

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /* Only duty arrives; the period it is a fraction of comes from the cached
     * config, which is why init has to keep it. */
    ticks  = tkl_pwm_duty_to_ticks(ctx->timing.period, duty, ctx->cfg.cycle);
    status = sl_si91x_pwm_set_duty_cycle(ticks, _pwm_channel(ch_id));
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pwm%d duty_set failed 0x%lx", ch_id, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    ctx->cfg.duty = duty;
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_frequency_set(TUYA_PWM_NUM_E ch_id, uint32_t frequency)
{
    tkl_pwm_ctx_t   *ctx = _pwm_ctx(ch_id);
    tkl_pwm_timing_t solved;
    OPERATE_RET      rt;

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }
    if (!tkl_pwm_solve_timing(frequency, &solved)) {
        TKL_LOGE("pwm%d frequency %lu Hz unreachable", ch_id, (unsigned long)frequency);
        return OPRT_INVALID_PARM;
    }

    /*
     * Changing the period moves the tick count a given duty maps to, so the
     * duty has to be re-applied against the new period or the ratio silently
     * changes with the frequency.
     */
    ctx->timing        = solved;
    ctx->cfg.frequency = frequency;

    rt = _pwm_apply(ch_id, ctx);
    return rt;
}

OPERATE_RET tkl_pwm_info_get(TUYA_PWM_NUM_E ch_id, TUYA_PWM_BASE_CFG_T *info)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (info == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    *info = ctx->cfg;
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_info_set(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_BASE_CFG_T *info)
{
    tkl_pwm_ctx_t   *ctx = _pwm_ctx(ch_id);
    tkl_pwm_timing_t solved;

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (info == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }
    if (!tkl_pwm_solve_timing(info->frequency, &solved)) {
        return OPRT_INVALID_PARM;
    }

    ctx->cfg    = *info;
    ctx->timing = solved;
    return _pwm_apply(ch_id, ctx);
}

OPERATE_RET tkl_pwm_polarity_set(TUYA_PWM_NUM_E ch_id, TUYA_PWM_POLARITY_E polarity)
{
    tkl_pwm_ctx_t *ctx = _pwm_ctx(ch_id);

    if (ctx == NULL) {
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->inited) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /*
     * sl_si91x_pwm_set_output_polarity() takes no channel: the low/high output
     * polarity is a property of the whole MCPWM. Setting it for one channel
     * would flip every other running channel too, so this is reported as
     * unsupported rather than done with a side effect the caller cannot see.
     * cfg.polarity is kept in the cached config for info_get, unapplied.
     */
    (void)polarity;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_pwm_multichannel_start(TUYA_PWM_NUM_E *ch_id, uint8_t num)
{
    OPERATE_RET rt = OPRT_OK;

    if (ch_id == NULL) {
        return OPRT_INVALID_PARM;
    }
    /* No hardware synchronised-start primitive is exposed, so this is a loop;
     * channels come up a few cycles apart. */
    for (uint8_t i = 0; i < num; i++) {
        OPERATE_RET one = tkl_pwm_start(ch_id[i]);
        if (one != OPRT_OK) {
            rt = one;
        }
    }
    return rt;
}

OPERATE_RET tkl_pwm_multichannel_stop(TUYA_PWM_NUM_E *ch_id, uint8_t num)
{
    OPERATE_RET rt = OPRT_OK;

    if (ch_id == NULL) {
        return OPRT_INVALID_PARM;
    }
    for (uint8_t i = 0; i < num; i++) {
        OPERATE_RET one = tkl_pwm_stop(ch_id[i]);
        if (one != OPRT_OK) {
            rt = one;
        }
    }
    return rt;
}

OPERATE_RET tkl_pwm_cap_start(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_CAP_IRQ_T *cfg)
{
    /* Input capture lives on the QEI/SCT blocks on this part, not MCPWM. */
    (void)ch_id;
    (void)cfg;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_pwm_cap_stop(TUYA_PWM_NUM_E ch_id)
{
    (void)ch_id;
    return OPRT_NOT_SUPPORTED;
}
