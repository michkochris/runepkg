# Installation Guide for upkg

## Overview

`upkg` is a lightweight package manager for Linux systems that manages `.deb` packages. This guide covers installation for both standard Linux systems and Termux (Android terminal emulator).

## Prerequisites

### For Standard Linux Systems:
- GCC compiler
- Make utility
- Standard C libraries
- Root access (for system-wide installation)

### For Termux:
- Termux app installed on Android
- Required packages: `gcc`, `make`, `binutils`

```bash
# Install build tools in Termux
pkg update
pkg install gcc make binutils
```

## Quick Start

### Standard Linux Installation
```bash
# Clone or download the source code
# cd into the upkg directory

# Build and install system-wide (requires sudo)
make
sudo make install

# Optional: Create user-specific config
make create-user-config
```

### Termux Installation
```bash
# Clone or download the source code
# cd into the upkg directory

# Build and install for Termux
make
make termux-install
```

## Detailed Installation Instructions

### 1. Building upkg

The build process is the same for both Linux and Termux:

```bash
make
```

This compiles the source files (`upkg_cli.c` and `upkg_config.c`) into the `upkg` executable.

#### Build Options:
- `make` - Standard build
- `make debug` - Build with debug symbols and verbose output
- `make clean` - Remove build artifacts

#### Testing the Build:
```bash
make test          # Test basic functionality
make test-verbose  # Test with verbose configuration loading
```

### 2. Installation for Standard Linux Systems

#### System-wide Installation (Recommended)
```bash
sudo make install
```

This installs:
- **Binary**: `/usr/local/bin/upkg`
- **System config**: `/etc/upkg/upkgconfig`

#### Custom Installation Paths
```bash
# Install to /usr instead of /usr/local
sudo make install PREFIX=/usr

# Install to custom location
sudo make install PREFIX=/opt/upkg

# Package creation (for distribution)
make install DESTDIR=/tmp/upkg-package PREFIX=/usr
```

#### User Configuration (Optional)
```bash
make create-user-config
```

Creates `~/.upkgconfig` for user-specific settings.

### 3. Installation for Termux

```bash
make termux-install
```

This installs:
- **Binary**: `/data/data/com.termux/files/usr/bin/upkg`
- **User config**: `~/.upkgconfig`

## Configuration System

upkg uses a **cascading configuration system** that checks for configuration files in this order:

1. **Environment Variable**: `$UPKG_CONFIG_PATH`
2. **System-wide**: `/etc/upkg/upkgconfig` (Linux only)
3. **User-specific**: `~/.upkgconfig`

### Configuration File Locations

| System | Priority | Location | Description |
|--------|----------|----------|-------------|
| Linux | 1 | `$UPKG_CONFIG_PATH` | Environment override |
| Linux | 2 | `/etc/upkg/upkgconfig` | System-wide config |
| Linux | 3 | `~/.upkgconfig` | User-specific config |
| Termux | 1 | `$UPKG_CONFIG_PATH` | Environment override |
| Termux | 2 | *N/A* | No system-wide config |
| Termux | 3 | `~/.upkgconfig` | Primary config for Termux |

### Configuration File Format

The `upkgconfig` file uses simple key=value pairs:

```bash
# Main working directory
upkg_dir=~/upkg_dir

# Control files extraction
control_dir=~/upkg_dir/controldir

# Installation target (/ for system, ~/upkg_dir/installdir for testing)
install_dir=~/upkg_dir/installdir

# Package database
db_dir=~/upkg_dir/db
```

### Custom Configuration

#### Using Environment Variable
```bash
export UPKG_CONFIG_PATH=/path/to/custom/upkgconfig
upkg --version  # Will use your custom config
```

#### Modifying User Config
```bash
# Edit user-specific configuration
nano ~/.upkgconfig

# Test the configuration
upkg -v --version
```

## Usage Examples

### Basic Commands
```bash
# Show help
upkg --help

# Show version and configuration info
upkg -v --version

# Install packages
upkg -i package1.deb package2.deb

# List installed packages
upkg -l

# Remove a package
upkg -r package-name

# Show package status
upkg -s package-name

# Search for packages
upkg -S search-term
```

### Verbose Mode
```bash
# Enable verbose output to see configuration loading
upkg -v -l
```

This shows which configuration file is being used and the resolved paths.

## Troubleshooting

### Build Issues

**Missing `strdup` function:**
```bash
# Ensure _GNU_SOURCE is defined (should be automatic)
gcc -D_GNU_SOURCE -o upkg upkg_cli.c upkg_config.c
```

**PATH_MAX not defined:**
- The code automatically defines PATH_MAX if not available
- This is typically only an issue on very minimal systems

### Configuration Issues

**No configuration file found:**
```bash
# Check if config file exists
ls -la ~/.upkgconfig
ls -la /etc/upkg/upkgconfig

# Create user config if missing
make create-user-config

# Test with verbose mode
upkg -v --version
```

**Permission denied:**
```bash
# Check file permissions
ls -la ~/.upkgconfig

# Fix permissions if needed
chmod 644 ~/.upkgconfig
```

### Runtime Issues

**Binary not found:**
```bash
# Check if binary is in PATH
which upkg

# For Termux, ensure /data/data/com.termux/files/usr/bin is in PATH
echo $PATH
```

## Uninstallation

### Remove upkg
```bash
# Remove installed files (works for both Linux and Termux)
sudo make uninstall  # Use sudo only on Linux

# Manually remove user config if desired
rm ~/.upkgconfig
```

### Clean Build Directory
```bash
make clean
```

## Development and Testing

### Development Targets
```bash
make info          # Show build and installation information
make run           # Build and run with --help
make version       # Build and run with --version
make test-verbose  # Build and test with verbose config loading
```

### Building for Distribution
```bash
# Create staged installation for packaging
make install DESTDIR=/tmp/upkg-package PREFIX=/usr

# The package contents will be in /tmp/upkg-package/
```

## Advanced Configuration

### Multiple Environments
You can maintain different configurations for different environments:

```bash
# Development environment
export UPKG_CONFIG_PATH=~/.upkgconfig-dev
upkg -v --version

# Production environment  
export UPKG_CONFIG_PATH=/etc/upkg/upkgconfig-prod
upkg -v --version
```

### System Administration

For system administrators managing upkg across multiple users:

1. Install system-wide: `sudo make install`
2. Configure system defaults in `/etc/upkg/upkgconfig`
3. Users can override with `~/.upkgconfig` if needed
4. Use `UPKG_CONFIG_PATH` for special cases

## Support

- Configuration files use simple key=value format
- Paths support tilde (`~`) expansion
- All directories are created automatically
- Use `-v` flag to debug configuration loading
- Check the cascading order if configuration seems wrong

For issues, check:
1. File permissions
2. Configuration file syntax
3. Path accessibility
4. Environment variables

