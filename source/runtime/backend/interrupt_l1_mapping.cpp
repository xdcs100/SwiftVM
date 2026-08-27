#include "runtime/backend/interrupt_l1_mapping.h"

#include <new>
#include <sys/mman.h>

namespace swift::runtime::backend {

InterruptL1Mapping::InterruptL1Mapping(std::size_t requested_size) : size(requested_size) {
    if (size == 0 || (size & (size - 1)) != 0) {
        throw std::bad_alloc{};
    }

    auto* allocation = static_cast<std::byte*>(mmap(nullptr,
                                                     size * 2,
                                                     PROT_NONE,
                                                     MAP_PRIVATE | MAP_ANON,
                                                     -1,
                                                     0));
    if (allocation == MAP_FAILED) {
        throw std::bad_alloc{};
    }

    const auto address = reinterpret_cast<std::uintptr_t>(allocation);
    const auto aligned = (address + size - 1) & ~(size - 1);
    const auto prefix = aligned - address;
    const auto suffix = size - prefix;
    if (prefix != 0) {
        munmap(allocation, prefix);
    }
    if (suffix != 0) {
        munmap(reinterpret_cast<void*>(aligned + size), suffix);
    }
    mapping = reinterpret_cast<void*>(aligned);
}

InterruptL1Mapping::~InterruptL1Mapping() {
    if (mapping) {
        munmap(mapping, size);
    }
}

bool InterruptL1Mapping::Contains(std::uintptr_t address) const {
    const auto begin = reinterpret_cast<std::uintptr_t>(mapping);
    return address >= begin && address - begin < size;
}

}  // namespace swift::runtime::backend
