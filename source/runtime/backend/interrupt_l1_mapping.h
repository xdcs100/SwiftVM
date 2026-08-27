#pragma once

#include <cstddef>
#include <cstdint>

namespace swift::runtime::backend {

class InterruptL1Mapping final {
public:
    explicit InterruptL1Mapping(std::size_t size);
    ~InterruptL1Mapping();

    InterruptL1Mapping(const InterruptL1Mapping&) = delete;
    InterruptL1Mapping& operator=(const InterruptL1Mapping&) = delete;

    [[nodiscard]] void* Data() const { return mapping; }
    [[nodiscard]] bool Contains(std::uintptr_t address) const;

private:
    void* mapping{};
    std::size_t size{};
};

}  // namespace swift::runtime::backend
