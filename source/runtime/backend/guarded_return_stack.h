#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/backend/context.h"
#include "runtime/backend/signal_handler.h"

namespace swift::runtime::backend {

class GuardedReturnStack : DeleteCopyAndMove {
public:
    static constexpr size_t kUsableSize = 4 * 1024 * 1024;

    GuardedReturnStack();
    ~GuardedReturnStack() override;

    [[nodiscard]] RSBFrame* Bottom() const;
    [[nodiscard]] RSBFrame* Empty() const;
    [[nodiscard]] RSBFrame* Top() const;
    [[nodiscard]] bool Reset(ucontext_t* uctx) const;
    [[nodiscard]] bool Recover(ucontext_t* uctx, std::uintptr_t fault_addr) const;

private:
    u8* mapping{};
    u8* usable_begin{};
    u8* usable_end{};
    size_t mapping_size{};
    size_t page_size{};
};

}  // namespace swift::runtime::backend
