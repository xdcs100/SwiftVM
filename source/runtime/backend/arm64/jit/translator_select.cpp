#include "translator.h"

#include <unordered_map>

#include "runtime/backend/context.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsImmediate(ir::Value value, u64 expected) {
    return value.Def() && value.Def()->GetOp() == ir::OpCode::LoadImm &&
           value.Def()->GetArg<ir::Imm>(0).Get() == expected;
}

bool IsNormalizedBoolean(ir::Value value, u32 depth = 0) {
    if (!value.Def() || depth == 4) {
        return false;
    }
    switch (value.Def()->GetOp()) {
        case ir::OpCode::LoadImm:
            return value.Def()->GetArg<ir::Imm>(0).Get() <= 1;
        case ir::OpCode::TestZero:
        case ir::OpCode::TestNotZero:
        case ir::OpCode::TestFlags:
        case ir::OpCode::TestNotFlags:
        case ir::OpCode::CondSet:
        case ir::OpCode::LocalCondSet:
        case ir::OpCode::LocalParitySet:
        case ir::OpCode::FCmpCondSet:
            return true;
        case ir::OpCode::And:
        case ir::OpCode::Or: {
            u32 inputs = 0;
            for (auto input : value.Def()->GetValues()) {
                ++inputs;
                if (!IsNormalizedBoolean(input, depth + 1)) {
                    return false;
                }
            }
            return inputs == 2;
        }
        default:
            return false;
    }
}

bool IsBooleanIdentitySelect(ir::Inst* inst) {
    return inst->GetOp() == ir::OpCode::Select && inst->ReturnType() == ir::ValueType::U8 &&
           IsNormalizedBoolean(inst->GetArg<ir::Value>(0)) &&
           IsImmediate(inst->GetArg<ir::Value>(1), 1) && IsImmediate(inst->GetArg<ir::Value>(2), 0);
}

}  // namespace

void JitTranslator::PrepareBooleanSelects(ir::Block* block) {
    normalized_bool_selects.clear();
    direct_cond_selects.clear();
    std::unordered_map<ir::Inst*, u32> eligible_constant_uses;
    for (auto& inst : block->GetInstList()) {
        if (!IsBooleanIdentitySelect(&inst)) {
            continue;
        }
        normalized_bool_selects.insert(&inst);
        ++eligible_constant_uses[inst.GetArg<ir::Value>(1).Def()];
        ++eligible_constant_uses[inst.GetArg<ir::Value>(2).Def()];
        auto* condition = inst.GetArg<ir::Value>(0).Def();
        if (condition->GetOp() == ir::OpCode::CondSet && condition->GetUses() == 1) {
            direct_cond_selects.emplace(&inst, condition->GetArg<ir::Cond>(0));
            disable_instructions.set(condition->Id());
        }
    }
    for (const auto& [constant, uses] : eligible_constant_uses) {
        if (constant->GetUses() == uses) {
            disable_instructions.set(constant->Id());
        }
    }
}

#define __ masm.

void JitTranslator::EmitSelect(ir::Inst* inst) {
    auto cond = inst->GetArg<ir::Value>(0);
    auto true_value = inst->GetArg<ir::Value>(1);
    auto false_value = inst->GetArg<ir::Value>(2);
    auto result = context.R(ir::Value{inst});
    if (auto direct = direct_cond_selects.find(inst); direct != direct_cond_selects.end()) {
        if (save_in_nzcv && nzcv_dirty) {
            __ Cset(result.W(), MapCond(direct->second));
            MergeNZCV();
        } else if (!TryEmitCondSetFromFlags(inst, direct->second)) {
            LoadNZCVFromFlags();
            __ Cset(result.W(), MapCond(direct->second));
        }
        return;
    }
    auto local = LocalConditionFor(cond);
    if (normalized_bool_selects.contains(inst)) {
        if (local) {
            __ Cset(result.W(), *local);
        } else {
            MergeNZCV();
            __ Cmp(context.W(cond), 0);
            __ Cset(result.W(), ne);
        }
        return;
    }
    if (local) {
        __ Csel(result, context.R(true_value), context.R(false_value), *local);
        return;
    }
    MergeNZCV();
    __ Cmp(context.W(cond), 0);
    __ Csel(result, context.R(true_value), context.R(false_value), ne);
}

#undef __

}  // namespace swift::runtime::backend::arm64
