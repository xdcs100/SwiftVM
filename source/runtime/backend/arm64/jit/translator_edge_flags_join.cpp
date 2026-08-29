#include "translator.h"

#include "runtime/backend/arm64/defines.h"
#include "runtime/backend/context.h"

namespace swift::runtime::backend::arm64 {

#define __ masm.

bool JitTranslator::RegionSuccessorAcceptsEdgeFlags(
        ir::Location target,
        const EdgeFlagsState& incoming) const {
    const auto found = region_block_map.find(target.Value());
    auto* block = found == region_block_map.end() ? nullptr : found->second;
    if (!block) {
        return false;
    }
    HostFlags needed = static_cast<HostFlags>(incoming.valid_nzcv_mask);
    for (auto& inst : block->GetInstList()) {
        const auto op = inst.GetOp();
        if (op == ir::OpCode::GetFlags || op == ir::OpCode::CallLambda ||
            op == ir::OpCode::CallLocation || op == ir::OpCode::CallDynamic ||
            op == ir::OpCode::X87Op || op == ir::OpCode::TestFlags ||
            op == ir::OpCode::TestNotFlags || op == ir::OpCode::Adc ||
            op == ir::OpCode::Sbb || op == ir::OpCode::CondSelect ||
            op == ir::OpCode::CondSet) {
            return false;
        }
        if (op == ir::OpCode::ClearFlags) {
            needed &= static_cast<HostFlags>(
                    ~static_cast<u64>(GuestNZCVToHost(
                            inst.GetArg<ir::Flags>(0) & ir::Flags::NZCV)));
        } else if (op == ir::OpCode::BranchOnlyFlags) {
            return true;
        } else if (op == ir::OpCode::SaveFlags) {
            needed &= static_cast<HostFlags>(
                    ~static_cast<u64>(GuestNZCVToHost(
                            inst.GetArg<ir::Flags>(1))));
        }
        if (!True(needed)) {
            return true;
        }
    }
    return BlockIsFlagsTransparent(block) &&
           (incoming.valid_nzcv_mask == kEdgeNZCVMask ||
            incoming.valid_nzcv_mask == static_cast<u32>(HostFlags::NZ));
}

bool JitTranslator::RegionSuccessorOverwritesFlagsToken(
        ir::Location target) const {
    const auto found = region_block_map.find(target.Value());
    if (found == region_block_map.end() || !found->second) {
        return false;
    }
    ir::Flags needed = ir::Flags::Parity | ir::Flags::AuxiliaryCarry;
    for (auto& inst : found->second->GetInstList()) {
        if (MayFaultOrObserve(inst.GetOp())) {
            return false;
        }
        if (inst.GetOp() == ir::OpCode::GetFlags ||
            inst.GetOp() == ir::OpCode::TestFlags ||
            inst.GetOp() == ir::OpCode::TestNotFlags) {
            return false;
        }
        if (inst.GetOp() == ir::OpCode::SaveFlags) {
            needed &= ~inst.GetArg<ir::Flags>(1);
        } else if (inst.GetOp() == ir::OpCode::ClearFlags) {
            needed &= ~inst.GetArg<ir::Flags>(0);
        }
        if (!True(needed)) {
            return true;
        }
        if (inst.GetOp() == ir::OpCode::AdvancePC) {
            return false;
        }
    }
    return false;
}

JitTranslator::RegionFlagsJoinPlan JitTranslator::PlanRegionFlagsJoin(
        ir::Location then_target,
        ir::Location else_target,
        bool allow_fallthrough) const {
    const auto producer = save_in_nzcv && nzcv_dirty
            ? EdgeFlagsProducer::Arithmetic
            : EdgeFlagsProducer::Restore;
    const auto incoming = PendingEdgeFlagsState(nzcv_requested, producer);
    ASSERT(incoming.HasPendingPState());
    const bool then_accepts = RegionSuccessorAcceptsEdgeFlags(
            then_target, incoming);
    const bool else_accepts = RegionSuccessorAcceptsEdgeFlags(
            else_target, incoming);
    if (then_accepts == else_accepts) {
        return {
                .mode = then_accepts ? RegionFlagsJoinMode::Deferred
                                     : RegionFlagsJoinMode::Canonical,
        };
    }
    if (incoming.valid_nzcv_mask != kEdgeNZCVMask ||
        !context.CanUseRegionTrampoline()) {
        return {};
    }

    const auto compatible = then_accepts ? then_target : else_target;
    const auto canonical = then_accepts ? else_target : then_target;
    if (flags_token_valid &&
        !RegionSuccessorOverwritesFlagsToken(compatible)) {
        return {};
    }
    if (IsRegionCycleEdge(compatible) || IsDirectCycleCutEdge(compatible) ||
        IsRegionCycleEdge(canonical) || IsDirectCycleCutEdge(canonical)) {
        return {};
    }
    const bool compatible_fallthrough = allow_fallthrough &&
            CanUseRegionSuccessorLayout(compatible);
    const bool canonical_fallthrough = allow_fallthrough &&
            CanUseRegionSuccessorLayout(canonical) &&
            !compatible_fallthrough;
    return {
            .mode = canonical_fallthrough
                    ? RegionFlagsJoinMode::Split
                    : RegionFlagsJoinMode::CanonicalTail,
            .incoming = incoming,
            .compatible_target = compatible,
            .canonical_target = canonical,
            .compatible_on_true = then_accepts,
            .compatible_fallthrough = compatible_fallthrough,
            .canonical_fallthrough = canonical_fallthrough,
            .canonical_merge_token = flags_token_valid,
    };
}

Label* JitTranslator::GetRegionFlagsCanonicalStub(
        const RegionFlagsJoinPlan& plan) {
    RegionFlagsCanonicalStubKey key{
            .target = plan.canonical_target.Value(),
            .mask = plan.incoming.valid_nzcv_mask,
            .polarity = plan.incoming.carry_polarity,
            .version = plan.incoming.packed_flags_version,
            .token = plan.canonical_merge_token,
    };
    auto& entry = region_flags_canonical_stubs[key];
    if (!entry) {
        entry = std::make_unique<Label>();
    }
    return entry.get();
}

void JitTranslator::EmitRegionFlagsCanonicalStubs() {
    for (auto& [key, entry] : region_flags_canonical_stubs) {
        __ Bind(entry.get());
        __ Adr(ip1, LocalBranchTarget(ir::Location{key.target}));
        context.EmitFlagsMergeBranch(
                key.token ? FlagsMergeTrampolineKind::NZCVToken
                          : FlagsMergeTrampolineKind::NZCV);
    }
    region_flags_canonical_stubs.clear();
}

bool JitTranslator::EmitRegionFlagsJoin(
        const RegionFlagsJoinPlan& plan,
        const std::function<void(Label*, bool)>& branch) {
    if (plan.mode == RegionFlagsJoinMode::Canonical) {
        MergeNZCV(FlagsRegsAuditMergeCause::PStateClobber,
                  FlagsRegsAuditEdgeKind::RegionInternal);
        return false;
    }
    if (plan.mode == RegionFlagsJoinMode::Deferred) {
        PublishFlagsToken();
        return false;
    }

    if (plan.mode == RegionFlagsJoinMode::CanonicalTail) {
        if (plan.canonical_merge_token) {
            MaterializeFlagsTokenResult();
        }
        branch(GetRegionFlagsCanonicalStub(plan),
               !plan.compatible_on_true);
        context.RecordExecCounter(exec_offset_exit_direct);
        context.RecordExecCounter(exec_offset_region_edges);
        ++region_block_edges;
        EmitRegionEdge(plan.compatible_target,
                       plan.compatible_fallthrough,
                       false,
                       false);
        return true;
    }

    branch(LocalBranchTarget(plan.compatible_target),
           plan.compatible_on_true);
    context.RecordExecCounter(exec_offset_exit_direct);
    context.RecordExecCounter(exec_offset_region_edges);
    ++region_block_edges;
    (void)EmitOutlinedNZCVMergeResume(plan.canonical_merge_token);
    EmitRegionEdge(plan.canonical_target,
                   plan.canonical_fallthrough,
                   false,
                   false);
    return true;
}

#undef __

}  // namespace swift::runtime::backend::arm64
