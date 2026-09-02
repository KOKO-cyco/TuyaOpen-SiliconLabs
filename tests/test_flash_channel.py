#!/usr/bin/env python3
"""
Exercise the real _choose_channel() in platform_flash_bridge.py.

No board and no pyserial: the serial layer is a fake module injected into
sys.modules (the bridge imports `serial.tools.list_ports` inside each function,
so this works), and the probe list is monkeypatched over _jlink_adapters, whose
real implementation shells out to Commander.

What this locks, and why it exists:

  _usb_serial_ports() has always refused to put the J-Link's own VCOM in the
  channel menu -- on both boards this platform supports it is the console UART,
  not the ISP UART, so nothing can be written over it. But `-p <that port>`
  used to short-circuit straight onto it anyway, one branch above. The two
  halves of the same file disagreed.

  That is invisible from a hand-typed CLI, where nobody passes a port they know
  cannot work. It is fatal to a caller that always passes one: the IDE lists
  the ports it can see, the user picks the only thing attached -- the VCOM --
  and SWD becomes unreachable, with a guaranteed-to-fail serial write in its
  place. Reinstating `return True, port` for a VCOM fails 4 of the checks
  below; reinstating the old env-below-port ordering fails a 5th. Both were
  measured by reverting each half on its own against this file.
"""

import importlib.util
import logging
import os
import sys
import types
import builtins

HERE = os.path.dirname(os.path.abspath(__file__))
PLAT = os.path.dirname(HERE)
BRIDGE = os.path.join(PLAT, "platform_flash_bridge.py")

JLINK_VID = 0x1366

VCOM = types.SimpleNamespace(device="/dev/ttyACM0", vid=JLINK_VID, pid=0x0105,
                             product="J-Link", manufacturer="SEGGER")
ISP = types.SimpleNamespace(device="/dev/ttyUSB0", vid=0x10C4, pid=0xEA60,
                            product="CP2102 USB to UART",
                            manufacturer="Silicon Labs")
# No vid: pyserial reports every legacy /dev/ttyS* this way and the bridge
# drops them. Present so the "exactly one adapter" branch is really counting
# adapters and not comports().
LEGACY = types.SimpleNamespace(device="/dev/ttyS0", vid=None, pid=None,
                               product=None, manufacturer=None)

failures = []
checks = 0


def expect(cond, what):
    global checks
    checks += 1
    if cond:
        print(f"  ok   {what}")
    else:
        print(f"  FAIL {what}")
        failures.append(what)


def _load(ports):
    """Import a fresh copy of the bridge over a fake pyserial listing `ports`."""
    lp = types.ModuleType("serial.tools.list_ports")
    lp.comports = lambda: list(ports)
    tools = types.ModuleType("serial.tools")
    tools.list_ports = lp
    serial = types.ModuleType("serial")
    serial.tools = tools
    sys.modules["serial"] = serial
    sys.modules["serial.tools"] = tools
    sys.modules["serial.tools.list_ports"] = lp

    spec = importlib.util.spec_from_file_location("bridge_under_test", BRIDGE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def choose(ports, port, env=None, probes=("J-Link DK2605A",), answer="1"):
    """
    Run _choose_channel; returns ((serial, port), menu_was_shown).

    `answer` is fed to the menu once and then the input stream ends. _ask()
    re-prompts on an out-of-range number, so a fake that keeps returning the
    same one spins forever -- the EOF is what lets a deliberately impossible
    choice terminate as "gave up" instead of hanging the suite.
    """
    os.environ.pop("SIWX917_CHANNEL", None)
    if env is not None:
        os.environ["SIWX917_CHANNEL"] = env

    mod = _load(ports)
    mod._jlink_adapters = lambda commander, logger: [
        {"serial": "440000000", "label": label} for label in probes
    ]

    asked = []
    replies = iter([answer])

    def fake_input(prompt=""):
        asked.append(prompt)
        try:
            return next(replies)
        except StopIteration:
            raise EOFError

    real_input = builtins.input
    builtins.input = fake_input

    log = logging.getLogger("test_flash_channel")
    log.addHandler(logging.NullHandler())
    log.setLevel(logging.CRITICAL)

    try:
        got = mod._choose_channel("commander", port, log)
    finally:
        builtins.input = real_input
        os.environ.pop("SIWX917_CHANNEL", None)
    return got, bool(asked)


def main():
    print("_choose_channel: a J-Link VCOM is not a channel choice")
    got, menu = choose([VCOM], "/dev/ttyACM0")
    expect(got == (False, ""), "-p <VCOM> falls through to the menu, which picks SWD")
    expect(menu, "-p <VCOM> does not silently decide; the menu is shown")

    got, menu = choose([VCOM, ISP], "/dev/ttyACM0", probes=(), answer="1")
    expect(got == (True, "/dev/ttyUSB0"),
           "-p <VCOM> with no probe still reaches the real ISP adapter")

    got, _ = choose([VCOM], "/dev/ttyACM0", probes=())
    expect(got == (None, ""),
           "-p <VCOM> with nothing else attached reports no channel, not a doomed write")

    print("\n_choose_channel: an explicit ISP port is still an override")
    got, menu = choose([ISP, VCOM], "/dev/ttyUSB0")
    expect(got == (True, "/dev/ttyUSB0"), "-p <real ISP port> is honoured")
    expect(not menu, "-p <real ISP port> shows no menu -- CLI behaviour is unchanged")

    print("\n_choose_channel: no port given")
    got, menu = choose([ISP, VCOM], "", answer="1")
    expect(got == (False, "") and menu, "no -p lists SWD first")
    got, _ = choose([ISP, VCOM], "", answer="2")
    expect(got == (True, "/dev/ttyUSB0"), "no -p lists the ISP adapter second")
    got, _ = choose([ISP, VCOM], "", answer="3")
    expect(got == (None, ""), "the J-Link VCOM is never offered as an entry")

    print("\n_choose_channel: SIWX917_CHANNEL outranks -p")
    got, _ = choose([ISP], "/dev/ttyUSB0", env="swd")
    expect(got == (False, ""), "SIWX917_CHANNEL=swd wins over a given -p")
    got, _ = choose([ISP, VCOM], "/dev/ttyUSB0", env="serial")
    expect(got == (True, "/dev/ttyUSB0"),
           "SIWX917_CHANNEL=serial names the channel, -p names the port")
    got, _ = choose([ISP, LEGACY], "", env="serial")
    expect(got == (True, "/dev/ttyUSB0"),
           "SIWX917_CHANNEL=serial with one adapter needs no -p")
    got, _ = choose([ISP], "", env="bogus")
    expect(got == (None, ""), "an unknown SIWX917_CHANNEL is refused, not ignored")

    print(f"\n{checks - len(failures)}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
