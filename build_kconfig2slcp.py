#!/usr/bin/env python3
# coding=utf-8

import sys
import os

maps = [
    {
        "name": "CONFIG_ENABLE_ULP_UART",
        "component": [
            {
                "name":   "sl_ulp_uart",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
            {
                "name": "SL_ULPUART_DMA_CONFIG_ENABLE",
                "value": "0"
            },
            {
                "name":  "ULP_UART_UC",
                "value": "0"
            }
        ]
    },
    {
        "name": "CONFIG_ENABLE_USART0",
        "component": [
            {
                "name":   "sl_usart",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
            {
                "name": "SL_USART0_DMA_CONFIG_ENABLE",
                "value": "0"
            },
            {
                "name":  "USART_UC",
                "value": "0"
            }
        ]
    },
    {
        "name": "CONFIG_ENABLE_UART1",
        "component": [
            {
                "name":   "sl_uart",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
            {
                "name": "SL_UART1_DMA_CONFIG_ENABLE",
                "value": "0"
            },
            {
                "name":  "UART_UC",
                "value": "0"
            }
        ]
    },
    {
        "name": "CONFIG_ENABLE_I2C",
        "component": [
            {
                "name":   "sl_i2c",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        # tkl_rtc.c drives the NPSS calendar block. sl_calendar pulls in
        # systemlevel_rtc and systemlevel_time_period itself, so listing the
        # unified-API component alone is enough.
        "name": "CONFIG_ENABLE_RTC",
        "component": [
            {
                "name":   "sl_calendar",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        # tkl_pwm.c drives the MCPWM block. sl_pwm brings rsilib_pwm and
        # romdriver_pwm with it, so the unified-API component is enough.
        "name": "CONFIG_ENABLE_PWM",
        "component": [
            {
                "name":   "sl_pwm",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        # tkl_adc.c drives the AUX ADC in static (one-shot) mode.
        "name": "CONFIG_ENABLE_ADC",
        "component": [
            {
                "name":   "sl_adc",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        # tkl_dac.c drives the AUX DAC in static mode.
        "name": "CONFIG_ENABLE_DAC",
        "component": [
            {
                "name":   "sl_dac",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    }
]

component_template = """  - id: $name
    from: $extension
"""

configuration_template = """- {name: $name, value: '$value'}
"""


def read_existing_components(slcp_file):
    """Read existing components from slcp file to avoid duplicates"""
    existing_components = set()
    existing_configurations = set()

    if not os.path.exists(slcp_file):
        return existing_components, existing_configurations

    with open(slcp_file, 'r') as f:
        lines = f.readlines()

    in_component_section = False
    in_configuration_section = False

    for line in lines:
        line = line.strip()
        if line.startswith("component:"):
            in_component_section = True
            in_configuration_section = False
            continue
        elif line.startswith("configuration:"):
            in_component_section = False
            in_configuration_section = True
            continue
        elif line.startswith("requires:") or line.startswith("define:") or line.startswith("toolchain_settings:"):
            in_component_section = False
            in_configuration_section = False
            continue

        if in_component_section and line.startswith("- id:"):
            # Extract component id: "- id: sl_ulp_uart"
            component_id = line.split(":", 1)[1].strip()
            existing_components.add(component_id)
        elif in_configuration_section and line.startswith("- {name:"):
            # Extract configuration name: "- {name: SL_ULPUART_DMA_CONFIG_ENABLE, value: '0'}"
            config_name = line.split("name:", 1)[1].split(",")[0].strip()
            existing_configurations.add(config_name)

    return existing_components, existing_configurations


def kconfig2slcp(config_file, slcp_file):
    existing_components, existing_configurations = read_existing_components(
        slcp_file)
    components = ""
    configurations = ""

    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            for i, mapping in enumerate(maps):
                if mapping["name"] in line and "=y" in line:
                    print(f"{line}")
                    for component in mapping["component"]:
                        name = component["name"]
                        extension = component["extension"]
                        if name not in existing_components:
                            components += component_template.replace(
                                "$name", name).replace("$extension", extension)
                            existing_components.add(name)
                            print(f"---> Add {name} from {extension}")
                        else:
                            print(f"---> Skip duplicate component {name}")
                    for configuration in mapping["configuration"]:
                        name = configuration["name"]
                        value = configuration["value"]
                        if name not in existing_configurations:
                            configurations += configuration_template.replace(
                                "$name", name).replace("$value", value)
                            existing_configurations.add(name)
                            print(
                                f"---> Add configuration {name} value {value}")
                        else:
                            print(f"---> Skip duplicate configuration {name}")

    # Read and modify slcp_file
    if os.path.exists(slcp_file):
        with open(slcp_file, 'r') as f:
            lines = f.readlines()

        modified_lines = []
        for i, line in enumerate(lines):
            modified_lines.append(line)
            if "component:" in line.strip():
                # Add components after the "component:" line
                modified_lines.append(components)
            elif "configuration:" in line.strip():
                # Add configurations after the "configuration:" line
                modified_lines.append(configurations)

        # Write back to slcp_file
        with open(slcp_file, 'w') as f:
            f.writelines(modified_lines)

        print(f"Updated {slcp_file} with components and configurations")


def main():
    if len(sys.argv) < 2:
        print(f"Error: At least 2 parameters are needed {sys.argv}.")
        sys.exit(1)
    config_file = sys.argv[1]
    slcp_file = sys.argv[2]
    print("================== slcp_generate ==================")
    kconfig2slcp(config_file, slcp_file)


if __name__ == "__main__":
    main()
