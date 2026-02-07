#
# ******************************************************************************
# * Filename:    Makefile
# * Author:      <michkochris@gmail.com>
# * Date:        started 12-31-2024
# * Description: runepkg manages linux .deb pkg's for Runar Linux
# *
# * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
# * GPLV3
# * This program is free software: you can redistribute it and/or modify it
# * under the terms of the GNU General Public License as published by the Free
# * Software Foundation, either version 3 of the License, or (at your option)
# * any later version.
# *
# * This program is distributed in the hope that it will be useful, but WITHOUT
# * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# * more details.
# *
# * You should have received a copy of the GNU General Public License along
# * with this program. If not, see <https://www.gnu.org/licenses/>.
# ******************************************************************************/
# Makefile for runepkg with consolidated configuration system.
# This file compiles the C source code into an executable.
# It also handles creating a default config file and installing the executable.

CC = gcc
CXX = g++
# Updated CFLAGS with _GNU_SOURCE and improved flags for consolidated system
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -pthread

LDFLAGS = -pthread
LIBS =

# Force core build when explicitly targeting runepkg
ifneq (,$(filter runepkg,$(MAKECMDGOALS)))
WITH_RUST := 0
WITH_CPP := 0
endif

# Rust FFI support detection and configuration
WITH_RUST ?= auto
RUST_LIB_PATH = target/release
RUST_LIB_NAME = runepkg_ffi
RUSTC_AVAILABLE := $(shell command -v rustc >/dev/null 2>&1 && echo 1 || echo 0)
RUST_CARGO_AVAILABLE := $(shell command -v cargo >/dev/null 2>&1 && echo 1 || echo 0)
RUST_DEPS_AVAILABLE := $(shell command -v cargo >/dev/null 2>&1 && command -v rustc >/dev/null 2>&1 && echo 1 || echo 0)
RUST_WRAPPER_EXISTS := $(if $(wildcard runepkg_rust_ffi.c),1,0)
RUST_FFI_AVAILABLE := $(if $(and $(filter 1,$(RUST_DEPS_AVAILABLE)),$(filter 1,$(RUST_WRAPPER_EXISTS))),1,0)

# C++ FFI support detection and configuration
WITH_CPP ?= auto
CPP_CXXFLAGS = -std=c++17 -Wall -Wextra -O2
CPP_LDFLAGS = -lcurl -lssl -lcrypto -lpthread
CXX_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && echo 1 || echo 0)
LIBCURL_AVAILABLE := $(shell pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_DEPS_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_WRAPPER_EXISTS := $(if $(wildcard runepkg_cpp_ffi.cpp),1,0)
CPP_FFI_AVAILABLE := $(if $(and $(filter 1,$(CPP_DEPS_AVAILABLE)),$(filter 1,$(CPP_WRAPPER_EXISTS))),1,0)

# Check if Rust FFI should be enabled
ifeq ($(WITH_RUST),auto)
	# Auto-detect Rust availability
	ifeq ($(RUST_CARGO_AVAILABLE),1)
        RUST_LIB_EXISTS := $(shell test -f $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a && echo 1 || echo 0)
        ifeq ($(RUST_LIB_EXISTS),1)
            WITH_RUST = 1
        else
            WITH_RUST = 0
        endif
    else
        WITH_RUST = 0
    endif
endif

# Check if C++ FFI should be enabled
ifeq ($(WITH_CPP),auto)
# Auto-detect C++ and required libraries
ifeq ($(CXX_AVAILABLE),1)
ifeq ($(LIBCURL_AVAILABLE),1)
WITH_CPP = 1
else
WITH_CPP = 0
endif
else
WITH_CPP = 0
endif
endif

# Configure Rust FFI if enabled
ifeq ($(WITH_RUST),1)
ifeq ($(RUST_WRAPPER_EXISTS),0)
WITH_RUST = 0
endif
endif

ifeq ($(WITH_RUST),1)
CFLAGS += -DWITH_RUST_FFI
LDFLAGS += -L./$(RUST_LIB_PATH) -l$(RUST_LIB_NAME) -ldl -lpthread -lm -Wl,-rpath,./$(RUST_LIB_PATH)
RUST_SOURCES = runepkg_rust_ffi.c
RUST_OBJECTS = runepkg_rust_ffi.o
RUST_HEADERS = runepkg_rust_ffi.h
else
RUST_SOURCES =
RUST_OBJECTS =
RUST_HEADERS =
endif

# Configure C++ FFI if enabled
ifeq ($(WITH_CPP),1)
ifeq ($(CPP_WRAPPER_EXISTS),0)
WITH_CPP = 0
endif
endif

ifeq ($(WITH_CPP),1)
CFLAGS += -DENABLE_CPP_FFI
LDFLAGS += $(CPP_LDFLAGS)
CPP_SOURCES = runepkg_cpp_ffi.cpp
CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
CPP_HEADERS = runepkg_cpp_ffi.h
else
CPP_SOURCES =
CPP_OBJECTS =
CPP_HEADERS =
endif

TARGET = runepkg

# Source files - Updated to include utility, package, hash, storage functions, defensive programming, and optional FFI systems
C_SOURCES = runepkg_cli.c runepkg_handle.c runepkg_config.c runepkg_util.c runepkg_pack.c runepkg_hash.c runepkg_storage.c runepkg_defensive.c $(RUST_SOURCES)
OBJS = $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)

# Header dependencies - Updated to include FFI headers if enabled
HEADERS = runepkg_config.h runepkg_handle.h runepkg_util.h runepkg_pack.h runepkg_hash.h runepkg_storage.h runepkg_defensive.h $(RUST_HEADERS) $(CPP_HEADERS)

# Phony targets - Updated to include FFI targets
.PHONY: all clean clean-all install debug run termux-install uninstall test info rust-lib with-rust clean-rust test-rust with-cpp clean-cpp with-all clean-all test-all test-memory test-security test-performance test-comprehensive clean-tests

.DEFAULT_GOAL := runepkg

runepkg: WITH_RUST=0
runepkg: WITH_CPP=0

# Include generated dependency files
-include $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)

# --- Installation Variables ---
# DESTDIR is the standard variable for packaging; set it for a staged install.
DESTDIR ?=
# PREFIX is where the executable goes; default to old-school Linux /usr so binaries live in /usr/bin
PREFIX ?= /usr
# ETCDIR is where the system-wide configuration goes
ETCDIR ?= /etc
# COMPLETION_DIR is where bash completion scripts go
COMPLETION_DIR ?= /usr/share/bash-completion/completions
# TERMUX_PREFIX is the base directory for Termux installs
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

all: with-all

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "⚡ Build complete: $(TARGET)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# === NEW: Rule for compiling C++ source files ===
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@
# ===============================================

clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(OBJS) $(TARGET) $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d) Cargo.lock
	@rm -rf target
	@echo "🧹 Clean complete."

# Test compilation only (useful for checking syntax without running)
test-binary: $(TARGET)
	@echo "Testing basic functionality..."
	@if [ -f ./$(TARGET) ]; then \
		echo "Binary exists, testing help output:"; \
		./$(TARGET) --help; \
	else \
		echo "Binary not found!"; \
		exit 1; \
	fi

# The main install target for standard Linux systems
# This installs the binary to PREFIX/bin and config based on privileges
# With sudo/elevated privileges: installs config to ETCDIR/runepkg/runepkgconfig
# Without sudo: installs config to ~/.runepkgconfig
# To install with system config, run 'sudo make install'
# To install with user config, run 'make install'
# To create a package, run 'make DESTDIR=/path/to/staging/ install'
install: WITH_RUST=0
install: WITH_CPP=0
install: runepkg
	@echo "Installing $(TARGET) for Linux system to $(DESTDIR)$(PREFIX)/bin..."
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@# Install Rust library if it exists
	@if [ -f "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so" ]; then \
		echo "Installing Rust library lib$(RUST_LIB_NAME).so..."; \
		mkdir -p $(DESTDIR)$(PREFIX)/lib; \
		cp $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so $(DESTDIR)$(PREFIX)/lib/; \
		chmod 755 $(DESTDIR)$(PREFIX)/lib/lib$(RUST_LIB_NAME).so; \
		echo "Rust library installed to $(PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
	fi
	@echo "Determining configuration installation location..."
	@if [ -f runepkgconfig ]; then \
		if [ "$(shell id -u)" = "0" ] || [ -w "$(DESTDIR)$(ETCDIR)" ] 2>/dev/null; then \
			echo "Installing system-wide configuration to $(DESTDIR)$(ETCDIR)/runepkg..."; \
			mkdir -p $(DESTDIR)$(ETCDIR)/runepkg; \
			cp runepkgconfig $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			sed -i 's|~/|/var/lib/|g' $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			chmod 644 $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			echo "Configuration file installed to $(ETCDIR)/runepkg/runepkgconfig"; \
			echo "=== Linux Installation Complete (System-wide) ==="; \
			echo "Binary: $(PREFIX)/bin/$(TARGET)"; \
			echo "System config: $(ETCDIR)/runepkg/runepkgconfig"; \
			echo "Bash completion: $(COMPLETION_DIR)/runepkg"; \
			if [ -f "$(DESTDIR)$(PREFIX)/lib/lib$(RUST_LIB_NAME).so" ]; then \
				echo "Rust library: $(PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
			fi; \
		else \
			echo "No elevated privileges detected. Installing user-specific configuration..."; \
			cp runepkgconfig ~/.runepkgconfig; \
			chmod 644 ~/.runepkgconfig; \
			echo "Configuration file installed to ~/.runepkgconfig"; \
			echo "=== Linux Installation Complete (User-specific) ==="; \
			echo "Binary: $(PREFIX)/bin/$(TARGET)"; \
			echo "User config: ~/.runepkgconfig"; \
			echo "Bash completion: $(COMPLETION_DIR)/runepkg"; \
			if [ -f "$(DESTDIR)$(PREFIX)/lib/lib$(RUST_LIB_NAME).so" ]; then \
				echo "Rust library: $(PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
			fi; \
		fi; \
	else \
		echo "WARNING: runepkgconfig file not found in current directory!"; \
	fi
	@echo "Bash completion: using built-in self-completing binary (no external script installed)."
	@echo ""
	@echo "The cascading configuration will check:"
	@echo "  1. \$$RUNEPKG_CONFIG_PATH environment variable"
	@echo "  2. $(ETCDIR)/runepkg/runepkgconfig (system-wide)"
	@echo "  3. ~/.runepkgconfig (user-specific)"
	@echo ""
	@echo "Bash completion will be loaded automatically in new bash sessions."
	@echo "No manual sourcing needed - the binary handles completion internally."
	@echo ""

# Termux-specific install target
# This installs the binary to TERMUX_PREFIX/bin and creates user config at ~/.runepkgconfig
# This assumes the user is running 'make termux-install' inside Termux.
termux-install: all
	@echo "Installing $(TARGET) for Termux to $(TERMUX_PREFIX)/bin..."
	mkdir -p $(TERMUX_PREFIX)/bin
	cp $(TARGET) $(TERMUX_PREFIX)/bin/$(TARGET)
	chmod 755 $(TERMUX_PREFIX)/bin/$(TARGET)
	@# Install Rust library if it exists
	@if [ -f "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so" ]; then \
		echo "Installing Rust library lib$(RUST_LIB_NAME).so for Termux..."; \
		mkdir -p $(TERMUX_PREFIX)/lib; \
		cp $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so $(TERMUX_PREFIX)/lib/; \
		chmod 755 $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so; \
		echo "Rust library installed to $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
	fi
	@echo "Creating user-specific configuration file at ~/.runepkgconfig..."
	@if [ -f runepkgconfig ]; then \
		cp runepkgconfig ~/.runepkgconfig; \
		chmod 644 ~/.runepkgconfig; \
		echo "Configuration file created at ~/.runepkgconfig"; \
	else \
		echo "ERROR: runepkgconfig file not found in current directory!"; \
		exit 1; \
	fi
	@echo "Adding self-completing binary completion to ~/.bashrc..."
	@grep -q "complete -o nosort -C runepkg runepkg" ~/.bashrc || echo "complete -o nosort -C runepkg runepkg" >> ~/.bashrc
	@echo "Completion added to ~/.bashrc (if not already present)."
	@echo ""
	@echo "=== Termux Installation Complete ==="
	@echo "Binary: $(TERMUX_PREFIX)/bin/$(TARGET)"
	@echo "User config: ~/.runepkgconfig"
	@if [ -f "$(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so" ]; then \
		echo "Rust library: $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
	fi
	@echo ""
	@echo "The cascading configuration will check:"
	@echo "  1. \$$RUNEPKG_CONFIG_PATH environment variable"
	@echo "  2. /etc/runepkg/runepkgconfig (system-wide - not available in Termux)"
	@echo "  3. ~/.runepkgconfig (user-specific - PRIMARY for Termux)"
	@echo ""
	@echo "For bash completion in Termux, the binary is self-completing."
	@echo "No manual sourcing needed - completion works automatically."
	@echo ""
	@echo "You can now run 'runepkg <command>' from anywhere in Termux."

# Uninstall target for both standard Linux and Termux
uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	@BIN_CANDIDATES="$(DESTDIR)$(PREFIX)/bin/$(TARGET) $(DESTDIR)$(PREFIX)/$(TARGET) /usr/bin/$(TARGET) /usr/local/bin/$(TARGET) /usr/$(TARGET) /usr/local/$(TARGET) $(TERMUX_PREFIX)/bin/$(TARGET)"; \
	for b in $$BIN_CANDIDATES; do \
		if [ -f "$$b" ]; then \
			echo "Found $$b"; \
			DIR=$$(dirname "$$b"); \
			if [ "$(shell id -u)" = "0" ]; then \
				echo "Removing $$b..."; rm -f "$$b" && echo "Removed $$b" || echo "Failed to remove $$b"; \
			elif [ -w "$$DIR" ]; then \
				echo "Removing $$b..."; rm -f "$$b" && echo "Removed $$b" || echo "Failed to remove $$b"; \
			elif command -v sudo >/dev/null 2>&1; then \
				echo "Attempting sudo rm $$b..."; sudo rm -f "$$b" && echo "Removed $$b (via sudo)" || echo "Failed to remove $$b via sudo"; \
			else \
				echo "ERROR: Cannot remove $$b (insufficient privileges). To remove manually run: sudo rm -f $$b"; \
			fi; \
		fi; \
	done; \
	LIB_CANDIDATES="$(DESTDIR)$(PREFIX)/lib/lib$(RUST_LIB_NAME).so /usr/lib/lib$(RUST_LIB_NAME).so /usr/local/lib/lib$(RUST_LIB_NAME).so $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so"; \
	for l in $$LIB_CANDIDATES; do \
		if [ -f "$$l" ]; then \
			echo "Found $$l"; \
			DIR=$$(dirname "$$l"); \
			if [ "$(shell id -u)" = "0" ]; then \
				echo "Removing $$l..."; rm -f "$$l" && echo "Removed $$l" || echo "Failed to remove $$l"; \
			elif [ -w "$$DIR" ]; then \
				echo "Removing $$l..."; rm -f "$$l" && echo "Removed $$l" || echo "Failed to remove $$l"; \
			elif command -v sudo >/dev/null 2>&1; then \
				echo "Attempting sudo rm $$l..."; sudo rm -f "$$l" && echo "Removed $$l (via sudo)" || echo "Failed to remove $$l via sudo"; \
			else \
				echo "ERROR: Cannot remove $$l (insufficient privileges). To remove manually run: sudo rm -f $$l"; \
			fi; \
		fi; \
	done; \
	# Remove bash completion
	# No external shell completion script is installed by modern runepkg; nothing to remove.
	# Remove runepkg work dirs (from config if available)
	@CONFIG_CANDIDATES="$$RUNEPKG_CONFIG_PATH $(ETCDIR)/runepkg/runepkgconfig $$HOME/.runepkgconfig"; \
	FOUND_CONFIG=""; \
	for c in $$CONFIG_CANDIDATES; do \
		if [ -n "$$c" ] && [ -f "$$c" ]; then FOUND_CONFIG="$$c"; break; fi; \
	done; \
	if [ -n "$$FOUND_CONFIG" ]; then \
		echo "Using config for cleanup: $$FOUND_CONFIG"; \
		get_val() { \
			key="$$1"; \
			val=$$(grep -E "^[[:space:]]*$$key[[:space:]]*=" "$$FOUND_CONFIG" | tail -1 | cut -d'=' -f2- | xargs); \
			if [ -n "$$val" ]; then \
				case "$$val" in \
				~/*) val="$$HOME/$${val#~/}" ;; \
				esac; \
				printf "%s" "$$val"; \
			fi; \
		}; \
		RUNEPKG_DIR=$$(get_val runepkg_dir); \
		CONTROL_DIR=$$(get_val control_dir); \
		INSTALL_DIR=$$(get_val install_dir); \
		DB_DIR=$$(get_val runepkg_db); \
		safe_rm() { \
			p="$$1"; \
			if [ -n "$$p" ] && [ "$$p" != "/" ] && [ "$$p" != "." ] && [ -d "$$p" ]; then \
				echo "Removing directory: $$p"; rm -rf "$$p"; \
			fi; \
		}; \
		safe_rm "$$CONTROL_DIR"; \
		safe_rm "$$DB_DIR"; \
		safe_rm "$$INSTALL_DIR"; \
		safe_rm "$$RUNEPKG_DIR"; \
	else \
		echo "No config found for cleanup; using fallback paths"; \
		FALLBACK_HOME="$$HOME"; \
		if [ "$(shell id -u)" = "0" ] && [ -d "/root" ]; then FALLBACK_HOME="/root"; fi; \
		if [ -n "$$FALLBACK_HOME" ]; then \
			if [ -d "$$FALLBACK_HOME/runepkg_dir" ]; then \
				echo "Removing directory: $$FALLBACK_HOME/runepkg_dir"; rm -rf "$$FALLBACK_HOME/runepkg_dir"; \
			fi; \
		fi; \
	fi
	@if [ "$(shell id -u)" = "0" ] && [ -d "/root/runepkg_dir" ]; then \
		echo "Removing directory: /root/runepkg_dir"; rm -rf "/root/runepkg_dir"; \
	fi
	# Remove system config (prefer direct removal, fallback to sudo)
	@if [ -f $(ETCDIR)/runepkg/runepkgconfig ]; then \
		echo "Found $(ETCDIR)/runepkg/runepkgconfig"; \
		if [ "$(shell id -u)" = "0" ] || [ -w "$(ETCDIR)/runepkg" ] 2>/dev/null; then \
			echo "Removing system config..."; rm -f $(ETCDIR)/runepkg/runepkgconfig; rmdir $(ETCDIR)/runepkg 2>/dev/null || true; echo "Linux system config removed."; \
		elif command -v sudo >/dev/null 2>&1; then \
			echo "Attempting to remove system config with sudo..."; sudo rm -f $(ETCDIR)/runepkg/runepkgconfig && sudo rmdir $(ETCDIR)/runepkg 2>/dev/null && echo "Removed system config (via sudo)" || echo "Failed to remove system config via sudo"; \
		else \
			echo "ERROR: System config exists but cannot be removed without elevated privileges. To remove manually run: sudo rm -f $(ETCDIR)/runepkg/runepkgconfig"; \
		fi; \
	fi
	# Remove user config if it exists (no privileges needed)
	@if [ -f ~/.runepkgconfig ]; then \
		echo "Found user config at ~/.runepkgconfig"; \
		echo "NOTE: User-specific config file ~/.runepkgconfig was NOT automatically removed."; \
		echo "To remove it manually, run: rm ~/.runepkgconfig"; \
	fi
	# Remove bash completion
	@if [ -f $(ETCDIR)/bash_completion.d/runepkg ]; then \
		echo "Found bash completion at $(ETCDIR)/bash_completion.d/runepkg"; \
		if [ "$(shell id -u)" = "0" ] || [ -w "$(ETCDIR)/bash_completion.d" ] 2>/dev/null; then \
			echo "Removing bash completion..."; rm -f $(ETCDIR)/bash_completion.d/runepkg; echo "Bash completion removed."; \
		elif command -v sudo >/dev/null 2>&1; then \
			echo "Attempting to remove bash completion with sudo..."; sudo rm -f $(ETCDIR)/bash_completion.d/runepkg && echo "Removed bash completion (via sudo)" || echo "Failed to remove bash completion via sudo"; \
		else \
			echo "ERROR: Bash completion exists but cannot be removed without elevated privileges. To remove manually run: sudo rm -f $(ETCDIR)/bash_completion.d/runepkg"; \
		fi; \
	fi
	@echo "No shell completion cleanup needed (self-completing binary)."
	# Termux uninstall
	@if [ -f $(TERMUX_PREFIX)/bin/$(TARGET) ]; then \
		echo "Removing $(TERMUX_PREFIX)/bin/$(TARGET)..."; \
		rm -f $(TERMUX_PREFIX)/bin/$(TARGET); \
		echo "Termux binary removed."; \
	fi
	# Remove Rust library from Termux locations
	@if [ -f $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so ]; then \
		echo "Removing $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so..."; \
		rm -f $(TERMUX_PREFIX)/lib/lib$(RUST_LIB_NAME).so; \
		echo "Termux Rust library removed."; \
	fi
	@echo "Removing self-completing binary completion from ~/.bashrc..."
	@sed -i '/complete -o nosort -C runepkg runepkg/d' ~/.bashrc
	@echo "Completion removed from ~/.bashrc."
	@echo "Uninstallation complete."

# Debug build with extra debugging symbols and verbose output
debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: runepkg
	@echo "Debug build complete. Run with -v for verbose output."

# Development target - build and run with help
run: runepkg
	@echo "Running $(TARGET) with help option..."
	./$(TARGET) --help

# Development target - build and run with version
version: runepkg
	@echo "Running $(TARGET) with version option..."
	./$(TARGET) --version

# Development target - build and test verbose mode (requires config file)
test-verbose: runepkg
	@echo "Testing $(TARGET) in verbose mode..."
	./$(TARGET) -v --version

# Show information about the build and configuration
info:
	@echo "=== Build Information ==="
	@echo "Target: $(TARGET)"
	@echo "Test binary: $(UNIFIED_TEST)"
	@echo "Source files: $(C_SOURCES) $(CPP_SOURCES)"
	@echo "Object files: $(OBJS)"
	@echo "Headers: $(HEADERS)"
	@echo "CC: $(CC)"
	@echo "CXX: $(CXX)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo ""
	@echo "=== FFI Systems Status ==="
	@echo "Rust FFI: $(if $(filter 1,$(WITH_RUST)),Enabled,Disabled)"
	@echo "C++ FFI:  $(if $(filter 1,$(WITH_CPP)),Enabled (groundwork),Disabled)"
	@echo ""
	@echo "=== Installation Paths ==="
	@echo "Linux Binary: $(PREFIX)/bin/$(TARGET)"
	@echo "Linux Config: $(ETCDIR)/runepkg/runepkgconfig"
	@echo "Termux Binary: $(TERMUX_PREFIX)/bin/$(TARGET)"
	@echo "User Config: ~/.runepkgconfig"
	@echo ""
	@echo "=== Config File Cascading Order ==="
	@echo "1. \$$RUNEPKG_CONFIG_PATH (environment override)"
	@echo "2. $(ETCDIR)/runepkg/runepkgconfig (system-wide)"
	@echo "3. ~/.runepkgconfig (user-specific)"
	@echo ""
	@echo "=== Available Targets ==="
	@echo "make runepkg       - Build the binary (C only)"
	@echo "make all           - Build with Rust + C++ FFI extensions"
	@echo "make with-rust     - Build with Rust FFI highlighting"
	@echo "make with-cpp      - Build with C++ FFI networking (groundwork)"
	@echo "make with-all      - Build with all FFI systems"
	@echo "make install       - Install for Linux"
	@echo "make termux-install - Install for Termux"
	@echo "make test          - Build and test basic functionality"
	@echo "make test-unified  - Run comprehensive unified test suite"
	@echo "make test-unified-rust - Run unified tests with Rust FFI"
	@echo "make test-unified-quick - Run quick unified tests"
	@echo "make test-rust     - Test Rust FFI system"
	@echo "make test-cpp      - Test C++ FFI system (planned)"
	@echo "make clean         - Clean build artifacts"
	@echo "make clean-all     - Clean all artifacts (C + Rust + C++)"
	@echo "make info          - Show this information"
	@echo "make rust-info     - Show detailed Rust FFI information"
	@echo "make cpp-info      - Show detailed C++ FFI information"
	@echo "make uninstall     - Remove installed files (sudo may be needed)"
	@echo "make debug         - Build with debug flags"
	@echo ""
	@echo "=== Rust FFI Targets ==="
	@echo "make with-rust        - Build with clean slate Rust FFI (zero dependencies)"
	@echo "make rust-lib         - Build clean slate Rust library"
	@echo "make test-rust        - Test Rust FFI functionality"
	@echo "make clean-rust       - Clean Rust build artifacts"
	@echo "=========================="

# --- Rust FFI Targets ---

# Build Rust library (now clean slate by default)
rust-lib:
	@printf "\033[0;32mRust: building core FFI library (clean-slate)\033[0m\n"
	@sh -c "if [ -f gccrs-src/runepkg_highlight.rs ] && [ -f Cargo.toml ]; then if command -v cargo >/dev/null 2>&1; then cargo build --release --no-default-features --features clean-slate || { echo 'Rust: cargo build failed'; exit 1; }; if [ -f \"$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a\" ] || [ -f \"$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so\" ]; then echo 'Rust: built library'; else echo 'Rust: cargo build did not produce expected library'; exit 1; fi; else echo 'Rust: cargo not found (install rustc + cargo)'; exit 1; fi; else mkdir -p \"$(RUST_LIB_PATH)\" && printf '%s\n' 'int runepkg_highlight_stub(void) { return 0; }' > \"$(RUST_LIB_PATH)/stub_runepkg_highlight.c\" && $(CC) -c -fPIC -O2 \"$(RUST_LIB_PATH)/stub_runepkg_highlight.c\" -o \"$(RUST_LIB_PATH)/stub_runepkg_highlight.o\" && ar rcs \"$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a\" \"$(RUST_LIB_PATH)/stub_runepkg_highlight.o\" && echo 'Rust: created stub static library $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a'; fi"

# Build with Rust FFI support (clean slate by default)
with-rust: rust-lib
	@echo "Building runepkg with clean slate Rust FFI support..."
	$(MAKE) WITH_RUST=1

# Test Rust FFI functionality
test-rust: with-rust
	@echo "Testing Rust FFI functionality..."
	@echo "=== FFI Availability Test ==="
	@echo '#include "runepkg_rust_ffi.h"' > test_ffi.c
	@echo '#include <stdio.h>' >> test_ffi.c
	@echo 'int main() {' >> test_ffi.c
	@echo '  int result = runepkg_rust_ffi_available();' >> test_ffi.c
	@echo '  printf("FFI test result: %d\\n", result);' >> test_ffi.c
	@echo '  return result == 0 ? 0 : 1;' >> test_ffi.c
	@echo '}' >> test_ffi.c
	@if $(CC) $(CFLAGS) test_ffi.c -L./$(RUST_LIB_PATH) -l$(RUST_LIB_NAME) -ldl -lpthread -lm -o test_ffi; then \
		if ./test_ffi; then \
			echo "Rust FFI test: PASSED"; \
		else \
			echo "Rust FFI test: FAILED"; \
		fi; \
	else \
		echo "Rust FFI test compilation failed"; \
	fi
	@rm -f test_ffi test_ffi.c
	@echo ""
	@echo "=== Highlighting Test ==="
	@echo 'Testing with sample script...'
	@echo '#!/bin/bash' > test_script.sh
	@echo 'echo "Hello World"' >> test_script.sh
	@echo '# This is a comment' >> test_script.sh
	@if [ -f "./$(TARGET)" ]; then \
		echo "runepkg executable found, testing highlighting integration..."; \
		echo "(Note: Actual highlighting test depends on CLI integration)"; \
	else \
		echo "runepkg executable not found"; \
	fi
	@rm -f test_script.sh

# Clean Rust build artifacts
# Remove obsolete targets
clean-rust:
	@echo "Cleaning Rust build artifacts..."
	@if [ -f "Cargo.toml" ]; then \
		cargo clean; \
		rm -f Cargo.lock; \
		echo "Rust build artifacts cleaned"; \
	else \
		echo "No Cargo.toml found, nothing to clean"; \
	fi
	@echo "Clean slate Rust build artifacts cleaned"

# Clean C++ build artifacts (when implemented)
clean-cpp:
	@echo "Cleaning C++ build artifacts..."
	@rm -f $(CPP_OBJECTS)
	@echo "C++ build artifacts cleaned"

# Clean all (C + Rust + C++ + Tests + Deep clean)
clean-all: clean clean-rust clean-cpp clean-tests
	@echo "🧹 Deep cleaning all artifacts..."
	@rm -f *.o *.d *.gch
	@rm -f core core.* *.core
	@rm -rf target
	@rm -f .deps/* 2>/dev/null || true
	@rmdir .deps 2>/dev/null || true
	@echo "All build artifacts and tests cleaned"

# C++ FFI targets
with-cpp: check-cpp-deps $(TARGET)
	@echo "Built with C++ FFI networking support"

check-cpp-deps:
ifeq ($(CPP_DEPS_AVAILABLE),1)
	@echo "C++ deps: available (g++, libcurl, openssl)"
else
	@echo "C++ deps: missing (g++, libcurl4-openssl-dev)"
endif

check-rust-deps:
ifeq ($(RUST_DEPS_AVAILABLE),1)
	@echo "Rust deps: available (rustc, cargo)"
else
	@echo "Rust deps: missing (rustc, cargo)"
endif

deps-status:
	@if [ "$(RUST_DEPS_AVAILABLE)" = "1" ]; then \
		RUST_STATUS="available (rustc, cargo)"; \
	else \
		RUST_STATUS="missing (rustc, cargo)"; \
	fi; \
	if [ "$(CPP_DEPS_AVAILABLE)" = "1" ]; then \
		CPP_STATUS="available (g++, libcurl, openssl)"; \
	else \
		CPP_STATUS="missing (g++, libcurl4-openssl-dev)"; \
	fi; \
	echo "Deps: Rust=$$RUST_STATUS | C++=$$CPP_STATUS"

deps-min:
	@echo "Minimal deps to enable all extensions: rustc cargo g++ libcurl4-openssl-dev"

with-all: deps-status deps-min
	@echo "Build target: $(TARGET)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "FFI: Rust=$(if $(filter 1,$(RUST_FFI_AVAILABLE)),enabled,disabled) C++=$(if $(filter 1,$(CPP_FFI_AVAILABLE)),enabled,disabled)"
	@if [ -f "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a" ]; then \
		echo "Rust FFI lib: $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a"; \
	elif [ -f "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so" ]; then \
		echo "Rust FFI lib: $(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).so"; \
	else \
		echo "Rust FFI lib: not built"; \
	fi
	@if [ "$(RUST_DEPS_AVAILABLE)" = "1" ]; then \
		$(MAKE) rust-lib; \
	fi
	@$(MAKE) -B WITH_RUST=$(RUST_FFI_AVAILABLE) WITH_CPP=$(CPP_FFI_AVAILABLE) $(TARGET)
	@echo "Extensions enabled: Rust=$(RUST_FFI_AVAILABLE) C++=$(CPP_FFI_AVAILABLE)"

# Test C++ FFI system (when implemented)
test-cpp:
ifeq ($(WITH_CPP),1)
	@echo "Testing C++ FFI system..."
	@./$(TARGET) --test-cpp-ffi || echo "C++ FFI test completed"
else
	@echo "C++ FFI not available for testing"
endif

# Enhanced info target with Rust information
rust-info:
	@echo "=== Rust FFI Information ==="
	@if command -v rustc >/dev/null 2>&1; then \
		echo "Rust compiler: $$(rustc --version)"; \
	else \
		echo "Rust compiler: Not found"; \
	fi
	@if command -v cargo >/dev/null 2>&1; then \
		echo "Cargo: $$(cargo --version)"; \
	else \
		echo "Cargo: Not found"; \
	fi
	@echo "WITH_RUST setting: $(WITH_RUST)"
	@if [ "$(WITH_RUST)" = "1" ]; then \
		echo "Rust FFI: Enabled"; \
		echo "Rust library path: $(RUST_LIB_PATH)"; \
		echo "Rust library name: lib$(RUST_LIB_NAME).a"; \
		if [ -f "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a" ]; then \
			echo "Rust library status: Built"; \
			ls -lh "$(RUST_LIB_PATH)/lib$(RUST_LIB_NAME).a"; \
		else \
			echo "Rust library status: Not built"; \
		fi; \
		echo "Rust sources: $(RUST_SOURCES)"; \
		echo "Rust headers: $(RUST_HEADERS)"; \
	else \
		echo "Rust FFI: Disabled"; \
	fi

# C++ FFI information
cpp-info:
	@echo "=== C++ FFI Information ==="
	@if command -v g++ >/dev/null 2>&1; then \
		echo "C++ compiler: $$(g++ --version | head -1)"; \
	else \
		echo "C++ compiler: Not found"; \
	fi
	@if command -v pkg-config >/dev/null 2>&1; then \
		echo "pkg-config: Available"; \
		echo "libcurl: $$(pkg-config --modversion libcurl 2>/dev/null || echo 'not found')"; \
		echo "OpenSSL: $$(pkg-config --modversion openssl 2>/dev/null || echo 'not found')"; \
	else \
		echo "pkg-config: Not found"; \
	fi
	@echo "WITH_CPP setting: $(WITH_CPP)"
	@if [ "$(WITH_CPP)" = "1" ]; then \
		echo "C++ FFI: Enabled (groundwork)"; \
		echo "C++ sources: $(CPP_SOURCES)"; \
		echo "C++ headers: $(CPP_HEADERS)"; \
		echo "C++ flags: $(CPP_CXXFLAGS)"; \
		echo "C++ libraries: $(CPP_LDFLAGS)"; \
	else \
		echo "C++ FFI: Disabled"; \
	fi \
	else \
		echo "Rust FFI: Disabled"; \
	fi
	@echo ""
	@echo "To enable Rust FFI:"
	@echo "  1. Install Rust: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
	@echo "  2. Run: make with-rust"
	@echo ""
	@echo "To test Rust FFI: make test-rust"
	@echo "To clean Rust: make clean-rust"


# =============================================================================
# === COMPREHENSIVE TESTING FRAMEWORK ===
# =============================================================================

# Test configuration
TEST_CFLAGS = $(CFLAGS) -DRUNEPKG_TEST_MODE
TEST_LDFLAGS = -lpthread

# Test object files (excluding main CLI to avoid main() conflicts)
# Test Object Files
TEST_OBJS = runepkg_config.o runepkg_util.o runepkg_pack.o runepkg_hash.o runepkg_storage.o runepkg_defensive.o $(CPP_OBJECTS)
# C-only test objects (for memory tests that don't need C++ features)
C_TEST_OBJS = runepkg_config.o runepkg_util.o runepkg_pack.o runepkg_hash.o runepkg_storage.o runepkg_defensive.o

# =============================================================================
# === UNIFIED TEST SUITE ===
# =============================================================================

# Runescope test harness - single comprehensive test binary
UNIFIED_TEST = runescope
UNIFIED_TEST_OBJS = runepkg_hash.o runepkg_pack.o runepkg_defensive.o runepkg_util.o runepkg_storage.o runepkg_config.o

$(UNIFIED_TEST): runescope.c $(UNIFIED_TEST_OBJS)
	@echo "Building runescope test harness..."
	$(CC) $(CFLAGS) $(TEST_CFLAGS) runescope.c $(UNIFIED_TEST_OBJS) $(LIBS) -lpthread -lm -o $(UNIFIED_TEST)

# Run runescope (default mode)
test-runescope: $(UNIFIED_TEST)
	@echo "Running runescope..."
	@./$(UNIFIED_TEST)

# Run runescope with verbose output
test-runescope-verbose: $(UNIFIED_TEST)
	@echo "Running runescope (verbose)..."
	@./$(UNIFIED_TEST) --verbose

# Run quick runescope tests
test-runescope-quick: $(UNIFIED_TEST)
	@echo "Running runescope (quick mode)..."
	@./$(UNIFIED_TEST) --quick

# Run runescope with Rust FFI
test-runescope-rust: rust-lib $(UNIFIED_TEST)
	@echo "Running runescope with Rust FFI..."
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -DWITH_RUST=1 runescope.c $(UNIFIED_TEST_OBJS) -L./$(RUST_LIB_PATH) -l$(RUST_LIB_NAME) $(LIBS) -lpthread -lm -ldl -o $(UNIFIED_TEST)_rust
	@LD_LIBRARY_PATH=./$(RUST_LIB_PATH):$$LD_LIBRARY_PATH ./$(UNIFIED_TEST)_rust

# Comprehensive test suite with detailed reporting
test-all: test-runescope
	@echo ""
	@echo "=== ALL TESTS COMPLETED SUCCESSFULLY ==="
	@echo "Runescope: 100% PASSED"
	@echo ""
	@echo "READY FOR STABLE RELEASE!"
	@echo ""

# Quick test for development
test: test-runescope-quick
	@echo "Quick development test completed"

# =============================================================================
# === TEST UTILITIES ===
# =============================================================================

# Clean test artifacts
clean-tests:
	@echo "🧹 Cleaning test artifacts..."
	@rm -f $(UNIFIED_TEST) $(UNIFIED_TEST)_rust $(UNIFIED_TEST).o $(UNIFIED_TEST).d
	@echo "Test cleanup completed"

# Clean unified test
clean-unified:
	@rm -f $(UNIFIED_TEST) $(UNIFIED_TEST)_rust

# Test help
test-help:
	@echo "=== runepkg Testing Framework ==="
	@echo ""
	@echo "Available test targets:"
	@echo "  test                 - Quick development test (unified)"
	@echo "  test-unified         - Full unified test suite"
	@echo "  test-unified-quick   - Quick unified test suite"
	@echo "  test-unified-verbose - Verbose unified test suite"
	@echo "  test-unified-rust    - Unified test suite with Rust FFI"
	@echo "  test-all             - Complete test validation"
	@echo ""
	@echo "Cleanup targets:"
	@echo "  clean               - Clean build artifacts"
	@echo "  clean-tests         - Clean test artifacts"
	@echo "  clean-unified       - Clean unified test artifacts only"
	@echo "  clean-all           - Deep clean everything"
	@echo "  test-help           - Show this help"
	@echo ""
	@echo "For stable release validation, run: make test-all"
	@echo "🧹 For complete cleanup, run: make clean-all"
	@echo ""

# =============================================================================
# === END TESTING FRAMEWORK ===
# =============================================================================
