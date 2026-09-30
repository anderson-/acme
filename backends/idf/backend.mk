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
include ${MKDIR}/backends/wifi.mk
DEFINES := ${WIFI_FLAGS}

# EXTRA_CFLAGS is only re-read by cmake at configure time (tools/cmake/project.cmake),
# so a change to any support file must force a fresh configure. Deleting the whole
# build dir guarantees the next 'idf.py build' reconfigures and recompiles with the
# new flags instead of silently reusing stale ones.
CONFIG_SRC := ${WIFI_STATE} ${PROP} ${PARTITIONS} \
	${MKDIR}/backends/idf/project.py ${MKDIR}/backends/idf/backend.mk \
	${MKDIR}/backends/idf/targets.mk ${MKDIR}/backends/wifi.mk
CONFIG_STAMP := ${BUILD}/.config-stamp

${CONFIG_STAMP}: ${CONFIG_SRC}
	@rm -rf ${BUILD}
	@mkdir -p ${BUILD}
	@touch $@

DEFINES += $(foreach def,${DEFINES_LIST},-D$(def))

define _idf_env
	unset PYTHONPATH
	export IDF_TOOLS_PATH="${IDF_TOOLS_PATH}"
	source "${IDF_ROOT}/export.sh" >/dev/null
	export EXTRA_CFLAGS="${DEFINES}"
	export EXTRA_CXXFLAGS="${DEFINES}"
endef

include ${MKDIR}/backends/idf/targets.mk
