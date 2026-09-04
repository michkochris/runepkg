#
# ******************************************************************************
# * Filename:    Makefile
# * Author:      <michkochris@gmail.com>
# * Date:        started 12-31-2024
# * Description: runepkg manages linux .deb pkg's for Runar Linux
# *
# * Copyright (c) 2026 runepkg (Runar Linux) All rights reserved.
# * GPLV3
# ******************************************************************************/
# Makefile for runepkg with consolidated configuration system.

# --- User Overrides ---
CC      ?= gcc
CXX     ?= g++
V       ?= 0
MUSL    ?= 0
WITH_CPP ?= auto

# --- Installation Variables ---
DESTDIR ?=
PREFIX  ?= /usr
ETCDIR  ?= /etc
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

# --- Power User Shortcut: MUSL ---
ifeq ($(MUSL),1)
  # Check for the Grand Forge toolchain from 'make musl-all'
  FORGE_BIN := $(CURDIR)/.forge_musl/toolchain/bin
  ifneq ($(wildcard $(FORGE_BIN)/x86_64-linux-musl-gcc),)
    CC  := $(FORGE_BIN)/x86_64-linux-musl-gcc
    CXX := $(FORGE_BIN)/x86_64-linux-musl-g++
    # Automatically include forge dependencies if present
    ifneq ($(wildcard $(CURDIR)/.forge_musl/deps/include),)
      override CFLAGS   += -I$(CURDIR)/.forge_musl/deps/include
      override CXXFLAGS += -I$(CURDIR)/.forge_musl/deps/include
      LDFLAGS  += -L$(CURDIR)/.forge_musl/deps/lib
    endif
  else
    CC  := musl-gcc
    CXX := $(shell if command -v musl-g++ >/dev/null 2>&1; then echo musl-g++; else echo musl-gcc; fi)
  endif
  # Disable glibc-specific hardening that can conflict with musl-gcc specs on some hosts
  BASE_CFLAGS   += -fno-stack-protector -U_FORTIFY_SOURCE
  BASE_CXXFLAGS += -fno-stack-protector -U_FORTIFY_SOURCE
endif

# --- Strict Flags and Security Hardening ---
BASE_CFLAGS   = -Wall -Wextra -Wpedantic -Werror -Wshadow -Wformat=2 -Wundef \
                -std=c89 -D_GNU_SOURCE -D_XOPEN_SOURCE=700 -g -O2 \
                -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -MMD -MP \
                -pthread -fdiagnostics-color=always
BASE_CXXFLAGS = -Wall -Wextra -Wpedantic -Werror -Wshadow -Wformat=2 -Wundef \
                -std=c++17 -D_GNU_SOURCE -g -O2 -D_FORTIFY_SOURCE=2 \
                -fstack-protector-strong -fPIE -MMD -MP -pthread -fdiagnostics-color=always
BASE_LDFLAGS  = -pthread -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack

# Append user-provided flags to the base flags
CFLAGS   := $(BASE_CFLAGS) $(CFLAGS)
CXXFLAGS := $(BASE_CXXFLAGS) $(CXXFLAGS)
LDFLAGS  := $(BASE_LDFLAGS) $(LDFLAGS)
LIBS     := $(LIBS)

# --- Build Configuration Logic ---
ifneq (,$(filter all with-all with-cpp,$(MAKECMDGOALS)))
  WITH_CPP := 1
else ifeq ($(MAKECMDGOALS),install)
  ifneq ($(wildcard .config_with_cpp),)
    WITH_CPP := $(shell cat .config_with_cpp)
  endif
else ifneq (,$(filter runepkg,$(MAKECMDGOALS))$(filter 0,$(words $(MAKECMDGOALS))))
  WITH_CPP := 0
endif

# --- Color & Formatting ---
ESC    := $(shell printf '\033')
BOLD   := $(ESC)[1m
RESET  := $(ESC)[0m
RED    := $(ESC)[1;31m
GREEN  := $(ESC)[1;32m
YELLOW := $(ESC)[1;33m
BLUE   := $(ESC)[1;34m
CYAN   := $(ESC)[1;36m
PURPLE := $(ESC)[1;35m

# --- Dependency detection ---
CC_AVAILABLE      := $(shell command -v $(CC) >/dev/null 2>&1 && echo 1 || echo 0)
CXX_AVAILABLE     := $(shell command -v $(CXX) >/dev/null 2>&1 && echo 1 || echo 0)
LIBCURL_AVAILABLE := $(shell pkg-config --exists libcurl 2>/dev/null && echo 1 || (curl-config --version >/dev/null 2>&1 && echo 1 || echo 0))
ZLIB_AVAILABLE    := $(shell pkg-config --exists zlib 2>/dev/null && echo 1 || (echo "int main(){}" | $(CC) -x c - -lz -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0))
CPP_WRAPPER_EXISTS := $(if $(wildcard runepkg_network.cpp),1,0)

# Resolve 'auto' if still needed
ifeq ($(WITH_CPP),auto)
  ifeq ($(and $(filter 1,$(CXX_AVAILABLE)),$(filter 1,$(LIBCURL_AVAILABLE)),$(filter 1,$(ZLIB_AVAILABLE)),$(filter 1,$(CPP_WRAPPER_EXISTS))),1)
    WITH_CPP = 1
  else
    WITH_CPP = 0
  endif
endif

# --- Validation ---
ifeq ($(CC_AVAILABLE),0)
    $(info $(RED)!! [forge] Error: The Forge is Cold! ($(CC) not found)$(RESET))
    $(error )
endif

ifeq ($(WITH_CPP),1)
  ifneq ($(and $(filter 1,$(CXX_AVAILABLE)),$(filter 1,$(LIBCURL_AVAILABLE)),$(filter 1,$(ZLIB_AVAILABLE))),1)
    $(info $(PURPLE)!! [ritual] Error: Extended dependencies missing.$(RESET))
    $(error )
  endif
endif

# --- Apply Configuration ---
ifeq ($(WITH_CPP),1)
  override CFLAGS  += -DENABLE_CPP_FFI
  LDFLAGS += $(shell pkg-config --libs libcurl openssl libcrypto 2>/dev/null || echo "-lcurl -lcrypto") -lz
  CPP_SOURCES = runepkg_security.cpp runepkg_util_cpp.cpp runepkg_resolver.cpp runepkg_network.cpp runepkg_guard.cpp
  CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
  LINKER = $(CXX)
else
  CPP_SOURCES =
  CPP_OBJECTS =
  LINKER = $(CC)
endif

TARGET = runepkg
C_SOURCES = runepkg_cli.c runepkg_handle.c runepkg_config.c runepkg_util.c \
            runepkg_pack.c runepkg_hash.c runepkg_storage.c runepkg_defensive.c \
            runepkg_completion.c runepkg_install.c runepkg_md5sums.c runepkg_crypto.c \
            runepkg_host.c runepkg_state.c
OBJS = $(C_SOURCES:.c=.o) $(CPP_OBJECTS)
HEADERS = runepkg_config.h runepkg_handle.h runepkg_util.h runepkg_pack.h \
          runepkg_hash.h runepkg_storage.h runepkg_defensive.h runepkg_md5sums.h \
          runepkg_crypto.h runepkg_cpp_ffi.h runepkg_host.h runepkg_state.h \
          runepkg_security.hpp runepkg_util_cpp.hpp runepkg_guard.hpp

# --- Output Control ---
Q = $(if $(filter 1,$(V)),,@)
E = $(if $(filter 1,$(V)),@true,@echo)

# --- Configuration Tracking ---
.config_with_cpp: FORCE
	@echo $(WITH_CPP) > $@.tmp
	@if [ ! -f $@ ] || ! diff $@ $@.tmp >/dev/null; then \
       mv $@.tmp $@; \
    else \
       rm $@.tmp; \
    fi

# --- Targets ---

.DEFAULT_GOAL := build
.PHONY: build all clean clean-forge clean-extended clean-all install install-target-rules debug run termux-install uninstall test info with-cpp build-banner musl-all FORCE

build: build-banner $(TARGET)

musl-all:
	@chmod +x forge-musl-all.sh
	@./forge-musl-all.sh

build-banner:
	@echo "$(BOLD)$(CYAN)--------------------------------------------------------$(RESET)"
	@echo "$(BOLD)$(CYAN)  Unearthing Runes: Compiling runepkg v1.0.4$(RESET)"
	@echo "$(BOLD)$(CYAN)--------------------------------------------------------$(RESET)"
	@echo "  Build Mode: $(if $(filter 1,$(WITH_CPP)),$(PURPLE)Extended (C++ FFI)$(RESET),$(BLUE)Core (C Minimal)$(RESET))"
	$(if $(filter 1,$(MUSL)),@echo "  Libc:       $(YELLOW)musl$(RESET)",)
	@echo ""

all: with-all

with-all:
	$(Q)$(MAKE) WITH_CPP=1 build

$(TARGET): $(OBJS) .config_with_cpp
	$(E) "  $(GREEN)[link]$(RESET)  Linking target: $@"
	$(Q)$(LINKER) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	$(E) "$(BOLD)$(GREEN)⚡ runepkg build complete: $@$(RESET)"

%.o: %.c $(HEADERS) .config_with_cpp
	$(E) "  $(BLUE)[forge]$(RESET) Compiling C Rune: $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp $(HEADERS) .config_with_cpp
	$(E) "  $(PURPLE)[ritual]$(RESET) Compiling C++ Rune: $<"
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)

clean:
	@echo "$(YELLOW)Cleaning up build artifacts and unearthed runes...$(RESET)"
	$(Q)rm -f $(OBJS) $(TARGET) $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d) *.deb .config_with_cpp
	@echo "$(GREEN)🧹 Clean complete. The forge is reset.$(RESET)"

clean-extended:
	@echo "$(YELLOW)Purging extended workspace artifacts (vrunepkg_dir, etc.)...$(RESET)"
	$(Q)rm -rf debs sources temp_install staging* vrunepkg_dir

clean-forge:
	@echo "$(RED)Dismantling the Grand Forge (.forge_musl)...$(RESET)"
	$(Q)rm -rf .forge_musl
	@echo "$(GREEN)Done.$(RESET)"

clean-all: clean clean-extended clean-forge

install: $(TARGET)
	@echo "$(CYAN)Installing $(TARGET) to $(DESTDIR)$(PREFIX)/bin...$(RESET)"
	$(Q)mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(Q)cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	$(Q)chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@if [ -f runepkgconfig ]; then \
       if [ "$(shell id -u)" = "0" ] || [ -w "$(DESTDIR)$(ETCDIR)" ] 2>/dev/null; then \
          echo "$(CYAN)Installing configuration to $(DESTDIR)$(ETCDIR)/runepkg...$(RESET)"; \
          mkdir -p $(DESTDIR)$(ETCDIR)/runepkg; \
          cp runepkgconfig $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
          chmod 644 $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
          if [ -d targets ]; then \
             echo "$(CYAN)Installing target profiles...$(RESET)"; \
             mkdir -p $(DESTDIR)$(ETCDIR)/runepkg/targets; \
             cp targets/*.conf $(DESTDIR)$(ETCDIR)/runepkg/targets/ 2>/dev/null || true; \
             chmod 644 $(DESTDIR)$(ETCDIR)/runepkg/targets/*.conf 2>/dev/null || true; \
          fi; \
       fi; \
    fi
	@echo "$(GREEN)Installation complete. (Run 'make install-target-rules' to sync the rules tree)$(RESET)"

install-target-rules:
	@if [ -d target-rules ]; then \
       echo "$(CYAN)Installing target rules to $(DESTDIR)$(ETCDIR)/runepkg/target-rules...$(RESET)"; \
       mkdir -p $(DESTDIR)$(ETCDIR)/runepkg/target-rules; \
       cp -r target-rules/* $(DESTDIR)$(ETCDIR)/runepkg/target-rules/; \
       echo "$(GREEN)Target rules installed.$(RESET)"; \
    else \
       echo "$(RED)Error: target-rules directory not found.$(RESET)"; \
       exit 1; \
    fi

uninstall:
	@echo "$(BOLD)$(YELLOW)Uninstalling $(TARGET)...$(RESET)"
	$(Q)rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	$(Q)rm -f $(TERMUX_PREFIX)/bin/$(TARGET)
	@echo "$(YELLOW)Note: $(DESTDIR)$(ETCDIR)/runepkg has been preserved.$(RESET)"
	@echo "$(GREEN)Uninstallation complete.$(RESET)"

debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: runepkg

test: $(TARGET)
	$(Q)./$(TARGET) --help

info:
	@echo "=== Build Information ==="
	@echo "Target:  $(TARGET)"
	@echo "CC:      $(CC)"
	@echo "CXX:     $(CXX)"
	@echo "Linker:  $(LINKER)"
	@echo "Mode:    $(if $(filter 1,$(WITH_CPP)),Extended,Core)"
	@echo "Libc:    $(if $(filter 1,$(MUSL)),musl,glibc/default)"
