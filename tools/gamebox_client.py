#!/usr/bin/env python3
"""C3 GameBox LAN client for Python 3 (standard library only)."""
import argparse
from contextlib import closing
import getpass
import http.client
import ipaddress
import json
import os
import re
import socket
import subprocess
import sys
import time


class ApiError(Exception):
    def __init__(self, status, reason):
        super().__init__(f"HTTP {status}: {reason}")
        self.status = status
        self.reason = reason


class GameBox:
    def __init__(self, host="192.168.4.1", token=None):
        self.host = str(ipaddress.IPv4Address(host))
        self.token = token

    def request(self, path, method="GET", data=None):
        try:
            return self._exchange(path, method, data)
        except (OSError, http.client.HTTPException):
            if method != "GET":
                raise
            return self._exchange(path, method, data)

    def _exchange(self, path, method, data):
        headers = {"Authorization": "Bearer " + self.token} if self.token else {}
        payload = None
        if data is not None:
            payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode()
            headers["Content-Type"] = "application/json"
        # Connect directly to the device. Each call owns its socket and has a deadline.
        with closing(http.client.HTTPConnection(self.host, timeout=5)) as conn:
            conn.request(method, "/api/v1/" + path, body=payload, headers=headers)
            response = conn.getresponse()
            body = response.read(4097)
            if len(body) > 4096:
                raise ValueError("Oversized device response")
            if response.status >= 300:
                raise ApiError(response.status, json.loads(body).get("error", response.reason))
            if path == "pet/frame":
                if len(body) != 1024:
                    raise ValueError("Incomplete pet frame")
                return body
            return json.loads(body)

    def pair(self, code):
        self.token = self.request("session", "POST", {"code": code})["token"]

    def logout(self):
        self.request("session", "DELETE")
        self.token = None


def broadcast_addresses():
    # A limited broadcast often leaves on only the default interface. Include
    # the directed broadcast of each local IPv4 subnet on multi-homed clients.
    addresses = {"255.255.255.255"}
    try:
        if sys.platform.startswith("linux"):
            data = subprocess.check_output(["ip", "-j", "-4", "addr", "show", "up"], timeout=3)
            for link in json.loads(data):
                for addr in link["addr_info"]:
                    if addr.get("broadcast"):
                        addresses.add(addr["broadcast"])
        elif sys.platform == "darwin":
            data = subprocess.check_output(["/sbin/ifconfig", "-a"], timeout=3).decode()
            addresses.update(re.findall(r"\bbroadcast (\d+\.\d+\.\d+\.\d+)", data))
        elif sys.platform == "win32":
            cmd = ("Get-NetIPAddress -AddressFamily IPv4 -AddressState Preferred | "
                   "Select-Object IPAddress,PrefixLength | ConvertTo-Json -Compress")
            data = subprocess.check_output(["powershell.exe", "-NoProfile", "-NonInteractive",
                                            "-Command", cmd], timeout=6)
            rows = json.loads(data)
            for row in rows if isinstance(rows, list) else [rows]:
                iface = ipaddress.IPv4Interface(f"{row['IPAddress']}/{row['PrefixLength']}")
                if not iface.ip.is_loopback and 0 < iface.network.prefixlen < 31:
                    addresses.add(str(iface.network.broadcast_address))
    except (OSError, subprocess.SubprocessError, ValueError, TypeError, KeyError):
        pass  # Discovery still works on the default interface or via an explicit --target.
    return addresses


def discover(target=None, timeout=1.5):
    found = {}
    targets = [str(ipaddress.IPv4Address(target))] if target else broadcast_addresses()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        end = time.monotonic() + timeout
        resend_at = 0
        while time.monotonic() < end:
            now = time.monotonic()
            if now >= resend_at:
                for address in targets:
                    try:
                        sock.sendto(b"C3GB_DISCOVER/1", (address, 4444))
                    except OSError:
                        if target:
                            raise
                resend_at = now + 0.5
            sock.settimeout(max(0.001, min(end, resend_at) - now))
            try:
                data, (host, port) = sock.recvfrom(256)
            except socket.timeout:
                continue
            try:
                device = json.loads(data)
                if (port == 4444 and device["v"] == 1 and device["http"] == 80
                        and re.fullmatch(r"C3GB-[0-9A-F]{6}", device["id"])):
                    found[host] = device["id"]
            except (ValueError, TypeError, KeyError):
                continue
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.1", help="Device IPv4 address")
    subs = parser.add_subparsers(dest="command", required=True)
    subs.add_parser("discover").add_argument("--target",
                                             help="Subnet broadcast or device IP")
    subs.add_parser("info")
    subs.add_parser("status")
    pet = subs.add_parser("pet")
    pet.add_argument("--style", type=int, choices=range(5))
    pet.add_argument("--action", choices="idle blink happy eat sleep surprised sad dance".split())
    pet.add_argument("--close", action="store_true")
    display = subs.add_parser("display")
    for field, count in (("motion", 3), ("brightness", 4), ("header", 4)):
        display.add_argument("--" + field, type=int, choices=range(count))
    subs.add_parser("wifi").add_argument("ssid")
    args = parser.parse_args()
    if args.command == "discover":
        print(json.dumps(discover(args.target), indent=2))
        return
    change = None
    if args.command in ("pet", "display"):
        keys = ("style", "action") if args.command == "pet" else ("motion", "brightness", "header")
        change = {key: getattr(args, key) for key in keys if getattr(args, key) is not None}
        if args.command == "pet" and args.close:
            if change:
                parser.error("--close takes no style or action")
            change = {"close": True}
        if not change:
            parser.error("Specify at least one change")
    client = GameBox(args.host, os.getenv("C3_SESSION"))
    temporary = args.command != "info" and not client.token
    if temporary:
        client.pair(getpass.getpass("Pairing code (Network > Up): ").strip())
    try:
        if args.command == "wifi":
            change = {"ssid": args.ssid, "password": getpass.getpass("Wi-Fi password (empty for open): ")}
        result = client.request(args.command, "POST" if change else "GET", change)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    finally:
        if temporary:
            try:
                client.logout()
            except (OSError, http.client.HTTPException, ApiError):
                pass  # a station change can interrupt the link; the session expires on-device


if __name__ == "__main__":
    try:
        main()
    except (OSError, http.client.HTTPException, ValueError, ApiError) as exc:
        sys.exit(str(exc))
