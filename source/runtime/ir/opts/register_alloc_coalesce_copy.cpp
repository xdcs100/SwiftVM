#include "register_alloc_internal.h"

#include <iterator>

namespace swift::runtime::ir {

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
            bridge.GetUses() != 1 || bridge.Id() >= use_end.size() ||
            reg_alloc->IsWidthChainCoalesced(bridge.Id()) ||
            reg_alloc->IsLow32CopyCoalesced(bridge.Id())) {
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

        auto source = ResolveBitCastSource(bridge.GetArg<Value>(0));
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
