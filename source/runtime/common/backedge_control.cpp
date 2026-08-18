#include "backedge_control.h"

#include "runtime/common/svm_config.h"

namespace swift::runtime {

bool BackedgeLatchEnabled() {
    // FLAGS_REGS token ABI needs the W81 P0 veneer so Signal/SMC can
    // observe last_result. Do not flip the latch env default; this is
    // implied only while the token switch is on.
    return GetSvmConfig().backedge_latch || FlagsRegsEnabled();
}

bool BackedgeFlagsEnabled() {
    // FLAGS_REGS owns the eventual lazy-token ABI. W81 P1 assumes x26 is
    // current, so the two recipes must not run together. Reservation-only
    // FLAGS_REGS still wins: P1 must not materialize against a pinned home.
    return BackedgeLatchEnabled() && GetSvmConfig().backedge_flags &&
           !FlagsRegsEnabled();
}

}  // namespace swift::runtime
