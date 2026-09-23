/******************************************************************************/
/* Filename:    runepkg_building.hpp                                           */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: Standard Debian Source Builder & Toolchain Forge Engine        */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_BUILDING_HPP
#define RUNEPKG_BUILDING_HPP

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Debian Ecosystem Source Building Functions */
int runepkg_building_unpack_and_patch(const char *target_or_dsc);
int runepkg_building_list_subpackages(const char *target_or_dsc);
int runepkg_building_debian_build(const char *target_or_dsc, bool split, const char *subpackage_target);
int runepkg_building_source_build_pipeline(const char *pkg_name, const char *target_arch);

/* Raw Matrix JIT Cross-Forge (Embedded / Power User) */
int runepkg_building_matrix_cross_build(const char *dsc_path, const char *target_sysroot);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_BUILDING_HPP */
