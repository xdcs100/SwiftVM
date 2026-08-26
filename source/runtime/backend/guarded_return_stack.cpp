#include "runtime/backend/guarded_return_stack.h"

#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace swift::runtime::backend {

GuardedReturnStack::GuardedReturnStack() {
    const long host_page_size = sysconf(_SC_PAGESIZE);
    if (host_page_size <= 0 || kUsableSize % static_cast<size_t>(host_page_size) != 0) {
        throw std::bad_alloc{};
    }
    page_size = static_cast<size_t>(host_page_size);
    mapping_size = kUsableSize + 2 * page_size;
    auto* allocated = static_cast<u8*>(mmap(nullptr,
                                            mapping_size,
                                            PROT_NONE,
                                            MAP_PRIVATE | MAP_ANON,
                                            -1,
                                            0));
    if (allocated == MAP_FAILED) {
        throw std::bad_alloc{};
    }
    mapping = allocated;
    usable_begin = mapping + page_size;
    usable_end = usable_begin + kUsableSize;
    if (mprotect(usable_begin, kUsableSize, PROT_READ | PROT_WRITE) != 0) {
        munmap(mapping, mapping_size);
        mapping = nullptr;
        throw std::bad_alloc{};
    }
}

GuardedReturnStack::~GuardedReturnStack() {
    if (mapping) {
        munmap(mapping, mapping_size);
    }
}

RSBFrame* GuardedReturnStack::Bottom() const {
    return reinterpret_cast<RSBFrame*>(usable_begin);
}

RSBFrame* GuardedReturnStack::Empty() const {
    return reinterpret_cast<RSBFrame*>(usable_begin + kUsableSize / 2);
}

RSBFrame* GuardedReturnStack::Top() const {
    return reinterpret_cast<RSBFrame*>(usable_end);
}

bool GuardedReturnStack::Recover(ucontext_t* uctx, std::uintptr_t fault_addr) const {
    const auto mapping_addr = reinterpret_cast<std::uintptr_t>(mapping);
    const auto bottom_addr = reinterpret_cast<std::uintptr_t>(usable_begin);
    const auto top_addr = reinterpret_cast<std::uintptr_t>(usable_end);
    const bool lower_guard = fault_addr >= mapping_addr && fault_addr < bottom_addr;
    const bool upper_guard = fault_addr >= top_addr &&
                             fault_addr < top_addr + page_size;
    if (!lower_guard && !upper_guard) {
        return false;
    }

    const auto pointer = SignalHandler::GetContextGPR(uctx, 25);
    constexpr auto frame_size = sizeof(RSBFrame);
    const bool lower_access = lower_guard && pointer >= bottom_addr - frame_size &&
                              pointer <= bottom_addr + frame_size;
    const bool upper_access = upper_guard && pointer >= top_addr - frame_size &&
                              pointer <= top_addr + frame_size;
    if (!lower_access && !upper_access) {
        return false;
    }
    return SignalHandler::SetContextGPR(
            uctx, 25, reinterpret_cast<std::uintptr_t>(Empty()));
}

}  // namespace swift::runtime::backend
