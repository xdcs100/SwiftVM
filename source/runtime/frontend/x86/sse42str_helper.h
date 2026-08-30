#pragma once

#include "runtime/common/sse42str_call_abi.h"

namespace swift::x86 {

VAddr Sse42StrVectorHelperAddress(u8 imm);

}  // namespace swift::x86
