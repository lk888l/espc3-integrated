#!/usr/bin/env python3
"""Exercise PC-monitor UDP, UI and Wi-Fi recovery on the connected C3.

Run with ESP-IDF's Python environment. Keeps saved settings and Wi-Fi credentials;
temporarily opens pages, runs a game and toggles Wi-Fi, then returns Home.
"""
import argparse
import json
from pathlib import Path
import socket
import threading
import time
import uuid

from serial_smoke import Device


def sample(session, seq):
    return {"v": 1, "session": session, "seq": seq, "host": "PC-SMOKE",
            "cpu_pct": 37.5, "ram_used": 8 << 30, "ram_total": 32 << 30,
            "gpu_name": "NVIDIA SMOKE", "gpu_pct": 81.0,
            "vram_used": 4 << 30, "vram_total": 12 << 30, "gpu_temp_c": 55.0}


def wait_status(device, predicate, timeout=10):
    end = time.monotonic() + timeout
    while True:
        state = device.status()
        if predicate(state):
            return state
        assert time.monotonic() < end, state
        time.sleep(0.1)


class Stream:
    def __init__(self, host):
        self.stop_event = threading.Event()
        self.session = uuid.uuid4().hex
        self.acks = 0
        self.thread = threading.Thread(target=self.run, args=(host,), daemon=True)

    def run(self, host):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.connect((host, 4443))
            udp.settimeout(0.2)
            seq = 0
            while not self.stop_event.is_set():
                packet = sample(self.session, seq)
                try:
                    udp.send(json.dumps(packet).encode())
                    ack = json.loads(udp.recv(1024))
                    if ack.get("session") == self.session and ack.get("seq") == seq:
                        self.acks += 1
                except (OSError, ValueError):
                    pass  # Wi-Fi interruption is an intentional part of this test.
                seq += 1
                self.stop_event.wait(0.3)

    def start(self):
        self.thread.start()

    def close(self):
        self.stop_event.set()
        self.thread.join(timeout=2)
        assert not self.thread.is_alive()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--host", help="Defaults to the MCU's current STA address")
    parser.add_argument("--duration", type=float, default=15,
                        help="Additional steady-stream resource check in seconds")
    parser.add_argument("--output", type=Path, default=Path("build/hardware/pc-monitor"))
    args = parser.parse_args()
    device = Device(args.port, args.output / "pc-monitor-smoke.log")
    stream = None
    initial = device.status()
    result = {"checks": [], "initial": initial}

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    try:
        assert not initial["client"], "Close the active DAP client before testing"
        device.command("wifi on")
        state = wait_status(device, lambda s: s.get("pc_listening") == 1)
        host = args.host or state["sta"]
        assert host != "0.0.0.0", "Connect STA or pass --host for the device AP address"
        result["host"] = host
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.connect((host, 4443))
            udp.settimeout(0.3)

            def send(packet, expect_ack=True):
                data = packet if isinstance(packet, bytes) else json.dumps(packet).encode()
                udp.send(data)
                if expect_ack:
                    ack = json.loads(udp.recv(1024))
                    assert ack == {"v": 1, "session": packet["session"],
                                   "seq": packet["seq"], "ack": True}, ack
                else:
                    try:
                        reply = udp.recv(1024)
                    except socket.timeout:
                        return
                    raise AssertionError(f"Unexpected ACK for invalid/old sample: {reply}")

            session = uuid.uuid4().hex
            packet = sample(session, 1)
            send(packet)
            state = device.status()
            assert state["pc_received"] and not state["pc_stale"], state
            assert float(state["pc_cpu_pct"]) == 37.5 and float(state["pc_gpu_pct"]) == 81, state
            assert state["pc_ram_used"] == packet["ram_used"], state
            passed("Complete metrics accepted, acknowledged and published to UI snapshot")

            packet = sample(session, 2)
            for key in ("cpu_pct", "gpu_pct", "vram_used", "vram_total", "gpu_temp_c"):
                packet[key] = None
            send(packet)
            state = device.status()
            assert not any(state[k] for k in ("pc_cpu_valid", "pc_gpu_valid",
                                              "pc_vram_valid", "pc_temp_valid")), state
            passed("Unavailable metrics remain unavailable rather than becoming zero")

            rejected = state["pc_rejected"]
            # Stay below the Ethernet MTU: a fragmented packet may be dropped by
            # lwIP before it reaches our socket and its rejection counter.
            for bad in (b"{", b"x" * 1200, json.dumps({**packet, "v": 2}).encode(),
                        json.dumps({**packet, "cpu_pct": 101}).encode(),
                        json.dumps({**packet, "ram_total": 0}).encode()):
                send(bad, False)
            state = device.status()
            assert state["pc_rejected"] >= rejected + 5, state
            old = state["pc_out_of_order"]
            send(sample(session, 1), False)
            send(packet, False)
            state = device.status()
            assert state["pc_out_of_order"] >= old + 2 and state["pc_seq"] == 2, state
            passed("Invalid, oversized, duplicate and out-of-order datagrams rejected")

            session = uuid.uuid4().hex
            send(sample(session, 0))
            wait_status(device, lambda s: s["pc_stale"] == 1, timeout=4.5)
            send(sample(session, 1))
            assert not device.status()["pc_stale"]
            passed("New sender session, 3-second expiry and live recovery")

        stream = Stream(host)
        stream.start()
        wait_status(device, lambda s: s["pc_session"] == stream.session)
        device.open_home_card(1)
        selected = device.status()["selected"]
        for _ in range((5 - selected) % 7):
            device.key("down")
        device.key("enter")
        assert device.status()["ui"] == 21
        device.key("down")
        device.key("up")
        device.key("back")
        assert device.status()["ui"] == 1
        device.open_home_card(0)
        device.key("enter")
        device.key("enter")
        time.sleep(1)
        device.key("back")
        assert device.status()["ui"] == 11
        device.home()
        passed("Monitor pagination, Back routing and game remain responsive while receiving")

        device.command("wifi off")
        wait_status(device, lambda s: not s["pc_listening"] and not s["pc_received"])
        device.command("wifi on")
        wait_status(device, lambda s: s["pc_received"] and not s["pc_stale"], timeout=40)
        passed("Wi-Fi off/on releases transport and restores streaming automatically")

        device.command("dap on")
        wait_status(device, lambda s: s["ready"] == 1)
        time.sleep(0.5)
        assert not device.status()["pc_stale"]
        device.command("dap off")
        passed("PC-monitor socket coexists with the existing DAP service")

        warm = device.status()
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            time.sleep(min(1, max(0, deadline - time.monotonic())))
            state = device.status()
            assert not state["pc_stale"] and state["pc_error"] == 0, state
            assert state["drops"] == initial["drops"] and state["spi_errors"] == initial["spi_errors"], state
        final = device.status()
        assert final["heap"] >= warm["heap"] - 2048, (warm, final)
        assert final["pc_stack_free"] >= 512 and final["ui_stack_free"] >= 512, final
        assert stream.acks > 5, stream.acks
        result.update(final=final, acknowledgements=stream.acks)
        passed("Steady streaming keeps heap, task stacks, input and OLED healthy")
    finally:
        if stream:
            stream.close()
        try:
            device.command("dap off")
            device.command("wifi on" if initial["wifi"] else "wifi off")
            device.home()
        finally:
            device.close()
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "pc-monitor-smoke.json").write_text(
                json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
