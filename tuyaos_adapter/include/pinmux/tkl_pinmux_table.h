/*****************************************************************************//**
 * @file tkl_pinmux_table.h
 * @brief (pin, function) -> mux table for the SiWx917, generated -- do not edit.
 *
 * Produced by script/gen/gen_pinmux_table.py from the board's RTE_Device_917.h.
 * Regenerate after changing boards; tests/siwx917 golden-checks the extraction
 * the generator reads.
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#ifndef __TKL_PINMUX_TABLE_H__
#define __TKL_PINMUX_TABLE_H__

#include <stdbool.h>
#include <stdint.h>

/* A pad reachable by one peripheral signal, in that signal's mux mode. */
typedef struct {
    uint16_t func;    /**< TUYA_PIN_FUNC_E value */
    uint8_t  pin;     /**< pad index within its domain */
    uint8_t  mux;     /**< mode for the SDK's set-pin-mode call */
    bool     ulp;     /**< true when the pad lives in the ULP domain */
} tkl_pinmux_entry_t;

static const tkl_pinmux_entry_t g_tkl_pinmux_table[] = {
    /* ---- TUYA_I2S0_SCK ---- */
    {TUYA_I2S0_SCK,    8,  7, false},  /* I2S0_SCLK        pad 3 */
    {TUYA_I2S0_SCK,   25,  7, false},  /* I2S0_SCLK        pad 0 */
    {TUYA_I2S0_SCK,   46,  7, false},  /* I2S0_SCLK        pad 10 */
    {TUYA_I2S0_SCK,   52,  7, false},  /* I2S0_SCLK        pad 16 */
    /* ---- TUYA_I2S0_SDI_0 ---- */
    {TUYA_I2S0_SDI_0, 10,  7, false},  /* I2S0_DIN0        pad 5 */
    {TUYA_I2S0_SDI_0, 27,  7, false},  /* I2S0_DIN0        pad 0 */
    {TUYA_I2S0_SDI_0, 48,  7, false},  /* I2S0_DIN0        pad 12 */
    {TUYA_I2S0_SDI_0, 56,  7, false},  /* I2S0_DIN0        pad 20 */
    /* ---- TUYA_I2S0_SDO_0 ---- */
    {TUYA_I2S0_SDO_0, 11,  7, false},  /* I2S0_DOUT0       pad 6 */
    {TUYA_I2S0_SDO_0, 28,  7, false},  /* I2S0_DOUT0       pad 0 */
    {TUYA_I2S0_SDO_0, 49,  7, false},  /* I2S0_DOUT0       pad 13 */
    {TUYA_I2S0_SDO_0, 57,  7, false},  /* I2S0_DOUT0       pad 21 */
    /* ---- TUYA_I2S0_WS ---- */
    {TUYA_I2S0_WS,     9,  7, false},  /* I2S0_WSCLK       pad 4 */
    {TUYA_I2S0_WS,    26,  7, false},  /* I2S0_WSCLK       pad 0 */
    {TUYA_I2S0_WS,    47,  7, false},  /* I2S0_WSCLK       pad 11 */
    {TUYA_I2S0_WS,    53,  7, false},  /* I2S0_WSCLK       pad 17 */
    /* ---- TUYA_IIC0_SCL ---- */
    {TUYA_IIC0_SCL,    7,  4, false},  /* I2C0_SCL         pad 2 */
    {TUYA_IIC0_SCL,   32, 11, false},  /* I2C0_SCL         pad 9 */
    {TUYA_IIC0_SCL,    1,  4, true },  /* I2C0_SCL         pad 23 */
    {TUYA_IIC0_SCL,    2,  4, true },  /* I2C0_SCL         pad 24 */
    {TUYA_IIC0_SCL,   11,  4, true },  /* I2C0_SCL         pad 33 */
    /* ---- TUYA_IIC0_SDA ---- */
    {TUYA_IIC0_SDA,    6,  4, false},  /* I2C0_SDA         pad 1 */
    {TUYA_IIC0_SDA,   31, 11, false},  /* I2C0_SDA         pad 9 */
    {TUYA_IIC0_SDA,    3,  4, true },  /* I2C0_SDA         pad 25 */
    {TUYA_IIC0_SDA,   10,  4, true },  /* I2C0_SDA         pad 32 */
    /* ---- TUYA_IIC1_SCL ---- */
    {TUYA_IIC1_SCL,    6,  5, false},  /* I2C1_SCL         pad 1 */
    {TUYA_IIC1_SCL,   29,  5, false},  /* I2C1_SCL         pad 0 */
    {TUYA_IIC1_SCL,   33, 11, false},  /* I2C1_SCL         pad 9 */
    {TUYA_IIC1_SCL,   50,  5, false},  /* I2C1_SCL         pad 14 */
    {TUYA_IIC1_SCL,   54,  5, false},  /* I2C1_SCL         pad 18 */
    {TUYA_IIC1_SCL,    2,  5, true },  /* I2C1_SCL         pad 24 */
    {TUYA_IIC1_SCL,    6,  5, true },  /* I2C1_SCL         pad 28 */
    /* ---- TUYA_IIC1_SDA ---- */
    {TUYA_IIC1_SDA,    7,  5, false},  /* I2C1_SDA         pad 2 */
    {TUYA_IIC1_SDA,   30,  5, false},  /* I2C1_SDA         pad 0 */
    {TUYA_IIC1_SDA,   34, 11, false},  /* I2C1_SDA         pad 9 */
    {TUYA_IIC1_SDA,   51,  5, false},  /* I2C1_SDA         pad 15 */
    {TUYA_IIC1_SDA,   55,  5, false},  /* I2C1_SDA         pad 19 */
    {TUYA_IIC1_SDA,    1,  5, true },  /* I2C1_SDA         pad 23 */
    {TUYA_IIC1_SDA,    3,  5, true },  /* I2C1_SDA         pad 25 */
    {TUYA_IIC1_SDA,    7,  5, true },  /* I2C1_SDA         pad 29 */
    /* ---- TUYA_IIC2_SCL ---- */
    {TUYA_IIC2_SCL,    7,  4, false},  /* I2C2_SCL         pad 2 */
    {TUYA_IIC2_SCL,   11,  4, false},  /* I2C2_SCL         pad 6 */
    {TUYA_IIC2_SCL,   15,  4, false},  /* I2C2_SCL         pad 8 */
    {TUYA_IIC2_SCL,   46,  4, false},  /* I2C2_SCL         pad 10 */
    {TUYA_IIC2_SCL,    1,  4, true },  /* I2C2_SCL         pad 0 */
    {TUYA_IIC2_SCL,    7,  4, true },  /* I2C2_SCL         pad 0 */
    {TUYA_IIC2_SCL,    8,  4, true },  /* I2C2_SCL         pad 0 */
    /* ---- TUYA_IIC2_SDA ---- */
    {TUYA_IIC2_SDA,    6,  4, false},  /* I2C2_SDA         pad 1 */
    {TUYA_IIC2_SDA,   10,  4, false},  /* I2C2_SDA         pad 5 */
    {TUYA_IIC2_SDA,   12,  4, false},  /* I2C2_SDA         pad 7 */
    {TUYA_IIC2_SDA,   47,  4, false},  /* I2C2_SDA         pad 11 */
    {TUYA_IIC2_SDA,   49,  4, false},  /* I2C2_SDA         pad 13 */
    {TUYA_IIC2_SDA,    6,  4, true },  /* I2C2_SDA         pad 0 */
    {TUYA_IIC2_SDA,    9,  4, true },  /* I2C2_SDA         pad 0 */
    {TUYA_IIC2_SDA,   11,  4, true },  /* I2C2_SDA         pad 0 */
    /* ---- TUYA_PWM0 ---- */
    {TUYA_PWM0,        6, 10, false},  /* PWM_1L           pad 1 */
    {TUYA_PWM0,        7, 10, false},  /* PWM_1H           pad 2 */
    {TUYA_PWM0,        1,  8, true },  /* PWM_1H           pad 23 */
    /* ---- TUYA_PWM1 ---- */
    {TUYA_PWM1,        8, 10, false},  /* PWM_2L           pad 3 */
    {TUYA_PWM1,        9, 10, false},  /* PWM_2H           pad 4 */
    {TUYA_PWM1,        2,  8, true },  /* PWM_2L           pad 24 */
    {TUYA_PWM1,        3,  8, true },  /* PWM_2H           pad 25 */
    /* ---- TUYA_PWM2 ---- */
    {TUYA_PWM2,       10, 10, false},  /* PWM_3L           pad 5 */
    {TUYA_PWM2,       11, 10, false},  /* PWM_3H           pad 6 */
    /* ---- TUYA_PWM3 ---- */
    {TUYA_PWM3,       12, 10, false},  /* PWM_4L           pad 7 */
    {TUYA_PWM3,       15, 10, false},  /* PWM_4H           pad 8 */
    {TUYA_PWM3,        6,  8, true },  /* PWM_4L           pad 28 */
    {TUYA_PWM3,        7,  8, true },  /* PWM_4H           pad 29 */
    /* ---- TUYA_SPI0_CLK ---- */
    {TUYA_SPI0_CLK,    8,  4, false},  /* GSPI_MASTER_CLK  pad 3 */
    {TUYA_SPI0_CLK,   25,  4, false},  /* GSPI_MASTER_CLK  pad 0 */
    {TUYA_SPI0_CLK,   46,  4, false},  /* GSPI_MASTER_CLK  pad 10 */
    {TUYA_SPI0_CLK,   52,  4, false},  /* GSPI_MASTER_CLK  pad 16 */
    /* ---- TUYA_SPI0_CS ---- */
    {TUYA_SPI0_CS,     9,  4, false},  /* GSPI_MASTER_CS0  pad 4 */
    {TUYA_SPI0_CS,    28,  4, false},  /* GSPI_MASTER_CS0  pad 0 */
    {TUYA_SPI0_CS,    49,  4, false},  /* GSPI_MASTER_CS0  pad 13 */
    {TUYA_SPI0_CS,    53,  4, false},  /* GSPI_MASTER_CS0  pad 17 */
    /* ---- TUYA_SPI0_MISO ---- */
    {TUYA_SPI0_MISO,  11,  4, false},  /* GSPI_MASTER_MISO pad 6 */
    {TUYA_SPI0_MISO,  26,  4, false},  /* GSPI_MASTER_MISO pad 0 */
    {TUYA_SPI0_MISO,  47,  4, false},  /* GSPI_MASTER_MISO pad 11 */
    {TUYA_SPI0_MISO,  56,  4, false},  /* GSPI_MASTER_MISO pad 20 */
    /* ---- TUYA_SPI0_MOSI ---- */
    {TUYA_SPI0_MOSI,   6, 12, false},  /* GSPI_MASTER_MOSI pad 1 */
    {TUYA_SPI0_MOSI,  12,  4, false},  /* GSPI_MASTER_MOSI pad 7 */
    {TUYA_SPI0_MOSI,  27,  4, false},  /* GSPI_MASTER_MOSI pad 0 */
    {TUYA_SPI0_MOSI,  48,  4, false},  /* GSPI_MASTER_MOSI pad 12 */
    {TUYA_SPI0_MOSI,  57,  4, false},  /* GSPI_MASTER_MOSI pad 21 */
    /* ---- TUYA_UART0_CTS ---- */
    {TUYA_UART0_CTS,   6,  2, false},  /* USART0_CTS       pad 1 */
    {TUYA_UART0_CTS,  26,  2, false},  /* USART0_CTS       pad 0 */
    {TUYA_UART0_CTS,  56,  2, false},  /* USART0_CTS       pad 20 */
    {TUYA_UART0_CTS,   6,  2, true },  /* USART0_CTS       pad 28 */
    /* ---- TUYA_UART0_RTS ---- */
    {TUYA_UART0_RTS,   9,  2, false},  /* USART0_RTS       pad 4 */
    {TUYA_UART0_RTS,  28,  2, false},  /* USART0_RTS       pad 0 */
    {TUYA_UART0_RTS,  53,  2, false},  /* USART0_RTS       pad 17 */
    /* ---- TUYA_UART0_RX ---- */
    {TUYA_UART0_RX,   10,  2, false},  /* USART0_RX        pad 5 */
    {TUYA_UART0_RX,   29,  2, false},  /* USART0_RX        pad 0 */
    {TUYA_UART0_RX,   55,  2, false},  /* USART0_RX        pad 19 */
    {TUYA_UART0_RX,    1,  2, true },  /* USART0_RX        pad 23 */
    {TUYA_UART0_RX,    6,  4, true },  /* USART0_RX        pad 28 */
    /* ---- TUYA_UART0_TX ---- */
    {TUYA_UART0_TX,   15,  2, false},  /* USART0_TX        pad 8 */
    {TUYA_UART0_TX,   30,  2, false},  /* USART0_TX        pad 0 */
    {TUYA_UART0_TX,   54,  2, false},  /* USART0_TX        pad 18 */
    {TUYA_UART0_TX,    7,  4, true },  /* USART0_TX        pad 29 */
    /* ---- TUYA_UART1_CTS ---- */
    {TUYA_UART1_CTS,  11,  6, false},  /* UART1_CTS        pad 6 */
    {TUYA_UART1_CTS,  28,  6, false},  /* UART1_CTS        pad 0 */
    {TUYA_UART1_CTS,  51,  9, false},  /* UART1_CTS        pad 15 */
    {TUYA_UART1_CTS,   1,  9, true },  /* UART1_CTS        pad 23 */
    {TUYA_UART1_CTS,   7,  6, true },  /* UART1_CTS        pad 29 */
    /* ---- TUYA_UART1_RTS ---- */
    {TUYA_UART1_RTS,  10,  6, false},  /* UART1_RTS        pad 5 */
    {TUYA_UART1_RTS,  27,  6, false},  /* UART1_RTS        pad 0 */
    {TUYA_UART1_RTS,  50,  9, false},  /* UART1_RTS        pad 14 */
    {TUYA_UART1_RTS,   6,  6, true },  /* UART1_RTS        pad 28 */
    {TUYA_UART1_RTS,   8,  9, true },  /* UART1_RTS        pad 30 */
    /* ---- TUYA_UART1_RX ---- */
    {TUYA_UART1_RX,    6,  6, false},  /* UART1_RX         pad 1 */
    {TUYA_UART1_RX,   29,  6, false},  /* UART1_RX         pad 0 */
    {TUYA_UART1_RX,    2,  9, true },  /* UART1_RX         pad 24 */
    {TUYA_UART1_RX,    8,  6, true },  /* UART1_RX         pad 30 */
    {TUYA_UART1_RX,   10,  9, true },  /* UART1_RX         pad 32 */
    /* ---- TUYA_UART1_TX ---- */
    {TUYA_UART1_TX,    7,  6, false},  /* UART1_TX         pad 2 */
    {TUYA_UART1_TX,   30,  6, false},  /* UART1_TX         pad 0 */
    {TUYA_UART1_TX,    3,  9, true },  /* UART1_TX         pad 25 */
    {TUYA_UART1_TX,    9,  6, true },  /* UART1_TX         pad 31 */
    {TUYA_UART1_TX,   11,  9, true },  /* UART1_TX         pad 33 */
    /* ---- TUYA_UART2_CTS ---- */
    {TUYA_UART2_CTS,   7,  3, false},  /* ULP_UART_CTS     pad 2 */
    {TUYA_UART2_CTS,  11,  3, false},  /* ULP_UART_CTS     pad 6 */
    {TUYA_UART2_CTS,  46,  3, false},  /* ULP_UART_CTS     pad 10 */
    {TUYA_UART2_CTS,   1,  3, true },  /* ULP_UART_CTS     pad 0 */
    {TUYA_UART2_CTS,   8,  3, true },  /* ULP_UART_CTS     pad 0 */
    /* ---- TUYA_UART2_RTS ---- */
    {TUYA_UART2_RTS,   6,  3, false},  /* ULP_UART_RTS     pad 1 */
    {TUYA_UART2_RTS,  10,  3, false},  /* ULP_UART_RTS     pad 5 */
    {TUYA_UART2_RTS,  48,  3, false},  /* ULP_UART_RTS     pad 12 */
    {TUYA_UART2_RTS,  10,  3, true },  /* ULP_UART_RTS     pad 0 */
    /* ---- TUYA_UART2_RX ---- */
    {TUYA_UART2_RX,    8,  3, false},  /* ULP_UART_RX      pad 3 */
    {TUYA_UART2_RX,   12,  3, false},  /* ULP_UART_RX      pad 7 */
    {TUYA_UART2_RX,   47,  3, false},  /* ULP_UART_RX      pad 11 */
    {TUYA_UART2_RX,    2,  3, true },  /* ULP_UART_RX      pad 0 */
    {TUYA_UART2_RX,    6,  3, true },  /* ULP_UART_RX      pad 0 */
    {TUYA_UART2_RX,    9,  3, true },  /* ULP_UART_RX      pad 0 */
    /* ---- TUYA_UART2_TX ---- */
    {TUYA_UART2_TX,    9,  3, false},  /* ULP_UART_TX      pad 4 */
    {TUYA_UART2_TX,   15,  3, false},  /* ULP_UART_TX      pad 8 */
    {TUYA_UART2_TX,   49,  3, false},  /* ULP_UART_TX      pad 13 */
    {TUYA_UART2_TX,    7,  3, true },  /* ULP_UART_TX      pad 0 */
    {TUYA_UART2_TX,   11,  3, true },  /* ULP_UART_TX      pad 0 */
};

#define TKL_PINMUX_TABLE_LEN (sizeof(g_tkl_pinmux_table) / sizeof(g_tkl_pinmux_table[0]))

#endif /* __TKL_PINMUX_TABLE_H__ */
