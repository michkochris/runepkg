/******************************************************************************
 * Filename:    runepkg_crypto.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Cryptographic verification for runepkg packages
 *
 * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#ifndef RUNEPKG_CRYPTO_H
#define RUNEPKG_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Verifies the cryptographic signature of a file.
 * @param file_path Path to the file to verify.
 * @param signature_path Path to the detached signature file.
 * @return 0 if verification is successful, non-zero otherwise.
 */
int runepkg_crypto_verify_file(const char *file_path, const char *signature_path);

/**
 * @brief Checks if cryptographic verification is enabled in configuration.
 * @return true if enabled, false otherwise.
 */
bool runepkg_crypto_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // RUNEPKG_CRYPTO_H
