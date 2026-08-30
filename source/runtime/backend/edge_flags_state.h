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

class EdgeCarrySourceState final {
public:
    constexpr void Reset() {
        polarity = EdgeCarryPolarity::Unknown;
    }

    constexpr void PublishRuntimePolarity(bool inverted) {
        polarity = inverted ? EdgeCarryPolarity::Inverted
                            : EdgeCarryPolarity::Direct;
    }

    constexpr void InvalidateRuntimePolarity() {
        polarity = EdgeCarryPolarity::Unknown;
    }

    [[nodiscard]] constexpr EdgeCarryPolarity Resolve(
            u32 valid_nzcv_mask,
            bool canonical) const {
        if ((valid_nzcv_mask & kEdgeCarryMask) == 0) {
            return EdgeCarryPolarity::Unknown;
        }
        return canonical ? EdgeCarryPolarity::Direct : polarity;
    }

private:
    EdgeCarryPolarity polarity{EdgeCarryPolarity::Unknown};
};

struct EdgeFlagsState {
    u32 valid_nzcv_mask{};
    EdgeCarryPolarity carry_polarity{EdgeCarryPolarity::Unknown};
    EdgeFlagsProducer producer{EdgeFlagsProducer::Canonical};

    [[nodiscard]] static constexpr EdgeFlagsState Pending(u32 valid_nzcv_mask,
                                                          EdgeCarryPolarity carry_polarity,
                                                          EdgeFlagsProducer producer) {
        return {
                .valid_nzcv_mask = valid_nzcv_mask,
                .carry_polarity = carry_polarity,
                .producer = producer,
        };
    }

    [[nodiscard]] constexpr bool IsCanonical() const {
        return valid_nzcv_mask == 0 && carry_polarity == EdgeCarryPolarity::Unknown &&
               producer == EdgeFlagsProducer::Canonical;
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
    u8 observed_nzcv_mask{};
    bool commits_before_fault{};
    bool barrier_before_commit{};

    [[nodiscard]] constexpr bool IsWellFormed() const {
        return (overwrite_before_observe & ~kEdgeNZCVMask) == 0 &&
               (observed_nzcv_mask & ~0xFu) == 0;
    }

    [[nodiscard]] constexpr u32 ObservedHostMask() const {
        return static_cast<u32>(observed_nzcv_mask) << 28;
    }

    [[nodiscard]] constexpr bool CanPublishPendingEntry() const {
        return IsWellFormed() && overwrite_before_observe != 0 && commits_before_fault &&
               !barrier_before_commit;
    }

    [[nodiscard]] constexpr bool Accepts(const EdgeFlagsState& incoming) const {
        return CanPublishPendingEntry() && incoming.HasPendingPState() &&
               (incoming.valid_nzcv_mask & ~overwrite_before_observe) == 0 &&
               (incoming.valid_nzcv_mask & ObservedHostMask()) == 0;
    }

    bool operator==(const EdgeFlagsTargetContract&) const = default;
};

}  // namespace swift::runtime::backend
