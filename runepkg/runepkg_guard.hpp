/******************************************************************************
 * Filename:    runepkg_guard.hpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-03-04
 * Description: RAII Transaction Guard for C++ Extended Suite
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_GUARD_HPP
#define RUNEPKG_GUARD_HPP

extern "C" {
#include "runepkg_state.h"
#include "runepkg_util.h"
}

#include <string>
#include <exception>

namespace runepkg {

class RunepkgTransactionGuard {
private:
    TransactionContext* ctx_;
    bool committed_;

public:
    explicit RunepkgTransactionGuard(TransactionContext* ctx, const std::string& pkg_name = "", const std::string& version = "");
    ~RunepkgTransactionGuard();

    void commit();
    TransactionContext* context() const { return ctx_; }

    /* Delete copy constructor & copy assignment */
    RunepkgTransactionGuard(const RunepkgTransactionGuard&) = delete;
    RunepkgTransactionGuard& operator=(const RunepkgTransactionGuard&) = delete;
};

} // namespace runepkg

#endif // RUNEPKG_GUARD_HPP
