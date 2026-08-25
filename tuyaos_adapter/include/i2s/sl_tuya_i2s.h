/**
 * @file sl_tuya_i2s.h
 * @brief SiWx917 platform-private I2S streaming extensions.
 *
 * These are NOT part of the TKL contract (tools/porting/adapter/i2s/). They
 * back the DMA "streaming" receive path this platform needs and are consumed
 * only by boards/SiWx917/common/audio/. They live outside the tkl_* namespace
 * on purpose so that tkl_i2s.h can stay a faithful copy of the canonical
 * header.
 */
#ifndef __SL_TUYA_I2S_H__
#define __SL_TUYA_I2S_H__

#include "tuya_cloud_types.h"
#include "tkl_i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sl_tuya_i2s_buffer_ready_callback_t)(void *args, void *buffer, uint32_t n_frames);

OPERATE_RET sl_tuya_i2s_set_streaming_config(TUYA_I2S_NUM_E i2s_num, void *buff, uint32_t n_frames);

OPERATE_RET sl_tuya_i2s_get_streaming_config(TUYA_I2S_NUM_E i2s_num, void **buff, uint32_t *n_frames);

OPERATE_RET sl_tuya_i2s_recv_streaming(TUYA_I2S_NUM_E i2s_num, sl_tuya_i2s_buffer_ready_callback_t callback,
                                       void *args);

bool sl_tuya_i2s_send_inprogress(TUYA_I2S_NUM_E i2s_num);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __SL_TUYA_I2S_H__
