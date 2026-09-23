/******************************************************************************/
/* Filename:    runepkg_multiarch.cpp                                          */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: Debian Multi-Arch Architecture & GNU Triplet Mapping Engine     */
/* License:     GPL v3                                                         */
/******************************************************************************/

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "runepkg_multiarch.hpp"

extern "C" {
#include "runepkg_host.h"
}

static const std::map<std::string, std::string> g_deb_to_gnu_map = {
    {"amd64",     "x86_64-linux-gnu"},
    {"arm64",     "aarch64-linux-gnu"},
    {"armhf",     "arm-linux-gnueabihf"},
    {"armel",     "arm-linux-gnueabi"},
    {"riscv64",   "riscv64-linux-gnu"},
    {"i386",      "i386-linux-gnu"},
    {"loong64",   "loongarch64-linux-gnu"},
    {"mips64el",  "mips64el-linux-gnuabi64"},
    {"ppc64el",   "powerpc64le-linux-gnu"},
    {"s390x",     "s390x-linux-gnu"}
};

std::string DebianMultiArchEngine::deb_arch_to_gnu_triplet(const std::string& deb_arch) {
    auto it = g_deb_to_gnu_map.find(deb_arch);
    if (it != g_deb_to_gnu_map.end()) {
        return it->second;
    }
    return deb_arch + "-linux-gnu";
}

std::string DebianMultiArchEngine::gnu_triplet_to_deb_arch(const std::string& triplet) {
    for (const auto& pair : g_deb_to_gnu_map) {
        if (pair.second == triplet) return pair.first;
    }
    return triplet;
}

std::string DebianMultiArchEngine::get_host_native_deb_arch() {
    const char *arch = runepkg_host_get_architecture();
    if (arch && strcmp(arch, "unknown") != 0) {
        return std::string(arch);
    }
    return "amd64";
}

DebianMultiArchConfig DebianMultiArchEngine::configure_multiarch(const std::string& target_arch) {
    DebianMultiArchConfig config;
    config.deb_build_arch = get_host_native_deb_arch();
    config.deb_build_triplet = deb_arch_to_gnu_triplet(config.deb_build_arch);

    if (target_arch.empty() || target_arch == config.deb_build_arch) {
        config.deb_host_arch = config.deb_build_arch;
        config.deb_host_triplet = config.deb_build_triplet;
        config.is_cross_compiling = false;
    } else {
        config.deb_host_arch = target_arch;
        config.deb_host_triplet = deb_arch_to_gnu_triplet(target_arch);
        config.is_cross_compiling = true;
    }

    config.lib_dir = "/usr/lib/" + config.deb_host_triplet;
    config.pkgconfig_dir = config.lib_dir + "/pkgconfig";

    return config;
}

int DebianMultiArchEngine::export_multiarch_env(const DebianMultiArchConfig& config) {
    setenv("DEB_BUILD_ARCH", config.deb_build_arch.c_str(), 1);
    setenv("DEB_BUILD_GNU_TYPE", config.deb_build_triplet.c_str(), 1);
    setenv("DEB_HOST_ARCH", config.deb_host_arch.c_str(), 1);
    setenv("DEB_HOST_GNU_TYPE", config.deb_host_triplet.c_str(), 1);

    if (config.is_cross_compiling) {
        std::string cc = config.deb_host_triplet + "-gcc";
        std::string cxx = config.deb_host_triplet + "-g++";
        std::string ar = config.deb_host_triplet + "-ar";
        std::string ranlib = config.deb_host_triplet + "-ranlib";
        std::string strip = config.deb_host_triplet + "-strip";

        setenv("CC", cc.c_str(), 1);
        setenv("CXX", cxx.c_str(), 1);
        setenv("AR", ar.c_str(), 1);
        setenv("RANLIB", ranlib.c_str(), 1);
        setenv("STRIP", strip.c_str(), 1);

        std::string pkg_path = config.pkgconfig_dir + ":/usr/share/pkgconfig";
        setenv("PKG_CONFIG_PATH", pkg_path.c_str(), 1);
        setenv("PKG_CONFIG_LIBDIR", config.pkgconfig_dir.c_str(), 1);
    }
    return 0;
}

void DebianMultiArchEngine::print_multiarch_info(const DebianMultiArchConfig& config) {
    std::cout << "\033[1;35m[multiarch]\033[0m Target Configuration:" << std::endl;
    std::cout << "  Build Arch:    " << config.deb_build_arch << " (" << config.deb_build_triplet << ")" << std::endl;
    std::cout << "  Host Arch:     " << config.deb_host_arch << " (" << config.deb_host_triplet << ")" << std::endl;
    std::cout << "  Mode:          " << (config.is_cross_compiling ? "\033[1;33mCross Multi-Arch\033[0m" : "\033[1;32mNative Build\033[0m") << std::endl;
    std::cout << "  Library Path:  " << config.lib_dir << std::endl;
}
