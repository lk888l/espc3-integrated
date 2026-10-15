#!/usr/bin/env python3
"""USB integration smoke test; use ESP-IDF's Python environment."""
import argparse
import json
from pathlib import Path
import re
import time

import serial
from esptool.reset import HardReset


class MonitorSerial(serial.Serial):
    """Preserve USB modem lines on open; enable control only for explicit reset."""
    allow_modem_control = False

    def _update_dtr_state(self):
        if self.allow_modem_control:
            super()._update_dtr_state()

    def _update_rts_state(self):
        if self.allow_modem_control:
            super()._update_rts_state()


class Device:
    def __init__(self, port, log):
        self.port = MonitorSerial()
        self.port.port = port
        self.port.baudrate = 115200
        self.port.timeout = 0.05
        self.port.dtr = self.port.rts = False
        self.port.exclusive = True
        self.port.open()
        log.parent.mkdir(parents=True, exist_ok=True)
        self.log = log.open("w", encoding="utf-8")
        self.capture(1.0)

    def close(self):
        self.port.close()
        self.log.close()

    def record(self, data):
        text = data.decode("utf-8", errors="replace")
        text = re.sub(r"(KEY:\s*)\S+", r"\1<redacted>", text)
        self.log.write(text)
        self.log.flush()
        for bad in ("Guru Meditation", "Stack canary", "assert failed", "Brownout"):
            assert bad not in text, text
        return data.decode("utf-8", errors="replace")

    def capture(self, seconds):
        end = time.monotonic() + seconds
        data = bytearray()
        while time.monotonic() < end:
            data.extend(self.port.read(min(max(self.port.in_waiting, 1), 4096)))
        return self.record(data)

    def command(self, command):
        self.port.write((command + "\r\n").encode())
        self.port.flush()
        end = time.monotonic() + 3
        data = bytearray()
        while time.monotonic() < end:
            data.extend(self.port.read(min(max(self.port.in_waiting, 1), 4096)))
            echo = bytes(data).find((command + "\r\n").encode())
            prompt = bytes(data).rfind(b"c3> ")
            if echo >= 0 and prompt >= echo + len(command) + 2:
                break
        text = self.record(data)
        assert "c3> " in text, f"Console timeout for {command}: {text}"
        assert "command failed" not in text, text
        return text

    def status(self):
        text = self.command("status")
        pairs = re.findall(r'(\w+)=("[^"]*"|\S+)', text)
        values = {key: int(value) if value.isdecimal() else value.strip('"') for key, value in pairs}
        assert "ui" in values and "heap" in values and "dap" in values, text
        return values

    def key(self, key, gesture=None):
        self.command("key " + key + (" " + gesture if gesture else ""))
        time.sleep(0.012)

    def reset(self):
        self.port.allow_modem_control = True
        try:
            HardReset(self.port, uses_usb=True)()
        finally:
            self.port.allow_modem_control = False
        self.capture(1.4)
        return self.status()

    def home(self):
        for _ in range(4):
            if self.status()["ui"] == 0:
                return
            self.key("back")
        raise AssertionError("Could not return to Home")

    def open_home_card(self, index):
        self.home()
        state = self.status()
        for _ in range((index - state["selected"]) % 5):
            self.key("down")
        self.key("enter")

    def settings(self):
        self.open_home_card(4)
        state = self.status()
        assert state["ui"] == 4, state
        for _ in range(state["selected"]):
            self.key("up")

    def apply_settings(self, target):
        self.settings()
        for row, (name, period) in enumerate(
                (("motion", 3), ("brightness", 4), ("header", 4))):
            if row:
                self.key("down")
            state = self.status()
            for _ in range((target[name] - state[name]) % period):
                self.key("enter")
        time.sleep(1)
        state = self.status()
        assert all(state[k] == v for k, v in target.items()), state

    def ap_identity(self):
        match = re.search(r"AP: (\S+)\s+KEY: (\S+)\s+IP:", self.command("wifi"))
        assert match, "Missing AP identity"
        return match.groups()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, default=Path("build/hardware"))
    args = parser.parse_args()
    device = Device(args.port, args.output / "serial-smoke.log")
    original = None
    result = {"checks": []}
    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    try:
        initial = device.status()
        assert not initial["client"], "Close the active DAP client before running this test"
        assert initial["heap"] > 50000 and initial["spi_errors"] == 0, initial
        original = {key: initial[key] for key in ("motion", "brightness", "header")}
        identity = device.ap_identity()
        passed("USB console and runtime health")

        device.open_home_card(1)
        assert device.status()["ui"] == 1
        device.key("down")
        device.key("enter")
        assert device.status()["ui"] == 6
        device.key("enter")
        assert device.status()["countdown_running"] == 1
        time.sleep(1.05)
        state = device.status()
        assert state["countdown_seconds"] <= 299, state
        device.key("enter")
        assert not device.status()["countdown_running"]
        device.key("back")
        device.key("up")
        device.key("enter")
        assert device.status()["ui"] == 5
        device.key("enter")
        time.sleep(1.05)
        device.key("enter")
        state = device.status()
        assert state["stopwatch_ms"] >= 1000 and not state["stopwatch_running"], state
        time.sleep(0.2)
        assert device.status()["stopwatch_ms"] == state["stopwatch_ms"]
        device.key("up")
        assert device.status()["stopwatch_ms"] == 0
        passed("Rapid page navigation and both timers")

        for card, view in ((0, 11), (2, 2), (3, 3), (4, 4)):
            device.open_home_card(card)
            assert device.status()["ui"] == view
        device.home()
        before = device.status()["events"]
        for _ in range(40):
            device.key("down")
        assert device.status()["events"] == before + 40
        passed("All five home cards and 40 presses during animation")

        changed = {"motion": (original["motion"] + 1) % 3,
                   "brightness": (original["brightness"] + 1) % 4,
                   "header": (original["header"] + 1) % 4}
        device.apply_settings(changed)
        rebooted = device.reset()
        assert all(rebooted[key] == value for key, value in changed.items()), rebooted
        assert device.ap_identity() == identity
        device.apply_settings(original)
        rebooted = device.reset()
        assert all(rebooted[key] == value for key, value in original.items()), rebooted
        assert device.ap_identity() == identity
        passed("NVS settings and AP password survive reboot; original settings restored")

        device.command("dap on")
        time.sleep(0.25)
        state = device.status()
        assert state["dap"] == 1 and state["ready"] == 1 and not state["swd"], state
        device.command("dap clock 250000")
        assert device.status()["limit"] == 250000
        device.command("wifi off")
        time.sleep(0.25)
        state = device.status()
        assert not state["wifi"] and not state["dap"] and not state["ready"], state
        device.command("wifi on")
        time.sleep(0.4)
        assert device.status()["wifi"] == 1
        device.command("dap clock 100000")
        device.command("dap off")
        passed("DAP listen/stop and Wi-Fi off/on lifecycle")

        result["final"] = device.status()
        assert result["final"]["spi_errors"] == 0 and result["final"]["drops"] == 0
        assert result["final"]["heap"] > 50000
        passed("No SPI errors or input queue drops")
        result["ok"] = True
    finally:
        if original:
            device.apply_settings(original)
            device.home()
            device.command("dap clock 100000")
            device.command("dap off")
        device.close()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "serial-smoke.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
