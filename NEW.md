runepkg is a native Debian package compiler and manager. It provides a high-performance, dual-tier architecture for compiling Debian source packages to arbitrary target architectures and libc implementations. The tool is engineered for embedded systems development, cross-compilation workflows, and environments where precise control over binary dependencies and compilation targets is required.

Unlike package managers that operate within a fixed distribution (such as apt or dpkg), runepkg treats the entire Debian package ecosystem (112,000+ source and binary packages) as a supply chain. It enables developers to:

Extract and compile Debian source packages for non-native architectures (e.g., ARM, RISC-V, x86_64 with musl)
Build minimal, dependency-reduced .deb binaries for embedded deployment
Construct isolated, disposable cross-compilation toolchains on demand
Deploy fully Debian-compatible packages to constrained environments without distribution overhead
runepkg is available in two complementary builds:

Minimal Core (C89): A 417 KB–536 KB static binary for local Debian package management, autocompletion, and basic source inspection. Designed for environments with stringent resource constraints.
Extended Suite (C++ FFI): A 2.5 MB fully static binary incorporating parallel repository synchronization, source package compilation, and cross-toolchain management. Compatible with Debian repositories for both binary and source operations.
Architecture & System Support
runepkg employs a dual-tier architecture to address distinct operational requirements:

Minimal Core (Pure C89): The foundation is strictly ANSI C89/C90 compliant, ensuring maximum portability across legacy and contemporary C compilers. This version is optimized for resource-constrained embedded systems, requiring only standard POSIX utilities.

Extended (C++ FFI): An optional C++ layer provides high-performance parallel networking, Debian repository synchronization, and native Debian source package compilation. All binary dependencies are included in static builds, eliminating runtime library requirements.

Compiler Flexibility: The build system permits compiler substitution at compile time. Users may select gcc, clang, tcc, pcc, or zig cc to accommodate specific target environments or historical toolchain constraints.

Utility Compatibility: runepkg integrates with standard GNU tools (ar, tar, gzip, xz) and operates in resource-constrained environments using BusyBox when necessary. When using BusyBox, ensure that the build includes the required applets (ar, tar, gzip, xz) with symlink installation enabled.

Technical Approach: The runepkg Difference
runepkg abandons the sequential, text-parsing model of traditional Debian tooling in favor of a performance-first architecture optimized for embedded systems workflows:

Binary Metadata Serialization: Rather than parsing flat Debian package index files (Packages, Sources), runepkg pre-computes and serializes metadata into binary structures (pkginfo.bin, runes_graph.bin). This eliminates repeated text parsing and enables O(1) package lookups via FNV-1a hash tables.

Minimal Dependency Graphs: The dependency resolver constructs a minimal, target-specific dependency graph. It resolves 112,000+ source packages to only those required by the target compilation, reducing build complexity and compilation time.

Parallel Repository Operations: A dual-concurrency model provides high-speed parallel downloads via C++ thread pools (mapped to hardware concurrency) for repository synchronization and package fetching. The C-based core remains single-threaded and deterministic for local operations.

Local-First Resolution: Before fetching from remote repositories, runepkg automatically detects and uses sibling .deb files in the local directory or download cache. This optimization is critical for embedded workflows where multiple packages are built sequentially.

Debian Source Compilation: An integrated C++ pipeline directly compiles Debian source packages (.dsc format) without requiring external build orchestration. The resulting .deb binaries are fully compatible with Debian repositories and tooling.

Disposable Toolchain Construction: The bootstrap <target> [pkgs...] command constructs an isolated, minimal cross-compilation environment containing only the binutils, GCC, kernel headers, and libc required to compile the specified packages. The toolchain is completely independent of the host system and can be discarded after use (build-toolchain option reserved for future development).

Static Linking & Portability: All extended-suite builds produce 100% statically-linked binaries. This ensures deployment to embedded systems without dependency resolution or dynamic loader compatibility concerns.

Security Hardening: The core engine implements a hardened memory model with secure_malloc, automatic zero-wiping of sensitive data, and strict path traversal validation. Supports optional detached GPG signatures (.sig files) for per-package cryptographic verification.

Key Features
1: Direct Debian Package Building
For developers working with Debian packaging workflows, runepkg provides direct .deb compilation without invoking complex build harnesses, dpkg-dev, or debhelper.

Direct Compilation: runepkg build <dir> [output.deb] compiles a .deb from a prepared directory structure. Intelligently detects standard Debian package layouts.

Manual Staging: Prepare source code and files using standard build tools. Invoke make install DESTDIR=/path/to/staging to populate a staging directory, then use runepkg -b /path/to/staging to package the results.

Filesystem Hierarchy Bootstrapping: When installing to non-root directories, runepkg automatically initializes a complete FHS skeleton, enabling sandboxed package installations and testing.

Minimal Dependencies: No requirement for debhelper, dpkg-dev, or Debian build infrastructure. Suitable for embedded systems and custom build environments.

2: Source Package Inspection and Compilation
runepkg provides surgical control over Debian source package operations:

Source Extraction: runepkg source <pkg> downloads and extracts only the upstream source and Debian patches into the build directory. Unlike apt-get source, this avoids downloading or installing build-dependencies.

Targeted Dependency Resolution:

source-depends <pkg> fetches the source package and all runtime dependencies required for execution.
source-build-depends <pkg> fetches the source package and all binary packages required to compile it.
Multi-Package Building: runepkg buildpkg-split <pkg|dir|.dsc> [target] compiles a Debian source package and optionally segments the output into multiple .deb files (e.g., bin, dev, doc, locale).

3: Cross-Compilation Workflows
runepkg enables rapid construction of cross-compilation environments for embedded targets:

Toolchain Synthesis: bootstrap <target> [pkgs...] constructs a minimal, isolated cross-compilation environment containing only the dependencies necessary to compile the specified packages for the target architecture and libc combination. (The legacy build-toolchain CLI option remains reserved for future development).

Target Profiles: Pre-defined or custom profiles specify target architecture (e.g., arm-linux-musleabihf, riscv64-linux-musl), linking strategy (static/shared), and sysroot location.

Debian-Compatible Output: All compiled .deb packages remain fully compatible with Debian tooling and repositories. Cross-compiled binaries may be deployed directly to target systems or integrated into embedded Linux distributions.

