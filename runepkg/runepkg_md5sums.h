/******************************************************************************
- Filename:    runepkg_md5sums.h
- Author:      <michkochris@gmail.com>
- Date:        2025-01-02
- Description: MD5 checksum computation context structures and function declarations
- LICENSE:     GPLv3
-
- GPLv3 is the platinum shield of open source software and freedom.
- Turning code into a permanent public asset that protects builders and users
- from patent warfare and locked-down hardware.
-
 ******************************************************************************/

#ifndef RUNEPKG_MD5SUMS_H
#define RUNEPKG_MD5SUMS_H

#include "runepkg_portable.h"

/* MD5 context structure */
typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} runepkg_md5_ctx;

void runepkg_md5_init(runepkg_md5_ctx *ctx);
void runepkg_md5_update(runepkg_md5_ctx *ctx, const uint8_t *input, size_t len);
void runepkg_md5_final(runepkg_md5_ctx *ctx, uint8_t digest[16]);

/**
 * @brief Computes MD5 hash of a file.
 * @param path Path to the file.
 * @param output Buffer to store the resulting 32-char hex string (must be 33 bytes).
 * @return 0 on success, -1 on failure.
 */
int runepkg_md5_file(const char *path, char output[33]);

#endif /* RUNEPKG_MD5SUMS_H */
