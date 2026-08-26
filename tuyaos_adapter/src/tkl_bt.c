/*****************************************************************************//**
 * @file tkl_bt.c
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
#include "ble_config.h"
#include "rsi_ble.h"
#include "rsi_ble_apis.h"
#include "rsi_ble_common_config.h"
#include "rsi_bt_common.h"
#include "rsi_bt_common_apis.h"
#include "rsi_common_apis.h"

#include "FreeRTOS.h"

#include "tuya_error_code.h"
#include "tuya_cloud_types.h"
#include "tkl_bluetooth.h"
#include "tkl_hci.h"
#include "tkl_log.h"
#include "tkl_memory.h"
#include "tkl_output.h"
#include "tkl_system.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define BLE_HCI_CMD_MSG_TYPE  0x01 /* command message type */
#define BLE_HCI_ACL_MSG_TYPE  0x02 /* ACL data message type */
#define BLE_HCI_SYNC_MSG_TYPE 0x03 /* Synchronous data message type */
#define BLE_HCI_EVT_MSG_TYPE  0x04 /* event message type */

#define BLE_HCI_CMD_MSG_TYPE_LEN        1
#define BLE_HCI_ACL_MSG_TYPE_LEN        1
#define BLE_HCI_RCP_RX_PKT_TYPE_OFFSET  12  /* offset from resp_buf->data to HCI pkt type in si91x host desc */

#define BLE_HCI_FRAME_TRACES 0
#define BLE_HCI_PSRAM_TRACES 0

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static TKL_HCI_FUNC_CB tuya_ble_hci_rx_cmd_hs_cb;
static TKL_HCI_FUNC_CB tuya_ble_hci_rx_acl_hs_cb;
static uint16_t        opcode = 0;

#if BLE_HCI_PSRAM_TRACES
#define HCI_TRACE_MAX_ENTRIES 4096

typedef struct {
    uint8_t *data;      // Malloc'd data
    uint16_t length;    // Data length
    uint8_t  direction; // 0=TX, 1=RX
    uint32_t timestamp; // Optional: system tick
} hci_trace_entry_t;

static hci_trace_entry_t g_hci_trace[HCI_TRACE_MAX_ENTRIES];
static uint32_t          g_hci_trace_index = 0;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

static void hci_trace_log(uint8_t dir, const uint8_t *data, uint16_t len)
{
    if (g_hci_trace_index >= HCI_TRACE_MAX_ENTRIES) {
        return;
    }

    hci_trace_entry_t *entry = &g_hci_trace[g_hci_trace_index];

    entry->data = tkl_system_malloc(len);
    if (entry->data == NULL) {
        TKL_LOGE("HCI trace malloc failed");
        return;
    }

    memcpy(entry->data, data, len);
    entry->length    = len;
    entry->direction = dir;
    entry->timestamp = tkl_system_get_millisecond();

    g_hci_trace_index++;
}

void hci_trace_dump(void)
{
    TKL_LOGI("=== HCI Trace Dump ===");
    TKL_LOGI("Total packets: %ld", g_hci_trace_index);
    TKL_LOGI("Timestamp | TXCount | Direction | Data");
    char hdr[64];
    int  tcount = 0;
    for (uint32_t i = 0; i < g_hci_trace_index; i++) {
        hci_trace_entry_t *entry = &g_hci_trace[i];
        if (entry->direction == 0) {
            tcount++;
            snprintf(hdr, sizeof(hdr), "%08ld | %03d | ---> | ", entry->timestamp, tcount);
        } else {
            snprintf(hdr, sizeof(hdr), "%08ld |     | <--- | ", entry->timestamp);
        }

        log_printhex_no_newline(hdr, entry->data, entry->length);
    }

    TKL_LOGI("=== End of HCI Trace ===");
}

static void hci_trace_free(void)
{
    for (uint32_t i = 0; i < g_hci_trace_index; i++) {
        if (g_hci_trace[i].data != NULL) {
            tkl_system_free(g_hci_trace[i].data);
            g_hci_trace[i].data = NULL;
        }
    }
    g_hci_trace_index = 0;
}
#else  /* !BLE_HCI_PSRAM_TRACES */
/*
 * tkl_wifi.c calls hci_trace_dump() from its PRINT_DEBUG_LOG recovery path.
 * That switch and BLE_HCI_PSRAM_TRACES above are independent, so keep the
 * symbol defined either way rather than making one imply the other.
 */
void hci_trace_dump(void)
{
}
#endif /* BLE_HCI_PSRAM_TRACES */

void _tkl_ble_on_rcp_resp_rcvd(uint16_t status, rsi_ble_event_rcp_rcvd_info_t *resp_buf)
{
    TKL_UNUSED(status);

    if (resp_buf == NULL) {
        return;
    }

    /*
     * The HCI packet-type byte is 12 bytes
     * before rsi_ble_event_rcp_rcvd_info_t::data (see BLE_HCI_RCP_RX_PKT_TYPE_OFFSET).
     * The public struct has only data[1024] and no length field; the NWP buffer still
     * contains the 12-byte prefix in the same allocation (same as WiseConnect SDK).
     *
     * Reference (identical resp_buf->data - 12 usage):
     *   sdks/wiseconnect/examples/snippets/ble/bt_stack_bypass/app.c
     *   rsi_ble_on_rcp_resp_rcvd(): memcpy(..., (resp_buf->data - 12), 1);
     */
    const uint8_t *rx_data = (resp_buf->data - BLE_HCI_RCP_RX_PKT_TYPE_OFFSET); // NOSONAR
    uint16_t       cmd_len;
    uint16_t       opcode_r;

#if BLE_HCI_FRAME_TRACES
    static uint8_t rtemp[256];

    rtemp[0] = rx_data[0]; // NOSONAR
#endif /* BLE_HCI_FRAME_TRACES */
    switch (rx_data[0]) { // NOSONAR
    case BLE_HCI_EVT_MSG_TYPE:
        cmd_len = (uint16_t)resp_buf->data[1];
        cmd_len += 2;
        /* Don't know WHY TA core sent duplicate data, Workaround is drop it */
        opcode_r = resp_buf->data[3] << 8 | resp_buf->data[4];
        if (opcode_r != opcode && opcode_r == 0x0a20) {
            /* Drop */
            TKL_LOGD("Drop duplicate data %02X", opcode_r);
        } else if (tuya_ble_hci_rx_cmd_hs_cb) {
            opcode = 0;
            tuya_ble_hci_rx_cmd_hs_cb(resp_buf->data, cmd_len);
        }
        break;

    case BLE_HCI_ACL_MSG_TYPE:
        cmd_len = *(uint16_t *)&resp_buf->data[2];
        cmd_len += 4;
        if (tuya_ble_hci_rx_acl_hs_cb) {
            tuya_ble_hci_rx_acl_hs_cb(resp_buf->data, cmd_len);
        }
        break;

    case BLE_HCI_SYNC_MSG_TYPE:
        /* Sync packets are not forwarded on the Si91x HCI bypass path. */
        TKL_LOGD("Unhandle cmd %x", rx_data[0]);
        return;

    default:
        TKL_LOGD("Unhandle cmd %x", rx_data[0]);
        return;
    }
#if BLE_HCI_FRAME_TRACES
    memcpy(&rtemp[1], resp_buf->data, cmd_len);
    log_printhex("HCI <-", rtemp, cmd_len + 1);
#endif /* BLE_HCI_FRAME_TRACES */

#if BLE_HCI_PSRAM_TRACES
    {
        static uint8_t rx_buf[256];
        rx_buf[0] = rx_data[0];
        memcpy(&rx_buf[1], resp_buf->data, cmd_len);
        hci_trace_log(1, rx_buf, cmd_len + 1); // RX
    }
#endif
}

/**
 * Sends an HCI command from the host to the controller.
 *
 * @param cmd                   The HCI command to send.  This buffer must be
 *                                  allocated via tuya_ble_hci_buf_alloc().
 *
 * @retval 0                    success
 * @retval Other                fail
 *                              A BLE_ERR_[...] error code on failure.
 */
OPERATE_RET tkl_hci_cmd_packet_send(const uint8_t *p_buf, uint16_t buf_len)
{
    uint8_t    *cmd_buf;
    uint16_t    cmd_buf_len;
    sl_status_t status;

    if (NULL == p_buf || 0 == buf_len) {
        TKL_LOGE("%s: input invalid params", __func__);
        return OPRT_INVALID_PARM;
    }

    cmd_buf_len = buf_len + BLE_HCI_CMD_MSG_TYPE_LEN;
    cmd_buf     = tkl_system_malloc(cmd_buf_len);
    if (NULL == cmd_buf) {
        return OPRT_MALLOC_FAILED;
    }

    *cmd_buf = BLE_HCI_CMD_MSG_TYPE;
    memcpy(cmd_buf + BLE_HCI_CMD_MSG_TYPE_LEN, p_buf, buf_len);

#if BLE_HCI_FRAME_TRACES
    log_printhex("HCI ->", cmd_buf, cmd_buf_len);
#endif /* BLE_HCI_FRAME_TRACES */

#if BLE_HCI_PSRAM_TRACES
    hci_trace_log(0, cmd_buf, cmd_buf_len); // TX CMD
#endif

    opcode = cmd_buf[1] << 8 | cmd_buf[2];
    status = rsi_bt_driver_send_cmd(RSI_BLE_REQ_HCI_RAW, cmd_buf, NULL);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("rsi_bt_driver_send_cmd error %lx", status);
    }

    tkl_system_free(cmd_buf);
    cmd_buf = NULL;

    return status == SL_STATUS_OK ? OPRT_OK : OPRT_COM_ERROR;
}

/**
 * Sends ACL data from host to controller.
 *
 * @param om                    The ACL data packet to send.
 *
 * @retval 0                    success
 * @retval Other                fail
 *                              A BLE_ERR_[...] error code on failure.
 */
OPERATE_RET tkl_hci_acl_packet_send(const uint8_t *p_buf, uint16_t buf_len)
{
    uint8_t    *acl_buf;
    uint16_t    acl_buf_len;
    sl_status_t status;

    if (NULL == p_buf || 0 == buf_len) {
        TKL_LOGE("%s: input invalid params", __func__);
        return OPRT_INVALID_PARM;
    }

    acl_buf_len = buf_len + BLE_HCI_ACL_MSG_TYPE_LEN;
    acl_buf     = tkl_system_malloc(acl_buf_len);
    if (NULL == acl_buf) {
        return OPRT_MALLOC_FAILED;
    }

    *acl_buf = BLE_HCI_ACL_MSG_TYPE;
    memcpy(acl_buf + BLE_HCI_ACL_MSG_TYPE_LEN, p_buf, buf_len);

#if BLE_HCI_FRAME_TRACES
    log_printhex("HCI ->", acl_buf, acl_buf_len);
#endif /* BLE_HCI_FRAME_TRACES */

#if BLE_HCI_PSRAM_TRACES
    hci_trace_log(0, acl_buf, acl_buf_len); // TX ACL
#endif

    status = rsi_bt_driver_send_cmd(RSI_BLE_REQ_HCI_RAW, acl_buf, NULL);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("rsi_bt_driver_send_cmd error %lx", status);
    }

    tkl_system_free(acl_buf);
    acl_buf = NULL;

    return status == SL_STATUS_OK ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tkl_hci_callback_register(const TKL_HCI_FUNC_CB hci_evt_cb, const TKL_HCI_FUNC_CB acl_pkt_cb)
{
    tuya_ble_hci_rx_cmd_hs_cb = hci_evt_cb;
    tuya_ble_hci_rx_acl_hs_cb = acl_pkt_cb;

    return OPRT_OK;
}

/**
 * Resets the HCI module to a clean state.  Frees all buffers and reinitializes
 * the underlying transport.
 *
 * @retval 0                    success
 * @retval Other                fail
 *                              A BLE_ERR_[...] error code on failure.
 */
OPERATE_RET tkl_hci_reset(void)
{
    TKL_LOGD(">> tkl_hci_reset");

#if BLE_HCI_PSRAM_TRACES
    hci_trace_dump();
    hci_trace_free();
#endif

    tkl_system_delay(100);
    tkl_system_reset();

    return OPRT_OK;
}

/**
 * Init the HCI module
 *
 * @retval 0                    success
 * @retval Other                fail
 *                              A BLE_ERR_[...] error code on failure.
 */
OPERATE_RET tkl_hci_init(void)
{
    rsi_ble_enhanced_gap_extended_register_callbacks(RSI_BLE_ON_RCP_EVENT,
                                                     (void *)&_tkl_ble_on_rcp_resp_rcvd);
    return OPRT_OK;
}

/* Deinit the controller and hci interface */
/* Host Use */
OPERATE_RET tkl_hci_deinit(void)
{
    TKL_LOGD(">> tkl_hci_deinit");

    return OPRT_OK;
}
