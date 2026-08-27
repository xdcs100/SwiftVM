#pragma once

#include <array>
#include <memory>
#include <span>
#include <unordered_set>

#include "jit_context.h"
#include "runtime/ir/block.h"

namespace swift::runtime::backend::arm64 {

class TerminalLocationPublication {
public:
    using RecoveryOffsets = std::array<u32, 32>;

    void Prepare(std::span<ir::Block* const> blocks, JitContext& context);
    void Reset();

    [[nodiscard]] bool Defers(const ir::Inst* inst) const;
    [[nodiscard]] Label* MissLabel(const XRegister& target) const;
    [[nodiscard]] RecoveryOffsets EmitColdPaths(MacroAssembler& masm);

private:
    std::unordered_set<const ir::Inst*> deferred;
    std::array<std::unique_ptr<Label>, 32> miss_labels{};
};

}  // namespace swift::runtime::backend::arm64
