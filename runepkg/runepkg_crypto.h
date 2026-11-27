/******************************************************************************
- Filename:    runepkg_crypto.h
- Author:      <michkochris@gmail.com>
- Date:        2025-01-04
- Description: Cryptographic verification function declarations and payload security interfaces
- LICENSE:     GPLv3
-
- GPLv3 is the platinum shield of open source software and freedom.
- Turning code into a permanent public asset that protects builders and users
- from patent warfare and locked-down hardware.
-
 ******************************************************************************/

#ifndef RUNEPKG_CRYPTO_H
#define RUNEPKG_CRYPTO_H

#ifndef RUNEPKG_PORTABLE_H
#include "runepkg_portable.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

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

#endif /* RUNEPKG_CRYPTO_H */
