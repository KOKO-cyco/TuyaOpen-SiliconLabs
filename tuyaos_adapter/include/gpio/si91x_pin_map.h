/*****************************************************************************//**
 * @file si91x_pin_map.h
 * @brief TUYA GPIO number -> SiWx917 (port, pad) mapping.
 *
 * Two adapters need this: tkl_gpio.c drives the pads, tkl_pinmux.c has to
 * reach the same pad to change its mux. A second copy would be a copy that
 * drifts, so it lives here and both include it.
 *
 * The X macros are left defined after the table -- an including file
 * instantiates its own array from SI91X_PIN_MAPPING and undefines them itself.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __SI91X_PIN_MAP_H__
#define __SI91X_PIN_MAP_H__

#include <stdint.h>

#include "tuya_cloud_types.h"
/* HP / ULP / UULP_VBAT come from RTE_Device_917.h, which em_device.h pulls in. */
#include "em_device.h"

/********************************************************************
 * Pin mapping table
 *
 * TUYA GPIO number -> (SiWx917 port, pad index), and nothing about any
 * particular board.
 *
 *   TUYA GPIO  [0:4]                        -> UULP GPIO [0:4]
 *   TUYA GPIO  [6:12], 15, [25:34], [46:57] -> HP   GPIO, same number
 *   TUYA GPIO  [20:24], [35:41]             -> ULP  GPIO [0:4], [5:11]
 *
 * The values are the SoC's. An HP pad is (HP, n), a UULP pad is
 * (UULP_VBAT, n), and a ULP pad is (ULP, n) -- checked against
 * RTE_Device_917.h for two different boards, where each of these is defined
 * identically, because none of it is board-specific.
 *
 * This used to read SL_SI91X_<pad>_PIN/_PORT out of sl_gpio_board.h, which is
 * the *board's* file: a board declares only the pads it brings out. So the
 * table stopped compiling on any board that leaves one out -- BRD2605A omits
 * ULP_GPIO_3 and UULP_GPIO_4, and its own RTE_Device_917.h does not define
 * them either, so that is the board stating a fact rather than an oversight.
 * SIWX917_AI_DEV_KIT built only because it ships no sl_gpio_board.h of its own
 * and so inherited the chip-wide default that declares every pad.
 *
 * Which pads a board brings out belongs to the board layer, and is already
 * there: boards/SiWx917/<BOARD>/Kconfig names the pins that board uses. This
 * is how T5AI does it too -- one flat, unconditional pinmap in the chip layer
 * and no board conditionals in it at all.
 *
 * HP and UULP rows take one argument because for them the TUYA number and the
 * pad index are the same; writing it once keeps the two from drifting apart.
 ********************************************************************/

/* ULP pads are reached on the ULP port by their own index on radio-board base
 * versions, and on the HP port at 64 + index otherwise. That is the SDK's own
 * distinction -- RTE_ULP_GPIO_n_PORT_ID, keyed on
 * SLI_SI91X_MCU_CONFIG_RADIO_BOARD_BASE_VER -- and a property of the silicon
 * revision rather than of a board, so it belongs here: one condition for the
 * whole class, not one per pad. */
#ifdef SLI_SI91X_MCU_CONFIG_RADIO_BOARD_BASE_VER
#define ULP_PAD(n) .pin = (n), .port = ULP
#else
#define ULP_PAD(n) .pin = 64 + (n), .port = HP
#endif

#define X_UULP(n)      {.pin_id = TUYA_GPIO_NUM_##n, .pin = (n), .port = UULP_VBAT},
#define X_HP(n)        {.pin_id = TUYA_GPIO_NUM_##n, .pin = (n), .port = HP},
#define X_ULP(tuya, n) {.pin_id = TUYA_GPIO_NUM_##tuya, ULP_PAD(n)},

#define SI91X_PIN_MAPPING                                                                                              \
    X_UULP(0)                                                                                                          \
    X_UULP(1)                                                                                                          \
    X_UULP(2)                                                                                                          \
    X_UULP(3)                                                                                                          \
    X_UULP(4)                                                                                                          \
    X_HP(6)                                                                                                            \
    X_HP(7)                                                                                                            \
    X_HP(8)                                                                                                            \
    X_HP(9)                                                                                                            \
    X_HP(10)                                                                                                           \
    X_HP(11)                                                                                                           \
    X_HP(12)                                                                                                           \
    X_HP(15)                                                                                                           \
    X_HP(25)                                                                                                           \
    X_HP(26)                                                                                                           \
    X_HP(27)                                                                                                           \
    X_HP(28)                                                                                                           \
    X_HP(29)                                                                                                           \
    X_HP(30)                                                                                                           \
    X_HP(31)                                                                                                           \
    X_HP(32)                                                                                                           \
    X_HP(33)                                                                                                           \
    X_HP(34)                                                                                                           \
    X_HP(46)                                                                                                           \
    X_HP(47)                                                                                                           \
    X_HP(48)                                                                                                           \
    X_HP(49)                                                                                                           \
    X_HP(50)                                                                                                           \
    X_HP(51)                                                                                                           \
    X_HP(52)                                                                                                           \
    X_HP(53)                                                                                                           \
    X_HP(54)                                                                                                           \
    X_HP(55)                                                                                                           \
    X_HP(56)                                                                                                           \
    X_HP(57)                                                                                                           \
    X_ULP(20, 0)                                                                                                       \
    X_ULP(21, 1)                                                                                                       \
    X_ULP(22, 2)                                                                                                       \
    X_ULP(23, 3)                                                                                                       \
    X_ULP(24, 4)                                                                                                       \
    X_ULP(35, 5)                                                                                                       \
    X_ULP(36, 6)                                                                                                       \
    X_ULP(37, 7)                                                                                                       \
    X_ULP(38, 8)                                                                                                       \
    X_ULP(39, 9)                                                                                                       \
    X_ULP(40, 10)                                                                                                      \
    X_ULP(41, 11)

typedef struct {
    TUYA_GPIO_NUM_E pin_id;
    uint8_t         pin;
    uint8_t         port;
} pin_map_t;

#endif /* __SI91X_PIN_MAP_H__ */
