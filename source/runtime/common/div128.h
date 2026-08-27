#pragma once

#include "runtime/common/host_pair_result.h"
#include "runtime/common/helper_abi.h"

namespace swift::runtime {

SVM_HELPER_PRESERVE_ALL HostPairResult DivideUnsigned128(u64 high, u64 low, u64 divisor);
SVM_HELPER_PRESERVE_ALL HostPairResult DivideSigned128(u64 high, u64 low, u64 divisor);

}  // namespace swift::runtime
