#!/usr/bin/env python3
"""Discover ACME HTTP OTA devices with Arduino's pinned native mDNS tool."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import queue
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
from urllib.request import urlopen

VERSION = "1.1.0"
RELEASES = {
    ("Linux", "x86_64"): ("Linux_64bit", "6966f406e5b6e80d82a388706d6d9d8a63d46035d19d1e6b4fb1ebeaf7fea056"),
    ("Linux", "aarch64"): ("Linux_ARM64", "19597d8d15aac922813b34064deb870c0e5efc08d7430d15c8501768890f7725"),
    ("Darwin", "x86_64"): ("macOS_64bit", "6815f6d1fdee1472d2a5cbc52692ee899fcc6a5b340fb07005de390c9578ad20"),
    ("Darwin", "arm64"): ("macOS_ARM64", "f6a221b01024f15de3370a6f3a7efe75f3ac745aef4d8cbb623ddbd57b13526c"),
}


def discovery_binary():
    override = os.environ.get("ACME_MDNS_DISCOVERY")
    if override:
        return override
    try:
        asset, expected = RELEASES[platform.system(), platform.machine()]
    except KeyError as error:
        raise RuntimeError(f"unsupported discovery host: {platform.system()} {platform.machine()}") from error
    destination = Path(__file__).resolve().parents[1] / "bin" / "mdns-discovery" / VERSION / asset
    binary = destination / "mdns-discovery"
    if binary.is_file():
        return str(binary)
    destination.mkdir(parents=True, exist_ok=True)
    filename = f"mdns-discovery_v{VERSION}_{asset}.tar.gz"
    url = f"https://github.com/arduino/mdns-discovery/releases/download/v{VERSION}/{filename}"
    print(f"Downloading mdns-discovery {VERSION} for {asset}...", file=sys.stderr)
    with tempfile.TemporaryDirectory(dir=destination) as temporary:
        archive = Path(temporary) / filename
        digest = hashlib.sha256()
        with urlopen(url, timeout=30) as response, archive.open("wb") as output:
            while chunk := response.read(65536):
                digest.update(chunk)
                output.write(chunk)
        if digest.hexdigest() != expected:
            raise RuntimeError("mdns-discovery SHA-256 mismatch")
        # Extract only the executable; do not extract archive paths or links.
        with tarfile.open(archive, "r:gz") as bundle:
            members = [m for m in bundle.getmembers() if m.isfile() and Path(m.name).name == "mdns-discovery"]
            if len(members) != 1:
                raise RuntimeError("mdns-discovery archive has no unique executable")
            candidate = Path(temporary) / "mdns-discovery"
            with bundle.extractfile(members[0]) as source, candidate.open("wb") as output:
                while chunk := source.read(65536):
                    output.write(chunk)
            candidate.chmod(0o755)
            os.replace(candidate, binary)
    return str(binary)


def read_events(stream, events):
    """Protocol JSON objects may span several lines."""
    decoder = json.JSONDecoder()
    pending = ""
    try:
        for line in stream:
            pending += line
            if len(pending) > 1048576:
                raise RuntimeError("discovery output exceeded the limit")
            while pending.strip():
                pending = pending.lstrip()
                try:
                    event, end = decoder.raw_decode(pending)
                except json.JSONDecodeError:
                    break
                events.put(event)
                pending = pending[end:]
        if pending.strip():
            raise RuntimeError("incomplete JSON from discovery")
    except Exception as error:
        events.put(error)
    finally:
        events.put(None)


def acme_device(port):
    properties = port.get("properties", {})
    if port.get("protocol") != "network" or properties.get("acme") != "1" or properties.get("ota_protocol") != "http-v1":
        return None
    identity = properties.get("id")
    hostname = properties.get("hostname", "").rstrip(".")
    address = port.get("address")
    try:
        number = int(properties["port"])
    except (KeyError, ValueError, TypeError):
        return None
    if not identity or not address or not 0 < number < 65536:
        return None
    return {"id": identity, "hostname": hostname, "address": address, "port": number}


def discover(timeout=5, binary=None, identity=None):
    if timeout <= 0:
        raise ValueError("discovery timeout must be positive")
    devices = {}
    events = queue.Queue()
    with tempfile.TemporaryFile(mode="w+") as errors:
        process = subprocess.Popen([binary or discovery_binary()], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=errors, text=True)
        reader = threading.Thread(target=read_events, args=(process.stdout, events), daemon=True)
        reader.start()
        try:
            process.stdin.write('HELLO 1 "acme"\nSTART_SYNC\n')
            process.stdin.flush()
            deadline = time.monotonic() + timeout
            while (remaining := deadline - time.monotonic()) > 0:
                try:
                    event = events.get(timeout=remaining)
                except queue.Empty:
                    break
                if isinstance(event, Exception):
                    raise event
                if event is None:
                    errors.seek(0)
                    raise RuntimeError(f"discovery exited unexpectedly: {errors.read().strip()}")
                if event.get("error"):
                    raise RuntimeError(event.get("message", "discovery failed"))
                device = acme_device(event.get("port", {}))
                if device:
                    if event.get("eventType") == "remove":
                        devices.pop(device["id"], None)
                    elif event.get("eventType") == "add":
                        devices[device["id"]] = device
                        if identity == device["id"]:
                            break
        finally:
            try:
                process.stdin.write("QUIT\n")
                process.stdin.flush()
                process.wait(timeout=2)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.terminate()
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdin.close()
            reader.join(timeout=1)
            process.stdout.close()
    return sorted(devices.values(), key=lambda d: (d["hostname"], d["id"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        devices = discover(args.timeout)
        if args.json:
            print(json.dumps(devices))
        else:
            for device in devices:
                print(f'{device["hostname"]} {device["address"]}:{device["port"]} id={device["id"]}')
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"Discovery failed: {error}\n")


if __name__ == "__main__":
    main()
