#!/usr/bin/env python3
"""Discover and inspect C3 CMSIS-DAP. Standard library only; no target writes."""
from __future__ import annotations

import argparse
import json
import socket
import struct
import time

HEADER = struct.Struct("<4sHBB")
MAX_PACKET = 64
QUERY = b"C3_DAP_V1?"
TAG = b"C3_DAP_V1!"


class ProtocolError(RuntimeError):
    pass


def receive_exact(sock: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise ProtocolError("Connection closed in the middle of a DAP frame")
        data.extend(part)
    return bytes(data)


class Client:
    def __init__(self, host: str, port: int = 4441, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.sock.close()

    def request(self, packet: bytes, fragment: bool = False) -> bytes:
        if not 1 <= len(packet) <= MAX_PACKET:
            raise ValueError("DAP packet must contain 1..64 bytes")
        if packet[0] == 7:
            raise ValueError("TransferAbort has no response; use abort()")
        frame = HEADER.pack(b"DAP\0", len(packet), 1, 0) + packet
        if fragment:
            for byte in frame:
                self.sock.sendall(bytes([byte]))
                time.sleep(0.001)
        else:
            self.sock.sendall(frame)
        magic, size, kind, reserved = HEADER.unpack(receive_exact(self.sock, 8))
        if magic != b"DAP\0" or kind != 2 or reserved or not 1 <= size <= MAX_PACKET:
            raise ProtocolError("Invalid DAP response header")
        response = receive_exact(self.sock, size)
        if response[0] != packet[0]:
            raise ProtocolError(f"DAP command mismatch: {response[0]:02x} != {packet[0]:02x}")
        return response

    def abort(self):
        self.sock.sendall(HEADER.pack(b"DAP\0", 1, 1, 0) + b"\x07")

    def info(self, identifier: int, fragment: bool = False) -> bytes:
        response = self.request(bytes([0, identifier]), fragment)
        if len(response) < 2 or response[1] != len(response) - 2:
            raise ProtocolError("Malformed DAP_Info response")
        return response[2:]

    def identity(self, fragment: bool = False) -> dict:
        result = {}
        for name, identifier in (("vendor", 1), ("product", 2), ("serial", 3), ("protocol", 4)):
            result[name] = self.info(identifier, fragment).rstrip(b"\0").decode("utf-8", "replace")
        result["capabilities"] = int.from_bytes(self.info(0xF0, fragment), "little")
        result["packet_size"] = int.from_bytes(self.info(0xFF, fragment), "little")
        return result


def parse_announcement(data: bytes, source: str) -> dict | None:
    if len(data) != len(TAG) + 14 or not data.startswith(TAG):
        return None
    try:
        serial = data[len(TAG):len(TAG) + 12].decode("ascii")
        if any(ch not in "0123456789ABCDEF" for ch in serial):
            return None
    except UnicodeDecodeError:
        return None
    port = int.from_bytes(data[-2:], "big")
    if not port:
        return None
    return {"serial": serial, "host": source, "port": port}


def discover(address: str = "255.255.255.255", timeout: float = 2.0) -> list[dict]:
    results = {}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind(("", 0))
        sock.sendto(QUERY, (address, 4442))
        until = time.monotonic() + timeout
        while (remaining := until - time.monotonic()) > 0:
            sock.settimeout(remaining)
            try:
                data, peer = sock.recvfrom(256)
            except socket.timeout:
                break
            result = parse_announcement(data, peer[0])
            if result:
                results[result["serial"]] = result
    return sorted(results.values(), key=lambda item: item["serial"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    discovery = subcommands.add_parser("discover")
    discovery.add_argument("--address", default="255.255.255.255")
    discovery.add_argument("--timeout", type=float, default=2.0)
    for name in ("info", "smoke"):
        command = subcommands.add_parser(name)
        command.add_argument("--host", default="192.168.4.1")
        command.add_argument("--port", type=int, default=4441)
        command.add_argument("--fragment", action="store_true")
        if name == "smoke":
            command.add_argument("--count", type=int, default=100)
    args = parser.parse_args()
    try:
        if args.command == "discover":
            if args.timeout <= 0:
                parser.error("--timeout must be positive")
            print(json.dumps(discover(args.address, args.timeout), indent=2))
            return 0
        with Client(args.host, args.port) as client:
            result = client.identity(args.fragment)
            if args.command == "smoke":
                if not 1 <= args.count <= 10000:
                    parser.error("--count must be 1..10000")
                started = time.monotonic()
                expected = result["serial"]
                for _ in range(args.count):
                    serial = client.info(3, args.fragment).rstrip(b"\0").decode("ascii")
                    if serial != expected:
                        raise ProtocolError("Device serial changed")
                result["round_trips"] = args.count
                result["elapsed_s"] = round(time.monotonic() - started, 3)
            print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, ProtocolError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
