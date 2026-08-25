#pragma once

#include <array>
#include <optional>
#include <utility>

#include "runtime/common/types.h"

namespace swift::runtime::backend::arm64 {

inline constexpr std::array<u8, 12> kPshufdDirectControls{
        0xE4,
        0x00,
        0x55,
        0xAA,
        0xFF,
        0x39,
        0x93,
        0x50,
        0xFA,
        0xA0,
        0xF5,
        0x4E,
};

inline constexpr std::pair<u64, u64> PshufdIndexMask(u8 control) {
    u64 low{};
    u64 high{};
    for (u32 byte = 0; byte < 16; ++byte) {
        const u32 lane = byte / 4;
        const u8 index = u8(((control >> (lane * 2)) & 3) * 4 + (byte & 3));
        auto& half = byte < 8 ? low : high;
        half |= u64(index) << ((byte & 7) * 8);
    }
    return {low, high};
}

inline constexpr std::optional<u8> DecodePshufdDirectControl(u64 low, u64 high) {
    for (u8 control : kPshufdDirectControls) {
        if (PshufdIndexMask(control) == std::pair{low, high}) {
            return control;
        }
    }
    return std::nullopt;
}

}  // namespace swift::runtime::backend::arm64
