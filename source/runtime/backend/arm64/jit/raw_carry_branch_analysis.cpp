#include "raw_carry_branch_analysis.h"

#include <algorithm>
#include <iterator>
#include <type_traits>

#include "runtime/common/variant_util.h"

namespace swift::runtime::backend::arm64 {

namespace {

ir::Inst* SoleUser(ir::Block* block, ir::Inst* definition) {
    if (!definition || definition->GetUses() != 1) {
        return nullptr;
    }
    ir::Inst* result{};
    for (auto& inst : block->GetInstList()) {
        for (ir::Value value : inst.GetValues()) {
            if (value.Def() != definition) {
                continue;
            }
            if (result) {
                return nullptr;
            }
            result = &inst;
        }
    }
    return result;
}

ir::Inst* OtherOperand(ir::Inst* combine, ir::Inst* known) {
    if (!combine || (combine->GetOp() != ir::OpCode::And &&
                     combine->GetOp() != ir::OpCode::Or)) {
        return nullptr;
    }
    if (combine->GetArg<ir::Value>(0).Def() == known) {
        const auto right = combine->GetArg<ir::Operand>(1).GetLeft();
        return right.IsValue() ? right.value.Def() : nullptr;
    }
    return combine->GetArg<ir::Value>(0).Def();
}

bool TerminalUses(const ir::Terminal& terminal, const ir::Inst* definition) {
    return VisitVariant<bool>(terminal, [&](const auto& term) {
        using T = std::decay_t<decltype(term)>;
        if constexpr (std::is_same_v<T, ir::terminal::If>) {
            return term.cond.Def() == definition ||
                   TerminalUses(term.then_, definition) ||
                   TerminalUses(term.else_, definition);
        } else if constexpr (std::is_same_v<T, ir::terminal::Condition>) {
            return TerminalUses(term.then_, definition) ||
                   TerminalUses(term.else_, definition);
        } else if constexpr (std::is_same_v<T, ir::terminal::CheckHalt>) {
            return TerminalUses(term.else_, definition);
        } else if constexpr (std::is_same_v<T, ir::terminal::Switch>) {
            for (const auto& item : term.cases) {
                if (TerminalUses(item.then, definition)) {
                    return true;
                }
            }
        }
        return false;
    });
}

bool HasLocalConditionUse(ir::Block* block, ir::Inst* definition) {
    if (definition->GetUses() != 1) {
        return false;
    }
    auto& instructions = block->GetInstList();
    for (auto it = std::next(instructions.iterator_to(*definition));
         it != instructions.end(); ++it) {
        bool names{};
        for (ir::Value value : it->GetValues()) {
            names |= value.Def() == definition;
        }
        if (!names) {
            continue;
        }
        return ((it->GetOp() == ir::OpCode::Goto ||
                 it->GetOp() == ir::OpCode::NotGoto) &&
                it->GetArg<ir::Value>(0).Def() == definition) ||
               (it->GetOp() == ir::OpCode::Select &&
                it->GetArg<ir::Value>(0).Def() == definition);
    }
    return TerminalUses(block->GetTerminal(), definition);
}

}  // namespace

void RawCarryBranchAnalysis::Analyze(ir::Block* block) {
    test_inverts.clear();
    test_conditions.clear();
    auto& instructions = block->GetInstList();
    for (auto test = instructions.begin(); test != instructions.end(); ++test) {
        if (test->GetOp() != ir::OpCode::TestFlags ||
            test->GetArg<ir::Flags>(0) != ir::Flags::Carry) {
            continue;
        }
        auto advance = test;
        if (advance == instructions.begin() ||
            (--advance)->GetOp() != ir::OpCode::AdvancePC) {
            continue;
        }
        auto invert = advance;
        if (invert == instructions.begin() ||
            (--invert)->GetOp() != ir::OpCode::InvertCarry) {
            continue;
        }
        auto save = invert;
        if (save == instructions.begin() ||
            (--save)->GetOp() != ir::OpCode::SaveFlags ||
            !True(save->GetArg<ir::Flags>(1) & ir::Flags::Carry)) {
            continue;
        }
        auto* producer = save->GetArg<ir::Value>(0).Def();
        if (!producer || (producer->GetOp() != ir::OpCode::Sub &&
                          producer->GetOp() != ir::OpCode::Sbb)) {
            continue;
        }
        auto* predicate = SoleUser(block, &*test);
        auto* combine = SoleUser(block, predicate);
        auto* other = OtherOperand(combine, predicate);
        if (!predicate || !combine || !other ||
            other->GetOp() != ir::OpCode::CondSet ||
            !HasLocalConditionUse(block, combine)) {
            continue;
        }
        const ir::Cond zero_condition = other->GetArg<ir::Cond>(0);
        std::optional<ir::Cond> raw_condition;
        if (predicate->GetOp() == ir::OpCode::TestZero &&
            combine->GetOp() == ir::OpCode::And &&
            zero_condition == ir::Cond::NE) {
            raw_condition = ir::Cond::HI;
        } else if (predicate->GetOp() == ir::OpCode::TestNotZero &&
                   combine->GetOp() == ir::OpCode::Or &&
                   zero_condition == ir::Cond::EQ) {
            raw_condition = ir::Cond::LS;
        }
        if (!raw_condition) {
            continue;
        }
        test_inverts.emplace(&*test, &*invert);
        test_conditions.emplace(&*test, *raw_condition);
    }
}

bool RawCarryBranchAnalysis::SuppressesInvert(const ir::Inst* inst) const {
    return std::any_of(test_inverts.begin(), test_inverts.end(),
                       [&](const auto& item) { return item.second == inst; });
}

std::optional<ir::Cond> RawCarryBranchAnalysis::ConditionForTest(
        const ir::Inst* inst) const {
    const auto condition = test_conditions.find(inst);
    return condition == test_conditions.end()
            ? std::nullopt
            : std::optional<ir::Cond>{condition->second};
}

ir::Inst* RawCarryBranchAnalysis::InvertForTest(const ir::Inst* inst) const {
    const auto invert = test_inverts.find(inst);
    return invert == test_inverts.end() ? nullptr : invert->second;
}

}  // namespace swift::runtime::backend::arm64
