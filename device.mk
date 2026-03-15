CACHE_USB := ${MKDIR}/.cache/usb/${SRC}
CACHE_OTA := ${MKDIR}/.cache/ota/${SRC}

BAUD := $(shell yq -r '.baudrate // "115200"' "${PROP}" 2>/dev/null)

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
		ARDUINO_DATA_DIR=${ADATA} arduino-cli --config-file ${CFG} board list 2>/dev/null \
			| grep -v "^Port" | grep "serial" > /tmp/acme-usb; \
		if [ ! -s /tmp/acme-usb ]; then \
			$(ERROR_S) "No USB devices found."; \
			exit 1; \
		fi; \
		i=1; \
		while IFS= read -r line; do \
			printf "  %d) %s\n" $$i "$$line"; \
			i=$$((i+1)); \
		done < /tmp/acme-usb; \
		printf "Select device [1]: "; \
		read CHOICE; \
		CHOICE=$${CHOICE:-1}; \
		PORT=$$(sed -n "$${CHOICE}p" /tmp/acme-usb | tr -s ' ' | cut -d' ' -f1); \
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
		python3 ${MKDIR}/tools/scan.py 2>/dev/null > /tmp/acme-ota; \
		if [ ! -s /tmp/acme-ota ]; then \
			$(ERROR_S) "No OTA devices found."; \
			exit 1; \
		fi; \
		i=1; \
		while IFS= read -r line; do \
			printf "  %d) %s\n" $$i "$$line"; \
			i=$$((i+1)); \
		done < /tmp/acme-ota; \
		printf "Select device [1]: "; \
		read CHOICE; \
		CHOICE=$${CHOICE:-1}; \
		SCAN_RESULT=$$(sed -n "$${CHOICE}p" /tmp/acme-ota); \
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