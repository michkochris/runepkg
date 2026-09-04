/******************************************************************************/
/* Filename:    runepkg_util_cpp.cpp                                           */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-08-29                                                     */
/* Description: Foundation C++ Utilities & Helpers Implementation              */
/* License:     GPL v3                                                         */
/******************************************************************************/

#include "runepkg_util_cpp.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <array>
#include <memory>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>

namespace runepkg::util {

std::string trim_left(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    return std::string(start, str.end());
}

std::string trim_right(const std::string& str) {
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    return (end <= str.begin()) ? std::string() : std::string(str.begin(), end);
}

std::string trim(const std::string& str) {
    return trim_right(trim_left(str));
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream token_stream(str);
    while (std::getline(token_stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string join(const std::vector<std::string>& elements, const std::string& delimiter) {
    std::ostringstream result;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) {
            result << delimiter;
        }
        result << elements[i];
    }
    return result.str();
}

bool starts_with(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

std::string format_bytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    size_t unit_idx = 0;

    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision((unit_idx == 0) ? 0 : 2) << size << " " << units[unit_idx];
    return out.str();
}

std::string read_file_to_string(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

bool write_string_to_file_atomic(const std::filesystem::path& filepath, const std::string& content) {
    std::filesystem::path tmp_path = filepath;
    tmp_path += ".tmp";

    {
        std::ofstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file.good()) {
            std::filesystem::remove(tmp_path);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, filepath, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    return true;
}

std::vector<std::string> read_file_lines(const std::filesystem::path& filepath) {
    std::vector<std::string> lines;
    std::ifstream file(filepath, std::ios::in);
    if (!file.is_open()) {
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool create_directories_secure(const std::filesystem::path& dir_path, mode_t mode) {
    std::error_code ec;
    if (std::filesystem::exists(dir_path, ec)) {
        return std::filesystem::is_directory(dir_path, ec);
    }

    if (!std::filesystem::create_directories(dir_path, ec) && ec) {
        return false;
    }

    chmod(dir_path.c_str(), mode);
    return true;
}

bool exec_command(const std::string& cmd, std::string& output_out, int& exit_code_out) {
    output_out.clear();
    exit_code_out = -1;

    std::array<char, 256> buffer;
    std::string redirect_cmd = cmd + " 2>&1";
    FILE* pipe = popen(redirect_cmd.c_str(), "r");
    if (!pipe) {
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output_out += buffer.data();
    }

    int status = pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        exit_code_out = WEXITSTATUS(status);
        return true;
    }

    return false;
}

std::map<std::string, std::string> parse_deb_control_fields(const std::string& control_text) {
    std::map<std::string, std::string> fields;
    std::istringstream stream(control_text);
    std::string line;
    std::string current_key;
    std::string current_val;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(line[0]))) {
            /* Continuation line */
            if (!current_key.empty()) {
                current_val += "\n" + trim(line);
                fields[current_key] = current_val;
            }
        } else {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                current_key = trim(line.substr(0, colon_pos));
                current_val = trim(line.substr(colon_pos + 1));
                fields[current_key] = current_val;
            }
        }
    }

    return fields;
}

std::vector<std::string> c_array_to_vector(const char** c_arr, int count) {
    std::vector<std::string> vec;
    if (!c_arr) return vec;

    if (count >= 0) {
        vec.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (c_arr[i]) vec.emplace_back(c_arr[i]);
        }
    } else {
        for (int i = 0; c_arr[i] != nullptr; ++i) {
            vec.emplace_back(c_arr[i]);
        }
    }
    return vec;
}

char** vector_to_c_array(const std::vector<std::string>& vec, int* out_count) {
    if (out_count) *out_count = static_cast<int>(vec.size());
    if (vec.empty()) return nullptr;

    char** arr = static_cast<char**>(std::malloc((vec.size() + 1) * sizeof(char*)));
    if (!arr) return nullptr;

    for (size_t i = 0; i < vec.size(); ++i) {
        arr[i] = strdup(vec[i].c_str());
    }
    arr[vec.size()] = nullptr;
    return arr;
}

void free_c_array(char** arr, int count) {
    if (!arr) return;
    if (count >= 0) {
        for (int i = 0; i < count; ++i) {
            std::free(arr[i]);
        }
    } else {
        for (int i = 0; arr[i] != nullptr; ++i) {
            std::free(arr[i]);
        }
    }
    std::free(arr);
}

} // namespace runepkg::util

/* -------------------------------------------------------------------------- */
/* C FFI Bridge Implementation                                                */
/* -------------------------------------------------------------------------- */

extern "C" {

char** runepkg_util_cpp_split_string(const char* str, char delim, int* count_out) {
    try {
        if (!str) {
            if (count_out) *count_out = 0;
            return nullptr;
        }
        auto tokens = runepkg::util::split(str, delim);
        return runepkg::util::vector_to_c_array(tokens, count_out);
    } catch (...) {
        if (count_out) *count_out = 0;
        return nullptr;
    }
}

void runepkg_util_cpp_free_string_array(char** arr, int count) {
    try {
        runepkg::util::free_c_array(arr, count);
    } catch (...) {
        /* No-op on error */
    }
}

char* runepkg_util_cpp_read_file(const char* filepath) {
    try {
        if (!filepath) return nullptr;
        std::string content = runepkg::util::read_file_to_string(filepath);
        if (content.empty()) return nullptr;
        return strdup(content.c_str());
    } catch (...) {
        return nullptr;
    }
}

int runepkg_util_cpp_exec_cmd(const char* cmd, char** output_out) {
    try {
        if (!cmd) return -1;
        std::string out;
        int exit_code = -1;
        bool ok = runepkg::util::exec_command(cmd, out, exit_code);
        if (output_out) {
            *output_out = ok ? strdup(out.c_str()) : nullptr;
        }
        return ok ? exit_code : -1;
    } catch (...) {
        if (output_out) *output_out = nullptr;
        return -1;
    }
}

} // extern "C"
