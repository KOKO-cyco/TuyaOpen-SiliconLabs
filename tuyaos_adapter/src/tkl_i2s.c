/*****************************************************************************//**
 * @file tkl_i2s.c
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

#include "tkl_i2s.h"

#include "rsi_i2s.h"
#include "rsi_rom_udma_wrapper.h"
#include "rsi_udma_wrapper.h"

#include "sl_si91x_dma.h"
#include "sl_si91x_i2s.h"

#include "tkl_log.h"
#include "tkl_system.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define N_FRAMES_PER_CALLBACK (1024 / 2)
/* ICS43434 stereo RX: pick right channel (index 1), step 2 → mono @ half DMA length */
#define MIC_I2S_STEREO_STEP      2
#define MIC_I2S_RX_CHANNEL_INDEX 1

typedef void (*sl_i2s_dma_event_handler_t)(uint32_t event, uint32_t ch);

typedef struct {
    int16_t *base;
    int16_t *end;
    int16_t *w_ptr;
    int16_t *r_ptr;
} sl_i2s_sample_buffer_t;

typedef struct {
    bool                            is_init;
    volatile bool                   is_streaming;
    volatile bool                   tx_flag;
    volatile bool                   rx_flag;
    volatile bool                   tx_configured;
    TUYA_I2S_BASE_CFG_T             config;
    sl_i2s_handle_t                 handle;
    sl_i2s_signal_event_t           event_handler;
    sl_i2s_dma_event_handler_t      dma_event_handler;
    sl_i2s_sample_buffer_t          rx_buffer;
    tkl_i2s_buffer_ready_callback_t callback;
    void                           *args;
    uint32_t                        n_frames;
    int16_t                         dma_buffer[N_FRAMES_PER_CALLBACK * 2];
} TKL_I2S_HANDLE_T;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------

static sl_status_t _tkl_i2s_mode_receive(TUYA_I2S_NUM_E i2s_num);
static sl_status_t _tkl_i2s_mode_transmit(TUYA_I2S_NUM_E i2s_num);
static sl_status_t _tkl_i2s_receive_data(TUYA_I2S_NUM_E i2s_num);
static uint32_t    _tkl_i2s_copy_stereo_right_to_mono(int16_t *dst, const int16_t *src, uint32_t mono_frames);

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static TKL_I2S_HANDLE_T  sg_i2s_hdl[TUYA_I2S_NUM_MAX] = {0};
extern UDMA_RESOURCES    UDMA0_Resources;
extern UDMA_Channel_Info udma0_chnl_info[32];
extern RSI_UDMA_HANDLE_T udmaHandle0;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

uint8_t _tkl_i2s_get_n_channels(TUYA_I2S_CHANNEL_FMT_E fmt)
{
    switch (fmt) {
    case TUYA_I2S_CHANNEL_FMT_RIGHT_LEFT:
    case TUYA_I2S_CHANNEL_FMT_ALL_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ALL_LEFT:
        return 2;

    case TUYA_I2S_CHANNEL_FMT_ONLY_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ONLY_LEFT:
        return 1;

    default:
        return 0;
    }
}

static uint32_t _tkl_i2s_copy_stereo_right_to_mono(int16_t *dst, const int16_t *src, uint32_t mono_frames)
{
    uint32_t i = 0;

    for (i = 0; i < mono_frames; ++i) {
        dst[i] = src[i * MIC_I2S_STEREO_STEP + MIC_I2S_RX_CHANNEL_INDEX];
    }

    return mono_frames;
}

void _tkl_i2s_dma_event_handler(TUYA_I2S_NUM_E i2s_num, uint32_t event, uint32_t ch)
{
    const int16_t *src             = sg_i2s_hdl[i2s_num].dma_buffer;
    int16_t       *ptr             = sg_i2s_hdl[i2s_num].rx_buffer.w_ptr;
    int16_t       *dst             = sg_i2s_hdl[i2s_num].rx_buffer.w_ptr;
    const int      length_to_end   = (int)(sg_i2s_hdl[i2s_num].rx_buffer.end - sg_i2s_hdl[i2s_num].rx_buffer.w_ptr);
    const int      mono_chunk_size = MIN(N_FRAMES_PER_CALLBACK, length_to_end);
    const int      mono_remaining  = N_FRAMES_PER_CALLBACK - mono_chunk_size;

    _tkl_i2s_copy_stereo_right_to_mono(dst, src, (uint32_t)mono_chunk_size);
    dst += mono_chunk_size;

    if (sg_i2s_hdl[i2s_num].callback != NULL) {
        sg_i2s_hdl[i2s_num].callback(sg_i2s_hdl[i2s_num].args, ptr, (uint32_t)mono_chunk_size);
    }

    if (mono_remaining > 0) {
        dst = sg_i2s_hdl[i2s_num].rx_buffer.base;
        _tkl_i2s_copy_stereo_right_to_mono(dst, src + mono_chunk_size * MIC_I2S_STEREO_STEP, (uint32_t)mono_remaining);

        if (sg_i2s_hdl[i2s_num].callback != NULL) {
            sg_i2s_hdl[i2s_num].callback(sg_i2s_hdl[i2s_num].args, sg_i2s_hdl[i2s_num].rx_buffer.base,
                                         (uint32_t)mono_remaining);
        }
        dst += mono_remaining;
    }

    sg_i2s_hdl[i2s_num].rx_buffer.w_ptr = dst;

    _tkl_i2s_receive_data(i2s_num);
}

void _tkl_i2s0_dma_event_handler(uint32_t event, uint32_t ch)
{
    _tkl_i2s_dma_event_handler(TUYA_I2S_NUM_0, event, ch);
}

void _tkl_i2s_event_handler(TUYA_I2S_NUM_E i2s_num, uint32_t event)
{
    switch (event) {
    case SL_I2S_SEND_COMPLETE:
        /* Only clear flag in ISR. Do not call into OS APIs here. */
        sg_i2s_hdl[i2s_num].tx_flag = 0;
        break;

    case SL_I2S_RECEIVE_COMPLETE:
        sg_i2s_hdl[i2s_num].rx_flag = 0;
        break;

    case SL_I2S_TX_UNDERFLOW:
        break;

    case SL_I2S_RX_OVERFLOW:
        break;

    case SL_I2S_FRAME_ERROR:
        break;
    }
}

void _tkl_i2s0_event_handler(uint32_t event)
{
    _tkl_i2s_event_handler(TUYA_I2S_NUM_0, event);
}

sl_status_t _tkl_i2s_mode_receive(TUYA_I2S_NUM_E i2s_num)
{
    sl_i2s_xfer_config_t i2s_xfer_config;
    sl_status_t          status;

    i2s_xfer_config.mode          = SL_I2S_MASTER;
    i2s_xfer_config.protocol      = SL_I2S_PROTOCOL;
    i2s_xfer_config.resolution    = SL_I2S_RESOLUTION_16;

    /* Don't know why BUT it works. Need to check Wiseconnect SDK for the correct sampling rate */
    i2s_xfer_config.sampling_rate = sg_i2s_hdl[i2s_num].config.sample_rate * 2;

    i2s_xfer_config.sync          = SL_I2S_ASYNC;
    i2s_xfer_config.data_size     = SL_I2S_DATA_SIZE16;
    i2s_xfer_config.transfer_type = SL_MIC_ICS43434_RECEIVE;

    status = sl_si91x_i2s_config_transmit_receive(sg_i2s_hdl[i2s_num].handle, &i2s_xfer_config);
    I2S0->CHANNEL_CONFIG[0].I2S_IMR &= ~F_RXDAM;
    I2S0->CHANNEL_CONFIG[0].I2S_IMR |= F_RXFOM;

    /* RX reconfigures the peripheral; force TX path to re-init next send */
    sg_i2s_hdl[i2s_num].tx_configured = false;

    return status;
}

/**
 * @brief Configure I2S peripheral for TX once per TX session
 * @param[in] i2s_num i2s port number
 * @return SL_STATUS_OK on success
 */
static sl_status_t _tkl_i2s_mode_transmit(TUYA_I2S_NUM_E i2s_num)
{
    sl_i2s_xfer_config_t i2s_xfer_config = {0};
    sl_status_t          status;

    i2s_xfer_config.mode          = SL_I2S_MASTER;
    i2s_xfer_config.protocol      = SL_I2S_PROTOCOL;
    i2s_xfer_config.resolution    = SL_I2S_RESOLUTION_16;
    /* Keep historical value that matches current speaker clocking on this board */
    i2s_xfer_config.sampling_rate = 8000;
    i2s_xfer_config.transfer_type = SL_I2S_TRANSMIT;
    i2s_xfer_config.data_size     = SL_I2S_DATA_SIZE16;

    status = sl_si91x_i2s_config_transmit_receive(sg_i2s_hdl[i2s_num].handle, &i2s_xfer_config);
    if (status == SL_STATUS_OK) {
        sg_i2s_hdl[i2s_num].tx_configured = true;
    }

    return status;
}

sl_status_t _tkl_i2s_receive_data(TUYA_I2S_NUM_E i2s_num)
{
    RSI_UDMA_CHA_CONFIG_DATA_T dma_control = {.transferType       = UDMA_MODE_BASIC,
                                              .nextBurst          = 0,
                                              .rPower             = ARBSIZE_1,
                                              .totalNumOfDMATrans = (N_FRAMES_PER_CALLBACK * 2) - 1,
                                              .srcSize            = SRC_SIZE_16,
                                              .srcInc             = SRC_INC_NONE,
                                              .dstSize            = DST_SIZE_16,
                                              .dstInc             = DST_INC_16};
    RSI_UDMA_CHA_CFG_T         chnl_cfg    = {.altStruct       = 0,
                                              .burstReq        = 1,
                                              .channelPrioHigh = UDMA0_CHNL_PRIO_LVL,
                                              .dmaCh           = RTE_I2S0_CHNL_UDMA_RX_CH,
                                              .periAck         = 0,
                                              .periphReq       = 0,
                                              .reqMask         = 0};
    int                        stat;

    stat = UDMAx_ChannelConfigure(&UDMA0_Resources, RTE_I2S0_CHNL_UDMA_RX_CH, (uint32_t) & (I2S0->I2S_RXDMA),
                                  (uint32_t)sg_i2s_hdl[i2s_num].dma_buffer, N_FRAMES_PER_CALLBACK * 2, dma_control,
                                  &chnl_cfg, sg_i2s_hdl[i2s_num].dma_event_handler, udma0_chnl_info, udmaHandle0);
    if (stat == -1) {
        TKL_LOGE("Failed to config dma");
        return SL_STATUS_FAIL;
    }

    UDMAx_ChannelEnable(RTE_I2S0_CHNL_UDMA_RX_CH, &UDMA0_Resources, udmaHandle0);
    if (stat == -1) {
        TKL_LOGE("Failed to enable dma ch");
        return SL_STATUS_FAIL;
    }

    UDMAx_DMAEnable(&UDMA0_Resources, udmaHandle0);
    if (stat == -1) {
        TKL_LOGE("Failed to enable dma");
        return SL_STATUS_FAIL;
    }

    if (!I2S0->I2S_CER_b.CLKEN) {
        I2S0->CHANNEL_CONFIG[0].I2S_RFCR_b.RXCHDT = 1;
        I2S0->CHANNEL_CONFIG[0].I2S_RER_b.RXCHEN  = 0x1;
        I2S0->I2S_IRER_b.RXEN                     = 0x1;
        I2S0->I2S_CER_b.CLKEN                     = ENABLE;
    }

    sg_i2s_hdl[i2s_num].is_streaming = true;

    return SL_STATUS_OK;
}

OPERATE_RET tkl_i2s_init(TUYA_I2S_NUM_E i2s_num, const TUYA_I2S_BASE_CFG_T *i2s_config)
{
    OPERATE_RET rt = OPRT_OK;
    sl_status_t status;

    if (i2s_num >= TUYA_I2S_NUM_MAX) {
        TKL_LOGE("Invalid i2s_num: %d", i2s_num);
        return OPRT_INVALID_PARM;
    }

    if (i2s_config == NULL) {
        TKL_LOGE("Invalid i2s_config");
        return OPRT_INVALID_PARM;
    }

    if (i2s_config->mode & TUYA_I2S_MODE_SLAVE) {
        TKL_LOGE("I2S slave mode is not supported");
        return OPRT_COM_ERROR;
    }

    if (sg_i2s_hdl[i2s_num].is_init) {
        return OPRT_OK;
    }

    memset(&sg_i2s_hdl[i2s_num], 0, sizeof(TKL_I2S_HANDLE_T));
    memcpy(&sg_i2s_hdl[i2s_num].config, i2s_config, sizeof(TUYA_I2S_BASE_CFG_T));

    status = sl_si91x_i2s_init(i2s_num, &sg_i2s_hdl[i2s_num].handle);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("I2S init failll %ld", status);
        return OPRT_COM_ERROR;
    }

    status = sl_si91x_i2s_configure_power_mode(sg_i2s_hdl[i2s_num].handle, SL_I2S_FULL_POWER);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("I2S power mode fail %ld", status);
        return OPRT_COM_ERROR;
    }

    sg_i2s_hdl[i2s_num].event_handler     = _tkl_i2s0_event_handler;
    sg_i2s_hdl[i2s_num].dma_event_handler = _tkl_i2s0_dma_event_handler;

    if (i2s_config->mode & TUYA_I2S_MODE_TX) {
    }
    if (i2s_config->mode & TUYA_I2S_MODE_RX) {
    }

    status = sl_si91x_i2s_register_event_callback(sg_i2s_hdl[i2s_num].handle, sg_i2s_hdl[i2s_num].event_handler);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("I2S user callback register fail %ld", status);
        return OPRT_COM_ERROR;
    }

    sg_i2s_hdl[i2s_num].is_init = 1;
    TKL_LOGI("I2S%d init success", i2s_num);

    NVIC_SetPriority(UDMA0_IRQn, 1);

    return rt;
}

OPERATE_RET tkl_i2s_send(TUYA_I2S_NUM_E i2s_num, void *buff, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;
    sl_status_t status;

    if (!sg_i2s_hdl[i2s_num].is_init) {
        TKL_LOGE("I2S not initialized");
        return OPRT_COM_ERROR;
    }

    if ((buff == NULL) || (len < 2)) {
        return OPRT_INVALID_PARM;
    }

    if (sg_i2s_hdl[i2s_num].tx_flag) {
        return OPRT_SEND_ERR;
    }

    /* Configure TX only once per session; avoid gap from reconfig on every chunk */
    if (!sg_i2s_hdl[i2s_num].tx_configured) {
        status = _tkl_i2s_mode_transmit(i2s_num);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("I2S transmit config fail %ld", status);
            return OPRT_COM_ERROR;
        }
    }

    sg_i2s_hdl[i2s_num].tx_flag = 1;
    status                      = sl_si91x_i2s_transmit_data(sg_i2s_hdl[i2s_num].handle, buff, len / 2);
    if (status != SL_STATUS_OK) {
        /* Retry once with fresh TX config */
        sg_i2s_hdl[i2s_num].tx_flag       = 0;
        sg_i2s_hdl[i2s_num].tx_configured = false;
        status = _tkl_i2s_mode_transmit(i2s_num);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("I2S transmit reconfig fail %ld", status);
            return OPRT_COM_ERROR;
        }
        sg_i2s_hdl[i2s_num].tx_flag = 1;
        status                      = sl_si91x_i2s_transmit_data(sg_i2s_hdl[i2s_num].handle, buff, len / 2);
        if (status != SL_STATUS_OK) {
            sg_i2s_hdl[i2s_num].tx_flag       = 0;
            sg_i2s_hdl[i2s_num].tx_configured = false;
            TKL_LOGE("I2S transmit start fail %ld", status);
            return OPRT_COM_ERROR;
        }
    }

    return rt;
}

int tkl_i2s_recv(TUYA_I2S_NUM_E i2s_num, void *buff, uint32_t len)
{
    if (!sg_i2s_hdl[i2s_num].is_init) {
        TKL_LOGE("I2S not initialized");
        return 0;
    }

    int bytes_read = 0;

    return bytes_read;
}

OPERATE_RET tkl_i2s_set_streaming_config(TUYA_I2S_NUM_E i2s_num, void *buff, uint32_t n_frames)
{
    uint8_t  n_channels;
    uint32_t sample_length;

    if (!sg_i2s_hdl[i2s_num].is_init) {
        TKL_LOGE("I2S not initialized");
        return OPRT_COM_ERROR;
    }

    n_channels                          = _tkl_i2s_get_n_channels(sg_i2s_hdl[i2s_num].config.channel_format);
    sample_length                       = n_frames * n_channels;
    sg_i2s_hdl[i2s_num].n_frames        = n_frames;
    sg_i2s_hdl[i2s_num].rx_buffer.base  = (int16_t *)buff;
    sg_i2s_hdl[i2s_num].rx_buffer.w_ptr = sg_i2s_hdl[i2s_num].rx_buffer.base;
    sg_i2s_hdl[i2s_num].rx_buffer.end   = sg_i2s_hdl[i2s_num].rx_buffer.w_ptr + sample_length * 2;

    return OPRT_OK;
}

OPERATE_RET tkl_i2s_get_streaming_config(TUYA_I2S_NUM_E i2s_num, void **buff, uint32_t *n_frames)
{
    *buff     = sg_i2s_hdl[i2s_num].rx_buffer.base;
    *n_frames = sg_i2s_hdl[i2s_num].n_frames;

    return OPRT_OK;
}

OPERATE_RET tkl_i2s_recv_streaming(TUYA_I2S_NUM_E i2s_num, tkl_i2s_buffer_ready_callback_t callback, void *args)
{
    OPERATE_RET rt = OPRT_OK;
    sl_status_t status;

    if (!sg_i2s_hdl[i2s_num].is_init) {
        TKL_LOGE("I2S not initialized");
        return OPRT_COM_ERROR;
    }

    sg_i2s_hdl[i2s_num].args     = args;
    sg_i2s_hdl[i2s_num].callback = callback;

    status = _tkl_i2s_mode_receive(i2s_num);
    if (status != SL_STATUS_OK) {
        return status;
    }

    status = _tkl_i2s_receive_data(i2s_num);

    return rt;
}

bool tkl_i2s_send_inprogress(TUYA_I2S_NUM_E i2s_num)
{
    return sg_i2s_hdl[i2s_num].tx_flag;
}

OPERATE_RET tkl_i2s_send_stop(TUYA_I2S_NUM_E i2s_num)
{
    OPERATE_RET rt = OPRT_OK;
    sl_status_t status;

    status = sl_si91x_i2s_end_transfer(sg_i2s_hdl[i2s_num].handle, SL_I2S_SEND_ABORT);
    TKL_LOGD("i2s tx stop %ld", status);
    sg_i2s_hdl[i2s_num].tx_flag       = 0;
    sg_i2s_hdl[i2s_num].tx_configured = false;

    return rt;
}

OPERATE_RET tkl_i2s_recv_stop(TUYA_I2S_NUM_E i2s_num)
{
    OPERATE_RET rt = OPRT_OK;
    sl_status_t status;

    status = sl_si91x_i2s_end_transfer(sg_i2s_hdl[i2s_num].handle, SL_I2S_RECEIVE_ABORT);
    TKL_LOGD("i2s rx stop %ld", status);
    sg_i2s_hdl[i2s_num].rx_flag = 0;

    return rt;
}

OPERATE_RET tkl_i2s_deinit(TUYA_I2S_NUM_E i2s_num)
{
    OPERATE_RET rt = OPRT_OK;

    sg_i2s_hdl[i2s_num].is_streaming = false;
    sg_i2s_hdl[i2s_num].is_init      = false;

    if (sg_i2s_hdl[i2s_num].tx_flag) {
        tkl_i2s_send_stop(i2s_num);
    }

    if (sg_i2s_hdl[i2s_num].rx_flag) {
        tkl_i2s_recv_stop(i2s_num);
    }

    sl_si91x_i2s_deinit(&sg_i2s_hdl[i2s_num].handle);

    return rt;
}
