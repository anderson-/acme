SYSDEPS := python3 pip3 bash rsync
_CHECK  := $(foreach exec,$(SYSDEPS),\
			$(if $(shell which $(exec)),,$(error "No $(exec) in PATH")))
SHELL   := /bin/bash
AC_REPO := https://github.com/arduino/arduino-cli
AC_VER  := 0.18.3
SO      := Linux_64bit
_REL    := /releases/download/
AC_TAR  := ${AC_REPO}${_REL}${AC_VER}/arduino-cli_${AC_VER}_${SO}.tar.gz
ADATA   := ${PWD}/bin/.arduino15
CFG     := ${ADATA}/arduino-cli.yaml
ARDUINO := ${PWD}/bin/arduino-cli
ARDUINO := ARDUINO_DATA_DIR=${ADATA} ${ARDUINO} --config-file ${CFG}
SRC     ?= main
PROP    := ${SRC}/project.yaml
FQBN    := $(shell cat ${PROP} | grep board | cut -d' ' -f2)
CORE    := $(shell echo ${FQBN} | cut -d: -f1)
LIBS    := $(shell cat ${PROP} | grep libs | cut -d' ' -f2)
SSID    := $(shell cat wifi.yaml | grep ssid | tr -d ' ' | cut -d: -f2)
PSK     := $(shell cat wifi.yaml | grep psk | tr -d ' ' | cut -d: -f2)
FLAGS   ?=
DEV     ?= 1
DEFINES := $(if ${DEV},DEVELOPMENT,)
RWC      = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) \
  			$(filter $(subst *,%,$2),$d))
FILES   := $(call RWC,${SRC},*.c *.cpp *.h *.hpp *.ino)
FLAGS   := ${FLAGS} $(foreach def, ${DEFINES}, -D$(def))
FLAGS   := ${FLAGS} -DSTASSID="$(SSID)"
FLAGS   := ${FLAGS} -DSTAPSK="$(PSK)"
RAMFS   := /dev/shm2  # disabled
RAMDISK := ${RAMFS}/quickbuild
BUILD   := .build/${SRC}
OBJ     := ${BUILD}/*.ino.elf
OTAIP   ?=
OTAPORT ?=
BAUD    ?= 115200
PORT    ?= /dev/ttyUSB0
OTA     := bin/.arduino15/packages/${CORE}/hardware/${CORE}/*/tools/espota.py

.ONESHELL:

.PHONY: deps
deps:
	which yq || pip3 install pyserial esptool yq

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
	sed "s/arduino_data.*/arduino_data: $${ADATA}/g" arduino-cli.yaml > ${CFG}

bin/arduino-cli: bin
	wget -q ${AC_TAR} -O - | tar -xz -C ${PWD}/bin/
	touch bin/arduino-cli

${ADATA}/package_index.json: bin/arduino-cli
	${ARDUINO} core update-index
	touch ${ADATA}/package_index.json

${ADATA}/packages/${CORE}: ${ADATA}/package_index.json
	${ARDUINO} core install ${CORE}:${CORE}
	touch ${ADATA}/packages/${CORE}

.PHONY: core
core: ${ADATA}/packages/${CORE}

${BUILD}:
	if [ -e ${RAMFS} ]; then
		mkdir -p ${RAMDISK}/build
		ln -s ${RAMDISK}/build ${BUILD}
	else
		mkdir -p ${BUILD}
	fi

${OBJ}: ${BUILD} ${ADATA}/packages/${CORE} ${FILES}
	time ${ARDUINO} compile --fqbn ${FQBN} \
		$(foreach include, \
	 		$(shell cat ${PROP} | yq -Y .include | tr -d ' -'), \
	 	  --libraries $(include)) \
	 	--build-property 'compiler.cpp.extra_flags=${FLAGS}' \
	 	--build-cache-path $$(pwd)/${BUILD} \
	 	--build-path $$(pwd)/${BUILD} \
	 	${SRC} -v

.PHONY: build
build: ${OBJ}

flash: ${OBJ}
	time ${ARDUINO} upload -p ${PORT} --fqbn ${FQBN} -i ${OBJ} ${SRC} -v

.PHONY: clean
clean:
	rm -rf .build

.PHONY: clean-all
clean-all: clean
	rm -rf bin
	rm -rf ${RAMDISK}

.PHONY: deploy
deploy:
	touch ${SRC}/*.ino
	DEV= ${MAKE} -s build

ota: ${OBJ}
	@ if [ -z ${OTAIP} ]; then
		OTAIP=$$(avahi-browse -ptr  "_arduino._tcp" | grep = | cut -d\; -f8)
	fi
	@ if [ -z ${OTAPORT} ]; then
		OTAPORT=$$(avahi-browse -ptr  "_arduino._tcp" | grep = | cut -d\; -f9)
	fi
	python3 ${OTA} -i $${OTAIP} -p $${OTAPORT} -f ${BUILD}/*.ino.bin

find:
	avahi-browse -ptr  "_arduino._tcp" | grep = | cut -d\; -f4,8,9

monitor:
	stty ${BAUD} -F ${PORT} raw -echo
	cat ${PORT}

monitor-hex:
	stty ${BAUD} -F ${PORT} raw -echo
	cat ${PORT} | hexdump -C

list-boards:
	${ARDUINO} board listall