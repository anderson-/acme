# ACME — Arduino CLI Maker Essentials

A Makefile that simplifies `arduino-cli` usage in a reproducible environment,
aimed at speeding up microcontroller project development.

Dependencies are managed automatically: `arduino-cli`, cores, and libraries
are downloaded on first build and kept locally inside the project.

---

## Setup

```sh
git submodule add https://github.com/anderson-/acme.git .acme
```
```make
MKDIR := .acme
SRC   := ${PWD}
# WIFI  := ${PWD}/wifi.yaml
-include ${MKDIR}/makefile
```

### ACME as a sibling directory

In your project, create a `Makefile`:
```make
MKDIR := ../acme
SRC   := ${PWD}
# WIFI  := ${PWD}/wifi.yaml
-include ${MKDIR}/makefile
```

---

## Project structure

Your project needs at minimum:
```
my-project/
├── my-project.ino   # must match the directory name
├── project.yaml
├── wifi.yaml        # optional
└── makefile
```

or 

```
my-project/
├── main                 # SRC   := ${PWD}/main
│   ├── main.ino
│   ├── project.yaml
│   └── wifi.yaml        # optional
└── makefile
```

---

## project.yaml
```yaml
board: esp32:esp32:lolin32-lite   # FQBN of your board

libs:
  - WebSockets@2.3.5              # libraries to install

include:                          # additional source directories
  - src
```

To list available board FQBNs:
```sh
make list-boards
```

---

## Wi-Fi credentials

Create `wifi.yaml` (keep it out of version control):
```yaml
ssid: my-wifi
psk: my-password
```

These are injected at compile time as `STASSID` and `STAPSK`:
```cpp
const char* ssid     = STASSID;
const char* password = STAPSK;
```

---

## Building and flashing
```sh
make build                            # compile
make list-usb                         # find usb device
make flash PORT=/dev/tty.usbmodem101  # flash via serial
```

By default, `DEVELOPMENT=1` is defined as a preprocessor flag.
To build without it (e.g. for production):
```sh
make deploy                         # equivalent to DEV= make build
```

---

## OTA
```sh
make find                           # discover device IP and OTA port
make ota                            # auto-discover and flash firmware
# make ota OTAIP=192.168.1.100 OTAPORT=3232 # use port 8266 for esp8266
```

---

## SPIFFS filesystem

Place your files in `data/` inside your project directory, then:
```sh
make fs                             # build the SPIFFS image
make ota-fs                         # upload via OTA (auto-discover)
# make ota-fs OTAIP=192.168.1.100 OTAPORT=3232 # use port 8266 for esp8266
```

---

## Serial monitor
```sh
make monitor           # raw serial output
make monitor-hex       # hexdump
make cat-serial        # via pyserial miniterm
```

Default baud rate is `115200`. Override with `BAUD=9600`.

---

## Running the examples
```sh
make build-blink
# make build SRC=examples/blink
make flash-blink PORT=/dev/tty.usbmodem101
# make flash SRC=examples/blink PORT=/dev/tty.usbmodem101
```

---

## Cleaning up
```sh
make clean        # removes build artifacts for current SRC
make clean-all    # removes build artifacts, downloaded arduino-cli, cores and libs
```

---

## Environment

ACME uses `nix-shell` for a reproducible environment:
```sh
make shell
```

Build artifacts can optionally be placed on a ramdisk (`/dev/shm`) to speed
up compilation — this is configured automatically if the ramdisk is available.