MKDIR ?= ${PWD}

# --- paths ---
ADATA := ${MKDIR}/bin/data
ALIBS := ${MKDIR}/bin
CFG   ?= ${ADATA}/arduino-cli.yaml

# Arduino only publishes an Intel ctags binary for macOS; use Nix's native one.
CTAGS_PATH := $(shell dirname "$$(command -v ctags)")
ARDUINO := arduino-cli --config-file ${CFG}

# --- build paths ---
BUILD       := ${MKDIR}/.cache/build/${CORE}/${SRC}
OBJ         := ${BUILD}/${SKETCH}.ino.elf
STAMP_BUILD := ${BUILD}/.stamp-build
STAMP_LIBS  := ${BUILD}/.stamp-libs
LOG         := ${BUILD}.log

# --- source files ---
RWC = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) $(filter $(subst *,%,$2),$d))
SRC_FILES := $(call RWC,${SRC},*.c *.cpp *.h *.hpp *.ino)

LOCAL_LIB_FILES := $(foreach lib,$(LIB_DIRS),$(call RWC,${PWD}/$(lib),*.c *.cpp *.h *.hpp))
FILES := ${SRC_FILES} ${LOCAL_LIB_FILES}

# --- wifi (optional) ---
include ${MKDIR}/backends/wifi.mk
FLAGS := ${WIFI_FLAGS}
DEV ?= 1

FLAGS += $(if ${DEV},-DDEVELOPMENT,)
FLAGS += $(foreach def,${DEFINES_LIST},-D$(def))

# --- OTA / serial ---
MKFS_SPIFFS := ${ADATA}/packages/${CORE}/tools/mkspiffs/*/mkspiffs
MKFS_LITTLEFS := ${ADATA}/packages/${CORE}/tools/mklittlefs/*/mklittlefs
MKFS_TOOL := $(if $(filter littlefs,$(FS)),${MKFS_LITTLEFS},${MKFS_SPIFFS})
ESPTOOL := ${ADATA}/packages/${CORE}/tools/esptool_py/*/esptool

include ${MKDIR}/backends/arduino/targets.mk
