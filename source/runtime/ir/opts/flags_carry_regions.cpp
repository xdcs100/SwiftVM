#include "flags_carry_regions.h"

#include "runtime/ir/block.h"

namespace swift::runtime::ir {

std::vector<bool> FlagsCarryRegions::Classify(Block* block) {
    struct Region {
        bool consumes_carry{};
        bool writes_carry{};
    };

    std::vector<Region> regions(1);
    for (auto& inst : block->GetInstList()) {
        regions.back().consumes_carry |= inst.GetOp() == OpCode::Adc || inst.GetOp() == OpCode::Sbb;
        switch (inst.GetOp()) {
            case OpCode::SaveFlags:
            case OpCode::BranchOnlyFlags:
                regions.back().writes_carry |= True(inst.GetArg<Flags>(1) & Flags::Carry);
                break;
            case OpCode::ClearFlags:
                regions.back().writes_carry |= True(inst.GetArg<Flags>(0) & Flags::Carry);
                break;
            case OpCode::SetCarry:
            case OpCode::PublishFCmpFlags:
                regions.back().writes_carry = true;
                break;
            case OpCode::PublishSse42StrFlags:
                regions.back().writes_carry |= True(inst.GetArg<Flags>(1) & Flags::Carry);
                break;
            default:
                break;
        }
        if (inst.GetOp() == OpCode::AdvancePC) {
            regions.emplace_back();
        }
    }

    std::vector<bool> protected_regions(regions.size());
    for (size_t i = 0; i < regions.size(); ++i) {
        if (!regions[i].consumes_carry) {
            continue;
        }
        protected_regions[i] = true;
        for (size_t producer = i; producer != 0;) {
            --producer;
            protected_regions[producer] = true;
            if (regions[producer].writes_carry) {
                break;
            }
        }
    }
    return protected_regions;
}

}  // namespace swift::runtime::ir
