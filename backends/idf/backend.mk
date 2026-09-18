MKDIR ?= ${PWD}

IDF_REPOSITORY ?= https://github.com/espressif/esp-idf.git
IDF_ROOT := ${MKDIR}/bin/esp-idf/${IDF_VERSION_KEY}
IDF_TOOLS_PATH := ${MKDIR}/bin/esp-idf-tools/${IDF_VERSION_KEY}
IDF_CHECKOUT := ${IDF_ROOT}/.acme-checkout
IDF_INSTALLED := ${IDF_TOOLS_PATH}/.acme-${TARGET}

BUILD := ${MKDIR}/.cache/build/esp-idf/${SRC}
STAGE := ${MKDIR}/.cache/stage/esp-idf/${SRC}
PARTITIONS := ${MKDIR}/backends/idf/partitions/${FLASH_SIZE}.csv
PROJECT_DIR := $(if $(wildcard ${SRC}/CMakeLists.txt),${SRC},${STAGE})
PROJECT_READY := $(if $(wildcard ${SRC}/CMakeLists.txt),${SRC}/CMakeLists.txt,${STAGE}/CMakeLists.txt)
OBJ := ${BUILD}/${SKETCH}.bin
FS_OBJ := ${BUILD}/storage.bin
LOG := ${BUILD}/build.log
DEFINES :=
WIFI := ${MKDIR}/wifi.yaml

ifneq (,$(wildcard ${WIFI}))
SSID := $(shell yq -r '.ssid // empty' "${WIFI}" 2>/dev/null)
PSK := $(shell yq -r '.psk // empty' "${WIFI}" 2>/dev/null)
ifneq (,$(strip ${SSID}))
DEFINES += -DWIFI_SSID=\"$(SSID)\" -DWIFI_PASSWORD=\"$(PSK)\"
endif
endif

DEFINES += $(foreach def,${DEFINES_LIST},-D$(def))

define _idf_env
	unset PYTHONPATH
	export IDF_TOOLS_PATH="${IDF_TOOLS_PATH}"
	source "${IDF_ROOT}/export.sh" >/dev/null
	export EXTRA_CFLAGS="${DEFINES}"
	export EXTRA_CXXFLAGS="${DEFINES}"
endef

include ${MKDIR}/backends/idf/targets.mk
