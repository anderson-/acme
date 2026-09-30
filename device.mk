CACHE_USB := ${MKDIR}/.cache/devices/usb/$(subst ${PWD}/,,${SRC})
CACHE_OTA := ${MKDIR}/.cache/devices/ota/$(subst ${PWD}/,,${SRC})

USB_SCAN ?= python3 -c "import serial.tools.list_ports as p; [print(x.device, 'serial', x.description, x.hwid) for x in p.comports()]"
USB_RESULTS := ${MKDIR}/.cache/devices/usb.scan
OTA_PORT ?= 80
OTA_PATH ?= /update
OTA_FS_PATH ?= /update-fs
OTA_DISCOVERY_TIMEOUT ?= 5
OTA_TIMEOUT ?= 120

# --- resolves USB port, interactively if needed ---
define _usb_resolve
	mkdir -p $(dir ${CACHE_USB}); \
	if [ -f "${CACHE_USB}" ]; then \
		PORT=$$(cat ${CACHE_USB}); \
		if [ ! -e "$$PORT" ]; then \
			$(WARN_S) "Device $$PORT not found."; \
			printf "Delete saved config and rescan? [y/N] "; \
			read REPLY; \
			if [ "$$REPLY" = "y" ] || [ "$$REPLY" = "Y" ]; then \
				rm -f ${CACHE_USB}; \
				$(INFO_S) "Cache cleared. Reconnect device and run again."; \
			fi; \
			exit 1; \
		fi; \
	else \
		$(INFO_S) "Scanning USB devices..."; \
		mkdir -p $(dir ${USB_RESULTS}); \
		${USB_SCAN} > ${USB_RESULTS}; \
		if [ ! -s ${USB_RESULTS} ]; then \
			$(ERROR_S) "No USB devices found."; \
			exit 1; \
		fi; \
		i=1; \
		while IFS= read -r line; do \
			printf "  %d) %s\n" $$i "$$line"; \
			i=$$((i+1)); \
		done < ${USB_RESULTS}; \
		printf "Select device [1]: "; \
		read CHOICE; \
		CHOICE=$${CHOICE:-1}; \
		PORT=$$(sed -n "$${CHOICE}p" ${USB_RESULTS} | tr -s ' ' | cut -d' ' -f1); \
		if [ -z "$$PORT" ]; then \
			$(ERROR_S) "Invalid selection."; \
			exit 1; \
		fi; \
		echo "$$PORT" > ${CACHE_USB}; \
		$(OK_S) "Saved: $$PORT"; \
	fi
endef

# --- shared HTTP OTA discovery and upload ---
define _ota_resolve
	OTA_URL=$$(python3 "${MKDIR}/tools/ota.py" \
		--cache "${CACHE_OTA}" --host "${OTAIP}" \
		--port "$(if ${OTAPORT},${OTAPORT},${OTA_PORT})" \
		--timeout "${OTA_DISCOVERY_TIMEOUT}") || exit 1
endef

define _ota_upload
	$(call _ota_resolve)
	$(INFO_S) "OTA upload to $$OTA_URL$(2)..."
	curl --fail --show-error --noproxy '*' --connect-timeout 5 \
		--max-time "${OTA_TIMEOUT}" --header 'Content-Type: application/octet-stream' --header 'Expect:' \
		--data-binary @"$(1)" "$$OTA_URL$(2)"
endef

.PHONY: resolve-usb list-usb forget-usb scan forget-ota serve
resolve-usb:
	$(call _usb_resolve)

list-usb:
	@${USB_SCAN}

forget-usb:
	rm -f ${CACHE_USB}
	$(OK) "USB device config cleared."

scan:
	@python3 "${MKDIR}/tools/scan.py" --timeout "${OTA_DISCOVERY_TIMEOUT}"

forget-ota:
	rm -f ${CACHE_OTA}
	$(OK) "OTA device config cleared."

serve:
	cd ${SRC}/data && python3 -m http.server 8000
