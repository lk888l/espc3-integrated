import importlib.util
from pathlib import Path
import socket
import threading
import unittest

spec = importlib.util.spec_from_file_location("dap_client", Path(__file__).parents[1] / "tools/dap_client.py")
dap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dap)


class ClientTests(unittest.TestCase):
    def exchange(self, response, action):
        local, peer = socket.socketpair()
        self.addCleanup(local.close)
        self.addCleanup(peer.close)
        local.settimeout(1)
        client = dap.Client.__new__(dap.Client)
        client.sock = local
        errors = []

        def serve():
            try:
                header = dap.receive_exact(peer, 8)
                magic, size, kind, reserved = dap.HEADER.unpack(header)
                self.assertEqual((magic, kind, reserved), (b"DAP\0", 1, 0))
                self.assertEqual(dap.receive_exact(peer, size), b"\0\3")
                for byte in response:
                    peer.sendall(bytes([byte]))
            except Exception as error:
                errors.append(error)

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            action(client)
        finally:
            thread.join(2)
            self.assertFalse(thread.is_alive())
            if errors:
                raise errors[0]

    def test_fragmented_response(self):
        packet = b"\0\4ABC\0"
        response = dap.HEADER.pack(b"DAP\0", len(packet), 2, 0) + packet
        self.exchange(response, lambda client: self.assertEqual(client.info(3, True), b"ABC\0"))

    def test_invalid_response_header(self):
        response = dap.HEADER.pack(b"DAP\0", 65, 2, 0)
        def action(client):
            with self.assertRaises(dap.ProtocolError):
                client.info(3)
        self.exchange(response, action)

    def test_truncated_connection(self):
        local, peer = socket.socketpair()
        self.addCleanup(local.close)
        peer.sendall(b"DA")
        peer.close()
        with self.assertRaises(dap.ProtocolError):
            dap.receive_exact(local, 8)

    def test_discovery(self):
        announcement = dap.TAG + b"0123456789AB" + (4441).to_bytes(2, "big")
        self.assertEqual(dap.parse_announcement(announcement, "192.0.2.1"),
                         {"serial": "0123456789AB", "host": "192.0.2.1", "port": 4441})
        self.assertIsNone(dap.parse_announcement(announcement[:-1], "192.0.2.1"))
        self.assertIsNone(dap.parse_announcement(announcement + b"x", "192.0.2.1"))
        self.assertIsNone(dap.parse_announcement(dap.TAG + b"ZZZZZZZZZZZZ" + b"\0\0", "192.0.2.1"))

    def test_empty_packets_are_rejected(self):
        client = dap.Client.__new__(dap.Client)
        with self.assertRaises(ValueError):
            client.request(b"")
        with self.assertRaises(ValueError):
            client.request(bytes(65))
        with self.assertRaises(ValueError):
            client.request(b"\x07")


if __name__ == "__main__":
    unittest.main()
