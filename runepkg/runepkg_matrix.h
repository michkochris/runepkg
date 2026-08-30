/*****************************************************************************
 * Filename:    runepkg_matrix.h
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-30
 * Description: Binary Matrix DB Layout & Matrix Engine Definitions
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_MATRIX_H
#define RUNEPKG_MATRIX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

extern "C" {
#endif

#define RUNE_MATRIX_MAGIC 0x52554E45 /* "RUNE" */
#define RUNE_MATRIX_VERSION 1

/* Compatibility & Behavioral Flags (Bitfield) */
typedef enum {
    RULE_FLAG_FORCE_STATIC      = 1 << 0,
    RULE_FLAG_DISABLE_LTO       = 1 << 1,
    RULE_FLAG_INJECT_FTS_SHIM   = 1 << 2, /* Needed for musl libc */
    RULE_FLAG_INJECT_ARG_MAX    = 1 << 3,
    RULE_FLAG_NO_SHARED_LIBS    = 1 << 4,
    RULE_FLAG_OVERRIDE_AUTOTOOLS= 1 << 5,
    RULE_FLAG_NEEDS_PTHREAD     = 1 << 6
} RuneRuleFlags;

/* Target Constraint Bitmasks */
typedef enum {
    TARGET_LIBC_ANY   = 0,
    TARGET_LIBC_MUSL  = 1 << 0,
    TARGET_LIBC_GLIBC = 1 << 1,
    TARGET_LIBC_UCLIBC= 1 << 2
} TargetLibcMask;

typedef enum {
    TARGET_ARCH_ANY     = 0,
    TARGET_ARCH_X86_64  = 1 << 0,
    TARGET_ARCH_AARCH64 = 1 << 1,
    TARGET_ARCH_ARMV7   = 1 << 2,
    TARGET_ARCH_RISCV64 = 1 << 3
} TargetArchMask;

/* File Header */
struct RuneMatrixHeader {
    uint32_t magic;           /* RUNE_MATRIX_MAGIC */
    uint16_t version;         /* Format version */
    uint16_t entry_size;      /* Size of RuneMatrixRecord for alignment checks */
    uint32_t bucket_count;    /* Number of prime hash buckets */
    uint32_t record_count;    /* Total rules stored */
    uint32_t string_pool_off; /* Offset to start of string pool */
};

/* Individual Rule Record (48 bytes fixed size) */
struct RuneMatrixRecord {
    uint32_t pkg_name_hash;   /* Precomputed FNV-1a hash of package name */
    uint32_t pkg_name_offset; /* Offset in string pool for verification */
    uint16_t libc_mask;       /* Bitmask of affected libcs */
    uint16_t arch_mask;       /* Bitmask of affected architectures */
    uint32_t rule_flags;      /* Bitfield of RuneRuleFlags */
    uint8_t  build_system;    /* 0=Auto, 1=Cmake, 2=Meson, 3=Make */
    uint8_t  padding[3];      /* Alignment */
    uint32_t extra_conf_off;  /* Offset to string pool for configure args */
    uint32_t extra_cflags_off;/* Offset to string pool: "-D_GNU_SOURCE ..." */
    uint32_t extra_ldflags_off;/* Offset to string pool: "-lfts ..." */
    uint32_t make_build_off;  /* Offset to string pool for custom make targets */
    uint32_t make_inst_off;   /* Offset to string pool for custom install targets */
    uint32_t pre_conf_off;    /* Offset to string pool for pre-configure shell hook */
    uint32_t build_override_off; /* Offset to string pool for build-time shell override */
    uint32_t post_inst_off;   /* Offset to string pool for post-install shell hook */
    uint32_t next_record_off; /* Hash collision chain (offset in file, 0 = end) */
};

#ifdef __cplusplus
} /* extern "C" */

/* -------------------------------------------------------------------------- */
/* C++ Matrix Engine & Recipe Definitions                                     */
/* -------------------------------------------------------------------------- */

enum class BuildSystem {
    AUTOTOOLS,
    CMAKE,
    MESON,
    CUSTOM_MAKE
};

struct ProfileMatrix {
    std::string profile_name;
    std::string arch;        /* "x86_64", "aarch64", "armhf", "riscv64" */
    std::string libc;        /* "musl", "glibc" */
    bool is_static = false;
    bool is_pie = false;
    std::string triplet;
    std::string sysroot;
    std::string crosstools;
    std::string cross_bin;
};

struct RuneMatrixRecipe {
    std::string package_name;
    BuildSystem build_system = BuildSystem::AUTOTOOLS;

    std::vector<std::string> extra_conf_args;
    std::vector<std::string> extra_cflags;
    std::vector<std::string> extra_ldflags;
    std::vector<std::string> make_build_targets;
    std::vector<std::string> make_install_targets;

    /* Shell-based hooks (loaded from binary DB) */
    std::string pre_configure_shell;
    std::string build_override_shell;
    std::string post_install_shell;
};

class RuneMatrixEngine {
public:
    static ProfileMatrix parse_profile(const std::string& profile_name);
    static const RuneMatrixRecipe* get_recipe(const std::string& pkg_name);
    static BuildSystem detect_build_system(const fs::path& src_dir);

    /* Binary Matrix Support */
    static bool is_binary_stale();
    static bool recompile_binary();
    static const RuneMatrixRecord* find_record(const std::string& pkg_name);
};

#endif /* __cplusplus */

#endif /* RUNEPKG_MATRIX_H */
