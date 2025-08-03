// upkgexec.c - Functions for secure script execution in upkg
// This file contains definitions for:
// - check_null_termination_and_exit
// - parse_shebang (static helper)
// - execute_pkginfo_script

#include <stdio.h>    // For fprintf, perror
#include <stdlib.h>   // For exit, malloc, free, strdup, getenv
#include <string.h>   // For strlen, strchr, strncpy, strtok_r, strerror
#include <unistd.h>   // For fork, pipe, dup2, close, execve, access
#include <sys/wait.h> // For waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <errno.h>    // For errno
#include <signal.h>   // For kill
#include <stdbool.h>  // For bool type

#include "upkg_hash.h" // Includes our new logging function prototypes
#include "upkg_lib.h"
#include "upkg_exec.h"

// --- Preprocessor Defines ---
// Adjust these values based on your project's expected limits.
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 1024 // Max path for interpreter (e.g., /usr/bin/python3)
#endif
#ifndef MAX_SHEBANG_ARGS
#define MAX_SHEBANG_ARGS 16 // Max number of arguments on shebang line (e.g., #!/usr/bin/env python3 -B -O)
#endif
#ifndef MAX_ARG_LEN
#define MAX_ARG_LEN 256 // Max length for a single argument from shebang
#endif
// Max length for the dynamically constructed PATH environment variable string
#ifndef MAX_ENV_PATH_LEN
#define MAX_ENV_PATH_LEN 2048
#endif


// --- Function Prototypes ---
void check_null_termination_and_exit(const char *buffer, int expected_len,
                                      const char *function_name, const char *param_name);
static int parse_shebang(const char *script_content, int script_len,
                         char *interpreter_buffer, size_t interpreter_buffer_size,
                         char *argv_buffer[], size_t argv_buffer_size);


// --- Function Definitions ---

/**
 * @brief Defensively checks if a char* buffer is null-terminated within its *exact* expected length.
 * Exits the program if not.
 *
 * This function is crucial for situations where a buffer is expected to be a valid C string
 * (null-terminated) and its precise allocated or valid content length is known. It prevents
 * `strlen` or other string operations from reading past allocated memory if the string
 * is truly malformed (not null-terminated within the given bounds).
 *
 * @param buffer The character array/string to check.
 * @param expected_len The expected length of the valid string content (excluding null terminator).
 * This should typically be the size of the *allocated buffer* (e.g., file_size + 1 for the null)
 * or the actual number of bytes read if the null terminator is *after* these bytes.
 * If your `collect_file_content` stores `file_size` as `preinst_len`, then pass `preinst_len + 1` here.
 * @param function_name The name of the calling function, for clearer error messages.
 * @param param_name The name of the parameter being checked, for clearer error messages.
 * @return void. Exits with EXIT_FAILURE if the buffer is NULL or not null-terminated
 * within `expected_len` bytes.
 */
void check_null_termination_and_exit(const char *buffer, int expected_len,
                                      const char *function_name, const char *param_name) {
    if (buffer == NULL) {
        upkg_log_debug("Defensive Check Error in %s: Parameter '%s' is NULL.\n", function_name, param_name);
        exit(EXIT_FAILURE);
    }

    bool found_null = false;
    for (int i = 0; i < expected_len; ++i) {
        if (buffer[i] == '\0') {
            found_null = true;
            break;
        }
    }

    if (!found_null) {
        upkg_log_debug("Defensive Check Error in %s: Parameter '%s' is not null-terminated "
                        "within its expected length of %d bytes. "
                        "This indicates a potential memory corruption or string handling error. Aborting.\n",
                function_name, param_name, expected_len);
        exit(EXIT_FAILURE);
    }
}


/**
 * @brief Parses a shebang line to extract the interpreter path and its arguments.
 *
 * Examples:
 * "#!/bin/sh" -> interpreter="/bin/sh", argv_buffer[0]="/bin/sh", argv_buffer[1]=NULL
 * "#!/usr/bin/env bash -e" -> interpreter="/usr/bin/env", argv_buffer[0]="/usr/bin/env", argv_buffer[1]="bash", argv_buffer[2]="-e", argv_buffer[3]=NULL
 *
 * @param script_content The content of the script, assumed to start with #! and be null-terminated.
 * @param script_len The total length of the script content (used for bounds checking the shebang line itself).
 * @param interpreter_buffer Buffer to store the extracted interpreter path.
 * @param interpreter_buffer_size Size of the interpreter_buffer.
 * @param argv_buffer Array of char pointers to store arguments. argv_buffer[0] will be
 * the interpreter, subsequent elements will be its arguments. This function performs `strdup`
 * for arguments, which must be `free`d by the caller (or the `execute_pkginfo_script` function).
 * @param argv_buffer_size Max number of arguments (size of argv_buffer array).
 * @return The number of arguments parsed (including interpreter as argv[0]), or -1 on error.
 */
static int parse_shebang(const char *script_content, int script_len,
                         char *interpreter_buffer, size_t interpreter_buffer_size,
                         char *argv_buffer[], size_t argv_buffer_size) {

    if (script_len < 2 || script_content[0] != '#' || script_content[1] != '!') {
        upkg_log_debug("Error in parse_shebang: Script content is too short or does not start with a shebang.\n");
        return -1;
    }
    if (!interpreter_buffer || interpreter_buffer_size == 0 || !argv_buffer || argv_buffer_size == 0) {
        upkg_log_debug("Error in parse_shebang: Invalid buffers provided for parsing.\n");
        return -1;
    }

    const char *line_start = script_content + 2;
    const char *line_end_nl = strchr(line_start, '\n');
    const char *line_end;

    if (line_end_nl) {
        line_end = line_end_nl;
    } else {
        line_end = script_content + script_len;
    }

    char shebang_line[MAX_PATH_LEN + MAX_SHEBANG_ARGS * MAX_ARG_LEN];
    size_t shebang_line_len = line_end - line_start;

    if (shebang_line_len >= sizeof(shebang_line)) {
        upkg_log_debug("Error in parse_shebang: Shebang line too long (%zu bytes). Max allowed for parsing: %zu bytes.\n",
                shebang_line_len, sizeof(shebang_line) - 1);
        return -1;
    }
    strncpy(shebang_line, line_start, shebang_line_len);
    shebang_line[shebang_line_len] = '\0';

    char *token;
    size_t arg_count = 0;
    char *rest = shebang_line;

    token = strtok_r(rest, " \t", &rest);
    if (!token) {
        upkg_log_debug("Error in parse_shebang: Empty shebang interpreter path found.\n");
        return -1;
    }

    if (strlen(token) >= interpreter_buffer_size) {
        upkg_log_debug("Error in parse_shebang: Interpreter path '%s' too long for buffer (max %zu).\n", token, interpreter_buffer_size);
        return -1;
    }
    strcpy(interpreter_buffer, token);
    argv_buffer[arg_count++] = interpreter_buffer;

    while ((token = strtok_r(rest, " \t", &rest)) != NULL && arg_count < argv_buffer_size - 1) {
        argv_buffer[arg_count] = strdup(token);
        if (argv_buffer[arg_count] == NULL) {
            upkg_log_debug("Error in parse_shebang: strdup failed for shebang argument: %s\n", strerror(errno));
            for (size_t i = 1; i < arg_count; ++i) { free(argv_buffer[i]); } // Use size_t for loop variable too
            return -1;
        }
        arg_count++;
    }

    if (arg_count >= argv_buffer_size) {
        upkg_log_verbose("Warning: Too many arguments in shebang, some might be truncated. Max: %zu args.\n", argv_buffer_size - 1);
    }

    argv_buffer[arg_count] = NULL;
    return (int)arg_count; // Cast back to int for return type
}


/**
 * @brief Executes a script directly from memory via a pipe, using its shebang
 * to determine the interpreter and its arguments. The script content is
 * expected to be a null-terminated C string with its explicit length provided.
 *
 * This function is designed for defensive and security-hardened execution
 * of trusted, package maintenance scripts. It ensures the child process has
 * a minimal, but correct, environment including the inherited PATH.
 *
 * @param script_content The content of the script (e.g., srch->preinst), must be null-terminated.
 * @param script_len The total length of the script content (bytes, excluding null terminator).
 * This should be `srch->preinst_len` (or similar).
 * @return 0 on success, non-zero on failure.
 */
int execute_pkginfo_script(const char *script_content, int script_len) {
    if (script_content == NULL) {
        upkg_log_debug("Error: Script content is NULL. Cannot execute.\n");
        return -1;
    }

    // Defensive check for null termination using the provided length.
    check_null_termination_and_exit(script_content, script_len + 1,
                                    __func__, "script_content");

    if (script_len == 0) {
        upkg_log_verbose("Info: Script content is empty (length 0). Skipping execution.\n");
        return 0;
    }

    char interpreter_path[MAX_PATH_LEN];
    char *argv_exec[MAX_SHEBANG_ARGS];
    int arg_count = parse_shebang(script_content, script_len,
                                  interpreter_path, sizeof(interpreter_path),
                                  argv_exec, MAX_SHEBANG_ARGS);

    if (arg_count == -1) {
        upkg_log_debug("Error: Failed to parse shebang from script content.\n");
        return -1;
    }

    if (access(interpreter_path, X_OK) != 0) {
        upkg_log_debug("Error: Shebang interpreter '%s' is not executable or does not exist: %s\n",
                interpreter_path, strerror(errno));
        for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
        return -1;
    }

    upkg_log_verbose("Executing script using interpreter '%s' (with %d args) from memory via pipe...\n",
            interpreter_path, arg_count - 1);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        upkg_log_debug("Error creating pipe for script execution: %s\n", strerror(errno));
        for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
        return -1;
    }

    pid_t pid = fork();

    if (pid == -1) {
        upkg_log_debug("Error forking process for script execution: %s\n", strerror(errno));
        close(pipefd[0]); close(pipefd[1]);
        for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
        return -1;
    } else if (pid == 0) { // Child process
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            upkg_log_debug("Error: dup2 failed to redirect stdin for script execution in child: %s\n", strerror(errno));
            close(pipefd[0]);
            for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
            _exit(EXIT_FAILURE);
        }
        close(pipefd[0]);

        // --- DYNAMIC PATH RETRIEVAL AND ENVIRONMENT SETUP ---
        char env_path_str[MAX_ENV_PATH_LEN];
        const char *parent_path = getenv("PATH"); // Get PATH from parent process's environment

        if (parent_path == NULL) {
            upkg_log_verbose("Warning: PATH environment variable not found in parent. Using a default safe PATH.\n");
            // Fallback for extremely minimal environments.
            snprintf(env_path_str, sizeof(env_path_str), "PATH=/bin:/usr/bin:/sbin:/usr/sbin");
        } else {
            // Construct the PATH string for the child's environment
            snprintf(env_path_str, sizeof(env_path_str), "PATH=%s", parent_path);
            if (strlen(env_path_str) >= sizeof(env_path_str)) {
                upkg_log_verbose("Warning: Constructed PATH string truncated due to MAX_ENV_PATH_LEN. Using default safe PATH.\n");
                // Fallback if the inherited PATH is too long for our buffer
                snprintf(env_path_str, sizeof(env_path_str), "PATH=/bin:/usr/bin:/sbin:/usr/sbin");
            }
        }

        // Define the child's environment variables.
        // These pointers must point to static/global memory or memory that persists
        // until execve replaces the process image. `env_path_str` is on the stack,
        // which is fine because `execve` happens immediately.
        char *const new_environ[] = {
            env_path_str, // Use the dynamically retrieved/constructed PATH
            (char*)"HOME=/tmp",
            (char*)"TERM=dumb",
            (char*)"LANG=C",
            NULL // Must be null-terminated
        };

        execve(interpreter_path, argv_exec, new_environ);

        upkg_log_debug("Error executing interpreter via execve in child process: %s\n", strerror(errno));
        for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
        _exit(EXIT_FAILURE);
    } else { // Parent process
        close(pipefd[0]);

        ssize_t bytes_written = write(pipefd[1], script_content, script_len);
        if (bytes_written == -1 || bytes_written != script_len) {
            upkg_log_debug("Error writing script content to pipe: %s\n", strerror(errno));
            close(pipefd[1]);
            upkg_log_debug("Warning: Sending SIGTERM to child process %d due to parent write failure.\n", pid);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
            return -1;
        }

        close(pipefd[1]);

        int status;
        if (waitpid(pid, &status, 0) == -1) {
            upkg_log_debug("Error waiting for child script process: %s\n", strerror(errno));
            for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }
            return -1;
        }

        for (int i = 1; i < arg_count; ++i) { free(argv_exec[i]); }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                upkg_log_debug("Error: Script exited with non-zero status %d.\n", WEXITSTATUS(status));
                return WEXITSTATUS(status);
            } else {
                upkg_log_verbose("Script executed successfully.\n");
                return 0;
            }
        } else if (WIFSIGNALED(status)) {
            upkg_log_debug("Error: Script terminated by signal %d (%s).\n", WTERMSIG(status), strsignal(WTERMSIG(status)));
            return -2;
        } else {
            upkg_log_debug("Error: Script terminated abnormally (status %d).\n", status);
            return -3;
        }
    }
}

