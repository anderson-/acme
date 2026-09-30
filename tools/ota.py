#!/usr/bin/env python3
"""Resolve a verified HTTP OTA endpoint, remembering the device's identity."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import sys
import tempfile
from urllib.parse import quote
from urllib.request import build_opener, ProxyHandler

from scan import discover


def endpoint(host, port):
    host = host.strip("[]")
    if not host or any(c in host for c in "/?#@") or any(c.isspace() for c in host) or not 0 < int(port) < 65536:
        raise ValueError("invalid OTA host or port")
    if ":" in host:
        host = f"[{quote(host, safe=':')}]"
    return f"http://{host}:{int(port)}"


def verify(url, identity=None):
    # Local device requests must not be sent to a workstation's HTTP proxy.
    with build_opener(ProxyHandler({})).open(url + "/info", timeout=2) as response:
        data = response.read(16385)
    if len(data) > 16384:
        raise ValueError("device info exceeds the limit")
    info = json.loads(data)
    if not isinstance(info, dict) or info.get("acme") != 1 or info.get("ota_protocol") != "http-v1" or not isinstance(info.get("id"), str) or not info["id"]:
        raise ValueError("device does not implement ACME HTTP OTA v1")
    if identity and info["id"] != identity:
        raise ValueError("device identity changed")
    return info


def reachable(device):
    # Use mDNS's resolved address directly, avoiding dependency on the host's
    # .local resolver (and DNS stalls on Linux without Avahi).
    url = endpoint(device.get("address") or device.get("hostname", ""), device["port"])
    verify(url, device["id"])
    return url


def save(cache, device):
    cache.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(dir=cache.parent, prefix=".ota-")
    try:
        with os.fdopen(descriptor, "w") as output:
            json.dump(device, output)
            output.write("\n")
        os.replace(temporary, cache)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def resolve(cache, host=None, port=80, timeout=5):
    if host:
        url = endpoint(host, port)
        verify(url)
        return url
    identity = None
    if cache.exists():
        try:
            device = json.loads(cache.read_text())
            identity = device["id"]
            return reachable(device)
        except (OSError, ValueError, KeyError, TypeError):
            print("Saved OTA endpoint needs rediscovery...", file=sys.stderr)
    devices = discover(timeout, identity=identity)
    if identity:
        devices = [device for device in devices if device["id"] == identity]
        if not devices:
            raise ValueError("saved device was not found; use forget-ota to select a different device")
    def candidate(device):
        try:
            return device, reachable(device)
        except (OSError, ValueError) as error:
            print(f"Skipping {device['hostname']}: {error}", file=sys.stderr)
            return None
    # These are small HTTP requests, bounded to four concurrent connections.
    with ThreadPoolExecutor(max_workers=4) as executor:
        verified = [result for result in executor.map(candidate, devices) if result]
    if not verified:
        raise ValueError("no ACME HTTP OTA devices found")
    index = 0
    if len(verified) > 1:
        for number, (device, url) in enumerate(verified, 1):
            print(f"  {number}) {device['hostname']} {url} id={device['id']}", file=sys.stderr)
        print("Select device [1]: ", end="", file=sys.stderr, flush=True)
        try:
            index = int(input() or "1") - 1
        except (EOFError, ValueError) as error:
            raise ValueError("invalid device selection; set OTAIP for noninteractive use") from error
        if not 0 <= index < len(verified):
            raise ValueError("invalid device selection")
    device, url = verified[index]
    save(cache, device)
    return url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--host", default="")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    try:
        print(resolve(args.cache, args.host, args.port, args.timeout))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"OTA resolution failed: {error}\n")


if __name__ == "__main__":
    main()
