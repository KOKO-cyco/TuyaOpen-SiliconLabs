/*****************************************************************************//**
 * @file tkl_pwm_calc.h
 * @brief Frequency and duty arithmetic for the SiWx917 MCPWM, split out so it
 *        can be unit-tested on the host without the Silicon Labs SDK.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __TKL_PWM_CALC_H__
#define __TKL_PWM_CALC_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * The MCPWM base timer counts the SoC clock divided by a prescaler, and the
 * period register that wraps it is only 16 bits:
 *
 *     f_pwm = SOC_CLK / (prescale * period),   period <= 65535
 *
 * mcu/src/app_tuya.c brings the core up at 180 MHz (SOC_PLL_CLK), so with no
 * prescaling the slowest reachable output is 180e6 / 65535 = 2747 Hz -- above
 * both of the frequencies this API is most often asked for (1 kHz LED dimming,
 * 50 Hz hobby servos). The prescaler is what makes those reachable, so picking
 * one is not optional and is why this is a search rather than a division.
 *
 * The smallest prescale that fits is the best one: period is also the duty
 * resolution, so dividing the clock harder than necessary throws away steps.
 */
#define TKL_PWM_SOC_CLK_HZ  180000000u
#define TKL_PWM_PERIOD_MAX  65535u

/*
 * Below 2 ticks a period has no usable duty range at all (only 0% and 100%),
 * so a frequency that cannot do better is refused rather than silently
 * producing a square wave the caller did not ask for.
 */
#define TKL_PWM_PERIOD_MIN  2u

/* Prescale select values, index i meaning a divider of (1 << i). */
#define TKL_PWM_PRESCALE_SEL_MAX 6u   /* 1,2,4,8,16,32,64 */

/* Tuya's duty is a fraction of cfg->cycle. Callers that leave cycle at 0 get
 * the ecosystem default: tdd_led_pwm.c and the T5AI adapter both treat duty as
 * parts per 10000. */
#define TKL_PWM_DEFAULT_CYCLE 10000u

typedef struct {
    uint8_t  prescale_sel; /* 0..6, divider is (1 << prescale_sel) */
    uint16_t period;       /* base timer period register value */
} tkl_pwm_timing_t;

/**
 * @brief Solve a requested frequency into a prescale/period pair.
 *
 * Walks the prescalers from least to most dividing and takes the first whose
 * period fits in 16 bits, which is also the one leaving the most duty steps.
 *
 * @param[in]  freq_hz  requested output frequency
 * @param[out] out      resulting prescale select and period
 * @return true when the frequency is reachable, false otherwise
 */
static inline bool tkl_pwm_solve_timing(uint32_t freq_hz, tkl_pwm_timing_t *out)
{
    if (freq_hz == 0u || out == NULL) {
        return false;
    }

    for (uint8_t sel = 0u; sel <= TKL_PWM_PRESCALE_SEL_MAX; sel++) {
        uint32_t divider = 1u << sel;
        /* 64-bit intermediate: SOC_CLK * 1 already fits, but keeping the
         * division in 32 bits would truncate before the compare below. */
        uint32_t period = (uint32_t)(TKL_PWM_SOC_CLK_HZ / (divider * (uint64_t)freq_hz));

        if (period < TKL_PWM_PERIOD_MIN) {
            /* Too fast even undivided; dividing further only makes it worse. */
            return false;
        }
        if (period <= TKL_PWM_PERIOD_MAX) {
            out->prescale_sel = sel;
            out->period       = (uint16_t)period;
            return true;
        }
    }
    return false; /* Slower than 180e6 / (64 * 65535) ~= 42.9 Hz. */
}

/**
 * @brief Convert a Tuya duty (parts of @p cycle) into base timer ticks.
 *
 * @param[in] period ticks in one output period, from tkl_pwm_solve_timing
 * @param[in] duty   numerator, 0..cycle
 * @param[in] cycle  denominator; 0 means the 10000 default
 * @return duty in ticks, clamped to @p period
 */
static inline uint16_t tkl_pwm_duty_to_ticks(uint16_t period, uint32_t duty, uint32_t cycle)
{
    uint32_t scale = (cycle != 0u) ? cycle : TKL_PWM_DEFAULT_CYCLE;
    uint32_t ticks;

    if (duty >= scale) {
        return period;
    }
    ticks = (uint32_t)(((uint64_t)period * duty) / scale);

    /*
     * A non-zero duty that rounds to zero ticks would read as "off" and look
     * like a broken channel at the dim end of an LED sweep; the T5AI adapter
     * makes the same adjustment.
     */
    if (ticks == 0u && duty != 0u) {
        ticks = 1u;
    }
    return (uint16_t)ticks;
}

#endif /* __TKL_PWM_CALC_H__ */
