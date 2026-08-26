#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <cstdint>

#include "runtime/backend/guarded_return_stack.h"
#include "runtime/backend/signal_handler.h"

namespace {

using swift::runtime::backend::GuardedReturnStack;
using swift::runtime::backend::SignalHandler;

bool RecoverReturnStack(void* context, ucontext_t* uctx, int sig, siginfo_t* info) {
    if ((sig != SIGSEGV && sig != SIGBUS) || !info) {
        return false;
    }
    return static_cast<GuardedReturnStack*>(context)->Recover(
            uctx, reinterpret_cast<std::uintptr_t>(info->si_addr));
}

class FaultRegistration {
public:
    explicit FaultRegistration(GuardedReturnStack& stack) : context(&stack) {
        SignalHandler::Install();
        SignalHandler::RegisterHandler(&RecoverReturnStack, context, -100);
    }

    ~FaultRegistration() {
        SignalHandler::UnregisterHandler(context);
    }

private:
    void* context;
};

#if defined(__aarch64__)
std::uintptr_t PushAcrossLowerGuard(void* bottom) {
    std::uintptr_t result;
    asm volatile(
            "mov x25, %1\n"
            "stp xzr, xzr, [x25, #-16]!\n"
            "mov %0, x25\n"
            : "=r"(result)
            : "r"(bottom)
            : "x25", "memory");
    return result;
}

std::uintptr_t PopAcrossUpperGuard(void* top) {
    std::uintptr_t result;
    asm volatile(
            "mov x25, %1\n"
            "ldp x9, x10, [x25], #16\n"
            "mov %0, x25\n"
            : "=r"(result)
            : "r"(top)
            : "x9", "x10", "x25", "memory");
    return result;
}
#endif

}  // namespace

TEST_CASE("guarded return stack recovers both overflow directions") {
    GuardedReturnStack stack;
    REQUIRE(reinterpret_cast<std::uintptr_t>(stack.Top()) -
                    reinterpret_cast<std::uintptr_t>(stack.Bottom()) ==
            GuardedReturnStack::kUsableSize);
    REQUIRE(stack.Bottom() < stack.Empty());
    REQUIRE(stack.Empty() < stack.Top());

#if defined(__aarch64__)
    FaultRegistration registration{stack};
    REQUIRE(PushAcrossLowerGuard(stack.Bottom()) ==
            reinterpret_cast<std::uintptr_t>(stack.Empty() - 1));
    REQUIRE(PopAcrossUpperGuard(stack.Top()) ==
            reinterpret_cast<std::uintptr_t>(stack.Empty() + 1));
#endif
}
