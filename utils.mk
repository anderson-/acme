RESET  := \033[0m
BOLD   := \033[1m
RED    := \033[31m
GREEN  := \033[32m
YELLOW := \033[33m
CYAN   := \033[36m

INFO  = @printf "$(CYAN)%s$(RESET)\n"
OK    = @printf "$(GREEN)%s$(RESET)\n"
WARN  = @printf "$(YELLOW)%s$(RESET)\n"
ERROR = @printf "$(RED)$(BOLD)%s$(RESET)\n"

INFO_S  = printf "$(CYAN)%s$(RESET)\n"
OK_S    = printf "$(GREEN)%s$(RESET)\n"
WARN_S  = printf "$(YELLOW)%s$(RESET)\n"
ERROR_S = printf "$(RED)$(BOLD)%s$(RESET)\n"