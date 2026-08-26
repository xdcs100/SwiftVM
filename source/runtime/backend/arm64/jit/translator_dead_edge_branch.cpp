#include "translator.h"

#include <algorithm>

namespace swift::runtime::backend::arm64 {

namespace {

struct ConditionPlan {
    ir::Flags required{};
    ir::Cond raw_condition{};
    std::unordered_set<ir::Inst*> discarded{};
};

ir::Inst* OperandDefinition(const ir::Operand& operand) {
    const auto value = operand.GetLeft();
    return value.IsValue() ? value.value.Def() : nullptr;
}

std::optional<ConditionPlan> AnalyzeCondition(ir::Inst* condition) {
    if (!condition || condition->GetUses() != 1) {
        return std::nullopt;
    }
    if (condition->GetOp() == ir::OpCode::LocalCondSet) {
        switch (condition->GetArg<ir::Cond>(0)) {
            case ir::Cond::EQ:
            case ir::Cond::NE:
                return ConditionPlan{ir::Flags::Zero,
                                     condition->GetArg<ir::Cond>(0)};
            case ir::Cond::CS:
                return ConditionPlan{ir::Flags::Carry, ir::Cond::CC};
            case ir::Cond::CC:
                return ConditionPlan{ir::Flags::Carry, ir::Cond::CS};
            default:
                return std::nullopt;
        }
    }
    if (condition->GetOp() != ir::OpCode::And &&
        condition->GetOp() != ir::OpCode::Or) {
        return std::nullopt;
    }

    auto* left = condition->GetArg<ir::Value>(0).Def();
    auto* right = OperandDefinition(condition->GetArg<ir::Operand>(1));
    auto is_zero_cond = [](ir::Inst* inst, ir::Cond cond) {
        return inst && inst->GetOp() == ir::OpCode::CondSet &&
               inst->GetUses() == 1 && inst->GetArg<ir::Cond>(0) == cond;
    };
    ir::Inst* predicate = left;
    ir::Inst* zero_condition = right;
    if (is_zero_cond(left, ir::Cond::EQ) ||
        is_zero_cond(left, ir::Cond::NE)) {
        predicate = right;
        zero_condition = left;
    }
    if (!predicate || predicate->GetUses() != 1 ||
        !zero_condition || zero_condition->GetUses() != 1) {
        return std::nullopt;
    }
    const bool high = condition->GetOp() == ir::OpCode::And &&
                      predicate->GetOp() == ir::OpCode::TestZero &&
                      is_zero_cond(zero_condition, ir::Cond::NE);
    const bool low_same = condition->GetOp() == ir::OpCode::Or &&
                          predicate->GetOp() == ir::OpCode::TestNotZero &&
                          is_zero_cond(zero_condition, ir::Cond::EQ);
    if (!high && !low_same) {
        return std::nullopt;
    }
    auto* carry_test = predicate->GetArg<ir::Value>(0).Def();
    if (!carry_test || carry_test->GetOp() != ir::OpCode::TestFlags ||
        carry_test->GetUses() != 1 ||
        carry_test->GetArg<ir::Flags>(0) != ir::Flags::Carry) {
        return std::nullopt;
    }
    ConditionPlan plan{ir::Flags::Carry | ir::Flags::Zero,
                       high ? ir::Cond::HI : ir::Cond::LS};
    plan.discarded.insert(carry_test);
    plan.discarded.insert(predicate);
    plan.discarded.insert(zero_condition);
    return plan;
}

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
    auto condition_plan = AnalyzeCondition(condition);
    if (!condition_plan) {
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
            .required = condition_plan->required,
            .raw_condition = condition_plan->raw_condition,
            .discarded = std::move(condition_plan->discarded),
    };
    u32 inverts = 0;
    u32 polarity_stores = 0;
    for (size_t i = producer_begin; i < condition_index; ++i) {
        auto* inst = instructions[i];
        if (plan.discarded.contains(inst)) {
            continue;
        }
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
    local_conditions.emplace(condition, MapCond(plan.raw_condition));
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
    return dead_edge_integer_branch->raw_condition;
}

std::optional<JitTranslator::DeadNarrowImmediateBranchPlan>
JitTranslator::MatchDeadNarrowImmediateBranch(ir::Inst* inst) const {
    if (!inst || inst->GetOp() != ir::OpCode::Sub || inst->GetUses() != 0 ||
        !IsDeadEdgeIntegerBranchProducer(inst)) {
        return std::nullopt;
    }
    const auto required = dead_edge_integer_branch->required;
    if (required != ir::Flags::Zero && required != ir::Flags::Carry &&
        required != (ir::Flags::Carry | ir::Flags::Zero)) {
        return std::nullopt;
    }
    const u32 width = ir::GetValueSizeByte(inst->ReturnType());
    if (width != sizeof(u8) && width != sizeof(u16)) {
        return std::nullopt;
    }

    const auto right = inst->GetArg<ir::Operand>(1);
    if (!right.GetRight().Null() || !right.GetLeft().IsValue() ||
        right.GetOp() != ir::OperandOp::Plus) {
        return std::nullopt;
    }
    auto* load = right.GetLeft().value.Def();
    if (!load || load->GetOp() != ir::OpCode::LoadImm ||
        load->GetUses(false) != 1) {
        return std::nullopt;
    }
    const u64 immediate = load->GetArg<ir::Imm>(0).Get();
    if (!masm.IsImmAddSub(immediate)) {
        return std::nullopt;
    }
    return DeadNarrowImmediateBranchPlan{
            .producer = inst,
            .immediate_load = load,
            .immediate = immediate,
            .required = required,
            .width = static_cast<u8>(width),
    };
}

void JitTranslator::PrepareDeadNarrowImmediateBranch() {
    dead_narrow_immediate_branch.reset();
    if (!dead_edge_integer_branch) {
        return;
    }
    dead_narrow_immediate_branch =
            MatchDeadNarrowImmediateBranch(dead_edge_integer_branch->producer);
    if (dead_narrow_immediate_branch) {
        disable_instructions.set(
                dead_narrow_immediate_branch->immediate_load->Id());
    }
}

}  // namespace swift::runtime::backend::arm64
