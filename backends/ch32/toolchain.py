#!/usr/bin/env python3
"""Install a pinned xPack RISC-V toolchain into ACME's bin directory."""

import argparse
import hashlib
import platform
from pathlib import Path
import shutil
import tarfile
import tempfile
from urllib.request import urlopen


RELEASES = {
    "15.2.0-1": {
        "darwin-arm64": "6588e8351455fad8aca37551f0e5a5543f3346bfa9a837cf03cbd3bdd4989f8f",
        "darwin-x64": "98e83f097b10163869dabffd58389ac8e4eb41bae0f67124569158655be593ea",
        "linux-arm64": "4e60e2a54c16385e4e2476d08240f857495d5a61609d97e1ee49f72875a6ec1e",
        "linux-x64": "aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758",
    }
}


def platform_key():
    systems = {"Darwin": "darwin", "Linux": "linux"}
    machines = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "x64", "AMD64": "x64"}
    try:
        return f"{systems[platform.system()]}-{machines[platform.machine()]}"
    except KeyError as error:
        raise SystemExit(f"unsupported host platform: {platform.system()} {platform.machine()}") from error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    host = platform_key()
    try:
        expected = RELEASES[args.version][host]
    except KeyError as error:
        raise SystemExit(f"no CH32 toolchain {args.version!r} for {host}") from error

    filename = f"xpack-riscv-none-elf-gcc-{args.version}-{host}.tar.gz"
    url = (
        "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/"
        f"download/v{args.version}/{filename}"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temporary:
        archive = Path(temporary) / filename
        digest = hashlib.sha256()
        print(f"Downloading {url}")
        with urlopen(url) as response, archive.open("wb") as stream:
            while chunk := response.read(1024 * 1024):
                stream.write(chunk)
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise SystemExit("toolchain SHA-256 mismatch")

        extracted = Path(temporary) / "extracted"
        extracted.mkdir()
        with tarfile.open(archive, "r:gz") as bundle:
            members = bundle.getmembers()
            top = Path(members[0].name).parts[0]
            for member in members:
                parts = Path(member.name).parts
                if not parts or parts[0] != top or ".." in parts:
                    raise SystemExit("unsafe toolchain archive")
            bundle.extractall(extracted)
        source = extracted / top
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.move(source, args.output)
    (args.output / ".acme-installed").touch()


if __name__ == "__main__":
    main()
