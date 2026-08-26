/*****************************************************************************//**
 * @file tkl_adc_calc.h
 * @brief Conversion rules for the SiWx917 AUX ADC, split out so they can be
 *        unit-tested on the host without the Silicon Labs SDK.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __TKL_ADC_CALC_H__
#define __TKL_ADC_CALC_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * The AUX ADC hands back a 12-bit code. The SDK's own static-mode example
 * converts with
 *
 *     vout = (code / 4095) * vref
 *
 * so 4095 -- not 4096 -- is full scale here. (sl_si91x_joystick.c in the same
 * SDK uses 4096 for the same register; the example's "maximum output value get
 * from adc data register" is the one that matches a 12-bit code's top value.)
 */
#define TKL_ADC_WIDTH_BITS   12u
#define TKL_ADC_FULL_SCALE   4095u

/* Board default when a caller leaves cfg->ref_vol at 0. The AUX ADC on this
 * part is referenced to the 3.3 V analog supply, which is what the SDK example
 * uses too. */
#define TKL_ADC_DEFAULT_VREF_MV 3300u

/*
 * pos_inp_sel picks which analog pad feeds a channel, and the SDK does the pad
 * muxing itself inside ADC_PinMux(). The selects run 0..15 and are NOT GPIO
 * numbers -- 0 is ULP_GPIO0, 6 is TOP_GPIO25, and so on -- so a caller's
 * channel index is passed straight through as the select rather than being
 * translated against a pin table the way PWM has to be.
 */
#define TKL_ADC_MAX_INPUT_SEL 15u

/**
 * @brief Convert a raw ADC code into millivolts.
 *
 * @param[in] code    12-bit conversion result
 * @param[in] vref_mv reference voltage in millivolts; 0 selects the default
 * @return voltage in millivolts, saturated at vref for out-of-range codes
 */
static inline int32_t tkl_adc_code_to_mv(int32_t code, uint32_t vref_mv)
{
    uint32_t vref = (vref_mv != 0u) ? vref_mv : TKL_ADC_DEFAULT_VREF_MV;

    if (code <= 0) {
        return 0;
    }
    if ((uint32_t)code >= TKL_ADC_FULL_SCALE) {
        return (int32_t)vref;
    }
    /* 64-bit product: 4095 * a large vref overflows 32 bits at ~1 MV, which is
     * not reachable here, but the cast costs nothing and removes the question. */
    return (int32_t)(((uint64_t)code * vref) / TKL_ADC_FULL_SCALE);
}

/**
 * @brief Whether a channel index can be used as an ADC input select.
 */
static inline bool tkl_adc_input_sel_valid(uint8_t ch_id)
{
    return ch_id <= TKL_ADC_MAX_INPUT_SEL;
}

/**
 * @brief Number of channels a ch_list bitmap asks for.
 *
 * TUYA_ADC_BASE_CFG_T carries both a bitmap and a count, and callers do not
 * always agree with themselves; the bitmap is the one that says which inputs
 * to touch, so it is what gets counted.
 */
static inline uint8_t tkl_adc_count_channels(uint32_t ch_list)
{
    uint8_t n = 0;

    for (uint8_t i = 0; i <= TKL_ADC_MAX_INPUT_SEL; i++) {
        if (ch_list & (1u << i)) {
            n++;
        }
    }
    return n;
}

/**
 * @brief Index of the nth set bit in a ch_list, or 0xFF when there is none.
 */
static inline uint8_t tkl_adc_nth_channel(uint32_t ch_list, uint8_t n)
{
    uint8_t seen = 0;

    for (uint8_t i = 0; i <= TKL_ADC_MAX_INPUT_SEL; i++) {
        if (ch_list & (1u << i)) {
            if (seen == n) {
                return i;
            }
            seen++;
        }
    }
    return 0xFFu;
}

#endif /* __TKL_ADC_CALC_H__ */
