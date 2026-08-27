#include "runtime/backend/interrupt_poll_state.h"

#include <cstdlib>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace swift::runtime::backend {

namespace {

std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

InterruptPollState::InterruptPollState(std::size_t payload_size) {
    const long host_page_size = sysconf(_SC_PAGESIZE);
    if (host_page_size <= 0 || payload_size == 0) {
        throw std::bad_alloc{};
    }
    page_size = static_cast<std::size_t>(host_page_size);
    if ((page_size & (page_size - 1)) != 0) {
        throw std::bad_alloc{};
    }
    mapping_size = page_size + AlignUp(payload_size, page_size);
    mapping = static_cast<u8*>(mmap(nullptr,
                                    mapping_size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANON,
                                    -1,
                                    0));
    if (mapping == MAP_FAILED) {
        mapping = nullptr;
        throw std::bad_alloc{};
    }
    payload = mapping + page_size;
    if (mprotect(mapping, page_size, PROT_READ) != 0) {
        munmap(mapping, mapping_size);
        mapping = nullptr;
        payload = nullptr;
        throw std::bad_alloc{};
    }
}

InterruptPollState::~InterruptPollState() {
    if (mapping) {
        munmap(mapping, mapping_size);
    }
}

void* InterruptPollState::PollAddress() const {
    return payload - sizeof(u64);
}

bool InterruptPollState::Contains(std::uintptr_t address) const {
    const auto begin = reinterpret_cast<std::uintptr_t>(mapping);
    return address >= begin && address - begin < page_size;
}

void InterruptPollState::Lock() const {
    while (transition_lock.test_and_set(std::memory_order_acquire)) {
    }
}

void InterruptPollState::Unlock() const {
    transition_lock.clear(std::memory_order_release);
}

void InterruptPollState::Protect(int protection) const {
    if (mprotect(mapping, page_size, protection) != 0) {
        std::abort();
    }
}

void InterruptPollState::Arm() const {
    Lock();
    if (!armed) {
        Protect(PROT_NONE);
        armed = true;
    }
    Unlock();
}

void InterruptPollState::DisarmIfIdle(u64* exit_request) const {
    Lock();
    const auto request = std::atomic_ref<u64>(*exit_request)
                                 .load(std::memory_order_acquire);
    if (armed && request == 0) {
        Protect(PROT_READ);
        armed = false;
    }
    Unlock();
}

}  // namespace swift::runtime::backend
