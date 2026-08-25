#include "translator.h"

#include <algorithm>

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPolarityStore(const ir::Inst& inst) {
    if (inst.GetOp() != ir::OpCode::StoreUniform) {
        return false;
    }
    const auto uniform = inst.GetArg<ir::Uniform>(0);
    const auto value = inst.GetArg<ir::Value>(1);
    return uniform.GetType() == ir::ValueType::U8 && value.Def() &&
           value.Def()->GetOp() == ir::OpCode::LoadImm &&
           value.Def()->GetArg<ir::Imm>(0).Get() <= 1;
}

bool IsFlagObserver(ir::OpCode op) {
    switch (op) {
        case ir::OpCode::TestFlags:
        case ir::OpCode::TestNotFlags:
        case ir::OpCode::GetFlags:
        case ir::OpCode::Adc:
        case ir::OpCode::Sbb:
        case ir::OpCode::SetCarry:
        case ir::OpCode::SetOverflow:
        case ir::OpCode::PublishFCmpFlags:
        case ir::OpCode::CondSelect:
        case ir::OpCode::CondSet:
        case ir::OpCode::LocalParitySet:
        case ir::OpCode::FCmpCondSet:
        case ir::OpCode::Goto:
        case ir::OpCode::NotGoto:
        case ir::OpCode::BindLabel:
            return true;
        default:
            return false;
    }
}

}  // namespace

void JitTranslator::PrepareDeadEdgeIntegerBranch(ir::Block* block) {
    dead_edge_integer_branch.reset();

    if (!block->HasDeadEdgeIntegerBranchProof()) {
        return;
    }

    auto terminal = block->GetTerminal();
    auto* branch = boost::get<ir::terminal::If>(&terminal);
    auto* condition = branch ? branch->cond.Def() : nullptr;
    if (!condition || condition->GetOp() != ir::OpCode::LocalCondSet ||
        condition->GetUses() != 1) {
        return;
    }

    const auto condition_code = condition->GetArg<ir::Cond>(0);
    if (condition_code != ir::Cond::EQ && condition_code != ir::Cond::NE) {
        return;
    }

    std::vector<ir::Inst*> instructions;
    instructions.reserve(block->GetInstList().size());
    for (auto& inst : block->GetInstList()) {
        instructions.push_back(&inst);
    }
    const auto condition_it =
            std::find(instructions.begin(), instructions.end(), condition);
    if (condition_it == instructions.end()) {
        return;
    }
    const size_t condition_index = std::distance(instructions.begin(), condition_it);

    size_t producer_end = condition_index;
    while (producer_end > 0 &&
           instructions[producer_end - 1]->GetOp() != ir::OpCode::AdvancePC) {
        --producer_end;
    }
    if (producer_end == 0) {
        return;
    }
    const size_t producer_advance = producer_end - 1;
    size_t producer_begin = producer_advance;
    while (producer_begin > 0 &&
           instructions[producer_begin - 1]->GetOp() != ir::OpCode::AdvancePC) {
        --producer_begin;
    }

    DeadEdgeIntegerBranchPlan plan{
            .condition = condition,
    };
    u32 inverts = 0;
    u32 polarity_stores = 0;
    for (size_t i = producer_begin; i < condition_index; ++i) {
        auto* inst = instructions[i];
        switch (inst->GetOp()) {
            case ir::OpCode::SaveFlags: {
                plan.discarded.insert(inst);
                auto value = inst->GetArg<ir::Value>(0);
                if (!value.Def() || value.Def()->GetOp() != ir::OpCode::Sub ||
                    (plan.producer && plan.producer != value.Def())) {
                    return;
                }
                plan.producer = value.Def();
                break;
            }
            case ir::OpCode::ClearFlags:
                plan.discarded.insert(inst);
                break;
            case ir::OpCode::InvertCarry:
                ++inverts;
                plan.discarded.insert(inst);
                break;
            case ir::OpCode::StoreUniform:
                if (IsPolarityStore(*inst)) {
                    ++polarity_stores;
                    plan.discarded.insert(inst);
                    auto* value = inst->GetArg<ir::Value>(1).Def();
                    if (value && value->GetUses(false) == 1) {
                        plan.discarded.insert(value);
                    }
                }
                break;
            default:
                if (IsFlagObserver(inst->GetOp())) {
                    return;
                }
                break;
        }
    }
    if (!plan.producer || inverts != 1 || polarity_stores > 1 ||
        plan.producer->Id() >= condition->Id()) {
        return;
    }

    bool after_producer = false;
    for (size_t i = producer_begin; i < condition_index; ++i) {
        auto* inst = instructions[i];
        if (inst == plan.producer) {
            after_producer = true;
            continue;
        }
        if (!after_producer || plan.discarded.contains(inst)) {
            continue;
        }
        if (!PreservesHostNZCV(inst->GetOp()) || MayFaultOrObserve(inst->GetOp())) {
            return;
        }
    }

    for (auto* inst : plan.discarded) {
        disable_instructions.set(inst->Id());
    }
    dead_edge_integer_branch = std::move(plan);
}

bool JitTranslator::IsDeadEdgeIntegerBranchProducer(ir::Inst* inst) const {
    return dead_edge_integer_branch && dead_edge_integer_branch->producer == inst;
}

std::optional<ir::Cond> JitTranslator::DeadEdgeIntegerBranchCondition(
        ir::Inst* inst) const {
    if (!dead_edge_integer_branch || dead_edge_integer_branch->condition != inst) {
        return std::nullopt;
    }
    return inst->GetArg<ir::Cond>(0);
}

}  // namespace swift::runtime::backend::arm64
