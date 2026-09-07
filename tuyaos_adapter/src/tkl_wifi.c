/*****************************************************************************//**
 * @file tkl_wifi.c
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

// #if ENABLE_BLUETOOTH
#include "ble_config.h"
#include "rsi_ble_common_config.h"
// #endif /* ENABLE_BLUETOOTH */

#include "sl_constants.h"
#include "sl_net.h"
#include "sl_net_for_lwip.h"
#include "sl_net_si91x.h"
#include "sl_si91x_config.h"
#include "sl_si91x_driver.h"
#include "sl_si91x_types.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"

#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "tkl_log.h"
#include "tkl_memory.h"
#include "tkl_output.h"
#include "tkl_system.h"
#include "tkl_wifi.h"

#include "lwip/apps/dhcpserver.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/icmp6.h"
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/mld6.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"

#ifdef PRINT_DEBUG_LOG
/*
 * Debug-only dumps owned by other translation units: hci_trace_dump() lives in
 * tkl_bt.c (so PRINT_DEBUG_LOG needs Bluetooth enabled in the build) and
 * sl_debug_log_dump() in mcu/src/app_tuya.c.
 */
extern void hci_trace_dump(void);
extern void sl_debug_log_dump(void);
#endif /* PRINT_DEBUG_LOG */

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

#if WIFI_INIT_MODE_STA
static WF_WK_MD_E g_wifi_work_mode = WWM_STATION;
#else
static WF_WK_MD_E g_wifi_work_mode = WWM_STATIONAP;
#endif

static WF_STATION_STAT_E wifi_sta_conn_status = WSS_IDLE;

static WIFI_EVENT_CB tkl_event_cb = NULL;

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

sl_net_wifi_lwip_context_t g_wifi_sta_context;
sl_net_wifi_lwip_context_t g_wifi_ap_context;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static sl_net_wifi_client_profile_t g_wifi_client_profile = {
    .config = {.ssid           = {.value = "", .length = 0},
               .channel        = {.channel   = SL_WIFI_AUTO_CHANNEL,
                                  .band      = SL_WIFI_AUTO_BAND,
                                  .bandwidth = SL_WIFI_AUTO_BANDWIDTH},
               .bssid          = {{0}},
               .bss_type       = SL_WIFI_BSS_TYPE_INFRASTRUCTURE,
               .security       = SL_WIFI_WPA2,
               .encryption     = SL_WIFI_CCMP_ENCRYPTION,
               .client_options = (sl_wifi_client_flag_t)0,
               .credential_id  = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID},
    .ip     = {.mode = SL_IP_MANAGEMENT_DHCP, .type = SL_IPV4, .host_name = NULL, {{{0}}}},
};

static sl_net_wifi_ap_profile_t g_wifi_ap_profile = {
    .config =
        {
            .ssid.value          = "",
            .ssid.length         = 0,
            .channel.channel     = SL_WIFI_AUTO_CHANNEL,
            .channel.band        = SL_WIFI_AUTO_BAND,
            .channel.bandwidth   = SL_WIFI_AUTO_BANDWIDTH,
            .security            = SL_WIFI_WPA2,
            .encryption          = SL_WIFI_CCMP_ENCRYPTION,
            .rate_protocol       = SL_WIFI_RATE_PROTOCOL_AUTO,
            .options             = 0,
            .credential_id       = SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
            .keepalive_type      = SL_SI91X_AP_NULL_BASED_KEEP_ALIVE,
            .beacon_interval     = 100,
            .client_idle_timeout = 0xFF,
            .dtim_beacon_count   = 3,
            .maximum_clients     = 3,
            .beacon_stop         = 0,
            .tdi_flags           = SL_WIFI_TDI_NONE,
            .is_11n_enabled      = 1,
        },
    .ip = {
        .mode      = SL_IP_MANAGEMENT_STATIC_IP,
        .type      = SL_IPV4,
        .host_name = NULL,
        .ip        = {.v4.ip_address.value = 0, .v4.gateway.value = 0, .v4.netmask.value = 0},
    }};

// clang-format off
static sl_wifi_device_configuration_t g_wifi_config =
{
    .boot_option = LOAD_NWP_FW,
    .mac_address = NULL,
    .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
    .region_code = US,
    .boot_config =
    {
#if WIFI_INIT_MODE_STA
        .oper_mode = SL_SI91X_CLIENT_MODE,//SL_SI91X_CONCURRENT_MODE,//SL_SI91X_ACCESS_POINT_MODE,
#else
        .oper_mode = SL_SI91X_CONCURRENT_MODE,
#endif

#if ENABLE_BLUETOOTH
        .coex_mode = SL_SI91X_WLAN_BLE_MODE,
#else
        .coex_mode = SL_SI91X_WLAN_ONLY_MODE,
#endif /* ENABLE_BLUETOOTH */

        .feature_bit_map = (SL_SI91X_FEAT_SECURITY_OPEN
                            | SL_SI91X_FEAT_AGGREGATION
                            | SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE
#ifdef SLI_SI91X_MCU_INTERFACE
                            | SL_SI91X_FEAT_WPS_DISABLE
#endif
                            ),
        .tcp_ip_feature_bit_map =  (SL_SI91X_TCP_IP_FEAT_BYPASS
                                //    | SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT
                                //    | SL_SI91X_TCP_IP_FEAT_DNS_CLIENT
                                   | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),
        .custom_feature_bit_map     = SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID,
        .ext_custom_feature_bit_map = (SL_SI91X_EXT_FEAT_LOW_POWER_MODE
                                       | SL_SI91X_EXT_FEAT_XTAL_CLK
                                       | SL_SI91X_EXT_FEAT_UART_SEL_FOR_DEBUG_PRINTS
                                       | MEMORY_CONFIG
#if defined(SLI_SI917) || defined(SLI_SI915)
                                       | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif

#if ENABLE_BLUETOOTH
                                       | SL_SI91X_EXT_FEAT_BT_CUSTOM_FEAT_ENABLE
#endif /* ENABLE_BLUETOOTH */
                                       ),
        .ext_tcp_ip_feature_bit_map = (SL_SI91X_CONFIG_FEAT_EXTENTION_VALID),
#if ENABLE_BLUETOOTH
        .bt_feature_bit_map = (RSI_BT_FEATURE_BITMAP),
        .ble_feature_bit_map = ((SL_SI91X_BLE_MAX_NBR_PERIPHERALS(RSI_BLE_MAX_NBR_PERIPHERALS)
                                 | SL_SI91X_BLE_MAX_NBR_CENTRALS(RSI_BLE_MAX_NBR_CENTRALS)
                                 | SL_SI91X_BLE_MAX_NBR_ATT_SERV(RSI_BLE_MAX_NBR_ATT_SERV)
                                 | SL_SI91X_BLE_MAX_NBR_ATT_REC(RSI_BLE_MAX_NBR_ATT_REC))
                                 | SL_SI91X_FEAT_BLE_CUSTOM_FEAT_EXTENTION_VALID
                                 | SL_SI91X_BLE_PWR_INX(RSI_BLE_PWR_INX)
                                 | SL_SI91X_BLE_PWR_SAVE_OPTIONS(RSI_BLE_PWR_SAVE_OPTIONS)
                                 | SL_SI91X_916_BLE_COMPATIBLE_FEAT_ENABLE
#if RSI_BLE_GATT_ASYNC_ENABLE
                                 | SL_SI91X_BLE_GATT_ASYNC_ENABLE
#endif
        ),

        .ble_ext_feature_bit_map = ((SL_SI91X_BLE_NUM_CONN_EVENTS(RSI_BLE_NUM_CONN_EVENTS)
                                    | SL_SI91X_BLE_NUM_REC_BYTES(RSI_BLE_NUM_REC_BYTES))
#if RSI_BLE_INDICATE_CONFIRMATION_FROM_HOST
                                    | SL_SI91X_BLE_INDICATE_CONFIRMATION_FROM_HOST // indication response from app
#endif
#if RSI_BLE_MTU_EXCHANGE_FROM_HOST
                                    | SL_SI91X_BLE_MTU_EXCHANGE_FROM_HOST // MTU Exchange request initiation from app
#endif
#if RSI_BLE_SET_SCAN_RESP_DATA_FROM_HOST
                                    | (SL_SI91X_BLE_SET_SCAN_RESP_DATA_FROM_HOST) // Set SCAN Resp Data from app
#endif
#if RSI_BLE_DISABLE_CODED_PHY_FROM_HOST
                                    | (SL_SI91X_BLE_DISABLE_CODED_PHY_FROM_HOST) // Disable Coded PHY from app
#endif
#if BLE_SIMPLE_GATT
                                    | SL_SI91X_BLE_GATT_INIT
#endif
#if RSI_BLE_ENABLE_ADV_EXTN
                                    | SL_SI91X_BLE_ENABLE_ADV_EXTN
#endif
#if RSI_BLE_AE_MAX_ADV_SETS
                                    | SL_SI91X_BLE_AE_MAX_ADV_SETS(RSI_BLE_AE_MAX_ADV_SETS)
#endif
        ),
#else
        .bt_feature_bit_map      = 0,
        .ble_feature_bit_map     = 0,
        .ble_ext_feature_bit_map = 0,
#endif /* ENABLE_BLUETOOTH */
        .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP
                                  | SL_SI91X_ENABLE_ENHANCED_MAX_PSP)
    }
};
// clang-format on

static bool                   g_wifi_initialized          = false;
static sl_wifi_scan_result_t *g_wifi_scan_result          = NULL;
static volatile bool          g_wifi_scan_complete        = false;
static volatile sl_status_t   g_wifi_scan_callback_status = SL_STATUS_OK;

/*
 * Last requested NWP power-save. Scan/join force HIGH_PERFORMANCE; when the
 * hold count drops back to zero the saved request is re-applied so a reconnect
 * cannot leave the radio stuck in high performance.
 */
#define TKL_WIFI_LP_DTIM_MAX 10
#define TKL_WIFI_BEACON_MS   100

static BOOL_T  g_wifi_lp_wanted = FALSE;
static uint8_t g_wifi_lp_dtim   = 1;
static uint8_t g_wifi_lp_hold   = 0;
const uint16_t                g_wifi_scan_buf_size =
    (sizeof(sl_wifi_scan_result_t) + (SL_WIFI_MAX_SCANNED_AP * sizeof(g_wifi_scan_result->scan_info[0])));

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define ARRAY_SIZE(a)                  (sizeof(a) / sizeof((a)[0]))
#define TKL_WIFI_STA_IP_ADDRS_INIT(ip) memset(&(ip), 0, sizeof(ip))

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#ifndef IP4TOSTR
#define IP4TOSTR(a) (a)[0], (a)[1], (a)[2], (a)[3]
#endif

#ifndef IP4STR
#define IP4STR "%d.%d.%d.%d"
#endif

#if LWIP_NETIF_EXT_STATUS_CALLBACK
NETIF_DECLARE_EXT_CALLBACK(_platform_netif_ext_callback);
#endif /* LWIP_NETIF_EXT_STATUS_CALLBACK */

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------

static sl_status_t _tkl_wifi_join_callback_handler(sl_wifi_event_t event, sl_status_t status_code, const char *result,
                                                   uint32_t result_length, const void *arg);
static sl_status_t _tkl_wifi_scan_callback_handler(sl_wifi_event_t event, sl_status_t status_code,
                                                   const sl_wifi_scan_result_t *data, uint32_t result_length,
                                                   const void *arg);
static sl_status_t _tkl_wifi_ap_connected_event_handler(sl_wifi_event_t event, sl_status_t status_code,
                                                        const void *data, uint32_t data_length, const void *arg);
static sl_status_t _tkl_wifi_ap_disconnected_event_handler(sl_wifi_event_t event, sl_status_t status_code,
                                                           const void *data, uint32_t data_length, const void *arg);
static sl_status_t _tkl_wifi_scan_wait_complete(sl_status_t start_status);
static OPERATE_RET _tkl_wifi_scan_prepare_retry(sl_status_t status);
static OPERATE_RET _tkl_wifi_scan_build_ap_list(const int8_t *ssid, AP_IF_S **ap_ary, uint32_t *num);
static size_t      _tkl_wifi_strnlen(const char *str, size_t max_len);
static void        _tkl_wifi_ssid_copy(sl_wifi_ssid_t *dst, const char *src);
static void        _tkl_wifi_register_callbacks(void);
static OPERATE_RET _tkl_wifi_set_high_performance(void);
static OPERATE_RET _tkl_wifi_apply_lp_profile(BOOL_T enable, uint8_t dtim);
static void        _tkl_wifi_restore_lp(void);
static void        _tkl_wifi_lp_hold_begin(void);
static OPERATE_RET _tkl_wifi_lp_hold_end(OPERATE_RET rt);
static BOOL_T      _tkl_wifi_is_sta_only(void);

static size_t _tkl_wifi_strnlen(const char *str, size_t max_len)
{
    size_t len = 0;

    if (str == NULL) {
        return 0;
    }

    while ((len < max_len) && (str[len] != '\0')) {
        len++;
    }

    return len;
}

static void _tkl_wifi_ssid_copy(sl_wifi_ssid_t *dst, const char *src)
{
    size_t len = _tkl_wifi_strnlen(src, sizeof(dst->value));

    memcpy(dst->value, src, len);
    dst->value[len] = '\0';
    dst->length     = (uint8_t)len;
}

/**
 * @brief Check whether firmware boots in STA-only mode
 * @return TRUE if STA-only, FALSE otherwise
 * @note SoftAP must not be started in this mode with BLE coex on SiWx917
 */
static BOOL_T _tkl_wifi_is_sta_only(void)
{
#if defined(WIFI_INIT_MODE_STA) && (WIFI_INIT_MODE_STA == 1)
    return TRUE;
#else
    return (g_wifi_config.boot_config.oper_mode == SL_SI91X_CLIENT_MODE) ? TRUE : FALSE;
#endif
}

/**
 * @brief Apply an NWP performance profile
 * @param[in] enable TRUE for associated power-save, FALSE for HIGH_PERFORMANCE
 * @param[in] dtim   DTIM count from tal; clamped to [1, 10]
 * @return OPRT_OK on success, OPRT_COM_ERROR on failure
 */
static OPERATE_RET _tkl_wifi_apply_lp_profile(BOOL_T enable, uint8_t dtim)
{
    sl_wifi_performance_profile_v2_t performance_profile = {0};
    sl_status_t                      status;
    uint8_t                          clamped             = dtim;
    const char                      *name;

    if (clamped == 0) {
        clamped = 1;
    } else if (clamped > TKL_WIFI_LP_DTIM_MAX) {
        clamped = TKL_WIFI_LP_DTIM_MAX;
    }

    if (!enable) {
        performance_profile.profile = HIGH_PERFORMANCE;
        name                        = "HP";
    } else {
        /* DTIM 1/2: Fast PSP. DTIM >= 3: Max PSP. Chip listen interval max 1000 ms. */
        performance_profile.profile           = (clamped >= 3) ? ASSOCIATED_POWER_SAVE
                                                               : ASSOCIATED_POWER_SAVE_LOW_LATENCY;
        performance_profile.dtim_aligned_type = SL_SI91X_ALIGN_WITH_DTIM_BEACON;
        performance_profile.listen_interval   = (uint32_t)clamped * TKL_WIFI_BEACON_MS;
        name                                  = (clamped >= 3) ? "MAX_PSP" : "FAST_PSP";
    }

    status = sl_wifi_set_performance_profile_v2(&performance_profile);
    if (status != SL_STATUS_OK) {
        TKL_LOGW("set performance profile %s failed: 0x%lx", name, status);
        return OPRT_COM_ERROR;
    }

    TKL_LOGI("NWP profile %s dtim=%u listen=%lu", name, (unsigned)clamped,
             (unsigned long)performance_profile.listen_interval);
    return OPRT_OK;
}

/**
 * @brief Keep NWP out of power-save for scan/join under BLE coex
 * @return OPRT_OK on success, OPRT_COM_ERROR on failure
 */
static OPERATE_RET _tkl_wifi_set_high_performance(void)
{
    return _tkl_wifi_apply_lp_profile(FALSE, 0);
}

/**
 * @brief Re-apply the last requested LP profile once scan/join hold is clear
 * @return none
 */
static void _tkl_wifi_restore_lp(void)
{
    if ((g_wifi_lp_hold > 0) || !g_wifi_lp_wanted) {
        return;
    }

    (void)_tkl_wifi_apply_lp_profile(TRUE, g_wifi_lp_dtim);
}

/**
 * @brief Pause LP while scan/join runs (nested)
 * @return none
 */
static void _tkl_wifi_lp_hold_begin(void)
{
    g_wifi_lp_hold++;
    (void)_tkl_wifi_set_high_performance();
}

/**
 * @brief Drop one scan/join hold and restore LP if this was the last
 * @param[in] rt caller result, passed through
 * @return rt
 */
static OPERATE_RET _tkl_wifi_lp_hold_end(OPERATE_RET rt)
{
    if (g_wifi_lp_hold > 0) {
        g_wifi_lp_hold--;
    }
    _tkl_wifi_restore_lp();
    return rt;
}

/**
 * @brief Register WiFi join/scan/AP client event callbacks
 * @return none
 */
static void _tkl_wifi_register_callbacks(void)
{
    sl_wifi_set_join_callback_v2((sl_wifi_join_callback_v2_t)_tkl_wifi_join_callback_handler, NULL);
    sl_wifi_set_scan_callback_v2((sl_wifi_scan_callback_v2_t)_tkl_wifi_scan_callback_handler, NULL);
    sl_wifi_set_callback_v2(SL_WIFI_CLIENT_CONNECTED_EVENTS,
                            (sl_wifi_callback_function_v2_t)_tkl_wifi_ap_connected_event_handler, NULL);
    sl_wifi_set_callback_v2(SL_WIFI_CLIENT_DISCONNECTED_EVENTS,
                            (sl_wifi_callback_function_v2_t)_tkl_wifi_ap_disconnected_event_handler, NULL);
}

static sl_status_t _tkl_wifi_join_callback_handler(sl_wifi_event_t event, sl_status_t status_code, const char *result,
                                                   uint32_t result_length, const void *arg)
{
    TKL_UNUSED(result);
    TKL_UNUSED(arg);
    TKL_UNUSED(result_length);

    TKL_LOGI("Join callback: 0x%08lx, status: 0x%lx", (uint32_t)event, status_code);
    if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
        wifi_sta_conn_status = WSS_CONN_FAIL;
        if (tkl_event_cb != NULL) {
            tkl_event_cb(WFE_DISCONNECTED, NULL);
        }
    }

    return SL_STATUS_OK;
}

static sl_status_t _tkl_wifi_scan_callback_handler(sl_wifi_event_t event, sl_status_t status_code,
                                                   const sl_wifi_scan_result_t *data, uint32_t result_length,
                                                   const void *arg)
{
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(result_length);

    TKL_LOGD("WiFi receive scan event: %x, status: 0x%lx", (int)event, status_code);

    /* Update scan complete flag. */
    g_wifi_scan_complete        = true;
    g_wifi_scan_callback_status = status_code;

    if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
        /* WiseConnect v2 already passes the real status in status_code.
         * Do NOT dereference data as sl_status_t — that corrupts 0x10021 into garbage. */
        return status_code;
    }

    if ((g_wifi_scan_result != NULL) && (data != NULL)) {
        memcpy(g_wifi_scan_result, data, g_wifi_scan_buf_size);
    }

    return SL_STATUS_OK;
}

static void _tkl_wifi_station_linkup(void)
{
    sl_net_wifi_client_profile_t *profile           = &g_wifi_client_profile;
    struct netif                 *wifi_client_netif = &g_wifi_sta_context.netif;

    if (SL_IP_MANAGEMENT_STATIC_IP == profile->ip.mode) {
        ip4_addr_t ipaddr  = {0};
        ip4_addr_t gateway = {0};
        ip4_addr_t netmask = {0};
        uint8_t   *address = &(profile->ip.ip.v4.ip_address.bytes[0]);

        IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.gateway.bytes[0]);
        IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.netmask.bytes[0]);
        IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

        netifapi_netif_set_addr(wifi_client_netif, &ipaddr, &netmask, &gateway);
    }

    netifapi_netif_set_up(wifi_client_netif);
    netifapi_netif_set_link_up(wifi_client_netif);

    if (SL_IP_MANAGEMENT_DHCP == profile->ip.mode) {
#if LWIP_IPV4 && LWIP_DHCP
        ip_addr_set_zero_ip4(&(wifi_client_netif->ip_addr));
        ip_addr_set_zero_ip4(&(wifi_client_netif->netmask));
        ip_addr_set_zero_ip4(&(wifi_client_netif->gw));
        dhcp_start(wifi_client_netif);
#endif /* LWIP_IPV4 && LWIP_DHCP */
        /*
         * Enable IPV6
         */
        //! Wait for DHCP to acquire IP Address
        while (!dhcp_supplied_address(wifi_client_netif)) {
            osDelay(100);
        }
        wifi_sta_conn_status = WSS_GOT_IP;
        TKL_LOGD("DHCP IP: %s", ip4addr_ntoa((const ip4_addr_t *)&wifi_client_netif->ip_addr));
    }

#if LWIP_IPV6
#if LWIP_IPV6_AUTOCONFIG
    wifi_client_netif->ip6_autoconfig_enabled = 1;
#endif /* LWIP_IPV6_AUTOCONFIG */
    netif_create_ip6_linklocal_address(wifi_client_netif, 1);
#endif
    return;
}

static sl_status_t _tkl_wifi_ap_connected_event_handler(sl_wifi_event_t event, sl_status_t status_code, const void *data,
                                                        uint32_t data_length, const void *arg)
{
    UNUSED_PARAMETER(data_length);
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(status_code);

    const uint8_t *mac = (const uint8_t *)data;

    TKL_LOGD("Remote Client connected: " MACSTR, MAC2STR(mac));

    return SL_STATUS_OK;
}

static sl_status_t _tkl_wifi_ap_disconnected_event_handler(sl_wifi_event_t event, sl_status_t status_code,
                                                           const void *data, uint32_t data_length, const void *arg)
{
    UNUSED_PARAMETER(data_length);
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(status_code);

    const uint8_t *mac = (const uint8_t *)data;

    TKL_LOGD("Remote Client disconnected: " MACSTR, MAC2STR(mac));

    return SL_STATUS_OK;
}

#if defined(ENABLE_LWIP_DHCPD) && (ENABLE_LWIP_DHCPD == 1)
static void _dhcps_lease_cb(u8_t *client_ip)
{
    TKL_LOGD("Client lease " IP4STR, IP4TOSTR(client_ip));
}
#endif

void _tkl_wifi_ap_linkup(void)
{
    sl_net_wifi_ap_profile_t *profile       = &g_wifi_ap_profile;
    struct netif             *wifi_ap_netif = &g_wifi_ap_context.netif;
    ip4_addr_t                ipaddr        = {0};
    ip4_addr_t                gateway       = {0};
    ip4_addr_t                netmask       = {0};

    if (SL_IP_MANAGEMENT_STATIC_IP == profile->ip.mode) {
        uint8_t *address = &(profile->ip.ip.v4.ip_address.bytes[0]);

        TKL_LOGV("AP Static: ip %08lx gw %08lx mask %08lx", profile->ip.ip.v4.ip_address.value,
                 profile->ip.ip.v4.gateway.value, profile->ip.ip.v4.netmask.value);

        IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.gateway.bytes[0]);
        IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.netmask.bytes[0]);
        IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

        netifapi_netif_set_addr(wifi_ap_netif, &ipaddr, &netmask, &gateway);
    }

    netifapi_netif_set_up(wifi_ap_netif);
    netifapi_netif_set_link_up(wifi_ap_netif);
#if defined(ENABLE_LWIP_DHCPD) && (ENABLE_LWIP_DHCPD == 1)
    dhcps_set_new_lease_cb(_dhcps_lease_cb);
    dhcps_start(wifi_ap_netif, ipaddr);
#endif
}

static void _tkl_wifi_ap_linkdown(void)
{
    struct netif *wifi_ap_netif = &g_wifi_ap_context.netif;

    netifapi_netif_set_down(wifi_ap_netif);
    netifapi_netif_set_link_down(wifi_ap_netif);
#if defined(ENABLE_LWIP_DHCPD) && (ENABLE_LWIP_DHCPD == 1)
    dhcps_stop(wifi_ap_netif);
#endif
}

#if LWIP_NETIF_EXT_STATUS_CALLBACK
/*
 * Registered so lwIP's ext-callback slot belongs to this adapter, but
 * deliberately a no-op: link and address transitions are reported upward
 * through the tkl_wifi event callbacks, not from here. The IPv6/link-status
 * bodies this used to carry were commented out, not implemented.
 */
static void _platform_netif_ext_callback_function(struct netif *netif, netif_nsc_reason_t reason,
                                                  const netif_ext_callback_args_t *args)
{
    LWIP_UNUSED_ARG(netif);
    LWIP_UNUSED_ARG(reason);
    LWIP_UNUSED_ARG(args);
}
#endif /* LWIP_NETIF_EXT_STATUS_CALLBACK */

static sl_status_t _tkl_wifi_scan_wait_complete(sl_status_t start_status)
{
    if (SL_STATUS_IN_PROGRESS != start_status) {
        return start_status;
    }

    uint32_t start = osKernelGetTickCount();
    while (!g_wifi_scan_complete && (osKernelGetTickCount() - start) <= SL_SI91X_WIFI_SCAN_TIMEOUT) {
        osDelay(20);
    }

    return g_wifi_scan_complete ? g_wifi_scan_callback_status : SL_STATUS_TIMEOUT;
}

static OPERATE_RET _tkl_wifi_scan_prepare_retry(sl_status_t status)
{
    TKL_LOGE("WiFi Scan wait error 0x%lx", status);
    sl_wifi_stop_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE);

    if (status == SL_STATUS_TIMEOUT) {
        return OPRT_TIMEOUT;
    }

    /* Soft recovery first: keep BLE coex alive, only wake WiFi radio. */
    (void)_tkl_wifi_set_high_performance();
    tkl_system_sleep(50);

    if (status != SL_STATUS_SI91X_COMMAND_GIVEN_IN_INVALID_STATE) {
        return OPRT_COM_ERROR;
    }

#ifdef PRINT_DEBUG_LOG
    sl_debug_log_dump();
    hci_trace_dump();

    tkl_system_reset();
#endif

    /*
     * Full NWP reinit would tear down BLE under WLAN_BLE coex.
     * Prefer soft recovery + one more scan attempt.
     */
    TKL_LOGW("WiFi INVALID_STATE: soft recover for retry (no NWP reinit)");
    g_wifi_work_mode                    = WWM_STATION;
    g_wifi_config.boot_config.oper_mode = SL_SI91X_CLIENT_MODE;
    _tkl_wifi_register_callbacks();

    return OPRT_OK;
}

static OPERATE_RET _tkl_wifi_scan_build_ap_list(const int8_t *ssid, AP_IF_S **ap_ary, uint32_t *num)
{
    if ((g_wifi_scan_result == NULL) || (g_wifi_scan_result->scan_count == 0)) {
        *ap_ary = NULL;
        *num    = 0;
        return OPRT_COM_ERROR;
    }

    TKL_LOGD("WiFi Scan found %lu AP.", g_wifi_scan_result->scan_count);

    AP_IF_S *ap_items = (AP_IF_S *)tkl_system_malloc(g_wifi_scan_result->scan_count * sizeof(AP_IF_S));
    if (NULL == ap_items) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ap_items, 0, g_wifi_scan_result->scan_count * sizeof(AP_IF_S));

    uint32_t totals = 0;
    for (uint32_t i = 0; i < g_wifi_scan_result->scan_count; i++) {
        if (ssid != NULL) {
            size_t filter_len = _tkl_wifi_strnlen((const char *)ssid, WIFI_SSID_LEN);
            if (strncmp((const char *)ssid, (const char *)g_wifi_scan_result->scan_info[i].ssid, filter_len) != 0) {
                continue;
            }
        }
        ap_items[totals].channel  = g_wifi_scan_result->scan_info[i].rf_channel;
        ap_items[totals].rssi     = g_wifi_scan_result->scan_info[i].rssi_val;
        ap_items[totals].security = g_wifi_scan_result->scan_info[i].security_mode;
        ap_items[totals].s_len =
            (uint8_t)_tkl_wifi_strnlen((const char *)g_wifi_scan_result->scan_info[i].ssid,
                                       sizeof(g_wifi_scan_result->scan_info[i].ssid));
        if (ap_items[totals].s_len > WIFI_SSID_LEN) {
            ap_items[totals].s_len = WIFI_SSID_LEN;
        }
        memcpy(ap_items[totals].ssid, g_wifi_scan_result->scan_info[i].ssid, ap_items[totals].s_len);
        ap_items[totals].ssid[ap_items[totals].s_len] = '\0';
        memcpy(ap_items[totals].bssid, &g_wifi_scan_result->scan_info[i].bssid, 6);

        TKL_LOGD("ssid %-32s bssid " MACSTR " chan %d sec %d rssi %d", ap_items[totals].ssid,
                 MAC2STR(ap_items[totals].bssid), ap_items[totals].channel, ap_items[totals].security,
                 ap_items[totals].rssi);
        totals++;
    }

    if (totals == 0) {
        tkl_system_free(ap_items);
        *ap_ary = NULL;
        *num    = 0;
        return OPRT_COM_ERROR;
    }

    *ap_ary = ap_items;
    *num    = totals;
    return OPRT_OK;
}

/**
 * @brief set wifi station work status changed callback
 *
 * @param[in]      cb        the wifi station work status changed callback
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_init(WIFI_EVENT_CB cb)
{
    if (!g_wifi_initialized) {
        _tkl_wifi_register_callbacks();
        (void)_tkl_wifi_set_high_performance();

#if LWIP_NETIF_EXT_STATUS_CALLBACK
        netif_add_ext_callback(&_platform_netif_ext_callback, _platform_netif_ext_callback_function);
#endif /* LWIP_NETIF_EXT_STATUS_CALLBACK */

        g_wifi_initialized = true;
    }

    tkl_event_cb = cb;

    return OPRT_OK;
}

/**
 * @brief scan current environment and obtain the ap
 *        infos in current environment
 *
 * @param[in]       ssid        the specific ssid
 * @param[out]      ap_ary      current ap info array
 * @param[out]      num         the num of ar_ary
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 *
 * @note if ssid == NULL means scan all ap, otherwise means scan the specific ssid
 */
OPERATE_RET tkl_wifi_scan_ap(const int8_t *ssid, AP_IF_S **ap_ary, uint32_t *num)
{
    assert(NULL != ap_ary);
    assert(NULL != num);

    *num    = 0;
    *ap_ary = NULL;

    if (!g_wifi_initialized) {
        tkl_wifi_init(NULL);
    }

    _tkl_wifi_lp_hold_begin();

    sl_wifi_ssid_t               _ssid;
    sl_wifi_ssid_t              *specific_ssid = NULL;
    sl_wifi_scan_configuration_t scan_cfg      = default_wifi_scan_configuration;
    OPERATE_RET                  tkl_result    = OPRT_COM_ERROR;
    const uint8_t                max_attempts  = 2;

    if (ssid != NULL) {
        TKL_LOGD("Scan specific AP: %s", (const char *)ssid);
        specific_ssid = &_ssid;
        _tkl_wifi_ssid_copy(specific_ssid, (const char *)ssid);
    }

    for (uint8_t attempt = 0; attempt < max_attempts; attempt++) {
        g_wifi_scan_complete        = false;
        g_wifi_scan_callback_status = SL_STATUS_FAIL;
        g_wifi_scan_result          = (sl_wifi_scan_result_t *)tkl_system_malloc(g_wifi_scan_buf_size);
        if (g_wifi_scan_result == NULL) {
            return _tkl_wifi_lp_hold_end(OPRT_MALLOC_FAILED);
        }
        memset(g_wifi_scan_result, 0, g_wifi_scan_buf_size);

        sl_status_t status =
            sl_wifi_start_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, specific_ssid, &scan_cfg);
        status = _tkl_wifi_scan_wait_complete(status);

        if (status == SL_STATUS_OK) {
            tkl_result = _tkl_wifi_scan_build_ap_list(ssid, ap_ary, num);
            tkl_system_free(g_wifi_scan_result);
            g_wifi_scan_result = NULL;
            return _tkl_wifi_lp_hold_end(tkl_result);
        }

        tkl_result = _tkl_wifi_scan_prepare_retry(status);
        tkl_system_free(g_wifi_scan_result);
        g_wifi_scan_result = NULL;

        if ((tkl_result != OPRT_OK) || ((attempt + 1) >= max_attempts)) {
            return _tkl_wifi_lp_hold_end((tkl_result == OPRT_OK) ? OPRT_COM_ERROR : tkl_result);
        }

        TKL_LOGW("WiFi scan retry %u after recover", (unsigned)(attempt + 1));
        tkl_system_sleep(100);
    }

    return _tkl_wifi_lp_hold_end(OPRT_COM_ERROR);
}

/**
 * @brief release the memory malloced in <tkl_wifi_ap_scan>
 *        if needed. tuyaos will call this function when the
 *        ap info is no use.
 *
 * @param[in]       ap          the ap info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_release_ap(AP_IF_S *ap)
{
    if (ap != NULL) {
        tkl_system_free(ap);
    }

    return OPRT_OK;
}

/**
 * @brief start a soft ap
 *
 * @param[in]       cfg         the soft ap config
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_start_ap(const WF_AP_CFG_IF_S *cfg)
{
    assert(NULL != cfg);
    sl_status_t        status;
    sl_wifi_security_t auth_mode = SL_WIFI_OPEN;

    /*
     * SiWx917: SoftAP + BLE cannot coexist. With WIFI_INIT_MODE_STA the NWP
     * boots in CLIENT_MODE — calling sl_wifi_start_ap corrupts NWP state and
     * later STA scan returns COMMAND_GIVEN_IN_INVALID_STATE (0x10021).
     * Reject SoftAP here so BLE netcfg remains the provisioning path.
     */
    if (_tkl_wifi_is_sta_only()) {
        TKL_LOGW("SoftAP skipped: STA-only + BLE coex (use BLE netcfg)");
        return OPRT_NOT_SUPPORTED;
    }

    // SSID
    g_wifi_ap_profile.config.ssid.length = cfg->s_len;
    memcpy(g_wifi_ap_profile.config.ssid.value, cfg->ssid, cfg->s_len);

    // IP: Convert string IPs to binary and assign
    ip4_addr_t ip_addr;
    ip4_addr_t gw_addr;
    ip4_addr_t mask_addr;
    ip4addr_aton(cfg->ip.ip, &ip_addr);
    ip4addr_aton(cfg->ip.gw, &gw_addr);
    ip4addr_aton(cfg->ip.mask, &mask_addr);
    g_wifi_ap_profile.ip.ip.v4.ip_address.value = ip_addr.addr;
    g_wifi_ap_profile.ip.ip.v4.gateway.value    = gw_addr.addr;
    g_wifi_ap_profile.ip.ip.v4.netmask.value    = mask_addr.addr;

    TKL_LOGD("AP ssid %s, pwd %s, ch %d, sec %d", cfg->ssid, cfg->passwd, cfg->chan, cfg->md);
    // TKL_LOGD("   ip %08x gw %08x mask %08x", ip_addr.addr, gw_addr.addr, mask_addr.addr);

    g_wifi_ap_profile.config.channel.channel = cfg->chan;
    g_wifi_ap_profile.config.beacon_interval = cfg->ms_interval;
    g_wifi_ap_profile.config.maximum_clients = cfg->max_conn;

    if (cfg->p_len && cfg->passwd[0] != '\0') {
        switch (cfg->md) {
        case WAAM_OPEN:
            auth_mode = SL_WIFI_OPEN;
            break;
        case WAAM_WEP:
            auth_mode = SL_WIFI_WEP;
            break;
        case WAAM_WPA_PSK:
            auth_mode = SL_WIFI_WPA;
            break;
        case WAAM_WPA2_PSK:
            auth_mode = SL_WIFI_WPA2;
            break;
        case WAAM_WPA_WPA2_PSK:
            auth_mode = SL_WIFI_WPA_WPA2_MIXED;
            break;
        case WAAM_WPA_WPA3_SAE:
            auth_mode = SL_WIFI_WPA_WPA2_MIXED;
            break;
        default:
            auth_mode = SL_WIFI_OPEN;
            break;
        }
    }

    g_wifi_ap_profile.config.security = auth_mode;
    TKL_LOGD("ap security %d", auth_mode);

    // Passwd
    if (cfg->p_len && cfg->md != WAAM_OPEN) {
        TKL_LOGD("pwd %s, length %d", cfg->passwd, cfg->p_len);
        status = sl_net_set_credential(g_wifi_ap_profile.config.credential_id, SL_NET_WIFI_PSK,
                                       (const void *)cfg->passwd, cfg->p_len);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("sl_net_set_credential %ld error %lx", g_wifi_ap_profile.config.credential_id, status);
            return OPRT_COM_ERROR;
        }
    }

    status = sl_wifi_start_ap(SL_WIFI_AP_2_4GHZ_INTERFACE, &g_wifi_ap_profile.config);
    if (status == SL_STATUS_OK) {
        _tkl_wifi_ap_linkup();
    } else {
        TKL_LOGE("sl_wifi_start_ap failed: 0x%lx", status);
    }

    return status == SL_STATUS_OK ? OPRT_OK : OPRT_COM_ERROR;
}

/**
 * @brief stop a soft ap
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_stop_ap(void)
{
    sl_status_t status;

    _tkl_wifi_ap_linkdown();

    status = sl_wifi_stop_ap(SL_WIFI_AP_2_4GHZ_INTERFACE);
    if (status != SL_STATUS_OK) {
        TKL_LOGD("sl_wifi_stop_ap error %08lx", status);
    }

    return OPRT_OK;
}

/**
 * @brief set wifi interface work channel
 *
 * @param[in]       chan        the channel to set
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_cur_channel(const uint8_t chan)
{
    TKL_UNUSED(chan);
    TKL_LOGD("tkl_wifi_set_cur_channel %d", chan);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief get wifi interface work channel
 *
 * @param[out]      chan        the channel wifi works
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_cur_channel(uint8_t *chan)
{
    TKL_UNUSED(chan);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief enable / disable wifi sniffer mode.
 *        if wifi sniffer mode is enabled, wifi recv from
 *        packages from the air, and user shoud send these
 *        packages to tuya-sdk with callback <cb>.
 *
 * @param[in]       en          enable or disable
 * @param[in]       cb          notify callback
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_sniffer(const BOOL_T en, const SNIFFER_CALLBACK cb)
{
    TKL_UNUSED(en);
    TKL_UNUSED(cb);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief get wifi ip info.when wifi works in
 *        ap+station mode, wifi has two ips.
 *
 * @param[in]       wf          wifi function type
 * @param[out]      ip          the ip addr info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_ip(const WF_IF_E wf, NW_IP_S *ip)
{
    if (ip == NULL) {
        return OPRT_INVALID_PARM;
    }

    struct netif *p_netif = NULL;

    if (WF_STATION == wf) {
        if (WSS_GOT_IP != wifi_sta_conn_status) {
            return OPRT_COM_ERROR;
        }
        p_netif = &g_wifi_sta_context.netif;
    } else if (WF_AP == wf) {
        p_netif = &g_wifi_ap_context.netif;
    } else {
        return OPRT_NOT_SUPPORTED;
    }

    TKL_LOGV("%s: ip %08x gw %08x mask %08x", WF_STATION == wf ? "STA" : "AP ", p_netif->ip_addr.addr, p_netif->gw.addr,
             p_netif->netmask.addr);

    NW_IP_S *const nw_ip = ip;
    nw_ip->ip[0]   = '\0';
    nw_ip->mask[0] = '\0';
    nw_ip->gw[0]   = '\0';

    (void)ip4addr_ntoa_r((const ip4_addr_t *)&p_netif->ip_addr, nw_ip->ip, sizeof(nw_ip->ip));
    (void)ip4addr_ntoa_r((const ip4_addr_t *)&p_netif->netmask, nw_ip->mask, sizeof(nw_ip->mask));
    (void)ip4addr_ntoa_r((const ip4_addr_t *)&p_netif->gw, nw_ip->gw, sizeof(nw_ip->gw));

    return OPRT_OK;
}

/**
 * @brief set wifi ip info.when wifi works in
 *        ap+station mode, wifi has two ips.
 *
 * @param[in]       wf     wifi function type
 * @param[in]       ip     the ip addr info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_ip(WF_IF_E wf, NW_IP_S *ip)
{
    TKL_UNUSED(wf);
    TKL_UNUSED(ip);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief set wifi mac info.when wifi works in
 *        ap+station mode, wifi has two macs.
 *
 * @param[in]       wf          wifi function type
 * @param[in]       mac         the mac info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_mac(const WF_IF_E wf, const NW_MAC_S *mac)
{
    TKL_UNUSED(wf);
    TKL_UNUSED(mac);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief get wifi mac info.when wifi works in
 *        ap+station mode, wifi has two macs.
 *
 * @param[in]       wf          wifi function type
 * @param[out]      mac         the mac info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_mac(const WF_IF_E wf, NW_MAC_S *mac)
{
    sl_mac_address_t    mac_addr;
    sl_status_t         status;
    sl_wifi_interface_t interface;

    interface = (wf == WF_AP) ? SL_WIFI_AP_INTERFACE : SL_WIFI_CLIENT_INTERFACE;
    status    = sl_wifi_get_mac_address(interface, &mac_addr);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("MAC address [%d] failed %lx", interface, status);
        return OPRT_COM_ERROR;
    }

    memcpy(mac, &mac_addr, sizeof(NW_MAC_S));

    return OPRT_OK;
}

/**
 * @brief set wifi work mode
 *
 * @param[in]       mode        wifi work mode
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_work_mode(const WF_WK_MD_E mode)
{
    assert(mode < WWM_UNKNOWN);

    TKL_LOGD("wifi_set_work_mode %d", mode);
    if (mode == g_wifi_work_mode) {
        return OPRT_OK;
    }

    switch (mode) {
    case WWM_STATION:
        g_wifi_config.boot_config.oper_mode = SL_SI91X_CLIENT_MODE;
        break;

    case WWM_SOFTAP:
        if (_tkl_wifi_is_sta_only()) {
            TKL_LOGW("wifi_set_work_mode SoftAP rejected: STA-only + BLE coex");
            return OPRT_NOT_SUPPORTED;
        }
        g_wifi_config.boot_config.oper_mode = SL_SI91X_ACCESS_POINT_MODE;
        break;

    case WWM_STATIONAP:
        if (_tkl_wifi_is_sta_only()) {
            TKL_LOGW("wifi_set_work_mode STATIONAP rejected: STA-only + BLE coex");
            return OPRT_NOT_SUPPORTED;
        }
        g_wifi_config.boot_config.oper_mode = SL_SI91X_CONCURRENT_MODE;
        break;

    case WWM_POWERDOWN:
    case WWM_SNIFFER:
    default:
        return OPRT_COM_ERROR;
    }

    g_wifi_work_mode = mode;

    return OPRT_OK;
}

/**
 * @brief get wifi work mode
 *
 * @param[out]      mode        wifi work mode
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_work_mode(WF_WK_MD_E *mode)
{
    *mode = g_wifi_work_mode;

    return OPRT_OK;
}

/**
 * @brief : get ap info for fast connect
 * @param[out]      fast_ap_info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_connected_ap_info(FAST_WF_CONNECTED_AP_INFO_T **fast_ap_info)
{
    TKL_UNUSED(fast_ap_info);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief get wifi bssid
 *
 * @param[out]      mac         uplink mac
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_get_bssid(uint8_t *mac)
{
    TKL_UNUSED(mac);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief set wifi country code
 *
 * @param[in]       ccode  country code
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_country_code(const COUNTRY_CODE_E ccode)
{
    sl_wifi_region_code_t region = SL_WIFI_DEFAULT_REGION;

    switch (ccode) {
    case COUNTRY_CODE_CN: {
        region = SL_WIFI_REGION_CN;
        break;
    }
    case COUNTRY_CODE_US: {
        region = SL_WIFI_REGION_US;
        break;
    }
    case COUNTRY_CODE_JP: {
        region = SL_WIFI_REGION_JP;
        break;
    }
    case COUNTRY_CODE_EU: {
        region = SL_WIFI_REGION_EU;
        break;
    }
    default:
        return OPRT_NOT_SUPPORTED;
    }

    g_wifi_config.region_code = region;

    return OPRT_OK;
}

/**
 * @brief do wifi calibration
 *
 * @note called when test wifi
 *
 * @return true on success. faile on failure
 */
BOOL_T tkl_wifi_set_rf_calibrated(void)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief set wifi lowpower mode
 *
 * @param[in]       enable      enbale lowpower mode
 * @param[in]       dtim     the wifi dtim
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_set_lp_mode(const BOOL_T enable, const uint8_t dtim)
{
    g_wifi_lp_wanted = enable;
    if (enable) {
        g_wifi_lp_dtim = dtim;
    }

    /* Scan/join forced HIGH_PERFORMANCE; remember the request and apply later. */
    if (g_wifi_lp_hold > 0) {
        TKL_LOGI("NWP LP request deferred enable=%d dtim=%u hold=%u", (int)enable, (unsigned)dtim,
                 (unsigned)g_wifi_lp_hold);
        return OPRT_OK;
    }

    return _tkl_wifi_apply_lp_profile(enable, dtim);
}

/**
 * @brief : fast connect
 * @param[in]      fast_ap_info
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_station_fast_connect(const FAST_WF_CONNECTED_AP_INFO_T *fast_ap_info)
{
    TKL_UNUSED(fast_ap_info);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief connect wifi with ssid and passwd
 *
 * @param[in]       ssid
 * @param[in]       passwd
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_station_connect(const int8_t *ssid, const int8_t *passwd)
{
    sl_status_t        status;
    OPERATE_RET        tkl_result;
    int                retry;
    AP_IF_S           *ap_info      = NULL;
    uint32_t           ap_info_nums = 0;
    sl_wifi_security_t security     = SL_WIFI_SECURITY_UNKNOWN;

    if (ssid == NULL || passwd == NULL) {
        return OPRT_INVALID_PARM;
    }

    // Add 1 byte to the SSID and PSK buffer to allow room for /0 byte.
    // so that SSID and PSK string can be separated
    uint8_t _ssid[WIFI_SSID_LEN + 1]  = {0};
    uint8_t _psk[WIFI_PASSWD_LEN + 1] = {0};

    memcpy(_ssid, ssid, WIFI_SSID_LEN);
    memcpy(_psk, passwd, WIFI_PASSWD_LEN);

    tkl_system_sleep(1000);
    _tkl_wifi_lp_hold_begin();

    tkl_result = tkl_wifi_scan_ap(ssid, &ap_info, &ap_info_nums);
    if ((tkl_result == OPRT_OK) && (ap_info_nums > 0) && (ap_info != NULL)) {
        security = ap_info[0].security;
        tkl_system_free(ap_info);
        ap_info = NULL;
    } else {
        /* Scan may fail under BLE coex; still try join with common security. */
        TKL_LOGW("scan before connect failed (%d), fallback WPA/WPA2 mixed", tkl_result);
        security = SL_WIFI_WPA_WPA2_MIXED;
        if (ap_info != NULL) {
            tkl_system_free(ap_info);
            ap_info = NULL;
        }
    }

    TKL_LOGD("Connect to SSID: %s, security: %d", _ssid, (int)security);
    status = sl_net_set_credential(g_wifi_client_profile.config.credential_id, SL_NET_WIFI_PSK, _psk,
                                   _tkl_wifi_strnlen((const char *)_psk, WIFI_PASSWD_LEN));
    if (status == SL_STATUS_OK) {
        /* Connect to WIFI with specified PSK if set credential success. */
        size_t ssid_len = _tkl_wifi_strnlen((const char *)ssid, WIFI_SSID_LEN);
        g_wifi_client_profile.config.ssid.length = (uint8_t)ssid_len;
        memcpy(g_wifi_client_profile.config.ssid.value, ssid, ssid_len);
        g_wifi_client_profile.config.security = security;

        status               = SL_STATUS_FAIL;
        retry                = SL_SI91X_WIFI_CONNECT_MAX_RETRY;
        wifi_sta_conn_status = WSS_CONNECTING;
        while (status != SL_STATUS_OK && retry > 0) {
            status = sl_wifi_connect(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &g_wifi_client_profile.config,
                                     SL_SI91X_WIFI_CONN_TIMEOUT);
            TKL_LOGD("sl_wifi_connect result %lx [%d]", status, SL_SI91X_WIFI_CONNECT_MAX_RETRY - retry);
            if (status != SL_STATUS_OK) {
                tkl_system_sleep(200);
            }
            retry--;
        }
    } else {
        TKL_LOGD("sli_net_set_credential error %lx", status);
    }

    if (status == SL_STATUS_OK) {
        wifi_sta_conn_status = WSS_CONN_SUCCESS;
        _tkl_wifi_station_linkup();
        tkl_result = OPRT_OK;
    } else {
        tkl_result = OPRT_COM_ERROR;
    }

    if (tkl_event_cb != NULL) {
        tkl_event_cb(status == SL_STATUS_OK ? WFE_CONNECTED : WFE_CONNECT_FAILED, NULL);
    }

    return _tkl_wifi_lp_hold_end(tkl_result);
}

/**
 * @brief disconnect wifi from connect ap
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_station_disconnect(void)
{
    sl_status_t status;

    if (wifi_sta_conn_status >= WSS_CONN_SUCCESS) {
        status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("sl_wifi_disconnect error %lx", status);
        }
    }

    return OPRT_OK;
}

/**
 * @brief get wifi connect rssi
 *
 * @param[out]      rssi        the return rssi
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_station_get_conn_ap_rssi(int8_t *rssi)
{
    int32_t     _rssi = 0;
    sl_status_t status;

    status = sl_wifi_get_signal_strength(SL_WIFI_CLIENT_INTERFACE, &_rssi);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("sl_wifi_get_signal_strength error %ld", status);
        return OPRT_COM_ERROR;
    }

    *rssi = (int8_t)_rssi;

    return OPRT_OK;
}

/**
 * @brief get wifi station work status
 *
 * @param[out]      stat        the wifi station work status
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_station_get_status(WF_STATION_STAT_E *stat)
{
    TKL_UNUSED(stat);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief send wifi management
 *
 * @param[in]       buf         pointer to buffer
 * @param[in]       len         length of buffer
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_send_mgnt(const uint8_t *buf, const uint32_t len)
{
    TKL_UNUSED(buf);
    TKL_UNUSED(len);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief register receive wifi management callback
 *
 * @param[in]       enable
 * @param[in]       recv_cb     receive callback
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_register_recv_mgnt_callback(const BOOL_T enable, const WIFI_REV_MGNT_CB recv_cb)
{
    TKL_UNUSED(enable);
    TKL_UNUSED(recv_cb);

    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief wifi ioctl
 *
 * @param[in]       cmd     refer to WF_IOCTL_CMD_E
 * @param[in]       args    args associated with the command
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wifi_ioctl(WF_IOCTL_CMD_E cmd, void *args)
{
    TKL_UNUSED(cmd);
    TKL_UNUSED(args);

    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_wifi_get_all_sta_info(WF_STA_INFO_S **sta_ary, uint32_t *num)
{
    TKL_UNUSED(num);
    TKL_UNUSED(sta_ary);

    return OPRT_NOT_SUPPORTED;
}

void *tkl_wifi_station_get_context(void)
{
    return &g_wifi_sta_context;
}

void *tkl_wifi_get_configuration(void)
{
    return &g_wifi_config;
}
