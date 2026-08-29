#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) { return index <= 9 || (index >= 19 && index <= 23) || index == 29; }

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

bool IsLowViewAlias(ir::Block* block, ir::Inst* alias, ir::Inst* value) {
    if (alias->GetOp() != ir::OpCode::BitExtract || alias->GetArg<ir::Value>(0).Def() != value ||
        alias->GetArg<ir::Imm>(1).Get() != 0 || alias->GetUses() != 1 ||
        alias->GetUses(false) != 1) {
        return false;
    }
    const u32 width = ir::GetValueSizeByte(alias->ReturnType());
    if ((width != sizeof(u8) && width != sizeof(u16) && width != sizeof(u32)) ||
        alias->GetArg<ir::Imm>(2).Get() != width * 8) {
        return false;
    }
    for (auto& consumer : block->GetInstList()) {
        const auto uses = std::ranges::count_if(
                consumer.GetValues(), [&](ir::Value input) { return input.Def() == alias; });
        if (!uses) {
            continue;
        }
        const auto op = consumer.GetOp();
        if (uses != 1 ||
            (op != ir::OpCode::Add && op != ir::OpCode::Sub && op != ir::OpCode::Select) ||
            ir::GetValueSizeByte(consumer.ReturnType()) != width) {
            return false;
        }
        return true;
    }
    return false;
}

u32 AliasLastUse(ir::Block* block, ir::Inst* alias) {
    u32 last_use = alias->Id();
    for (auto& consumer : block->GetInstList()) {
        for (auto input : consumer.GetValues()) {
            if (input.Def() == alias) {
                last_use = std::max<u32>(last_use, consumer.Id());
            }
        }
    }
    return last_use;
}

}  // namespace

std::optional<JitTranslator::PinnedGPRPublicationView> JitTranslator::MatchPinnedGPRPublicationView(
        ir::Inst* publication) const {
    if (!publication || publication->GetOp() != ir::OpCode::SetHostGPR ||
        publication->GetArg<ir::Imm>(2).Get() != 0 ||
        dead_pinned_gpr_writes.contains(publication)) {
        return std::nullopt;
    }
    const u32 target = publication->GetArg<ir::Imm>(1).Get();
    const auto published = publication->GetArg<ir::Value>(0);
    auto* value = published.Def();
    if (!IsPinnedGPR(target) || !value || value->Id() >= publication->Id() ||
        ir::GetValueSizeByte(published.Type()) != sizeof(u64) ||
        ir::GetValueSizeByte(value->ReturnType()) != sizeof(u64) ||
        value->GetOp() == ir::OpCode::GetHostGPR ||
        value->GetOp() == ir::OpCode::ZeroExtend32To64) {
        return std::nullopt;
    }

    bool saw_publication = false;
    u32 ordinary_uses{};
    u32 last_use = publication->Id();
    std::vector<ir::Inst*> aliases;
    for (auto& consumer : cur_block->GetInstList()) {
        const auto uses = std::ranges::count_if(
                consumer.GetValues(), [&](ir::Value input) { return input.Def() == value; });
        if (!uses) {
            continue;
        }
        ordinary_uses += uses;
        if (&consumer == publication && uses == 1) {
            saw_publication = true;
            continue;
        }
        if (consumer.Id() <= publication->Id() || uses != 1 ||
            !IsLowViewAlias(cur_block, &consumer, value)) {
            return std::nullopt;
        }
        aliases.push_back(&consumer);
        last_use = std::max(last_use, AliasLastUse(cur_block, &consumer));
    }
    if (!saw_publication || aliases.empty() || ordinary_uses != value->GetUses(false)) {
        return std::nullopt;
    }
    for (auto& scan : cur_block->GetInstList()) {
        if (scan.Id() <= publication->Id() || scan.Id() >= last_use) {
            continue;
        }
        if ((scan.GetOp() == ir::OpCode::SetHostGPR && scan.GetArg<ir::Imm>(1).Get() == target) ||
            (target <= 9 && IsHelperClobber(scan.GetOp()))) {
            return std::nullopt;
        }
    }
    return PinnedGPRPublicationView{
            .value = value,
            .publication = publication,
            .aliases = std::move(aliases),
            .target = static_cast<u16>(target),
            .last_use = last_use,
    };
}

void JitTranslator::PreparePinnedGPRPublicationViews(ir::Block* block) {
    pinned_gpr_publication_views.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchPinnedGPRPublicationView(&inst);
        if (!plan || std::ranges::any_of(plan->aliases, [&](ir::Inst* alias) {
                return fused_pin_gpr_reads.contains(alias);
            })) {
            continue;
        }
        for (auto* alias : plan->aliases) {
            fused_pin_gpr_reads.emplace(alias, plan->target);
        }
        pinned_gpr_publication_views.emplace(&inst, std::move(*plan));
    }
}

}  // namespace swift::runtime::backend::arm64
