/*****************************************************************************
 * Filename:    runepkg_crypto.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Cryptographic verification implementation for runepkg
 * LICENSE:     GPL v3
 ***************************************************************************/

#include "runepkg_portable.h"
#include "runepkg_crypto.h"
#include "runepkg_util.h"
#include "runepkg_defensive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

extern bool g_verify_signatures; /* Defined in runepkg_config.c */

static char* get_pkg_name_from_path(const char* file_path) {
    char* bname = strdup(file_path);
    char* base = basename(bname);
    char* underscore = strchr(base, '_');
    char* result;
    if (underscore) *underscore = '\0';
    result = strdup(base);
    free(bname);
    return result;
}

bool runepkg_crypto_is_enabled(void) {
    return g_verify_signatures;
}

int runepkg_crypto_verify_file(const char *file_path, const char *signature_path) {
    char *gpg_path = "/usr/bin/gpg";
    char *argv[8];
    int result;
    char* pkg_name;

    if (!file_path || !signature_path) return -1;

    runepkg_util_log_verbose("Verifying signature: %s (detached: %s)\n", file_path, signature_path);

    /* Security Check: Validate paths */
    if (runepkg_validate_path(file_path) != RUNEPKG_SUCCESS ||
        runepkg_validate_path(signature_path) != RUNEPKG_SUCCESS) {
        return -1;
    }

    argv[0] = "gpg";
    argv[1] = "--quiet";
    argv[2] = "--batch";
    argv[3] = "--no-tty";
    argv[4] = "--verify";
    argv[5] = (char *)signature_path;
    argv[6] = (char *)file_path;
    argv[7] = NULL;

    result = runepkg_util_execute_command_silent(gpg_path, argv);

    pkg_name = get_pkg_name_from_path(file_path);
    if (result == 0) {
        printf("\033[1;34m[gpg_sig]\033[0m \033[1;32mPassed\033[0m for %s\n", pkg_name);
    }
    free(pkg_name);

    return result;
}
