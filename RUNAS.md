# 🕯️ `runas`: High-Performance Debian Source Package & Toolchain Forge Engine

**runas** is the advanced C++17 source-compiling companion executable for **runepkg**, affectionately named after *Runas the Shamed* from *World of Warcraft*. While `runepkg` manages binary `.deb` installation, removal, and graph resolution, `runas` acts as a specialized Debian source package builder and multi-arch cross-compilation forge.

*Kobold Says "You no take candle!"*

For complete step-by-step tutorials and cross-architecture build workflows, read on below.

---

## 1. Core Responsibilities & Architecture

To maintain a pristine host environment regardless of host architecture (`amd64`, `arm64`, `riscv64`, etc.), `runas` and `runepkg` enforce strict separation between host build tools and target sysroots:

* **Host Build Environment (Native Host)**: Executes native compilers (`gcc`, `g++`), build automation (`make`, `autoconf`, `automake`), and packaging tools (`dpkg-dev`, `debhelper`). `runepkg` automatically detects your native host architecture via multi-arch introspection.
* **Target Sysroot (`/mnt/runepkg/<target-triplet>`)**: Isolated multi-arch root (e.g., `/mnt/runepkg/aarch64-linux-gnu`) containing target-architecture runtime libraries and header files (`*-dev`).

---

## 2. Step-by-Step Cross-Build Workflow

### Step 1: Initialize Host State
Before building or resolving build dependencies, ensure your host is synced so `runepkg` knows what packages are already present on your system:
```bash
runepkg update
runepkg sync
```

### Step 2: Initialize Target Profile & Sysroot
Provision and update indexes for your target architecture (e.g., `arm64:aarch64-linux-gnu`):
```bash
runepkg --target=arm64:aarch64-linux-gnu update
```

### Step 3: Inspect & Install Target Build Dependencies
Query the target build dependency graph for your package (e.g., `file`):
```bash
runepkg --target=arm64:aarch64-linux-gnu build-depends file
```
`runepkg` renders the recursive ASCII dependency tree and automatically outputs a **copy-pasteable one-liner** at the bottom. Copy and paste it directly into `runepkg -i`:
```bash
runepkg --target=arm64:aarch64-linux-gnu -i <paste-one-liner-here>
```
This stages all target `-dev` headers and libraries strictly into `/mnt/runepkg/aarch64-linux-gnu` without touching your host `/usr`.

### Step 4: Forge & Build with `runas`
Once the target sysroot is populated, invoke `runas` to verify host build tools, set up cross-compilation environment variables (`CC`, `PKG_CONFIG_LIBDIR`), and build the package:
```bash
runas --target=arm64:aarch64-linux-gnu build file
```
To split a source package into all its individual subpackages (e.g., binaries, dev libraries, doc packages), use:
```bash
runas --target=arm64:aarch64-linux-gnu buildpkg-split file
```

---

## 3. Reference: `runas` Command Summary

| Command | Description |
| :--- | :--- |
| `runas sync` | Synchronize host package database snapshot (`/var/lib/dpkg/status`) |
| `runas update` | Download repository indices & update binary graphs |
| `runas source <pkg>` | Download Debian source package files (`.dsc`, `.orig`, `.debian`) |
| `runas prepare <pkg>` | Unpack source rune, apply Debian patches, & fix timestamps |
| `runas build <pkg>` | Verify host build tools and cross-compile primary package |
| `runas buildpkg-split <pkg>` | Build source package and split into all subpackages |
| `runas source-build <pkg>` | Run full 3-Stage Pipeline: fetch runes ➔ verify tools ➔ build `.deb` |
| `runas autocomplete <query>` | Run $O(1)$ memory-mapped binary autocomplete lookup |
