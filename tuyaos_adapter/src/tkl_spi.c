/*****************************************************************************//**
 * @file tkl_spi.c
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sl_status.h"
#include "sl_si91x_ssi.h"
#include "sl_si91x_gspi.h"
#include "rsi_rom_egpio.h"
#include "sl_si91x_gpio_common.h"
#include "RTE_Device_917.h"
#include "tkl_spi.h"
#include "tkl_log.h"
#include "FreeRTOSConfig.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#if defined(SL_GSPI_DMA_CONFIG_ENABLE) && (SL_GSPI_DMA_CONFIG_ENABLE == 1)
#define TKL_GSPI_DMA_ON 1
#else
#define TKL_GSPI_DMA_ON 0
#endif

#define MAX_PORT_SUPPORTED              4
#define SSI_MASTER_RECEIVE_SAMPLE_DELAY 0

/********************************************************************
 * SPI mapping table
 * More detail see below
 ********************************************************************
 *   TUYA_SPI       |    Si91x SPI
 ********************************************************************/
#define SI91X_SPI_MAPPING                                                                                              \
    X_SPI(TUYA_SPI_NUM_0, SSI_ULP_MASTER)                                                                              \
    X_SPI(TUYA_SPI_NUM_1, SSI_MASTER)                                                                                  \
    X_SPI(TUYA_SPI_NUM_2, SSI_SLAVE)                                                                                   \
    X_SPI(TUYA_SPI_NUM_3, GSPI_MASTER)

typedef enum { SPI_TYPE_NONE, SPI_TYPE_SSI, SPI_TYPE_GSPI } spi_type_t;

typedef struct {
    spi_type_t type;
    uint8_t    initialized;
    union {
        sl_ssi_handle_t  ssi_handle;
        sl_gspi_handle_t gspi_handle;
    };

    TUYA_SPI_IRQ_CB cb;
} spi_dev_t;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

static spi_dev_t list_devs[MAX_PORT_SUPPORTED] = {0};

#define DECLARE_SPIx_EVENT_CALLBACK(x)                                                                                 \
    void sl_spi##x##_event_callback(uint32_t event)                                                                    \
    {                                                                                                                  \
        if (list_devs[x].cb == NULL) {                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
                                                                                                                       \
        switch (event) {                                                                                               \
        case ARM_SPI_EVENT_TRANSFER_COMPLETE:                                                                          \
            list_devs[x].cb((TUYA_SPI_NUM_E)x, TUYA_SPI_EVENT_TX_COMPLETE);                                            \
            break;                                                                                                     \
        case ARM_SPI_EVENT_DATA_LOST:                                                                                  \
            list_devs[x].cb((TUYA_SPI_NUM_E)x, TUYA_SPI_EVENT_DATA_LOST);                                              \
            break;                                                                                                     \
        case ARM_SPI_EVENT_MODE_FAULT:                                                                                 \
            list_devs[x].cb((TUYA_SPI_NUM_E)x, TUYA_SPI_EVENT_MODE_FAULT);                                             \
            break;                                                                                                     \
        default:                                                                                                       \
            break;                                                                                                     \
        }                                                                                                              \
    }

DECLARE_SPIx_EVENT_CALLBACK(0);
DECLARE_SPIx_EVENT_CALLBACK(1);
DECLARE_SPIx_EVENT_CALLBACK(2);
DECLARE_SPIx_EVENT_CALLBACK(3);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

ARM_SPI_SignalEvent_t list_spi_event_callback[] = {
    sl_spi0_event_callback,
    sl_spi1_event_callback,
    sl_spi2_event_callback,
    sl_spi3_event_callback,
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

sl_status_t sl_spi_send(spi_dev_t *dev, const void *data, uint32_t length)
{
    switch (dev->type) {
    case SPI_TYPE_SSI:
        return sl_si91x_ssi_send_data(dev->ssi_handle, data, length);

    case SPI_TYPE_GSPI:
        return sl_si91x_gspi_send_data(dev->gspi_handle, data, length);

    default:
        return SL_STATUS_NOT_SUPPORTED;
    }
}

sl_status_t sl_spi_receive(spi_dev_t *dev, void *data, uint32_t length)
{
    switch (dev->type) {
    case SPI_TYPE_SSI:
        return sl_si91x_ssi_receive_data(dev->ssi_handle, data, length);

    case SPI_TYPE_GSPI:
        return sl_si91x_gspi_receive_data(dev->gspi_handle, data, length);

    default:
        return SL_STATUS_NOT_SUPPORTED;
    }
}

sl_status_t sl_spi_transfer(spi_dev_t *dev, const void *send_buf, void *receive_buf, uint32_t length)
{
    switch (dev->type) {
    case SPI_TYPE_SSI:
        return sl_si91x_ssi_transfer_data(dev->ssi_handle, send_buf, receive_buf, length);

    case SPI_TYPE_GSPI:
        return sl_si91x_gspi_transfer_data(dev->gspi_handle, send_buf, receive_buf, length);

    default:
        return SL_STATUS_NOT_SUPPORTED;
    }
}

sl_status_t sl_spi_get_status(spi_dev_t *dev, TUYA_SPI_STATUS_T *status)
{
    switch (dev->type) {
    case SPI_TYPE_SSI: {
        sl_ssi_status_t ssi_status = sl_si91x_ssi_get_status(dev->ssi_handle);
        status->busy               = (uint32_t)ssi_status.busy;
        status->data_lost          = (uint32_t)ssi_status.data_lost;
        status->mode_fault         = (uint32_t)ssi_status.mode_fault;
        return SL_STATUS_OK;
    }

    case SPI_TYPE_GSPI: {
        sl_gspi_status_t gspi_status = sl_si91x_gspi_get_status(dev->gspi_handle);
        status->busy                 = (uint32_t)gspi_status.busy;
        status->data_lost            = (uint32_t)gspi_status.data_lost;
        status->mode_fault           = (uint32_t)gspi_status.mode_fault;
        return SL_STATUS_OK;
    }

    default:
        return SL_STATUS_NOT_SUPPORTED;
    }
}

/*==============================================================================
 *                             PUBLIC API FUNCTIONS
 *============================================================================*/

/**
 * @brief Initialize SPI interface
 *
 * @param port SPI port number
 * @param cfg  Pointer to SPI configuration structure
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_init(TUYA_SPI_NUM_E port, const TUYA_SPI_BASE_CFG_T *cfg)
{
    sl_status_t status;
    spi_dev_t  *dev = &list_devs[port];

    if (port >= MAX_PORT_SUPPORTED || cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (dev->initialized) {
        return OPRT_INIT_MORE_THAN_ONCE;
    }

    memset(dev, 0, sizeof(spi_dev_t));

    // Determine SPI type based on port mapping
    if (port == TUYA_SPI_NUM_3) {
        sl_gspi_control_config_t gspi_config;
        sl_gspi_status_t         gspi_status;

        // Port 3 uses GSPI
        dev->type = SPI_TYPE_GSPI;
        if (cfg->role != TUYA_SPI_ROLE_MASTER) {
            return OPRT_NOT_SUPPORTED;
        }

        // Map SPI modes to GSPI modes
        switch (cfg->mode) {
        case TUYA_SPI_MODE0:
            gspi_config.clock_mode = SL_GSPI_MODE_0;
            break;

        case TUYA_SPI_MODE3:
            gspi_config.clock_mode = SL_GSPI_MODE_3;
            break;

        default:
            return OPRT_NOT_SUPPORTED;
        }

        gspi_config.bit_width = (cfg->databits == TUYA_SPI_DATA_BIT16) ? 16 : 8;
        gspi_config.bitrate   = cfg->freq_hz;
        gspi_config.slave_select_mode = SL_GSPI_MASTER_HW_OUTPUT;
        gspi_config.swap_read         = false;
        gspi_config.swap_write        = false;

        status = sl_si91x_gspi_init(SL_GSPI_MASTER, &dev->gspi_handle);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("sl_si91x_gspi_init error %lx", status);
            return OPRT_RESOURCE_NOT_READY;
        }

        {
            static const struct {
                uint8_t pin;
                uint8_t mux;
                uint8_t pad;
            } gspi_pins[] = {
                {RTE_GSPI_MASTER_CLK_PIN, RTE_GSPI_MASTER_CLK_MUX, RTE_GSPI_MASTER_CLK_PAD},
                {RTE_GSPI_MASTER_MOSI_PIN, RTE_GSPI_MASTER_MOSI_MUX, RTE_GSPI_MASTER_MOSI_PAD},
                {RTE_GSPI_MASTER_CS0_PIN, RTE_GSPI_MASTER_CS0_MUX, RTE_GSPI_MASTER_CS0_PAD},
            };
            for (unsigned i = 0; i < sizeof(gspi_pins) / sizeof(gspi_pins[0]); i++) {
                uint8_t pin = gspi_pins[i].pin;
                if (gspi_pins[i].pad != 0) {
                    RSI_EGPIO_PadSelectionEnable(gspi_pins[i].pad);
                }
                if (pin >= HOST_PAD_MIN && pin <= HOST_PAD_MAX) {
                    RSI_EGPIO_HostPadsGpioModeEnable(pin);
                }
                RSI_EGPIO_PadReceiverEnable(pin);
                RSI_EGPIO_SetPinMux(EGPIO, RTE_GSPI_MASTER_CLK_PORT, pin, gspi_pins[i].mux);
            }
            RSI_EGPIO_SetPinMux(EGPIO, RTE_GSPI_MASTER_MISO_PORT, RTE_GSPI_MASTER_MISO_PIN, EGPIO_PIN_MUX_MODE0);
        }

        gspi_status = sl_si91x_gspi_get_status(dev->gspi_handle);
        TKL_LOGD("GSPI busy: %d, data lost: %d, mode fault: %d", gspi_status.busy, gspi_status.data_lost,
                 gspi_status.mode_fault);

        status = sl_si91x_gspi_set_configuration(dev->gspi_handle, &gspi_config);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("sl_si91x_gspi_set_configuration error %lx", status);
            sl_si91x_gspi_deinit(dev->gspi_handle);
            return OPRT_RESOURCE_NOT_READY;
        }

        status = sl_si91x_gspi_register_event_callback(list_devs[port].gspi_handle, list_spi_event_callback[port]);
        if (status != SL_STATUS_OK) {
            TKL_LOGE("sl_si91x_gspi_register_event_callback error %lx", status);
        }

        TKL_LOGI("GSPI clk div %lu, frame length %lu, DMA %s",
                 sl_si91x_gspi_get_clock_division_factor(dev->gspi_handle), sl_si91x_gspi_get_frame_length(),
                 TKL_GSPI_DMA_ON ? "on" : "OFF (one IRQ per word)");

        sl_si91x_gspi_set_slave_number(GSPI_SLAVE_0);
    } else {
        sl_ssi_control_config_t ssi_config;

        // Ports 0, 1, 2 use SSI
        dev->type = SPI_TYPE_SSI;

        // Determine device mode based on role and port
        switch (cfg->role) {
        case TUYA_SPI_ROLE_MASTER:
            if (port == TUYA_SPI_NUM_0) {
                ssi_config.device_mode = SL_SSI_ULP_MASTER_ACTIVE;
            } else if (port == TUYA_SPI_NUM_1) {
                ssi_config.device_mode = SL_SSI_MASTER_ACTIVE;
            } else {
                return OPRT_NOT_SUPPORTED;
            }
            break;

        case TUYA_SPI_ROLE_SLAVE:
            if (port == TUYA_SPI_NUM_2) {
                ssi_config.device_mode = SL_SSI_SLAVE_ACTIVE;
            } else {
                return OPRT_NOT_SUPPORTED;
            }
            break;

        default:
            return OPRT_NOT_SUPPORTED;
        }

        // Map SPI modes to SSI modes
        switch (cfg->mode) {
        case TUYA_SPI_MODE0:
            ssi_config.clock_mode = SL_SSI_PERIPHERAL_CPOL0_CPHA0;
            break;
        case TUYA_SPI_MODE1:
            ssi_config.clock_mode = SL_SSI_PERIPHERAL_CPOL0_CPHA1;
            break;
        case TUYA_SPI_MODE2:
            ssi_config.clock_mode = SL_SSI_PERIPHERAL_CPOL1_CPHA0;
            break;
        case TUYA_SPI_MODE3:
            ssi_config.clock_mode = SL_SSI_PERIPHERAL_CPOL1_CPHA1;
            break;
        }

        ssi_config.bit_width            = (cfg->databits == TUYA_SPI_DATA_BIT16) ? 16 : 8;
        ssi_config.baud_rate            = cfg->freq_hz;
        ssi_config.receive_sample_delay = SSI_MASTER_RECEIVE_SAMPLE_DELAY;
        ssi_config.transfer_mode        = 0;

        status = sl_si91x_ssi_init(ssi_config.device_mode, &dev->ssi_handle);
        if (status != SL_STATUS_OK) {
            return OPRT_RESOURCE_NOT_READY;
        }

        status = sl_si91x_ssi_set_configuration(dev->ssi_handle, &ssi_config, 0);
        if (status != SL_STATUS_OK) {
            sl_si91x_ssi_deinit(dev->ssi_handle);
            return OPRT_RESOURCE_NOT_READY;
        }

        sl_si91x_ssi_set_slave_number(SSI_SLAVE_0);
        sl_si91x_ssi_register_event_callback(list_devs[port].ssi_handle, list_spi_event_callback[port]);
    }

    dev->initialized = 1;

    return OPRT_OK;
}

/**
 * @brief Deinitialize SPI interface
 *
 * @param port SPI port number
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_deinit(TUYA_SPI_NUM_E port)
{
    spi_dev_t *dev = &list_devs[port];

    if (port >= MAX_PORT_SUPPORTED) {
        return OPRT_INVALID_PARM;
    }

    if (!dev->initialized) {
        return OPRT_OS_ADAPTER_SPI_DEINIT_FAILED;
    }

    if (dev->type == SPI_TYPE_GSPI) {
        sl_si91x_gspi_deinit(dev->gspi_handle);
        dev->gspi_handle = NULL;

    } else if (dev->type == SPI_TYPE_SSI) {
        sl_si91x_ssi_deinit(dev->ssi_handle);
        dev->ssi_handle = NULL;
    }

    memset(dev, 0, sizeof(spi_dev_t));

    return OPRT_OK;
}

/**
 * @brief Send data via SPI
 *
 * @param port SPI port number
 * @param data Pointer to data to send
 * @param size Number of bytes to send
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_send(TUYA_SPI_NUM_E port, void *data, uint32_t size)
{
    sl_status_t status;

    if (port >= MAX_PORT_SUPPORTED || data == NULL || size == 0) {
        return OPRT_INVALID_PARM;
    }

    status = sl_spi_send(&list_devs[port], data, size);
    if (status != SL_STATUS_OK) {
        TKL_LOGE("sl_spi_send error %lx", status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Receive data via SPI
 *
 * @param port SPI port number
 * @param data Pointer to buffer for received data
 * @param size Number of bytes to receive
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_recv(TUYA_SPI_NUM_E port, void *data, uint32_t size)
{
    sl_status_t status;

    if (port >= MAX_PORT_SUPPORTED || data == NULL || size == 0) {
        return OPRT_INVALID_PARM;
    }

    status = sl_spi_receive(&list_devs[port], data, size);
    if (status != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Full duplex SPI transfer
 *
 * @param port SPI port number
 * @param send_buf Pointer to data to send
 * @param receive_buf Pointer to buffer for received data
 * @param length Number of bytes to transfer
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_transfer(TUYA_SPI_NUM_E port, void *send_buf, void *receive_buf, uint32_t length)
{
    sl_status_t status;

    if (port >= MAX_PORT_SUPPORTED || send_buf == NULL || receive_buf == NULL || length == 0) {
        return OPRT_INVALID_PARM;
    }

    status = sl_spi_transfer(&list_devs[port], send_buf, receive_buf, length);
    if (status != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief SPI transfer with different send and receive lengths
 *
 * @param port SPI port number
 * @param send_buf Pointer to data to send
 * @param send_len Number of bytes to send
 * @param receive_buf Pointer to buffer for received data
 * @param receive_len Number of bytes to receive
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_transfer_with_length(TUYA_SPI_NUM_E port, void *send_buf, uint32_t send_len, void *receive_buf,
                                         uint32_t receive_len)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Abort SPI transfer
 *
 * @param port SPI port number
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_abort_transfer(TUYA_SPI_NUM_E port)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Get SPI status
 *
 * @param port SPI port number
 * @param status Pointer to store SPI status
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_get_status(TUYA_SPI_NUM_E port, TUYA_SPI_STATUS_T *status)
{
    sl_status_t sl_status;

    if (port >= MAX_PORT_SUPPORTED || status == NULL) {
        return OPRT_INVALID_PARM;
    }

    sl_status = sl_spi_get_status(&list_devs[port], status);
    if (sl_status != SL_STATUS_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Initialize SPI interrupt
 *
 * @param port SPI port number
 * @param cb SPI interrupt callback function
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_irq_init(TUYA_SPI_NUM_E port, TUYA_SPI_IRQ_CB cb)
{
    if (port >= MAX_PORT_SUPPORTED || cb == NULL) {
        return OPRT_INVALID_PARM;
    }

    list_devs[port].cb = cb;

    return OPRT_OK;
}

/**
 * @brief Enable SPI interrupt
 *
 * @param port SPI port number
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_irq_enable(TUYA_SPI_NUM_E port)
{
    if (port >= MAX_PORT_SUPPORTED) {
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Disable SPI interrupt
 *
 * @param port SPI port number
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_irq_disable(TUYA_SPI_NUM_E port)
{
    if (port >= MAX_PORT_SUPPORTED) {
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Get SPI transfer data count
 *
 * @param port SPI port number
 * @return Number of transferred bytes, or negative on error
 */
int32_t tkl_spi_get_data_count(TUYA_SPI_NUM_E port)
{
    /* The SSI rx/tx data-count registers are not surfaced by this adapter. */
    TKL_UNUSED(port);
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief SPI I/O control
 *
 * @param port SPI port number
 * @param cmd Control command
 * @param args Command arguments
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tkl_spi_ioctl(TUYA_SPI_NUM_E port, uint32_t cmd, void *args)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Get maximum DMA data length
 *
 * @return Maximum supported DMA transfer length
 */
uint32_t tkl_spi_get_max_dma_data_length(void)
{
    return (1024);
}
