.PHONY: _checksrc
_checksrc:
	@if [ ! -d "${SRC}" ]; then
		$(ERROR_S) "Source directory '${SRC}' not found."
		exit 1
	fi
	@if [ ! -f "${PARTITIONS}" ]; then
		$(ERROR_S) "Unsupported flash_size '${FLASH_SIZE}'. Expected 4MB, 8MB, or 16MB."
		exit 1
	fi

.PHONY: fields
fields:
	@echo "=== Project Fields ==="
	@echo "TARGET: ${TARGET}"
	@echo "IDF_VERSION: ${IDF_VERSION}"
	@echo "IDF_ROOT: ${IDF_ROOT}"
	@echo "IDF_TOOLS_PATH: ${IDF_TOOLS_PATH}"
	@echo "FLASH_SIZE: ${FLASH_SIZE}"
	@echo "FILESYSTEM: ${FS}"
	@echo "COMPONENTS: ${COMPONENTS_LIST}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"
	@echo "BAUD: ${BAUD}"

${IDF_CHECKOUT}:
	mkdir -p $(dir ${IDF_ROOT})
	git clone --filter=blob:none --no-checkout ${IDF_REPOSITORY} ${IDF_ROOT}
	git -C ${IDF_ROOT} fetch --depth 1 origin "${IDF_VERSION}"
	git -C ${IDF_ROOT} checkout --detach FETCH_HEAD
	git -C ${IDF_ROOT} submodule update --init --recursive --depth 1
	touch $@

${IDF_INSTALLED}: ${IDF_CHECKOUT}
	mkdir -p ${IDF_TOOLS_PATH}
	IDF_TOOLS_PATH="${IDF_TOOLS_PATH}" ${IDF_ROOT}/install.sh ${TARGET}
	touch $@

${STAGE}/CMakeLists.txt: ${PROP} ${PARTITIONS} ${MKDIR}/backends/idf/project.py $(wildcard ${SRC}/*) $(wildcard ${SRC}/data/*)
	python3 ${MKDIR}/backends/idf/project.py \
		--source "${SRC}" --output "${STAGE}" --name "${SKETCH}" \
		--target "${TARGET}" --flash-size "${FLASH_SIZE}" \
		--filesystem "${FS}" --partitions "${PARTITIONS}" \
		--components "${COMPONENTS_LIST}" \
		--sdkconfig "${SDKCONFIG_LIST}"

.PHONY: build
build: _checksrc ${IDF_INSTALLED} ${PROJECT_READY} ${CONFIG_STAMP}
	mkdir -p ${BUILD}
	rm -f ${LOG}
	$(call _idf_env)
	idf.py -C "${PROJECT_DIR}" -B "${BUILD}" -D IDF_TARGET="${TARGET}" build 2>&1 | tee ${LOG}
	STATUS=$${PIPESTATUS[0]}
	if [ "$$STATUS" -ne 0 ]; then exit "$$STATUS"; fi
	test -f ${OBJ}
	$(OK) "Build successful!"

.PHONY: flash
flash: build
	$(call _usb_resolve)
	$(call _idf_env)
	idf.py -C "${PROJECT_DIR}" -B "${BUILD}" -p "$$PORT" flash

.PHONY: monitor
monitor: build
	$(call _usb_resolve)
	$(call _idf_env)
	idf.py -C "${PROJECT_DIR}" -B "${BUILD}" -p "$$PORT" monitor

.PHONY: fs
fs: build
	@test -f "${FS_OBJ}" || { $(ERROR_S) "No data/ filesystem image was generated."; exit 1; }
	$(OK) "Filesystem image ready: ${FS_OBJ}"

.PHONY: flash-fs
flash-fs: fs
	$(call _usb_resolve)
	$(call _idf_env)
	idf.py -C "${PROJECT_DIR}" -B "${BUILD}" -p "$$PORT" storage-flash

.PHONY: ota
ota: build
	$(call _ota_resolve)
	PORT=$${OTAPORT:-${OTA_PORT}}
	$(INFO_S) "OTA firmware to $$OTAIP:$$PORT${OTA_PATH}..."
	curl --fail --show-error --data-binary @"${OBJ}" "http://$$OTAIP:$$PORT${OTA_PATH}"

.PHONY: ota-fs
ota-fs: fs
	$(call _ota_resolve)
	PORT=$${OTAPORT:-${OTA_PORT}}
	$(INFO_S) "OTA filesystem to $$OTAIP:$$PORT${OTA_FS_PATH}..."
	curl --fail --show-error --data-binary @"${FS_OBJ}" "http://$$OTAIP:$$PORT${OTA_FS_PATH}"

.PHONY: clean clean-build
clean clean-build:
	rm -rf ${BUILD} ${STAGE}
	$(OK) "Build cache removed."

.PHONY: clean-bin
clean-bin:
	@if [ "${CONFIRM}" != "yes" ]; then
		$(ERROR_S) "Run: make clean-bin CONFIRM=yes"
		exit 1
	fi
	rm -rf ${IDF_ROOT} ${IDF_TOOLS_PATH}
	$(OK) "ESP-IDF ${IDF_VERSION} and its tools removed."
