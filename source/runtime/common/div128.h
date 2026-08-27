#pragma once

#include "runtime/common/helper_abi.h"
#include "runtime/common/types.h"

namespace swift::runtime {

struct Div128Result {
    u64 quotient;
    u64 remainder;
};

SVM_HELPER_PRESERVE_ALL Div128Result DivideUnsigned128(u64 high, u64 low, u64 divisor);
SVM_HELPER_PRESERVE_ALL Div128Result DivideSigned128(u64 high, u64 low, u64 divisor);

}  // namespace swift::runtime
