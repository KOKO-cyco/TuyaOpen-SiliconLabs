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
        "name": "CONFIG_ENABLE_DAC",
        "component": [
            {
                "name":   "sl_dac",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        "name": "CONFIG_ENABLE_SIWX917_TICKLESS",
        "component": [
            {
                "name":   "sl_power_manager",
                "extension": "wiseconnect3_sdk",
            },
            {
                "name":   "wireless_wakeup_ulp_component",
                "extension": "wiseconnect3_sdk",
            }
        ],
        "configuration": [
        ]
    },
    {
        "name": "CONFIG_ENABLE_SPI",
        "component": [
            {"name": "sl_gspi", "extension": "wiseconnect3_sdk"},
            {"name": "sl_ssi", "extension": "wiseconnect3_sdk"},
            {"name": "sl_ssi_instance", "extension": "wiseconnect3_sdk",
             "instance": "primary"},
            {"name": "sl_ssi_instance", "extension": "wiseconnect3_sdk",
             "instance": "secondary"},
            {"name": "sl_ssi_instance", "extension": "wiseconnect3_sdk",
             "instance": "ulp_primary"},
        ],
        "configuration": [
            {"name": "SL_GSPI_DMA_CONFIG_ENABLE", "value": "1"},
            {"name": "GSPI_UC", "value": "0"},
        ],
    },
    {
        "name": "CONFIG_ENABLE_AUDIO",
        "component": [
            {"name": "i2s_instance", "extension": "wiseconnect3_sdk",
             "instance": "i2s0"},
        ],
        "configuration": [],
    },
    {
        "name": "CONFIG_ENABLE_EXT_RAM",
        "component": [
            {"name": "psram_core", "extension": "wiseconnect3_sdk"},
            {"name": "psram_aps6404l_sqh", "extension": "wiseconnect3_sdk"},
        ],
        "configuration": [],
        "define": [
            {"name": "SLI_SI91X_MCU_ENABLE_PSRAM_FEATURE"},
            {"name": "TKL_MEMORY", "value": "MEMORY_FREERTOS_HEAP_PSRAM"},
        ],
    },
    {
        "name": "CONFIG_SIWX917_FREERTOS_HEAP_SIZE",
        "component": [],
        "configuration": [
            {"name": "configTOTAL_HEAP_SIZE", "from_config": True},
        ],
    },
    {
        "name": "CONFIG_SIWX917_FREERTOS_HEAP_SIZE_PSRAM",
        "component": [],
        "configuration": [
            {"name": "configTOTAL_HEAP_SIZE_PSRAM", "from_config": True,
             "skip_values": ["0"]},
        ],
    },
    {
        "name": "CONFIG_SIWX917_TFLITE_ARENA_SIZE",
        "component": [],
        "configuration": [
            {"name": "SL_TFLITE_MICRO_ARENA_SIZE", "from_config": True,
             "skip_values": ["0"]},
        ],
    },
]

component_template = """  - id: $name
    from: $extension
"""

instance_template = """  - id: $name
    from: $extension
    instance: [$instance]
"""

configuration_template = """- {name: $name, value: '$value'}
"""

define_template = """  - name: $name
"""

define_value_template = """  - name: $name
    value: $value
"""


def read_existing_components(slcp_file):
    """Read existing components from slcp file to avoid duplicates"""
    existing_components = set()
    existing_configurations = set()
    existing_defines = set()

    if not os.path.exists(slcp_file):
        return existing_components, existing_configurations, existing_defines

    with open(slcp_file, 'r') as f:
        lines = f.readlines()

    section = None

    for line in lines:
        line = line.strip()
        if line.startswith("component:"):
            section = "component"
            continue
        elif line.startswith("configuration:"):
            section = "configuration"
            continue
        elif line.startswith("define:"):
            section = "define"
            continue
        elif line.startswith("requires:") or line.startswith("toolchain_settings:"):
            section = None
            continue

        if section == "component" and line.startswith("- id:"):
            # Extract component id: "- id: sl_ulp_uart"
            existing_components.add(line.split(":", 1)[1].strip())
        elif section == "configuration" and line.startswith("- {name:"):
            # Extract configuration name: "- {name: SL_ULPUART_DMA_CONFIG_ENABLE, value: '0'}"
            existing_configurations.add(line.split("name:", 1)[1].split(",")[0].strip())
        elif section == "define" and line.startswith("- name:"):
            existing_defines.add(line.split(":", 1)[1].strip())

    return existing_components, existing_configurations, existing_defines


def config_value(line, symbol):
    value = line.split("=", 1)[1].strip()
    if len(value) >= 2 and value[0] == value[-1] == '"':
        value = value[1:-1]
    return value


def kconfig2slcp(config_file, slcp_file):
    existing_components, existing_configurations, existing_defines = \
        read_existing_components(slcp_file)
    components = ""
    configurations = ""
    defines = ""

    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            for i, mapping in enumerate(maps):
                symbol = mapping["name"]
                if not line.startswith(symbol + "="):
                    continue
                if line == symbol + "=n" or line.endswith("is not set"):
                    continue
                print(f"{line}")
                for component in mapping["component"]:
                    name = component["name"]
                    extension = component["extension"]
                    instance = component.get("instance")
                    key = f"{name}[{instance}]" if instance else name
                    if key in existing_components:
                        print(f"---> Skip duplicate component {key}")
                        continue
                    if instance:
                        components += instance_template.replace(
                            "$name", name).replace(
                            "$extension", extension).replace(
                            "$instance", instance)
                    else:
                        components += component_template.replace(
                            "$name", name).replace("$extension", extension)
                    existing_components.add(key)
                    print(f"---> Add {key} from {extension}")
                for configuration in mapping["configuration"]:
                    name = configuration["name"]
                    if configuration.get("from_config"):
                        value = config_value(line, symbol)
                    else:
                        value = configuration["value"]
                    if value in configuration.get("skip_values", []):
                        print(f"---> Skip configuration {name}: value {value}")
                        continue
                    if name not in existing_configurations:
                        configurations += configuration_template.replace(
                            "$name", name).replace("$value", value)
                        existing_configurations.add(name)
                        print(f"---> Add configuration {name} value {value}")
                    else:
                        print(f"---> Skip duplicate configuration {name}")
                for define in mapping.get("define", []):
                    name = define["name"]
                    if define.get("from_config"):
                        value = config_value(line, symbol)
                    else:
                        value = define.get("value")
                    if value is not None and value in define.get("skip_values", []):
                        print(f"---> Skip define {name}: value {value}")
                        continue
                    if name in existing_defines:
                        print(f"---> Skip duplicate define {name}")
                        continue
                    if value is None:
                        defines += define_template.replace("$name", name)
                        print(f"---> Add define {name}")
                    else:
                        defines += define_value_template.replace(
                            "$name", name).replace("$value", value)
                        print(f"---> Add define {name} value {value}")
                    existing_defines.add(name)

    # Read and modify slcp_file
    if os.path.exists(slcp_file):
        with open(slcp_file, 'r') as f:
            lines = f.readlines()

        modified_lines = []
        for i, line in enumerate(lines):
            modified_lines.append(line)
            stripped = line.strip()
            if stripped == "component:":
                # Add components after the "component:" line
                modified_lines.append(components)
            elif stripped == "configuration:":
                # Add configurations after the "configuration:" line
                modified_lines.append(configurations)
            elif stripped == "define:":
                modified_lines.append(defines)

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
