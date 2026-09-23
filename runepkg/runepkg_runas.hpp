/******************************************************************************/
/* Filename:    runepkg_runas.hpp                                              */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: Interleaved Autocomplete & Source Engine (runas) Header        */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_RUNAS_HPP
#define RUNEPKG_RUNAS_HPP

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

/* Interleaved Fast Autocomplete Engine Hooks */
int runepkg_runas_interleaved_autocomplete(const char *query, char suggestions[][256], int max_suggestions);

/* Source Package Engine (runas) Declarations */
int runepkg_repo_source_download(const char *pkg_name);
int runepkg_repo_source_download_multiple(const char **pkg_names, int count);
int runepkg_repo_source_depends_download(const char *pkg_name);
int runepkg_repo_source_depends_download_multiple(const char **pkg_names, int count);
int runepkg_repo_source_build_depends_download(const char *pkg_name);
int runepkg_repo_source_build_depends_download_multiple(const char **pkg_names, int count);
int runepkg_repo_build_depends_download(const char *pkg_name);
int runepkg_repo_build_depends_download_multiple(const char **pkg_names, int count);

char* runepkg_repo_find_source_for_binary(const char *bin_pkg_name);
char* runepkg_repo_get_source_build_depends(const char *src_pkg_name);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_RUNAS_HPP */
