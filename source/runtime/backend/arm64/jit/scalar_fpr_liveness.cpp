#include "scalar_fpr_liveness.h"

#include <functional>
#include <unordered_map>

namespace swift::runtime::backend::arm64 {

namespace {

bool IsScalarFPRBinary(ir::OpCode op) {
    using O = ir::OpCode;
    switch (op) {
        case O::VecFAddScalar32:
        case O::VecFSubScalar32:
        case O::VecFMulScalar32:
        case O::VecFDivScalar32:
        case O::VecFAddScalar64:
        case O::VecFSubScalar64:
        case O::VecFMulScalar64:
        case O::VecFDivScalar64:
            return true;
        default:
            return false;
    }
}

enum class UpperLiveness : u8 {
    Unknown,
    Visiting,
    Dead,
    Live,
};

}  // namespace

void ScalarFPRLiveness::Analyze(ir::Block* block) {
    upper_dead_values.clear();
    std::unordered_map<ir::Inst*, UpperLiveness> states;
    auto& list = block->GetInstList();

    std::function<bool(ir::Inst*)> upper_dead = [&](ir::Inst* value) {
        const auto state = states[value];
        if (state == UpperLiveness::Dead) {
            return true;
        }
        if (state == UpperLiveness::Live || state == UpperLiveness::Visiting) {
            return false;
        }
        states[value] = UpperLiveness::Visiting;

        u32 direct_uses = 0;
        bool dead = true;
        for (auto& use : list) {
            u32 references = 0;
            for (const auto input : use.GetValues()) {
                references += input.Def() == value;
            }
            if (references == 0) {
                continue;
            }
            direct_uses += references;

            if (use.IsBitCastOperation()) {
                dead = upper_dead(&use);
            } else if (IsScalarFPRBinary(use.GetOp())) {
                const bool is_left = use.GetArg<ir::Value>(0).Def() == value;
                dead = !is_left || upper_dead(&use);
            } else if (use.GetOp() == ir::OpCode::VecFCmp) {
                dead = true;
            } else if (use.GetOp() == ir::OpCode::VecFUnary &&
                       use.GetArg<ir::Imm>(4).Get() != 0) {
                const bool is_merge = use.GetArg<ir::Value>(1).Def() == value;
                dead = !is_merge || upper_dead(&use);
            } else {
                dead = false;
            }
            if (!dead) {
                break;
            }
        }
        if (direct_uses < value->GetUses(false)) {
            dead = false;
        }
        states[value] = dead ? UpperLiveness::Dead : UpperLiveness::Live;
        if (dead) {
            upper_dead_values.insert(value);
        }
        return dead;
    };

    for (auto& inst : list) {
        if (IsScalarFPRBinary(inst.GetOp())) {
            upper_dead(&inst);
        }
    }
}

bool ScalarFPRLiveness::UpperDead(ir::Inst* inst) const {
    return upper_dead_values.contains(inst);
}

}  // namespace swift::runtime::backend::arm64
