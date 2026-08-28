#pragma once

#include "runtime/common/types.h"

namespace swift::x86 {

struct Sse42StrVectorCallABI {
    static constexpr u32 ResultGPR = 16;
    static constexpr u32 GPRClobbers =
            (1u << 10) | (1u << 11) | (1u << 13) | (1u << 14) |
            (1u << 15) | (1u << 16) | (1u << 17);
    static constexpr u32 FPRClobbers = 0xffu;
};

VAddr Sse42StrVectorHelperAddress();

}  // namespace swift::x86
