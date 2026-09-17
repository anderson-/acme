.PHONY: _checksrc
_checksrc:
	@if [ ! -f "${SRC}/${SKETCH}.c" ]; then
		$(ERROR_S) "'${SRC}/${SKETCH}.c' not found (not a ch32fun project)."
		exit 1
	fi

.PHONY: fields
fields:
	@echo "=== Project Fields ==="
	@echo "MCU: ${MCU}"
	@echo "CH32FUN_REF: ${CH32FUN_REF}"
	@echo "TOOLCHAIN_VERSION: ${TOOLCHAIN_VERSION}"
	@echo "PROGRAMMER: ${PROGRAMMER}"
	@echo "PREFIX: ${PREFIX}"
	@echo "LIB_DIRS: ${LIB_DIRS}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"

${FUN_CHECKOUT}:
	mkdir -p $(dir ${FUN})
	git clone --filter=blob:none --no-checkout ${CH32FUN_REPOSITORY} ${FUN}
	git -C ${FUN} fetch --depth 1 origin "${CH32FUN_REF}"
	git -C ${FUN} checkout --detach FETCH_HEAD
	touch $@

${TOOLCHAIN_READY}: ${MKDIR}/backends/ch32/toolchain.py
	python3 ${MKDIR}/backends/ch32/toolchain.py \
		--version "${TOOLCHAIN_VERSION}" --output "${TOOLCHAIN}"

${OBJ}: ${PROP} ${SRC}/${SKETCH}.c ${LOCAL_FILES} ${FUN_CHECKOUT} ${TOOLCHAIN_READY}
	mkdir -p ${BUILD}
	find ${SRC} -maxdepth 1 -type f -exec cp -f {} ${BUILD}/ \;
	$(foreach file,${LOCAL_FILES},cp -f "$(file)" ${BUILD}/;)
	printf '%s\n' '#ifndef _FUNCONFIG_H' '#define _FUNCONFIG_H' > ${BUILD}/funconfig.h
	for define in ${DEFINES_LIST}; do
		echo "#define $$define" | tr '=' ' ' >> ${BUILD}/funconfig.h
	done
	echo '#endif' >> ${BUILD}/funconfig.h
	$(MAKE) -f ${FUN_MK} -C ${BUILD} \
		TARGET_MCU=${MCU} TARGET=${SKETCH} PREFIX=${PREFIX} \
		ADDITIONAL_C_FILES="${LOCAL_NAMES}" ${SKETCH}.bin

.PHONY: build
build: _checksrc ${OBJ}
	$(OK) "Build successful!"

.PHONY: flash
flash: build
ifeq (${PROGRAMMER},esp32)
	$(call _usb_resolve)
	python3 ${MKDIR}/tools/ch32_flash.py --port "$$PORT" --bin "${OBJ}" --reset
else ifeq (${PROGRAMMER},minichlink)
	$(MAKE) -f ${FUN_MK} -C ${BUILD} \
		TARGET_MCU=${MCU} TARGET=${SKETCH} PREFIX=${PREFIX} cv_flash
else
	$(error Unsupported CH32 programmer '${PROGRAMMER}'; expected esp32 or minichlink)
endif

.PHONY: monitor
monitor: build
ifeq (${PROGRAMMER},esp32)
	$(call _usb_resolve)
	python3 ${MKDIR}/tools/ch32_flash.py --port "$$PORT" --monitor
else
	$(MAKE) -f ${FUN_MK} -C ${BUILD} monitor
endif

.PHONY: clean clean-build
clean clean-build:
	rm -rf ${BUILD}
	$(OK) "Build cache removed."

.PHONY: clean-bin
clean-bin:
	@if [ "${CONFIRM}" != "yes" ]; then
		$(ERROR_S) "Run: make clean-bin CONFIRM=yes"
		exit 1
	fi
	rm -rf ${FUN} ${TOOLCHAIN}
	$(OK) "CH32 toolchain and ch32fun ${CH32FUN_REF} removed."
