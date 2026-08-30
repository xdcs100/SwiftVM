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
        if (MayFaultOrObserve(scan) || IsWriteBoundary(scan.GetOp())) {
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
    StackVector<GuestStateMap::CoalescedWrite, 8> coalesced_writes;
    StackVector<ir::Inst*, 8> published_versions;
    bool has_reused_publication = false;
    for (auto& inst : block->GetInstList()) {
        if (!has_reused_publication) {
            for (auto value : inst.GetValues()) {
                if (value.Def() &&
                    std::ranges::find(published_versions, value.Def()) !=
                            published_versions.end()) {
                    has_reused_publication = true;
                    break;
                }
            }
        }
        const bool dead = IsDeadPinnedGPRWrite(&inst);
        if (dead) {
            dead_pinned_gpr_writes.insert(&inst);
        }
        if (inst.GetOp() != ir::OpCode::SetHostGPR ||
            inst.GetArg<ir::Imm>(2).Get() != 0 || dead) {
            continue;
        }
        const u32 home = inst.GetArg<ir::Imm>(1).Get();
        if (!IsPinnedGPR(home)) {
            continue;
        }
        auto published = inst.GetArg<ir::Value>(0);
        if (!has_reused_publication && published.Def()) {
            auto* version = published.Def();
            published_versions.push_back(version);
            while (version &&
                   (version->GetOp() == ir::OpCode::ZeroExtend32 ||
                    version->GetOp() == ir::OpCode::ZeroExtend32To64 ||
                    version->GetOp() == ir::OpCode::SignExtend)) {
                auto alias = version->GetArg<ir::Value>(0);
                version = alias.Def();
                if (version) {
                    published_versions.push_back(version);
                }
            }
        }
        if (context.IsGPRMappedTo(published, home)) {
            coalesced_writes.push_back({&inst, static_cast<u16>(home)});
        }
    }
    guest_state_map.BuildValueVersions(
            dead_pinned_gpr_writes,
            std::span<const GuestStateMap::CoalescedWrite>{
                    coalesced_writes.data(), coalesced_writes.size()},
            has_reused_publication);
}

}  // namespace swift::runtime::backend::arm64
