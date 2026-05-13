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

# Force core build when explicitly targeting runepkg or install
# This ensures 'make install' doesn't try to upgrade a core build to C++
# unless 'make all install' is used.
ifneq (,$(filter runepkg install,$(MAKECMDGOALS)))
ifeq (,$(filter all with-all with-cpp,$(MAKECMDGOALS)))
WITH_CPP := 0
endif
endif

# C++ FFI support detection and configuration
WITH_CPP ?= auto
CPP_CXXFLAGS = -std=c++17 -Wall -Wextra -O2
CPP_LDFLAGS = -lcurl -lssl -lcrypto -lz -lpthread
CXX_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && echo 1 || echo 0)
LIBCURL_AVAILABLE := $(shell pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_DEPS_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_WRAPPER_EXISTS := $(if $(wildcard runepkg_network.cpp),1,0)
CPP_FFI_AVAILABLE := $(if $(and $(filter 1,$(CPP_DEPS_AVAILABLE)),$(filter 1,$(CPP_WRAPPER_EXISTS))),1,0)

# Check if C++ FFI should be enabled
ifeq ($(WITH_CPP),auto)
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

# Configure C++ FFI if enabled
ifeq ($(WITH_CPP),1)
ifeq ($(CPP_WRAPPER_EXISTS),0)
WITH_CPP = 0
endif
endif

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
.config_with_cpp:
	@echo $(WITH_CPP) > $@.tmp
	@if [ ! -f $@ ] || ! diff $@ $@.tmp >/dev/null; then mv $@.tmp $@; else rm $@.tmp; fi

.PHONY: all clean clean-all install debug run termux-install uninstall test test-binary test-help info with-cpp clean-cpp with-all

.DEFAULT_GOAL := runepkg

runepkg: WITH_CPP=0

-include $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)

# --- Installation Variables ---
DESTDIR ?=
PREFIX ?= /usr
ETCDIR ?= /etc
TERMUX_PREFIX ?= /data/data/com.termux/files/usr

all: with-all

$(TARGET): $(OBJS) .config_with_cpp
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "⚡ Build complete: $(TARGET)"

%.o: %.c $(HEADERS) .config_with_cpp
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp $(HEADERS) .config_with_cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(OBJS) $(TARGET) $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)
	@echo "🧹 Clean complete."

test-binary: $(TARGET)
	@echo "Testing basic functionality..."
	@if [ -f ./$(TARGET) ]; then \
		echo "Binary exists, testing help output:"; \
		./$(TARGET) --help; \
	else \
		echo "Binary not found!"; \
		exit 1; \
	fi

install: runepkg
	@echo "Installing $(TARGET) for Linux system to $(DESTDIR)$(PREFIX)/bin..."
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Determining configuration installation location..."
	@if [ -f runepkgconfig ]; then \
		if [ "$(shell id -u)" = "0" ] || [ -w "$(DESTDIR)$(ETCDIR)" ] 2>/dev/null; then \
			echo "Installing system-wide configuration to $(DESTDIR)$(ETCDIR)/runepkg..."; \
			mkdir -p $(DESTDIR)$(ETCDIR)/runepkg; \
			cp runepkgconfig $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			chmod 644 $(DESTDIR)$(ETCDIR)/runepkg/runepkgconfig; \
			echo "Configuration file installed to $(ETCDIR)/runepkg/runepkgconfig"; \
		else \
			echo "ERROR: No elevated privileges detected. Configuration requires system install."; \
			exit 1; \
		fi; \
	fi

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
	@$(MAKE) -B WITH_CPP=$(CPP_FFI_AVAILABLE) $(TARGET)

test: test-binary
