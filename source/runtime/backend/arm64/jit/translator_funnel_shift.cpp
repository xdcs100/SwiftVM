#include "translator.h"

#include <iterator>

namespace swift::runtime::backend::arm64 {

#define __ masm.

namespace {

bool IsPlainImmediate(const ir::Operand& operand, u64 immediate) {
    return operand.GetRight().Null() && operand.GetLeft().IsImm() &&
           operand.GetLeft().imm.Get() == immediate;
}

bool SavesParity(ir::Inst* inst) {
    for (auto* pseudo : inst->GetPseudoOperations(ir::OpCode::SaveFlags)) {
        if (True(pseudo->GetArg<ir::Flags>(1) & ir::Flags::Parity)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<JitTranslator::FunnelShiftPlan>
JitTranslator::MatchFunnelShift(ir::Inst* result) const {
    if (!result || result->GetOp() != ir::OpCode::Or ||
        local_conditions.contains(result)) {
        return std::nullopt;
    }
    const auto right = result->GetArg<ir::Operand>(1);
    if (!right.GetRight().Null() || !right.GetLeft().IsValue()) {
        return std::nullopt;
    }
    auto* left_shift = result->GetArg<ir::Value>(0).Def();
    auto* right_shift = right.GetLeft().value.Def();
    if (!left_shift || !right_shift || left_shift->GetUses() != 1 ||
        right_shift->GetUses() != 1 || left_shift->GetUses(false) != 1 ||
        right_shift->GetUses(false) != 1 || local_conditions.contains(left_shift) ||
        local_conditions.contains(right_shift)) {
        return std::nullopt;
    }

    ir::Inst* lsr{};
    ir::Inst* lsl{};
    for (auto* shift : {left_shift, right_shift}) {
        if (shift->GetOp() == ir::OpCode::LsrImm) {
            lsr = shift;
        } else if (shift->GetOp() == ir::OpCode::LslImm) {
            lsl = shift;
        } else {
            return std::nullopt;
        }
    }
    if (!lsr || !lsl || lsr->ReturnType() != lsl->ReturnType() ||
        result->ReturnType() != lsr->ReturnType()) {
        return std::nullopt;
    }
    const u32 bits = ir::GetValueSizeByte(result->ReturnType()) * 8;
    const u32 amount = lsr->GetArg<ir::Imm>(1).Get();
    const u32 left_amount = lsl->GetArg<ir::Imm>(1).Get();
    if ((bits != 32 && bits != 64) || amount == 0 || amount >= bits ||
        left_amount != bits - amount) {
        return std::nullopt;
    }
    return FunnelShiftPlan{
            .first = left_shift,
            .second = right_shift,
            .result = result,
            .high = lsl->GetArg<ir::Value>(0),
            .low = lsr->GetArg<ir::Value>(0),
            .amount = static_cast<u8>(amount),
    };
}

void JitTranslator::PrepareFunnelShifts(ir::Block* block) {
    funnel_shifts.clear();
    funnel_shift_parts.clear();
    funnel_shift_parity_producers.clear();
    auto& list = block->GetInstList();
    for (auto first = list.begin(); first != list.end(); ++first) {
        auto second = std::next(first);
        if (second == list.end()) {
            break;
        }
        auto result = std::next(second);
        if (result == list.end()) {
            break;
        }
        auto plan = MatchFunnelShift(&*result);
        if (!plan || plan->first != &*first || plan->second != &*second ||
            fused_narrow_extract_shifts.contains(plan->first) ||
            fused_narrow_extract_shifts.contains(plan->second)) {
            continue;
        }
        funnel_shift_parts.emplace(plan->first, plan->result);
        funnel_shift_parts.emplace(plan->second, plan->result);
        funnel_shifts.emplace(plan->result, *plan);

        auto flags_value = std::next(result);
        ir::Inst* flags_source = plan->result;
        if (flags_value != list.end() && flags_value->GetOp() == ir::OpCode::And &&
            flags_value->GetArg<ir::Value>(0).Def() == flags_source &&
            IsPlainImmediate(flags_value->GetArg<ir::Operand>(1), UINT32_MAX)) {
            flags_source = &*flags_value;
            ++flags_value;
        }
        if (flags_value != list.end() && flags_value->GetOp() == ir::OpCode::Or &&
            flags_value->GetArg<ir::Value>(0).Def() == flags_source &&
            IsPlainImmediate(flags_value->GetArg<ir::Operand>(1), 0) &&
            SavesParity(&*flags_value)) {
            funnel_shift_parity_producers.emplace(&*flags_value, plan->result);
        }
    }
}

bool JitTranslator::EmitFunnelShiftPart(ir::Inst* inst) {
    const auto part = funnel_shift_parts.find(inst);
    if (part == funnel_shift_parts.end()) {
        return false;
    }
    const auto planned = funnel_shifts.find(part->second);
    const auto reproved = MatchFunnelShift(part->second);
    ASSERT_MSG(planned != funnel_shifts.end() && reproved &&
                       *reproved == planned->second,
               "funnel shift proof diverged at IR {}", inst->Id());
    if (inst != planned->second.first) {
        return true;
    }
    auto result = context.RForWrite(ir::Value{planned->second.result});
    auto high = context.R(planned->second.high);
    auto low = context.R(planned->second.low);
    __ Extr(result, high, low, planned->second.amount);
    return true;
}

bool JitTranslator::EmitFunnelShiftResult(ir::Inst* inst) {
    const auto planned = funnel_shifts.find(inst);
    if (planned == funnel_shifts.end()) {
        return false;
    }
    const auto reproved = MatchFunnelShift(inst);
    ASSERT_MSG(reproved && *reproved == planned->second,
               "funnel shift result proof diverged at IR {}",
               inst->Id());
    auto pseudo_flags = GetPseudoFlags(inst);
    if (!pseudo_flags.Null() && !pseudo_flags.branch_only) {
        BeginFlagsTokenProducer(pseudo_flags);
    }
    if (!pseudo_flags.Null()) {
        auto result = context.R(ir::Value{inst});
        SaveLogicalResultFlags(result, inst->ReturnType(), pseudo_flags);
        FinishFlagsTokenProducer(result, inst->ReturnType(), pseudo_flags, inst);
    }
    return true;
}

#undef __

}  // namespace swift::runtime::backend::arm64
