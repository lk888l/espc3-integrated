#!/usr/bin/env python3
"""Wi-Fi CMSIS-DAP integration checks; only DAP_Info, no target access."""
import argparse
import json
from pathlib import Path
import socket
import time

from dap_client import Client, HEADER, MAX_PACKET, discover, receive_exact


def expect_close(sock):
    try:
        assert sock.recv(1) == b"", "Malformed/second connection was not closed"
    except ConnectionResetError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--count", type=int, default=300)
    parser.add_argument("--output", type=Path, default=Path("build/hardware/wifi-smoke.json"))
    args = parser.parse_args()
    if not 1 <= args.count <= 10000:
        parser.error("--count must be 1..10000")
    result = {"host": args.host, "checks": []}

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    with Client(args.host) as client:
        identity = client.identity()
        assert identity["packet_size"] == MAX_PACKET and identity["capabilities"] & 1
        result["identity"] = identity
        expected = identity["serial"].encode() + b"\0"
        client.abort()
        assert client.info(3) == expected, "TransferAbort must not return a response"
        passed("Identity and response-free TransferAbort")
        with socket.create_connection((args.host, 4441), 2) as second:
            second.settimeout(2)
            expect_close(second)
        assert client.info(3) == expected
        passed("Single-client ownership")

    announcements = discover(args.host, timeout=0.6)
    assert any(item["serial"] == identity["serial"] for item in announcements)
    passed("UDP discovery matches TCP identity")

    with Client(args.host) as client:
        started = time.monotonic()
        for _ in range(args.count):
            assert client.info(3) == expected
        elapsed = time.monotonic() - started
        result["round_trips"] = args.count
        result["elapsed_s"] = round(elapsed, 3)
        result["mean_ms"] = round(elapsed * 1000 / args.count, 3)
        passed(f"{args.count} consecutive DAP_Info round trips")
        for _ in range(100):
            assert client.info(3, fragment=True) == expected
        passed("100 byte-fragmented TCP requests")

        # A truncated command is rejected before the Arm engine can touch pins.
        client.sock.sendall(HEADER.pack(b"DAP\0", 3, 1, 0) + b"\x05\0\x01")
        magic, size, kind, reserved = HEADER.unpack(receive_exact(client.sock, 8))
        assert (magic, size, kind, reserved) == (b"DAP\0", 1, 2, 0)
        assert receive_exact(client.sock, 1) == b"\xff"
        assert client.info(3) == expected
        passed("Truncated command rejected and stream remains usable")

    for length in (0, 65):
        with socket.create_connection((args.host, 4441), 2) as peer:
            peer.settimeout(2)
            peer.sendall(HEADER.pack(b"DAP\0", length, 1, 0))
            expect_close(peer)
    passed("Invalid frame lengths close the connection")

    with socket.create_connection((args.host, 4441), 2) as peer:
        peer.settimeout(3)
        started = time.monotonic()
        peer.sendall(b"DAP\0\x02")
        expect_close(peer)
        elapsed = time.monotonic() - started
        assert 1.8 < elapsed < 3, f"Unexpected partial-frame timeout: {elapsed}"
        result["partial_timeout_s"] = round(elapsed, 3)
    passed("Partial-frame timeout")

    with Client(args.host) as client:
        assert client.identity()["serial"] == identity["serial"]
    passed("Reconnect after all protocol error cases")

    result["ok"] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("round_trips", "elapsed_s", "mean_ms")}),
          flush=True)


if __name__ == "__main__":
    main()
