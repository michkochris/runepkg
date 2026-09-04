/******************************************************************************
 * Filename:    runepkg_guard.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-03-04
 * Description: RAII Transaction Guard implementation
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_guard.hpp"
#include <iostream>

namespace runepkg {

RunepkgTransactionGuard::RunepkgTransactionGuard(TransactionContext* ctx, const std::string& pkg_name, const std::string& version)
    : ctx_(ctx), committed_(false)
{
    if (ctx_) {
        runepkg_fsm_init(ctx_, pkg_name.empty() ? nullptr : pkg_name.c_str(), version.empty() ? nullptr : version.c_str());
        runepkg_fsm_transition(ctx_, RUNEPKG_STATE_PREPARING);
    }
}

RunepkgTransactionGuard::~RunepkgTransactionGuard()
{
    if (!ctx_) return;

    if (!committed_ && ctx_->state != RUNEPKG_STATE_IDLE && ctx_->state != RUNEPKG_STATE_FAILED) {
        std::string err_msg = "Uncommitted transaction unwound via exception or early return";
#if __cplusplus >= 201703L
        if (std::uncaught_exceptions() > 0) {
            err_msg += " (uncaught C++ exception in flight)";
        }
#endif

        runepkg_log_fail(err_msg.c_str(), ctx_->log_dir);
        runepkg_fsm_transition(ctx_, RUNEPKG_STATE_ROLLBACK);
        step_rollback(ctx_);
        step_cleanup(ctx_);
        ctx_->state = RUNEPKG_STATE_FAILED;
    }
}

void RunepkgTransactionGuard::commit()
{
    if (ctx_ && !committed_) {
        step_commit(ctx_);
        runepkg_fsm_transition(ctx_, RUNEPKG_STATE_CLEANUP);
        step_cleanup(ctx_);
        committed_ = true;
    }
}

} // namespace runepkg
