#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) {
    return index <= 9 || (index >= 19 && index <= 23) || index == 29;
}

bool SupportsDirectPublication(ir::OpCode op) {
    using O = ir::OpCode;
    return op == O::LoadImm || op == O::LoadMemory || op == O::Add ||
           op == O::Sub || op == O::And;
}

}  // namespace

std::optional<JitTranslator::AdjacentSpilledGPRPublication>
JitTranslator::MatchAdjacentSpilledGPRPublication(ir::Inst* publication) {
    if (!publication || publication->GetOp() != ir::OpCode::SetHostGPR ||
        publication->GetArg<ir::Imm>(2).Get() != 0 ||
        dead_pinned_gpr_writes.contains(publication) ||
        context.IsHostWriteCoalesced(publication->Id())) {
        return std::nullopt;
    }

    const auto value = publication->GetArg<ir::Value>(0);
    auto* producer = value.Def();
    const u32 width = ir::GetValueSizeByte(value.Type());
    const u32 target = publication->GetArg<ir::Imm>(1).Get();
    if (!producer || !SupportsDirectPublication(producer->GetOp()) ||
        (width != sizeof(u32) && width != sizeof(u64)) ||
        ir::GetValueSizeByte(producer->ReturnType()) != width ||
        !IsPinnedGPR(target) || !context.IsSpilled(value) ||
        producer->GetUses(false) != 1 || producer->GetUses() != 1 ||
        !GetPseudoFlags(producer).Null()) {
        return std::nullopt;
    }

    auto& instructions = cur_block->GetInstList();
    const auto next = std::next(instructions.iterator_to(*producer));
    if (next == instructions.end() || next.operator->() != publication) {
        return std::nullopt;
    }

    return AdjacentSpilledGPRPublication{
            .producer = producer,
            .publication = publication,
            .target = static_cast<u16>(target),
    };
}

void JitTranslator::PrepareAdjacentSpilledGPRPublications(ir::Block* block) {
    adjacent_spilled_gpr_publications.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchAdjacentSpilledGPRPublication(&inst);
        if (!plan || pinned_gpr_values.contains(plan->producer)) {
            continue;
        }
        pinned_gpr_values.emplace(plan->producer, plan->target);
        adjacent_spilled_gpr_publications.emplace(&inst, *plan);
    }
}

}  // namespace swift::runtime::backend::arm64
