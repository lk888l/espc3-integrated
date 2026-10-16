#!/usr/bin/env python3
"""Exercise desktop pets, transactional upload, persistence and input on real USB hardware."""
import argparse
import json
from pathlib import Path
import re
import struct
import time
import zlib

from serial_smoke import Device


def fixture():
    """Maximum-size pack: three distinct OLED frames for every action."""
    header = bytearray(64)
    header[:8] = b"PET1\x40\x20\x08\x00"
    header[8:20] = b"Smoke Pet v1"
    header[24:32] = bytes([3] * 8)
    struct.pack_into("<8H", header, 32, *([160] * 8))
    pixels = bytearray()
    for action in range(8):
        for phase in range(3):
            frame = bytearray(256)
            for y in range(32):
                for x in range(64):
                    eyes = (14 <= x < 23 or 41 <= x < 50) and 9 + phase <= y < 23 - phase
                    smile = 25 <= x < 39 and y == 25 + action % 3
                    if eyes or smile:
                        frame[y * 8 + x // 8] |= 0x80 >> (x % 8)
            pixels.extend(frame)
    return bytes(header + pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, default=Path("build/hardware"))
    args = parser.parse_args()
    device = Device(args.port, args.output / "pet-smoke.log")
    result = {"checks": []}

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    def command(text, ok=True):
        reply = device.command(text)
        assert ("pet_ok " if ok else "pet_error ") in reply, reply
        return reply

    def state():
        reply = command("pet status")
        return dict(re.findall(r"(\w+)=([\w]+)", reply))

    def send_pack(data, crc=None):
        checksum = zlib.crc32(data) if crc is None else crc
        command(f"pet begin {len(data)} {checksum:08x}")
        for offset in range(0, len(data), 64):
            reply = command(f"pet chunk {offset} {data[offset:offset + 64].hex()}")
            assert f"next={min(offset + 64, len(data))}" in reply

    try:
        initial = device.status()
        assert initial["spi_errors"] == 0 and initial["heap"] > 50000, initial
        for style in range(4):
            command(f"pet open {style}")
            assert state()["style"] == str(style) and state()["active"] == "1"
            for action in ("idle", "blink", "happy", "eat", "sleep", "surprised", "sad", "dance"):
                command("pet action " + action)
                assert state()["action"] == action
        passed("four built-in pets and all eight actions")

        command("pet open 0")
        device.key("enter")
        device.key("enter", "double")
        assert state()["action"] == "eat"
        device.key("enter")
        device.key("enter", "long")
        assert state()["action"] == "sleep"
        device.key("left")
        assert state()["action"] != "sleep"
        device.key("down")
        assert state()["style"] == "1"
        device.key("back")
        assert device.status()["ui"] == 0
        device.open_home_card(5)
        assert state()["active"] == "1"
        passed("physical input routing, wake, style switch, return and menu entry")

        command("pet open 9", False)
        command("pet action invalid", False)
        command("pet begin 999999 00000000", False)
        command("pet begin 320 00000000")
        command("pet chunk 1 aa", False)
        command("pet chunk 0 zz", False)
        command("pet commit", False)
        command("pet abort")
        passed("malformed and incomplete uploads rejected")

        data = fixture()
        send_pack(data, zlib.crc32(data) ^ 1)
        command("pet commit", False)
        command("pet abort")
        malformed = bytearray(data)
        malformed[48] = 1
        send_pack(malformed)
        command("pet commit", False)
        command("pet abort")
        passed("CRC and pack schema validation")

        for _ in range(2):
            send_pack(data)
            command("pet commit")
        assert state()["style"] == "4" and state()["bytes"] == str(len(data))
        for action in ("idle", "blink", "happy", "eat", "sleep", "surprised", "sad", "dance"):
            command("pet action " + action)
            assert state()["action"] == action
        passed("maximum-size custom pet upload, replacement and all actions")

        time.sleep(1)
        after_reset = device.reset()
        assert state()["style"] == "4" and state()["bytes"] == str(len(data))
        command("pet open 4")
        time.sleep(1)
        assert state()["active"] == "1"
        passed("custom pet and selection survive hardware reset")

        command("pet open 1")
        command("pet action happy")
        final = device.status()
        assert final["spi_errors"] == 0 and final["heap"] > 40000, final
        assert final["ui_stack_free"] > 1000, final
        assert final["drops"] == after_reset["drops"], final
        result.update(initial=initial, final=final, custom_bytes=len(data), crc=f"{zlib.crc32(data):08x}")
        passed("OLED transfers, heap, stack and input queue health")
        (args.output / "pet-smoke.json").write_text(json.dumps(result, indent=2) + "\n")
    finally:
        device.close()


if __name__ == "__main__":
    main()
