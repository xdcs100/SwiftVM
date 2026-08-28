#pragma once

#include <vector>

namespace swift::runtime::ir {

class Block;

class FlagsCarryRegions {
public:
    [[nodiscard]] static std::vector<bool> Classify(Block* block);
};

}  // namespace swift::runtime::ir
