.PHONY: _checksrc
_checksrc:
	@if [ ! -f "${SRC}/CMakeLists.txt" ]; then
		$(ERROR_S) "'${SRC}/CMakeLists.txt' not found (not an ESP-IDF project)."
		exit 1
	fi
	@if [ ! -f "${IDF_ROOT}/export.sh" ]; then
		$(ERROR_S) "ESP-IDF not found at '${IDF_ROOT}'. Set idf_path in ${PROP}."
		exit 1
	fi

define _idf_env
	unset PYTHONPATH
	source "${IDF_ROOT}/export.sh" >/dev/null
	export EXTRA_CFLAGS="${DEFINES}"
	export EXTRA_CXXFLAGS="${DEFINES}"
endef

.PHONY: fields
fields:
	@echo "=== Project Fields ==="
	@echo "TARGET: ${TARGET}"
	@echo "IDF_ROOT: ${IDF_ROOT}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"
	@echo "BAUD: ${BAUD}"

.PHONY: build
build: _checksrc
	mkdir -p ${BUILD}
	rm -f ${LOG}
	$(call _idf_env)
	idf.py -B "${BUILD}" -D IDF_TARGET="${TARGET}" build 2>&1 | tee ${LOG}
	test $${PIPESTATUS[0]} -eq 0
	$(OK) "Build successful!"

.PHONY: flash
flash: build
	$(call _usb_resolve)
	$(call _idf_env)
	idf.py -B "${BUILD}" -p "$$PORT" flash

.PHONY: monitor
monitor: _checksrc
	$(call _usb_resolve)
	$(call _idf_env)
	idf.py -B "${BUILD}" -p "$$PORT" monitor

.PHONY: clean clean-build
clean clean-build:
	rm -rf ${BUILD}
	$(OK) "Build cache removed."
