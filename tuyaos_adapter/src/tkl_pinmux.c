/*****************************************************************************//**
 * @file tkl_pinmux.c
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
#include "tkl_pinmux.h"
#include "tkl_pinmux_table.h"
#include "tkl_log.h"

/* Shared with tkl_gpio.c: TUYA pin number -> (port, pad). */
#include "si91x_pin_map.h"
#include "sl_driver_gpio.h"

static const pin_map_t gpio_list[] = {SI91X_PIN_MAPPING};

#undef X_ULP
#undef X_HP
#undef X_UULP
#undef ULP_PAD

#define TKL_PIN_COUNT (sizeof(gpio_list) / sizeof(gpio_list[0]))

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

static const pin_map_t *_pin_info(TUYA_PIN_NAME_E pin)
{
    for (uint32_t i = 0; i < TKL_PIN_COUNT; i++) {
        if (gpio_list[i].pin_id == (TUYA_GPIO_NUM_E)pin) {
            return &gpio_list[i];
        }
    }
    return NULL;
}

/*
 * Find the mux this pad takes for this function.
 *
 * The table is keyed on the pad's index within its own domain, which is how
 * si91x_pin_map.h reports it and how the SDK's own drivers use it -- the RTE
 * header writes ULP pads as 64 + index and the CMSIS drivers subtract that
 * back out (SL_I2C_SCL_PIN in cmsis_driver/I2C.c).
 */
static const tkl_pinmux_entry_t *_lookup(uint8_t pad, bool ulp, TUYA_PIN_FUNC_E func)
{
    for (uint32_t i = 0; i < TKL_PINMUX_TABLE_LEN; i++) {
        const tkl_pinmux_entry_t *e = &g_tkl_pinmux_table[i];

        if (e->func == (uint16_t)func && e->pin == pad && e->ulp == ulp) {
            return e;
        }
    }
    return NULL;
}

/* Log what the pad CAN do, so a rejection points somewhere. */
static void _log_options(TUYA_PIN_NAME_E pin, uint8_t pad, bool ulp)
{
    uint32_t n = 0;

    for (uint32_t i = 0; i < TKL_PINMUX_TABLE_LEN; i++) {
        if (g_tkl_pinmux_table[i].pin == pad && g_tkl_pinmux_table[i].ulp == ulp) {
            n++;
        }
    }
    TKL_LOGE("pin %d has %lu mux option(s), none of them the requested function",
             (int)pin, (unsigned long)n);
}

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief route a pin to a peripheral function
 *
 * @param[in] pin: TUYA pin number
 * @param[in] pin_func: TUYA_PIN_FUNC_E value
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_io_pinmux_config(TUYA_PIN_NAME_E pin, TUYA_PIN_FUNC_E pin_func)
{
    const pin_map_t          *info;
    const tkl_pinmux_entry_t *entry;
    sl_gpio_t                 gpio;
    sl_status_t               status;

    info = _pin_info(pin);
    if (info == NULL) {
        TKL_LOGE("pin %d is not a pad this part brings out", (int)pin);
        return OPRT_INVALID_PARM;
    }

    /*
     * UULP pads reach only the always-on block and have their own mux call
     * (sl_si91x_gpio_driver_set_uulp_npss_pin_mux); no peripheral signal in the
     * RTE table lands on one, so a request for one is a caller mistake rather
     * than a gap here.
     */
    if (info->port == UULP_VBAT) {
        TKL_LOGE("pin %d is a UULP pad, no peripheral mux exists for it", (int)pin);
        return OPRT_NOT_SUPPORTED;
    }

    entry = _lookup(info->pin, info->port != HP, pin_func);
    if (entry == NULL) {
        _log_options(pin, info->pin, info->port != HP);
        return OPRT_NOT_SUPPORTED;
    }

    if (entry->ulp) {
        /*
         * A SoC peripheral routed onto a ULP pad goes through
         * sl_si91x_gpio_driver_set_soc_peri_on_ulp_pin_mode() rather than the
         * plain pad-mode call -- the ULP domain has its own enable path. That
         * route is in the table (65 of its entries) but has not been exercised
         * on hardware, and reporting success for a mux that never took effect
         * is the failure this whole adapter exists to avoid.
         */
        TKL_LOGE("pin %d is a ULP pad; runtime mux for the ULP domain is not wired up", (int)pin);
        return OPRT_NOT_SUPPORTED;
    }

    gpio.port = SL_GPIO_PORT_A;
    gpio.pin  = entry->pin;

    status = sl_gpio_driver_set_pin_mode(&gpio, (sl_gpio_mode_t)entry->mux, 0);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("pin %d mux %u failed 0x%lx", (int)pin, entry->mux, (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief route several pins in one call
 *
 * @param[in] cfg: array of pin/function pairs
 * @param[in] num: entries in cfg
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_multi_io_pinmux_config(TUYA_MUL_PIN_CFG_T *cfg, uint16_t num)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    /*
     * Stops at the first failure instead of routing the rest: a half-configured
     * bus is harder to diagnose than one that plainly did not come up, and the
     * caller cannot tell which pins took effect from a single return code.
     */
    for (uint16_t i = 0; i < num; i++) {
        OPERATE_RET rt = tkl_io_pinmux_config(cfg[i].pin, cfg[i].pin_func);

        if (rt != OPRT_OK) {
            TKL_LOGE("multi pinmux stopped at entry %u of %u", i, num);
            return rt;
        }
    }
    return OPRT_OK;
}

/**
 * @brief which instance and channel of a peripheral type a pin can serve
 *
 * @param[in] pin: TUYA pin number
 * @param[in] pin_type: peripheral family
 *
 * @return (port << 8) | channel, or negative on error
 */
int32_t tkl_io_pin_to_func(uint32_t pin, TUYA_PIN_TYPE_E pin_type)
{
    static const uint16_t type_base[] = {
        [TUYA_IO_TYPE_PWM]  = 0x0300u, [TUYA_IO_TYPE_ADC]  = 0x0400u,
        [TUYA_IO_TYPE_DAC]  = 0x0500u, [TUYA_IO_TYPE_UART] = 0x0100u,
        [TUYA_IO_TYPE_SPI]  = 0x0200u, [TUYA_IO_TYPE_I2C]  = 0x0000u,
        [TUYA_IO_TYPE_I2S]  = 0x0600u,
    };
    const pin_map_t *info;
    uint16_t         base;

    if (pin_type >= (TUYA_PIN_TYPE_E)(sizeof(type_base) / sizeof(type_base[0]))) {
        return OPRT_INVALID_PARM;
    }
    base = type_base[pin_type];

    info = _pin_info((TUYA_PIN_NAME_E)pin);
    if (info == NULL || info->port == UULP_VBAT) {
        return OPRT_INVALID_PARM;
    }

    /*
     * The TUYA function constants encode (type << 8) | index, and this returns
     * the same shape the header documents: port in the high byte, channel in
     * the low one. The first match wins -- a pad that can serve two instances
     * of one family (GPIO 6 reaches both I2C0_SDA and I2C1_SCL) has no single
     * answer, and the caller that needs a specific one should be naming the
     * function rather than asking which.
     */
    for (uint32_t i = 0; i < TKL_PINMUX_TABLE_LEN; i++) {
        const tkl_pinmux_entry_t *e = &g_tkl_pinmux_table[i];

        if (e->pin == info->pin && e->ulp == (info->port != HP) && (e->func & 0xFF00u) == base) {
            return (int32_t)e->func;
        }
    }
    return OPRT_NOT_SUPPORTED;
}
