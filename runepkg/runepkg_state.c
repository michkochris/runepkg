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

static TransactionContext *g_active_tx_ctx = NULL;

static void fsm_signal_handler(int sig)
{
    if (g_active_tx_ctx && !g_active_tx_ctx->committed &&
        g_active_tx_ctx->state != RUNEPKG_STATE_IDLE &&
        g_active_tx_ctx->state != RUNEPKG_STATE_FAILED)
    {
        char err_msg[256];
        runepkg_secure_snprintf(err_msg, sizeof(err_msg),
            "Transaction aborted by signal %d (%s)", sig, strsignal(sig));

        runepkg_log_fail(err_msg, g_active_tx_ctx->log_dir);
        step_rollback(g_active_tx_ctx);
        step_cleanup(g_active_tx_ctx);
    }
    _exit(128 + sig);
}

void runepkg_fsm_install_signal_handlers(TransactionContext *ctx)
{
    g_active_tx_ctx = ctx;
    signal(SIGINT, fsm_signal_handler);
    signal(SIGTERM, fsm_signal_handler);
    signal(SIGSEGV, fsm_signal_handler);
}

void runepkg_fsm_restore_signal_handlers(void)
{
    g_active_tx_ctx = NULL;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
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

    runepkg_log_init(ctx->log_dir, ctx->timestamp);
    runepkg_log_write("INIT", "Transaction initialized for %s (%s)", ctx->package_name, ctx->version);

    return 0;
}

RunepkgState runepkg_fsm_transition(TransactionContext *ctx, RunepkgState next_state)
{
    if (!ctx) return RUNEPKG_STATE_FAILED;

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

    runepkg_log_write("INFO", "Staging package payload under %s", ctx->staging_dir);
    return 0;
}

int step_commit(TransactionContext *ctx)
{
    if (!ctx) return -1;

    runepkg_log_write("INFO", "Committing staging changes atomically to %s", ctx->install_dir);
    ctx->committed = true;
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
        } else if (curr->action == RUNEPKG_ACTION_OVERWRITE) {
            runepkg_log_write("ROLLBACK", "Restoring %s from backup %s", curr->target_path, curr->backup_path);
            rename(curr->backup_path, curr->target_path);
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
    runepkg_fsm_restore_signal_handlers();

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
