/**
 * @file sl_tuya_system.h
 * @brief SiWx917 platform-private system helpers.
 *
 * These are NOT part of the TKL contract (tools/porting/adapter/system/).
 * They live outside the tkl_* namespace on purpose so that tkl_system.h can
 * stay a faithful copy of the canonical header.
 */
#ifndef __SL_TUYA_SYSTEM_H__
#define __SL_TUYA_SYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the platform tick/timer source used by the TKL layer.
 */
void sl_tuya_system_timer_init(void);

/**
 * @brief Dump FreeRTOS per-task run-time statistics to the log output.
 */
void sl_tuya_system_print_task_stats(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SL_TUYA_SYSTEM_H__ */
