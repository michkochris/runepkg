/******************************************************************************
 * Filename:    runepkg_md5sums.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-05-12
 * Description: Standalone MD5 implementation for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_MD5SUMS_H
#define RUNEPKG_MD5SUMS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t size;        // Size of input in bytes
    uint32_t buffer[4];   // Current accumulated MD5 sum
    uint8_t input[64];    // Input to be used in next step
} runepkg_md5_ctx;

void runepkg_md5_init(runepkg_md5_ctx *ctx);
void runepkg_md5_update(runepkg_md5_ctx *ctx, const uint8_t *input, size_t input_len);
void runepkg_md5_final(runepkg_md5_ctx *ctx, uint8_t result[16]);

/**
 * @brief Computes the MD5 hash of a file.
 * @param path Path to the file.
 * @param output Buffer of at least 33 bytes to store the hex string.
 * @return 0 on success, -1 on failure.
 */
int runepkg_md5_file(const char *path, char output[33]);

#endif // RUNEPKG_MD5SUMS_H
