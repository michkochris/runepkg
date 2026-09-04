/******************************************************************************
 * Filename:    runepkg_state.h
 * Author:      <michkochris@gmail.com>
 * Date:        2026-03-04
 * Description: Finite State Machine (FSM) transactional lifecycle & rollback
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_STATE_H
#define RUNEPKG_STATE_H

#include "runepkg_portable.h"
#include "runepkg_config.h"
#include "runepkg_defensive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- FSM Transactional States --- */
typedef enum {
    RUNEPKG_STATE_IDLE = 0,
    RUNEPKG_STATE_PREPARING,   /* Resolving dependencies, acquiring locks, setting up paths */
    RUNEPKG_STATE_FETCHING,    /* Downloading packages or unpacking source runes */
    RUNEPKG_STATE_VALIDATING,  /* SHA256/MD5 hashing, GPG signature verification */
    RUNEPKG_STATE_STAGING,     /* Extracting payload to temporary staging sysroot */
    RUNEPKG_STATE_COMMITTING,  /* Atomic file renaming/swapping and DB update */
    RUNEPKG_STATE_ROLLBACK,    /* Error handler: restoring previous state / purging staging */
    RUNEPKG_STATE_CLEANUP,     /* Purging temporary build/download directories */
    RUNEPKG_STATE_FAILED       /* Terminal abort state */
} RunepkgState;

/* --- Journal Action Types for Rollback --- */
typedef enum {
    RUNEPKG_ACTION_CREATE = 0,   /* File created: rollback deletes it */
    RUNEPKG_ACTION_OVERWRITE,    /* File overwritten: rollback restores backup */
    RUNEPKG_ACTION_DELETE        /* File deleted: rollback restores backup */
} RunepkgJournalAction;

/* --- Journal Entry Node --- */
typedef struct RunepkgJournalEntry {
    RunepkgJournalAction action;
    char target_path[PATH_MAX];
    char backup_path[PATH_MAX];
    struct RunepkgJournalEntry *next;
} RunepkgJournalEntry;

/* --- Transaction Context --- */
typedef struct TransactionContext {
    RunepkgState state;
    char package_name[256];
    char version[128];
    char staging_dir[PATH_MAX];
    char install_dir[PATH_MAX];
    char log_dir[PATH_MAX];
    char timestamp[32];
    RunepkgJournalEntry *journal_head;
    int journal_count;
    int lock_fd;
    int sig_pipe[2];
    volatile sig_atomic_t abort_requested;
    bool committed;
    bool cleanup_enabled;
} TransactionContext;

/* --- FSM Lifecycle Core --- */

/**
 * @brief Initializes a transaction context with package details and paths
 * @param ctx Pointer to TransactionContext struct
 * @param pkg_name Package name
 * @param version Package version string
 * @return 0 on success, non-zero on error
 */
int runepkg_fsm_init(TransactionContext *ctx, const char *pkg_name, const char *version);

/**
 * @brief Returns the string representation of an FSM state
 * @param state RunepkgState enum value
 * @return String description of state
 */
const char *runepkg_state_to_string(RunepkgState state);

/**
 * @brief Transition FSM state and record transition in transaction log
 * @param ctx Pointer to TransactionContext
 * @param next_state New state to transition to
 * @return New state
 */
RunepkgState runepkg_fsm_transition(TransactionContext *ctx, RunepkgState next_state);

/**
 * @brief Main FSM state dispatch loop executing full transaction pipeline
 * @param ctx Pointer to TransactionContext
 * @return Final RunepkgState (RUNEPKG_STATE_IDLE on success, RUNEPKG_STATE_FAILED on error)
 */
RunepkgState runepkg_execute_transaction(TransactionContext *ctx);

/* --- FSM Step Handlers --- */
int step_prepare(TransactionContext *ctx);
int step_validate(TransactionContext *ctx);
int step_stage(TransactionContext *ctx);
int step_commit(TransactionContext *ctx);
int step_rollback(TransactionContext *ctx);
int step_cleanup(TransactionContext *ctx);

/* --- Journal Helpers --- */
int runepkg_journal_record_create(TransactionContext *ctx, const char *target_path);
int runepkg_journal_record_overwrite(TransactionContext *ctx, const char *target_path, const char *backup_path);
int runepkg_journal_record_delete(TransactionContext *ctx, const char *target_path, const char *backup_path);
void runepkg_journal_free(TransactionContext *ctx);

/* --- Signal Trap & Self-Pipe Management --- */
void runepkg_fsm_install_signal_handlers(TransactionContext *ctx);
void runepkg_fsm_restore_signal_handlers(void);
void runepkg_fsm_check_signals(TransactionContext *ctx);

/* --- Process Locking --- */
int runepkg_fsm_acquire_lock(TransactionContext *ctx);
void runepkg_fsm_release_lock(TransactionContext *ctx);

/* --- Thread Local Context Accessors --- */
void runepkg_set_current_tx(TransactionContext *ctx);
TransactionContext *runepkg_get_current_tx(void);

/* --- Startup Crash Recovery & Audit --- */
int runepkg_fsm_recover_orphaned_transactions(void);

#endif /* RUNEPKG_STATE_H */
