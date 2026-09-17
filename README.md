# ACME — Embedded build targets

Generated Make targets for Arduino, ESP-IDF, and CH32 projects in reproducible
Nix environments.

Each `project.yaml` selects a platform. ACME generates the corresponding
`build-<platform>-<name>`, `flash-<platform>-<name>`, and
`monitor-<platform>-<name>` targets.

---

## Requirements

- [Nix](https://nixos.org/download) — manages the Arduino and CH32 toolchains
- An official ESP-IDF installation — only for `platform: esp-idf`

Everything else is handled automatically on first run.

---

## Getting started

```sh
# Generate project targets
make

# Build an example
make build-arduino-blink
```

On first `flash` or `ota`, ACME scans for available devices and asks you to pick one. The choice is saved — next time it just works.

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

## Platforms

### Arduino

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

### ESP-IDF

ESP-IDF projects must contain their standard `CMakeLists.txt`. ACME uses an
existing official ESP-IDF installation and keeps its build directory under
`.cache/`.

```yaml
platform: esp-idf
target: esp32c3
idf_path: /path/to/esp-idf
baudrate: 115200
defines:
  - MY_FEATURE=1
```

### CH32

CH32 projects use [ch32fun](https://github.com/cnlohr/ch32fun). The source file
must have the same name as its directory, for example `blink/blink.c`.

```yaml
platform: ch32
mcu: CH32V003
prefix: riscv64-none-elf
defines:
  - FUNCONF_USE_DEBUGPRINTF=1
```

---

## Targets

| Target | Description |
|---|---|
| `make` | Generate project targets (default) |
| `make build-<platform>-<name>` | Compile a project |
| `make flash-<platform>-<name>` | Build and flash a project |
| `make monitor-<platform>-<name>` | Open its monitor |

Arduino projects with a `data/` directory additionally receive `serve`, `fs`,
`flash-fs`, `ota`, and `ota-fs` targets with the same suffix.

---

## Examples

Projects are grouped by platform under `examples/`:

```sh
make build-arduino-blink
make flash-esp-idf-blink
make monitor-ch32-blink
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
make monitor-esp-idf-app BAUD=9600
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
