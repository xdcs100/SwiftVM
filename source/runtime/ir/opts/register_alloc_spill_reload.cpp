#include "register_alloc_spill_reload.h"

#include <map>

#include "register_alloc_internal.h"

namespace swift::runtime::ir {

namespace {

bool IsLocalControlFlow(OpCode op) {
    return op == OpCode::BindLabel || op == OpCode::Goto || op == OpCode::NotGoto;
}

u32 ScratchOnlyGPRs(Inst& inst, const backend::RegAlloc& reg_alloc, const FeatureSet& features) {
    u32 count =
            backend::X86PinExtLevel3AluScratchEnabled(reg_alloc.GetGprs(), inst.GetOp()) ? 1u : 0u;
    if (!backend::X86PinExtScratchOnlyEnabled(reg_alloc.GetGprs(), features)) {
        return count;
    }
    const u32 fixed = backend::FixedGPRClobbers(inst, features, true);
    const bool last_result_pinned = GetSvmConfig().flags_regs;
    return count + ((fixed & (1u << 12)) || last_result_pinned ? 0u : 1u) +
           ((fixed & (1u << 13)) ? 0u : 1u);
}

u32 SpillReloadDemand(Inst& inst, backend::RegAlloc& reg_alloc, u32 excluded_value) {
    StackVector<u32, 8> counted{};
    auto count = [&](Value value) {
        if (!value.Defined() || reg_alloc.ValueType(value) != backend::RegAlloc::MEM) {
            return;
        }
        const u32 value_id = reg_alloc.AllocationId(value);
        auto source = ResolveBitCastSource(value);
        if (!source.Defined() || IsFloatValueType(source.Type()) || value_id == excluded_value ||
            reg_alloc.HasSpillReload(value_id, inst.Id()) ||
            std::find(counted.begin(), counted.end(), value_id) != counted.end()) {
            return;
        }
        counted.push_back(value_id);
    };
    for (auto value : inst.GetValues()) {
        count(value);
    }
    if (inst.HasValue()) {
        count(Value{&inst});
    }
    return static_cast<u32>(counted.size());
}

bool HasHeadroom(Inst& inst,
                 backend::RegAlloc& reg_alloc,
                 const FeatureSet& features,
                 u32 excluded_value) {
    const auto need = backend::ScratchBudget(inst, features);
    const u32 reloads = SpillReloadDemand(inst, reg_alloc, excluded_value);
    u32 required = need.gpr + reloads;
    const u32 scratch_only = ScratchOnlyGPRs(inst, reg_alloc, features);
    if (!backend::ScratchPreciseRequested(features) && scratch_only &&
        (inst.GetOp() == OpCode::Add || inst.GetOp() == OpCode::Sub)) {
        required = std::max<u32>(need.gpr, backend::kDefaultScratchGPR + reloads);
    }
    const u32 available = reg_alloc.DirtyGPR(inst.Id()).GetClearCount() + scratch_only;
    return available > required;
}

std::optional<u16> FindRegionRegister(const Vector<Inst*>& instructions,
                                      size_t first,
                                      size_t last,
                                      u32 value_id,
                                      backend::RegAlloc* reg_alloc,
                                      const FeatureSet& features) {
    for (u16 reg = 0; reg < 31; ++reg) {
        if (reg_alloc->GetGprs().Get(reg)) {
            continue;
        }
        bool available = true;
        for (size_t i = first; i <= last; ++i) {
            auto* inst = instructions[i];
            if (reg_alloc->DirtyGPR(inst->Id()).Get(reg) ||
                !HasHeadroom(*inst, *reg_alloc, features, value_id)) {
                available = false;
                break;
            }
        }
        if (available) {
            return reg;
        }
    }
    return std::nullopt;
}

void PlanSegment(const Vector<Inst*>& instructions,
                 backend::RegAlloc* reg_alloc,
                 const FeatureSet& features) {
    std::map<u32, Vector<size_t>> uses;
    for (size_t i = 0; i < instructions.size(); ++i) {
        auto* inst = instructions[i];
        if (inst->IsPseudoOperation()) {
            continue;
        }
        StackVector<u32, 8> counted{};
        for (auto value : inst->GetValues()) {
            if (!value.Defined() || reg_alloc->ValueType(value) != backend::RegAlloc::MEM) {
                continue;
            }
            auto source = ResolveBitCastSource(value);
            if (!source.Defined() || IsFloatValueType(source.Type())) {
                continue;
            }
            const u32 value_id = reg_alloc->AllocationId(source);
            if (std::find(counted.begin(), counted.end(), value_id) != counted.end()) {
                continue;
            }
            counted.push_back(value_id);
            uses[value_id].push_back(i);
        }
    }

    for (auto& [value_id, positions] : uses) {
        size_t begin = 0;
        while (begin + 1 < positions.size()) {
            size_t best = begin;
            std::optional<u16> best_reg;
            for (size_t end = begin + 1; end < positions.size(); ++end) {
                auto reg = FindRegionRegister(instructions,
                                              positions[begin],
                                              positions[end],
                                              value_id,
                                              reg_alloc,
                                              features);
                if (!reg) {
                    break;
                }
                best = end;
                best_reg = reg;
            }
            if (!best_reg) {
                ++begin;
                continue;
            }
            reg_alloc->MapSpillReload(value_id,
                                      instructions[positions[begin]]->Id(),
                                      instructions[positions[best]]->Id(),
                                      HostGPR{*best_reg});
            begin = best + 1;
        }
    }
}

void PlanBlock(Block* block, backend::RegAlloc* reg_alloc, const FeatureSet& features) {
    Vector<Inst*> segment;
    auto flush = [&] {
        if (!segment.empty()) {
            PlanSegment(segment, reg_alloc, features);
            segment.clear();
        }
    };
    for (auto& inst : block->GetInstList()) {
        if (IsLocalControlFlow(inst.GetOp())) {
            flush();
            continue;
        }
        segment.push_back(&inst);
    }
    flush();
}

}  // namespace

void PlanSpillReloadRegions(HIRFunction* function,
                            backend::RegAlloc* reg_alloc,
                            const FeatureSet& features) {
    for (auto* hir_block : function->GetHIRBlocks()) {
        PlanBlock(hir_block->GetBlock(), reg_alloc, features);
    }
}

void PlanSpillReloadRegions(Block* block,
                            backend::RegAlloc* reg_alloc,
                            const FeatureSet& features) {
    PlanBlock(block, reg_alloc, features);
}

}  // namespace swift::runtime::ir
