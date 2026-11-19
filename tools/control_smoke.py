#!/usr/bin/env python3
"""Exercise the actual GameBox API over Wi-Fi, checking the UI independently over USB."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import secrets
import time

from gamebox_client import ApiError, GameBox, discover
from serial_smoke import Device


def expect_error(status, reason, call):
    try:
        call()
    except ApiError as error:
        assert (error.status, error.reason) == (status, reason), str(error)
    else:
        raise AssertionError("Expected " + reason)


def pair_code(usb):
    match = re.search(r"PAIR CODE:\s*(\d{6})", usb.command("wifi pair"))
    assert match, "Missing USB pairing code"
    return match[1]


def wait_station(usb, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        match = re.search(r"STA:\s*([0-9.]+)", usb.command("wifi"))
        if match and match[1] != "0.0.0.0":
            return match[1]
        time.sleep(0.6)
    raise AssertionError("Station did not obtain an IP")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--host", help="Default is the current STA IP from USB")
    parser.add_argument("--exercise-recovery", action="store_true",
                        help="Temporarily disconnect STA, test failed provisioning, then reboot")
    parser.add_argument("--output", type=Path, default=Path("build/hardware/local-api"))
    args = parser.parse_args()
    usb = Device(args.port, args.output / "usb.log")
    original = None
    clients = []
    checks = []

    def passed(name):
        checks.append(name)
        print("PASS", name, flush=True)

    try:
        baseline = usb.status()
        assert baseline["ui"] in (0, 20) and not baseline["client"], "Run from Home or Desktop Pet"
        host = args.host or wait_station(usb, 15)
        first, second = GameBox(host), GameBox(host)
        clients.extend([first, second])
        info = first.request("info")
        assert discover(host, 0.5).get(host) == info["id"]
        expect_error(401, "pair_required", lambda: first.request("status"))
        code = pair_code(usb)
        first.pair(code)
        expect_error(401, "invalid_code", lambda: second.pair(code))
        second.pair(pair_code(usb))
        original = first.request("status")
        passed("UDP discovery, two paired sessions and one-use code")

        first.request("pet", "POST", {"close": True})
        expect_error(409, "pet_inactive", lambda: first.request("pet", "POST", {"action": "dance"}))
        expect_error(409, "frame_unavailable", lambda: first.request("pet/frame"))
        expect_error(400, "invalid_pet", lambda: first.request("pet", "POST", {"style": 5}))
        actions = "idle blink happy eat sleep surprised sad dance".split()
        shapes = {}
        count = 4 + int(original["pet"]["custom"])
        with ThreadPoolExecutor(max_workers=1) as pool:
            for style in range(count):
                for action in actions:
                    # A blink can finish before its HTTP response arrives. Observe it over
                    # USB while the request is in flight and match the exact UI sequence.
                    job = pool.submit(first.request, "pet", "POST", {"style": style, "action": action})
                    observed = set()
                    end = time.monotonic() + 6
                    while time.monotonic() < end:
                        reply = usb.command("pet status")
                        match = re.search(r"active=1 style=(\d+) action=(\w+) sequence=(\d+)", reply)
                        if match:
                            observed.add((int(match[1]), match[2], int(match[3])))
                        if job.done():
                            state = job.result()["pet"]
                            assert state["active"] and (state["style"], state["action"]) == (style, action)
                            if (style, action, state["sequence"]) in observed:
                                break
                        time.sleep(0.01)
                    else:
                        raise AssertionError(f"USB did not observe {style}/{action}: {observed}")
                frame = second.request("pet/frame")
                shapes[style] = hashlib.sha256(frame).hexdigest()
        assert len({shapes[s] for s in range(4)}) == 4
        passed(f"{count} shapes × 8 actions confirmed by USB; distinct built-in OLED frames")

        changed = {"brightness": (original["display"]["brightness"] + 1) % 4,
                   "motion": (original["display"]["motion"] + 1) % 3}
        with ThreadPoolExecutor(max_workers=3) as pool:
            tasks = [pool.submit(client.request, "display", "POST", {field: value})
                     for client, (field, value) in zip(clients, changed.items())]
            tasks.append(pool.submit(usb.command, "pet open 2"))
            for task in tasks:
                task.result()
        state = first.request("status")
        assert state["pet"]["style"] == 2 and all(state["display"][k] == v for k, v in changed.items())
        assert all(usb.status()[k] == v for k, v in changed.items())
        second.logout()
        expect_error(401, "pair_required", lambda: second.request("status"))
        assert first.request("status")["pet"]["active"]
        passed("Concurrent USB / HTTP commands, partial display settings and independent logout")

        if args.exercise_recovery:
            assert original["wifi"]["connected"], "The recovery test requires an existing STA connection"
            result = first.request("wifi", "POST", {"reconnect": True})
            assert result["provision"] == "trying"
            time.sleep(1.5)
            restored_ip = wait_station(usb, 30)
            if not args.host:
                first.host = restored_ip
            state = first.request("status")
            assert state["wifi"]["connected"] and state["wifi"]["provision"] == "saved", state["wifi"]
            passed("Verified association, DHCP and NVS success using the configured network")
            start = time.monotonic()
            fake_ssid = "C3-test-" + secrets.token_hex(8)
            result = first.request("wifi", "POST", {"ssid": fake_ssid, "password": ""})
            assert result["provision"] == "trying"
            time.sleep(2)
            assert usb.status()["sta"] == "0.0.0.0"
            restored_ip = wait_station(usb, 70)
            if not args.host:
                first.host = restored_ip
            state = first.request("status")
            assert state["wifi"]["connected"] and state["wifi"]["provision"] == "failed", state["wifi"]
            passed(f"Failed station trial automatically restored the saved link in {time.monotonic()-start:.1f}s")
            # Keep the changed display settings long enough for NVS, then verify them across reset.
            usb.reset()
            restored_ip = wait_station(usb, 25)
            if not args.host:
                first.host = restored_ip
            expect_error(401, "pair_required", lambda: first.request("status"))
            first.pair(pair_code(usb))
            state = first.request("status")
            assert state["pet"]["custom"] == original["pet"]["custom"]
            assert state["pet"]["style"] == 2 and all(state["display"][k] == v for k, v in changed.items())
            passed("Reboot restored station, selected pet and display settings; old session expired")
            code = pair_code(usb)
            for _ in range(5):
                expect_error(401, "invalid_code", lambda: second.pair("000000"))
            expect_error(401, "invalid_code", lambda: second.pair(code))
            second.pair(pair_code(usb))
            usb.open_home_card(2)
            usb.key("down")
            expect_error(401, "pair_required", lambda: first.request("status"))
            expect_error(401, "pair_required", lambda: second.request("status"))
            first.pair(pair_code(usb))
            passed("Five-try pairing limit and on-device revocation of both sessions")
    finally:
        try:
            if original:
                usb.command("pet open " + str(original["pet"]["style"]))
                if original["pet"]["active"]:
                    usb.command("pet action " + original["pet"]["action"])
                else:
                    usb.command("pet close")
                    usb.home()
                try:
                    clients[0].request("display", "POST", original["display"])
                except (ApiError, OSError):
                    usb.apply_settings(original["display"])
                    if original["pet"]["active"]:
                        usb.command("pet open " + str(original["pet"]["style"]))
                        usb.command("pet action " + original["pet"]["action"])
                    else:
                        usb.home()
                if not original["pet"]["active"]:
                    for _ in range((baseline["selected"] - usb.status()["selected"]) % 6):
                        usb.key("down")
                time.sleep(0.9)
                final = usb.status()
                assert all(final[k] == v for k, v in original["display"].items())
                assert final["drops"] == baseline["drops"] and final["spi_errors"] == baseline["spi_errors"]
                assert final["heap"] > 50000
            for client in clients:
                if client.token:
                    try:
                        client.logout()
                    except (ApiError, OSError):
                        pass
        finally:
            usb.close()
    result = {"checks": checks, "id": info["id"], "ip": host,
              "frame_sha256": shapes, "heap": final["heap"], "min_heap": final["min"],
              "spi_errors": final["spi_errors"], "key_drops": final["drops"]}
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
