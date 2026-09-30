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
	@echo "=== Project Fields ==="
	@echo "FQBN: ${FQBN}"
	@echo "CORE: ${CORE}"
	@echo "DEPENDENCIES: ${DEPENDENCIES}"
	@echo "LIB_DIRS: ${LIB_DIRS}"
	@echo "INJECT: ${INJECT}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"
	@echo "BAUD: ${BAUD}"
	@echo "FILESYSTEM: ${FS}"
	@echo "FLAGS: ${FLAGS}"

# --- arduino-cli config ---
${CFG}: ${MKDIR}/arduino-cli.yaml
	$(INFO) "Setting up arduino-cli config..."
	mkdir -p "$(dir ${CFG})" "${ALIBS}"
	ADATA="${ADATA}" ALIBS="${ALIBS}" yq \
		'.directories.data = strenv(ADATA) | .directories.user = strenv(ALIBS) | del(.arduino_data)' \
		"${MKDIR}/arduino-cli.yaml" > "${CFG}"

# --- core install ---
${ADATA}/package_index.json: ${CFG}
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
	@yq -r '(.dependencies // [])[]' "${PROP}" 2>/dev/null | while IFS= read -r LIB; do
		NAME=$$(echo $$LIB | cut -d@ -f1)
		VERSION=$$(echo $$LIB | cut -d@ -f2)
		if [ "$$VERSION" = "$$NAME" ]; then
			VERSION=""
		fi
		if [[ "$$NAME" == *.git* ]]; then
			URL=$$NAME
			BRANCH=
			if [[ $$URL == *#* ]]; then
				BRANCH=$${URL##*#}
				URL=$${URL%%#*}
			fi
			LIB_NAME=$$(basename $$URL .git)
			LIB_DIR="${ALIBS}/libraries/$$LIB_NAME"
			if [ ! -e $$LIB_DIR ]; then
				$(INFO_S) "Cloning $$LIB_NAME$${BRANCH:+ (branch $$BRANCH)}..."
				git clone --depth 1 $${BRANCH:+--branch $$BRANCH} $$URL $$LIB_DIR
			fi
		else
			if [ ! -e "${ALIBS}/libraries/$$NAME" ]; then
				$(INFO_S) "Installing $$NAME$${VERSION:+@$$VERSION}..."
				${ARDUINO} lib install "$${VERSION:+$$NAME@$$VERSION}$${VERSION:-$$NAME}"
			else
				IVER=$$(grep version "${ALIBS}/libraries/$$NAME/library.properties" | cut -d= -f2)
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

${STAMP_BUILD}: ${STAMP_LIBS} ${ADATA}/packages/${CORE} ${FILES} ${WIFI_STATE} ${PROP} \
	${MKDIR}/backends/arduino/backend.mk ${MKDIR}/backends/arduino/targets.mk \
	${MKDIR}/backends/wifi.mk $(wildcard ${MKDIR}/libraries/AcmeOTA/src/*) | ${BUILD}
	@ $(MAKE) _checksrc
	@ $(foreach sym,$(INJECT), if [ -d "${PWD}/$(sym)" ]; \
	    then ln -sf ${PWD}/$(sym)/* ${PWD}/${SRC}; \
	    else ln -sf ${PWD}/$(sym) ${PWD}/${SRC}/; \
	    fi &&) true
	echo "LOG=${LOG} BUILD=${BUILD}"
	@ mkdir -p ${BUILD}; rm -f ${LOG}; touch ${LOG}; \
	$(call file_spinner,${LOG},Building ${SKETCH}...) & WATCH_PID=$$!; \
	trap "kill -- -$$WATCH_PID 2>/dev/null; wait $$WATCH_PID 2>/dev/null; printf '\r\033[K' >&2" EXIT INT TERM; \
	CMD="${ARDUINO} compile --fqbn ${FQBN} \
		$(foreach lib,$(LIB_DIRS),--libraries ${PWD}/$(lib)) \
		--libraries \"${MKDIR}/libraries\" \
		--build-property 'runtime.tools.ctags.path=${CTAGS_PATH}' \
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

# --- filesystem ---
${BUILD}/img.bin: ${STAMP_BUILD} ${SRC}/data/*
	SIZE=$$(grep -E 'spiffs|littlefs' ${BUILD}/partitions.csv | cut -d, -f5)
	$(INFO_S) "Building ${FS} image ($$SIZE bytes)..."
	time ${MKFS_TOOL} -c ${SRC}/data -s $${SIZE} ${BUILD}/img.bin

.PHONY: fs
fs: ${BUILD}/img.bin

.PHONY: flash-fs
flash-fs: ${BUILD}/img.bin
	$(call _usb_resolve)
	OFFSET=$$(grep -E 'spiffs|littlefs' ${BUILD}/partitions.csv | cut -d, -f4)
	$(INFO_S) "Flashing ${FS} image (offset $${OFFSET}) to $$PORT..."
	time ${ESPTOOL} -p $$PORT write_flash $${OFFSET} ${BUILD}/img.bin

# --- OTA ---
.PHONY: ota
ota: ${STAMP_BUILD}
	$(call _ota_upload,${BUILD}/${SKETCH}.ino.bin,${OTA_PATH})

.PHONY: ota-fs
ota-fs: ${BUILD}/img.bin
	$(call _ota_upload,${BUILD}/img.bin,${OTA_FS_PATH})

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
# --- board info ---
.PHONY: list-boards
list-boards:
	${ARDUINO} board listall

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
