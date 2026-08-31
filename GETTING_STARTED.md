# Getting Started with TuyaOpen for SiWx917 Platform

This guide explains how to set up and build TuyaOpen with SiWx917 support on Linux
and on macOS. See [Prerequisites](#prerequisites) for what has actually been run on
which host.

The SiWx917 platform is **not** a standalone project. It must live inside the TuyaOpen tree at:

```text
~/TuyaOpen/platform/SIWX917
```

> **Note:** SiWx917 is a first-class platform in current TuyaOpen. Board support lives in
> `boards/SIWX917/` in the main repository, so no integration patch has to be applied.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Project Structure](#project-structure)
- [Overview](#overview)
- [Step-by-Step Setup](#step-by-step-setup)
  - [Step 1: Clone TuyaOpen](#step-1-clone-tuyaopen)
  - [Step 2: Prepare SiWx917 Platform](#step-2-prepare-siwx917-platform)
  - [Step 3: Set Up Build Environment](#step-3-set-up-build-environment)
  - [Step 4: Configure the Application](#step-4-configure-the-application)
  - [Step 5: Build](#step-5-build)
- [Quick Reference](#quick-reference)
- [Next Steps](#next-steps)
- [Notes](#notes)
- [Additional Resources](#additional-resources)

## Prerequisites

Needed on every host:

- **Git**
- **Python 3.12** -- installed and managed by `export.sh` / `uv`, not by you
- CMake, Ninja and the Python packages -- installed by `export.sh`
- Silicon Labs SLC and Simplicity Commander -- fetched by `platform_prepare.py`
  into `platform/SIWX917/tools/`, nothing goes into the system

Verify basics:

```bash
python3 --version
git --version
```

### Host platforms

What has actually been run, rather than what ought to work:

| Host | State |
|------|-------|
| Ubuntu 22.04 LTS or compatible Linux | build, flash, monitor |
| macOS 15 on Intel (`x86_64`) | build, flash, monitor -- measured 2026-08-21 |
| macOS on Apple Silicon | untested |
| Windows | untested -- the code paths exist and have never run |

### macOS

- **Xcode Command Line Tools is all you need**: `xcode-select --install`.
  Homebrew is not required, and nothing here installs system packages on a Mac
  -- verified by a build that left the Homebrew formula count unchanged.
- **Ignore `/usr/bin/java`.** On macOS it is a stub that answers `command -v`
  and then fails with "Unable to locate a Java Runtime". The bootstrap tests
  whether java *runs*, and fetches a Temurin JRE into
  `platform/SIWX917/tools/jre` for slc when it does not. Installing Homebrew's
  `openjdk` does not help either: that formula is keg-only and never becomes
  `java`.
- **The first `source export.sh` is slow and silent.** There is no prebuilt
  `cryptography` wheel for macOS `x86_64` on Python 3.12, so uv builds it from
  source -- about 15 minutes, measured, with no output while it works, because
  `export.sh` runs uv with `--quiet`. If it is stuck rather than slow,
  `TUYAOPEN_EXPORT_VERBOSE=1` shows uv's own progress and errors, and
  `TUYAOPEN_PYPI_MIRROR=0` switches off the mainland-China mirror (which did
  stall once, for 12 minutes at 0% CPU).
- **Serial ports** appear as `/dev/cu.usbserial-*`. Both `tos.py flash` and
  `tos.py monitor` list what they find, so there is nothing to configure.

## Project Structure

```text
~/TuyaOpen/
├── .venv/
├── apps/
│   ├── tuya.ai/your_chat_bot/config/SIWX917_AI_DEV_KIT.config
│   └── tuya_cloud/switch_demo/config/SIWX917_AI_DEV_KIT.config
├── boards/SIWX917/                  # first-class board support in main repo
├── platform/
│   ├── platform_config.yaml         # includes SIWX917 entry
│   └── SIWX917/                     # this platform repository
│       ├── tuyaos_adapter/
│       ├── mcu/
│       │   └── patch/RS9117_WC_SI.rps   # TA firmware (flash once per device)
│       ├── slc/
│       └── tools/
└── export.sh
```

## Overview

Setup flow:

1. Clone a TuyaOpen tree that already contains `boards/SIWX917/` and a `SIWX917` entry in `platform/platform_config.yaml`
2. Ensure `platform/SIWX917` points at this repository (`dev` branch recommended while porting)
3. Configure and build `switch_demo` first, then `your_chat_bot`

## Step-by-Step Setup

### Step 1: Clone TuyaOpen

```bash
mkdir -p ~/TuyaOpen
cd ~/TuyaOpen
git clone https://github.com/tuya/TuyaOpen.git .
```

Use a tree that already integrates SiWx917 (boards + `platform_config.yaml`). If you are developing against a local fork, keep that commit pinned.

### Step 2: Prepare SiWx917 Platform

`tos.py` / platform prepare downloads platforms listed in `platform/platform_config.yaml`.

For local development you can also place this repo directly:

```bash
cd ~/TuyaOpen
mkdir -p platform
git clone https://github.com/tuya/TuyaOpen-SiliconLabs.git platform/SIWX917
cd platform/SIWX917
git checkout dev
```

Or symlink a working copy:

```bash
ln -sfn /path/to/TuyaOpen-SiliconLabs ~/TuyaOpen/platform/SIWX917
```

Confirm `platform/platform_config.yaml` contains:

```yaml
- name: SIWX917
  repo: https://github.com/tuya/TuyaOpen-SiliconLabs
  branch: dev
  commit: <pinned-commit-sha>
```

### Step 3: Set Up Build Environment

```bash
cd ~/TuyaOpen
source export.sh
```

This script:

- Creates `~/TuyaOpen/.venv`
- Syncs Python dependencies
- Prepares host build tools

Run `source export.sh` in every new terminal session before building.

### Step 4: Configure the Application

Select the board config with `tos.py config choice` and pick **`SIWX917_AI_DEV_KIT.config`**
by name — the menu index shifts as new board configs are added. `-c NAME` skips the
menu entirely, which is what to use in a script:

```bash
tos.py config choice -c SIWX917_AI_DEV_KIT   # by name, no prompt
tos.py config choice -l                      # list what is on offer
```

```bash
cd ~/TuyaOpen/apps/tuya_cloud/switch_demo   # smoke test first
source ~/TuyaOpen/export.sh
tos.py config choice
```

```bash
cd ~/TuyaOpen/apps/tuya.ai/your_chat_bot    # then the AI chat bot
tos.py config choice
```

`tos.py config choice` copies the selected file over `app_default.config` — that
is how the tool records the active selection — and then regenerates the build
cache. Treat `app_default.config` as tool-managed scratch: keep durable settings
in `config/<BOARD>.config`, and do **not** commit the churn `config choice` leaves
behind. `app_default.config` is the app's default for every platform, so
committing a SiWx917 copy of it makes an unconfigured build target SiWx917 for
everyone else.

#### Advanced configuration (optional)

```bash
tos.py config menu
```

Useful options under **SIWX917**:

| Menu | Options |
|------|---------|
| Choice a board | `SIWX917_AI_DEV_KIT` (recommended), `BRD2605A` |
| Peripherals config | UART, I2S, GPIO, SPI/I2C |
| Core M4 Flash Size | `2040 KB` (default), `3008 KB` |

**Changing M4 flash size requires an MBR update on the device.** After building
with `3008 KB`, the build prints instructions and generates `mbr_config.json`:

```bash
commander manufacturing write tambr  --data mbr_config.json -d SiWG917M111MGTBA
commander manufacturing write m4mbrcf --data mbr_config.json -d SiWG917M111MGTBA
```

Commander is bundled at `platform/SIWX917/tools/commander/commander`.

### Step 5: Build

```bash
tos.py build
```

Firmware artifacts are under the app `dist/` / `.build/` directories.

## Quick Reference

| Item | Value |
|------|-------|
| Platform name | `SIWX917` |
| Board (AI kit) | `SIWX917_AI_DEV_KIT` |
| Platform repo | `platform/SIWX917` |
| Chat bot config | `apps/tuya.ai/your_chat_bot/config/SIWX917_AI_DEV_KIT.config` |
| Switch demo config | none of its own; `switch_demo` has no `config/` directory, so `config choice` there offers the board defaults from `boards/SIWX917/config/` |

## Next Steps

### 0. TA (NWP) firmware — check before writing

The radio runs its own firmware, separate from the M4 application built here,
and the application cannot start without it. Boards normally arrive with it
already programmed, so **check first and write only if you have to**: writing it
erases the NWP region, and an interrupted write has left a board needing
recovery.

`tos.py flash` reads the device's version and writes TA only when the device
reports having none. To look yourself:

```bash
cd platform/SIWX917
tools/commander/commander mfg917 info -d SiWG917M111MGTBA --json
```

`nwp_firmware_version` in that output means an image is *stored*. It does not
mean the image is intact, nor that it is the one the boot process will load --
both have been seen false on a device reporting a version that looked perfectly
normal. It also does not mean the image has finished installing: one board read
its new version back and still would not run until it was power-cycled. When
the application starts but the radio does not, go to
[Recovering a device whose radio will not start](#recovering-a-device-whose-radio-will-not-start).

To write it deliberately, pick **TA ONLY** from the `tos.py flash` menu, or skip
the menu:

```bash
SIWX917_FLASH=ta tos.py flash              # over SWD, if a probe is attached
SIWX917_FLASH=ta tos.py flash -p /dev/ttyUSB0   # over serial/ISP
```

Both channels can write it. Over SWD, `commander rps load` uploads a
flash-loader algorithm into RAM and lets it drive the NWP bootloader -- the
mechanism AN1497 (SiWx917 SoC SWD Algorithm Programmer) documents; Commander
prints *Uploading flashloader...* when it does this. No ISP keypress is
involved on that path.

> An earlier revision of this guide said SWD had no working path for TA
> firmware, citing `commander rps load` writing nothing and `commander flash`
> hanging at *Waiting for bootloader to perform upgrade*. Verified 2026-08-20:
> `commander rps load <ta>.rps -d SiWG917M111MGTBA --tif SWD` completes on both
> boards tried, and the radio starts.
>
> What the old observations were really showing is that **a completed write is
> not a started radio**. On one board the write reported DONE, `mfg917 info`
> read the new version back, and the application never printed a line -- until
> the board was power-cycled, after which it came up normally. The install
> appears to be finalised at reset, and the reset Commander issues over a plain
> J-Link does not always do it. Power-cycle before concluding the write failed.

### 1. Flash M4 application firmware

Use Simplicity Commander to flash the build output from the app's `dist/` directory.

### 2. Provision and test

1. Provision the device over BLE (see [Notes](#notes))
2. Run the example and verify Wi-Fi and cloud connectivity
3. Keep Release builds for voice apps; Debug can hurt audio real-time performance

## Notes

### Network provisioning

On SiWx917, only **BLE provisioning** works: SoftAP and BLE cannot coexist on this
chip. The adapter enforces this itself — `tkl_wifi_start_ap()` and
`tkl_wifi_set_work_mode(WWM_SOFTAP / WWM_STATIONAP)` return `OPRT_NOT_SUPPORTED`
in STA-only mode, so an app requesting `NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP`
still provisions over BLE. No application edit is required.

### Debug task status

To enable FreeRTOS run-time stats, add to `.build/slc/autogen/sl_event_handler.c`:

```c
__attribute__((weak)) uint32_t ulGetRunTimeCounterValue(void)
{
  return osKernelGetTickCount();
}
```

And in `.build/slc/config/FreeRTOSConfig.h`:

```c
extern uint32_t ulGetRunTimeCounterValue(void);
void vConfigureTimerForRunTimeStats(void);
#define configGENERATE_RUN_TIME_STATS 1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE() ulGetRunTimeCounterValue()
```

Then call `tkl_system_print_task_stats()` from your application.

### Build and porting notes

- Chip vendor SDKs (Simplicity / Wiseconnect) are downloaded by `platform_prepare.py` and pinned in `platform_libsdepend`
- `build_setup.py` expects the app build directory as the 5th CLI argument (passed by current `tos.py` / `cli_build.py`)
- AES-GCM uses platform AES via `ENABLE_PLATFORM_AES` + `tal_aes_gcm_*`, with the scratch buffer allocated per call so concurrent callers cannot corrupt each other. Note there is **no runtime fallback**: `ENABLE_PLATFORM_AES` is a compile-time either/or (`src/tal_security/src/mbedtls/mbedtls_symmetry.c` guards its software `tkl_aes_gcm_*` with `#if !defined(ENABLE_PLATFORM_AES)`), and `tal_aes_gcm_encode/decode` forward straight to the platform. A hardware failure returns an error to the caller. The only callers are the AI image/video paths in `src/tuya_ai_service/` -- not TLS
- No-codec playback volume is applied as digital gain in the board's `tdd_audio_no_codec`
- MP3 playback needs `MP3_DECODER_STATIC_BUF` (selected by the SiWx917 boards). Without it the minimp3 scratch comes from PSRAM via `MP3_MALLOC`, and the per-frame access latency stutters audibly. It trades ~24KB of internal RAM for that and makes decoding single-stream only
- Large static buffers can use `TUYA_MEM_SECTION_RAM` / `TUYA_MEM_SECTION_PSRAM` (`tuya_mem_section.h`). The section names carry **no** leading dot and the linker scripts must match exactly, or the input section is silently orphaned
- **Anti-stutter (latest TuyaOpen):** keep large `AI_*_RINGBUF_SIZE` in board config; enable both `ENABLE_EXT_RAM` (AI path) and `CONFIG_SPIRAM` (adapter). Latest defaults (20KB) are too small for voice.

## Recovering a device whose radio will not start

The application boots and prints one of these, then gets no further:

```
[tuyaos][E][app_tuya.c] WiFi initialization error 16056    SL_STATUS_VALID_FIRMWARE_NOT_PRESENT
[tuyaos][E][app_tuya.c] WiFi initialization error 16059    SL_STATUS_CARD_READY_TIMEOUT
[tuyaos][I][app_tuya.c] Failed to bring m4_ta_secure_handshake: 0x7   (timeout)
```

**Do not start by rewriting the firmware.** On a real recovery the stored image
was intact and rewriting it three different ways -- 1.6 MB each time -- fixed
nothing, because what had broken was the bootloader's record of *which* image to
load. Ask the ROM bootloader instead; it can answer both questions and repair
the second in a few bytes.

Wire the ISP UART and put the chip in ISP mode:

| Adapter | → | Chip | GPIO | Package pin |
|---|---|---|---|---|
| TX | → | RX | GPIO_8 | A20 |
| RX | ← | TX | GPIO_9 | A21 |
| GND | — | GND | — | — |

ISP mode: hold GPIO_34 (`JTAG_TDO_SWO`) low, tap Reset, release. The chip
samples that pin as reset is released; the datasheet (5.8.6) says it must be
left *unconnected* during reset for the bootloader to skip ISP and run the
flash.

Holding it low does **not** take SWD away. The datasheet calls SWD a two-pin
port (5.9) and `JTAG_TDO_SWO` is the pin JTAG shares with SWV, so it is not one
of the two -- SWD runs on `JTAG_TCK_SWCLK`/GPIO_31 and `JTAG_TMS_SWDIO`/GPIO_33
(6.3). 5.8.6 gives ISP's purpose as reprogramming the flash "if the application
code uses JTAG pins for functional use", i.e. ISP is the way in when JTAG is
unavailable, not a switch that disables debugging.

The log UART is a *different* pair, so `tos.py monitor` and this cannot share
wires. The datasheet names the ISP pair explicitly: `GPIO_8 / ISP_UART_RX` and
`GPIO_9 / ISP_UART_TX` (6.3).

### Where the application log comes out

`tkl_uart.c` maps `TUYA_UART_NUM_0` -- the port `app_tuya.c` opens and
`syscalls.c`'s `_write()` prints through -- to the **ULP UART**, so every
`printf` leaves on whichever pins `RTE_ULP_UART_*_PORT_ID` selects. With the
board's current `ENABLE_ULP_UART=y`, `TX_PORT_ID=1`, `RX_PORT_ID=2`:

| Adapter | → | Chip signal | TuyaOpen GPIO | Flat pin |
|---|---|---|---|---|
| RX | ← | ULP_GPIO_11 (log TX) | `GPIO_NUM_41` | 75 |
| TX | → | ULP_GPIO_9 (log RX) | `GPIO_NUM_39` | 73 |
| GND | — | GND | | |

115200 8N1; connecting RX and GND is enough to read the log. The flat numbers
come from `RTE_ULP_UART_TX_PIN = 11 + GPIO_MAX_PIN` with `GPIO_MAX_PIN` 64.

On a kit with an on-board Silicon Labs debugger (DK2605A/BRD2605A) this already
lands on the J-Link VCOM, so `tos.py monitor` finds it with no wiring. A board
driven by an external probe has no VCOM -- wire a USB-serial adapter to the pins
above, or there will be no log however well the flash went.

### Talking to the ROM bootloader

Any serial terminal will do (Tera Term, minicom, `screen`, a dozen lines of
pyserial). Send `Ctrl+\` (0x1C) to wake it, then `U` to print its menu.

These are the entries that matter here. `B` / `4` / `1` are given by AN1431 and
the Matter documentation, and the SDK spells the first one
`#define BURN_NWP_FW 'B'` (`sl_si91x_constants.h`). The rest of this list is
transcribed from a menu a device actually printed -- treat it as observed
rather than documented:

| Key | Entry | Writes flash? |
|---|---|---|
| `K` | Check Wireless Firmware Integrity (Image No : 0-f) | no |
| `5` | Select Default Wireless Firmware (Image No : 0-f) | a few bytes |
| `1` | Load Default Wireless Firmware | no |
| `B` | Burn Wireless Firmware (Image No : 0-f) | yes, ~1.6 MB |
| `A` | Load Wireless Firmware (Image No : 0-f) | no |
| `F` | Select M4 and Wireless Images Pair | a few bytes |
| `4` → `1` | Burn M4 Firmware, image 1 | yes |
| `b` | Change UART Baud Rate | no |

Work in this order, because the cost differs by orders of magnitude:

1. **`K` on each slot 0-f.** This is the only way to tell an image that is
   *stored* from one that is *stored and intact* -- `mfg917 info` reads
   metadata and cannot make that distinction.
2. **Power-cycle before anything else.** A staged image is finalised at reset,
   and a debug probe's reset may not be enough. This has recovered a board that
   looked dead.
3. **`5` + slot** if a slot passes integrity but the device still reports
   16056. That repoints the default in a few bytes instead of retransmitting
   1.6 MB.
4. **`B` + slot** only when no slot holds a good image.

### Transferring an image over Kermit

The bootloader's Kermit receiver is a minimal implementation, and C-Kermit's
defaults do not work with it. These settings come from reading the device's
own Send-Init response and the packet log of a transfer that failed:

```
set send packet-length 94       ; device advertises MAXL=94, CAPAS=2, CHKT=1
set receive packet-length 94    ; C-Kermit's default extended-length packets
set window 1                    ; are NAKed, as is any sliding window
set block-check 1
set retry 20
set prefixing all               ; default is CAUTIOUS, which sends 0x0c/0x80
                                ; bare; the ROM receiver NAKs bare controls
set transfer cancellation off   ; a stray keystroke is read as X/Z (cancel);
                                ; run the client with stdin on /dev/null too
set file type binary
set flow-control none
set handshake none
set carrier-watch off
```

Then `send <file>.rps`. AN1431 quotes ~85 s for the NWP image at 921600 baud;
at 115200 the same transfer runs well over ten minutes, so raise the rate
(menu key `b`, or `set speed`) before starting.

Use `mcu/patch/RS9117_WC_SI.rps`, the image with measured evidence of booting
on this hardware. The SDK also ships
`sdks/wiseconnect/connectivity_firmware/standard/SiWG917-B.*.rps`; writing that
over a working device has not been shown to be an improvement, and once left a
board that would not start.

Release GPIO_34, power-cycle, and check the log again. A recovered device
prints:

```
WiFi initialization success
m4_ta_secure_handshake success
Running TA fw: 1611.2.1.1.255.11.63
```

## Additional Resources

- [TuyaOpen](https://github.com/tuya/TuyaOpen)
- [TuyaOpen-SiliconLabs](https://github.com/tuya/TuyaOpen-SiliconLabs)
- [AN1431: SiWx917 SoC Firmware Update](https://docs.silabs.com/wifi-application-notes/latest/wifi-siwx917-soc-firmware-update-application-note/update-mechanisms)
