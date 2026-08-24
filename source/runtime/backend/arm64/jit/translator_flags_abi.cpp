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
    __ Msr(NZCV, flags);
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
    if (TargetKillsIncomingFlags(block->GetStartLocation())) {
        context.MarkIncomingFlagsDiscarded(block->GetStartLocation().Value());
    } else {
        UnparkFlagsHot();
    }
    // Land before the entry counter so L2 visits match FLAGS=0 published
    // entries. Internal taken edges still target the post-counter label.
    __ B(context.GetCountedEntryLabel(block->GetStartLocation().Value()));
}

}  // namespace swift::runtime::backend::arm64
