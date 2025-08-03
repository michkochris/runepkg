/******************************************************************************
 *  Filename:    upkgstruct.c
 *  Author:      <michkochris@gmail.com>
 *  Date:        started 12-31-2024
 *  Description: upkg manages linux .deb pkg's
 *
 *  Copyright (c) 2025 upkg (ulinux) All rights reserved.
 *  GPLV3
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/
/*file description: file to gather pkg info into a struct from a .deb control file*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "upkglib.h"
#include "upkghash.h"
#include "upkgstruct.h"
#include "upkgconfig.h"

// Helper macro for secure string copy
#define SAFE_STRCPY(dest, src, size) do { \
    strncpy((dest), (src), (size) - 1); \
    (dest)[(size) - 1] = '\0'; \
} while(0)

// Helper macro for secure string copy with NULL check
#define SAFE_ASSIGN(dest, src, size) do { \
    if ((src) != NULL) { \
        SAFE_STRCPY((dest), (src), (size)); \
    } \
} while(0)

// Sanitize input to remove dangerous shell characters
void sanitize_string(char *str) {
    if (!str) return;
    for (char *p = str; *p; ++p) {
        if (*p == ';' || *p == '&' || *p == '|' || *p == '`' || *p == '$') {
            *p = '_';
        }
    }
}

struct Pkginfo gatherinfo() {
    struct Pkginfo controlinfo;
    memset(&controlinfo, 0, sizeof(struct Pkginfo)); // Defensive: zero struct
    char *configfile = "upkgconfig";
    char *control_dir = get_config_value(configfile, "control_dir");
    if (!control_dir) {
        fprintf(stderr, "Error: control_dir not found in config\n");
        exit(1);
    }
    sanitize_string(control_dir);
    char *controlfile = concat_path(control_dir, "/control");
    if (!controlfile) {
        fprintf(stderr, "Error: could not create control file path\n");
        free(control_dir);
        exit(1);
    }
    sanitize_string(controlfile);
    // Defensive: check file existence
    if (access(controlfile, F_OK) != 0) {
        fprintf(stderr, "Error: control file %s does not exist\n", controlfile);
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    char *pkgname_search = search_file(controlfile, "Package: ");
    if (pkgname_search != NULL) {
        rmstr(pkgname_search, "Package: ");
        remove_white(pkgname_search);
        sanitize_string(pkgname_search);
        SAFE_ASSIGN(controlinfo.pkgname, pkgname_search, PKGNAME_SIZE);
        if (strlen(pkgname_search) == 0) {
            printf("Package: field is found but is empty!\n");
        }
        free(pkgname_search);
    } else {
        printf("Package: is mandatory!\n");
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    char *version_search = search_file(controlfile, "Version: ");
    if (version_search != NULL) {
        rmstr(version_search, "Version: ");
        remove_white(version_search);
        sanitize_string(version_search);
        SAFE_ASSIGN(controlinfo.version, version_search, VERSION_SIZE);
        if (strlen(version_search) == 0) {
            printf("Version: field is found but is empty!\n");
        }
        free(version_search);
    } else {
        printf("Version: is mandatory!\n");
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    char *arch_search = search_file(controlfile, "Architecture: ");
    if (arch_search != NULL) {
        rmstr(arch_search, "Architecture: ");
        remove_white(arch_search);
        sanitize_string(arch_search);
        SAFE_ASSIGN(controlinfo.arch, arch_search, ARCH_SIZE);
        if (strlen(arch_search) == 0) {
            printf("Architecture: field is found but is empty!\n");
        }
        free(arch_search);
    } else {
        printf("Architecture: is mandatory!\n");
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    char *maintainer_search = search_file(controlfile, "Maintainer: ");
    if (maintainer_search != NULL) {
        rmstr(maintainer_search, "Maintainer: ");
        remove_white(maintainer_search);
        sanitize_string(maintainer_search);
        SAFE_ASSIGN(controlinfo.maintainer, maintainer_search, MAINTAINER_SIZE);
        if (strlen(maintainer_search) == 0) {
            printf("Maintainer: field is found but is empty!\n");
        }
        free(maintainer_search);
    } else {
        printf("Maintainer: is mandatory!\n");
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    char *homepage_search = search_file(controlfile, "Homepage: ");
    if (homepage_search != NULL) {
        rmstr(homepage_search, "Homepage: ");
        sanitize_string(homepage_search);
        SAFE_ASSIGN(controlinfo.homepage, homepage_search, HOMEPAGE_SIZE);
        if (strlen(homepage_search) == 0) {
            printf("Homepage: field is found but is empty!\n");
        }
        free(homepage_search);
    }
    char *sources_search = search_file(controlfile, "Source: ");
    if (sources_search != NULL) {
        rmstr(sources_search, "Source: ");
        sanitize_string(sources_search);
        SAFE_ASSIGN(controlinfo.sources, sources_search, SOURCES_SIZE);
        if (strlen(sources_search) == 0) {
            printf("Source: field is found but is empty!\n");
        }
        free(sources_search);
    }
    char *section_search = search_file(controlfile, "Section: ");
    if (section_search != NULL) {
        rmstr(section_search, "Section: ");
        sanitize_string(section_search);
        SAFE_ASSIGN(controlinfo.section, section_search, SECTION_SIZE);
        if (strlen(section_search) == 0) {
            printf("Section: field is found but is empty!\n");
        }
        free(section_search);
    }
    char *priority_search = search_file(controlfile, "Priority: ");
    if (priority_search != NULL) {
        rmstr(priority_search, "Priority: ");
        sanitize_string(priority_search);
        SAFE_ASSIGN(controlinfo.priority, priority_search, PRIORITY_SIZE);
        if (strlen(priority_search) == 0) {
            printf("Priority: field is found but is empty!\n");
        }
        free(priority_search);
    }
    char *depends_search = search_file(controlfile, "Depends: ");
    if (depends_search != NULL) {
        rmstr(depends_search, "Depends: ");
        sanitize_string(depends_search);
        SAFE_ASSIGN(controlinfo.depends, depends_search, DEPENDS_SIZE);
        if (strlen(depends_search) == 0) {
            printf("Depends: field is found but is empty!\n");
        }
        free(depends_search);
    }
    char *comment_search = search_file(controlfile, "Comment: ");
    if (comment_search != NULL) {
        rmstr(comment_search, "Comment: ");
        sanitize_string(comment_search);
        SAFE_ASSIGN(controlinfo.comment, comment_search, COMMENT_SIZE);
        free(comment_search);
    }
    char *description_search = searchandreadtoend(controlfile, "Description: ");
    if (description_search != NULL) {
        rmstr(description_search, "Description: ");
        sanitize_string(description_search);
        SAFE_ASSIGN(controlinfo.description, description_search, DESCRIPTION_SIZE);
        if (strlen(description_search) == 0) {
            printf("Description: field is found but is empty!\n");
        }
        free(description_search);
    } else {
        printf("Description: field is mandatory!\n");
        free(control_dir);
        free(controlfile);
        exit(1);
    }
    // Defensive: free memory
    free(control_dir);
    free(controlfile);
    return controlinfo;
}

void printpkginfo(char *name) {
    if (!name) {
        fprintf(stderr, "Error: name is NULL\n");
        return;
    }
    Pkginfo *controlinfo = search(name);
    if (!controlinfo) {
        printf("Package %s not installed! \n", name);
        return;
    }
    printf("\nprinting pkg struct:\n");
    // Defensive: print only if string is not empty
    if (strlen(controlinfo->pkgname) > 0) {
        printf("Package: %s\n", controlinfo->pkgname);
    }
    if (strlen(controlinfo->version) > 0) {
        printf("Version: %s\n", controlinfo->version);
    }
    if (strlen(controlinfo->arch) > 0) {
        printf("Architecture: %s\n", controlinfo->arch);
    }
    if (strlen(controlinfo->maintainer) > 0) {
        printf("Maintainer: %s\n", controlinfo->maintainer);
    }
    if (strlen(controlinfo->homepage) > 0) {
        printf("Homepage: %s\n", controlinfo->homepage);
    }
    if (strlen(controlinfo->sources) > 0) {
        printf("Source: %s\n", controlinfo->sources);
    }
    if (strlen(controlinfo->section) > 0) {
        printf("Section: %s\n", controlinfo->section);
    }
    if (strlen(controlinfo->priority) > 0) {
        printf("Priority: %s\n", controlinfo->priority);
    }
    if (strlen(controlinfo->depends) > 0) {
        printf("Depends: %s\n", controlinfo->depends);
    }
    if (strlen(controlinfo->comment) > 0) {
        printf("Comment: %s\n", controlinfo->comment);
    }
    if (strlen(controlinfo->description) > 0) {
        printf("Description: %s\n", controlinfo->description);
    }
}

void resetstruct(struct Pkginfo *p) {
    if (!p) return; // Defensive: null pointer check
    memset(p, 0, sizeof(struct Pkginfo));
}

void add_files_to_pkginfo(Pkginfo *info, const char *unpack_dir) {
    if (!info || !unpack_dir) return;
    info->file_list = collect_file_list(unpack_dir, &info->file_count);
}
// end of file

