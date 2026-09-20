/******************************************************************************
 * Filename:    runepkg_state.c
 * Author:      <michkochris@gmail.com>
 * Date:        2026-03-04
 * Description: Implementation of FSM transactional lifecycle & rollback
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_state.h"
#include "runepkg_util.h"
#include "runepkg_storage.h"
#include "runepkg_defensive.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

static TransactionContext *g_active_tx_ctx = NULL;
static __thread TransactionContext *g_current_tx = NULL;

void runepkg_set_current_tx(TransactionContext *ctx)
{
    g_current_tx = ctx;
}

TransactionContext *runepkg_get_current_tx(void)
{
    return g_current_tx;
}

static void fsm_signal_handler(int sig)
{
    int saved_errno = errno;
    unsigned char b = (unsigned char)sig;
    if (g_active_tx_ctx && g_active_tx_ctx->sig_pipe[1] >= 0) {
        ssize_t n = write(g_active_tx_ctx->sig_pipe[1], &b, 1);
        (void)n;
    }
    errno = saved_errno;
}

void runepkg_fsm_install_signal_handlers(TransactionContext *ctx)
{
    struct sigaction sa;
    int flags;

    if (!ctx) return;

    g_active_tx_ctx = ctx;
    ctx->sig_pipe[0] = -1;
    ctx->sig_pipe[1] = -1;
    ctx->abort_requested = 0;

    if (pipe(ctx->sig_pipe) == 0) {
        flags = fcntl(ctx->sig_pipe[0], F_GETFL, 0);
        if (flags >= 0) fcntl(ctx->sig_pipe[0], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(ctx->sig_pipe[1], F_GETFL, 0);
        if (flags >= 0) fcntl(ctx->sig_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fsm_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

void runepkg_fsm_restore_signal_handlers(void)
{
    struct sigaction sa;

    if (g_active_tx_ctx) {
        if (g_active_tx_ctx->sig_pipe[0] >= 0) { close(g_active_tx_ctx->sig_pipe[0]); g_active_tx_ctx->sig_pipe[0] = -1; }
        if (g_active_tx_ctx->sig_pipe[1] >= 0) { close(g_active_tx_ctx->sig_pipe[1]); g_active_tx_ctx->sig_pipe[1] = -1; }
    }

    g_active_tx_ctx = NULL;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

void runepkg_fsm_check_signals(TransactionContext *ctx)
{
    unsigned char sig_char = 0;
    ssize_t n;

    if (!ctx || ctx->sig_pipe[0] < 0) return;

    n = read(ctx->sig_pipe[0], &sig_char, 1);
    if (n > 0) {
        char err_msg[256];
        ctx->abort_requested = 1;
        runepkg_secure_snprintf(err_msg, sizeof(err_msg),
            "Transaction abort requested by signal %d (%s)", (int)sig_char, strsignal((int)sig_char));
        runepkg_log_write("SIGNAL", "%s", err_msg);
        runepkg_log_fail(err_msg, ctx->log_dir);
        runepkg_fsm_transition(ctx, RUNEPKG_STATE_ROLLBACK);
        step_rollback(ctx);
        step_cleanup(ctx);
    }
}

int runepkg_fsm_acquire_lock(TransactionContext *ctx)
{
    char lock_path[PATH_MAX];
    struct flock fl;
    const char *base_dir;
    int attempts = 0;
    int max_attempts = 6;
    useconds_t delay_us = 50000; /* 50ms initial delay */

    if (!ctx) return -1;
    if (ctx->lock_fd > 0) return 0;

    base_dir = ctx->log_dir[0] != '\0' ? ctx->log_dir : (g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log");
    runepkg_util_create_dir_recursive(base_dir, 0755);
    runepkg_secure_snprintf(lock_path, sizeof(lock_path), "%s/transaction.lock", base_dir);

    ctx->lock_fd = open(lock_path, O_CREAT | O_RDWR, 0640);
    if (ctx->lock_fd < 0) {
        runepkg_log_write("ERROR", "Failed to open lock file %s: %s", lock_path, strerror(errno));
        return -1;
    }

    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    while (attempts < max_attempts) {
        if (fcntl(ctx->lock_fd, F_SETLK, &fl) == 0) {
            runepkg_log_write("INFO", "Acquired process-global transaction lock on %s (attempt %d)", lock_path, attempts + 1);
            return 0;
        }

        if (errno == EACCES || errno == EAGAIN) {
            attempts++;
            if (attempts >= max_attempts) {
                break;
            }
            runepkg_log_write("WARN", "Transaction lock busy on %s, retrying in %u ms (attempt %d/%d)...",
                lock_path, (unsigned int)(delay_us / 1000), attempts, max_attempts);
            usleep(delay_us);
            if (delay_us < 400000) {
                delay_us *= 2;
            }
        } else {
            break;
        }
    }

    runepkg_log_write("ERROR", "Failed to acquire exclusive transaction lock on %s after %d attempts: %s",
        lock_path, attempts, strerror(errno));
    close(ctx->lock_fd);
    ctx->lock_fd = -1;
    return -1;
}

void runepkg_fsm_release_lock(TransactionContext *ctx)
{
    struct flock fl;

    if (!ctx || ctx->lock_fd < 0) return;

    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    fcntl(ctx->lock_fd, F_SETLK, &fl);
    close(ctx->lock_fd);
    ctx->lock_fd = -1;
    runepkg_log_write("INFO", "Released process-global transaction lock");
}

const char *runepkg_state_to_string(RunepkgState state)
{
    switch (state) {
        case RUNEPKG_STATE_IDLE:       return "IDLE";
        case RUNEPKG_STATE_PREPARING:  return "PREPARING";
        case RUNEPKG_STATE_FETCHING:   return "FETCHING";
        case RUNEPKG_STATE_VALIDATING: return "VALIDATING";
        case RUNEPKG_STATE_STAGING:    return "STAGING";
        case RUNEPKG_STATE_COMMITTING: return "COMMITTING";
        case RUNEPKG_STATE_ROLLBACK:   return "ROLLBACK";
        case RUNEPKG_STATE_CLEANUP:    return "CLEANUP";
        case RUNEPKG_STATE_FAILED:     return "FAILED";
        default:                       return "UNKNOWN";
    }
}

int runepkg_fsm_init(TransactionContext *ctx, const char *pkg_name, const char *version)
{
    time_t now;
    struct tm *t;
    pid_t pid;

    if (!ctx) return -1;

    memset(ctx, 0, sizeof(TransactionContext));
    ctx->state = RUNEPKG_STATE_IDLE;
    ctx->committed = false;
    ctx->cleanup_enabled = g_cleanup_extract_dirs;
    ctx->lock_fd = -1;
    ctx->sig_pipe[0] = -1;
    ctx->sig_pipe[1] = -1;

    if (pkg_name) {
        runepkg_secure_strcpy(ctx->package_name, sizeof(ctx->package_name), pkg_name);
    } else {
        runepkg_secure_strcpy(ctx->package_name, sizeof(ctx->package_name), "unknown");
    }

    if (version) {
        runepkg_secure_strcpy(ctx->version, sizeof(ctx->version), version);
    } else {
        runepkg_secure_strcpy(ctx->version, sizeof(ctx->version), "0.0.0");
    }

    now = time(NULL);
    t = localtime(&now);
    strftime(ctx->timestamp, sizeof(ctx->timestamp), "%Y%m%d-%H%M%S", t);

    pid = getpid();
    if (g_log_dir && g_log_dir[0] != '\0') {
        runepkg_secure_strcpy(ctx->log_dir, sizeof(ctx->log_dir), g_log_dir);
    } else {
        runepkg_secure_strcpy(ctx->log_dir, sizeof(ctx->log_dir), "/var/lib/runepkg_dir/log");
    }

    if (g_install_dir_internal && g_install_dir_internal[0] != '\0') {
        runepkg_secure_strcpy(ctx->install_dir, sizeof(ctx->install_dir), g_install_dir_internal);
    } else {
        runepkg_secure_strcpy(ctx->install_dir, sizeof(ctx->install_dir), "/");
    }

    runepkg_secure_snprintf(ctx->staging_dir, sizeof(ctx->staging_dir),
        "%s/staging_%d", ctx->log_dir, (int)pid);

    runepkg_set_current_tx(ctx);
    runepkg_log_init(ctx->log_dir, ctx->timestamp);
    runepkg_log_write("INIT", "Transaction initialized for %s (%s)", ctx->package_name, ctx->version);

    return 0;
}

RunepkgState runepkg_fsm_transition(TransactionContext *ctx, RunepkgState next_state)
{
    if (!ctx) return RUNEPKG_STATE_FAILED;

    runepkg_fsm_check_signals(ctx);

    runepkg_log_write("FSM", "Transition: %s -> %s [Package: %s]",
        runepkg_state_to_string(ctx->state),
        runepkg_state_to_string(next_state),
        ctx->package_name);

    ctx->state = next_state;
    return ctx->state;
}

int runepkg_journal_record_create(TransactionContext *ctx, const char *target_path)
{
    RunepkgJournalEntry *entry;

    if (!ctx || !target_path) return -1;

    entry = (RunepkgJournalEntry *)runepkg_secure_malloc(sizeof(RunepkgJournalEntry));
    if (!entry) return -1;

    memset(entry, 0, sizeof(RunepkgJournalEntry));
    entry->action = RUNEPKG_ACTION_CREATE;
    runepkg_secure_strcpy(entry->target_path, sizeof(entry->target_path), target_path);

    entry->next = ctx->journal_head;
    ctx->journal_head = entry;
    ctx->journal_count++;

    runepkg_log_write("JOURNAL", "[CREATE] Target: %s", target_path);

    return 0;
}

int runepkg_journal_record_overwrite(TransactionContext *ctx, const char *target_path, const char *backup_path)
{
    RunepkgJournalEntry *entry;

    if (!ctx || !target_path || !backup_path) return -1;

    entry = (RunepkgJournalEntry *)runepkg_secure_malloc(sizeof(RunepkgJournalEntry));
    if (!entry) return -1;

    memset(entry, 0, sizeof(RunepkgJournalEntry));
    entry->action = RUNEPKG_ACTION_OVERWRITE;
    runepkg_secure_strcpy(entry->target_path, sizeof(entry->target_path), target_path);
    runepkg_secure_strcpy(entry->backup_path, sizeof(entry->backup_path), backup_path);

    entry->next = ctx->journal_head;
    ctx->journal_head = entry;
    ctx->journal_count++;

    runepkg_log_write("JOURNAL", "[OVERWRITE] Target: %s | Backup: %s", target_path, backup_path);

    return 0;
}

int runepkg_journal_record_delete(TransactionContext *ctx, const char *target_path, const char *backup_path)
{
    RunepkgJournalEntry *entry;

    if (!ctx || !target_path) return -1;

    entry = (RunepkgJournalEntry *)runepkg_secure_malloc(sizeof(RunepkgJournalEntry));
    if (!entry) return -1;

    memset(entry, 0, sizeof(RunepkgJournalEntry));
    entry->action = RUNEPKG_ACTION_DELETE;
    runepkg_secure_strcpy(entry->target_path, sizeof(entry->target_path), target_path);
    if (backup_path) {
        runepkg_secure_strcpy(entry->backup_path, sizeof(entry->backup_path), backup_path);
    }

    entry->next = ctx->journal_head;
    ctx->journal_head = entry;
    ctx->journal_count++;

    runepkg_log_write("JOURNAL", "[DELETE] Target: %s | Backup: %s",
        target_path, backup_path ? backup_path : "N/A");

    return 0;
}

void runepkg_journal_free(TransactionContext *ctx)
{
    RunepkgJournalEntry *curr, *next;

    if (!ctx) return;

    curr = ctx->journal_head;
    while (curr) {
        next = curr->next;
        runepkg_secure_free((void **)&curr, sizeof(RunepkgJournalEntry));
        curr = next;
    }
    ctx->journal_head = NULL;
    ctx->journal_count = 0;
}

int step_prepare(TransactionContext *ctx)
{
    if (!ctx) return -1;

    runepkg_fsm_install_signal_handlers(ctx);

    if (runepkg_fsm_acquire_lock(ctx) != 0) {
        runepkg_log_write("WARN", "Could not acquire transaction lock, continuing best-effort");
    }

    if (runepkg_log_init(ctx->log_dir, ctx->timestamp) != 0) {
        return -1;
    }

    runepkg_log_write("INFO", "Preparing transaction for %s (%s)", ctx->package_name, ctx->version);

    if (mkdir(ctx->staging_dir, 0755) != 0 && errno != EEXIST) {
        runepkg_log_write("ERROR", "Failed to create staging directory %s: %s",
            ctx->staging_dir, strerror(errno));
        return -1;
    }

    return 0;
}

int step_validate(TransactionContext *ctx)
{
    if (!ctx) return -1;

    runepkg_fsm_check_signals(ctx);
    runepkg_log_write("INFO", "Validating environment and path traversal safety for %s", ctx->package_name);

    if (runepkg_validate_path(ctx->install_dir) != RUNEPKG_SUCCESS) {
        runepkg_log_write("ERROR", "Install target path %s failed validation", ctx->install_dir);
        return -1;
    }

    return 0;
}

int step_stage(TransactionContext *ctx)
{
    if (!ctx) return -1;

    runepkg_fsm_check_signals(ctx);
    runepkg_log_write("INFO", "Staging package payload under %s", ctx->staging_dir);
    return 0;
}

int step_commit(TransactionContext *ctx)
{
    RunepkgJournalEntry *curr;

    if (!ctx) return -1;

    runepkg_fsm_check_signals(ctx);
    runepkg_log_write("INFO", "Committing staging changes atomically to %s", ctx->install_dir);

    curr = ctx->journal_head;
    while (curr) {
        /* Check for staging files that need atomic swapping */
        if (curr->backup_path[0] != '\0' && runepkg_util_file_exists(curr->backup_path)) {
            /* Keep pre-mutation backups until cleanup */
            runepkg_log_write("COMMIT", "Preserving pre-mutation backup: %s", curr->backup_path);
        }
        curr = curr->next;
    }

    ctx->committed = true;
    runepkg_log_write("INFO", "Committed all transactional changes successfully for %s", ctx->package_name);
    return 0;
}

int step_rollback(TransactionContext *ctx)
{
    RunepkgJournalEntry *curr;

    if (!ctx) return -1;

    runepkg_log_write("WARN", "Initiating transaction rollback for %s", ctx->package_name);

    curr = ctx->journal_head;
    while (curr) {
        if (curr->action == RUNEPKG_ACTION_CREATE) {
            runepkg_log_write("ROLLBACK", "Unlinking created file: %s", curr->target_path);
            unlink(curr->target_path);
        } else if (curr->action == RUNEPKG_ACTION_OVERWRITE || curr->action == RUNEPKG_ACTION_DELETE) {
            if (curr->backup_path[0] != '\0') {
                runepkg_log_write("ROLLBACK", "Restoring %s from backup %s", curr->target_path, curr->backup_path);
                rename(curr->backup_path, curr->target_path);
            }
        }
        curr = curr->next;
    }

    if (ctx->staging_dir[0] != '\0') {
        runepkg_log_write("ROLLBACK", "Purging staging directory: %s", ctx->staging_dir);
        runepkg_storage_remove_directory_tree(ctx->staging_dir);
    }

    return 0;
}

int step_cleanup(TransactionContext *ctx)
{
    if (!ctx) return -1;

    runepkg_log_write("INFO", "Cleaning up transaction resources for %s", ctx->package_name);

    if (ctx->staging_dir[0] != '\0') {
        runepkg_storage_remove_directory_tree(ctx->staging_dir);
    }

    runepkg_journal_free(ctx);
    runepkg_fsm_release_lock(ctx);
    runepkg_fsm_restore_signal_handlers();
    runepkg_set_current_tx(NULL);

    runepkg_log_close(ctx->cleanup_enabled);
    return 0;
}

RunepkgState runepkg_execute_transaction(TransactionContext *ctx)
{
    RunepkgState current_state;

    if (!ctx) return RUNEPKG_STATE_FAILED;

    current_state = RUNEPKG_STATE_PREPARING;
    ctx->state = current_state;

    while (current_state != RUNEPKG_STATE_IDLE && current_state != RUNEPKG_STATE_FAILED) {
        runepkg_fsm_check_signals(ctx);
        if (ctx->abort_requested) {
            current_state = RUNEPKG_STATE_FAILED;
            break;
        }

        switch (current_state) {
            case RUNEPKG_STATE_PREPARING:
                if (step_prepare(ctx) == 0) {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_VALIDATING);
                } else {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_ROLLBACK);
                }
                break;

            case RUNEPKG_STATE_VALIDATING:
                if (step_validate(ctx) == 0) {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_STAGING);
                } else {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_ROLLBACK);
                }
                break;

            case RUNEPKG_STATE_STAGING:
                if (step_stage(ctx) == 0) {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_COMMITTING);
                } else {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_ROLLBACK);
                }
                break;

            case RUNEPKG_STATE_COMMITTING:
                if (step_commit(ctx) != 0) {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_ROLLBACK);
                } else {
                    current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_CLEANUP);
                }
                break;

            case RUNEPKG_STATE_ROLLBACK:
                step_rollback(ctx);
                current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_FAILED);
                break;

            case RUNEPKG_STATE_CLEANUP:
                step_cleanup(ctx);
                current_state = runepkg_fsm_transition(ctx, RUNEPKG_STATE_IDLE);
                break;

            default:
                current_state = RUNEPKG_STATE_FAILED;
                break;
        }
    }

    return current_state;
}

int runepkg_fsm_recover_orphaned_transactions(void)
{
    const char *base_dir = g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log";
    DIR *dir;
    struct dirent *entry;
    int recovered_count = 0;

    if (!runepkg_util_is_directory(base_dir)) return 0;

    dir = opendir(base_dir);
    if (!dir) return 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "staging_", 8) == 0) {
            int pid = atoi(entry->d_name + 8);
            if (pid > 0) {
                if (kill(pid, 0) == -1 && errno == ESRCH) {
                    char orphan_path[PATH_MAX];
                    runepkg_secure_snprintf(orphan_path, sizeof(orphan_path), "%s/%s", base_dir, entry->d_name);
                    runepkg_log_write("RECOVERY", "Purging orphaned staging workspace from dead process PID %d: %s",
                        pid, orphan_path);
                    runepkg_storage_remove_directory_tree(orphan_path);
                    recovered_count++;
                }
            }
        }
    }

    closedir(dir);

    if (recovered_count > 0) {
        runepkg_log_write("RECOVERY", "Recovered and cleaned up %d orphaned staging workspaces", recovered_count);
    }

    return recovered_count;
}

int runepkg_fsm_list_transactions(void)
{
    const char *base_dir = g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log";
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (!runepkg_util_is_directory(base_dir)) {
        printf("No transaction logs found (log directory does not exist: %s)\n", base_dir);
        return 0;
    }

    dir = opendir(base_dir);
    if (!dir) return -1;

    printf("Historical Transaction Logs (%s):\n", base_dir);
    printf("========================================================================\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "transaction-", 12) == 0 && strstr(entry->d_name, ".log")) {
            char full_path[PATH_MAX];
            struct stat st;
            runepkg_secure_snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, entry->d_name);
            if (stat(full_path, &st) == 0) {
                printf("  [log] %-35s (%ld bytes)\n", entry->d_name, (long)st.st_size);
                count++;
            }
        }
    }

    closedir(dir);

    if (count == 0) {
        printf("  (No transaction execution logs present)\n");
    }
    printf("\n");
    return count;
}

int runepkg_fsm_inspect_transaction(const char *target)
{
    const char *base_dir = g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log";
    char log_path[PATH_MAX];
    FILE *fp;
    char line[PATH_MAX * 2];

    if (!target || target[0] == '\0') {
        printf("Error: inspect requires a timestamp or log filename.\n");
        return -1;
    }

    if (target[0] == '/') {
        runepkg_secure_strcpy(log_path, sizeof(log_path), target);
    } else if (strncmp(target, "transaction-", 12) == 0) {
        runepkg_secure_snprintf(log_path, sizeof(log_path), "%s/%s", base_dir, target);
    } else {
        runepkg_secure_snprintf(log_path, sizeof(log_path), "%s/transaction-%s.log", base_dir, target);
    }

    fp = fopen(log_path, "r");
    if (!fp) {
        printf("Error: Transaction log file not found: %s\n", log_path);
        return -1;
    }

    printf("Inspecting Transaction Log: %s\n", log_path);
    printf("========================================================================\n");

    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }

    fclose(fp);
    printf("========================================================================\n\n");
    return 0;
}
