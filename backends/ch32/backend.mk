MKDIR ?= ${PWD}

CH32FUN_REPOSITORY ?= https://github.com/cnlohr/ch32fun.git
FUN := ${MKDIR}/bin/ch32fun/${CH32FUN_REF_KEY}
FUN_CHECKOUT := ${FUN}/.acme-checkout
FUN_MK := ${FUN}/ch32fun/ch32fun.mk
TOOLCHAIN := ${MKDIR}/bin/ch32-toolchain/${TOOLCHAIN_VERSION}
TOOLCHAIN_READY := ${TOOLCHAIN}/.acme-installed
PREFIX := $(if ${PREFIX_OVERRIDE},${PREFIX_OVERRIDE},${TOOLCHAIN}/bin/riscv-none-elf)
BUILD := ${MKDIR}/.cache/build/ch32/${SRC}
OBJ := ${BUILD}/${SKETCH}.bin

RWC = $(foreach d,$(wildcard $1*),$(call RWC,$d/,$2) $(filter $(subst *,%,$2),$d))
LOCAL_FILES := $(foreach lib,${LIB_DIRS},$(call RWC,${PWD}/$(lib),*.c *.h))
LOCAL_NAMES := $(filter %.c,$(foreach file,${LOCAL_FILES},$(notdir $(file))))

include ${MKDIR}/backends/ch32/targets.mk
