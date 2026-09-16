MKDIR ?= ${PWD}
SRC   ?= main
include ${MKDIR}/utils.mk

.DEFAULT_GOAL := shell
MAKEFLAGS += --no-print-directory

SHELL := /bin/bash

.PHONY: shell
shell:
	@$(MAKE) targets
	@if [ "$$ACME_SHELL_BASE" = "1" ]; then \
		$(OK_S) "ACME shell already active."; \
	else \
		nix-shell ${MKDIR} || $(ERROR_S) "nix-shell not found. See https://nixos.org/download"; \
	fi

PROJECTS   := $(wildcard examples/*/project.yaml examples/*/*/project.yaml ${SRC}/project.yaml)
NAMES      := $(subst /,-,$(patsubst %/project.yaml,%.mk,$(PROJECTS)))
NAMES      := $(subst examples-,,$(addprefix .cache/mk/,$(NAMES)))
CURRENT    := $(wildcard .cache/mk/*.mk)
CURRENT    := $(filter-out $(NAMES),$(CURRENT))
TO_RM      := $(addsuffix .rm,$(CURRENT))

define MK
.cache/mk/$(2).mk: $(1) | .cache/mk
	@python3 backends/$$$$(grep platform $(1) | cut -d ':' -f 2 | tr -d ' ' | grep . || echo arduino)/gen.py "$$@" "$(1)" "$(2)" "$(3)"

-include .cache/mk/$(2).mk
endef

$(foreach p,$(PROJECTS),$(eval $(call MK,$(p),$(subst examples-,,$(subst /,-,$(patsubst %/project.yaml,%,$(p)))),$(patsubst %/project.yaml,%,$(p)))))

.cache/mk:
	mkdir -p .cache/mk

define RM
$(1):
	@rm $(patsubst %.rm,%,$(1))
endef

$(foreach f,$(TO_RM),$(eval $(call RM,$(f))))

_build_targets: $(NAMES)

_remove_targets: $(TO_RM)

.PHONY: targets
targets: _build_targets _remove_targets

.PHONY: clean-targets
clean-targets:
	rm -rf .cache/mk

include ${MKDIR}/device.mk
