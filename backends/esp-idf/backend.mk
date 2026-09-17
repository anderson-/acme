MKDIR ?= ${PWD}

BUILD := ${MKDIR}/.cache/build/esp-idf/${SRC}
LOG := ${BUILD}/build.log
DEFINES := $(foreach def,${DEFINES_LIST},-D$(def))

USB_SCAN := python3 -c "import serial.tools.list_ports as p; [print(x.device, 'serial', x.description, x.hwid) for x in p.comports()]"

include ${MKDIR}/backends/esp-idf/targets.mk
