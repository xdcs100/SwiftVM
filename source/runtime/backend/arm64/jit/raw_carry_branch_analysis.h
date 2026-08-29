#pragma once

#include <optional>
#include <unordered_map>

#include "runtime/ir/block.h"

namespace swift::runtime::backend::arm64 {

class RawCarryBranchAnalysis final {
public:
    void Analyze(ir::Block* block);

    [[nodiscard]] bool SuppressesInvert(const ir::Inst* inst) const;
    [[nodiscard]] std::optional<ir::Cond> ConditionForTest(
            const ir::Inst* inst) const;
    [[nodiscard]] ir::Inst* InvertForTest(const ir::Inst* inst) const;

private:
    std::unordered_map<const ir::Inst*, ir::Inst*> test_inverts;
    std::unordered_map<const ir::Inst*, ir::Cond> test_conditions;
};

}  // namespace swift::runtime::backend::arm64
