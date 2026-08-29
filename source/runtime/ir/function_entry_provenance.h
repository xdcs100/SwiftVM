#pragma once

#include <vector>

#include "runtime/ir/location.h"

namespace swift::runtime::ir {

enum class FunctionEntryDisposition : u8 {
    Candidate,
    Accepted,
    Rejected,
};

enum class FunctionEntryRejection : u8 {
    None,
    NoOwner,
    AmbiguousOwner,
    CallReturnOwnership,
    OwnerResetFailed,
    BoundaryMismatch,
};

struct FunctionEntrySourceRange {
    Location start{};
    Location end{};

    bool operator==(const FunctionEntrySourceRange&) const = default;
};

struct FunctionEntryProvenance {
    Location target{};
    Location owner_start{};
    Location owner_end{};
    std::vector<FunctionEntrySourceRange> dependencies{};
    FunctionEntryDisposition disposition{FunctionEntryDisposition::Candidate};
    FunctionEntryRejection rejection{FunctionEntryRejection::None};
    bool call_return_owned{};

    [[nodiscard]] bool IsAccepted() const {
        return disposition == FunctionEntryDisposition::Accepted;
    }
};

}  // namespace swift::runtime::ir
