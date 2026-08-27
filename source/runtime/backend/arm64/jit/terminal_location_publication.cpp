#include "terminal_location_publication.h"

#include <algorithm>
#include <vector>

#include "runtime/backend/arm64/defines.h"
#include "runtime/backend/context.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool SupportsDeferredLocation(const ir::Terminal& terminal) {
    return VisitVariant<bool>(terminal, [](auto value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, ir::terminal::Invalid> ||
               std::is_same_v<T, ir::terminal::ReturnToDispatch> ||
               std::is_same_v<T, ir::terminal::PopRSBHint>;
    });
}

ir::Inst* FindPendingLocation(ir::Block* block) {
    ir::Inst* pending{};
    for (auto& inst : block->GetInstList()) {
        if (inst.GetOp() == ir::OpCode::SetLocation) {
            pending = &inst;
        } else if (inst.GetOp() != ir::OpCode::PopRSB &&
                   inst.GetOp() != ir::OpCode::CallReturn) {
            pending = nullptr;
        }
    }
    return pending;
}

}  // namespace

void TerminalLocationPublication::Prepare(std::span<ir::Block* const> blocks,
                                          JitContext& context) {
    Reset();
    if (!context.GetFeatures().indirect_l1) {
        return;
    }

    struct Candidate {
        const ir::Inst* inst;
        u32 reg;
        bool required;
    };
    std::vector<Candidate> candidates;
    std::array<u32, 32> counts{};
    for (auto* block : blocks) {
        if (!SupportsDeferredLocation(block->GetTerminal())) {
            continue;
        }
        auto* location_inst = FindPendingLocation(block);
        if (!location_inst) {
            continue;
        }
        const auto location = location_inst->GetArg<ir::Lambda>(0);
        if (!location.IsValue()) {
            continue;
        }
        const u32 reg = context.X(location.GetValue()).GetCode();
        const bool required = std::any_of(
                block->GetInstList().begin(), block->GetInstList().end(),
                [](const ir::Inst& inst) {
                    return inst.GetOp() == ir::OpCode::CallReturn;
                });
        candidates.push_back({location_inst, reg, required});
        ++counts[reg];
    }

    for (const auto& candidate : candidates) {
        if (counts[candidate.reg] < 2 && !candidate.required) {
            continue;
        }
        deferred.insert(candidate.inst);
        if (!miss_labels[candidate.reg]) {
            miss_labels[candidate.reg] = std::make_unique<Label>();
        }
    }
}

void TerminalLocationPublication::Reset() {
    deferred.clear();
    for (auto& label : miss_labels) {
        label.reset();
    }
}

bool TerminalLocationPublication::Defers(const ir::Inst* inst) const {
    return deferred.contains(inst);
}

Label* TerminalLocationPublication::MissLabel(const XRegister& target) const {
    return miss_labels[target.GetCode()].get();
}

TerminalLocationPublication::RecoveryOffsets
TerminalLocationPublication::EmitColdPaths(JitContext& context) {
    auto& masm = context.GetMasm();
    RecoveryOffsets offsets{};
    for (u32 reg = 0; reg < miss_labels.size(); ++reg) {
        if (!miss_labels[reg]) {
            continue;
        }
        offsets[reg] = masm.GetBuffer()->GetSizeInBytes();
        masm.Bind(miss_labels[reg].get());
        masm.Str(XRegister(reg), MemOperand(state, state_offset_current_loc));
        context.ReturnHost();
    }
    Reset();
    return offsets;
}

}  // namespace swift::runtime::backend::arm64
