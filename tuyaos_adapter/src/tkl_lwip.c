/*****************************************************************************//**
 * @file tkl_lwip.c
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
#include "sl_net_for_lwip.h"
#include "sl_rsi_utility.h"
#include "sl_status.h"
#include "sl_wifi.h"

#include "tuya_error_code.h"
#include "sl_tuya_log.h"
#include "tkl_lwip.h"
#include "tkl_wifi.h"

#include "lwip/def.h"
#include "lwip/err.h"
#include "lwip/icmp.h"
#include "lwip/inet.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "netif/etharp.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define LWIP_FRAME_ALIGNMENT 60
#define STRUCT_PBUF          ((struct pbuf *)0)

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

extern sl_net_wifi_lwip_context_t g_wifi_sta_context;
extern sl_net_wifi_lwip_context_t g_wifi_ap_context;

uint32_t g_overrun_count = 0;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief ethernet interface hardware init
 *
 * @param[in]      netif     the netif to which to send the packet
 * @return  err_t  SEE "err_enum_t" in "lwip/err.h" to see the lwip err(ERR_OK: SUCCESS other:fail)
 */
OPERATE_RET tkl_ethernetif_init(TKL_NETIF_HANDLE netif)
{

    struct netif       *p_netif   = (struct netif *)netif;
    sl_wifi_interface_t interface = SL_WIFI_CLIENT_INTERFACE;
    sl_mac_address_t    mac_addr;
    sl_status_t         status;

    if (p_netif == NULL) {
        return ERR_ARG;
    }
    if ((uint32_t)p_netif == (uint32_t)&g_wifi_sta_context.netif) {
        interface = SL_WIFI_CLIENT_INTERFACE;
    } else if ((uint32_t)p_netif == (uint32_t)&g_wifi_ap_context.netif) {
        interface = SL_WIFI_AP_INTERFACE;
    } else {
        return ERR_ARG;
    }

    // Request MAC address
    status = sl_wifi_get_mac_address(interface, &mac_addr);
    if (status != SL_STATUS_OK) {
        TKL_LOGD("MAC address ifc%d error %lx", interface, status);
        return ERR_ARG;
    }

    p_netif->hwaddr[0] = mac_addr.octet[0];
    p_netif->hwaddr[1] = mac_addr.octet[1];
    p_netif->hwaddr[2] = mac_addr.octet[2];
    p_netif->hwaddr[3] = mac_addr.octet[3];
    p_netif->hwaddr[4] = mac_addr.octet[4];
    p_netif->hwaddr[5] = mac_addr.octet[5];

    return ERR_OK;
}

/**
 * @brief ethernet interface sendout the pbuf packet
 *
 * @param[in]      netif     the netif to which to send the packet
 * @param[in]      p         the packet to be send, in pbuf mode
 * @return  err_t  SEE "err_enum_t" in "lwip/err.h" to see the lwip err(ERR_OK: SUCCESS other:fail)
 */
OPERATE_RET tkl_ethernetif_output(TKL_NETIF_HANDLE netif, TKL_PBUF_HANDLE p)
{

    struct pbuf        *p_buf   = (struct pbuf *)p;
    struct netif       *p_netif = (struct netif *)netif;
    WF_WK_MD_E          mode;
    sl_wifi_interface_t interface = SL_WIFI_CLIENT_INTERFACE;
    sl_status_t         status;

    if (p_buf == NULL || p_netif == NULL) {
        return ERR_ARG;
    }

    // if ((uint32_t)p_netif == (uint32_t)&g_wifi_sta_context.netif)
    // {
    //     interface = SL_WIFI_CLIENT_INTERFACE;
    // }
    // else if ((uint32_t)p_netif == (uint32_t)&g_wifi_ap_context.netif)
    // {
    //     interface = SL_WIFI_AP_INTERFACE;
    // }
    // else
    // {
    //     return ERR_ARG;
    // }

    tkl_wifi_get_work_mode(&mode);
    if (mode == WWM_SOFTAP) {
        interface = SL_WIFI_AP_INTERFACE;
    } else {
        interface = SL_WIFI_CLIENT_INTERFACE;
    }

    status = sl_wifi_send_raw_data_frame(interface, (uint8_t *)p_buf->payload, p_buf->len);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("WiFi frame ifc%d send error %lx", interface, status);
        return ERR_IF;
    }

    return ERR_OK;
}

/**
 * @brief ethernet interface recv the packet
 *
 * @param[in]      netif       the netif to which to recieve the packet
 * @param[in]      total_len   the length of the packet recieved from the netif
 * @return  void
 */
void tkl_ethernetif_recv(TKL_NETIF_HANDLE netif, uint8_t *b, uint16_t len)
{
    struct netif *p_netif = (struct netif *)netif;
    struct pbuf  *p, *q;
    uint32_t      bufferoffset;

    if (len <= 0) {
        return;
    }

    if (len < LWIP_FRAME_ALIGNMENT) { /* 60 : LWIP frame alignment */
        len = LWIP_FRAME_ALIGNMENT;
    }

    // Drop packets originated from the same interface and is not destined for the said interface
    const uint8_t *src_mac = b + p_netif->hwaddr_len;
    const uint8_t *dst_mac = b;

#if LWIP_IPV6
    if (!(ip6_addr_ispreferred(netif_ip6_addr_state(netif, 0))) &&
        (memcmp(netif->hwaddr, src_mac, netif->hwaddr_len) == 0) &&
        (memcmp(netif->hwaddr, dst_mac, netif->hwaddr_len) != 0)) {
        TKL_LOGV("DROP, [%02x:%02x:%02x:%02x:%02x:%02x]<-[%02x:%02x:%02x:%02x:%02x:%02x] type=%02x%02x", dst_mac[0],
                 dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5], src_mac[0], src_mac[1], src_mac[2],
                 src_mac[3], src_mac[4], src_mac[5], b[12], b[13]);
        return;
    }
#endif

    /* We allocate a pbuf chain of pbufs from the Lwip buffer pool
     * and copy the data to the pbuf chain
     */
    if ((p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL)) != STRUCT_PBUF) {
        for (q = p, bufferoffset = 0; q != NULL; q = q->next) {
            memcpy((uint8_t *)q->payload, (uint8_t *)b + bufferoffset, q->len);
            bufferoffset += q->len;
        }

        TKL_LOGV("ACCEPT %ld, [%02x:%02x:%02x:%02x:%02x:%02x]<-[%02x:%02x:%02x:%02x:%02x:%02x] "
                 "type=%02x%02x",
                 bufferoffset, dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5], src_mac[0],
                 src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], b[12], b[13]);

        if (p_netif->input(p, netif) != ERR_OK) {
            g_overrun_count++;
            pbuf_free(p);
        }
    } else {
        g_overrun_count++;
    }
}

struct netif *tkl_lwip_get_netif_by_index(int net_if_idx)
{
    if (net_if_idx == 0) {
        return &g_wifi_sta_context.netif;
    } else if (net_if_idx == 1) {
        return &g_wifi_ap_context.netif;
    }

    return NULL;
}

sl_status_t sl_si91x_host_process_data_frame(sl_wifi_interface_t interface, sl_wifi_buffer_t *buffer)
{
    void                    *packet;
    struct netif            *ifp;
    sl_wifi_system_packet_t *rsi_pkt;
    WF_WK_MD_E               mode;

    packet  = sl_si91x_host_get_buffer_data(buffer, 0, NULL);
    rsi_pkt = (sl_wifi_system_packet_t *)packet;
    TKL_LOGV("WiFi ifc %d, r %d", interface, rsi_pkt->length);

    tkl_wifi_get_work_mode(&mode);
    if (mode == WWM_SOFTAP) {
        ifp = &g_wifi_ap_context.netif;
    } else {
        ifp = &g_wifi_sta_context.netif;
    }

    tkl_ethernetif_recv(ifp, rsi_pkt->data, rsi_pkt->length);

    return SL_STATUS_OK;
}
