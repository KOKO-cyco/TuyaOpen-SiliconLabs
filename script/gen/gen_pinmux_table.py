#!/usr/bin/env python3
# coding=utf-8
"""
Turn the RTE pin/mux data into the C table tkl_io_pinmux_config() looks up.

    ./gen_pinmux_table.py <RTE_Device_917.h> > ../../tuyaos_adapter/include/pinmux/tkl_pinmux_table.h

The mux value for a (pin, function) pair is data, not a formula -- the same
signal on a different pad takes a different mux -- so the only way to offer
runtime pin muxing is to carry the table. extract_rte_pinmux.py reads it out of
the vendored board header; this maps Silicon Labs' signal names onto the TUYA
pin-function constants and emits it as C.

Signals with no TUYA equivalent (SIO, QEI, SCT, OPAMP, COMP, the RS485 control
lines, I2S1, the second GSPI chip selects) are dropped rather than invented.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# RTE signal name -> TUYA_PIN_FUNC_E constant.
#
# UART: the part has three blocks and TUYA numbers four ports, so the mapping is
# a choice. USART0/UART1/ULP_UART become TUYA_UART0/1/2, which is also how the
# IDE board manifest names them -- keeping one story across the two.
#
# PWM: TUYA's pin function is the channel, and a Si91x channel drives a
# complementary PAD PAIR off one duty register (see tkl_pwm.c). Both pads of a
# channel therefore map to the same TUYA function; which one a board routes is
# the board's business.
SIGNAL_MAP = {
    "I2C0_SCL": "TUYA_IIC0_SCL", "I2C0_SDA": "TUYA_IIC0_SDA",
    "I2C1_SCL": "TUYA_IIC1_SCL", "I2C1_SDA": "TUYA_IIC1_SDA",
    "I2C2_SCL": "TUYA_IIC2_SCL", "I2C2_SDA": "TUYA_IIC2_SDA",

    "USART0_TX": "TUYA_UART0_TX", "USART0_RX": "TUYA_UART0_RX",
    "USART0_RTS": "TUYA_UART0_RTS", "USART0_CTS": "TUYA_UART0_CTS",
    "UART1_TX": "TUYA_UART1_TX", "UART1_RX": "TUYA_UART1_RX",
    "UART1_RTS": "TUYA_UART1_RTS", "UART1_CTS": "TUYA_UART1_CTS",
    "ULP_UART_TX": "TUYA_UART2_TX", "ULP_UART_RX": "TUYA_UART2_RX",
    "ULP_UART_RTS": "TUYA_UART2_RTS", "ULP_UART_CTS": "TUYA_UART2_CTS",

    "GSPI_MASTER_CLK": "TUYA_SPI0_CLK", "GSPI_MASTER_MISO": "TUYA_SPI0_MISO",
    "GSPI_MASTER_MOSI": "TUYA_SPI0_MOSI", "GSPI_MASTER_CS0": "TUYA_SPI0_CS",

    "PWM_1L": "TUYA_PWM0", "PWM_1H": "TUYA_PWM0",
    "PWM_2L": "TUYA_PWM1", "PWM_2H": "TUYA_PWM1",
    "PWM_3L": "TUYA_PWM2", "PWM_3H": "TUYA_PWM2",
    "PWM_4L": "TUYA_PWM3", "PWM_4H": "TUYA_PWM3",

    "I2S0_SCLK": "TUYA_I2S0_SCK", "I2S0_WSCLK": "TUYA_I2S0_WS",
    "I2S0_DOUT0": "TUYA_I2S0_SDO_0", "I2S0_DIN0": "TUYA_I2S0_SDI_0",
}

# RTE puts ULP pads in the HP numbering space at 64 + index; the CMSIS drivers
# subtract it back out (see SL_I2C_SCL_PIN in cmsis_driver/I2C.c). Rows are
# emitted with the domain split out so the adapter can pick the right SDK call.
GPIO_MAX_PIN = 64


def rows(header):
    out = subprocess.run(
        [sys.executable, os.path.join(HERE, "extract_rte_pinmux.py"), header],
        capture_output=True, text=True, check=True).stdout
    for line in out.splitlines()[1:]:
        f = line.split()
        if len(f) >= 5:
            yield f[0], f[1], f[2], f[3], f[4]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    header = sys.argv[1]

    seen, table, dropped = set(), [], {}
    for signal, port, pin, mux, pad in rows(header):
        func = SIGNAL_MAP.get(signal)
        if func is None:
            dropped[signal] = dropped.get(signal, 0) + 1
            continue
        pin_i, mux_i = int(pin), int(mux)
        # The domain is decided by where the PAD is, not which block owns the
        # signal: RTE_ULP_PORT and HP are both 0, so the port name carries no
        # information, and a ULP peripheral routed onto an HP pad is muxed with
        # the ordinary HP call. Pads at 64 and above are the ULP ones.
        ulp = pin_i >= GPIO_MAX_PIN
        local = pin_i - GPIO_MAX_PIN if pin_i >= GPIO_MAX_PIN else pin_i
        key = (func, local, ulp)
        if key in seen:
            continue
        seen.add(key)
        table.append((func, local, mux_i, ulp, signal, pad))

    table.sort(key=lambda r: (r[0], r[3], r[1]))

    print("/*****************************************************************************//**")
    print(" * @file tkl_pinmux_table.h")
    print(" * @brief (pin, function) -> mux table for the SiWx917, generated -- do not edit.")
    print(" *")
    print(" * Produced by script/gen/gen_pinmux_table.py from the board's RTE_Device_917.h.")
    print(" * Regenerate after changing boards; tests/siwx917 golden-checks the extraction")
    print(" * the generator reads.")
    print(" *******************************************************************************")
    print(" * SPDX-License-Identifier: Zlib")
    print(" ******************************************************************************/")
    print("#ifndef __TKL_PINMUX_TABLE_H__")
    print("#define __TKL_PINMUX_TABLE_H__")
    print("")
    print("#include <stdbool.h>")
    print("#include <stdint.h>")
    print("")
    print("/* A pad reachable by one peripheral signal, in that signal's mux mode. */")
    print("typedef struct {")
    print("    uint16_t func;    /**< TUYA_PIN_FUNC_E value */")
    print("    uint8_t  pin;     /**< pad index within its domain */")
    print("    uint8_t  mux;     /**< mode for the SDK's set-pin-mode call */")
    print("    bool     ulp;     /**< true when the pad lives in the ULP domain */")
    print("} tkl_pinmux_entry_t;")
    print("")
    print("static const tkl_pinmux_entry_t g_tkl_pinmux_table[] = {")
    last = None
    for func, pin, mux, ulp, signal, pad in table:
        if func != last:
            print("    /* ---- %s ---- */" % func)
            last = func
        print("    {%-16s %2d, %2d, %-5s},  /* %-16s pad %s */"
              % (func + ",", pin, mux, "true" if ulp else "false", signal, pad))
    print("};")
    print("")
    print("#define TKL_PINMUX_TABLE_LEN "
          "(sizeof(g_tkl_pinmux_table) / sizeof(g_tkl_pinmux_table[0]))")
    print("")
    print("#endif /* __TKL_PINMUX_TABLE_H__ */")

    hp = sum(1 for r in table if not r[3])
    sys.stderr.write("emitted %d entries (%d HP, %d ULP)\n" % (len(table), hp, len(table) - hp))
    sys.stderr.write("dropped signals with no TUYA equivalent: %s\n"
                     % ", ".join("%s x%d" % (k, v) for k, v in sorted(dropped.items())))


if __name__ == "__main__":
    main()
