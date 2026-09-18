# ACME — Embedded build targets

Generated Make targets for Arduino projects in reproducible Nix environments.

Each `project.yaml` selects a platform. ACME generates the corresponding
`build-<platform>-<name>`, `flash-<platform>-<name>`, and
`monitor-<platform>-<name>` targets.

---

## Requirements

- [Nix](https://nixos.org/download) — provides the host tools and isolated shells

Arduino cores are installed under `bin/` automatically on first use. Versions
are isolated, so projects pinned to different releases do not overwrite each
other.

---

## Getting started

```sh
# Generate project targets
make

# Build an example
make build-arduino-blink
```

On first `flash` or `ota`, ACME scans for available devices and asks you to pick
one. The choice is saved — next time it just works.

---

## Project structure

```
your-project/
  examples/
    arduino/
      blink/
        blink.ino
        project.yaml
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

## Platform: Arduino

```yaml
platform: arduino
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
| `make` | Generate project targets (default) |
| `make build-<platform>-<name>` | Compile a project |
| `make flash-<platform>-<name>` | Build and flash a project |
| `make monitor-<platform>-<name>` | Open its monitor |
| `make list-usb-<platform>-<name>` | List serial devices without platform-specific tooling |
| `make forget-usb-<platform>-<name>` | Forget the saved serial-device choice |

Projects with a `data/` directory additionally receive `serve`, `fs`,
`flash-fs`, and `ota-fs` targets. Network-capable projects provide firmware
`ota`. Every project provides the shared USB discovery targets.

---

## Examples

Projects are grouped by platform under `examples/`:

```sh
make build-arduino-blink
make build-arduino-blink-c3-zero
make build-arduino-ota32
make build-arduino-ota8266
make build-arduino-websockets
```

If the example has a `data/` directory, ACME also generates filesystem targets:

```sh
make serve-arduino-websockets
make fs-arduino-websockets
make ota-fs-arduino-websockets
make flash-fs-arduino-websockets
```

---

## Variables

Override any of these on the command line:

```sh
make build-arduino-blink
make flash-arduino-blink PORT=/dev/cu.usbmodem1234
make ota-arduino-websockets OTAIP=192.168.1.42 OTAPORT=3232
make monitor-arduino-blink BAUD=9600
```

---

## Device selection

On first `flash`, `monitor`, or `ota`, ACME scans for available devices and
shows an interactive list:

```
  1) /dev/cu.usbmodem1123401   serial   ESP32 Family Device
  2) /dev/cu.usbmodem1123402   serial   ESP32 Family Device
Select device [1]:
```

The selection is saved in `.cache/devices/usb/<sketch>` or
`.cache/devices/ota/<sketch>`. If the device is no longer available on the next
run, ACME asks whether to clear the saved config and rescan.

---

## How ACME is included

ACME can be used in three ways:

- **Embedded** — copy the `acme/` folder inside your project
- **Sibling** — keep `acme/` next to your project folder
- **Custom path** — set `ACME_DIR=/path/to/acme` before running make

`acme.mk` handles discovery automatically.