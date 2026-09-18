MKDIR ?= ${PWD}
SRC   ?= main
include ${MKDIR}/utils.mk

.ONESHELL:
.DEFAULT_GOAL := shell
MAKEFLAGS += --no-print-directory

SHELL := /bin/bash

# This recipe is deliberately one backslash-joined shell line. macOS ships GNU
# Make 3.81, which predates .ONESHELL (3.82+), so a multi-line recipe here would
# run line-by-line and fail with "syntax error: unexpected end of file".
# From here on we use the GNU Make bundled by nix-shell (see shell.nix).
.PHONY: shell
shell:
	@if [ "$$ACME_SHELL_BASE" = "1" ]; then \
		$(MAKE) targets; \
		$(OK_S) "ACME shell already active."; \
	else \
		nix-shell ${MKDIR} --run 'make targets'; \
		STATUS=$$?; \
		if [ "$$STATUS" -ne 0 ]; then \
			$(ERROR_S) "nix-shell not found. See https://nixos.org/download"; \
			exit "$$STATUS"; \
		fi; \
	fi

ifeq ($(ACME_SHELL_BASE),1)

PROJECT_FILES := $(wildcard examples/*/project.yaml examples/*/*/project.yaml ${SRC}/project.yaml)
TARGET_FILES  := $(subst /,-,$(patsubst %/project.yaml,%.mk,$(PROJECT_FILES)))
TARGET_FILES  := $(subst examples-,,$(addprefix .cache/mk/,$(TARGET_FILES)))
CACHED_TARGET_FILES := $(wildcard .cache/mk/*.mk)
STALE_TARGET_FILES  := $(filter-out $(TARGET_FILES),$(CACHED_TARGET_FILES))
STALE_TARGETS       := $(addsuffix .rm,$(STALE_TARGET_FILES))

define GENERATE_TARGET_FILE
.cache/mk/$(2).mk: $(1) ${MKDIR}/backends/generate.py ${MKDIR}/backends/generator.py $(wildcard ${MKDIR}/backends/*/gen.py) | .cache/mk
	@python3 ${MKDIR}/backends/generate.py "$$@" "$(1)" "$(notdir $(3))" "$(3)"

-include .cache/mk/$(2).mk
endef

$(foreach project,$(PROJECT_FILES),$(eval $(call GENERATE_TARGET_FILE,$(project),$(subst examples-,,$(subst /,-,$(patsubst %/project.yaml,%,$(project)))),$(patsubst %/project.yaml,%,$(project)))))

.cache/mk:
	mkdir -p .cache/mk

define REMOVE_STALE_TARGET_FILE
$(1):
	@rm $(patsubst %.rm,%,$(1))
endef

$(foreach target,$(STALE_TARGETS),$(eval $(call REMOVE_STALE_TARGET_FILE,$(target))))

_build_targets: $(TARGET_FILES)

_remove_stale_targets: $(STALE_TARGETS)

.PHONY: targets
targets: _build_targets _remove_stale_targets

.PHONY: clean-targets
clean-targets:
	rm -rf .cache/mk

endif

include ${MKDIR}/device.mk
