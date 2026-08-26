#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) {
    return index <= 9 || (index >= 19 && index <= 23) || index == 29;
}

bool IsMemoryAddressUse(ir::Inst& inst, ir::Inst* definition) {
    if (inst.GetOp() != ir::OpCode::LoadMemory &&
        inst.GetOp() != ir::OpCode::StoreMemory) {
        return false;
    }
    const auto address = inst.GetArg<ir::Operand>(0);
    const auto left = address.GetLeft();
    const auto right = address.GetRight();
    return (left.IsValue() && left.value.Def() == definition) ||
           (right.IsValue() && right.value.Def() == definition);
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

}  // namespace

std::optional<JitTranslator::PinnedGPRCopy>
JitTranslator::MatchPinnedGPRCopy(ir::Inst* inst) const {
    if (!inst || inst->GetOp() != ir::OpCode::SetHostGPR ||
        inst->GetArg<ir::Imm>(2).Get() != 0 ||
        context.IsHostWriteCoalesced(inst->Id()) ||
        dead_pinned_gpr_writes.contains(inst)) {
        return std::nullopt;
    }

    const u32 target = inst->GetArg<ir::Imm>(1).Get();
    auto published = inst->GetArg<ir::Value>(0);
    auto* extend = published.Def();
    if (!IsPinnedGPR(target) || !extend ||
        extend->GetOp() != ir::OpCode::ZeroExtend32To64 ||
        ir::GetValueSizeByte(published.Type()) != sizeof(u64)) {
        return std::nullopt;
    }

    auto source = extend->GetArg<ir::Value>(0);
    ir::Inst* narrow_extend{};
    if (source.Def() && source.Def()->GetOp() == ir::OpCode::ZeroExtend32) {
        narrow_extend = source.Def();
        if (narrow_extend->GetUses() != 1 ||
            ir::GetValueSizeByte(source.Type()) != sizeof(u32)) {
            return std::nullopt;
        }
        source = narrow_extend->GetArg<ir::Value>(0);
    }
    auto* read = source.Def();
    const u32 source_width = ir::GetValueSizeByte(source.Type());
    if (!read || read->GetUses() != 1 ||
        (source_width != sizeof(u8) && source_width != sizeof(u16) &&
         source_width != sizeof(u32))) {
        return std::nullopt;
    }
    const bool load_source = read->GetOp() == ir::OpCode::LoadMemory;
    std::optional<u16> source_index;
    if (!load_source) {
        if (read->GetOp() != ir::OpCode::GetHostGPR ||
            read->GetArg<ir::Imm>(1).Get() != 0 ||
            context.IsHostReadCoalesced(read->Id())) {
            return std::nullopt;
        }
        const u32 index = read->GetArg<ir::Imm>(0).Get();
        if (!IsPinnedGPR(index)) {
            return std::nullopt;
        }
        source_index = static_cast<u16>(index);
    }

    bool saw_publication = false;
    u32 published_uses = 0;
    std::vector<ir::Inst*> aliases;
    for (auto& scan : cur_block->GetInstList()) {
        for (auto used : scan.GetValues()) {
            if (used.Def() != extend) {
                continue;
            }
            ++published_uses;
            if (&scan == inst &&
                scan.GetArg<ir::Value>(0).Def() == extend) {
                saw_publication = true;
                continue;
            }
            if (scan.GetOp() != ir::OpCode::BitCast ||
                scan.GetArg<ir::Value>(0).Def() != extend ||
                scan.Id() <= inst->Id()) {
                return std::nullopt;
            }
            aliases.push_back(&scan);
        }
    }
    if (!saw_publication || published_uses != extend->GetUses()) {
        return std::nullopt;
    }

    u32 last_use = inst->Id();
    for (auto* alias : aliases) {
        u32 alias_uses = 0;
        for (auto& scan : cur_block->GetInstList()) {
            if (IsMemoryAddressUse(scan, alias)) {
                ++alias_uses;
                last_use = std::max<u32>(last_use, scan.Id());
            }
        }
        if (alias_uses == 0 || alias_uses != alias->GetUses()) {
            return std::nullopt;
        }
    }

    if (read->Id() >= extend->Id() || extend->Id() >= inst->Id()) {
        return std::nullopt;
    }
    for (auto& scan : cur_block->GetInstList()) {
        if (read->Id() < scan.Id() && scan.Id() < inst->Id() &&
            (MayFaultOrObserve(scan.GetOp()) ||
             (scan.GetOp() == ir::OpCode::SetHostGPR &&
              (scan.GetArg<ir::Imm>(1).Get() == target ||
               (source_index &&
                scan.GetArg<ir::Imm>(1).Get() == *source_index))))) {
            return std::nullopt;
        }
        if (inst->Id() < scan.Id() && scan.Id() < last_use &&
            ((scan.GetOp() == ir::OpCode::SetHostGPR &&
              scan.GetArg<ir::Imm>(1).Get() == target) ||
             (target <= 9 && IsHelperClobber(scan.GetOp())))) {
            return std::nullopt;
        }
    }
    return PinnedGPRCopy{
            .read = read,
            .narrow_extend = narrow_extend,
            .extend = extend,
            .aliases = std::move(aliases),
            .source = source_index,
            .target = static_cast<u16>(target),
            .width = static_cast<u8>(source_width),
    };
}

std::optional<XRegister>
JitTranslator::ResolvePinnedGPRValue(ir::Value value) const {
    if (!value.Def()) {
        return std::nullopt;
    }
    auto pinned = pinned_gpr_values.find(value.Def());
    if (pinned == pinned_gpr_values.end()) {
        return std::nullopt;
    }
    return XRegister(pinned->second);
}

void JitTranslator::PreparePinnedGPRCopies(ir::Block* block) {
    fused_pin_zext32.clear();
    fused_pin_gpr_reads.clear();
    pinned_gpr_values.clear();
    pinned_gpr_copies.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchPinnedGPRCopy(&inst);
        if (!plan) {
            continue;
        }
        if (!plan->source) {
            pinned_gpr_values.emplace(plan->read, plan->target);
        } else {
            fused_pin_gpr_reads.emplace(plan->read, *plan->source);
        }
        if (plan->narrow_extend) {
            fused_pin_zext32.insert(plan->narrow_extend);
        }
        fused_pin_zext32.insert(plan->extend);
        for (auto* alias : plan->aliases) {
            pinned_gpr_values.emplace(alias, plan->target);
        }
        pinned_gpr_copies.emplace(&inst, *plan);
    }
}

}  // namespace swift::runtime::backend::arm64
