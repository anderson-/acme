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
  (.defines // [] | join(" ")) \
] | join("${YAML_SEP}")' "${PROP}" 2>/dev/null)

_yaml_field = $(shell echo "${YAML_CACHE}" | cut -d'${YAML_SEP}' -f$(1))

# --- board / core ---
FQBN         := $(call _yaml_field,1)
CORE         := $(shell echo ${FQBN} | cut -d: -f1)

# --- build paths ---
BUILD       := ${MKDIR}/.cache/build/${CORE}/${SRC}
OBJ         := ${BUILD}/${SKETCH}.ino.elf
STAMP_BUILD := ${BUILD}/.stamp-build
STAMP_LIBS  := ${MKDIR}/.cache/.stamp-libs
LOG         := ${BUILD}/build.log

# --- source files ---
RWC = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) $(filter $(subst *,%,$2),$d))
SRC_FILES := $(call RWC,${SRC},*.c *.cpp *.h *.hpp *.ino)

# --- project.yaml fields ---
BAUD         := $(call _yaml_field,2)
DEPENDENCIES := $(call _yaml_field,3)
LIB_DIRS     := $(call _yaml_field,4)
INJECT       := $(call _yaml_field,5)
DEFINES_LIST := $(call _yaml_field,6)

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
endif

FLAGS += $(if ${DEV},-DDEVELOPMENT,)
FLAGS += $(foreach def,${DEFINES_LIST},-D$(def))

# --- OTA / serial ---
OTA  := ${ADATA}/packages/${CORE}/hardware/${CORE}/*/tools/espota.py
MKFS := ${ADATA}/packages/${CORE}/tools/mkspiffs/*/mkspiffs

include ${MKDIR}/device.mk

# --- internal checks ---
.PHONY: _checksrc
_checksrc:
	@if [ ! -d "${SRC}" ]; then
		$(ERROR_S) "Source directory '${SRC}' not found. Set SRC= to your sketch directory."
		exit 1
	fi
	@if [ -z "${FQBN}" ]; then
		$(ERROR_S) "Missing 'board' field in ${PROP}"
		exit 1
	fi

.PHONY: fields
fields:
	@echo "YAML_CACHE: ${YAML_CACHE}"
	@echo "=== Project Fields ==="
	@echo "FQBN: ${FQBN}"
	@echo "CORE: ${CORE}"
	@echo "DEPENDENCIES: ${DEPENDENCIES}"
	@echo "LIB_DIRS: ${LIB_DIRS}"
	@echo "INJECT: ${INJECT}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"
	@echo "BAUD: ${BAUD}"
	@echo "FLAGS: ${FLAGS}"

# --- arduino-cli config ---
${MKDIR}/bin/data:
	$(INFO) "Setting up arduino-cli config..."
	mkdir -p ${ADATA}
	ADATA=${ADATA}; ADATA=$${ADATA//\//\\\/}; \
	sed "s/arduino_data.*/arduino_data: $${ADATA}/g" \
		${MKDIR}/arduino-cli.yaml > ${CFG}
	ALIBS=${ALIBS}; ALIBS=$${ALIBS//\//\\\/}; \
	sed -i "s/  user:.*/  user: $${ALIBS}/g" ${CFG}

# --- core install ---
${ADATA}/package_index.json: ${MKDIR}/bin/data
	$(INFO) "Updating core index..."
	${ARDUINO} core update-index
	touch ${ADATA}/package_index.json

${ADATA}/packages/${CORE}: ${ADATA}/package_index.json
	$(MAKE) _checksrc
	$(INFO) "Installing core ${CORE}..."
	${ARDUINO} core install ${CORE}:${CORE}
	touch ${ADATA}/packages/${CORE}

.PHONY: core
core: ${ADATA}/packages/${CORE}

# --- dependencies ---
${STAMP_LIBS}: ${PROP}
	$(INFO) "Installing dependencies..."
	mkdir -p $(dir ${STAMP_LIBS})
	@for LIB in $(DEPENDENCIES); do
		NAME=$$(echo $$LIB | cut -d@ -f1)
		VERSION=$$(echo $$LIB | cut -d@ -f2)
		if [ "$$VERSION" = "$$NAME" ]; then
			VERSION=""
		fi
		if [[ "$$NAME" == *.git ]]; then
			LIB_NAME=$$(basename $$NAME .git)
			LIB_DIR="${ALIBS}/libraries/$$LIB_NAME"
			if [ ! -e $$LIB_DIR ]; then
				$(INFO_S) "Cloning $$LIB_NAME..."
				git clone --depth 1 $$NAME $$LIB_DIR
			fi
		else
			if [ ! -e ${ALIBS}/libraries/$$NAME ]; then
				$(INFO_S) "Installing $$NAME$${VERSION:+@$$VERSION}..."
				${ARDUINO} lib install "$${VERSION:+$$NAME@$$VERSION}$${VERSION:-$$NAME}"
			else
				IVER=$$(grep version ${ALIBS}/libraries/$$NAME/library.properties | cut -d= -f2)
				if [ -n "$$VERSION" ] && [ "$$IVER" != "$$VERSION" ]; then
					$(INFO_S) "Updating $$NAME from $$IVER to $$VERSION..."
					${ARDUINO} lib install "$$NAME@$$VERSION"
				fi
			fi
		fi
	done
	touch ${STAMP_LIBS}

# --- build ---
${BUILD}:
	mkdir -p ${BUILD}

${STAMP_BUILD}: ${STAMP_LIBS} ${BUILD} ${ADATA}/packages/${CORE} ${FILES}
	@ $(MAKE) _checksrc
	@ $(foreach sym,$(INJECT),ln -s ${PWD}/$(sym)/* ${PWD}/${SRC} &&) true
	echo "LOG=${LOG} BUILD=${BUILD}"
	@ mkdir -p ${BUILD}; rm -f ${LOG}; touch ${LOG}; \
	$(call file_spinner,${LOG},Building ${SKETCH}...) & WATCH_PID=$$!; \
	trap "kill -- -$$WATCH_PID 2>/dev/null; wait $$WATCH_PID 2>/dev/null; printf '\r\033[K' >&2" EXIT INT TERM; \
	CMD="${ARDUINO} compile --fqbn ${FQBN} \
		$(foreach lib,$(LIB_DIRS),--libraries ${PWD}/$(lib)) \
		--build-property 'compiler.cpp.extra_flags=${FLAGS}' \
		--build-property 'compiler.c.extra_flags=${FLAGS}' \
		--build-path ${BUILD} ${SRC} -v"; \
	echo "$$CMD" | tr -s ' ' | sed 's/\t/ /g' >> ${LOG}; \
	time eval "$$CMD" >> ${LOG} 2>&1; \
	BUILD_EXIT=$$?; \
	kill $$WATCH_PID 2>/dev/null; wait $$WATCH_PID 2>/dev/null; printf '\r\033[K\n' >&2; \
	if [ $$BUILD_EXIT -eq 0 ] && test -f ${OBJ}; then \
		find ${PWD}/${SRC} -type l -delete; \
		touch ${STAMP_BUILD}; \
		$(OK_S) "Build successful!"; \
	else \
		find ${PWD}/${SRC} -type l -delete; \
		$(ERROR_S) "Build failed. Log:"; \
		cat ${LOG}; \
		rm -f ${STAMP_BUILD}; \
		exit 1; \
	fi

.PHONY: build
build: ${STAMP_BUILD}

.PHONY: flash
flash: ${STAMP_BUILD}
	$(call _usb_resolve)
	$(INFO_S) "Flashing ${SKETCH} to $$PORT..."
	time ${ARDUINO} upload -p $$PORT --fqbn ${FQBN} -i ${OBJ} ${SRC} -v

.PHONY: resolve-usb
resolve-usb:
	$(call _usb_resolve)

# --- filesystem ---
${BUILD}/img.bin: ${SRC}/data/*
	SIZE=$$(grep spiffs ${BUILD}/partitions.csv | cut -d, -f5)
	time ${MKFS} -c ${SRC}/data -s $${SIZE} ${BUILD}/img.bin

.PHONY: fs
fs: ${BUILD}/img.bin

# --- OTA ---
.PHONY: scan
scan:
	$(INFO) "Scanning for OTA devices..."
	SCAN_RESULT=$$(python3 ${MKDIR}/tools/scan.py 2>/dev/null)
	if [ -z "$$SCAN_RESULT" ]; then
		$(ERROR_S) "No OTA device found."
		exit 1
	fi
	$(OK_S) "Found: $$SCAN_RESULT"

.PHONY: forget-usb
forget-usb:
	rm -f ${CACHE_USB}
	$(OK) "USB device config cleared."

.PHONY: forget-ota
forget-ota:
	rm -f ${CACHE_OTA}
	$(OK) "OTA device config cleared."

.PHONY: ota
ota: ${STAMP_BUILD}
	$(call _ota_resolve)
	$(INFO_S) "OTA flash to $$OTAIP:$$OTAPORT..."
	time python3 ${OTA} -i "$$OTAIP" -p $$OTAPORT -f ${BUILD}/*.ino.bin

.PHONY: ota-fs
ota-fs: ${BUILD}/img.bin
	$(call _ota_resolve)
	$(INFO_S) "OTA filesystem to $$OTAIP:$$OTAPORT..."
	time python3 ${OTA} -i "$$OTAIP" -p $$OTAPORT -s -f ${BUILD}/img.bin

# --- deploy (no DEV flag) ---
.PHONY: deploy
deploy:
	touch ${SRC}/*.ino
	DEV= ${MAKE} build

# --- serial monitor ---
.PHONY: monitor
monitor:
	$(call _usb_resolve)
	python3 -m serial.tools.miniterm --raw --xonxoff --exit-char 3 $$PORT ${BAUD}

# --- serve local data dir ---
.PHONY: serve
serve:
	cd ${SRC}/data && python3 -m http.server 8000

# --- board info ---
.PHONY: list-boards
list-boards:
	${ARDUINO} board listall

.PHONY: list-usb
list-usb:
	${ARDUINO} board list

# --- clean targets ---
.PHONY: clean
clean:
	rm -f ${STAMP_BUILD}
	find ${PWD}/${SRC} -type l -delete
	$(OK) "Build stamp removed. Run 'make build' to recompile."

.PHONY: clean-libs
clean-libs:
	rm -f ${STAMP_LIBS}
	$(OK) "Libs stamp removed. Run 'make build' to re-download dependencies."

.PHONY: clean-build
clean-build:
	rm -rf ${BUILD}
	find ${PWD}/${SRC} -type l -delete
	$(OK) "Build cache removed."

.PHONY: clean-all
clean-all:
	rm -rf ${MKDIR}/.cache
	find ${PWD}/${SRC} -type l -delete
	$(OK) "Cache cleared."
	$(WARN) "To also remove binaries, run: make clean-bin CONFIRM=yes"

.PHONY: clean-bin
clean-bin:
	@if [ "${CONFIRM}" != "yes" ]; then
		$(ERROR_S) "This will delete all downloaded binaries and libraries."
		$(WARN_S) "Run: make clean-bin CONFIRM=yes"
		exit 1
	fi
	rm -rf ${MKDIR}/bin
	$(OK) "Binaries removed."

# --- example targets ---
EXAMPLES := $(wildcard examples/*)
EXAMPLE_NAMES := $(notdir $(EXAMPLES))

define EXAMPLE_TARGETS
.PHONY: build-$(1)
build-$(1):
	$${MAKE} build SRC=examples/$(1)

.PHONY: flash-$(1)
flash-$(1):
	$${MAKE} flash SRC=examples/$(1)
endef

DATA_EXAMPLES := $(wildcard examples/*/data)
DATA_EXAMPLE_NAMES := $(notdir $(patsubst %/data,%,$(DATA_EXAMPLES)))

define DATA_EXAMPLE_TARGETS
.PHONY: serve-$(1)
serve-$(1):
	cd examples/$(1)/data && python3 -m http.server 8000
endef

$(foreach example,$(EXAMPLE_NAMES),$(eval $(call EXAMPLE_TARGETS,$(example))))
$(foreach example,$(DATA_EXAMPLE_NAMES),$(eval $(call DATA_EXAMPLE_TARGETS,$(example))))