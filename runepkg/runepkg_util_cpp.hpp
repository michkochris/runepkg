/******************************************************************************/
/* Filename:    runepkg_util_cpp.hpp                                           */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-08-29                                                     */
/* Description: Foundation C++ Utilities & Helpers for runepkg                 */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_UTIL_CPP_HPP
#define RUNEPKG_UTIL_CPP_HPP

#include <string>
#include <vector>
#include <map>
#include <string_view>
#include <filesystem>
#include <cstdint>
#include <sys/types.h>

namespace runepkg::util {

/* --- String Utilities --- */

/** Trims leading and trailing whitespace. */
std::string trim(const std::string& str);

/** Trims leading whitespace. */
std::string trim_left(const std::string& str);

/** Trims trailing whitespace. */
std::string trim_right(const std::string& str);

/** Splits string by delimiter character. */
std::vector<std::string> split(const std::string& str, char delimiter);

/** Joins string elements with specified delimiter. */
std::string join(const std::vector<std::string>& elements, const std::string& delimiter);

/** Returns true if string starts with prefix. */
bool starts_with(std::string_view str, std::string_view prefix);

/** Returns true if string ends with suffix. */
bool ends_with(std::string_view str, std::string_view suffix);

/** Converts string to lowercase. */
std::string to_lower(const std::string& str);

/** Converts string to uppercase. */
std::string to_upper(const std::string& str);

/** Formats byte counts into human-readable size string (B, KB, MB, GB). */
std::string format_bytes(uint64_t bytes);

/* --- Filesystem & I/O Helpers --- */

/** Reads entire file into a std::string. Returns empty string on failure. */
std::string read_file_to_string(const std::filesystem::path& filepath);

/** Writes content to temporary file first then renames atomically. */
bool write_string_to_file_atomic(const std::filesystem::path& filepath, const std::string& content);

/** Reads entire file line-by-line into a std::vector<std::string>. */
std::vector<std::string> read_file_lines(const std::filesystem::path& filepath);

/** Recursively creates directories with secure permissions (default 0755). */
bool create_directories_secure(const std::filesystem::path& dir_path, mode_t mode = 0755);

/* --- Process Execution Helpers --- */

/** Executes command line via popen, capturing stdout/stderr and exit code. */
bool exec_command(const std::string& cmd, std::string& output_out, int& exit_code_out);

/* --- Debian Package Control Helpers --- */

/** Parses Debian control file key-value pairs into a map. */
std::map<std::string, std::string> parse_deb_control_fields(const std::string& control_text);

/* --- FFI Array Conversion Helpers --- */

/** Converts NULL-terminated or counted C string array to std::vector<std::string>. */
std::vector<std::string> c_array_to_vector(const char** c_arr, int count);

/** Allocates C string array from std::vector<std::string>. Caller must free_c_array. */
char** vector_to_c_array(const std::vector<std::string>& vec, int* out_count);

/** Frees a C string array allocated by vector_to_c_array. */
void free_c_array(char** arr, int count);

} // namespace runepkg::util

/* -------------------------------------------------------------------------- */
/* C FFI Bridge Declarations                                                  */
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

char** runepkg_util_cpp_split_string(const char* str, char delim, int* count_out);
void runepkg_util_cpp_free_string_array(char** arr, int count);
char* runepkg_util_cpp_read_file(const char* filepath);
int runepkg_util_cpp_exec_cmd(const char* cmd, char** output_out);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_UTIL_CPP_HPP */
