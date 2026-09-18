CACHE_USB := ${MKDIR}/.cache/devices/usb/$(subst ${PWD}/,,${SRC})
CACHE_OTA := ${MKDIR}/.cache/devices/ota/$(subst ${PWD}/,,${SRC})

USB_SCAN ?= python3 -c "import serial.tools.list_ports as p; [print(x.device, 'serial', x.description, x.hwid) for x in p.comports()]"
USB_RESULTS := ${MKDIR}/.cache/devices/usb.scan
OTA_RESULTS := ${MKDIR}/.cache/devices/ota.scan

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

# --- resolves OTA ip:port, interactively if needed ---
define _ota_resolve
	mkdir -p $(dir ${CACHE_OTA}); \
	if [ -f "${CACHE_OTA}" ]; then \
		SCAN_RESULT=$$(cat ${CACHE_OTA}); \
		OTAIP=$$(echo $$SCAN_RESULT | cut -d: -f1); \
		OTAPORT=$$(echo $$SCAN_RESULT | cut -d: -f2); \
		if ! python3 -c "import socket; s=socket.create_connection(('$$OTAIP',$$OTAPORT),timeout=2)" 2>/dev/null; then \
			$(WARN_S) "Device $$OTAIP:$$OTAPORT not reachable."; \
			printf "Delete saved config and rescan? [y/N] "; \
			read REPLY; \
			if [ "$$REPLY" = "y" ] || [ "$$REPLY" = "Y" ]; then \
				rm -f ${CACHE_OTA}; \
				$(INFO_S) "Cache cleared. Reconnect device and run again."; \
			fi; \
			exit 1; \
		fi; \
	else \
		$(INFO_S) "Scanning for OTA devices..."; \
		mkdir -p $(dir ${OTA_RESULTS}); \
		python3 ${MKDIR}/tools/scan.py 2>/dev/null > ${OTA_RESULTS}; \
		if [ ! -s ${OTA_RESULTS} ]; then \
			$(ERROR_S) "No OTA devices found."; \
			exit 1; \
		fi; \
		i=1; \
		while IFS= read -r line; do \
			printf "  %d) %s\n" $$i "$$line"; \
			i=$$((i+1)); \
		done < ${OTA_RESULTS}; \
		printf "Select device [1]: "; \
		read CHOICE; \
		CHOICE=$${CHOICE:-1}; \
		SCAN_RESULT=$$(sed -n "$${CHOICE}p" ${OTA_RESULTS}); \
		if [ -z "$$SCAN_RESULT" ]; then \
			$(ERROR_S) "Invalid selection."; \
			exit 1; \
		fi; \
		OTAIP=$$(echo $$SCAN_RESULT | cut -d: -f1); \
		OTAPORT=$$(echo $$SCAN_RESULT | cut -d: -f2); \
		echo "$$SCAN_RESULT" > ${CACHE_OTA}; \
		$(OK_S) "Saved: $$SCAN_RESULT"; \
	fi
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
	@python3 ${MKDIR}/tools/scan.py

forget-ota:
	rm -f ${CACHE_OTA}
	$(OK) "OTA device config cleared."

serve:
	cd ${SRC}/data && python3 -m http.server 8000
