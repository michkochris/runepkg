// upkg_struct.c
/******************************************************************************
 * Filename:    upkg_struct.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 12-31-2024
 * Description: upkg manages linux .deb pkg's
 *
 * Copyright (c) 2025 upkg (ulinux) All rights reserved.
 * GPLV3
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/
/*file description: file for package info struct and related functions like gathering*/

#include "upkg_struct.h"
#include "upkg_lib.h"    // For safe_strncpy, read_file_content, get_config_value, strdup, concat_path, etc.
#include "upkg_script.h" // For secure_touch_shebang_rwx, etc. (still needed for script touching)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> // For opendir, readdir
#include <dirent.h>    // For opendir, readdir
#include <errno.h>     // For errno
#include <sys/stat.h>  // For lstat, S_ISDIR, S_ISREG
#include <limits.h>    // For PATH_MAX
#include <stdbool.h>   // For bool type (good practice)
#include <libgen.h>    // For dirname()
#include <unistd.h>    // For unlink(), check_file_exists()

// --- Constants for file paths within functions ---
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 1024
#endif

// --- Pkginfo Management Functions ---

/**
 * @brief Initializes a Pkginfo struct by setting all its members to zero.
 *
 * This function performs a byte-wise zeroing of the struct. It does NOT
 * free any dynamically allocated memory that might be pointed to by members
 * like `preinst`, `file_list`, etc. If the struct contains such pointers
 * that need to be freed before resetting, `free_pkginfo_members()` should
 * be called first.
 *
 * @param p A pointer to the Pkginfo struct to be reset.
 */
void resetstruct(Pkginfo *p) {
    if (!p) return;
    memset(p, 0, sizeof(Pkginfo));
}

/**
 * @brief Gathers essential package information from a Debian control file.
 *
 * Reads key-value pairs from the 'control' file located in the specified
 * `control_dir_path` and populates the corresponding fields in a `Pkginfo` struct.
 * This function uses `get_config_value` with a colon (':') as the separator,
 * as is standard for Debian control files.
 *
 * @param control_dir_path The absolute path to the directory containing the 'control' file.
 * @return A Pkginfo struct populated with the gathered information. Fixed-size
 * array members are populated directly; dynamically allocated members
 * (like script content or file lists) are handled by other functions.
 */
Pkginfo gatherinfo(const char *control_dir_path) {
    Pkginfo info;
    resetstruct(&info); // Initialize all fields to zero for safety

    if (!control_dir_path) {
        errormsg("gatherinfo: control_dir_path is NULL.");
        return info;
    }

    // Construct the full path to the 'control' file inside the provided directory.
    char *control_file_path = concat_path(control_dir_path, "control");
    if (!control_file_path) {
        errormsg("gatherinfo: Failed to construct control file path.");
        return info;
    }

    // Use get_config_value to read info from the specific 'control' file.
    // The separator for Debian control files is a colon ':'.
    char *value;
    infomsg("Reading package info from control file: '%s'", control_file_path);

    // Retrieve Package name
    value = get_config_value(control_file_path, "Package", ':');
    if (value) {
        strncpy(info.pkgname, value, PKGNAME_SIZE - 1);
        info.pkgname[PKGNAME_SIZE - 1] = '\0';
        dbgmsg("  - Package: %s", info.pkgname);
        free_and_null(&value);
    } else {
        warnmsg("Package field not found in control file. This is usually critical.");
    }

    // Retrieve Version
    value = get_config_value(control_file_path, "Version", ':');
    if (value) {
        strncpy(info.version, value, VERSION_SIZE - 1);
        info.version[VERSION_SIZE - 1] = '\0';
        dbgmsg("  - Version: %s", info.version);
        free_and_null(&value);
    } else {
        warnmsg("Version field not found in control file. This is usually critical.");
    }

    // Retrieve Architecture
    value = get_config_value(control_file_path, "Architecture", ':');
    if (value) {
        strncpy(info.arch, value, ARCH_SIZE - 1);
        info.arch[ARCH_SIZE - 1] = '\0';
        dbgmsg("  - Architecture: %s", info.arch);
        free_and_null(&value);
    } else {
        warnmsg("Architecture field not found in control file.");
    }

    // Retrieve Maintainer
    value = get_config_value(control_file_path, "Maintainer", ':');
    if (value) {
        strncpy(info.maintainer, value, MAINTAINER_SIZE - 1);
        info.maintainer[MAINTAINER_SIZE - 1] = '\0';
        dbgmsg("  - Maintainer: %s", info.maintainer);
        free_and_null(&value);
    }

    // Retrieve Homepage
    value = get_config_value(control_file_path, "Homepage", ':');
    if (value) {
        strncpy(info.homepage, value, HOMEPAGE_SIZE - 1);
        info.homepage[HOMEPAGE_SIZE - 1] = '\0';
        dbgmsg("  - Homepage: %s", info.homepage);
        free_and_null(&value);
    }

    // Retrieve Sources (if applicable, though 'Source' is more common for Debian)
    value = get_config_value(control_file_path, "Sources", ':');
    if (value) {
        strncpy(info.sources, value, SOURCES_SIZE - 1);
        info.sources[SOURCES_SIZE - 1] = '\0';
        dbgmsg("  - Sources: %s", info.sources);
        free_and_null(&value);
    }

    // Retrieve Section
    value = get_config_value(control_file_path, "Section", ':');
    if (value) {
        strncpy(info.section, value, SECTION_SIZE - 1);
        info.section[SECTION_SIZE - 1] = '\0';
        dbgmsg("  - Section: %s", info.section);
        free_and_null(&value);
    }

    // Retrieve Priority
    value = get_config_value(control_file_path, "Priority", ':');
    if (value) {
        strncpy(info.priority, value, PRIORITY_SIZE - 1);
        info.priority[PRIORITY_SIZE - 1] = '\0';
        dbgmsg("  - Priority: %s", info.priority);
        free_and_null(&value);
    }

    // Retrieve Depends
    value = get_config_value(control_file_path, "Depends", ':');
    if (value) {
        strncpy(info.depends, value, DEPENDS_SIZE - 1);
        info.depends[DEPENDS_SIZE - 1] = '\0';
        dbgmsg("  - Depends: %s", info.depends);
        free_and_null(&value);
    }

    // Retrieve Comment (if custom, typically 'Description' is used)
    value = get_config_value(control_file_path, "Comment", ':');
    if (value) {
        strncpy(info.comment, value, COMMENT_SIZE - 1);
        info.comment[COMMENT_SIZE - 1] = '\0';
        dbgmsg("  - Comment: %s", info.comment);
        free_and_null(&value);
    }

    // Retrieve Description (can be multi-line in Debian control files, get_config_value will get first line)
    value = get_config_value(control_file_path, "Description", ':');
    if (value) {
        strncpy(info.description, value, DESCRIPTION_SIZE - 1);
        info.description[DESCRIPTION_SIZE - 1] = '\0';
        dbgmsg("  - Description: %s", info.description);
        free_and_null(&value);
    }

    free_and_null(&control_file_path); // Free the path constructed earlier
    return info; // Return the struct by value
}

// --- NEW HELPER FUNCTION: Recursively list files in a directory ---
/**
 * @brief Recursively lists all regular files and symlinks within a directory and its subdirectories.
 *
 * Dynamically reallocates `file_list` to store paths relative to `base_dir`.
 *
 * @param file_list Pointer to the array of strings (char**) where file paths will be stored.
 * @param file_count Pointer to the integer count of files found.
 * @param base_dir The absolute root directory being scanned (e.g., /path/to/unpacked_data).
 * @param current_relative_path The current directory being traversed, relative to `base_dir` (initially "").
 * @return 0 on success, -1 on a critical error (e.g., memory allocation, unopenable directory)
 * Errors are logged but traversal attempts to continue if possible.
 */
static int list_directory_recursive(char ***file_list, int *file_count, const char *base_dir, const char *current_relative_path) {
    char full_current_path[PATH_MAX];
    DIR *dp;
    struct dirent *entry;
    struct stat st;
    int ret = 0; // 0 for success, -1 for error

    // Construct the absolute path of the current directory
    int snprintf_res = snprintf(full_current_path, sizeof(full_current_path), "%s/%s", base_dir, current_relative_path);
    if (snprintf_res < 0 || snprintf_res >= (int)sizeof(full_current_path)) {
        errormsg("Path buffer too small for: %s/%s", base_dir, current_relative_path);
        return -1;
    }

    dp = opendir(full_current_path);
    if (dp == NULL) {
        // If directory cannot be opened, it might be due to permissions or non-existence, log as warning
        warnmsg("Could not open directory %s: %s", full_current_path, strerror(errno));
        return -1; // Consider this a non-fatal warning, but record error for this path
    }

    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip current and parent directory entries
        }

        char entry_full_path[PATH_MAX];
        snprintf_res = snprintf(entry_full_path, sizeof(entry_full_path), "%s/%s", full_current_path, entry->d_name);
        if (snprintf_res < 0 || snprintf_res >= (int)sizeof(entry_full_path)) {
            errormsg("Path buffer too small for entry: %s/%s", full_current_path, entry->d_name);
            ret = -1; // Mark as error but continue to try other entries
            continue;
        }

        char entry_relative_path[PATH_MAX];
        if (strlen(current_relative_path) == 0) {
            snprintf_res = snprintf(entry_relative_path, sizeof(entry_relative_path), "%s", entry->d_name);
        } else {
            snprintf_res = snprintf(entry_relative_path, sizeof(entry_relative_path), "%s/%s", current_relative_path, entry->d_name);
        }

        if (snprintf_res < 0 || snprintf_res >= (int)sizeof(entry_relative_path)) {
            errormsg("Path buffer too small for relative entry: %s/%s", current_relative_path, entry->d_name);
            ret = -1;
            continue;
        }

        if (lstat(entry_full_path, &st) == -1) { // Use lstat to correctly handle symlinks
            warnmsg("Could not stat %s: %s", entry_full_path, strerror(errno));
            ret = -1; // Mark error, but continue scanning
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // If it's a directory, recursively call this function for its contents
            if (list_directory_recursive(file_list, file_count, base_dir, entry_relative_path) != 0) {
                ret = -1; // Propagate error from sub-calls
            }
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) { // Include regular files and symbolic links
            // Reallocate memory for the array of char pointers to store the new file path
            char **realloc_tmp = realloc(*file_list, (*file_count + 1) * sizeof(char*));
            if (!realloc_tmp) {
                errormsg("Memory re-allocation failed for file_list.");
                ret = -1;
                break; // Exit loop on critical memory error
            }
            *file_list = realloc_tmp;

            // FIX: Store the truly relative path, not a concatenated full path.
            // upkg_cli.c will later combine this relative path with the correct source and destination roots.
            char *relative_path_copy = strdup(entry_relative_path);
            if (!relative_path_copy) {
                errormsg("Memory allocation failed for relative path copy.");
                ret = -1;
                break;
            }

            // Store the relative path in the array
            (*file_list)[*file_count] = relative_path_copy;
            dbgmsg("Collected relative file path: '%s'", (*file_list)[*file_count]);
            (*file_count)++; // Increment the count of files
        }
        // Other types of files (e.g., pipes, devices, sockets) are ignored for package file lists
    }

    closedir(dp); // Close the directory stream
    return ret;   // Return overall status
}

/**
 * @brief Scans a directory for package files and adds their relative paths to Pkginfo.
 *
 * Initializes the `file_list` member of the `Pkginfo` struct by recursively
 * scanning the specified `unpack_dir`. Stores paths relative to `unpack_dir`.
 *
 * @param pkg A pointer to the Pkginfo struct to update.
 * @param unpack_dir The directory containing the unpacked package data.
 */
void add_files_to_pkginfo(Pkginfo *pkg, const char *unpack_dir) {
    if (!pkg || !unpack_dir) {
        errormsg("add_files_to_pkginfo: Invalid arguments.");
        return;
    }

    // Initialize file_list and file_count for the current package
    pkg->file_list = NULL;
    pkg->file_count = 0;

    dbgmsg("Scanning '%s' for package files...", unpack_dir);
    // FIX: Removed the upkg_installdir parameter from the function call.
    int result = list_directory_recursive(&(pkg->file_list), &(pkg->file_count), unpack_dir, "");

    if (result == 0 && pkg->file_count > 0) {
        goodmsg("Found %d files for package.", pkg->file_count);
    } else if (result != 0) {
        errormsg("Failed to scan directory '%s' for package files. (Check permissions or existence)", unpack_dir);
        // Ensure file_list is cleaned up on error
        free_pkginfo_members(pkg); // This will free pkg->file_list if it was partially allocated
    } else { // result == 0, but file_count == 0 (directory was scanned, but no files found)
        warnmsg("No files found in '%s' for package.", unpack_dir);
    }
}

/**
 * @brief Reads the content of a specified script file and stores it in the Pkginfo struct.
 *
 * Locates the script within `script_dir`, reads its content into a dynamically
 * allocated buffer, and updates the corresponding pointer and length in `Pkginfo`.
 * It also calls `secure_touch_shebang_rwx` to ensure the script is executable.
 *
 * @param pkg A pointer to the Pkginfo struct where script content will be stored.
 * @param script_dir The directory where the script is expected to be found.
 * @param script_name The filename of the script (e.g., "preinst").
 * @param script_buffer_ptr A pointer to the char* member in Pkginfo (e.g., &pkg->preinst).
 * This will be allocated and must be freed by `free_pkginfo_members`.
 * @param script_len_ptr A pointer to the size_t member in Pkginfo (e.g., &pkg->preinst_len).
 * Will store the length of the read script.
 */
void add_script_content_to_pkginfo(Pkginfo *pkg, const char *script_dir,
                                   const char *script_name, char **script_buffer_ptr,
                                   size_t *script_len_ptr) {
    if (!pkg || !script_dir || !script_name || !script_buffer_ptr || !script_len_ptr) {
        errormsg("add_script_content_to_pkginfo: Invalid arguments (NULL pointer detected).");
        return;
    }

    // Call get_file_contents to read the script into the provided buffer pointer.
    size_t len = get_file_contents(script_dir, script_name, script_buffer_ptr);

    if (len > 0) {
        *script_len_ptr = len; // Update the length field in Pkginfo
        // Construct the full path to the script for setting permissions.
        char *script_path = concat_path(script_dir, script_name);
        if (script_path) {
            // Make the script executable after reading its content.
            // secure_touch_shebang_rwx is defined in upkg_lib.c
            secure_touch_shebang_rwx(script_path);
            free_and_null(&script_path); // Free the temporary script path string
        } else {
            warnmsg("Could not construct path for script '%s' to set permissions.", script_name);
        }
    } else {
        // Script not found or could not be read. Ensure pointers are NULL and length is 0.
        free_and_null(script_buffer_ptr); // Ensure old content is freed if no new content
        *script_len_ptr = 0;
        // warnmsg("Could not read script '%s' from '%s'.", script_name, script_dir); // More specific error
    }
}

// --- Function to free dynamically allocated members of Pkginfo ---
/**
 * @brief Frees all dynamically allocated memory associated with a Pkginfo struct.
 *
 * This includes script content buffers and the `file_list` array along with
 * all individual file path strings within it. This function should be called
 * when a `Pkginfo` struct is no longer needed, to prevent memory leaks.
 *
 * @param pkg A pointer to the Pkginfo struct whose members are to be freed.
 */
void free_pkginfo_members(Pkginfo *pkg) {
    if (pkg == NULL) {
        return;
    }

    // Free script content buffers using the helper function.
    free_and_null(&pkg->preinst);
    free_and_null(&pkg->postinst);
    free_and_null(&pkg->prerm);
    free_and_null(&pkg->postrm);
    free_and_null(&pkg->buildscript);

    // Free the array of file paths and each individual file path string.
    if (pkg->file_list) {
        for (int i = 0; i < pkg->file_count; ++i) {
            free_and_null(&pkg->file_list[i]); // Free individual file path strings
        }
        free_and_null((char**)&pkg->file_list); // Free the array of char* pointers itself
        pkg->file_count = 0; // Reset count
    }
    // Fixed-size arrays (pkgname, version, etc.) are part of the struct itself
    // and do not need to be freed by this function; they are stack or part of a larger allocation.
}

// --- Consolidated function to create a fully populated Pkginfo ---
/**
 * @brief Creates and populates a complete Pkginfo struct for a given package.
 *
 * This is a high-level function that orchestrates the gathering of package
 * metadata from the control file, scanning for all package files, and loading
 * the content of any associated scripts.
 *
 * @param control_dir_path The path to the directory containing the package's 'control' file.
 * @param unpack_dir_path The path to the directory where the package's data has been unpacked.
 * @return A fully populated Pkginfo struct. The caller is responsible for calling
 * `free_pkginfo_members` on the returned struct to release its dynamically
 * allocated members when no longer needed.
 */
Pkginfo create_fully_populated_pkginfo(const char *control_dir_path, const char *unpack_dir_path) {
    Pkginfo info;
    resetstruct(&info); // Initialize all fields to zero for safety

    // 1. Gather basic info from control file (e.g., Package name, Version, etc.)
    info = gatherinfo(control_dir_path);

    // Critical check: If the package name couldn't be gathered, something is fundamentally wrong.
    if (info.pkgname[0] == '\0') {
        errormsg("create_fully_populated_pkginfo: Failed to gather essential package name. Returning empty struct.");
        // No dynamic members to free at this point, as gatherinfo populates fixed-size arrays.
        return info;
    }

    // 2. Add file list by recursively scanning the unpacked data directory.
    // FIX: Removed the upkg_installdir parameter from the function call.
    add_files_to_pkginfo(&info, unpack_dir_path);

    // 3. Add script contents by reading specific script files.
    // The script content pointers (e.g., info.preinst) are allocated here and must be freed later.
    add_script_content_to_pkginfo(&info, control_dir_path, "preinst", &info.preinst, &info.preinst_len);
    add_script_content_to_pkginfo(&info, control_dir_path, "postinst", &info.postinst, &info.postinst_len);
    add_script_content_to_pkginfo(&info, control_dir_path, "prerm", &info.prerm, &info.prerm_len);
    add_script_content_to_pkginfo(&info, control_dir_path, "postrm", &info.postrm, &info.postrm_len);
    add_script_content_to_pkginfo(&info, control_dir_path, "buildscript", &info.buildscript, &info.buildscript_len);

    return info; // Return the fully populated struct by value
}

// --- NEW FUNCTION to write file list to disk ---
/**
 * @brief Writes the installed file list from a Pkginfo struct to a file on disk.
 *
 * This function iterates through the in-memory file_list and writes each path
 * to a new file. This is the final step to record all installed files.
 *
 * @param pkg A pointer to the Pkginfo struct.
 * @param file_list_path The absolute path where the file list should be written.
 * @return 0 on success, -1 on failure.
 */
int write_pkginfo_file_list_to_disk(const Pkginfo* pkg, const char* file_list_path) {
    if (!pkg || !file_list_path) {
        errormsg("write_pkginfo_file_list_to_disk: Invalid arguments.");
        return -1;
    }
    if (pkg->file_count == 0) {
        warnmsg("No files found in Pkginfo struct. Not creating an empty file list.");
        return 0; // Return success, as there's nothing to write.
    }

    // Get the parent directory for the output file
    char* parent_dir = strdup(file_list_path);
    if (!parent_dir) {
        errormsg("Memory allocation failed for parent directory path.");
        return -1;
    }
    // Using dirname() on a copy as it modifies the input string.
    char* dir_to_create = dirname(parent_dir);
    if (dir_to_create && create_dir_recursive(dir_to_create, 0755) != 0) {
        errormsg("Failed to create parent directory for file list at '%s'", dir_to_create);
        free_and_null(&parent_dir);
        return -1;
    }
    free_and_null(&parent_dir);

    FILE* fp = fopen(file_list_path, "w");
    if (!fp) {
        // This is likely the source of the "couldn't create" message.
        errormsg("Failed to create file list at '%s': %s", file_list_path, strerror(errno));
        return -1;
    }

    dbgmsg("Writing installed file list to '%s'...", file_list_path);
    for (int i = 0; i < pkg->file_count; ++i) {
        if (pkg->file_list[i]) {
            fprintf(fp, "%s\n", pkg->file_list[i]);
        }
    }

    fclose(fp);
    goodmsg("Successfully wrote file list for '%s' to '%s'.", pkg->pkgname, file_list_path);
    return 0;
}


// --- Functions moved from upkg_script.c ---
// NOTE: I've kept these functions as they are, but moved the comments
// to match the style of the rest of the file.

/**
 * @brief Helper function to get the size of a file in bytes.
 * @param filepath The path to the file.
 * @return The size of the file in bytes, or 0 on error (e.g., file not found or unreadable).
 */
size_t get_file_size(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        // perror("Error opening file for size check"); // Suppress this if file not existing is common/handled
        return 0; // Return 0 if file cannot be opened (size is effectively 0 or unknown)
    }
    fseek(fp, 0, SEEK_END);
    long size_long = ftell(fp); // ftell returns long
    fclose(fp);

    if (size_long == -1) {
        perror("Error getting file size with ftell");
        return 0; // Indicate error by returning 0 or specific error code if distinct from empty file
    }
    return (size_t)size_long; // Cast to size_t
}

/**
 * @brief Helper function to read file content into a dynamically allocated char* and return its length.
 * @param directory The directory containing the file.
 * @param filename The name of the file within the directory.
 * @param content A pointer to a char* which will be allocated and filled with file content.
 * It must be freed by the caller.
 * @return The number of bytes read, or 0 on error or if the file is empty/not found.
 */
size_t get_file_contents(const char *directory, const char *filename, char **content) {
    char filepath[MAX_PATH_LEN];
    // Ensure the buffer is large enough for the combined path and null terminator
    int snprintf_res = snprintf(filepath, MAX_PATH_LEN, "%s/%s", directory, filename);

    if (snprintf_res < 0 || snprintf_res >= (int)MAX_PATH_LEN) {
        errormsg("Error: Path buffer too small or snprintf failed for file '%s' in directory '%s'.", filename, directory);
        *content = NULL;
        return 0;
    }

    size_t file_size = get_file_size(filepath); // get_file_size now returns size_t
    if (file_size == 0) { // Check for 0, as get_file_size now returns 0 on error/empty
        // errormsg("File '%s' not found or is empty in '%s'.", filename, directory); // More specific error
        *content = NULL;
        return 0; // Return 0 bytes read
    }

    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        perror("Error opening file for content read");
        *content = NULL;
        return 0;
    }

    *content = (char *)malloc(file_size + 1); // +1 for null terminator
    if (*content == NULL) {
        perror("Memory allocation failed");
        fclose(fp);
        return 0;
    }

    size_t bytes_read = fread(*content, 1, file_size, fp);
    if (bytes_read != file_size) {
        warnmsg("Warning: Expected to read %zu bytes but read %zu bytes from '%s'.",
                file_size, bytes_read, filepath);
    }
    (*content)[bytes_read] = '\0'; // Null-terminate at actual bytes read

    fclose(fp);
    return bytes_read; // Return actual bytes read
}

// --- NEW FUNCTIONS FOR BINARY DATABASE PERSISTENCE ---

/**
 * @brief Helper to write a dynamically sized buffer to a file with its length.
 *
 * @param fp The file pointer.
 * @param data The buffer to write.
 * @param len The length of the buffer.
 * @return 0 on success, -1 on failure.
 */
static int write_dynamic_buffer(FILE *fp, const char *data, size_t len) {
    if (fwrite(&len, sizeof(size_t), 1, fp) != 1) return -1;
    if (len > 0) {
        if (fwrite(data, 1, len, fp) != len) return -1;
    }
    return 0;
}

/**
 * @brief Helper to read a dynamically sized buffer from a file.
 *
 * @param fp The file pointer.
 * @param data_ptr A pointer to the char* to be allocated and filled.
 * @param len_ptr A pointer to the size_t to store the length.
 * @return 0 on success, -1 on failure.
 */
static int read_dynamic_buffer(FILE *fp, char **data_ptr, size_t *len_ptr) {
    if (fread(len_ptr, sizeof(size_t), 1, fp) != 1) {
        *len_ptr = 0;
        *data_ptr = NULL;
        return -1;
    }
    if (*len_ptr > 0) {
        *data_ptr = malloc(*len_ptr + 1);
        if (!*data_ptr) {
            *len_ptr = 0;
            return -1;
        }
        if (fread(*data_ptr, 1, *len_ptr, fp) != *len_ptr) {
            free_and_null(data_ptr);
            *len_ptr = 0;
            return -1;
        }
        (*data_ptr)[*len_ptr] = '\0';
    } else {
        *data_ptr = NULL;
    }
    return 0;
}

/**
 * @brief Saves a Pkginfo struct to a binary file in the package database.
 *
 * This function serializes the Pkginfo struct, including its dynamic members,
 * into a single binary file. It creates a dedicated directory for the package
 * in g_db_dir and places the binary file inside it.
 *
 * @param info A pointer to the Pkginfo struct to save.
 * @return 0 on success, -1 on failure.
 */
int save_pkginfo(const Pkginfo *info) {
    if (!info || info->pkgname[0] == '\0') {
        errormsg("save_pkginfo: Invalid Pkginfo struct or package name is empty.");
        return -1;
    }

    // Construct the package directory path (e.g., g_db_dir/busybox-static)
    char *pkg_dir_path = concat_path(g_db_dir, info->pkgname);
    if (!pkg_dir_path) {
        errormsg("save_pkginfo: Failed to construct package database directory path.");
        return -1;
    }

    if (create_dir_recursive(pkg_dir_path, 0755) != 0) {
        errormsg("save_pkginfo: Failed to create package database directory '%s'.", pkg_dir_path);
        free_and_null(&pkg_dir_path);
        return -1;
    }

    // Construct the full path to the binary file (e.g., g_db_dir/busybox-static/pkginfo.dat)
    char *pkg_file_path = concat_path(pkg_dir_path, "pkginfo.dat");
    free_and_null(&pkg_dir_path); // Path no longer needed
    if (!pkg_file_path) {
        errormsg("save_pkginfo: Failed to construct binary file path.");
        return -1;
    }

    FILE *fp = fopen(pkg_file_path, "wb");
    if (!fp) {
        errormsg("save_pkginfo: Failed to open binary file '%s' for writing: %s", pkg_file_path, strerror(errno));
        free_and_null(&pkg_file_path);
        return -1;
    }

    // 1. Write the fixed-size part of the struct
    if (fwrite(info, sizeof(Pkginfo), 1, fp) != 1) {
        errormsg("save_pkginfo: Failed to write fixed-size struct data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    // 2. Write the file_list count and then each path string
    if (fwrite(&info->file_count, sizeof(int), 1, fp) != 1) {
        errormsg("save_pkginfo: Failed to write file count.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    for (int i = 0; i < info->file_count; ++i) {
        if (info->file_list[i]) {
            size_t len = strlen(info->file_list[i]) + 1; // +1 for null terminator
            if (fwrite(&len, sizeof(size_t), 1, fp) != 1 || fwrite(info->file_list[i], 1, len, fp) != len) {
                errormsg("save_pkginfo: Failed to write file list path %d.", i);
                fclose(fp);
                free_and_null(&pkg_file_path);
                return -1;
            }
        }
    }

    // 3. Write script contents and their lengths
    if (write_dynamic_buffer(fp, info->preinst, info->preinst_len) != 0) {
        errormsg("save_pkginfo: Failed to write preinst script data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    if (write_dynamic_buffer(fp, info->postinst, info->postinst_len) != 0) {
        errormsg("save_pkginfo: Failed to write postinst script data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    if (write_dynamic_buffer(fp, info->prerm, info->prerm_len) != 0) {
        errormsg("save_pkginfo: Failed to write prerm script data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    if (write_dynamic_buffer(fp, info->postrm, info->postrm_len) != 0) {
        errormsg("save_pkginfo: Failed to write postrm script data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    if (write_dynamic_buffer(fp, info->buildscript, info->buildscript_len) != 0) {
        errormsg("save_pkginfo: Failed to write buildscript script data.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return -1;
    }

    fclose(fp);
    goodmsg("Successfully saved Pkginfo for '%s' to '%s'.", info->pkgname, pkg_file_path);
    free_and_null(&pkg_file_path);
    return 0;
}

/**
 * @brief Loads a Pkginfo struct from a binary file in the package database.
 *
 * This function reads the serialized Pkginfo data, allocates memory for
 * the dynamic members, and reconstructs the struct in memory.
 *
 * @param pkgname The name of the package to load.
 * @return A pointer to a newly allocated Pkginfo struct, or NULL on failure.
 * The caller is responsible for freeing the struct and its members using
 * `free_pkginfo_members` and then `free`.
 */
Pkginfo* load_pkginfo(const char *pkgname) {
    if (!pkgname || pkgname[0] == '\0') {
        errormsg("load_pkginfo: Invalid package name.");
        return NULL;
    }

    // Construct the full path to the binary file
    char *pkg_dir_path = concat_path(g_db_dir, pkgname);
    if (!pkg_dir_path) return NULL;
    char *pkg_file_path = concat_path(pkg_dir_path, "pkginfo.dat");
    free_and_null(&pkg_dir_path);
    if (!pkg_file_path) return NULL;

    FILE *fp = fopen(pkg_file_path, "rb");
    if (!fp) {
        dbgmsg("load_pkginfo: Failed to open binary file '%s'. It may not exist. %s", pkg_file_path, strerror(errno));
        free_and_null(&pkg_file_path);
        return NULL;
    }

    Pkginfo *info = (Pkginfo*)malloc(sizeof(Pkginfo));
    if (!info) {
        errormsg("load_pkginfo: Memory allocation failed for Pkginfo struct.");
        fclose(fp);
        free_and_null(&pkg_file_path);
        return NULL;
    }
    resetstruct(info);

    // 1. Read the fixed-size part of the struct
    if (fread(info, sizeof(Pkginfo), 1, fp) != 1) {
        errormsg("load_pkginfo: Failed to read fixed-size struct data.");
        free(info);
        fclose(fp);
        free_and_null(&pkg_file_path);
        return NULL;
    }
    
    // 2. Read the file_list count and then each path string
    if (fread(&info->file_count, sizeof(int), 1, fp) != 1) {
        errormsg("load_pkginfo: Failed to read file count.");
        free_pkginfo_members(info);
        free(info);
        fclose(fp);
        free_and_null(&pkg_file_path);
        return NULL;
    }

    if (info->file_count > 0) {
        info->file_list = (char**)malloc(info->file_count * sizeof(char*));
        if (!info->file_list) {
            errormsg("load_pkginfo: Failed to allocate memory for file list array.");
            free_pkginfo_members(info);
            free(info);
            fclose(fp);
            free_and_null(&pkg_file_path);
            return NULL;
        }

        for (int i = 0; i < info->file_count; ++i) {
            size_t len;
            if (fread(&len, sizeof(size_t), 1, fp) != 1) {
                errormsg("load_pkginfo: Failed to read file list path length for entry %d.", i);
                free_pkginfo_members(info);
                free(info);
                fclose(fp);
                free_and_null(&pkg_file_path);
                return NULL;
            }
            info->file_list[i] = (char*)malloc(len);
            if (!info->file_list[i]) {
                errormsg("load_pkginfo: Failed to allocate memory for file list path for entry %d.", i);
                free_pkginfo_members(info);
                free(info);
                fclose(fp);
                free_and_null(&pkg_file_path);
                return NULL;
            }
            if (fread(info->file_list[i], 1, len, fp) != len) {
                errormsg("load_pkginfo: Failed to read file list path data for entry %d.", i);
                free_pkginfo_members(info);
                free(info);
                fclose(fp);
                free_and_null(&pkg_file_path);
                return NULL;
            }
        }
    } else {
        info->file_list = NULL;
    }

    // 3. Read script contents
    if (read_dynamic_buffer(fp, &info->preinst, &info->preinst_len) != 0 ||
        read_dynamic_buffer(fp, &info->postinst, &info->postinst_len) != 0 ||
        read_dynamic_buffer(fp, &info->prerm, &info->prerm_len) != 0 ||
        read_dynamic_buffer(fp, &info->postrm, &info->postrm_len) != 0 ||
        read_dynamic_buffer(fp, &info->buildscript, &info->buildscript_len) != 0) {
        errormsg("load_pkginfo: Failed to read script data.");
        free_pkginfo_members(info);
        free(info);
        fclose(fp);
        free_and_null(&pkg_file_path);
        return NULL;
    }

    fclose(fp);
    dbgmsg("Successfully loaded Pkginfo for '%s' from '%s'.", pkgname, pkg_file_path);
    free_and_null(&pkg_file_path);
    return info;
}
