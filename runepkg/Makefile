#
# ******************************************************************************
# * Filename:    Makefile
# * Author:      <michkochris@gmail.com>
# * Date:        started 12-31-2024
# * Description: runepkg manages linux .deb pkg's for Runar Linux
# *
# * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
# * GPLV3
# ******************************************************************************/
# Makefile for runepkg with consolidated configuration system.

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -pthread

LDFLAGS = -pthread
LIBS =

# --- Build Configuration Logic ---
# This MUST come before any variable definitions that use WITH_CPP
WITH_CPP ?= auto

# 1. If 'all' or 'with-all' is a goal, we definitely want C++
ifneq (,$(filter all with-all with-cpp,$(MAKECMDGOALS)))
  WITH_CPP := 1
# 2. If 'install' is the goal, try to preserve the existing build's flavor
else ifeq ($(MAKECMDGOALS),install)
  ifneq ($(wildcard .config_with_cpp),)
    WITH_CPP := $(shell cat .config_with_cpp)
  endif
# 3. If 'runepkg' is the goal or no goal is specified, default to Core
else ifneq (,$(filter runepkg,$(MAKECMDGOALS))$(filter 0,$(words $(MAKECMDGOALS))))
  WITH_CPP := 0
endif

# --- Color & Formatting ---
BOLD := \033[1m
RESET := \033[0m
RED := \033[1;31m
GREEN := \033[1;32m
YELLOW := \033[1;33m
BLUE := \033[1;34m
CYAN := \033[1;36m
PURPLE := \033[1;35m

# --- Dependency detection ---
CC_AVAILABLE := $(shell command -v $(CC) >/dev/null 2>&1 && echo 1 || echo 0)
CXX_AVAILABLE := $(shell command -v $(CXX) >/dev/null 2>&1 && echo 1 || echo 0)
LIBCURL_AVAILABLE := $(shell pkg-config --exists libcurl 2>/dev/null && echo 1 || (curl-config --version >/dev/null 2>&1 && echo 1 || echo 0))
ZLIB_AVAILABLE := $(shell pkg-config --exists zlib 2>/dev/null && echo 1 || (echo "int main(){}" | $(CC) -x c - -lz -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0))
CPP_WRAPPER_EXISTS := $(if $(wildcard runepkg_network.cpp),1,0)

# 4. Resolve 'auto' if still needed
ifeq ($(WITH_CPP),auto)
  ifeq ($(and $(filter 1,$(CXX_AVAILABLE)),$(filter 1,$(LIBCURL_AVAILABLE)),$(filter 1,$(ZLIB_AVAILABLE)),$(filter 1,$(CPP_WRAPPER_EXISTS))),1)
    WITH_CPP = 1
  else
    WITH_CPP = 0
  endif
endif

# --- Force failure if core dependencies are missing ---
ifeq ($(CC_AVAILABLE),0)
	$(info $(RED)!! [forge] Compiling runepkg: some build dependency not met.$(RESET))
	$(info $(RED)!!-----------------------------------------------------------------------!!$(RESET))
	$(info $(RED)   CRITICAL ERROR: The Forge is Cold! ($(CC) not found)$(RESET))
	$(info $(YELLOW)   You cannot forge runepkg without a C compiler.$(RESET))
	$(info $(CYAN)   Seek the ancient tools: sudo apt install gcc$(RESET))
	$(info $(RED)!!-----------------------------------------------------------------------!!$(RESET))
	$(error )
endif

# --- Force failure if WITH_CPP=1 but dependencies are missing ---
ifeq ($(WITH_CPP),1)
  ifneq ($(and $(filter 1,$(CXX_AVAILABLE)),$(filter 1,$(LIBCURL_AVAILABLE)),$(filter 1,$(ZLIB_AVAILABLE))),1)
	$(info $(PURPLE)!! [ritual] Compiling runepkg: extended build dependency not met.$(RESET))
	$(info $(PURPLE)!!-----------------------------------------------------------------------!!$(RESET))
	$(info $(PURPLE)   ERROR: The Ritual of the Full Build has Failed!$(RESET))
	$(info $(YELLOW)   Extended dependencies are missing for the complete runepkg experience:$(RESET))
	$(info   $(BLUE)- g++:$(RESET)      $(if $(filter 1,$(CXX_AVAILABLE)),$(GREEN)READY$(RESET),$(RED)MISSING$(RESET)))
	$(info   $(BLUE)- libcurl:$(RESET)  $(if $(filter 1,$(LIBCURL_AVAILABLE)),$(GREEN)READY$(RESET),$(RED)MISSING$(RESET)))
	$(info   $(BLUE)- zlib:$(RESET)     $(if $(filter 1,$(ZLIB_AVAILABLE)),$(GREEN)READY$(RESET),$(RED)MISSING$(RESET)))
	$(info )
	$(info $(CYAN)   Restore the runes by running: ./debian-depends.sh$(RESET))
	$(info $(PURPLE)!!-----------------------------------------------------------------------!!$(RESET))
	$(error )
  endif
  ifeq ($(CPP_WRAPPER_EXISTS),0)
    $(error $(RED)!! [error] Compiling runepkg: runepkg_network.cpp is missing!$(RESET))
  endif
endif

# --- Apply Configuration ---
CPP_CXXFLAGS = -std=c++17 -Wall -Wextra -O2
CPP_LDFLAGS = $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl") -lz

ifeq ($(WITH_CPP),1)
CFLAGS += -DENABLE_CPP_FFI
LDFLAGS += $(CPP_LDFLAGS)
CPP_SOURCES = runepkg_network.cpp runepkg_building.cpp
CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
CPP_HEADERS = runepkg_cpp_ffi.h
else
CPP_SOURCES =
CPP_OBJECTS =
CPP_HEADERS =
endif

TARGET = runepkg

# Source files
C_SOURCES = runepkg_cli.c runepkg_handle.c runepkg_config.c runepkg_util.c runepkg_pack.c runepkg_hash.c runepkg_storage.c runepkg_defensive.c runepkg_completion.c runepkg_install.c runepkg_md5sums.c
OBJS = $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)

# Header dependencies
HEADERS = runepkg_config.h runepkg_handle.h runepkg_util.h runepkg_pack.h runepkg_hash.h runepkg_storage.h runepkg_defensive.h runepkg_md5sums.h $(CPP_HEADERS)

# Track configuration changes to force rebuilds when WITH_CPP changes
.config_with_cpp: FORCE
	@echo $(WITH_CPP) > $@.tmp
	@if [ ! -f $@ ] || ! diff $@ $@.tmp >/dev/null; then \
		mv $@.tmp $@; \
	else \
		rm $@.tmp; \
	fi

.PHONY: all clean clean-all install debug run termux-install uninstall test test-binary test-help info with-cpp clean-cpp with-all build-banner FORCE

.DEFAULT_GOAL := runepkg

-include $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)

# --- Installation Variables ---
DESTDIR ?=
PREFIX ?= /usr
ETCDIR ?= /etc
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

build-banner:
	@echo "$(BOLD)$(CYAN)--------------------------------------------------------$(RESET)"
	@echo "$(BOLD)$(CYAN)  Unearthing Runes: Compiling runepkg v1.0.4$(RESET)"
	@echo "$(BOLD)$(CYAN)--------------------------------------------------------$(RESET)"
	@echo "  Build Mode: $(if $(filter 1,$(WITH_CPP)),$(PURPLE)Extended (C++ FFI)$(RESET),$(BLUE)Core (C Minimal)$(RESET))"
	@echo ""

all: with-all

$(TARGET): build-banner $(OBJS) .config_with_cpp
	@printf "  $(BLUE)%-10s$(RESET) %s\n" "[ LD ]" "$@"
	@$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "--------------------------------------------------------"
	@echo "$(GREEN)⚡ runepkg build complete: $@$(RESET)"
	@echo "--------------------------------------------------------"

%.o: %.c $(HEADERS) .config_with_cpp
	@printf "  $(CYAN)%-10s$(RESET) %s\n" "[ CC ]" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp $(HEADERS) .config_with_cpp
	@printf "  $(PURPLE)%-10s$(RESET) %s\n" "[ CXX ]" "$<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "$(YELLOW)Cleaning up build artifacts...$(RESET)"
	@rm -f $(OBJS) $(TARGET) $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d) *.deb .config_with_cpp
	@echo "$(GREEN)🧹 Clean complete.$(RESET)"

test-binary: $(TARGET)
	@echo "Testing basic functionality..."
	@if [ -f ./$(TARGET) ]; then \
		echo "Binary exists, testing help output:"; \
		./$(TARGET) --help; \
	else \
		echo "Binary not found!"; \
		exit 1; \
	fi

install: $(TARGET)
	@echo "$(CYAN)Installing $(TARGET) to $(DESTDIR)$(PREFIX)/bin...$(RESET)"
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "$(CYAN)Detecting configuration destiny...$(RESET)"
	@if [ -f runepkgconfig ]; then \
		if [ "$(shell id -u)" = "0" ] || [ -w "$(DESTDIR)$(ETCDIR)" ] 2>/dev/null; then \
			echo "$(CYAN)Installing runes of configuration to $(DESTDIR)$(ETCDIR)/runepkg...$(RESET)"; \
			mkdir -p $(DESTDIR)$(ETCDIR)/runepkg; \
			cp runepkgconfig $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			chmod 644 $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			echo "$(GREEN)Configuration file installed to $(ETCDIR)/runepkg/runepkgconfig$(RESET)"; \
		else \
			echo "$(RED)ERROR: No elevated privileges detected. Configuration requires system install.$(RESET)"; \
			exit 1; \
		fi; \
	fi
	@echo "-----------------------------------------------------------------------"
	@echo "$(BOLD)$(YELLOW)⚡ LIGHTNING FAST AUTOCOMPLETE ⚡$(RESET)"
	@echo "To enable context-aware completion for the current session, run:"
	@echo "  $(CYAN)complete -C runepkg runepkg$(RESET)"
	@echo ""
	@echo "To enable it permanently, add that same line to your $(BOLD)~/.bashrc$(RESET)"
	@echo "-----------------------------------------------------------------------"

termux-install: all
	@echo "Installing $(TARGET) for Termux to $(TERMUX_PREFIX)/bin..."
	mkdir -p $(TERMUX_PREFIX)/bin
	cp $(TARGET) $(TERMUX_PREFIX)/bin/$(TARGET)
	chmod 755 $(TERMUX_PREFIX)/bin/$(TARGET)
	@if [ -f runepkgconfig ]; then \
		mkdir -p $(TERMUX_PREFIX)/etc/runepkg; \
		cp runepkgconfig $(TERMUX_PREFIX)/etc/runepkg/runepkgconfig; \
		chmod 644 $(TERMUX_PREFIX)/etc/runepkg/runepkgconfig; \
	fi
	@echo "-----------------------------------------------------------------------"
	@echo "⚡ LIGHTNING FAST AUTOCOMPLETE ⚡"
	@echo "To enable context-aware completion for the current session, run:"
	@echo "  complete -C runepkg runepkg"
	@echo ""
	@echo "To enable it permanently, add that same line to your ~/.bashrc"
	@echo "-----------------------------------------------------------------------"

uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	@if [ -f $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig ]; then \
		CONF="$(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig"; \
	elif [ -f $(TERMUX_PREFIX)/etc/runepkg/runepkgconfig ]; then \
		CONF="$(TERMUX_PREFIX)/etc/runepkg/runepkgconfig"; \
	fi; \
	if [ -n "$$CONF" ]; then \
		echo "-----------------------------------------------------------------------"; \
		echo "NOTICE: Configuration found at $$CONF"; \
		echo "The following data directories may still exist and were NOT removed:"; \
		grep -E "^(runepkg_dir|control_dir|install_dir|runepkg_db|download_dir)=" $$CONF | cut -d= -f2 | sed 's/^/  - /'; \
		echo "You may want to remove these manually to fully purge runepkg data."; \
		echo "-----------------------------------------------------------------------"; \
	fi
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(TERMUX_PREFIX)/bin/$(TARGET)
	rm -rf $(DESTDIR)$(ETCDIR)/runepkg
	rm -rf $(TERMUX_PREFIX)/etc/runepkg
	@echo "Uninstallation complete."

debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: runepkg

run: runepkg
	./$(TARGET) --help

version: runepkg
	./$(TARGET) --version

info:
	@echo "=== Build Information ==="
	@echo "Target: $(TARGET)"
	@echo "CC: $(CC)"
	@echo "CXX: $(CXX)"
	@echo "C++ FFI:  $(if $(filter 1,$(WITH_CPP)),Enabled,Disabled)"

clean-cpp:
	@rm -f $(CPP_OBJECTS)

clean-all: clean clean-cpp
	@rm -rf target

with-cpp: $(TARGET)

with-all:
	@$(MAKE) -B WITH_CPP=1 $(TARGET)

test: test-binary
