/******************************************************************************/
/* Filename:    runepkg_multiarch.hpp                                          */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: Debian Multi-Arch Architecture & GNU Triplet Mapping Engine     */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_MULTIARCH_HPP
#define RUNEPKG_MULTIARCH_HPP

#include <string>
#include <vector>

struct DebianMultiArchConfig {
    std::string deb_build_arch;     /* e.g. "amd64" */
    std::string deb_build_triplet;  /* e.g. "x86_64-linux-gnu" */
    std::string deb_host_arch;      /* e.g. "arm64" */
    std::string deb_host_triplet;   /* e.g. "aarch64-linux-gnu" */
    std::string lib_dir;            /* e.g. "/usr/lib/aarch64-linux-gnu" */
    std::string pkgconfig_dir;      /* e.g. "/usr/lib/aarch64-linux-gnu/pkgconfig" */
    bool is_cross_compiling;
};

class DebianMultiArchEngine {
public:
    static std::string deb_arch_to_gnu_triplet(const std::string& deb_arch);
    static std::string gnu_triplet_to_deb_arch(const std::string& triplet);
    static std::string get_host_native_deb_arch();
    static DebianMultiArchConfig configure_multiarch(const std::string& target_arch);
    static int export_multiarch_env(const DebianMultiArchConfig& config);
    static void print_multiarch_info(const DebianMultiArchConfig& config);
};

#endif /* RUNEPKG_MULTIARCH_HPP */
