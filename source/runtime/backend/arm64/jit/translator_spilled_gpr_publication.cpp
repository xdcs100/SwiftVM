#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) {
    return index <= 9 || (index >= 19 && index <= 23) || index == 29;
}

bool SupportsDirectPublication(ir::OpCode op) {
    using O = ir::OpCode;
    return op == O::LoadImm || op == O::LoadMemory || op == O::Add ||
           op == O::Sub || op == O::And || op == O::GetOperand ||
           op == O::SignExtend || op == O::BitExtract;
}

}  // namespace

bool JitTranslator::CanAdoptPendingSpillWrite(ir::Inst* inst) {
    if (!inst) {
        return false;
    }
    if (inst->GetOp() != ir::OpCode::SetHostGPR) {
        return true;
    }
    if (pinned_select_publications.contains(inst) ||
        spilled_gpr_publications.contains(inst) ||
        dead_pinned_gpr_writes.contains(inst) ||
        pinned_gpr_value_transfers.contains(inst) ||
        pinned_gpr_copies.contains(inst) ||
        context.IsHostWriteCoalesced(inst->Id())) {
        return false;
    }
    if (auto update = pinned_load_update_instructions.find(inst);
        update != pinned_load_update_instructions.end() &&
        inst == update->second.publication) {
        return false;
    }
    const auto published = inst->GetArg<ir::Value>(0);
    if (auto update = MatchBiasedMemoryUpdate(published.Def());
        update && update->publication == inst) {
        return false;
    }
    if (auto update = MatchPreIndexMemoryUpdate(published.Def());
        update && update->publication == inst) {
        return false;
    }
    if (published.Def() && fused_pin_zext32.contains(published.Def())) {
        return false;
    }
    return !guest_state_map.FixedHomeForUse(published, inst).has_value();
}

std::optional<JitTranslator::SpilledGPRPublication>
JitTranslator::MatchSpilledGPRPublication(ir::Inst* publication) {
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
        producer->GetUses(false) != 1) {
        return std::nullopt;
    }

    auto& instructions = cur_block->GetInstList();
    const auto producer_it = instructions.iterator_to(*producer);
    const auto publication_it = instructions.iterator_to(*publication);
    if (producer->Id() >= publication->Id() ||
        !guest_state_map.PublicationWindowSafe(
                target, value, producer->Id(), publication->Id())) {
        return std::nullopt;
    }
    for (auto it = std::next(producer_it); it != publication_it; ++it) {
        if (it == instructions.end() || it->GetOp() == ir::OpCode::Goto ||
            it->GetOp() == ir::OpCode::NotGoto ||
            it->GetOp() == ir::OpCode::BindLabel ||
            (backend::FixedGPRClobbers(*it, context.GetFeatures(), true) &
             (1u << target))) {
            return std::nullopt;
        }
        for (auto input : it->GetValues()) {
            if (input.Defined() && context.IsGPRMappedTo(input, target)) {
                return std::nullopt;
            }
        }
        if (it->HasValue() &&
            context.IsGPRMappedTo(ir::Value{it.operator->()}, target)) {
            return std::nullopt;
        }
    }

    return SpilledGPRPublication{
            .producer = producer,
            .publication = publication,
            .target = static_cast<u16>(target),
    };
}

void JitTranslator::PrepareSpilledGPRPublications(ir::Block* block) {
    spilled_gpr_publications.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchSpilledGPRPublication(&inst);
        if (!plan || pinned_gpr_values.contains(plan->producer)) {
            continue;
        }
        pinned_gpr_values.emplace(plan->producer, plan->target);
        spilled_gpr_publications.emplace(&inst, *plan);
    }
}

}  // namespace swift::runtime::backend::arm64
