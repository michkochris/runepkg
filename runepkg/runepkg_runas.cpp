/******************************************************************************/
/* Filename:    runepkg_runas.cpp                                              */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: Interleaved Autocomplete & Source Engine (runas) Module        */
/* License:     GPL v3                                                         */
/******************************************************************************/

#include <iostream>
#include <string>
#include <vector>
#include <future>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <unistd.h>

#include "runepkg_runas.hpp"
#include "runepkg_cpp_ffi.h"
#include "runepkg_portable.h"
#include "runepkg_multiarch.hpp"
#include "runepkg_building.hpp"

extern "C" {
#include "runepkg_config.h"
#include "runepkg_hash.h"
#include "runepkg_completion.h"
#include "runepkg_host.h"
#include "runepkg_handle.h"
}

/* Interleaved Fast Autocomplete Engine Hook for runas (uses memory-mapped pkginfo.bin index) */
extern "C" int runepkg_runas_interleaved_autocomplete(const char *query, char suggestions[][256], int max_suggestions) {
    if (!query || !suggestions || max_suggestions <= 0) return 0;

    char raw_suggs[64][PATH_MAX];
    int max_fetch = (max_suggestions < 64) ? max_suggestions : 64;
    int count = runepkg_completion_get_repo_suggestions(query, raw_suggs, max_fetch);

    for (int i = 0; i < count; i++) {
        size_t len = strlen(raw_suggs[i]);
        if (len > 255) len = 255;
        memcpy(suggestions[i], raw_suggs[i], len);
        suggestions[i][len] = '\0';
    }
    return count;
}

/* Ensure missing host-level binary build tools are installed before compilation */
static int ensure_host_build_dependencies(const char *pkg_name) {
    if (!pkg_name) return -1;
    RuneTargetPlan *plan = nullptr;
    if (runepkg_resolver_resolve_target(pkg_name, &plan) == 0 && plan) {
        if (plan->node_count > 0 && plan->nodes[0].host_tools_required) {
            std::cout << "\033[1;35m[runas]\033[0m Verifying host build tools for " << pkg_name << "..." << std::endl;
            for (int i = 0; i < plan->nodes[0].host_tools_count; i++) {
                const char *tool = plan->nodes[0].host_tools_required[i];
                if (tool) {
                    PkgInfo *info = runepkg_main_hash_table ? runepkg_hash_search(runepkg_main_hash_table, tool) : NULL;
                    if (!info) {
                        std::cout << "\033[1;33m[runas]\033[0m Auto-installing missing host tool: " << tool << "..." << std::endl;
                        runepkg_repo_install(tool);
                    } else {
                        std::cout << "  -> Host tool \033[1;32m" << tool << "\033[0m is satisfied." << std::endl;
                    }
                }
            }
        }
        runepkg_resolver_free_plan(plan);
    }
    return 0;
}

/* Handle Bash Autocomplete when invoked via `complete -C runas runas` */
static bool handle_bash_completion_if_requested(void) {
    const char *comp_line = getenv("COMP_LINE");
    if (!comp_line || comp_line[0] == '\0') return false;

    runepkg_init();

    std::string line = comp_line;
    std::vector<std::string> tokens;
    size_t pos = 0;
    while ((pos = line.find_first_not_of(" \t")) != std::string::npos) {
        line = line.substr(pos);
        size_t end = line.find_first_of(" \t");
        if (end == std::string::npos) {
            tokens.push_back(line);
            line.clear();
        } else {
            tokens.push_back(line.substr(0, end));
            line = line.substr(end);
        }
    }

    std::string last_token = "";
    if (comp_line[strlen(comp_line) - 1] == ' ') {
        last_token = "";
    } else if (!tokens.empty()) {
        last_token = tokens.back();
    }

    /* Subcommands completion */
    if (tokens.size() <= 1 || (tokens.size() == 2 && comp_line[strlen(comp_line) - 1] != ' ')) {
        const char *subcmds[] = { "sync", "update", "source", "prepare", "unpack", "source-depends", "source-build-depends", "depends", "resolve-tree", "build", "buildpkg-split", "source-build", "autocomplete", "--help" };
        for (const char *cmd : subcmds) {
            if (last_token.empty() || std::string(cmd).rfind(last_token, 0) == 0) {
                std::cout << cmd << std::endl;
            }
        }
        return true;
    }

    /* Package name completion for source commands */
    std::string action = (tokens.size() >= 2) ? tokens[1] : "";
    if (action == "source" || action == "prepare" || action == "unpack" || action == "source-depends" || action == "source-build-depends" || action == "depends" || action == "resolve-tree" || action == "build" || action == "buildpkg-split" || action == "source-build" || action == "autocomplete") {
        char suggestions[64][PATH_MAX];
        int count = runepkg_completion_get_repo_suggestions(last_token.c_str(), suggestions, 64);
        for (int i = 0; i < count; i++) {
            std::cout << suggestions[i] << std::endl;
        }
        return true;
    }

    return true;
}

static const char *const RUNAS_SAYINGS[] = {
    "Oh, sweet relief! The energy... it flows through me!",
    "Just a small taste... to keep the madness at bay.",
    "Feeding on such raw energy... it clouds my judgment.",
    "Yes, yes! Just a bit more... for clarity!",
    "Can you feel the energy radiating from this compiler?",
    "I am not a monster! I am merely... enthusiastic.",
    "I am not some mana-addled FREAK!",
    "It is a condition, you see. An affliction. Not a choice.",
    "Do not look at me like that. I am completely in control.",
    "A dragon's pool... Can you even imagine the flavor?",
    "It's getting dark... My hands, I cannot feel my hands...",
    "The hunger... it never truly goes away, does it?",
    "I... I cannot see you anymore. Where did you go?",
    "Forgive me... the cravings... they are too strong.",
    "Thank you, my friend, for letting my last few hours mean something.",
    "You have given me a great gift. I will not forget it.",
    "See? I told you I could be useful!",
    "Together, we have done a good thing today.",
    "Ah, a friend! Have you brought any source runes for poor old Runas?",
    "Can you hear them? The ley lines, they whisper to me... 'Build the source, Runas!'",
    "I need... mana! No, wait... I need more Debian source runes!",
    "Hahaha! So much power! The dependencies... they flow through me!",
    "Mmm, the sweet, sweet aroma of freshly compiled C code in the morning!"
};

static void show_runas_version(void) {
    std::cout << "\033[1;36m┌────────────────────────────────────────────────────────────┐\033[0m" << std::endl;
    std::cout << "\033[1;36m│\033[0m  \033[1;35mᚱᚢᚾᚨᛊ\033[0m  \033[1;32mrunas v1.0.4+\033[0m (Debian Source Mana Fiend)          \033[1;36m │\033[0m" << std::endl;
    std::cout << "\033[1;36m│\033[0m  Copyright (c) 2026 runepkg project. License: GPLv3        \033[1;36m│\033[0m" << std::endl;
    std::cout << "\033[1;36m└────────────────────────────────────────────────────────────┘\033[0m" << std::endl;

    std::cout << "  \033[1;30m[#####]\033[0m  \033[1;35mrunas\033[0m (Debian Source Mana Fiend)" << std::endl;
    std::cout << "  \033[1;30m[#\033[1;36m\\ /\033[1;30m#]\033[0m  version \033[1;32m1.0.4+\033[0m" << std::endl;
    std::cout << "  \033[1;30m[# \033[1;36mV \033[1;30m#]\033[0m  " << std::endl;
    std::cout << "  \033[1;30m[#####]\033[0m  GPL-v3" << std::endl;
    std::cout << std::endl;
    std::cout << "\033[3;37m*Built with ❤️ for the old school GNU/Linux community...*\033[0m" << std::endl;
    std::cout << "Copyright (c) 2026 runepkg (Runar Linux) All rights reserved." << std::endl;
    std::cout << "Contact: [michkochris@gmail.com] | [runepkg@gmail.com]" << std::endl;

    size_t num_sayings = sizeof(RUNAS_SAYINGS) / sizeof(RUNAS_SAYINGS[0]);
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    srand(static_cast<unsigned int>(seed ^ getpid()));
    int idx = rand() % num_sayings;
    const char *quote = RUNAS_SAYINGS[idx];

    std::cout << std::endl;
    std::cout << "\033[1;35m🧙 Runas says:\033[0m" << std::endl;
    std::cout << "  \"\033[1;33m" << quote << "\033[0m\"" << std::endl;
}

int main(int argc, char **argv) {
    if (handle_bash_completion_if_requested()) {
        return 0;
    }

    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "version") == 0)) {
        show_runas_version();
        return 0;
    }

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        std::cout << "\033[1;35mrunas\033[0m - High-Performance Debian Source Package & Toolchain Forge Engine" << std::endl;
        std::cout << "Usage:" << std::endl;
        std::cout << "  runas sync                               Synchronize host package database snapshot" << std::endl;
        std::cout << "  runas update                             Download repository indices & update binary graphs" << std::endl;
        std::cout << "  runas source <package>                   Download Debian source package files (.dsc, .orig, .debian)" << std::endl;
        std::cout << "  runas prepare <package>                  Unpack source rune, apply Debian patches (*), & fix timestamps" << std::endl;
        std::cout << "  runas source-depends <pkg>               Download complete source runes for package and runtime dependencies" << std::endl;
        std::cout << "  runas source-build-depends <pkg>         Download complete source runes for package and build dependencies" << std::endl;
        std::cout << "  runas depends <package>                  Render recursive ASCII dependency tree of target package" << std::endl;
        std::cout << "  runas resolve-tree <package>             Resolve target dependency graph for source building" << std::endl;
        std::cout << "  runas build <package|dsc> [subpkg|help]  Verify host tools and build primary package (or subpackage/help)" << std::endl;
        std::cout << "  runas buildpkg-split <package>           Build source package and split into all subpackages" << std::endl;
        std::cout << "  runas source-build <package>             3-Stage Pipeline: fetch runes -> verify host tools -> build .deb" << std::endl;
        std::cout << "  runas autocomplete <query>               Run $O(1)$ memory-mapped binary autocomplete lookup" << std::endl;
        std::cout << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --arch=<target_arch>                     Specify target Multi-Arch architecture (amd64, arm64, riscv64, etc.)" << std::endl;
        return 0;
    }

    runepkg_init();

    std::string target_arch = "";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--arch=", 0) == 0) {
            target_arch = arg.substr(7);
        }
    }

    DebianMultiArchConfig ma_config = DebianMultiArchEngine::configure_multiarch(target_arch);
    DebianMultiArchEngine::export_multiarch_env(ma_config);
    DebianMultiArchEngine::print_multiarch_info(ma_config);

    if (strcmp(argv[1], "sync") == 0) {
        return runepkg_host_sync();
    }

    if (strcmp(argv[1], "update") == 0) {
        return runepkg_update();
    }

    if (strcmp(argv[1], "autocomplete") == 0 && argc >= 3) {
        char suggestions[64][PATH_MAX];
        int count = runepkg_completion_get_repo_suggestions(argv[2], suggestions, 64);
        for (int i = 0; i < count; i++) {
            std::cout << suggestions[i] << std::endl;
        }
        return 0;
    }

    if (strcmp(argv[1], "source") == 0 && argc >= 3) {
        return runepkg_repo_source_download(argv[2]);
    }

    if ((strcmp(argv[1], "prepare") == 0 || strcmp(argv[1], "unpack") == 0) && argc >= 3) {
        return runepkg_building_unpack_and_patch(argv[2]);
    }

    if (strcmp(argv[1], "source-depends") == 0 && argc >= 3) {
        return runepkg_repo_source_depends_download(argv[2]);
    }

    if (strcmp(argv[1], "source-build-depends") == 0 && argc >= 3) {
        return runepkg_repo_source_build_depends_download(argv[2]);
    }

    if ((strcmp(argv[1], "depends") == 0 || strcmp(argv[1], "resolve-tree") == 0) && argc >= 3) {
        return runepkg_resolver_dump_tree(argv[2]);
    }

    if (strcmp(argv[1], "build") == 0 && argc >= 3) {
        if (argc >= 4 && (strcmp(argv[3], "help") == 0 || strcmp(argv[3], "--help") == 0 || strcmp(argv[3], "-h") == 0)) {
            return runepkg_building_list_subpackages(argv[2]);
        }
        ensure_host_build_dependencies(argv[2]);
        const char *subpkg = (argc >= 4) ? argv[3] : nullptr;
        return runepkg_building_debian_build(argv[2], false, subpkg);
    }

    if (strcmp(argv[1], "buildpkg-split") == 0 && argc >= 3) {
        ensure_host_build_dependencies(argv[2]);
        const char *subpkg = (argc >= 4) ? argv[3] : nullptr;
        return runepkg_building_debian_build(argv[2], true, subpkg);
    }

    if (strcmp(argv[1], "source-build") == 0 && argc >= 3) {
        ensure_host_build_dependencies(argv[2]);
        return runepkg_building_source_build_pipeline(argv[2], target_arch.c_str());
    }

    std::cerr << "\033[1;31m[runas]\033[0m Unknown command: " << argv[1] << std::endl;
    return -1;
}
