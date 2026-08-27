#include "div128.h"

namespace swift::runtime {

SVM_HELPER_PRESERVE_ALL Div128Result DivideUnsigned128(u64 high, u64 low, u64 divisor) {
    if (!divisor) {
        return {};
    }
    const auto dividend = (static_cast<unsigned __int128>(high) << 64) | low;
    return {static_cast<u64>(dividend / divisor), static_cast<u64>(dividend % divisor)};
}

SVM_HELPER_PRESERVE_ALL Div128Result DivideSigned128(u64 high, u64 low, u64 divisor) {
    const auto signed_divisor = static_cast<s64>(divisor);
    if (!signed_divisor) {
        return {};
    }
    if (signed_divisor == -1 && high == (u64{1} << 63) && low == 0) {
        return {};
    }
    const auto dividend =
            static_cast<__int128>((static_cast<unsigned __int128>(high) << 64) | low);
    return {static_cast<u64>(dividend / signed_divisor),
            static_cast<u64>(dividend % signed_divisor)};
}

}  // namespace swift::runtime
