# Preserve wifi.yaml selected by acme.mk or by the caller.
WIFI ?= ${MKDIR}/wifi.yaml
WIFI_FLAGS :=

ifneq (,$(filter STASSID STASSID=% STAPSK STAPSK=%,${DEFINES_LIST}))
$(warning Wi-Fi defines found in project.yaml; prefer wifi.yaml to avoid duplicate STASSID/STAPSK definitions)
endif

ifneq (,$(wildcard ${WIFI}))
WIFI_READABLE := $(shell yq '.' "${WIFI}" >/dev/null 2>&1 && echo yes)
ifneq (${WIFI_READABLE},yes)
$(error Cannot read ${WIFI}; check YAML syntax and that yq is installed)
endif
# Compatible with both python-yq and yq-go.
SSID := $(shell yq -r '.ssid // ""' "${WIFI}")
PSK := $(shell yq -r '.psk // ""' "${WIFI}")
ifneq (,$(strip ${SSID}))
WIFI_FLAGS := -DSTASSID=\"$(SSID)\" -DSTAPSK=\"$(PSK)\"
else
$(warning ${WIFI} has an empty SSID; building without Wi-Fi defines)
endif
endif

# Compare file content on every build, including the missing-file state.
# Keep this outside BUILD, which IDF removes when configuration changes.
WIFI_STATE := ${BUILD}.wifi-state
.PHONY: _check_wifi_state
_check_wifi_state:

${WIFI_STATE}: _check_wifi_state
	@mkdir -p "$(dir ${WIFI_STATE})"
	@{ printf '%s\n' "${WIFI}"; \
		if [ -f "${WIFI}" ]; then cksum < "${WIFI}"; else echo missing; fi; \
	} > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"
