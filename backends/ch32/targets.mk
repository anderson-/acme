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
	@echo "PREFIX: ${PREFIX}"
	@echo "LIB_DIRS: ${LIB_DIRS}"
	@echo "DEFINES_LIST: ${DEFINES_LIST}"

${FUN_MK}:
	mkdir -p ${MKDIR}/bin
	git clone --depth 1 https://github.com/cnlohr/ch32fun.git ${FUN}

${OBJ}: ${PROP} ${SRC}/${SKETCH}.c ${LOCAL_FILES} ${FUN_MK}
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
	$(MAKE) -f ${FUN_MK} -C ${BUILD} \
		TARGET_MCU=${MCU} TARGET=${SKETCH} PREFIX=${PREFIX} cv_flash

.PHONY: monitor
monitor: ${FUN_MK}
	$(MAKE) -f ${FUN_MK} -C ${BUILD} monitor

.PHONY: clean clean-build
clean clean-build:
	rm -rf ${BUILD}
	$(OK) "Build cache removed."
