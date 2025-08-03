#
# ******************************************************************************
# * Filename:    Makefile
# * Author:      <michkochris@gmail.com>
# * Date:        started 12-31-2024
# * Description: upkg manages linux .deb pkg's
# *
# * Copyright (c) 2025 upkg (ulinux) All rights reserved.
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
# Makefile for upkg with consolidated configuration system.
# This file compiles the C source code into an executable.
# It also handles creating a default config file and installing the executable.

CC = gcc
# Updated CFLAGS with _GNU_SOURCE and improved flags for consolidated system
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP
LDFLAGS =
LIBS =

TARGET = upkg

# Source files - Updated to reflect the new consolidated structure (only the essentials)
SRCS = upkg_cli.c upkg_config.c
OBJS = $(SRCS:.c=.o)

# Header dependencies
HEADERS = upkg_config.h

# Phony targets
.PHONY: all clean install debug run termux-install create-user-config uninstall test info

# Include generated dependency files
-include $(SRCS:.c=.d)

# --- Installation Variables ---
# DESTDIR is the standard variable for packaging; set it for a staged install.
DESTDIR ?=
# PREFIX is where the executable goes, typically /usr or /usr/local
PREFIX ?= /usr/local
# ETCDIR is where the system-wide configuration goes
ETCDIR ?= /etc
# TERMUX_PREFIX is the base directory for Termux installs
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "Build complete: $(TARGET)"

%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up..."
	rm -f $(OBJS) $(TARGET) $(SRCS:.c=.d)
	@echo "Clean complete."

# Test compilation only (useful for checking syntax without running)
test: $(TARGET)
	@echo "Testing basic functionality..."
	@if [ -f ./$(TARGET) ]; then \
		echo "Binary exists, testing help output:"; \
		./$(TARGET) --help; \
	else \
		echo "Binary not found!"; \
		exit 1; \
	fi

# Create user-specific configuration, separate from the install process
create-user-config:
	@echo "Creating user configuration..."
	@if [ ! -f ~/.upkgconfig ]; then \
		if [ -f upkgconfig ]; then \
			cp upkgconfig ~/.upkgconfig; \
			echo "Default upkgconfig created at ~/.upkgconfig"; \
		else \
			echo "ERROR: upkgconfig file not found in current directory!"; \
			exit 1; \
		fi; \
	else \
		echo "upkgconfig already exists at ~/.upkgconfig. Not overwriting."; \
	fi

# The main install target for standard Linux systems
# This installs the binary to PREFIX/bin and config to ETCDIR/upkg/
# To install, run 'sudo make install'
# To create a package, run 'make DESTDIR=/path/to/staging/ install'
install: all
	@echo "Installing $(TARGET) for Linux system to $(DESTDIR)$(PREFIX)/bin..."
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Installing system-wide configuration to $(DESTDIR)$(ETCDIR)/upkg..."
	mkdir -p $(DESTDIR)$(ETCDIR)/upkg
	@if [ -f upkgconfig ]; then \
		cp upkgconfig $(DESTDIR)$(ETCDIR)/upkg/upkgconfig; \
		chmod 644 $(DESTDIR)$(ETCDIR)/upkg/upkgconfig; \
		echo "Configuration file installed to $(ETCDIR)/upkg/upkgconfig"; \
	else \
		echo "WARNING: upkgconfig file not found in current directory!"; \
	fi
	@echo ""
	@echo "=== Linux Installation Complete ==="
	@echo "Binary: $(PREFIX)/bin/$(TARGET)"
	@echo "System config: $(ETCDIR)/upkg/upkgconfig"
	@echo ""
	@echo "The cascading configuration will check:"
	@echo "  1. \$$UPKG_CONFIG_PATH environment variable"
	@echo "  2. $(ETCDIR)/upkg/upkgconfig (system-wide)"
	@echo "  3. ~/.upkgconfig (user-specific)"
	@echo ""
	@echo "To create a user-specific config, run: make create-user-config"

# Termux-specific install target
# This installs the binary to TERMUX_PREFIX/bin and creates user config at ~/.upkgconfig
# This assumes the user is running 'make termux-install' inside Termux.
termux-install: all
	@echo "Installing $(TARGET) for Termux to $(TERMUX_PREFIX)/bin..."
	mkdir -p $(TERMUX_PREFIX)/bin
	cp $(TARGET) $(TERMUX_PREFIX)/bin/$(TARGET)
	chmod 755 $(TERMUX_PREFIX)/bin/$(TARGET)
	@echo "Creating user-specific configuration file at ~/.upkgconfig..."
	@if [ -f upkgconfig ]; then \
		cp upkgconfig ~/.upkgconfig; \
		chmod 644 ~/.upkgconfig; \
		echo "Configuration file created at ~/.upkgconfig"; \
	else \
		echo "ERROR: upkgconfig file not found in current directory!"; \
		exit 1; \
	fi
	@echo ""
	@echo "=== Termux Installation Complete ==="
	@echo "Binary: $(TERMUX_PREFIX)/bin/$(TARGET)"
	@echo "User config: ~/.upkgconfig"
	@echo ""
	@echo "The cascading configuration will check:"
	@echo "  1. \$$UPKG_CONFIG_PATH environment variable"
	@echo "  2. /etc/upkg/upkgconfig (system-wide - not available in Termux)"
	@echo "  3. ~/.upkgconfig (user-specific - PRIMARY for Termux)"
	@echo ""
	@echo "You can now run 'upkg <command>' from anywhere in Termux."

# Uninstall target for both standard Linux and Termux
uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	# Standard Linux uninstall
	@if [ -f $(PREFIX)/bin/$(TARGET) ]; then \
		echo "Removing $(PREFIX)/bin/$(TARGET)..."; \
		rm -f $(PREFIX)/bin/$(TARGET); \
		echo "Linux binary removed."; \
	fi
	@if [ -f $(ETCDIR)/upkg/upkgconfig ]; then \
		echo "Removing $(ETCDIR)/upkg/upkgconfig..."; \
		rm -f $(ETCDIR)/upkg/upkgconfig; \
		rmdir $(ETCDIR)/upkg 2>/dev/null || true; \
		echo "Linux system config removed."; \
	fi
	# Termux uninstall
	@if [ -f $(TERMUX_PREFIX)/bin/$(TARGET) ]; then \
		echo "Removing $(TERMUX_PREFIX)/bin/$(TARGET)..."; \
		rm -f $(TERMUX_PREFIX)/bin/$(TARGET); \
		echo "Termux binary removed."; \
	fi
	@echo "Uninstallation complete."
	@echo "NOTE: User-specific config file ~/.upkgconfig was NOT removed."
	@echo "To remove it manually, run: rm ~/.upkgconfig"

# Debug build with extra debugging symbols and verbose output
debug: CFLAGS += -DDEBUG -O0
debug: all
	@echo "Debug build complete. Run with -v for verbose output."

# Development target - build and run with help
run: all
	@echo "Running $(TARGET) with help option..."
	./$(TARGET) --help

# Development target - build and run with version
version: all
	@echo "Running $(TARGET) with version option..."
	./$(TARGET) --version

# Development target - build and test verbose mode (requires config file)
test-verbose: all create-user-config
	@echo "Testing $(TARGET) in verbose mode..."
	./$(TARGET) -v --version

# Show information about the build and configuration
info:
	@echo "=== Build Information ==="
	@echo "Target: $(TARGET)"
	@echo "Source files: $(SRCS)"
	@echo "Object files: $(OBJS)"
	@echo "Headers: $(HEADERS)"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo ""
	@echo "=== Installation Paths ==="
	@echo "Linux Binary: $(PREFIX)/bin/$(TARGET)"
	@echo "Linux Config: $(ETCDIR)/upkg/upkgconfig"
	@echo "Termux Binary: $(TERMUX_PREFIX)/bin/$(TARGET)"
	@echo "User Config: ~/.upkgconfig"
	@echo ""
	@echo "=== Config File Cascading Order ==="
	@echo "1. \$$UPKG_CONFIG_PATH (environment override)"
	@echo "2. $(ETCDIR)/upkg/upkgconfig (system-wide)"
	@echo "3. ~/.upkgconfig (user-specific)"
	@echo ""
	@echo "=== Available Targets ==="
	@echo "make               - Build the binary"
	@echo "make install       - Install for Linux (needs sudo)"
	@echo "make termux-install - Install for Termux"
	@echo "make create-user-config - Create ~/.upkgconfig"
	@echo "make test          - Build and test basic functionality"
	@echo "make clean         - Clean build artifacts"
	@echo "make uninstall     - Remove installed files"
	@echo "make debug         - Build with debug flags"
	@echo "make info          - Show this information"
	@echo "=========================="
# ETCDIR is where the system-wide configuration goes
ETCDIR ?= /etc
# TERMUX_PREFIX is the base directory for Termux installs
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(SRCS:.c=.d)

# New create-user-config target, now separate from the install process
create-user-config:
	@if [ ! -f ~/.upkgconfig ]; then \
		cp upkgconfig ~/.upkgconfig; \
		echo "Default upkgconfig created at ~/.upkgconfig"; \
	else \
		echo "upkgconfig already exists at ~/.upkgconfig. Not overwriting."; \
	fi

# The main install target, now using DESTDIR for flexibility
# To install, run 'sudo make install'
# To create a package, run 'make DESTDIR=/path/to/staging/ install'
install: all
	@echo "Installing $(TARGET) to $(DESTDIR)$(PREFIX)/bin..."
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Installing system-wide configuration to $(DESTDIR)$(ETCDIR)/upkg..."
	mkdir -p $(DESTDIR)$(ETCDIR)/upkg
	cp upkgconfig $(DESTDIR)$(ETCDIR)/upkg/upkgconfig
	@echo "Installation complete for a standard Linux system."

# Termux-specific install target (installs to /data/data/com.termux/files/usr/bin)
# Note: A system install for Termux is more common than a user-specific one.
# This assumes the user is running 'make termux-install' inside Termux.
termux-install: all
	@echo "Installing $(TARGET) for Termux to $(TERMUX_PREFIX)/bin..."
	mkdir -p $(TERMUX_PREFIX)/bin
	cp $(TARGET) $(TERMUX_PREFIX)/bin/$(TARGET)
	@echo "Creating user-specific configuration file at ~/.upkgconfig..."
	cp upkgconfig ~/.upkgconfig
	@echo "Installation complete for Termux. You can now run 'upkg <command>'."

uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	sudo rm -f $(PREFIX)/bin/$(TARGET)
	sudo rm -f $(ETCDIR)/upkg/upkgconfig
	rmdir $(ETCDIR)/upkg 2>/dev/null || true
	# Also remove the Termux-specific installation
	rm -f $(TERMUX_PREFIX)/bin/$(TARGET)
	@echo "Uninstallation attempt complete. Note: user-specific config file ~/.upkgconfig was not removed."

debug: CFLAGS += -DDEBUG
debug: all

run: all
	./$(TARGET)
