"""Host-side OTA protocol and discovery tests; no board or third-party Python needed."""

import io
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ota
import scan


class DiscoveryTest(unittest.TestCase):
    def test_multiline_protocol_and_truncated_output(self):
        events = queue.Queue()
        scan.read_events(io.StringIO('{\n"eventType": "hello"\n}\n{"error":true}\n'), events)
        self.assertEqual(events.get()["eventType"], "hello")
        self.assertTrue(events.get()["error"])
        self.assertIsNone(events.get())
        scan.read_events(io.StringIO('{"eventType":'), events)
        self.assertIsInstance(events.get(), RuntimeError)

    def test_native_tool_events_filter_update_remove_and_exit(self):
        def event(identity, kind="add", address="192.0.2.1", acme="1"):
            return {"eventType": kind, "port": {
                "protocol": "network", "address": address,
                "properties": {"acme": acme, "id": identity,
                               "hostname": identity + ".local.", "port": "80",
                               "ota_protocol": "http-v1"}}}
        messages = [event("ignored", acme="0"), event("keep"),
                    event("keep", address="192.0.2.2"), event("gone"),
                    event("gone", kind="remove")]
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "discovery"
            binary.write_text(f'''#!{sys.executable}
import json, sys
for command in sys.stdin:
    if command.startswith('HELLO'):
        print(json.dumps({{"eventType":"hello"}}), flush=True)
    elif command.startswith('START_SYNC'):
        for event in {messages!r}:
            print(json.dumps(event, indent=2), flush=True)
    elif command.startswith('QUIT'):
        break
''')
            binary.chmod(0o755)
            result = scan.discover(timeout=0.3, binary=str(binary))
        self.assertEqual([d["id"] for d in result], ["keep"])
        self.assertEqual(result[0]["address"], "192.0.2.2")
        self.assertEqual(result[0]["hostname"], "keep.local")


class OTATest(unittest.TestCase):
    def setUp(self):
        self.info = {"acme": 1, "id": "device-1", "ota_protocol": "http-v1"}
        self.uploads = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                body = json.dumps(owner.info).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self):
                owner.uploads.append((self.path, self.headers.get("Content-Type"),
                                      self.rfile.read(int(self.headers["Content-Length"]))))
                self.send_response(200)
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.cache = self.directory / "cache"
        self.port = self.server.server_port
        self.url = ota.endpoint("127.0.0.1", self.port)
        self.device = {"id": "device-1", "hostname": "127.0.0.1",
                       "address": "127.0.0.1", "port": self.port}

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
        self.temporary.cleanup()

    def test_identity_and_http_protocol_are_required_even_with_explicit_ip(self):
        self.assertEqual(ota.resolve(self.cache, "127.0.0.1", self.port), self.url)
        with self.assertRaisesRegex(ValueError, "identity"):
            ota.verify(self.url, "other-device")
        self.info["ota_protocol"] = "arduinoota"
        with self.assertRaisesRegex(ValueError, "HTTP OTA"):
            ota.resolve(self.cache, "127.0.0.1", self.port)

    def test_cache_is_reused_and_legacy_cache_is_migrated(self):
        with patch.object(ota, "discover", return_value=[self.device]) as discovery:
            self.cache.write_text("192.0.2.99:3232\n")
            self.assertEqual(ota.resolve(self.cache), self.url)
            self.assertEqual(json.loads(self.cache.read_text())["id"], "device-1")
            self.assertEqual(ota.resolve(self.cache), self.url)
            discovery.assert_called_once()

    def test_saved_identity_is_preserved_after_dhcp_change(self):
        ota.save(self.cache, dict(self.device, address="192.0.2.99"))
        with patch.object(ota, "reachable", side_effect=[ValueError("old address"), self.url]), \
                patch.object(ota, "discover", return_value=[dict(self.device, id="wrong-device"), self.device]):
            self.assertEqual(ota.resolve(self.cache), self.url)
        self.assertEqual(json.loads(self.cache.read_text())["address"], "127.0.0.1")
        with patch.object(ota, "reachable", side_effect=ValueError("offline")), \
                patch.object(ota, "discover", return_value=[dict(self.device, id="wrong-device")]):
            with self.assertRaisesRegex(ValueError, "forget-ota"):
                ota.resolve(self.cache)

    def test_shared_make_upload_sends_raw_image_and_stops_on_invalid_device(self):
        image = self.directory / "firmware.bin"
        image.write_bytes(b"\xe9firmware\x00\xff")
        harness = self.directory / "makefile"
        harness.write_text(f'''.ONESHELL:
MKDIR := {ROOT}
SRC := test
OTAIP := 127.0.0.1
OTAPORT := {self.port}
include {ROOT}/utils.mk
include {ROOT}/device.mk
upload:
\t$(call _ota_upload,{image},/update)
''')
        command = ["make", "-f", str(harness), "upload"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.uploads, [("/update", "application/octet-stream", image.read_bytes())])
        self.info["id"] = ""
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.uploads), 1)

    def test_ipv6_urls_and_invalid_input(self):
        self.assertEqual(ota.endpoint("fe80::123%en0", 80), "http://[fe80::123%25en0]:80")
        with self.assertRaises(ValueError):
            ota.endpoint("host/path", 80)


if __name__ == "__main__":
    unittest.main()
