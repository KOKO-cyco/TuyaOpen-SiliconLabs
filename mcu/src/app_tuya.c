/***************************************************************************//**
 * @file app_tuya.c
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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "cmsis_os2.h"

#include "rsi_rom_clks.h"
#include "sl_component_catalog.h"
#include "sl_constants.h"
#include "sl_si91x_clock_manager.h"
#include "sl_si91x_driver.h"
#include "sl_status.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "sl_tuya_log.h"
#include "sl_tuya_system.h"
#include "sl_tuya_wifi.h"
#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_uart.h"
#include "tkl_wifi.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define SOC_PLL_CLK ((uint32_t)(180000000))

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------

static bool cpu_clock_set(uint32_t hz);
static void app_main_handle(void *arg);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

extern void tuya_app_main(void);
extern void user_main(void);

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static THREAD_HANDLE app_main_thread    = NULL;
static volatile bool app_main_init_done = false;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

static bool cpu_clock_set(uint32_t hz)
{
    int32_t status;

    sl_si91x_clock_manager_m4_set_core_clk(M4_SOCPLLCLK, hz);
    sl_si91x_clock_manager_set_pll_freq(INFT_PLL, hz, PLL_REF_CLK_VAL_XTAL);

    status = RSI_CLK_QspiClkConfig(M4CLK, QSPI_SOCPLLCLK, 0, 0, 0);
    if (status != RSI_OK) {
        assert(!"Failed to set QSPI Clock as SOC PLL clock");
        return false;
    }

    status = RSI_CLK_Qspi2ClkConfig(M4CLK, QSPI_SOCPLLCLK, 0, 0, 0);
    if (status != RSI_OK) {
        assert(!"Failed to set QSPI 2 Clock as SOC PLL clock");
        return false;
    }

    return true;
}

static void app_main_handle(void *arg)
{
    TKL_UNUSED(arg);

    sl_status_t                     status;
    sl_wifi_device_configuration_t *wifi_config;
    sl_wifi_firmware_version_t      firmware_version = {0};
    TUYA_UART_BASE_CFG_T            base_cfg         = {
                           .baudrate = 115200,
                           .databits = TUYA_UART_DATA_LEN_8BIT,
                           .stopbits = TUYA_UART_STOP_LEN_1BIT,
                           .parity   = TUYA_UART_PARITY_TYPE_NONE,
    };

    /* Core Clock runs at 180MHz SOC PLL Clock */
    cpu_clock_set(SOC_PLL_CLK);

#if TKL_ULP_TIMER_SYSTICK_ENABLE
    sl_tuya_system_timer_init();
#endif

    tkl_uart_init(TUYA_UART_NUM_0, &base_cfg);

    wifi_config = (sl_wifi_device_configuration_t *)sl_tuya_wifi_get_configuration();

    status = sl_wifi_init(wifi_config, NULL, sl_wifi_default_event_handler);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("WiFi initialization error %lx", status);
    } else {
        TKL_LOGI("WiFi initialization success");
    }

    /* HIGH_PERFORMANCE is applied in tkl_wifi scan/connect path under BLE coex.
     * Keep boot path aligned with V1.6 (do not force profile here). */

#ifdef SLI_SI91X_MCU_INTERFACE
    uint8_t xtal_enable = 1;
    status              = sl_si91x_m4_ta_secure_handshake(SL_SI91X_ENABLE_XTAL, 1, &xtal_enable, 0, NULL);
    if (status != SL_STATUS_OK) {
        TKL_LOGI("Failed to bring m4_ta_secure_handshake: 0x%lx", status);
        return;
    }
    TKL_LOGI("m4_ta_secure_handshake success");
#endif

    sl_wifi_get_firmware_version(&firmware_version);
    TKL_LOGI("Running M4 fw: %s %s", __DATE__, __TIME__);
    TKL_LOGI("Running TA fw: %x%x.%d.%d.%d.%d.%d.%d", firmware_version.chip_id, firmware_version.rom_id,
             firmware_version.major, firmware_version.minor, firmware_version.security_version,
             firmware_version.patch_num, firmware_version.customer_id, firmware_version.build_num);

    SYS_TIME_T ms = tkl_system_get_millisecond();

    /* Delay for attach debug */
    tkl_system_sleep(3000);

    TKL_LOGI("Running TuyaOpen application [%d]", (int)(tkl_system_get_millisecond() - ms));
    sl_tuya_log_output_set(tal_log_print_raw);
    tuya_app_main();
    tal_thread_delete(app_main_thread);
    app_main_thread    = NULL;
    app_main_init_done = true;
}

void app_init(const void *unused)
{
    TUYA_UNUSED(unused);
    THREAD_CFG_T thrd_param = {4096, 0, "app_main_thread"};

    tal_thread_create_and_start(&app_main_thread, NULL, NULL, app_main_handle, NULL, &thrd_param);
}

#if defined(ENABLE_ML_AUDIO_CLASSIFIER) && (ENABLE_ML_AUDIO_CLASSIFIER == 1)
bool sl_main_start_task_should_continue(void)
{
    static bool initial_flag = true;

    while (!app_main_init_done) {
        tkl_system_sleep(100);
    }

    if (initial_flag) {
        initial_flag = false;
        return true;
    } else {
        return false;
    }
}
#endif

#ifdef PRINT_DEBUG_LOG

#define DEBUG_LOG_BUFFER_SIZE (1024 * 1024) // Debug log buffer in PSRAM
#define DEBUG_LOG_ENTRY_MAX   512           // Max length per log entry

typedef struct {
    char    *buffer;
    uint32_t write_pos;
    uint32_t total_size;
    uint32_t entry_count;
    bool     initialized;
} debug_log_buffer_t;

static debug_log_buffer_t g_debug_log = {0};

static void debug_log_init(void)
{
    if (g_debug_log.initialized) {
        return;
    }

    extern void *tkl_system_psram_malloc(size_t size);
    g_debug_log.buffer = (char *)tkl_system_psram_malloc(DEBUG_LOG_BUFFER_SIZE);

    if (g_debug_log.buffer != NULL) {
        memset(g_debug_log.buffer, 0, DEBUG_LOG_BUFFER_SIZE);
        g_debug_log.write_pos   = 0;
        g_debug_log.total_size  = DEBUG_LOG_BUFFER_SIZE;
        g_debug_log.entry_count = 0;
        g_debug_log.initialized = true;
        printf("[DEBUG_LOG] Initialized %d KB buffer in PSRAM\r\n", DEBUG_LOG_BUFFER_SIZE / 1024);
    } else {
        printf("[DEBUG_LOG] Failed to allocate PSRAM buffer\r\n");
    }
}

void sl_debug_log(const char *format, ...)
{
    static char temp_buf[DEBUG_LOG_ENTRY_MAX];
    static char msg_buf[DEBUG_LOG_ENTRY_MAX - 32];
    va_list     args;
    int         len, total_len;

    if (!g_debug_log.initialized) {
        debug_log_init();
    }

    va_start(args, format);
    len = vsnprintf(msg_buf, sizeof(msg_buf), format, args);
    va_end(args);

    if (len < 0 || len >= (int)sizeof(msg_buf)) {
        len          = sizeof(msg_buf) - 1;
        msg_buf[len] = '\0';
    }

    uint32_t timestamp_ms = tkl_system_get_millisecond();

    total_len = snprintf(temp_buf, sizeof(temp_buf), "[%08lu] %s", timestamp_ms, msg_buf);

    if (total_len < 0 || total_len >= (int)sizeof(temp_buf)) {
        total_len           = sizeof(temp_buf) - 1;
        temp_buf[total_len] = '\0';
    }

    if (g_debug_log.initialized && g_debug_log.buffer != NULL) {
        uint32_t space_left = g_debug_log.total_size - g_debug_log.write_pos;

        if ((uint32_t)total_len < space_left) {
            memcpy(&g_debug_log.buffer[g_debug_log.write_pos], temp_buf, total_len);
            g_debug_log.write_pos += total_len;
        } else {
            memcpy(&g_debug_log.buffer[g_debug_log.write_pos], temp_buf, space_left);
            memcpy(&g_debug_log.buffer[0], &temp_buf[space_left], total_len - space_left);
            g_debug_log.write_pos = total_len - space_left;
        }

        g_debug_log.entry_count++;
    }
}

void sl_debug_log_dump(void)
{
    if (!g_debug_log.initialized || g_debug_log.buffer == NULL) {
        printf("[DEBUG_LOG] Buffer not initialized\r\n");
        return;
    }

    printf("\r\n");
    printf("=============================================================\r\n");
    printf("====== DEBUG LOG M4-TA DUMP FROM PSRAM ============================\r\n");
    printf("=============================================================\r\n");
    printf("Buffer size: %lu bytes (%lu KB)\r\n", g_debug_log.total_size, g_debug_log.total_size / 1024);
    printf("Write position: %lu\r\n", g_debug_log.write_pos);
    printf("Total entries: %lu\r\n", g_debug_log.entry_count);
    printf("-------------------------------------------------------------\r\n");

    /* Current dump prints only the contiguous region from 0 to write_pos. */
    if (g_debug_log.write_pos > 0) {
        char last_char                            = g_debug_log.buffer[g_debug_log.write_pos];
        g_debug_log.buffer[g_debug_log.write_pos] = '\0';

        printf("%s", g_debug_log.buffer);

        g_debug_log.buffer[g_debug_log.write_pos] = last_char;
    }

    printf("\r\n");
    printf("=============================================================\r\n");
    printf("====== END OF DEBUG LOG DUMP ================================\r\n");
    printf("=============================================================\r\n");
}

void sl_debug_log_clear(void)
{
    if (g_debug_log.initialized && g_debug_log.buffer != NULL) {
        memset(g_debug_log.buffer, 0, g_debug_log.total_size);
        g_debug_log.write_pos   = 0;
        g_debug_log.entry_count = 0;
        printf("[DEBUG_LOG] Buffer cleared\r\n");
    }
}

#endif /* PRINT_DEBUG_LOG */
