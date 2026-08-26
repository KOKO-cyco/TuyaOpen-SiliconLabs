/*****************************************************************************//**
 * @file tkl_i2c.c
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

#include "tuya_kconfig.h"
#include "RTE_Device_917.h"
#include "tkl_i2c.h"
#include "tkl_log.h"
#include "sl_si91x_i2c.h"
#include "sl_si91x_peripheral_i2c.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

/*
 * I2C mapping table
 *   TUYA_I2C | Si91x I2C
 */
#define SI91X_I2C_MAPPING                                                                                              \
    X_I2C(TUYA_I2C_NUM_0, SL_I2C0, I2C0_BASE, i2c0_io)                                                                 \
    X_I2C(TUYA_I2C_NUM_1, SL_I2C1, I2C1_BASE, i2c1_io)                                                                 \
    X_I2C(TUYA_I2C_NUM_2, SL_I2C2, I2C2_BASE, i2c2_io)

#define SI91X_I2C_IO_DEFINE(ins)                                                                                       \
    sl_i2c_pin_init_t i2c##ins##_io = {                                                                                \
        .sda_port = RTE_I2C##ins##_SCL_PORT,                                                                           \
        .sda_pin  = RTE_I2C##ins##_SDA_PIN,                                                                            \
        .sda_mux  = RTE_I2C##ins##_SDA_MUX,                                                                            \
        .sda_pad  = RTE_I2C##ins##_SDA_PAD,                                                                            \
        .scl_port = RTE_I2C##ins##_SCL_PORT,                                                                           \
        .scl_pin  = RTE_I2C##ins##_SCL_PIN,                                                                            \
        .scl_mux  = RTE_I2C##ins##_SCL_MUX,                                                                            \
        .scl_pad  = RTE_I2C##ins##_SCL_PAD,                                                                            \
        .instance = SL_I2C##ins,                                                                                       \
    }

#define DUMMY_FOLLOWER_ADDRESS 0x00 // In Follower mode, the I2C follower address is ignored, so kept the value as 0

typedef struct {
    TUYA_I2C_NUM_E     port;
    sl_i2c_instance_t  i2c_num;
    I2C0_Type         *i2c_base;
    sl_i2c_pin_init_t *pin_config;
} i2c_dev_t;

SI91X_I2C_IO_DEFINE(0);
SI91X_I2C_IO_DEFINE(1);
SI91X_I2C_IO_DEFINE(2);

const i2c_dev_t i2c_dev[] = {
#define X_I2C(port, i2c_num, i2c_base, i2c_io) {port, i2c_num, (I2C0_Type *)i2c_base, &i2c_io},
    SI91X_I2C_MAPPING
#undef X_I2C
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

const i2c_dev_t *get_i2c_dev(TUYA_I2C_NUM_E port)
{
    switch (port) {
    case TUYA_I2C_NUM_0:
#if (defined(ENABLE_I2C0)) && ENABLE_I2C0
        return &i2c_dev[0];
#else
        TKL_LOGE("run menuconfig to enable I2C0");
#endif
        break;

    case TUYA_I2C_NUM_1:
#if (defined(ENABLE_I2C1)) && ENABLE_I2C1
        return &i2c_dev[1];
#else
        TKL_LOGE("run menuconfig to enable I2C1");
#endif
        break;

    case TUYA_I2C_NUM_2:
#if (defined(ENABLE_I2C2)) && ENABLE_I2C2
        return &i2c_dev[2];
#else
        TKL_LOGE("run menuconfig to enable I2C2");
#endif
        break;

    default:
        break;
    }

    return NULL;
}

OPERATE_RET tkl_i2c_init(TUYA_I2C_NUM_E port, const TUYA_IIC_BASE_CFG_T *cfg)
{
    sl_i2c_config_t  i2c_config;
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);

    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (cfg->role == TUYA_IIC_MODE_MASTER) {
        i2c_config.mode = SL_I2C_LEADER_MODE;
    } else if (cfg->role == TUYA_IIC_MODE_SLAVE) {
        i2c_config.mode = SL_I2C_FOLLOWER_MODE;
    } else {
        return OPRT_INVALID_PARM;
    }

    switch (cfg->speed) {
    case TUYA_IIC_BUS_SPEED_100K:
        i2c_config.operating_mode = SL_I2C_STANDARD_MODE;
        break;
    case TUYA_IIC_BUS_SPEED_400K:
        i2c_config.operating_mode = SL_I2C_FAST_MODE;
        break;
    case TUYA_IIC_BUS_SPEED_1M:
        i2c_config.operating_mode = SL_I2C_FAST_PLUS_MODE;
        break;
    case TUYA_IIC_BUS_SPEED_3_4M:
        i2c_config.operating_mode = SL_I2C_HIGH_SPEED_MODE;
        break;
    default:
        return OPRT_INVALID_PARM;
    }

    if (cfg->addr_width != TUYA_IIC_ADDRESS_7BIT && cfg->addr_width != TUYA_IIC_ADDRESS_10BIT) {
        return OPRT_INVALID_PARM;
    }

    i2c_config.transfer_type = 0;
    i2c_config.i2c_callback  = NULL;

    // Initializing I2C pin
    i2c_status = sl_si91x_i2c_pin_init(i2c_device->pin_config);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_si91x_i2c_pin_init error %u", i2c_status);
        return OPRT_INVALID_PARM;
    }

    // Initializing I2C instance
    i2c_status = sl_i2c_driver_init(i2c_device->i2c_num, &i2c_config);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_init error %u", i2c_status);
        return OPRT_INVALID_PARM;
    }

    // Configuring RX and TX FIFO thresholds
    i2c_status = sl_i2c_driver_configure_fifo_threshold(i2c_device->i2c_num, 0, 0);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_configure_fifo_threshold error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_deinit(TUYA_I2C_NUM_E port)
{
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    sl_i2c_driver_deinit(i2c_device->i2c_num);

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_master_send(TUYA_I2C_NUM_E port, uint16_t dev_addr, const void *data, uint32_t size,
                                BOOL_T xfer_pending)
{
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    i2c_status = sl_i2c_driver_enable_repeated_start(i2c_device->i2c_num, (boolean_t)xfer_pending);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_enable_repeated_start error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    i2c_status = sl_i2c_driver_send_data_blocking(i2c_device->i2c_num, dev_addr, (uint8_t *)data, size);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_send_data_blocking error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_master_receive(TUYA_I2C_NUM_E port, uint16_t dev_addr, void *data, uint32_t size,
                                   BOOL_T xfer_pending)
{
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    i2c_status = sl_i2c_driver_enable_repeated_start(i2c_device->i2c_num, (boolean_t)xfer_pending);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_enable_repeated_start error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    i2c_status = sl_i2c_driver_receive_data_blocking(i2c_device->i2c_num, dev_addr, data, size);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_receive_data_blocking error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_set_slave_addr(TUYA_I2C_NUM_E port, uint16_t dev_addr)
{
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);

    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    // Configuring follower mask address
    i2c_status = sl_i2c_driver_set_follower_address(i2c_device->i2c_num, dev_addr);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_init error %u", i2c_status);
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_slave_send(TUYA_I2C_NUM_E port, const void *data, uint32_t size)
{
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    i2c_status = sl_i2c_driver_send_data_blocking(i2c_device->i2c_num, DUMMY_FOLLOWER_ADDRESS, (uint8_t *)data, size);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_send_data_blocking error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_slave_receive(TUYA_I2C_NUM_E port, void *data, uint32_t size)
{
    sl_i2c_status_t  i2c_status;
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    i2c_status = sl_i2c_driver_receive_data_blocking(i2c_device->i2c_num, DUMMY_FOLLOWER_ADDRESS, data, size);
    if (i2c_status != SL_I2C_SUCCESS) {
        TKL_LOGE("sl_i2c_driver_receive_data_blocking error %u ", i2c_status);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_reset(TUYA_I2C_NUM_E port)
{
    const i2c_dev_t *i2c_device = get_i2c_dev(port);
    if (i2c_device == NULL) {
        return OPRT_INVALID_PARM;
    }

    sl_si91x_i2c_reset(i2c_device->i2c_base);

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_get_status(TUYA_I2C_NUM_E port, TUYA_IIC_STATUS_T *status)
{
    TKL_UNUSED(port);
    TKL_UNUSED(status);
    return OPRT_NOT_SUPPORTED;
}

int32_t tkl_i2c_get_data_count(TUYA_I2C_NUM_E port)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_ioctl(TUYA_I2C_NUM_E port, uint32_t cmd, void *args)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_irq_init(TUYA_I2C_NUM_E port, TUYA_I2C_IRQ_CB cb)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_irq_enable(TUYA_I2C_NUM_E port)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_irq_disable(TUYA_I2C_NUM_E port)
{
    return OPRT_NOT_SUPPORTED;
}
