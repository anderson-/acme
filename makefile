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
ADATA   := ${MKDIR}/bin/.arduino15
ALIBS   := ${MKDIR}/bin
CFG     ?= ${ADATA}/arduino-cli.yaml
ARDUINO := ${MKDIR}/bin/arduino-cli
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
BUILD   ?= ${MKDIR}/.build/${CORE}/${SRC}
OBJ     := ${BUILD}/*.ino.elf
OTAIP   ?=
OTAPORT ?=
BAUD    ?= 115200
PORT    ?= /dev/ttyUSB0
VENV    := . ${MKDIR}/.venv/bin/activate
PY      := ${VENV} && python3
OTA     := ${MKDIR}/bin/.arduino15/packages/${CORE}/hardware/${CORE}/*/tools/espota.py
MKFS    := ${MKDIR}/bin/.arduino15/packages/${CORE}/tools/mkspiffs/*/mkspiffs

.ONESHELL:

shell:
	nix-shell ${MKDIR}

${MKDIR}/.venv:
	virtualenv -p python3 ${MKDIR}/.venv

${MKDIR}/bin:
	${MAKE} deps
	if [ -e ${RAMFS} ]; then
		mkdir -p ${RAMDISK}/bin
		ln -s ${RAMDISK}/bin ${MKDIR}/bin
	else
		mkdir -p ${MKDIR}/bin
	fi
	mkdir -p ${ADATA}
	ADATA=${ADATA}; ADATA=$${ADATA//\//\\\/}; \
	sed "s/arduino_data.*/arduino_data: $${ADATA}/g" \
		${MKDIR}/arduino-cli.yaml > ${CFG}
	ALIBS=${ALIBS}; ALIBS=$${ALIBS//\//\\\/}; \
	sed -i "s/  user:.*/  user: $${ALIBS}/g" ${CFG}

${MKDIR}/bin/arduino-cli: ${MKDIR}/bin
	curl -fsSL ${AC_TAR} | tar -xz -C ${MKDIR}/bin/
	touch ${MKDIR}/bin/arduino-cli

${ADATA}/package_index.json: ${MKDIR}/bin/arduino-cli
	${ARDUINO} core update-index
	touch ${ADATA}/package_index.json

${ADATA}/packages/${CORE}: ${ADATA}/package_index.json
	${MAKE} checksrc
	${ARDUINO} core install ${CORE}:${CORE}
	touch ${ADATA}/packages/${CORE}

.PHONY: checksrc
checksrc:
	@ if [ ! -d "${SRC}" ]; then \
		echo -e "\033[31m>>> Source directory ${SRC} not found! Set SRC variable to point to your sketch directory.\033[0m"; \
		exit 1; \
	fi

.PHONY: core
core: ${ADATA}/packages/${CORE}

${BUILD}:
	if [ -e ${RAMFS} ]; then
		mkdir -p ${RAMDISK}/build
		ln -s ${RAMDISK}/build ${MKDIR}/${BUILD}
	else
		mkdir -p ${MKDIR}/${BUILD}
	fi

${MKDIR}/.libs-downloaded: ${PROP}
	@ ${VENV} && cat ${PROP} | yq -Y .libs | sed 's/- //' | \
		while read LIB; do
			NAME=$$(echo $$LIB | cut -d@ -f1)
			VERSION=$$(echo $$LIB | cut -d@ -f2)
			
			# Check if this is a git URL
			if [[ "$$NAME" == *.git ]]; then
				# Handle git repository
				REPO_URL=$$NAME
				LIB_NAME=$$(basename $$REPO_URL .git)
				LIB_DIR="${MKDIR}/bin/libraries/$$LIB_NAME"
				
				if [ ! -e $$LIB_DIR ]; then
					echo "Cloning $$REPO_URL@$$VERSION to $$LIB_NAME" >&2
					git clone --depth 1 $$REPO_URL $$LIB_DIR
				else
					# Check if we need to update version
					if [ -f $$LIB_DIR/library.properties ]; then
						IVER=$$(cat $$LIB_DIR/library.properties | grep version | cut -d= -f2)
						if [ ! "$$IVER" = "$$VERSION" ]; then
							echo "Updating $$LIB_NAME from $$IVER to $$VERSION" >&2
							cd $$LIB_DIR && git fetch
						else 
							echo "Library $$LIB_NAME already @$$VERSION"
						fi
					else
						echo "No library.properties, just checkout the version"
						cd $$LIB_DIR && git fetch
					fi
				fi
			else
				# Handle regular arduino library
				if [ ! -e ${MKDIR}/bin/libraries/$$NAME ]; then
					echo "Installing $$NAME@$$VERSION" >&2
					${ARDUINO} lib install $$NAME@$$VERSION
				else
					IVER=$$(cat ${MKDIR}/bin/libraries/$$NAME/library.properties \
						| grep version | cut -d= -f2)
					if [ ! "$$IVER" = "$$VERSION" ]; then
						echo "Updating $$NAME from $$IVER to $$VERSION" >&2
						${ARDUINO} lib install $$NAME@$$VERSION
					fi
				fi
			fi
			
			# Apply special fixes if needed
			if [ "$$NAME" = "Tiny4kOLED" ] || [ "$$LIB_NAME" = "Tiny4kOLED" ]; then
				# find string avr/pgmspace in files and replace with pgmspace
				find ${MKDIR}/bin/libraries/$$NAME -type f -exec sed -i \
					-e 's/avr\/pgmspace/pgmspace/g' {} \;
			fi
		done
	@ touch ${MKDIR}/.libs-downloaded

.PHONY: download-libs
download-libs: ${MKDIR}/.libs-downloaded

${OBJ}: download-libs ${BUILD} ${ADATA}/packages/${CORE} ${FILES} ${WIFI}
	${MAKE} checksrc
	time ${ARDUINO} compile --fqbn ${FQBN} \
		$(foreach include, \
	 		$(shell ${VENV} && cat ${PROP} | yq -Y .include | tr -d ' -'), \
	 	  --libraries $(include)) \
	 	--build-property 'compiler.cpp.extra_flags=${FLAGS}' \
	 	--build-path ${BUILD} \
	 	${SRC} -v

.PHONY: build
build: ${OBJ}

.PHONY: deps
deps: ${MKDIR}/.venv

flash: ${OBJ}
	time ${ARDUINO} upload -p ${PORT} --fqbn ${FQBN} -i ${OBJ} ${SRC} -v

.PHONY: clean
clean:
	rm -rf ${BUILD}

.PHONY: clean-all
clean-all: clean
	rm -rf ${MKDIR}/bin
	rm -rf ${MKDIR}/.build
	if [ -e ${RAMFS} ]; then
		rm -rf ${MKDIR}/${RAMDISK}
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

list-boards:
	${ARDUINO} board listall

list-usb:
	${ARDUINO} board list

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

cat-serial:
	python3 -m serial.tools.miniterm --exit-char 3 ${PORT} ${BAUD}

serve-html:
	cd ${SRC}/data && python3 -m http.server 8000

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

$(foreach example,$(EXAMPLE_NAMES),$(eval $(call EXAMPLE_TARGETS,$(example))))