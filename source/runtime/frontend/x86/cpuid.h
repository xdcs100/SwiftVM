#pragma once

#include "runtime/common/helper_abi.h"
#include "runtime/common/host_pair_result.h"

namespace swift::x86 {

enum CpuidFeature : u64 {
    CpuidAbiBaseline = 1ull << 0,
    CpuidAvx = 1ull << 1,
    CpuidCrypto = 1ull << 2,
    CpuidSha = 1ull << 3,
    CpuidSse4 = 1ull << 4,
    CpuidSse42 = 1ull << 5,
    CpuidFsgsbase = 1ull << 6,
    CpuidBmi = 1ull << 7,
    CpuidAdx = 1ull << 8,
    CpuidXsave = 1ull << 9,
};

SVM_HELPER_PRESERVE_ALL runtime::HostPairResult QueryCpuid(u64 leaf,
                                                          u64 subleaf,
                                                          u64 features);

}  // namespace swift::x86
