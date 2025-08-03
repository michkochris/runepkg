// upkg_struct.h
#ifndef UPKG_STRUCT_H
#define UPKG_STRUCT_H

#include <stddef.h> // For size_t
#include <time.h>   // For time_t
#include <stdbool.h>

// --- Data Structure Constants ---
#define PKGNAME_SIZE 64
#define VERSION_SIZE 32
#define ARCH_SIZE 16
#define MAINTAINER_SIZE 64
#define HOMEPAGE_SIZE 128
#define SOURCES_SIZE 128
#define SECTION_SIZE 32
#define PRIORITY_SIZE 16
#define DEPENDS_SIZE 256
#define COMMENT_SIZE 256
#define DESCRIPTION_SIZE 1024

// --- Core Data Structures ---

// Pkginfo struct to hold all package metadata and file information
typedef struct {
    // Standard Debian fields (fixed-size buffers)
    char pkgname[PKGNAME_SIZE];
    char version[VERSION_SIZE];
    char arch[ARCH_SIZE];
    char maintainer[MAINTAINER_SIZE];
    char homepage[HOMEPAGE_SIZE];
    char sources[SOURCES_SIZE];
    char section[SECTION_SIZE];
    char priority[PRIORITY_SIZE];
    char depends[DEPENDS_SIZE];
    char comment[COMMENT_SIZE];
    char description[DESCRIPTION_SIZE];

    // Script contents (dynamically allocated)
    char *preinst;
    size_t preinst_len;
    char *postinst;
    size_t postinst_len;
    char *prerm;
    size_t prerm_len;
    char *postrm;
    size_t postrm_len;
    char *buildscript;
    size_t buildscript_len;

    // File list (dynamically allocated array of strings)
    char **file_list;
    int file_count;

    // Package status and metadata
    bool installed;
    time_t install_date;
} Pkginfo;

// --- Function Prototypes for Pkginfo Operations ---

// Frees dynamically allocated memory within a Pkginfo struct
void free_pkginfo_members(Pkginfo *info);

// Populates a Pkginfo struct by parsing the control file and extracting file lists
void create_fully_populated_pkginfo(const char *control_dir_path, const char *unpack_dir_path, Pkginfo *output_info);

// --- File I/O for Pkginfo struct ---
int save_pkginfo(const Pkginfo *info);
Pkginfo *load_pkginfo(const char *pkgname);

#endif // UPKG_STRUCT_H
