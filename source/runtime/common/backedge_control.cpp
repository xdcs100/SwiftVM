#include "backedge_control.h"

#include "runtime/common/svm_config.h"

namespace swift::runtime {

bool BackedgeLatchEnabled() {
    // FLAGS_REGS publishes the token on every unit exit, including self
    // edges, so x26 is current at Signal/SMC. Do not imply latch: the P0
    // poll is a separate tax and failed the CoreMark flip gate on its own.
    return GetSvmConfig().backedge_latch;
}

bool BackedgeFlagsEnabled() {
    // FLAGS_REGS owns the eventual lazy-token ABI. W81 P1 assumes x26 is
    // current, so the two recipes must not run together. Reservation-only
    // FLAGS_REGS still wins: P1 must not materialize against a pinned home.
    return BackedgeLatchEnabled() && GetSvmConfig().backedge_flags &&
           !FlagsRegsEnabled();
}

}  // namespace swift::runtime
