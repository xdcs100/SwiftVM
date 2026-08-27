#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "runtime/common/types.h"

namespace swift::runtime::backend {

class InterruptPollState final {
public:
    explicit InterruptPollState(std::size_t payload_size);
    ~InterruptPollState();

    InterruptPollState(const InterruptPollState&) = delete;
    InterruptPollState& operator=(const InterruptPollState&) = delete;

    [[nodiscard]] u8* Data() const { return payload; }
    [[nodiscard]] void* PollAddress() const;
    [[nodiscard]] bool Contains(std::uintptr_t address) const;

    void Arm() const;
    void DisarmIfIdle(u64* exit_request) const;

private:
    void Lock() const;
    void Unlock() const;
    void Protect(int protection) const;

    u8* mapping{};
    u8* payload{};
    std::size_t mapping_size{};
    std::size_t page_size{};
    mutable std::atomic_flag transition_lock = ATOMIC_FLAG_INIT;
    mutable bool armed{};
};

}  // namespace swift::runtime::backend
