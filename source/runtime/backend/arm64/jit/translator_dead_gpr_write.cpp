#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 index) {
    return index <= 9 || (index >= 19 && index <= 23) || index == 29;
}

bool IsFullGPRWrite(ir::Inst& inst) {
    if (inst.GetOp() != ir::OpCode::SetHostGPR ||
        inst.GetArg<ir::Imm>(2).Get() != 0) {
        return false;
    }
    const auto width = ir::GetValueSizeByte(inst.GetArg<ir::Value>(0).Type());
    return width == sizeof(u32) || width == sizeof(u64);
}

bool IsWriteBoundary(ir::OpCode op) {
    switch (op) {
        case ir::OpCode::SetLocation:
        case ir::OpCode::GetUniformAddress:
        case ir::OpCode::UniformBarrier:
        case ir::OpCode::Goto:
        case ir::OpCode::NotGoto:
        case ir::OpCode::BindLabel:
            return true;
        default:
            return false;
    }
}

ir::Inst* PublicationRoot(ir::Inst& store) {
    auto value = store.GetArg<ir::Value>(0);
    auto* root = value.Def();
    while (root &&
           (root->IsBitCastOperation() ||
            root->GetOp() == ir::OpCode::ZeroExtend32To64)) {
        value = root->GetArg<ir::Value>(0);
        root = value.Def();
    }
    return root;
}

}  // namespace

bool JitTranslator::IsDeadPinnedGPRWrite(ir::Inst* inst) const {
    if (!inst || inst->GetOp() != ir::OpCode::SetHostGPR ||
        context.IsHostWriteCoalesced(inst->Id())) {
        return false;
    }
    const u32 target = inst->GetArg<ir::Imm>(1).Get();
    if (!IsPinnedGPR(target)) {
        return false;
    }

    bool after = false;
    for (auto& scan : cur_block->GetInstList()) {
        if (&scan == inst) {
            after = true;
            continue;
        }
        if (!after) {
            continue;
        }
        if (scan.GetOp() == ir::OpCode::GetHostGPR &&
            scan.GetArg<ir::Imm>(0).Get() == target) {
            return false;
        }
        if (MayFaultOrObserve(scan.GetOp()) || IsWriteBoundary(scan.GetOp())) {
            return false;
        }
        if (scan.GetOp() == ir::OpCode::SetHostGPR &&
            scan.GetArg<ir::Imm>(1).Get() == target) {
            if (!IsFullGPRWrite(scan)) {
                return false;
            }
            if (!context.IsHostWriteCoalesced(scan.Id())) {
                return true;
            }
            auto* root = PublicationRoot(scan);
            return root && root->Id() > inst->Id();
        }
    }
    return false;
}

void JitTranslator::PrepareDeadPinnedGPRWrites(ir::Block* block) {
    dead_pinned_gpr_writes.clear();
    for (auto& inst : block->GetInstList()) {
        if (IsDeadPinnedGPRWrite(&inst)) {
            dead_pinned_gpr_writes.insert(&inst);
        }
    }
}

}  // namespace swift::runtime::backend::arm64
