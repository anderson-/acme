MKDIR ?= ${PWD}
include ${MKDIR}/utils.mk

.ONESHELL:
.DEFAULT_GOAL := shell
MAKEFLAGS += --no-print-directory

# --- default target ---
.PHONY: shell
shell:
	@ nix-shell ${MKDIR} && exit 0 || true
	$(ERROR) "nix-shell not found. See https://nixos.org/download"

# --- nix check ---
ifneq ($(MAKECMDGOALS),)
  ifneq ($(MAKECMDGOALS),shell)
    ifndef IN_NIX_SHELL
      $(error Not in nix-shell. Run 'make' or 'make shell')
    endif
  endif
endif

SHELL := /bin/bash
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# --- paths ---
ADATA := ${MKDIR}/bin/data
ALIBS := ${MKDIR}/bin
CFG   ?= ${ADATA}/arduino-cli.yaml

ARDUINO := ARDUINO_DATA_DIR=${ADATA} arduino-cli --config-file ${CFG}

# --- sketch ---
SRC    ?= main
PROP   ?= ${SRC}/project.yaml
SKETCH := $(notdir ${SRC})

YAML_SEP := |
YAML_CACHE := $(shell yq -r '[ \
  .board // "", \
  (.baudrate // "115200"), \
  (.dependencies // [] | join(" ")), \
  (.lib_dirs // [] | join(" ")), \
  (.inject // [] | join(" ")), \
  (.defines // [] | join(" ")), \
  (.filesystem // "spiffs") \
] | join("${YAML_SEP}")' "${PROP}" 2>/dev/null)

_yaml_field = $(shell echo "${YAML_CACHE}" | cut -d'${YAML_SEP}' -f$(1))

# --- board / core ---
FQBN         := $(call _yaml_field,1)
CORE         := $(shell echo ${FQBN} | cut -d: -f1)

# --- build paths ---
BUILD       := ${MKDIR}/.cache/build/${CORE}/${SRC}
OBJ         := ${BUILD}/${SKETCH}.ino.elf
STAMP_BUILD := ${BUILD}/.stamp-build
STAMP_LIBS  := ${BUILD}/.stamp-libs
LOG         := ${BUILD}.log

# --- source files ---
RWC = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) $(filter $(subst *,%,$2),$d))
SRC_FILES := $(call RWC,${SRC},*.c *.cpp *.h *.hpp *.ino)

# --- project.yaml fields ---
BAUD         := $(call _yaml_field,2)
DEPENDENCIES := $(call _yaml_field,3)
LIB_DIRS     := $(call _yaml_field,4)
INJECT       := $(call _yaml_field,5)
DEFINES_LIST := $(call _yaml_field,6)
FS           := $(call _yaml_field,7)

LOCAL_LIB_FILES := $(foreach lib,$(LIB_DIRS),$(call RWC,${PWD}/$(lib),*.c *.cpp *.h *.hpp))
FILES := ${SRC_FILES} ${LOCAL_LIB_FILES}

# --- wifi (optional) ---
FLAGS :=
WIFI  := ${MKDIR}/wifi.yaml
DEV   ?= 1

ifneq (,$(wildcard ${WIFI}))
SSID := $(shell yq -r '.ssid // empty' "${WIFI}" 2>/dev/null)
PSK  := $(shell yq -r '.psk // empty' "${WIFI}" 2>/dev/null)
FLAGS += -DSTASSID=\"$(SSID)\" -DSTAPSK=\"$(PSK)\"
else
WIFI =
endif

FLAGS += $(if ${DEV},-DDEVELOPMENT,)
FLAGS += $(foreach def,${DEFINES_LIST},-D$(def))

# --- OTA / serial ---
OTA  := ${ADATA}/packages/${CORE}/hardware/${CORE}/*/tools/espota.py
MKFS_SPIFFS := ${ADATA}/packages/${CORE}/tools/mkspiffs/*/mkspiffs
MKFS_LITTLEFS := ${ADATA}/packages/${CORE}/tools/mklittlefs/*/mklittlefs
MKFS_TOOL := $(if $(filter littlefs,$(FS)),${MKFS_LITTLEFS},${MKFS_SPIFFS})
ESPTOOL := ${ADATA}/packages/${CORE}/tools/esptool_py/*/esptool

include ${MKDIR}/device.mk
include ${MKDIR}/backends/arduino/targets.mk
