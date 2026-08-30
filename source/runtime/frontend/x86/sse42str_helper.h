#pragma once

#include "runtime/common/types.h"

namespace swift::x86 {

struct Sse42StrVectorCallABI {
    static constexpr u32 ResultGPR = 16;
    static constexpr u32 NativeGPRClobbers =
            (1u << 10) | (1u << 11) | (1u << 13) | (1u << 14) |
            (1u << 15) | (1u << 16) | (1u << 17);
    static constexpr u32 EqualAnyGPRClobbers =
            (1u << 13) | (1u << 14) | (1u << 15) |
            (1u << 16) | (1u << 17);
    static constexpr u32 GenericGPRClobbers = (1u << 19) - 1u;
    static constexpr u32 GPRClobbers(u8 imm) {
        return imm == 0x02 ? EqualAnyGPRClobbers
                           : (imm == 0x1a ? NativeGPRClobbers
                                         : GenericGPRClobbers);
    }
    static constexpr u32 NativeFPRClobbers = 0xfcu;
    static constexpr u32 EqualAnyFPRClobbers = 0xf8u;
    static constexpr u32 GenericFPRClobbers = 0xffu;
    static constexpr u32 ArgumentFPRClobbers = 0x3u;
    static constexpr u32 FPRClobbers(u8 imm) {
        return imm == 0x02 ? EqualAnyFPRClobbers
                           : (imm == 0x1a ? NativeFPRClobbers
                                         : GenericFPRClobbers);
    }
};

VAddr Sse42StrVectorHelperAddress(u8 imm);

}  // namespace swift::x86
