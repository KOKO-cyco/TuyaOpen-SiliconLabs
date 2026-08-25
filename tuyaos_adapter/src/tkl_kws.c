/**
 * @file tkl_kws.c
 * @brief SiWx917 KWS stub — platform does not support keyword spotting
 * @version 1.0
 * @date 2026-08-07
 * @copyright Copyright (c) Tuya Inc.
 *
 * @note SiWx917 chat bot only supports hold-to-talk. Upper layers still call
 *       tkl_kws_init() during ai_chat_init(); return OPRT_OK so init can proceed.
 *       Wakeup/free modes must not be enabled on this board.
 */
#include "tkl_kws.h"
#include "sl_tuya_log.h"

/**
 * @brief Initialize KWS (no-op on SiWx917)
 * @return OPRT_OK always
 */
OPERATE_RET tkl_kws_init(void)
{
    TKL_LOGI("KWS not supported on SiWx917; hold-to-talk only");
    return OPRT_OK;
}

/**
 * @brief Register wakeup callback (ignored)
 * @param[in] wakeup_cb unused
 * @return OPRT_OK
 */
OPERATE_RET tkl_kws_reg_wakeup_cb(TKL_KWS_WAKEUP_CB wakeup_cb)
{
    TKL_UNUSED(wakeup_cb);
    return OPRT_OK;
}

/**
 * @brief Enable KWS (no-op)
 * @return OPRT_OK
 */
OPERATE_RET tkl_kws_enable(void)
{
    return OPRT_OK;
}

/**
 * @brief Disable KWS (no-op)
 * @return OPRT_OK
 */
OPERATE_RET tkl_kws_disable(void)
{
    return OPRT_OK;
}

/**
 * @brief Deinitialize KWS (no-op)
 * @return OPRT_OK
 */
OPERATE_RET tkl_kws_deinit(void)
{
    return OPRT_OK;
}
