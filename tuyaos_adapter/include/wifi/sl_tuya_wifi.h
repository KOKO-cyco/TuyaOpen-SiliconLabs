/**
 * @file sl_tuya_wifi.h
 * @brief SiWx917 platform-private Wi-Fi accessors.
 *
 * NOT part of the TKL contract (tools/porting/adapter/wifi/). These expose
 * SiWx917-specific WiseConnect state to platform startup code in mcu/, and
 * live outside the tkl_* namespace on purpose so that tkl_wifi.h can stay a
 * faithful copy of the canonical header.
 */
#ifndef __SL_TUYA_WIFI_H__
#define __SL_TUYA_WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the WiseConnect device configuration the TKL Wi-Fi layer owns.
 *
 * @return Pointer to the platform's sl_wifi_device_configuration_t. Cast at
 *         the call site to avoid pulling WiseConnect headers in here.
 */
void *sl_tuya_wifi_get_configuration(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SL_TUYA_WIFI_H__ */
