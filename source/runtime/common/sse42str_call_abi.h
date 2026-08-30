#pragma once

#include "runtime/common/types.h"

namespace swift::x86 {

struct Sse42StrVectorCallABI {
    static constexpr u32 ResultGPR = 16;
    static constexpr u32 EqualAnyGPRClobbers =
            (1u << 11) | (1u << 13) | (1u << 14) | (1u << 15) |
            (1u << 16) | (1u << 17);
    static constexpr u32 EqualEachGPRClobbers = EqualAnyGPRClobbers;
    static constexpr u32 EqualAnyFPRClobbers = 0xf8u;
    static constexpr u32 EqualEachFPRClobbers = 0xfcu;
    static constexpr u32 ArgumentFPRClobbers = 0x3u;

    [[nodiscard]] static constexpr bool Supports(u8 imm) {
        return imm == 0x02 || imm == 0x1a;
    }

    [[nodiscard]] static constexpr u32 GPRClobbers(u8 imm) {
        return imm == 0x02
                ? EqualAnyGPRClobbers
                : imm == 0x1a ? EqualEachGPRClobbers : 0;
    }

    [[nodiscard]] static constexpr u32 FPRClobbers(u8 imm) {
        return imm == 0x02
                ? EqualAnyFPRClobbers
                : imm == 0x1a ? EqualEachFPRClobbers : 0;
    }
};

}  // namespace swift::x86
