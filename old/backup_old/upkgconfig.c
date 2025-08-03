/******************************************************************************
 *  Filename:    upkgconfig.c
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
/*file description: function for reading values from a  config file*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "upkglib.h"
#include "upkghash.h"
#include "upkgstruct.h"
#include "upkgconfig.h"

char *get_config_value(const char *filename, const char *key) {
    if (!filename || !key) {
        fprintf(stderr, "get_config_value: NULL argument\n");
        return NULL;
    }
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        return NULL;
    }
    char line[64];
    char* value = NULL;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        char* key_ptr = strtok(line, "=");
        char* value_ptr = strtok(NULL, "=");
        if (key_ptr != NULL && value_ptr != NULL) {
            key_ptr = strtok(key_ptr, " \t\n\r");
            value_ptr = strtok(value_ptr, " \t\n\r");
            if (key_ptr && value_ptr && strcmp(key_ptr, key) == 0) {
                value = strdup(value_ptr);
                break;
            }
        }
    }
    fclose(file);
    if (value) {
        // Sanitize value: remove dangerous characters
        for (char *p = value; *p; ++p) {
            if (*p == ';' || *p == '&' || *p == '|') {
                *p = '_';
            }
        }
    }
    return value;
}

void check_upkgconfig() {
    char *configfile = "upkgconfig";
    if (access(configfile, F_OK) != 0) {
        printf("upkgconfig File does not exist!\n");
        exit(1);
    }
    char *upkg_dir = get_config_value(configfile, "upkg_dir");
    char *control_dir = get_config_value(configfile, "control_dir");
    char *unpack_dir = get_config_value(configfile, "unpack_dir");
    char *install_dir = get_config_value(configfile, "install_dir");

    if (!upkg_dir) {
        printf("upkg_dir not found in config file!\n");
        exit(1);
    }
    create_dir(upkg_dir);
    free(upkg_dir);

    if (!control_dir) {
        printf("control_dir not found in config file!\n");
        exit(1);
    }
    remove_dir(control_dir);
    create_dir(control_dir);
    free(control_dir);

    if (!unpack_dir) {
        printf("unpack_dir not found in config file!\n");
        exit(1);
    }
    remove_dir(unpack_dir);
    create_dir(unpack_dir);
    free(unpack_dir);

    if (!install_dir) {
        printf("install_dir not found in config file!\n");
        exit(1);
    }
    create_dir(install_dir);
    free(install_dir);
}

void print_config() {
    char *config = "upkgconfig";
    if (access(config, F_OK) != 0) {
        printf("Config file %s does not exist!\n", config);
        return;
    }
    char *upkg = get_config_value(config, "upkg_dir");
    char *control = get_config_value(config, "control_dir");
    char *unpack = get_config_value(config, "unpack_dir");
    char *install = get_config_value(config, "install_dir");
    printf("\nupkg config settings:\n");
    printf("config=%s\n", config);
    if (upkg) {
        printf("upkg_dir=%s\n", upkg);
        free(upkg);
    }
    if (control) {
        printf("control_dir=%s\n", control);
        free(control);
    }
    if (unpack) {
        printf("unpack_dir=%s\n", unpack);
        free(unpack);
    }
    if (install) {
        printf("install_dir=%s\n", install);
        free(install);
    }
}
//end of file
