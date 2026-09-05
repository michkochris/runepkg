/******************************************************************************/
/* Filename:    fuzz_sanitize_path.cpp                                         */
/* Description: libFuzzer harness for runepkg::security::sanitize_extract_path */
/******************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string>
#include "../../runepkg_security.hpp"

extern "C" {
void runepkg_log_write(int, const char*, ...) {}
void runepkg_log_fail(const char*, ...) {}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;

    // Convert raw input into base directory and entry path
    // Split input at first null byte or midpoint
    size_t split = 0;
    while (split < size && data[split] != 0) {
        split++;
    }

    std::string base_dir = "/tmp/runepkg_extract_base";
    std::string entry_path;

    if (split < size) {
        // Data contains a null separator
        base_dir.append((const char*)data, split);
        entry_path.assign((const char*)data + split + 1, size - split - 1);
    } else {
        entry_path.assign((const char*)data, size);
    }

    std::string resolved_path;
    // Fuzz sanitize_extract_path - must never crash or fault under any input
    runepkg::security::sanitize_extract_path(base_dir, entry_path, resolved_path);

    return 0;
}
