/******************************************************************************
 *  Filename:    upkghash.c
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
/*file description: file for adding pkginfo into hash table for storage...*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

#include "upkglib.h"
#include "upkghash.h"
#include "upkgstruct.h"

Node* hashTable[TABLE_SIZE];

int hashFunction(char *name) {
    if (name == NULL) {
        // Defensive: Null input
        return 0;
    }
    int hash = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        hash += name[i];
    }
    // Defensive: TABLE_SIZE must be > 0
    if (TABLE_SIZE <= 0) return 0;
    return hash % TABLE_SIZE;
}

char *search_hash(char *name) {
    if (name == NULL) {
        // Invalid input
        return NULL;
    }
    int index = hashFunction(name);
    if (index < 0 || index >= TABLE_SIZE) {
        // Out-of-bounds index
        return NULL;
    }
    Node *entry = hashTable[index];
    while (entry != NULL) {
        if (entry->data.pkgname[0] != '\0' &&
            strcmp(entry->data.pkgname, name) == 0) {
            return entry->data.pkgname;
        }
        entry = entry->next;
    }
    // Not found
    return NULL;
}

Pkginfo *search(char *name) {
    if (name == NULL) {
        // Invalid input
        return NULL;
    }
    int index = hashFunction(name);
    if (index < 0 || index >= TABLE_SIZE) {
        // Out-of-bounds index
        return NULL;
    }
    Node* current = hashTable[index];
    while (current != NULL) {
        if (current->data.pkgname[0] != '\0' &&
            strcmp(current->data.pkgname, name) == 0) {
            return &(current->data);
        }
        current = current->next;
    }
    // Not found
    return NULL;
}

void addpkg(char *name) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    int index = hashFunction(name);
    if (index < 0 || index >= TABLE_SIZE) {
        return;
    }
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        fprintf(stderr, "addpkg: Memory allocation failed\n");
        return;
    }
    memset(&(newNode->data), 0, sizeof(Pkginfo));
    strncpy(newNode->data.pkgname, name, sizeof(newNode->data.pkgname) - 1);
    newNode->next = hashTable[index];
    hashTable[index] = newNode;
}

void removepkg(char *name) {
    if (name == NULL) {
        return;
    }
    int index = hashFunction(name);
    if (index < 0 || index >= TABLE_SIZE) {
        return;
    }
    Node* current = hashTable[index];
    Node* prev = NULL;
    while (current != NULL && strcmp(current->data.pkgname, name) != 0) {
        prev = current;
        current = current->next;
    }
    if (current != NULL) {
        if (prev == NULL) {
            hashTable[index] = current->next;
        } else {
            prev->next = current->next;
        }
        free(current);
    }
}

void glob() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *current = hashTable[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("%s ", current->data.pkgname);
            }
            current = current->next;
        }
    }
}

void list() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *current = hashTable[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("%s\n", current->data.pkgname);
            }
            current = current->next;
        }
    }
}

// Print all items in the hash table, showing collisions
void print_hash_table() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        printf("Index %d: ", i);
        Node *current = hashTable[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("(%s) -> ", current->data.pkgname);
            }
            current = current->next;
        }
        printf("(null)\n");
    }
}

#define MAX_SUGGESTIONS 10
char **suggestions(char *name) {
    if (name == NULL) return NULL;
    char **suggestions = (char **)malloc(MAX_SUGGESTIONS * sizeof(char *));
    if (!suggestions) return NULL;
    int count = 0;
    size_t name_len = strlen(name);
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *current = hashTable[i];
        while (current != NULL && count < MAX_SUGGESTIONS) {
            if (current->data.pkgname[0] != '\0' &&
                strncmp(name, current->data.pkgname, name_len) == 0) {
                suggestions[count++] = strdup(current->data.pkgname);
            }
            current = current->next;
        }
        if (count >= MAX_SUGGESTIONS) break;
    }
    suggestions[count] = NULL; // Null-terminate the array
    return suggestions;
}

void print_suggestions(char *prefix) {
    if (prefix == NULL || prefix[0] == '\0') return;
    char first_letter = tolower(prefix[0]);
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *current = hashTable[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0' &&
                tolower(current->data.pkgname[0]) == first_letter) {
                printf("%s ", current->data.pkgname);
            }
            current = current->next;
        }
    }
}

void initialadd() {
    struct Pkginfo info = gatherinfo();
    if (info.pkgname[0] == '\0') return;
    int index = hashFunction(info.pkgname);
    if (index < 0 || index >= TABLE_SIZE) return;
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        fprintf(stderr, "initialadd: Memory allocation failed\n");
        return;
    }
    memset(&(newNode->data), 0, sizeof(Pkginfo));
    strncpy(newNode->data.pkgname, info.pkgname, sizeof(newNode->data.pkgname) - 1);
    strncpy(newNode->data.version, info.version, sizeof(newNode->data.version) - 1);
    strncpy(newNode->data.arch, info.arch, sizeof(newNode->data.arch) - 1);
    strncpy(newNode->data.maintainer, info.maintainer, sizeof(newNode->data.maintainer) - 1);
    strncpy(newNode->data.homepage, info.homepage, sizeof(newNode->data.homepage) - 1);
    strncpy(newNode->data.sources, info.sources, sizeof(newNode->data.sources) - 1);
    strncpy(newNode->data.section, info.section, sizeof(newNode->data.section) - 1);
    strncpy(newNode->data.priority, info.priority, sizeof(newNode->data.priority) - 1);
    strncpy(newNode->data.depends, info.depends, sizeof(newNode->data.depends) - 1);
    strncpy(newNode->data.comment, info.comment, sizeof(newNode->data.comment) - 1);
    strncpy(newNode->data.description, info.description, sizeof(newNode->data.description) - 1);
    newNode->next = hashTable[index];
    hashTable[index] = newNode;
    resetstruct(&info);
}

void status_search(char *name) {
    if (name == NULL) {
        printf("Invalid package name.\n");
        return;
    }
    char *srch = search_hash(name);
    if (srch != NULL) {
        Pkginfo *found = search(name);
        if (found != NULL && strlen(found->pkgname) > 0) {
            printf("Package: %s\n", found->pkgname);
            if (strlen(found->version) > 0) {
                printf("Version: %s\n", found->version);
            }
            if (strlen(found->arch) > 0) {
                printf("Architecture: %s\n", found->arch);
            }
            if (strlen(found->maintainer) > 0) {
                printf("Maintainer: %s\n", found->maintainer);
            }
            if (strlen(found->homepage) > 0) {
                printf("Homepage: %s", found->homepage);
            }
            if (strlen(found->sources) > 0) {
                printf("Source: %s", found->sources);
            }
            if (strlen(found->section) > 0) {
                printf("Section: %s", found->section);
            }
            if (strlen(found->priority) > 0) {
                printf("Priority: %s", found->priority);
            }
            if (strlen(found->depends) > 0) {
                printf("Depends: %s", found->depends);
            }
            if (strlen(found->comment) > 0) {
                printf("Comment: %s\n", found->comment);
            }
            if (strlen(found->description) > 0) {
                printf("Description: %s\n", found->description);
            }
        } else {
            printf("Error: Package info not found for %s\n", name);
        }
    } else {
        printf("%s Not installed! did you mean: \n", name);
        print_suggestions(name);
    }
}

void testhash() {
    addpkg("binutils");
    addpkg("findutils");
    addpkg("coreutils");
    addpkg("util-linux");
    addpkg("gawk");
    addpkg("bash");
    addpkg("neofetch");
    addpkg("nano");
    addpkg("file");
    //glob();
    //list();
    //print_hash_table();
    Pkginfo *srch = search("bash");
    if (srch != NULL) {
        //printf("pkginfo_search: %s\n", srch->pkgname);
    } else {
        //printf("pkginfo_search: bash not found\n");
    }
    //print_suggestions("f");
    char *hello = search_hash("busybox-static");
    if (hello) {
        //printf("hello: %s\n", hello);
    }
    //printpkginfo("busybox-static");
}
