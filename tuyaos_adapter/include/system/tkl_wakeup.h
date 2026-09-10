/**
 * @file tkl_wakeup.h
 * @brief Wakeup source configuration for SiWx917 deep sleep.
 *
 * Only UULP VBAT GPIO 0-4 can wake Sleep/PS0 (datasheet Table 5.9);
 * anything else is refused with OPRT_INVALID_PARM so a misconfigured
 * key can never strand the device in un-wakeable deep sleep.
 */

#ifndef __TKL_WAKEUP_H__
#define __TKL_WAKEUP_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TKL_WAKEUP_MAX_RECORDS 8

OPERATE_RET tkl_wakeup_source_set(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param);

OPERATE_RET tkl_wakeup_source_clear(const TUYA_WAKEUP_SOURCE_BASE_CFG_T *param);

#ifdef __cplusplus
}
#endif

#endif
