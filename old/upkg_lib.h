#ifndef UPKG_LIB_H
#define UPKG_LIB_H

#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <limits.h>

// Preprocessor guards for a hypothetical pkginfo struct.
// You will need to define this struct in pkginfo.h
// The upkg_lib.h should declare it.
#include "pkginfo.h"


// --- Log Levels ---
// Defined verbosity levels for the logging functions.
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

// Global variable for logging verbosity
extern int g_log_level;


// --- Logging/Messaging Functions ---
// These functions provide colored, formatted output based on a global log level.
void dbgmsg(const char *format, ...);
void infomsg(const char *format, ...);
void warnmsg(const char *format, ...);
void errormsg(const char *format, ...);
void goodmsg(const char *format, ...);


// --- Memory Management ---
// Safely frees a dynamically allocated pointer and sets it to NULL.
void free_and_null(char **ptr);


// --- String Manipulation ---
// Safely copies a string, ensuring null-termination.
char *safe_strncpy(char *dest, const char *src, size_t n);

// Trims leading and trailing whitespace from a string.
char *trim_whitespace(char *str);

// Concatenates a directory path and a filename. Caller must free the result.
char *concat_path(const char *dir, const char *file);


// --- File System Operations ---
// Checks if a file or directory exists.
int file_exists(const char *filepath);

// Reads a file's content into a buffer. Caller must free the buffer.
char *read_file_content(const char *filepath, size_t *len);

// Recursively creates a directory path with specified permissions.
int create_dir_recursive(const char *path, mode_t mode);

// Deletes a single file.
int delete_file(const char *filepath);

// Recursively deletes a directory and its contents.
int delete_directory(const char *path);

// Deletes all contents of a directory, but not the directory itself.
int delete_directory_contents(const char *path);

// Deprecated function for testing.
int secure_touch_shebang_rwx(const char *filename);


// --- Configuration File Functions ---
// Reads a key-value pair from a config file, expanding '~' in the value.
// Caller must free the returned string.
char *get_config_value(const char *filepath, const char *key, char separator);


// --- Command Execution ---
// Executes an external command safely in a child process and waits for its completion.
int execute_command_safely(const char *command_path, char *const argv[]);


// --- .deb Package Specific Operations ---
// Extracts components from a .deb package using 'ar' into a destination directory.
int extract_deb(const char *deb_path, const char *destination_dir);

// Finds the specific control.tar.* and data.tar.* archive names in an extraction directory.
// Caller must free the returned paths.
int find_deb_archive_members(const char *deb_extract_dir, char **control_archive_path, char **control_archive_path);

// Extracts the contents of a .tar.* archive using 'tar'.
int extract_tar_archive(const char *archive_path, const char *destination_dir);


// --- File Installation Helpers ---
// Copies a file from source to destination, preserving permissions.
int copy_file(const char *source_path, const char *destination_path);

// Writes script content to a file and makes the file executable.
int write_script_to_file_and_make_executable(const char *filepath, const char *content);


#endif // UPKG_LIB_H
