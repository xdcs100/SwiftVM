#include "translator.h"

#include "runtime/backend/arm64/defines.h"
#include "runtime/backend/context.h"
#include "runtime/common/svm_config.h"

namespace swift::runtime::backend::arm64 {

#define __ masm.

void JitTranslator::ParkFlagsHot() {
    if (!FlagsRegsEnabled()) {
        return;
    }
    __ Mrs(ip1, NZCV);
    __ Orr(ip1, ip1, 1u << kFlagsNzcvParkValidBit);
    __ Str(ip1, MemOperand(state, state_offset_flags_nzcv_park));
    __ Str(atomic_scratch, MemOperand(state, state_offset_flags_result_park));
}

void JitTranslator::UnparkFlagsHot() {
    if (!FlagsRegsEnabled()) {
        return;
    }
    Label from_x26;
    Label done;
    __ Ldr(ip1, MemOperand(state, state_offset_flags_nzcv_park));
    __ Tbz(ip1, kFlagsNzcvParkValidBit, &from_x26);
    __ Ldr(atomic_scratch, MemOperand(state, state_offset_flags_result_park));
    __ And(ip1, ip1, static_cast<u64>(HostFlags::NZCV));
    __ Msr(NZCV, ip1);
    __ B(&done);
    __ Bind(&from_x26);
    __ And(ip1, flags, static_cast<u64>(HostFlags::NZCV));
    __ Msr(NZCV, ip1);
    __ Bind(&done);
}

void JitTranslator::EmitFlagsPublishedVeneer(ir::Block* block) {
    if (!FlagsRegsEnabled() || !region_edges_active || !block) {
        return;
    }
    auto* published = context.GetLabel(block->GetStartLocation().Value());
    if (published->IsBound()) {
        return;
    }
    __ Bind(published);
    UnparkFlagsHot();
    __ B(context.GetInternalLabel(block->GetStartLocation().Value()));
}

}  // namespace swift::runtime::backend::arm64
