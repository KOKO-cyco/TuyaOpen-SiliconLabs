/**
 * @file tkl_wakeup.c
 * @brief wake up source for SiWx917 (UULP GPIO + deep sleep timer)
 *
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 *
 */

#include "tkl_wakeup.h"
#include "tkl_log.h"
#include "tuya_error_code.h"

#include "sl_si91x_driver_gpio.h"
#include "sl_si91x_power_manager.h"
#include "rsi_power_save.h"
#include "rsi_ds_timer.h"

#define UULP_WAKE_PIN_MAX 3

uint32_t g_wake_timer_ms;
uint32_t sli_si91x_power_manager_dst_duration_us;

static void uulp_wake_cb(uint32_t pin_intr)
{
    (void)pin_intr;
}

static OPERATE_RET timer_wakeup_set(uint32_t ms)
{
    g_wake_timer_ms                         = ms;
    sli_si91x_power_manager_dst_duration_us = (uint32_t)((uint64_t)ms * 1000);
    if (sl_si91x_power_manager_set_wakeup_sources(SL_SI91X_POWER_MANAGER_DST_WAKEUP, true) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static OPERATE_RET timer_wakeup_clear(void)
{
    return (sl_si91x_power_manager_set_wakeup_sources(SL_SI91X_POWER_MANAGER_DST_WAKEUP, false) == SL_STATUS_OK)
               ? OPRT_OK
               : OPRT_COM_ERROR;
}

static OPERATE_RET gpio_wakeup_set(TUYA_GPIO_NUM_E gpio_num, TUYA_GPIO_WAKE_TYPE_E level)
{
    uint8_t                                pin = (uint8_t)gpio_num;
    sl_si91x_gpio_polarity_t               polarity;
    sl_si91x_gpio_interrupt_config_flag_t  flag;

    if (pin > UULP_WAKE_PIN_MAX) {
        return OPRT_NOT_SUPPORTED;
    }

    polarity = (TUYA_GPIO_WAKEUP_HIGH == level || TUYA_GPIO_WAKEUP_RISE == level) ? GPIO_POLARITY_1 : GPIO_POLARITY_0;
    flag     = (GPIO_POLARITY_1 == polarity) ? SL_GPIO_INTERRUPT_LEVEL_HIGH : SL_GPIO_INTERRUPT_LEVEL_LOW;

    if (sl_gpio_driver_unregister(UULP_GPIO_INSTANCE, pin, pin) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_select_uulp_npss_receiver(pin, SET) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_set_uulp_npss_pin_mux(pin, NPSS_GPIO_PIN_MUX_MODE2) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_set_uulp_npss_direction(pin, GPIO_INPUT) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_select_uulp_npss_polarity(pin, polarity) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_set_uulp_npss_wakeup_interrupt(pin) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_gpio_driver_configure_uulp_interrupt(flag, pin, (void *)&uulp_wake_cb) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }
    if (sl_si91x_power_manager_set_wakeup_sources(SL_SI91X_POWER_MANAGER_GPIO_WAKEUP, true) != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static OPERATE_RET gpio_wakeup_clear(TUYA_GPIO_NUM_E gpio_num)
{
    uint8_t pin = (uint8_t)gpio_num;

    if (pin > UULP_WAKE_PIN_MAX) {
        return OPRT_NOT_SUPPORTED;
    }

    sl_si91x_gpio_driver_clear_uulp_npss_wakeup_interrupt(pin);

    return (sl_si91x_power_manager_set_wakeup_sources(SL_SI91X_POWER_MANAGER_GPIO_WAKEUP, false) == SL_STATUS_OK)
               ? OPRT_OK
               : OPRT_COM_ERROR;
}

OPERATE_RET tkl_wakeup_source_set(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param)
{
    if (NULL == param) {
        return OPRT_INVALID_PARM;
    }

    switch (param->source) {
    case TUYA_WAKEUP_SOURCE_TIMER:
        return timer_wakeup_set(param->wakeup_para.timer_param.ms);
    case TUYA_WAKEUP_SOURCE_RTC:
        return timer_wakeup_set(param->wakeup_para.rtc_param.ms);
    case TUYA_WAKEUP_SOURCE_GPIO:
        return gpio_wakeup_set(param->wakeup_para.gpio_param.gpio_num, param->wakeup_para.gpio_param.level);
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

OPERATE_RET tkl_wakeup_source_clear(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param)
{
    if (NULL == param) {
        return OPRT_INVALID_PARM;
    }

    switch (param->source) {
    case TUYA_WAKEUP_SOURCE_TIMER:
    case TUYA_WAKEUP_SOURCE_RTC:
        return timer_wakeup_clear();
    case TUYA_WAKEUP_SOURCE_GPIO:
        return gpio_wakeup_clear(param->wakeup_para.gpio_param.gpio_num);
    default:
        return OPRT_NOT_SUPPORTED;
    }
}
