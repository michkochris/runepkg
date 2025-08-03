/******************************************************************************
 *  Filename:    upkg.c
 *  Author:      michkochris@gmail.com
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
/*file description: main c file for managing intc argv*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "upkglib.h"
#include "upkghash.h"
#include "upkgstruct.h"
#include "upkgconfig.h"

extern Node *hashTable[];

void process_upkg(char *deb_file) {
    if (!deb_file) {
        fprintf(stderr, "process_upkg: NULL deb_file\n");
        return;
    }
    char safe_deb_file[256];
    strncpy(safe_deb_file, deb_file, sizeof(safe_deb_file) - 1);
    safe_deb_file[sizeof(safe_deb_file) - 1] = '\0';
    sanitize_path(safe_deb_file);

    char *configfile = "upkgconfig";
    char *upkg_dir = get_config_value(configfile, "upkg_dir");
    char *control_dir = get_config_value(configfile, "control_dir");
    char *unpack_dir = get_config_value(configfile, "unpack_dir");
    char *install_dir = get_config_value(configfile, "install_dir");
    if (!upkg_dir || !control_dir || !unpack_dir || !install_dir) {
        fprintf(stderr, "Error: missing config value\n");
        free(upkg_dir); free(control_dir); free(unpack_dir); free(install_dir);
        return;
    }
    char *controltar = concat_path(control_dir, "/control.tar.xz");
    char *datatar = concat_path(control_dir, "/data.tar.xz");
    if (!controltar || !datatar) {
        fprintf(stderr, "Error: could not create controltar or datatar path\n");
        free(upkg_dir); free(control_dir); free(unpack_dir); free(install_dir);
        if (controltar) free(controltar);
        if (datatar) free(datatar);
        return;
    }
    extract_deb(safe_deb_file, control_dir);
    extract_tar_xz(controltar, control_dir);
    extract_data_tar_xz(datatar, unpack_dir);

    // Gather package info and file list
    struct Pkginfo info = gatherinfo();
    add_files_to_pkginfo(&info, unpack_dir);

    // Add package info to hash table (all fields and file list)
    int index = hashFunction(info.pkgname);
    if (index < 0 || index >= TABLE_SIZE) {
        free(upkg_dir); free(control_dir); free(unpack_dir); free(install_dir);
        free(controltar); free(datatar);
        return;
    }
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        fprintf(stderr, "process_upkg: Memory allocation failed\n");
        free(upkg_dir); free(control_dir); free(unpack_dir); free(install_dir);
        free(controltar); free(datatar);
        return;
    }
    memset(&(newNode->data), 0, sizeof(Pkginfo));
    newNode->data = info; // Copy all fields including file list
    newNode->next = hashTable[index];
    hashTable[index] = newNode;

    free(upkg_dir); free(control_dir); free(unpack_dir); free(install_dir);
    free(controltar); free(datatar);
}

int main(int argc, char *argv[]) {
    // Check for root privileges
    if (geteuid() != 0) {
        printf(RED "Warning: Some operations may require root privileges.\n" RESET);
        printf("Please run with sudo if you intend to install or modify system packages.\n\n");
    }
    check_upkgconfig();
    if (argc < 2) {
        usage_info();
        help_msg();
        exit(1);
    }
    for (int i = 1; i < argc; i++) {
        int handled = 0;
        char *filename = argv[i];
        if (!filename) continue;
        char *extension = strrchr(filename, '.');

        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            version_info();
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            print_config();
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--usage") == 0) {
            usage_info();
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            help_msg();
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            printf("listing all pkg's in list veiw: \n");list();
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--glob") == 0) {
            printf("listing all pkg's in glob veiw: \n");glob();
        } else if (strcmp(argv[i], "--license") == 0) {
            license_info(); version_info();
        } else if (extension != NULL && strcmp(extension, ".deb") == 0) {
            printf("processing %s\n\n", argv[i]);
            process_upkg(argv[i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                printf("Error: Option '%s' requires an argument.\n", argv[i]);
                exit(1);
            }
            status_search(argv[i + 1]);
            i++; // Skip the package name argument
	} else if (strcmp(argv[i], "-lpf") == 0 || strcmp(argv[i], "--list_pkg_files") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                printf("Error: Option '%s' requires an argument.\n", argv[i]);
                exit(1);
            }
            print_pkg_file_list(argv[i + 1]);
            i++; // Skip the package name argument
	 } else if (strcmp(argv[i], "-gpf") == 0 || strcmp(argv[i], "--glob_pkg_files") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                printf("Error: Option '%s' requires an argument.\n", argv[i]);
                exit(1);
            }
            print_pkg_file_list_glob_veiw(argv[i + 1]);
            i++; // Skip the package name argument
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--testhash") == 0) {
            testhash();
        } else if (strcmp(argv[i], "-pht") == 0 || strcmp(argv[i], "--print_hash_table") == 0) {
            print_hash_table();
        } else {
            version_info();
            printf("Command executed: ");
            for (int j = 0; j < argc; j++) {
                printf("%s ", argv[j]);
            }
            printf("\n");
        }
    }
    // After processing all arguments, print version and command executed
    version_info();
    printf("Command executed: ");
    for (int j = 0; j < argc; j++) {
        printf("%s ", argv[j]);
    }
    printf("\n");
    return 0;
}
