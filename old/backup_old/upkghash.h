/******************************************************************************
 *  Filename:    upkghash.h
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
/*file description: file to export functions from upkghash.c*/
#ifndef UPKHASH_H
#define UPKHASH_H
#define TABLE_SIZE 20
#define PKGNAME_SIZE 32
#define VERSION_SIZE 32
#define ARCH_SIZE 32
#define MAINTAINER_SIZE 128
#define HOMEPAGE_SIZE 64
#define SOURCES_SIZE 128
#define SECTION_SIZE 32
#define PRIORITY_SIZE 32
#define DEPENDS_SIZE 128
#define COMMENT_SIZE 128
#define DESCRIPTION_SIZE 1028

typedef struct Pkginfo {
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
    char **file_list;      // Array of file paths
    int file_count;        // Number of files
} Pkginfo;
typedef struct Node {
    Pkginfo data;
    struct Node* next;
} Node;
int hashFunction(char *name);
char *search_hash(char *name);
Pkginfo* search(char *name);
void addpkg(char *name);
void removepkg(char *name);
void glob();void list();
void print_hash_table();
char **suggestions(char *name);
void print_suggestions(char *prefix);
void initialadd();
void status_search(char *name);

void testhash();
#endif
