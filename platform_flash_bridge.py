#!/usr/bin/env python3
# coding=utf-8
"""
SiWx917 flashing hook for `tos.py flash`.

tools/cli_command/cli_flash.py looks for platform_flash_bridge.py in the
selected platform's root and, when present, calls platform_flash() instead of
tyutool.

Simplicity Commander can reach this part over two physically different
channels, and both can write either firmware:

    channel        command                 needs
    -------------  ----------------------  ------------------------------------
    SWD / J-Link   commander rps load      a debug probe on SWCLK/SWDIO
    serial / ISP   commander serial load   ISP mode (GPIO_34 low, then Reset)

Both take a .rps and route it by the image type in its RPS header, so the same
command writes either the M4 application or the NWP/TA wireless firmware.
Writing both means writing two images in sequence; a single merged image was
tried and does not load (see the comment next to _find_ta_image).

`rps load` uploads a flash-loader algorithm into RAM and lets it drive the NWP
bootloader -- the mechanism AN1497 (SiWx917 SoC SWD Algorithm Programmer)
documents. Commander prints "Uploading flashloader..." when it does this. No ISP
keypress is involved on this path.

    Earlier revisions of this file claimed SWD had no working path for NWP/TA
    firmware. Measured 2026-08-20 on a DK2605A: `rps load <ta>.rps --tif SWD`
    completes and the board boots. Two boards have now been written this way.

    A completed write is still not a started radio. On one board the write
    reported DONE, `mfg917 info` read the new version back, and the application
    never printed a line -- until the board was power-cycled, after which it
    came up normally. The install looks to be finalised at reset, and the reset
    Commander issues over a plain J-Link does not always do it. Power-cycle
    after writing NWP firmware before concluding anything.

Channel and target are both chosen from a menu rather than guessed, so what got
detected is visible before anything is written. -p <port> and the SIWX917_*
environment variables below skip the prompts for CI and production programming.

Writing NWP/TA firmware erases the radio's flash first; an interrupted write has
left a board needing recovery. It therefore happens only when explicitly asked
for, never as a repair for firmware that is merely different, and the menu
says when the image on disk is the one the device already runs. When a device
flashes cleanly and the radio still will not start, GETTING_STARTED.md has the
recovery procedure under "Recovering a device whose radio will not start".

This module is loaded by importlib.util.spec_from_file_location() under a
synthetic module name, so its package context is not set up and it cannot
import the platform's script.util helpers -- hence the local platform.system()
call instead of reusing get_system_name().
"""

import glob
import json
import os
import platform
import subprocess
import sys

# -----------------------------------------------------------------------------
#                                  Constants
# -----------------------------------------------------------------------------

DEFAULT_DEVICE = "SiWG917M111MGTBA"

# Silicon Labs publishes a full build and a CLI-only build of Commander, and
# they carry different executable names. Both answer the same 28 subcommands
# (verified), and only CLI subcommands are used here, so either will do.
# script/bootstrap_silabs flattens whichever it installed into tools/commander/.
# Ordered by preference; the first hit wins.
# The bundle paths are what a macOS install actually looks like: the archive
# ships Commander-cli.app (or Commander.app for the full build) and
# script/bootstrap_silabs keeps the bundle whole, because the binary inside
# resolves its dylibs through @executable_path/../Frameworks. bootstrap_silabs
# also drops a "commander" symlink next to it, so the first entry usually wins;
# the bundle paths cover a hand-made install that has no symlink.
COMMANDER_CANDIDATES = {
    "Linux": ("commander", "commander-cli"),
    "Darwin": ("commander", "commander-cli",
               "Commander-cli.app/Contents/MacOS/commander-cli",
               "Commander.app/Contents/MacOS/commander"),
    "Windows": ("commander.exe", "commander-cli.exe"),
}

# Fallback globs for when pyserial is unavailable. Windows has no usable glob
# for COM ports, which is why it relies on pyserial or an explicit -p.
SERIAL_GLOBS = {
    "Linux": ("/dev/ttyUSB*", "/dev/ttyACM*"),
    "Darwin": ("/dev/cu.usbserial*", "/dev/cu.usbmodem*", "/dev/cu.SLAB_USBtoUART*"),
    "Windows": (),
}

# The NWP/TA firmware to write. This is the image with measured evidence of
# booting on this hardware: a board came up with "Running TA fw:
# 1611.2.1.1.255.11.63" twice, on 2026-08-19 and again on 2026-08-20.
#
# An earlier revision pointed this at the SDK's SiWG917-B.*.rps because AN1435
# says a SiWx917 reports version prefix 1711 and this image reports 1611. That
# reasoning was wrong: "images named SiWG917-B.* report 1711" does not imply
# "an image reporting 1611 cannot run here". RS9117 is this silicon's
# pre-acquisition name and 1611 is an older firmware line for it. Writing 1711
# over a working 1611 stopped a board from booting.
TA_FIRMWARE_RELPATH = os.path.join("mcu", "patch", "RS9117_WC_SI.rps")

# SEGGER's USB vendor ID. A J-Link's own VCOM shows up as a serial port but is
# the board's console UART, not the ISP UART, on both boards this platform
# supports -- so it is kept out of the channel menu (an explicit -p still
# honours it, with a warning).
JLINK_USB_VID = 0x1366

SWD_PROBE_TIMEOUT = 20
MFG_INFO_TIMEOUT = 30
ADAPTER_LIST_TIMEOUT = 25

# RPS header layout. Offsets and the version field order were cross-checked
# against labels Silicon Labs wrote themselves: the SDK's own file names
# (connectivity_firmware/standard/SiWG917-B.2.15.5.0.0.2.rps decodes to
# 2.15.5.0.0.2, the lite image likewise) and what a running board reports.
#
# The word0 flag bits were derived by diffing images before and after
# `commander rps convert --combinedimage` (2026-08-20): the only bit that
# changed on either image was bit 7. Bit 0 separates the two M4 images built
# here (0x41) from the four NWP images the SDK ships (0x00).
RPS_HEADER_LEN = 64
RPS_MAGIC_OFFSET = 4          # uint16 LE, 0x900D
RPS_MAGIC = 0x900D
RPS_IMAGE_SIZE_OFFSET = 8     # uint32 LE, file length minus the header
RPS_VER_A_OFFSET = 0x0C       # build, security, minor, major
RPS_VER_B_OFFSET = 0x2C       # patch, customer, rom_id, chip_id
RPS_FLAG_M4 = 0x01            # set on M4 application images
RPS_FLAG_COMBINED = 0x80      # set on images eligible for / produced by combining

# Skip the prompts. SIWX917_FLASH answers the target menu, SIWX917_CHANNEL the
# channel menu.
FLASH_ACTION_ENV = "SIWX917_FLASH"
FLASH_ACTIONS = ("app", "ta", "both")
CHANNEL_ENV = "SIWX917_CHANNEL"
CHANNELS = ("swd", "serial")

MENU_RULE = "--------------------"

ISP_PROMPT = """
--------------------------------------------------------------------
 Serial (ISP) flashing -- this needs one manual step.

 The chip only listens on the ISP UART while it is held in ISP mode,
 and it does not stay there across commands. Right before the
 transfer starts:

   1. pull SWO / GPIO_34 low (ISP button, or a wire to GND)
   2. tap Reset

 Do not interrupt the transfer once it begins writing. A partial
 write can leave the device unbootable and recoverable only through
 a different procedure.
--------------------------------------------------------------------
"""

SWD_HINTS = """  SWD troubleshooting:
    - is the board powered?
    - SWCLK on GPIO_31, SWDIO on GPIO_33 (datasheet 6.3: JTAG_TCK_SWCLK is
      GPIO_31, JTAG_TMS_SWDIO is GPIO_33, JTAG_TDO_SWO is GPIO_34)
    - GPIO_34 low at reset selects ISP mode, and that does not take SWD away:
      datasheet 5.9 calls SWD a two-pin port and says TDO/SWO is the pin
      JTAG shares with SWV, so it is not one of the two. Datasheet 5.8.6 gives
      ISP's purpose as reprogramming "if the application code uses JTAG pins
      for functional use", i.e. ISP is the way in when JTAG is unavailable."""

# Printed after a TA write. A completed transfer is not a working radio, and
# saying so is the whole point -- "Platform flash success" would imply more.
#
# This used to be followed by twenty-odd lines on 16056/16059 and the ROM
# bootloader menu. That is a boot symptom, not a write outcome, and printing
# the recovery procedure for a symptom nobody had seen yet only buried the one
# step that mattered. GETTING_STARTED.md carries it; this points there.
TA_AFTER_WRITE = """  The transfer finished, which is not the same as a radio that starts.
  Power-cycle the board -- the install is finalised at reset, and a probe's
  reset does not always do it -- then check the application log for:
      WiFi initialization success
      Running TA fw: <version>
  If instead it reports WiFi initialization error 16056 or 16059, or
  m4_ta_secure_handshake: 0x7, see GETTING_STARTED.md, "Recovering a device
  whose radio will not start". Rewriting the image is the last thing to try
  there, not the first."""

# Printed when writing the NWP image fails over a link that has already proven
# itself. Ordered by cost, and each claim is measured: 2026-08-21, two writes
# over SWD reported this and the radio kept running both times.
#
# The tell is which line Commander stops on. A write that really did not land
# never reaches "Resetting"; one that did prints it and says DONE. Both TA
# failures stopped at the upgrade wait, while an M4 write over the same probe
# printed "Resetting" and succeeded -- so the probe is not the variable.
TA_WRITE_FAILED = """  The link worked and the data was sent. What timed out is the confirmation
  that the NWP finished installing it, which Commander reports as
  "Flashloader is not ready". That is not evidence of a damaged radio: the
  NWP takes the RAM-resident flash-loader down with it when it resets to
  finalise, so the confirmation has nothing left to answer it.

  Cheapest first:
    1. power-cycle the board -- pull the power, not the reset button
    2. read the version back, and compare it with what was printed above:
         commander mfg917 info -d {device} --json
    3. only if it is gone or changed does the ROM bootloader come into it.
       GETTING_STARTED.md, "Recovering a device whose radio will not start",
       has the menu and the order: 'K' to check, '5' to point the default at
       a slot, and only then 'B' to burn."""

SERIAL_HINTS = """  Serial/ISP troubleshooting:
    - chip must be in ISP mode: GPIO_34 low, then Reset
    - CH340 TX -> GPIO_8 (chip RX), CH340 RX -> GPIO_9 (chip TX)
    - GND must be common between adapter and board
    - a J-Link's VCOM is the console UART, not the ISP UART"""


# -----------------------------------------------------------------------------
#                              Internal helpers
# -----------------------------------------------------------------------------

def _platform_root() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def _find_commander(logger) -> str:
    """Locate the Simplicity Commander executable for this host OS."""
    host = platform.system()
    candidates = COMMANDER_CANDIDATES.get(host)
    if candidates is None:
        logger.error(f"Unsupported host OS for SiWx917 flashing: {host}")
        return ""

    base = os.path.join(_platform_root(), "tools", "commander")
    for rel in candidates:
        path = os.path.join(base, rel)
        if os.path.isfile(path) and (host == "Windows" or os.access(path, os.X_OK)):
            logger.debug(f"Using commander: {path}")
            return path

    logger.error(f"Simplicity Commander not found under {base}")
    logger.error("Run ./script/bootstrap_silabs in the platform directory to "
                 "install it.")
    return ""


def _device(using_data) -> str:
    return using_data.get("CONFIG_CHIP_CHOICE", "") or DEFAULT_DEVICE


def _artifact_paths(using_data, binfile: str) -> str:
    """
    Derive the M4 application image from the QIO bin path cli_flash.py hands us.

      binfile  <app>/.build/bin/<project>_QIO_<ver>.bin
      rps      <app>/.build/<project>/<project>.rps
    """
    bin_dir = os.path.dirname(binfile)
    build_dir = os.path.dirname(bin_dir)
    project = using_data.get("CONFIG_PROJECT_NAME", "")

    return os.path.join(build_dir, project, f"{project}.rps")


def _run(argv, logger) -> bool:
    """Run commander with its output going straight through to the terminal."""
    logger.debug("exec: " + " ".join(argv))
    try:
        return subprocess.run(argv).returncode == 0
    except OSError as e:
        logger.error(f"Failed to launch commander: {e}")
        return False


def _run_quiet(argv, timeout, logger):
    """Run commander and capture its output. Returns (ok, stdout)."""
    logger.debug("exec: " + " ".join(argv))
    try:
        done = subprocess.run(argv, capture_output=True, text=True,
                              timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        logger.debug(f"{argv[1] if len(argv) > 1 else argv[0]} failed: {e}")
        return False, ""
    return done.returncode == 0, done.stdout


def _check_image(image: str, logger) -> bool:
    if os.path.isfile(image):
        return True
    logger.error(f"Image not found: {image}")
    logger.error("Build first with [tos.py build].")
    return False


# -----------------------------------------------------------------------------
#                              RPS header decoding
# -----------------------------------------------------------------------------

def _rps_header(image: str, logger):
    """
    Decode the RPS header. Returns a dict, or None when it is not an RPS.

    Keys: flags, is_m4, combine_flag, is_combined, chip_id, rom_id, version.
    """
    try:
        size = os.path.getsize(image)
        with open(image, "rb") as f:
            head = f.read(RPS_HEADER_LEN)
    except OSError as e:
        logger.debug(f"Cannot read {image}: {e}")
        return None

    if len(head) < RPS_HEADER_LEN:
        logger.debug(f"{image} is shorter than an RPS header")
        return None

    magic = int.from_bytes(
        head[RPS_MAGIC_OFFSET:RPS_MAGIC_OFFSET + 2], "little")
    if magic != RPS_MAGIC:
        logger.debug(f"{image} is not an RPS (magic 0x{magic:04x}, "
                     f"expected 0x{RPS_MAGIC:04x})")
        return None

    flags = int.from_bytes(head[0:4], "little")
    image_size = int.from_bytes(
        head[RPS_IMAGE_SIZE_OFFSET:RPS_IMAGE_SIZE_OFFSET + 4], "little")

    # RPS_FLAG_COMBINED means "this image may take part in combining", not
    # "this file holds two images" -- `rps convert --combinedimage` sets it on
    # single images too, which is how the parts are prepared before merging.
    # Whether a file actually carries both is a question of size: image_size
    # describes only the first image, and a merged file has the second one
    # appended whole, header and all.
    #
    # Measured on a 2478720-byte merged file: image_size 810656 (the M4 part),
    # first image ends at 810720, and the remaining 1668000 bytes are exactly
    # the NWP file that went in, whose own header claims 1667936. Checking that
    # second claim is what catches a truncated merge.
    first_end = image_size + RPS_HEADER_LEN
    carries_two = False
    if size < first_end:
        logger.debug(f"{image} is truncated (header claims {image_size} bytes "
                     f"of payload, file is {size})")
        return None
    if size > first_end:
        if size < first_end + RPS_HEADER_LEN:
            logger.debug(f"{image} has {size - first_end} trailing bytes, too "
                         "few for a second RPS header")
            return None
        try:
            with open(image, "rb") as f:
                f.seek(first_end)
                second = f.read(RPS_HEADER_LEN)
        except OSError as e:
            logger.debug(f"Cannot read the second image in {image}: {e}")
            return None
        second_magic = int.from_bytes(
            second[RPS_MAGIC_OFFSET:RPS_MAGIC_OFFSET + 2], "little")
        second_size = int.from_bytes(
            second[RPS_IMAGE_SIZE_OFFSET:RPS_IMAGE_SIZE_OFFSET + 4], "little")
        if second_magic != RPS_MAGIC:
            logger.debug(f"{image} has data at offset {first_end} that is not "
                         f"an RPS (magic 0x{second_magic:04x})")
            return None
        if second_size != size - first_end - RPS_HEADER_LEN:
            logger.debug(f"{image} second image is truncated (claims "
                         f"{second_size}, has "
                         f"{size - first_end - RPS_HEADER_LEN})")
            return None
        carries_two = True
    build, security, minor, major = head[RPS_VER_A_OFFSET:RPS_VER_A_OFFSET + 4]
    patch, customer, rom_id, chip_id = \
        head[RPS_VER_B_OFFSET:RPS_VER_B_OFFSET + 4]

    return {
        "flags": flags,
        "is_m4": bool(flags & RPS_FLAG_M4),
        # combine_flag: allowed to be merged. is_combined: actually holds both.
        "combine_flag": bool(flags & RPS_FLAG_COMBINED),
        "is_combined": carries_two,
        "chip_id": chip_id,
        "rom_id": rom_id,
        # Same field order and formatting the running firmware prints at boot,
        # so the two can be compared by eye as well as by string.
        "version": (f"{chip_id:x}{rom_id:x}.{major}.{minor}.{security}"
                    f".{patch}.{customer}.{build}"),
    }


def _find_ta_image(logger, quiet=False) -> str:
    """
    The NWP/TA image shipped here, or "" when it is missing.

    quiet=True for the look before the menu, where a missing image is not yet
    an error -- it only becomes one if NWP firmware is then chosen, which the
    caller checks after the choice is made.
    """
    image = os.path.join(_platform_root(), TA_FIRMWARE_RELPATH)
    if not os.path.isfile(image):
        if not quiet:
            logger.error(f"No NWP/TA firmware at {TA_FIRMWARE_RELPATH}")
        return ""
    return image


# Why "TA + M4" writes two images instead of one combined image
# -------------------------------------------------------------
# Commander can merge them, and the merge itself works:
#
#   rps convert m4_c.rps  --app <m4>.rps   --combinedimage
#   rps convert nwp_c.rps --nwpapp <ta>.rps --combinedimage
#   rps convert combined.rps --app m4_c.rps --nwpapp nwp_c.rps
#
# Three steps because `rps convert` refuses inputs that do not already carry
# the COMBINED_IMAGE flag ("Image does not have the COMBINED_IMAGE flag set;
# cannot combine this image!"), and neither the image built here nor the one
# the SDK ships has it.
#
# But writing the result did not work. Measured 2026-08-20 on a DK2605A, with
# a 2478720-byte merged image that `rps convert` produced without complaint:
#
#   $ commander rps load combined.rps -d SiWG917M111MGTBA --tif SWD
#   Uploading flashloader...
#   Waiting for flashloader to become ready
#   Writing data...
#   ERROR: Waiting for flashloader failed: 0 - Flashloader is not ready
#
# The board was undamaged (radio still booted, version unchanged), so nothing
# was committed. Cause unknown -- possibly the size, possibly that the merged
# image presents an M4 header with the NWP image appended and the flash-loader
# takes the M4 path. The same command with the NWP image alone (1668000 bytes)
# succeeds and the radio comes up, so the channel is fine.
#
# Writing the two images one after another uses only operations that have each
# been verified on hardware, so that is what this does. If you want to revisit
# the merged path, the recipe above is the starting point.


# -----------------------------------------------------------------------------
#                          Channel detection and menus
# -----------------------------------------------------------------------------

# `commander adapter list` keys worth keeping, mapped to our own names.
_ADAPTER_FIELDS = {
    "adapterName": "name",
    "adapterDescription": "desc",
    "adapterNickname": "name",
    "AdapterType": "type",
}


def _jlink_adapters(commander, logger) -> list:
    """
    Debug probes Commander can see, as [{"serial", "label"}].

    Parsed from `commander adapter list`, which prints one key=value per line
    per adapter. adapterName/adapterDescription are what make the entry
    recognisable in a menu when more than one probe is attached.
    """
    ok, out = _run_quiet([commander, "adapter", "list"],
                         ADAPTER_LIST_TIMEOUT, logger)
    if not ok:
        return []

    adapters, cur = [], {}
    for raw in out.splitlines():
        line = raw.strip()
        # Silicon Labs adapters print key=value. A third-party J-Link prints
        # far less, and its firmware banner uses " is " instead of "=":
        #   FirmwareString is J-Link ARM V8 compiled Nov 28 2014 13:44:46
        if line.startswith("FirmwareString is "):
            if cur:
                cur["firmware"] = line[len("FirmwareString is "):].strip()
            continue
        key, sep, value = line.partition("=")
        if not sep:
            continue
        key, value = key.strip(), value.strip()
        if key == "serialNumber" and value:
            if cur.get("serial"):
                adapters.append(cur)
            cur = {"serial": value, "name": "", "desc": "", "type": "",
                   "firmware": ""}
        elif key in _ADAPTER_FIELDS and cur and value:
            cur[_ADAPTER_FIELDS[key]] = value
    if cur.get("serial"):
        adapters.append(cur)

    out_list = []
    for a in adapters:
        if a.get("type") and a["type"].lower() != "jlink":
            continue
        # A clone reports serialNumber=4294967295 (0xFFFFFFFF) and no name, so
        # printing the number alone tells the user nothing. Prefer whatever
        # identity is actually there, and fall back to the firmware banner.
        serial = "" if a["serial"] == "4294967295" else a["serial"]
        parts = [p for p in (serial, a["name"], a["desc"]) if p]
        if not parts and a["firmware"]:
            parts = [a["firmware"]]
        out_list.append({"serial": serial,
                         "label": " ".join(parts) or "unidentified probe"})
    return out_list


def _usb_serial_ports(skip_jlink_vcom: bool = True) -> list:
    """
    USB serial adapters, as [{"device", "label"}].

    pyserial lists every legacy /dev/ttyS* alongside the real adapters, and
    only USB devices carry a vendor ID, so that is the discriminator.

    A J-Link's own VCOM is dropped by default: on both boards this platform
    supports it is the console UART, not the ISP UART, so offering it as a
    flashing channel only invites a transfer that cannot work.
    """
    try:
        from serial.tools import list_ports
        found = []
        for p in sorted(list_ports.comports(), key=lambda x: x.device):
            if p.vid is None:
                continue
            if skip_jlink_vcom and p.vid == JLINK_USB_VID:
                continue
            label = " ".join(x for x in (p.product, p.manufacturer) if x)
            found.append({"device": p.device,
                          "label": label or f"{p.vid:04x}:{p.pid:04x}"})
        return found
    except ImportError:
        found = []
        for pattern in SERIAL_GLOBS.get(platform.system(), ()):
            for dev in sorted(glob.glob(pattern)):
                found.append({"device": dev, "label": ""})
        return found


def _ask(prompt: str, count: int, logger) -> int:
    """Read a 1-based menu choice. Returns 0 when the user gives up."""
    while True:
        try:
            raw = input(prompt)
        except (EOFError, KeyboardInterrupt):
            print("")
            return 0
        try:
            num = int(raw.strip())
        except ValueError:
            continue
        if 1 <= num <= count:
            return num


def _choose_channel(commander, port, logger):
    """
    Pick the flash channel. Returns (serial: bool, port: str) or (None, "").

    The menu is shown even when only one channel was detected: seeing what is
    attached before anything is written is worth one keypress, and it keeps
    this from special-casing the single-candidate path.

    A given -p is honoured as "serial/ISP on this one" -- unless it is the
    J-Link's own VCOM, which _usb_serial_ports() already refuses to put in the
    menu for the reason its docstring gives: on both boards this platform
    supports that port is the console UART, so nothing can be written over it.
    Short-circuiting onto a channel the menu will not even offer meant a
    caller that hands us a port list -- the IDE picks one and always passes
    -p -- could never reach SWD, and got a guaranteed failure instead. So a
    VCOM is warned about and dropped, and the menu decides.
    """
    forced = os.environ.get(CHANNEL_ENV, "").strip().lower()
    if forced:
        if forced not in CHANNELS:
            logger.error(f"{CHANNEL_ENV}={forced} is not one of "
                         f"{'/'.join(CHANNELS)}")
            return None, ""
        logger.info(f"{CHANNEL_ENV}={forced} -- honouring it as given.")
        if forced == "swd":
            return False, ""
        # The env names the channel, -p names the port within it. This pairing
        # is what the "Pass -p <port>" hint below asks for, and it was
        # unreachable while -p short-circuited above this block.
        if port:
            if _is_jlink_vcom(port):
                logger.warning(f"{port} is the J-Link's VCOM, not the ISP "
                               "UART; this will not work. Honouring it only "
                               f"because {CHANNEL_ENV}=serial and -p both "
                               "say so.")
            return True, port
        ports = _usb_serial_ports()
        if len(ports) != 1:
            logger.error("SIWX917_CHANNEL=serial needs exactly one USB serial "
                         f"adapter, found {len(ports)}. Pass -p <port>.")
            return None, ""
        return True, ports[0]["device"]

    if port and not _is_jlink_vcom(port):
        logger.info(f"Port given ({port}) -- using serial/ISP on it.")
        return True, port

    if port:
        logger.warning(f"{port} is the J-Link's VCOM, which is the console "
                       "UART on this board, not the ISP UART -- nothing can "
                       "be flashed over it. Ignoring it; the probe behind it "
                       "can flash over SWD, so pick a channel below.")

    entries = []
    for a in _jlink_adapters(commander, logger):
        entries.append(("swd", "", f"SWD / J-Link   {a['label']}"))
    for p in _usb_serial_ports():
        entries.append(("serial", p["device"],
                        f"serial / ISP   {p['device']}  {p['label']}".rstrip()))

    if not entries:
        logger.error("No flash channel detected: no debug probe and no USB "
                     "serial adapter.")
        logger.error(SWD_HINTS)
        logger.error(SERIAL_HINTS)
        return None, ""

    print(MENU_RULE)
    for i, (_, _, label) in enumerate(entries):
        print(f"{i + 1}. {label}")
    print(MENU_RULE)
    num = _ask("Select flash channel: ", len(entries), logger)
    if not num:
        return None, ""

    kind, dev, _ = entries[num - 1]
    return kind == "serial", dev


def _is_jlink_vcom(port: str) -> bool:
    try:
        from serial.tools import list_ports
    except ImportError:
        return False
    for p in list_ports.comports():
        if p.device == port:
            return p.vid == JLINK_USB_VID
    return False


def _ta_version_note(ta_image, answered, device_ver, logger) -> str:
    """
    What the menu should say about writing the NWP image, from the two versions
    already in hand: the device's and the file's.

    Both were being read and then only logged, one line apart, with nothing
    comparing them. Writing an image the device already runs erases a working
    radio to put the same bytes back, and if that write then reports a timeout
    it costs a recovery scare on top -- which is what happened twice on
    2026-08-21, both sides reading 1611.2.1.1.255.11.63.

    This annotates; it does not block. Rewriting the same version is sometimes
    exactly the repair (a corrupt image reports the same version as a good one
    -- see GETTING_STARTED.md), so the choice stays with whoever is asking.
    """
    if not ta_image:
        return "No NWP image is shipped here, so 2 and 3 have nothing to write."

    info = _rps_header(ta_image, logger)
    if not info or info["is_m4"]:
        return ""

    file_ver = info["version"]
    if not file_ver:
        return ""
    if not answered or _version_is_blank(device_ver):
        return f"The image on disk is {file_ver}; the device reported no version."
    if file_ver == device_ver:
        return (f"The device already runs {file_ver}: 2 and 3 write the same "
                "bytes back.")
    return f"2 and 3 would replace {device_ver} with {file_ver}."


def _choose_action(logger, ta_note="") -> str:
    """
    Pick what to write. Returns one of FLASH_ACTIONS, or "" to abort.

    Writing NWP/TA firmware erases the radio first, so this is never inferred
    from device state: it is asked, or given outright via SIWX917_FLASH.
    ta_note, when given, is printed just above the prompt.
    """
    forced = os.environ.get(FLASH_ACTION_ENV, "").strip().lower()
    # SIWX917_FLASH_TA=1 predates this and meant "write the TA firmware instead
    # of the application". Keep it working as a name for that.
    if not forced and os.environ.get("SIWX917_FLASH_TA") == "1":
        forced = "ta"
    if forced:
        if forced not in FLASH_ACTIONS:
            logger.error(f"{FLASH_ACTION_ENV}={forced} is not one of "
                         f"{'/'.join(FLASH_ACTIONS)}")
            return ""
        logger.info(f"{FLASH_ACTION_ENV}={forced} -- honouring it as given.")
        return forced

    options = (
        ("app", "M4 ONLY    application"),
        ("ta", "TA ONLY    NWP wireless firmware (erases the radio's flash)"),
        ("both", "TA + M4    NWP firmware, then the application"),
    )
    print(MENU_RULE)
    for i, (_, label) in enumerate(options):
        print(f"{i + 1}. {label}")
    print(MENU_RULE)
    # After the rule, not appended to the entries: the NWP label already runs
    # to 58 columns and hanging a version pair off it wraps on a narrow
    # terminal. Last thing printed before the prompt is also the thing read.
    if ta_note:
        print(ta_note)
    num = _ask("Select what to flash: ", len(options), logger)
    if not num:
        return ""
    return options[num - 1][0]


# -----------------------------------------------------------------------------
#                              NWP/TA inspection
# -----------------------------------------------------------------------------

def _version_is_blank(version: str) -> bool:
    """A version of nothing but zeros and separators means no firmware."""
    return not [c for c in version if c not in "0.: \t"]


def _device_nwp_version(commander, device, serial, port, logger):
    """
    Ask the device what NWP/TA firmware it runs.

    return: (answered, version)

    answered says the device replied, whatever it replied. That is the
    distinction the caller needs: "this board reports no NWP firmware" is a
    fact to act on, while "nothing could be asked" -- no probe, a serial
    channel that does not carry this command, a Commander too old to report it
    -- is not, and must never be mistaken for the first.
    """
    argv = [commander, "mfg917", "info", "-d", device, "--json"]
    if serial and port:
        argv += ["--serialinterface", "--serialport", port]

    ok, out = _run_quiet(argv, MFG_INFO_TIMEOUT, logger)
    if not ok:
        return False, ""

    try:
        info = json.loads(out)["result"]["mfg917_info"]
    except (ValueError, KeyError, TypeError) as e:
        logger.debug(f"Cannot parse mfg917 info output: {e}")
        return False, ""

    version = str(info.get("nwp_firmware_version", "")).strip()
    return True, "" if _version_is_blank(version) else version


# -----------------------------------------------------------------------------
#                                  Writing
# -----------------------------------------------------------------------------

def _load(commander, image, device, serial, port, fixedspeed, logger,
          link_proven=False) -> bool:
    """
    Write one .rps, over whichever channel was chosen.

    Both commands route the image by the type in its RPS header, so the same
    file works for either the M4 application or the NWP firmware.

    link_proven says the channel has already carried a reply from the device,
    which decides whether the wiring hints are worth printing on failure.
    """
    if not _check_image(image, logger):
        return False
    if os.path.splitext(image)[1].lower() != ".rps":
        logger.error(f"Flashing needs a .rps image, got: {image}")
        return False

    info = _rps_header(image, logger)
    if info:
        what = ("combined M4+NWP" if info["is_combined"]
                else "M4 application" if info["is_m4"] else "NWP firmware")
        logger.info(f"Writing {os.path.basename(image)} -- {what}"
                    + (f", version {info['version']}" if not info["is_m4"] else ""))

    if serial:
        logger.info(ISP_PROMPT)
        argv = [commander, "serial", "load", image,
                "--serialport", port, "--showprogress", "-d", device]
        if fixedspeed:
            argv.append("--fixedspeed")
        hints = SERIAL_HINTS
    else:
        argv = [commander, "rps", "load", image, "-d", device, "--tif", "SWD"]
        hints = SWD_HINTS

    if _run(argv, logger):
        return True
    # Wiring and power hints help only when the channel never came up. Once it
    # has -- mfg917 info answered over this same link, the flash-loader
    # uploaded, the data went out -- "is the board powered? is SWCLK on
    # GPIO_31?" is false and sends the reader the wrong way. It did exactly
    # that on 2026-08-21, after a link that had just carried 1.6 MB.
    if not link_proven:
        logger.error(hints)
    return False


# -----------------------------------------------------------------------------
#                                 Entry point
# -----------------------------------------------------------------------------

def platform_flash(using_data=None,
                   binfile="",
                   port="",
                   baud=0,
                   boards_root="",
                   logger=None,
                   **kwargs) -> dict:
    """
    Flash a SiWx917 target. Returns {"success": bool, "message": str}.

    Asks which channel (SWD/J-Link or serial/ISP) and what to write (M4 only,
    TA only, or both). -p <port> forces serial on that port;
    SIWX917_CHANNEL=swd|serial and SIWX917_FLASH=app|ta|both skip the prompts.
    """
    using_data = using_data or {}

    commander = _find_commander(logger)
    if not commander:
        return {"success": False, "message": "Simplicity Commander not available"}

    device = _device(using_data)
    m4_image = _artifact_paths(using_data, binfile)

    # commander negotiates 921600 baud before transferring and aborts the whole
    # load when that handshake fails -- which is what a CH340-class adapter
    # typically does. --fixedspeed skips the handshake and transfers at the
    # default rate, so it is the default here: a slower flash beats one that
    # never completes. Set SIWX917_HIGHSPEED=1 for an adapter that can take it.
    fixedspeed = os.environ.get("SIWX917_HIGHSPEED") != "1"
    if baud:
        logger.info(f"-b {baud} is not used: commander offers only its own "
                    "negotiated rate or --fixedspeed, not an arbitrary value.")

    serial, chosen = _choose_channel(commander, port, logger)
    if serial is None:
        return {"success": False, "message": "no flash channel selected"}

    answered, device_ver = _device_nwp_version(
        commander, device, serial, chosen, logger)
    if answered:
        logger.info(f"Device NWP/TA firmware: {device_ver or 'none reported'}")

    # Locate and read the NWP image before the menu, so the entries that would
    # write it can say whether that changes anything. Quiet: a missing image is
    # only an error once NWP firmware is actually chosen, below.
    ta_image = _find_ta_image(logger, quiet=True)
    action = _choose_action(
        logger, _ta_version_note(ta_image, answered, device_ver, logger))
    if not action:
        return {"success": False, "message": "flash cancelled"}

    if action in ("ta", "both") and not ta_image:
        _find_ta_image(logger)          # now it is an error, so say so
        return {"success": False, "message": "no usable NWP/TA image"}

    if action == "app":
        ok = _load(commander, m4_image, device, serial, chosen, fixedspeed,
                   logger, answered)
        return {"success": ok, "message": "" if ok else "M4 flash failed"}

    if action == "ta":
        ok = _load(commander, ta_image, device, serial, chosen, fixedspeed,
                   logger, answered)
        if ok:
            logger.warning(TA_AFTER_WRITE)
        elif answered:
            logger.error(TA_WRITE_FAILED.format(device=device))
        return {"success": ok, "message": "" if ok else "TA flash failed"}

    # both: NWP first, then the application. Not a single merged image -- see
    # the comment above _find_ta_image's neighbours for what was tried.
    #
    # NWP first so a failure stops before the application is touched: a board
    # with a working application and a broken radio can still be reached over
    # SWD and told what happened, which is the more recoverable of the two
    # half-finished states.
    if not _check_image(m4_image, logger):
        return {"success": False, "message": "M4 image not built"}

    for image, what in ((ta_image, "NWP"), (m4_image, "M4")):
        logger.info(f"[{what}] {os.path.basename(image)}")
        if not _load(commander, image, device, serial, chosen, fixedspeed,
                     logger, answered):
            if what == "NWP":
                if answered:
                    logger.error(TA_WRITE_FAILED.format(device=device))
            else:
                logger.error("The NWP firmware was written but the application "
                             "was not. Re-run and pick M4 ONLY.")
            return {"success": False, "message": f"{what} flash failed"}
    logger.warning(TA_AFTER_WRITE)
    return {"success": True, "message": ""}
