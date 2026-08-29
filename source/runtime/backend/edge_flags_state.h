#pragma once

#include "runtime/common/types.h"

namespace swift::runtime::backend {

inline constexpr u32 kEdgeNZCVMask = 0xF000'0000u;
inline constexpr u32 kEdgeCarryMask = 0x2000'0000u;

enum class EdgeCarryPolarity : u8 {
    Direct,
    Inverted,
    Unknown,
    Count,
};

enum class EdgeFlagsProducer : u8 {
    Arithmetic,
    Logical,
    Restore,
    Canonical,
    Count,
};

struct EdgeFlagsState {
    u32 valid_nzcv_mask{};
    EdgeCarryPolarity carry_polarity{EdgeCarryPolarity::Unknown};
    EdgeFlagsProducer producer{EdgeFlagsProducer::Canonical};
    u64 packed_flags_version{};

    [[nodiscard]] static constexpr EdgeFlagsState Pending(u32 valid_nzcv_mask,
                                                          EdgeCarryPolarity carry_polarity,
                                                          EdgeFlagsProducer producer,
                                                          u64 packed_flags_version = 0) {
        return {
                .valid_nzcv_mask = valid_nzcv_mask,
                .carry_polarity = carry_polarity,
                .producer = producer,
                .packed_flags_version = packed_flags_version,
        };
    }

    [[nodiscard]] constexpr bool IsCanonical() const {
        return valid_nzcv_mask == 0 && carry_polarity == EdgeCarryPolarity::Unknown &&
               producer == EdgeFlagsProducer::Canonical && packed_flags_version == 0;
    }

    [[nodiscard]] constexpr bool IsWellFormed() const {
        if ((valid_nzcv_mask & ~kEdgeNZCVMask) != 0 || carry_polarity >= EdgeCarryPolarity::Count ||
            producer >= EdgeFlagsProducer::Count) {
            return false;
        }
        if (valid_nzcv_mask == 0) {
            return IsCanonical();
        }
        return producer != EdgeFlagsProducer::Canonical &&
               ((valid_nzcv_mask & kEdgeCarryMask) != 0 ||
                carry_polarity == EdgeCarryPolarity::Unknown);
    }

    [[nodiscard]] constexpr bool HasPendingPState() const {
        return valid_nzcv_mask != 0 && IsWellFormed();
    }

    bool operator==(const EdgeFlagsState&) const = default;
};

struct EdgeFlagsTargetContract {
    u32 overwrite_before_observe{};
    bool commits_before_fault{};
    bool observes_before_commit{};

    [[nodiscard]] constexpr bool IsWellFormed() const {
        return (overwrite_before_observe & ~kEdgeNZCVMask) == 0;
    }

    [[nodiscard]] constexpr bool CanPublishPendingEntry() const {
        return IsWellFormed() && overwrite_before_observe != 0 && commits_before_fault &&
               !observes_before_commit;
    }

    [[nodiscard]] constexpr bool Accepts(const EdgeFlagsState& incoming) const {
        return CanPublishPendingEntry() && incoming.HasPendingPState() &&
               (incoming.valid_nzcv_mask & ~overwrite_before_observe) == 0;
    }

    bool operator==(const EdgeFlagsTargetContract&) const = default;
};

}  // namespace swift::runtime::backend
