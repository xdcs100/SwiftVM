#include "register_alloc_internal.h"

#include <iterator>

namespace swift::runtime::ir {

namespace {

bool IsReadOnlyLow32Consumer(OpCode op) {
    return op == OpCode::StoreMemory || op == OpCode::StoreUniform ||
           op == OpCode::SetHostGPR;
}

bool CanCoalesceLiveLow32View(
        Block* block,
        backend::RegAlloc* reg_alloc,
        Inst& bridge,
        Value source,
        const Vector<u32>& use_end) {
    if (bridge.GetUses() == 0 || bridge.Id() >= use_end.size() ||
        source.Id() >= use_end.size() ||
        reg_alloc->ValueType(source) != backend::RegAlloc::GPR ||
        reg_alloc->ValueType(Value{&bridge}) != backend::RegAlloc::GPR) {
        return false;
    }
    const u32 source_reg = reg_alloc->ValueGPR(source).id;
    const u32 bridge_reg = reg_alloc->ValueGPR(Value{&bridge}).id;
    if (source_reg == bridge_reg || use_end[bridge.Id()] <= bridge.Id() ||
        use_end[source.Id()] < use_end[bridge.Id()]) {
        return false;
    }

    bool has_consumer = false;
    for (auto& scan : block->GetInstList()) {
        if (scan.Id() < bridge.Id()) {
            continue;
        }
        if (scan.Id() > use_end[bridge.Id()]) {
            break;
        }
        if (!reg_alloc->DirtyGPR(scan.Id()).Get(source_reg)) {
            return false;
        }
        bool uses = false;
        for (auto input : scan.GetValues()) {
            uses |= input.Def() == &bridge;
        }
        if (!uses) {
            continue;
        }
        has_consumer = true;
        const auto type = reg_alloc->ValueType(Value{&scan});
        if (type == backend::RegAlloc::GPR) {
            const u32 target = reg_alloc->ValueGPR(Value{&scan}).id;
            if (target == source_reg || target == bridge_reg) {
                return false;
            }
        } else if (type == backend::RegAlloc::NONE &&
                   !IsReadOnlyLow32Consumer(scan.GetOp())) {
            return false;
        }
    }
    return has_consumer;
}

bool TransferLow32ViewTail(
        Block* block,
        backend::RegAlloc* reg_alloc,
        Inst& bridge,
        Value source,
        const Vector<u32>& use_end,
        const RegisterAllocFamilyCallbacks& callbacks) {
    if (!source.Defined() || bridge.GetUses() == 0 ||
        source.Id() >= use_end.size() || bridge.Id() >= use_end.size() ||
        use_end[source.Id()] < bridge.Id() ||
        use_end[source.Id()] >= use_end[bridge.Id()] ||
        use_end[bridge.Id()] <= bridge.Id() ||
        reg_alloc->IsFixedGPR(source.Id()) || reg_alloc->IsFixedGPR(bridge.Id()) ||
        reg_alloc->ValueType(source) != backend::RegAlloc::GPR ||
        reg_alloc->ValueType(Value{&bridge}) != backend::RegAlloc::GPR) {
        return false;
    }
    const u32 source_reg = reg_alloc->ValueGPR(source).id;
    const u32 bridge_reg = reg_alloc->ValueGPR(Value{&bridge}).id;
    if (source_reg == bridge_reg) {
        return false;
    }
    for (auto& other : block->GetInstList()) {
        if (other.Id() <= bridge.Id() || other.Id() > use_end[bridge.Id()] ||
            !other.HasValue()) {
            continue;
        }
        if (reg_alloc->ValueType(Value{&other}) == backend::RegAlloc::GPR &&
            reg_alloc->ValueGPR(Value{&other}).id == source_reg) {
            return false;
        }
    }

    struct ActiveRegs {
        u32 id;
        backend::GPRSMask gprs;
        backend::FPRSMask fprs;
    };
    Vector<ActiveRegs> changed;
    auto restore = [&] {
        for (auto& state : changed) {
            reg_alloc->SetActiveRegs(state.id, state.gprs, state.fprs);
        }
    };
    for (auto& scan : block->GetInstList()) {
        if (scan.Id() < bridge.Id()) {
            continue;
        }
        if (scan.Id() > use_end[bridge.Id()]) {
            break;
        }
        bool uses_bridge = false;
        for (auto input : scan.GetValues()) {
            uses_bridge |= input.Def() == &bridge;
        }
        if (uses_bridge) {
            if (scan.GetOp() == OpCode::AtomicExchange) {
                restore();
                return false;
            }
            const auto type = reg_alloc->ValueType(Value{&scan});
            if (type == backend::RegAlloc::GPR) {
                const u32 target = reg_alloc->ValueGPR(Value{&scan}).id;
                if (target == source_reg || target == bridge_reg) {
                    restore();
                    return false;
                }
            } else if (type == backend::RegAlloc::NONE &&
                       !IsReadOnlyLow32Consumer(scan.GetOp())) {
                restore();
                return false;
            }
        }

        auto gprs = reg_alloc->DirtyGPR(scan.Id());
        auto fprs = reg_alloc->DirtyFPR(scan.Id());
        changed.push_back({scan.Id(), gprs, fprs});
        gprs.Clear(bridge_reg);
        gprs.Mark(source_reg);
        reg_alloc->SetActiveRegs(scan.Id(), gprs, fprs);
        if (!callbacks.check_instr(callbacks.context, &scan, 0, 0)) {
            restore();
            return false;
        }
    }
    reg_alloc->MapReference(source.Id(), bridge.Id());
    reg_alloc->MarkLow32CopyCoalesced(bridge.Id(), source.Id());
    return true;
}

}  // namespace

void CoalesceLow32CopyChains(
        Block* lir_block,
        backend::RegAlloc* reg_alloc,
        const Vector<u32>& use_end,
        const RegisterAllocFamilyCallbacks& callbacks) {
    auto& list = lir_block->GetInstList();
    for (auto bridge_it = list.begin(); bridge_it != list.end(); ++bridge_it) {
        auto& bridge = *bridge_it;
        if (bridge.GetOp() != OpCode::BitExtract ||
            GetValueSizeByte(bridge.ReturnType()) != sizeof(u32) ||
            bridge.GetArg<Imm>(1).Get() != 0 ||
            bridge.GetArg<Imm>(2).Get() != 32 ||
            reg_alloc->IsWidthChainCoalesced(bridge.Id()) ||
            reg_alloc->IsLow32CopyCoalesced(bridge.Id())) {
            continue;
        }

        auto source = ResolveBitCastSource(bridge.GetArg<Value>(0));
        if (source.Defined() && CanCoalesceLiveLow32View(
                                        lir_block, reg_alloc, bridge, source,
                                        use_end)) {
            reg_alloc->MapReference(source.Id(), bridge.Id());
            reg_alloc->MarkLow32CopyCoalesced(bridge.Id(), source.Id());
            continue;
        }
        if (TransferLow32ViewTail(lir_block, reg_alloc, bridge, source,
                                  use_end, callbacks)) {
            continue;
        }
        if (bridge.GetUses() != 1 || bridge.Id() >= use_end.size()) {
            continue;
        }

        auto wrapper_it = std::next(bridge_it);
        if (wrapper_it == list.end()) {
            continue;
        }
        auto& wrapper = *wrapper_it;
        if (wrapper.GetOp() != OpCode::ZeroExtend32To64 ||
            wrapper.GetArg<Value>(0).Def() != &bridge ||
            use_end[bridge.Id()] != wrapper.Id() ||
            reg_alloc->IsWidthChainCoalesced(wrapper.Id())) {
            continue;
        }

        if (!source.Defined() ||
            reg_alloc->ValueType(source) != backend::RegAlloc::GPR ||
            reg_alloc->ValueType(Value{&wrapper}) != backend::RegAlloc::GPR) {
            continue;
        }
        const u32 source_reg = reg_alloc->ValueGPR(source).id;
        if (source_reg == reg_alloc->ValueGPR(Value{&wrapper}).id) {
            continue;
        }

        auto wrapper_gprs = reg_alloc->DirtyGPR(wrapper.Id());
        auto wrapper_fprs = reg_alloc->DirtyFPR(wrapper.Id());
        auto checked_gprs = wrapper_gprs;
        checked_gprs.Mark(source_reg);
        reg_alloc->SetActiveRegs(wrapper.Id(), checked_gprs, wrapper_fprs);
        if (!callbacks.check_instr(callbacks.context, &bridge, 0, 0) ||
            !callbacks.check_instr(callbacks.context, &wrapper, 0, 0)) {
            reg_alloc->SetActiveRegs(wrapper.Id(), wrapper_gprs, wrapper_fprs);
            continue;
        }
        reg_alloc->MapReference(source.Id(), bridge.Id());
        reg_alloc->MarkLow32CopyCoalesced(bridge.Id(), source.Id());
    }
}

}  // namespace swift::runtime::ir
