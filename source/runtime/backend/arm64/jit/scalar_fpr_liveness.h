#pragma once

#include <unordered_set>

#include "runtime/ir/block.h"

namespace swift::runtime::backend::arm64 {

class ScalarFPRLiveness {
public:
    void Analyze(ir::Block* block);
    [[nodiscard]] bool UpperDead(ir::Inst* inst) const;

private:
    std::unordered_set<ir::Inst*> upper_dead_values{};
};

}  // namespace swift::runtime::backend::arm64
