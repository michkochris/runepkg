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
# Makefile for upkg.
# This file compiles the C source code into an executable.
# It also handles creating a default config file and installing the executable.

CC = gcc
# Added -MMD and -MP for automatic dependency generation (useful for header changes)
CFLAGS = -Wall -Wextra -std=c11 -g -MMD -MP
LDFLAGS =
LIBS =

TARGET = upkg

# Source files (Ensure all .c files that compile to part of your project are listed here)
# upkg.c is renamed to upkg_cli.c and we've added a new upkg_config.c
SRCS = upkg_cli.c upkg_config.c upkg_lib.c upkg_script.c upkg_hash.c upkg_struct.c upkg_exec.c upkg_highlight.c
OBJS = $(SRCS:.c=.o)

# Phony targets
.PHONY: all clean install debug run termux-install create-user-config uninstall

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
