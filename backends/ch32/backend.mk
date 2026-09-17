MKDIR ?= ${PWD}

FUN := ${MKDIR}/bin/ch32fun
FUN_MK := ${FUN}/ch32fun/ch32fun.mk
BUILD := ${MKDIR}/.cache/build/ch32/${SRC}
OBJ := ${BUILD}/${SKETCH}.bin

RWC = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) $(filter $(subst *,%,$2),$d))
LOCAL_FILES := $(foreach lib,${LIB_DIRS},$(call RWC,${PWD}/$(lib),*.c *.h))
LOCAL_NAMES := $(filter %.c,$(foreach file,${LOCAL_FILES},$(notdir $(file))))

include ${MKDIR}/backends/ch32/targets.mk
