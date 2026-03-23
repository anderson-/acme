# ACME — Arduino CLI Maker Essentials

A Makefile that simplifies `arduino-cli` usage in a reproducible environment,
aimed at speeding up microcontroller project development.

Dependencies are managed automatically: `arduino-cli`, cores, and libraries
are downloaded on first build and kept locally inside the project.

---

## Requirements

- [Nix](https://nixos.org/download) — manages the entire toolchain (`arduino-cli`, `python3`, `yq`, etc.)

Everything else is handled automatically on first run.

---

## Getting started

```sh
# Enter the development environment
make

# Build the default sketch (main/)
make build

# Flash via USB
make flash

# Flash via OTA
make ota
```

On first `flash` or `ota`, ACME scans for available devices and asks you to pick one. The choice is saved — next time it just works.

---

## Project structure

```
your-project/
  main/
    main.ino
    project.yaml     # board, dependencies, and build config
  wifi.yaml          # optional, git-ignored WiFi credentials
  acme.mk            # copied from ACME repo
  makefile           # one-liner that includes ACME
```

**`makefile`** (drop-in, you can add custom targets):
```makefile
-include acme.mk
```

**`acme.mk`** (copied from ACME repo, finds ACME automatically):
```makefile
# looks for acme in ./acme or ../acme, or set ACME_DIR manually
```

---

## project.yaml

```yaml
board: esp32:esp32:m5stack_cardputer
baudrate: 115200

dependencies:
  - FastLED
  - WebSockets@2.3.5
  - ArduinoJson@7.1.0

# local libs with library.properties
lib_dirs:
  - ../lib/my-arduino-lib

# local code without library.properties — injected into sketch dir
inject:
  - ../lib/my-raw-lib

# compiler defines
defines:
  - MY_FEATURE=1
```

**`wifi.yaml`** (optional, add to `.gitignore`):
```yaml
ssid: MyNetwork
psk: mypassword
```

When present, `STASSID` and `STAPSK` are automatically added as compiler defines.

---

## Targets

| Target | Description |
|---|---|
| `make` | Open nix-shell (default) |
| `make build` | Compile the sketch |
| `make flash` | Flash via USB |
| `make ota` | Flash via OTA |
| `make fs` | Build SPIFFS filesystem image |
| `make ota-fs` | Flash filesystem via OTA |
| `make monitor` | Open serial monitor |
| `make deploy` | Build without `DEVELOPMENT` flag |
| `make scan` | Scan for OTA devices on the network |
| `make list-usb` | List connected USB boards |
| `make list-boards` | List all known boards |
| `make serve` | Serve `data/` on http://localhost:8000 |

**Clean targets:**

| Target | Description |
|---|---|
| `make clean` | Remove build stamp (forces recompile) |
| `make clean-libs` | Remove libs stamp (forces re-download) |
| `make clean-build` | Remove build cache |
| `make clean-all` | Remove all cache |
| `make clean-bin CONFIRM=yes` | Remove downloaded binaries and libraries |

**Device config:**

| Target | Description |
|---|---|
| `make forget-usb` | Clear saved USB port |
| `make forget-ota` | Clear saved OTA address |

---

## Examples

If the project has an `examples/` directory, ACME generates targets automatically:

```sh
make build-blink
make flash-blink
```

If the example has a `data/` directory, ACME also generates a `serve-<example>` target:

```sh
make serve-websockets
```

---

## Variables

Override any of these on the command line:

```sh
make build SRC=examples/blink
make flash PORT=/dev/cu.usbmodem1234
make ota OTAIP=192.168.1.42 OTAPORT=3232
make monitor BAUD=9600
```

---

## Device selection

On first `flash`, `monitor`, or `ota`, ACME scans for available devices and shows an interactive list:

```
  1) /dev/cu.usbmodem1123401   serial   ESP32 Family Device
  2) /dev/cu.usbmodem1123402   serial   ESP32 Family Device
Select device [1]:
```

The selection is saved in `.cache/usb/<sketch>` or `.cache/ota/<sketch>`. If the device is no longer available on the next run, ACME asks whether to clear the saved config and rescan.

---

## How ACME is included

ACME can be used in three ways:

- **Embedded** — copy the `acme/` folder inside your project
- **Sibling** — keep `acme/` next to your project folder
- **Custom path** — set `ACME_DIR=/path/to/acme` before running make

`acme.mk` handles discovery automatically.