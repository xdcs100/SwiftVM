#include <algorithm>

#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) {
    return index <= 9 || (index >= 19 && index <= 23) || index == 29;
}

bool IsHelperClobber(ir::OpCode op) {
    switch (op) {
        case ir::OpCode::CallLambda:
        case ir::OpCode::CallLocation:
        case ir::OpCode::CallDynamic:
        case ir::OpCode::X87Op:
        case ir::OpCode::Sse42Str:
            return true;
        default:
            return false;
    }
}

u32 CountUses(ir::Inst& consumer, ir::Inst* definition) {
    return std::ranges::count_if(
            consumer.GetValues(),
            [&](ir::Value value) { return value.Def() == definition; });
}

bool IsLow32Alias(ir::Inst& alias, ir::Inst* definition) {
    return alias.GetOp() == ir::OpCode::BitExtract &&
           alias.GetArg<ir::Value>(0).Def() == definition &&
           alias.GetArg<ir::Imm>(1).Get() == 0 &&
           alias.GetArg<ir::Imm>(2).Get() == 32 &&
           alias.ReturnType() == ir::ValueType::U32;
}

bool IsAliasConsumer(ir::Inst& consumer, ir::Inst* alias) {
    const u32 uses = CountUses(consumer, alias);
    if (uses != 1) {
        return false;
    }
    if (consumer.GetOp() == ir::OpCode::ZeroExtend32To64) {
        return consumer.GetArg<ir::Value>(0).Def() == alias;
    }
    switch (consumer.GetOp()) {
        case ir::OpCode::Add:
        case ir::OpCode::Sub:
        case ir::OpCode::And:
        case ir::OpCode::Or:
        case ir::OpCode::Xor:
        case ir::OpCode::Select:
            return consumer.ReturnType() == ir::ValueType::U32;
        default:
            return false;
    }
}

}  // namespace

std::optional<JitTranslator::PinnedSelectPublication>
JitTranslator::MatchPinnedSelectPublication(ir::Inst* publication) const {
    if (!publication || publication->GetOp() != ir::OpCode::SetHostGPR ||
        publication->GetArg<ir::Imm>(2).Get() != 0 ||
        dead_pinned_gpr_writes.contains(publication) ||
        context.IsHostWriteCoalesced(publication->Id())) {
        return std::nullopt;
    }

    const u32 target = publication->GetArg<ir::Imm>(1).Get();
    auto* extend = publication->GetArg<ir::Value>(0).Def();
    if (!IsPinnedGPR(target) || !extend ||
        extend->GetOp() != ir::OpCode::ZeroExtend32To64 ||
        extend->ReturnType() != ir::ValueType::U64) {
        return std::nullopt;
    }
    auto* producer = extend->GetArg<ir::Value>(0).Def();
    if (!producer || producer->GetOp() != ir::OpCode::SelectZero ||
        (producer->ReturnType() != ir::ValueType::U32 &&
         producer->ReturnType() != ir::ValueType::U64) ||
        producer->Id() >= extend->Id() || extend->Id() >= publication->Id() ||
        producer->GetUses(false) != 1) {
        return std::nullopt;
    }

    for (auto& scan : cur_block->GetInstList()) {
        if (scan.Id() > producer->Id() && scan.Id() < publication->Id() &&
            &scan != extend &&
            (MayFaultOrObserve(scan.GetOp()) ||
             (scan.GetOp() == ir::OpCode::SetHostGPR &&
              scan.GetArg<ir::Imm>(1).Get() == target))) {
            return std::nullopt;
        }
    }

    bool saw_publication = false;
    u32 extend_uses = 0;
    u32 last_use = publication->Id();
    std::vector<ir::Inst*> aliases;
    for (auto& consumer : cur_block->GetInstList()) {
        const u32 uses = CountUses(consumer, extend);
        if (!uses) {
            continue;
        }
        extend_uses += uses;
        if (&consumer == publication && uses == 1) {
            saw_publication = true;
            continue;
        }
        if (consumer.Id() <= publication->Id() || uses != 1 ||
            !IsLow32Alias(consumer, extend)) {
            return std::nullopt;
        }
        aliases.push_back(&consumer);
    }
    if (!saw_publication || extend_uses != extend->GetUses(false)) {
        return std::nullopt;
    }

    for (auto* alias : aliases) {
        u32 alias_uses = 0;
        for (auto& consumer : cur_block->GetInstList()) {
            const u32 uses = CountUses(consumer, alias);
            if (!uses) {
                continue;
            }
            alias_uses += uses;
            if (!IsAliasConsumer(consumer, alias)) {
                return std::nullopt;
            }
            last_use = std::max<u32>(last_use, consumer.Id());
        }
        if (alias_uses != alias->GetUses(false)) {
            return std::nullopt;
        }
    }

    for (auto& scan : cur_block->GetInstList()) {
        if (scan.Id() <= publication->Id() || scan.Id() >= last_use) {
            continue;
        }
        if ((scan.GetOp() == ir::OpCode::SetHostGPR &&
             scan.GetArg<ir::Imm>(1).Get() == target) ||
            (target <= 9 && IsHelperClobber(scan.GetOp()))) {
            return std::nullopt;
        }
    }

    return PinnedSelectPublication{
            .producer = producer,
            .extend = extend,
            .publication = publication,
            .aliases = std::move(aliases),
            .target = static_cast<u16>(target),
            .last_use = last_use,
    };
}

void JitTranslator::PreparePinnedSelectPublications(ir::Block* block) {
    pinned_select_results.clear();
    pinned_select_publications.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchPinnedSelectPublication(&inst);
        if (!plan || pinned_gpr_values.contains(plan->producer) ||
            fused_pin_gpr_reads.contains(plan->extend) ||
            std::ranges::any_of(plan->aliases, [&](ir::Inst* alias) {
                return fused_pin_gpr_reads.contains(alias);
            })) {
            continue;
        }
        pinned_gpr_values.emplace(plan->producer, plan->target);
        fused_pin_zext32.insert(plan->extend);
        for (auto* alias : plan->aliases) {
            fused_pin_gpr_reads.emplace(alias, plan->target);
        }
        pinned_select_results.emplace(plan->producer, *plan);
        pinned_select_publications.emplace(&inst, std::move(*plan));
    }
}

}  // namespace swift::runtime::backend::arm64
