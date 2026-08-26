/*****************************************************************************//**
 * @file tkl_dac_calc.h
 * @brief Value rules and the write-payload shape for the SiWx917 AUX DAC,
 *        split out so they can be unit-tested on the host.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __TKL_DAC_CALC_H__
#define __TKL_DAC_CALC_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * The AUX DAC takes a 10-bit code -- the SDK's own example writes 0x3FF as its
 * full-scale sample -- even though sl_si91x_dac_write_data() carries int16_t.
 * A caller handing over a 12-bit value (easy, since the ADC on this same part
 * is 12-bit) would otherwise wrap somewhere inside the driver.
 */
#define TKL_DAC_WIDTH_BITS 10u
#define TKL_DAC_FULL_SCALE 1023

/* Board default reference, same rail the ADC uses. */
#define TKL_DAC_DEFAULT_VREF_MV 3300u

/*
 * tkl_dac_controller_config(port, TUYA_DAC_WRITE_FIFO, argu) passes argu as a
 * bare void *, and the TKL headers never say what it points at. Nothing in the
 * tree calls it either -- there is no tal_dac, and no driver references
 * tkl_dac_* at all -- so there is no existing usage to match.
 *
 * This is therefore a platform-defined contract, stated here rather than left
 * for a caller to guess: argu points at one of these. If a common definition
 * ever lands in tuya_cloud_types.h, this should give way to it.
 */
typedef struct {
    const int16_t *data; /**< samples, each 0..TKL_DAC_FULL_SCALE */
    uint16_t       len;  /**< number of samples; static mode uses the first */
} TKL_DAC_WRITE_T;

/**
 * @brief Clamp a sample to what the converter can actually represent.
 *
 * Clamping rather than rejecting: a write is a stream operation and failing
 * the whole buffer because one sample overshot would be worse than pinning it
 * to the rail, which is what the hardware would do anyway.
 */
static inline int16_t tkl_dac_clamp(int32_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > TKL_DAC_FULL_SCALE) {
        return (int16_t)TKL_DAC_FULL_SCALE;
    }
    return (int16_t)value;
}

/**
 * @brief Convert millivolts into a DAC code for a given reference.
 *
 * @param[in] mv      requested output in millivolts
 * @param[in] vref_mv reference in millivolts; 0 selects the default
 */
static inline int16_t tkl_dac_mv_to_code(int32_t mv, uint32_t vref_mv)
{
    uint32_t vref = (vref_mv != 0u) ? vref_mv : TKL_DAC_DEFAULT_VREF_MV;

    if (mv <= 0) {
        return 0;
    }
    if ((uint32_t)mv >= vref) {
        return (int16_t)TKL_DAC_FULL_SCALE;
    }
    return (int16_t)(((uint64_t)mv * TKL_DAC_FULL_SCALE) / vref);
}

#endif /* __TKL_DAC_CALC_H__ */
