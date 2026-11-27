/******************************************************************************
- Filename:    runepkg_guard.hpp
- Author:      <michkochris@gmail.com>
- Date:        2026-03-04
- Description: RAII Transaction Guard header declarations for C++ Extended Suite
- LICENSE:     GPLv3
-
- GPLv3 is the platinum shield of open source software and freedom.
- Turning code into a permanent public asset that protects builders and users
- from patent warfare and locked-down hardware.
-
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
