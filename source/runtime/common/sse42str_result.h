#pragma once

#include "runtime/common/types.h"

namespace swift::runtime::sse42str {

inline constexpr u32 kMaskShift = 0;
inline constexpr u32 kIndexShift = 16;
inline constexpr u32 kSignBit = 24;
inline constexpr u32 kZeroBit = 25;
inline constexpr u32 kCarryBit = 26;
inline constexpr u32 kOverflowBit = 27;

}  // namespace swift::runtime::sse42str
