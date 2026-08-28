#pragma once

#include "runtime/common/helper_abi.h"
#include "runtime/common/types.h"

namespace swift::runtime {

template <auto Function, typename... Args>
SVM_HELPER_GENERAL_REGS_ONLY __attribute__((noinline))
auto CallPreservingV16V23(Args... args) -> decltype(Function(args...)) {
#if defined(__aarch64__)
    alignas(16) u64 saved[16];
    asm volatile(
            "stp q16, q17, [%0, #0]\n"
            "stp q18, q19, [%0, #32]\n"
            "stp q20, q21, [%0, #64]\n"
            "stp q22, q23, [%0, #96]\n"
            :
            : "r"(saved)
            : "memory");
    auto result = Function(args...);
    asm volatile(
            "ldp q16, q17, [%0, #0]\n"
            "ldp q18, q19, [%0, #32]\n"
            "ldp q20, q21, [%0, #64]\n"
            "ldp q22, q23, [%0, #96]\n"
            :
            : "r"(saved)
            : "memory");
    return result;
#else
    return Function(args...);
#endif
}

}  // namespace swift::runtime
