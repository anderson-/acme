MKDIR ?= ${PWD}
include ${MKDIR}/utils.mk

.ONESHELL:
.DEFAULT_GOAL := shell
MAKEFLAGS += --no-print-directory

SHELL := /bin/bash
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

.PHONY: shell
shell:
	@if [ "$$ACME_SHELL_BASE" = "1" ]; then
		$(OK_S) "ACME shell already active."
		exit 0
	fi
	@ nix-shell ${MKDIR} && exit 0 || true
	$(ERROR) "nix-shell not found. See https://nixos.org/download"

.PHONY: arduino-shell
arduino-shell:
	@if [ "$$ACME_SHELL_ARDUINO" = "1" ]; then
		$(OK_S) "Arduino shell already active."
		exit 0
	fi
	@ nix-shell ${MKDIR}/backends/arduino && exit 0 || true
	$(ERROR) "nix-shell not found. See https://nixos.org/download"

SRC  ?= main
PROP ?= ${SRC}/project.yaml

ACME_BACKEND_DIR_arduino := ${MKDIR}/backends/arduino
ACME_SHELL_ACTIVE_arduino = ${ACME_SHELL_ARDUINO}

# With no explicit goal, include the backend so Make can expose its targets to
# completion and then follow the default `shell` target.
ifeq ($(MAKECMDGOALS),)
  include ${MKDIR}/backends/arduino/backend.mk
else
  ACME_PROJECT_GOALS := $(strip $(filter-out shell arduino-shell,$(MAKECMDGOALS)))

  ifneq ($(ACME_PROJECT_GOALS),)
    ACME_PLATFORM ?= $(strip $(shell sed -n 's/^[[:space:]]*platform:[[:space:]]*//p' "${PROP}" 2>/dev/null))
    ACME_PLATFORM := $(if $(ACME_PLATFORM),$(ACME_PLATFORM),arduino)
    ACME_BACKEND_DIR := $(ACME_BACKEND_DIR_${ACME_PLATFORM})
    ACME_SHELL_ACTIVE := $(ACME_SHELL_ACTIVE_${ACME_PLATFORM})

    ifeq ($(ACME_BACKEND_DIR),)
      $(error Unknown platform '${ACME_PLATFORM}' in ${PROP})
    endif

    ifneq ($(ACME_SHELL_ACTIVE),)
      include ${ACME_BACKEND_DIR}/backend.mk
    else
      .PHONY: _acme-platform-shell $(ACME_PROJECT_GOALS)
      $(ACME_PROJECT_GOALS): _acme-platform-shell ;

      _acme-platform-shell:
	@ nix-shell ${ACME_BACKEND_DIR} --command '$(MAKE) $(MAKECMDGOALS); return'
    endif
  endif
endif
