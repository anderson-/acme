RESET  := \033[0m
BOLD   := \033[1m
RED    := \033[31m
GREEN  := \033[32m
YELLOW := \033[33m
CYAN   := \033[36m

INFO  := @printf "$(CYAN)%s$(RESET)\n"
OK    := @printf "$(GREEN)%s$(RESET)\n"
WARN  := @printf "$(YELLOW)%s$(RESET)\n"
ERROR := @printf "$(RED)$(BOLD)%s$(RESET)\n"

INFO_S  := printf "$(CYAN)%s$(RESET)\n"
OK_S    := printf "$(GREEN)%s$(RESET)\n"
WARN_S  := printf "$(YELLOW)%s$(RESET)\n"
ERROR_S := printf "$(RED)$(BOLD)%s$(RESET)\n"

# watches any file and prints spinner + current activity
define file_spinner
	_watch() { \
		local frames='▘▀▝▐▗▄▖▌'; \
		local i=0; \
		local last_size=0; \
		local last_time=0; \
		local green_until=0; \
		while true; do \
			current_size=$$(wc -c < "$(1)" 2>/dev/null || echo 0); \
			current_time=$$(date +%s%3N); \
			if [ "$$current_size" != "$$last_size" ] && [ $$((current_time - last_time)) -ge 50 ]; then \
				printf "\r\033[K\033[92m%s %s\033[0m" "$${frames:$$((i%8)):1}" "$(2)" >&2; \
				i=$$((i+1)); \
				last_size=$$current_size; \
				last_time=$$current_time; \
				green_until=$$((current_time + 300)); \
			elif [ $$current_time -lt $$green_until ]; then \
				printf "\r\033[K\033[32m%s %s\033[0m" "$${frames:$$((i%8)):1}" "$(2)" >&2; \
				i=$$((i+1)); \
			else \
				printf "\r\033[K\033[36m%s %s\033[0m" "$${frames:$$((i%8)):1}" "$(2)" >&2; \
			fi; \
			sleep 0.05; \
		done; \
	}
	_watch
endef