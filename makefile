SYSDEPS := python3 pip3 bash rsync curl
_CHECK  := $(foreach exec,$(SYSDEPS),\
			$(if $(shell which $(exec)),,$(error "No $(exec) in PATH")))
SHELL   := /bin/bash
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
TIMEOUT := 0.2
ifeq ($(UNAME_S),Darwin)
DEFAULT_SO := $(if $(filter arm64 aarch64,$(UNAME_M)),macOS_ARM64,macOS_64bit)
SCAN      := timeout ${TIMEOUT} dns-sd -B _arduino._tcp > /tmp/scan
SCAN_DEV  := timeout ${TIMEOUT} dns-sd -Gv4 $$(cat /tmp/scan | grep local | tr -s ' ' | cut -d' ' -f 7).local. > /tmp/scan
SCAN_IP   := cat /tmp/scan | grep local | tr -s ' ' | cut -d' ' -f 6
SCAN_PORT := echo 3232
else
DEFAULT_SO := $(if $(filter arm64 aarch64,$(UNAME_M)),Linux_ARM64,Linux_64bit)
SCAN      := avahi-browse -ptr "_arduino._tcp" > /tmp/scan
SCAN_DEV  := cat /tmp/scan | grep "="
SCAN_IP   := cat /tmp/scan | cut -d\; -f8
SCAN_PORT := cat /tmp/scan | cut -d\; -f9
endif
MKDIR   ?= ${PWD}
AC_REPO ?= https://github.com/arduino/arduino-cli
AC_VER  ?= 1.3.1
SO      ?= ${DEFAULT_SO}
AC_BASE ?= https://downloads.arduino.cc/arduino-cli
AC_TAR  := ${AC_BASE}/arduino-cli_${AC_VER}_${SO}.tar.gz
ADATA   := ${PWD}/bin/.arduino15
ALIBS   := ${PWD}/bin
CFG     ?= ${ADATA}/arduino-cli.yaml
ARDUINO := ${PWD}/bin/arduino-cli
ARDUINO := ARDUINO_DATA_DIR=${ADATA} ${ARDUINO} --config-file ${CFG}
SRC     ?= main
PROP    ?= ${SRC}/project.yaml
FQBN    ?= $(shell cat ${PROP} | grep board | cut -d' ' -f2)
CORE    := $(shell echo ${FQBN} | cut -d: -f1)
WIFI    ?= ${MKDIR}/wifi.yaml
SSID    ?= $(shell cat ${WIFI} | grep ssid | tr -d ' ' | cut -d: -f2)
PSK     ?= $(shell cat ${WIFI} | grep psk | tr -d ' ' | cut -d: -f2)
FLAGS   ?=
DEV     ?= 1
DEFINES := $(if ${DEV},DEVELOPMENT,)
RWC      = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) \
  			$(filter $(subst *,%,$2),$d))
FILES   := $(call RWC,${SRC},*.c *.cpp *.h *.hpp *.ino)
FLAGS   := ${FLAGS} $(foreach def, ${DEFINES}, -D$(def))
FLAGS   := ${FLAGS} -DSTASSID="$(SSID)"
FLAGS   := ${FLAGS} -DSTAPSK="$(PSK)"
RAMFS   ?= /dev/shm2  # disabled
RAMDISK ?= ${RAMFS}/quickbuild
BUILD   ?= .build/${CORE}/${SRC}
OBJ     := ${BUILD}/*.ino.elf
OTAIP   ?=
OTAPORT ?=
BAUD    ?= 115200
PORT    ?= /dev/ttyUSB0
VENV    := . .venv/bin/activate
PY      := ${VENV} && python3
OTA     := bin/.arduino15/packages/${CORE}/hardware/${CORE}/*/tools/espota.py
MKFS    := bin/.arduino15/packages/${CORE}/tools/mkspiffs/*/mkspiffs

.ONESHELL:

shell:
	nix-shell

.venv:
	virtualenv -p python3 .venv

bin:
	${MAKE} deps
	if [ -e ${RAMFS} ]; then
		mkdir -p ${RAMDISK}/bin
		ln -s ${RAMDISK}/bin bin
	else
		mkdir -p bin
	fi
	mkdir -p ${ADATA}
	ADATA=${ADATA}; ADATA=$${ADATA//\//\\\/}; \
	sed "s/arduino_data.*/arduino_data: $${ADATA}/g" \
		${MKDIR}/arduino-cli.yaml > ${CFG}
	ALIBS=${ALIBS}; ALIBS=$${ALIBS//\//\\\/}; \
	sed -i "s/  user:.*/  user: $${ALIBS}/g" ${CFG}

bin/arduino-cli: bin
	curl -fsSL ${AC_TAR} | tar -xz -C ${PWD}/bin/
	touch bin/arduino-cli

${ADATA}/package_index.json: bin/arduino-cli
	${ARDUINO} core update-index
	touch ${ADATA}/package_index.json

${ADATA}/packages/${CORE}: ${ADATA}/package_index.json
	${ARDUINO} core install ${CORE}:${CORE}
	touch ${ADATA}/packages/${CORE}

.PHONY: checksrc
checksrc:
	@ if [ ! -d "${SRC}" ]; then \
		echo -e "\033[31m>>> Source directory ${SRC} not found! Set SRC variable to point to your sketch directory.\033[0m"; \
		exit 1; \
	fi

.PHONY: core
core: checksrc ${ADATA}/packages/${CORE}

${BUILD}:
	if [ -e ${RAMFS} ]; then
		mkdir -p ${RAMDISK}/build
		ln -s ${RAMDISK}/build ${BUILD}
	else
		mkdir -p ${BUILD}
	fi

.PHONY: download-libs
download-libs:
	@ ${VENV} && cat ${PROP} | yq -Y .libs | tr -d ' -' | \
		while read LIB; do
			NAME=$$(echo $$LIB | cut -d@ -f1)
			VERSION=$$(echo $$LIB | cut -d@ -f2)
			if [ ! -e ${PWD}/bin/libraries/${NAME} ]; then
				${ARDUINO} lib install $$NAME@$$VERSION
			else
				IVER=$$(cat ${PWD}/bin/libraries/$$NAME/library.properties \
					| grep version | cut -d= -f2)
				if [ ! "$$IVER" = "$$VERSION" ]; then
					${ARDUINO} lib install $$NAME@$$VERSION
				fi
			fi
			if [ "$$NAME" = "Tiny4kOLED" ]; then
				# find string avr/pgmspace in files and replace with pgmspace
				find ${PWD}/bin/libraries/$$NAME -type f -exec sed -i \
					-e 's/avr\/pgmspace/pgmspace/g' {} \;
			fi
		done

${OBJ}: checksrc download-libs ${BUILD} ${ADATA}/packages/${CORE} ${FILES}
	time ${ARDUINO} compile --fqbn ${FQBN} \
		$(foreach include, \
	 		$(shell ${VENV} && cat ${PROP} | yq -Y .include | tr -d ' -'), \
	 	  --libraries $(include)) \
	 	--build-property 'compiler.cpp.extra_flags=${FLAGS}' \
	 	--build-path $$(pwd)/${BUILD} \
	 	${SRC} -v

.PHONY: build
build: ${OBJ}

.PHONY: deps
deps: .venv

flash: ${OBJ}
	time ${ARDUINO} upload -p ${PORT} --fqbn ${FQBN} -i ${OBJ} ${SRC} -v

.PHONY: clean
clean:
	rm -rf ${BUILD}

.PHONY: clean-all
clean-all: clean
	rm -rf bin
	rm -rf .build
	if [ -e ${RAMFS} ]; then
		rm -rf ${RAMDISK}
	fi

.PHONY: deploy
deploy:
	touch ${SRC}/*.ino
	DEV= ${MAKE} -s build

ota: ${OBJ}
	@ if [ -z ${OTAIP} -o -z ${OTAPORT} ]; then
		${SCAN}
		${SCAN_DEV}
		if [ -z ${OTAIP} ]; then
			OTAIP=$$(${SCAN_IP})
		fi
		if [ -z ${OTAPORT} ]; then
			OTAPORT=$$(${SCAN_PORT})
		fi
	fi
	${PY} ${OTA} -i "$${OTAIP}" -p $${OTAPORT} -f ${BUILD}/*.ino.bin

find:
	${SCAN}
	${SCAN_DEV}
	OTAIP=$$(${SCAN_IP})
	OTAPORT=$$(${SCAN_PORT})
	echo "Found: $${OTAIP}@$${OTAPORT}"

monitor:
	stty ${BAUD} -F ${PORT} raw -echo
	cat ${PORT}

monitor-hex:
	stty ${BAUD} -F ${PORT} raw -echo
	cat ${PORT} | hexdump -C

list-boards:
	${ARDUINO} board listall

${BUILD}/img.bin: ${SRC}/data/*
	SIZE=$$(cat ${BUILD}/partitions.csv | grep spiffs | cut -d, -f5)
	${MKFS} -c ${SRC}/data -s $${SIZE} ${BUILD}/img.bin

.PHONY: fs
fs: ${BUILD}/img.bin

ota-fs: ${BUILD}/img.bin
	@ if [ -z ${OTAIP} -o -z ${OTAPORT} ]; then
		${SCAN}
		${SCAN_DEV}
		if [ -z ${OTAIP} ]; then
			OTAIP=$$(${SCAN_IP})
		fi
		if [ -z ${OTAPORT} ]; then
			OTAPORT=$$(${SCAN_PORT})
		fi
	fi
	${PY} ${OTA} -i "$${OTAIP}" -p $${OTAPORT} -s -f ${BUILD}/img.bin