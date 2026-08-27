#include "translator.h"
#include "runtime/backend/context.h"
#include "runtime/backend/arm64/defines.h"
#include "runtime/common/svm_config.h"

#include <functional>
#include <iterator>
#include <type_traits>

namespace swift::runtime::backend::arm64 {

#define __ masm.

void JitTranslator::EmitTerminal(const ir::Terminal& terminal,
                                 LinkSiteKind direct_link_kind,
                                 DirectLinkFlagsBypass flags_bypass) {
    VisitVariant<void>(terminal, [this, direct_link_kind, flags_bypass](auto term) {
        using T = std::decay_t<decltype(term)>;
        if constexpr (!std::is_same_v<T, ir::terminal::Invalid> &&
                      !std::is_same_v<T, ir::terminal::ReturnToDispatch>) {
            PublishPendingStaticLocation();
        }
        if constexpr (std::is_same_v<T, ir::terminal::Invalid>) {
            // Flat decoded blocks have no explicit terminal; their trailing
            // SetLocation supplies the next dispatch location.
            constexpr auto merge_cause =
                    FlagsRegsAuditMergeCause::TerminalDispatcher;
            const bool pending_call_flags =
                    CanUseIndirectCallContinuation() &&
                    CanDeferFullNZCVMerge(merge_cause);
            const bool call_continuation = CanUseCallContinuation();
            const auto local_flags_bypass = pending_call_flags
                    ? DirectLinkFlagsBypass{}
                    : MergeNZCV(merge_cause,
                                FlagsRegsAuditEdgeKind::Dispatcher,
                                context.ContinuationActive() && static_next_loc &&
                                        context.CanEmitDirectLink(
                                                ir::Location{*static_next_loc}));
            context.RecordExecCounter(static_next_loc ? exec_offset_exit_direct
                                                      : exec_offset_exit_indirect);
            if (!EmitStaticForward(
                        call_continuation ? LinkSiteKind::Call
                                          : direct_link_kind,
                        local_flags_bypass.Valid() ? local_flags_bypass
                                                   : flags_bypass) &&
                !(CanUseIndirectCallContinuation()
                          ? EmitIndirectCallForward(pending_call_flags)
                          : EmitIndirectForward())) {
                context.ReturnHost();
            }
        } else if constexpr (std::is_same_v<T, ir::terminal::ReturnToDispatch>) {
            constexpr auto merge_cause =
                    FlagsRegsAuditMergeCause::TerminalDispatcher;
            const bool pending_call_flags =
                    CanUseIndirectCallContinuation() &&
                    CanDeferFullNZCVMerge(merge_cause);
            const bool call_continuation = CanUseCallContinuation();
            const auto local_flags_bypass = pending_call_flags
                    ? DirectLinkFlagsBypass{}
                    : MergeNZCV(merge_cause,
                                FlagsRegsAuditEdgeKind::Dispatcher,
                                context.ContinuationActive() && static_next_loc &&
                                        context.CanEmitDirectLink(
                                                ir::Location{*static_next_loc}));
            context.RecordExecCounter(
                    cur_block_is_call ? exec_offset_exit_call
                                      : (static_next_loc ? exec_offset_exit_direct
                                                         : exec_offset_exit_indirect));
            if (!EmitStaticForward(
                        call_continuation ? LinkSiteKind::Call
                                          : direct_link_kind,
                        local_flags_bypass.Valid() ? local_flags_bypass
                                                   : flags_bypass) &&
                !(CanUseIndirectCallContinuation()
                          ? EmitIndirectCallForward(pending_call_flags)
                          : EmitIndirectForward())) {
                context.ReturnHost();
            }
        } else if constexpr (std::is_same_v<T, ir::terminal::ReturnToHost>) {
            MergeNZCV(FlagsRegsAuditMergeCause::HostExit,
                      FlagsRegsAuditEdgeKind::Host);
            context.RecordExecCounter(exec_offset_exit_syscall);
            __ Mov(ipw, static_cast<u32>(HaltReason::CallHost));
            __ Str(ipw, MemOperand(state, state_offset_halt_reason));
            context.ReturnHost();
        } else if constexpr (std::is_same_v<T, ir::terminal::LinkBlock>) {
            if (IsRegionInternalEdge(term.next)) {
                EmitRegionEdge(term.next);
                return;
            }
            const auto local_flags_bypass = MergeNZCV(
                    flags_audit_block_edge ==
                                    FlagsRegsAuditEdgeKind::Dispatcher
                            ? FlagsRegsAuditMergeCause::TerminalDispatcher
                            : FlagsRegsAuditMergeCause::TerminalInternal,
                    flags_audit_block_edge);
            context.RecordExecCounter(exec_offset_exit_direct);
            auto* exit = IsSelfEdge(term.next) && backedge_exit_label
                    ? backedge_exit_label.get()
                    : GetDirectCycleExit(term.next);
            backedge_exit_referenced |=
                    exit && exit == backedge_exit_label.get();
            auto* self_target = IsSelfEdge(term.next) &&
                                        (backedge_flags_plan || loop_hoist_body_entry)
                    ? LocalBranchTarget(term.next)
                    : nullptr;
            const u32 link_before = context.CurrentBufferSize();
            RecordExitPollFault(
                    context.Forward(term.next,
                                    exit,
                                    self_target,
                                    direct_link_kind,
                                    local_flags_bypass.Valid() ? local_flags_bypass
                                                              : flags_bypass),
                    exit);
            RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                                context.CurrentBufferSize());
        } else if constexpr (std::is_same_v<T, ir::terminal::LinkBlockFast>) {
            if (IsRegionInternalEdge(term.next)) {
                EmitRegionEdge(term.next);
                return;
            }
            const auto local_flags_bypass = MergeNZCV(
                    flags_audit_block_edge ==
                                    FlagsRegsAuditEdgeKind::Dispatcher
                            ? FlagsRegsAuditMergeCause::TerminalDispatcher
                            : FlagsRegsAuditMergeCause::TerminalInternal,
                    flags_audit_block_edge);
            context.RecordExecCounter(exec_offset_exit_direct);
            auto* exit = IsSelfEdge(term.next) && backedge_exit_label
                    ? backedge_exit_label.get()
                    : GetDirectCycleExit(term.next);
            backedge_exit_referenced |=
                    exit && exit == backedge_exit_label.get();
            auto* self_target = IsSelfEdge(term.next) &&
                                        (backedge_flags_plan || loop_hoist_body_entry)
                    ? LocalBranchTarget(term.next)
                    : nullptr;
            const u32 link_before = context.CurrentBufferSize();
            RecordExitPollFault(
                    context.Forward(term.next,
                                    exit,
                                    self_target,
                                    direct_link_kind,
                                    local_flags_bypass.Valid() ? local_flags_bypass
                                                              : flags_bypass),
                    exit);
            RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                                context.CurrentBufferSize());
        } else if constexpr (std::is_same_v<T, ir::terminal::PopRSBHint>) {
            // A retained return target uses the inline L1 path. Without that
            // target an L1 module returns to the dispatcher; only modules that
            // disable L1 consume RSB frames.
            const bool l1_enabled = context.GetFeatures().indirect_l1;
            const bool inline_l1_return = l1_enabled && dynamic_next_loc.has_value();
            MergeNZCV(l1_enabled && !inline_l1_return
                              ? FlagsRegsAuditMergeCause::TerminalDispatcher
                              : FlagsRegsAuditMergeCause::TerminalInternal,
                      l1_enabled ? FlagsRegsAuditEdgeKind::Dispatcher
                                 : FlagsRegsAuditEdgeKind::RSBMiss);
            context.RecordExecCounter(exec_offset_exit_ret);
            if (l1_enabled) {
                if (inline_l1_return) {
                    const bool emitted = context.ContinuationActive()
                            ? EmitContinuationForward()
                            : EmitIndirectForward();
                    ASSERT(emitted);
                } else {
                    context.ReturnHost();
                }
                return;
            }
            if (True(context.GetConfig().global_opts & Optimizations::ReturnStackBuffer)) {
                const u32 link_before = context.CurrentBufferSize();
                const auto actual_target = dynamic_next_loc
                        ? std::optional{context.X(*dynamic_next_loc)}
                        : std::nullopt;
                dynamic_next_loc.reset();
                dynamic_location_miss = nullptr;
                context.EmitRSBPop(actual_target);
                RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                                    context.CurrentBufferSize());
            } else {
                context.ReturnHost();
            }
        } else if constexpr (std::is_same_v<T, ir::terminal::If>) {
            if (EmitRegionIf(term,
                             direct_link_kind == LinkSiteKind::Unconditional)) {
                return;
            }
            // One commit for both arms. MergeNZCV does not clobber host NZCV,
            // so a local b.cond can still read the cmp. Publishing per arm
            // doubled the AdvancePC merge we just removed.
            const auto local_flags_bypass = MergeNZCV(
                    FlagsRegsAuditMergeCause::TerminalDispatcher,
                    flags_audit_block_edge);
            const auto branch_flags_bypass =
                    CanBypassTerminalFlagsMerge(term.then_) &&
                                    CanBypassTerminalFlagsMerge(term.else_)
                            ? (local_flags_bypass.Valid() ? local_flags_bypass
                                                         : flags_bypass)
                            : DirectLinkFlagsBypass{};
            nzcv_dirty = false;
            nzcv_requested = {};
            InvalidateFlagsToken();
            Label else_label;
            if (!EmitDeadEdgeZeroBranch(term.cond, &else_label, false)) {
                if (auto local = LocalConditionFor(term.cond)) {
                    __ B(&else_label,
                         static_cast<Condition>(static_cast<u8>(*local) ^ 1));
                } else {
                    __ Cbz(context.W(term.cond), &else_label);
                }
            }
            EmitTerminal(term.then_,
                         LinkSiteKind::ConditionalThen,
                         branch_flags_bypass);
            __ Bind(&else_label);
            EmitTerminal(term.else_,
                         LinkSiteKind::ConditionalElse,
                         branch_flags_bypass);
        } else if constexpr (std::is_same_v<T, ir::terminal::Condition>) {
            if (EmitRegionCondition(
                        term,
                        direct_link_kind == LinkSiteKind::Unconditional)) {
                return;
            }
            DirectLinkFlagsBypass branch_flags_bypass{};
            if (save_in_nzcv && nzcv_dirty) {
                const auto local_flags_bypass = MergeNZCV(
                        FlagsRegsAuditMergeCause::TerminalDispatcher,
                        flags_audit_block_edge);
                if (CanBypassTerminalFlagsMerge(term.then_) &&
                    CanBypassTerminalFlagsMerge(term.else_)) {
                    branch_flags_bypass = local_flags_bypass;
                }
            } else {
                LoadNZCVFromFlags();
            }
            nzcv_dirty = false;
            nzcv_requested = {};
            InvalidateFlagsToken();
            Label else_label;
            auto host_cond = MapCond(term.cond);
            __ B(&else_label, static_cast<Condition>(static_cast<u8>(host_cond) ^ 1));
            EmitTerminal(term.then_,
                         LinkSiteKind::ConditionalThen,
                         branch_flags_bypass);
            __ Bind(&else_label);
            EmitTerminal(term.else_,
                         LinkSiteKind::ConditionalElse,
                         branch_flags_bypass);
        } else if constexpr (std::is_same_v<T, ir::terminal::Switch>) {
            // Linear compare chain; each arm ends with its own terminal.
            MergeNZCV(FlagsRegsAuditMergeCause::PStateClobber,
                      flags_audit_block_edge);
            // Cmp below clobbers host NZCV. Commit is done; do not let
            // terminal keep re-merge the switch key into x26.
            nzcv_dirty = false;
            nzcv_requested = {};
            auto value = context.R(term.value);
            for (auto& case_ : term.cases) {
                Label next_case;
                __ Mov(ip, case_.case_value.Get());
                __ Cmp(value, ip);
                __ B(&next_case, ne);
                EmitTerminal(case_.then, LinkSiteKind::SwitchArm);
                __ Bind(&next_case);
            }
            // No case matched: bail out to the dispatcher.
            context.RecordExecCounter(exec_offset_exit_indirect);
            context.ReturnHost();
        } else if constexpr (std::is_same_v<T, ir::terminal::CheckHalt>) {
            Label no_halt;
            __ Ldr(ipw, MemOperand(state, state_offset_halt_reason));
            __ Cbz(ipw, &no_halt);
            MergeNZCV(FlagsRegsAuditMergeCause::HostExit,
                      FlagsRegsAuditEdgeKind::Host);
            context.ReturnHost();
            __ Bind(&no_halt);
            EmitTerminal(term.else_, LinkSiteKind::CheckHalt);
        } else {
            PANIC("Unknown terminal!");
        }
    });
}

bool JitTranslator::CanBypassTerminalFlagsMerge(
        const ir::Terminal& terminal) const {
    return VisitVariant<bool>(terminal, [this](const auto& term) {
        using T = std::decay_t<decltype(term)>;
        if constexpr (std::is_same_v<T, ir::terminal::LinkBlock> ||
                      std::is_same_v<T, ir::terminal::LinkBlockFast>) {
            return !IsRegionInternalEdge(term.next) &&
                   context.CanEmitDirectLink(term.next);
        } else if constexpr (std::is_same_v<T, ir::terminal::If>) {
            return CanBypassTerminalFlagsMerge(term.then_) &&
                   CanBypassTerminalFlagsMerge(term.else_);
        } else {
            return false;
        }
    });
}

std::optional<Condition> JitTranslator::LocalConditionFor(ir::Value value) const {
    if (!value.Def()) {
        return std::nullopt;
    }
    if (auto it = local_conditions.find(value.Def()); it != local_conditions.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool JitTranslator::LaterNeedsHostPstate(ir::Inst* from) const {
    if (!local_conditions.empty()) {
        return true;
    }
    bool seen = false;
    for (auto& inst : cur_block->GetInstList()) {
        if (!seen) {
            seen = &inst == from;
            continue;
        }
        switch (inst.GetOp()) {
            case ir::OpCode::CondSet:
            case ir::OpCode::CondSelect:
            case ir::OpCode::LocalCondSet:
            case ir::OpCode::FCmpCondSet:
            case ir::OpCode::InvertCarry:
            case ir::OpCode::Adc:
            case ir::OpCode::Sbb:
            case ir::OpCode::TestFlags:
            case ir::OpCode::TestNotFlags:
            case ir::OpCode::VecFCmp:
            case ir::OpCode::PublishFCmpFlags:
                return true;
            default:
                break;
        }
    }
    bool needs = false;
    std::function<void(const ir::Terminal&)> visit = [&](const ir::Terminal& terminal) {
        VisitVariant<void>(terminal, [&](auto term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, ir::terminal::Condition>) {
                needs = true;
            } else if constexpr (std::is_same_v<T, ir::terminal::If>) {
                visit(term.then_);
                visit(term.else_);
            } else if constexpr (std::is_same_v<T, ir::terminal::CheckHalt>) {
                visit(term.else_);
            } else if constexpr (std::is_same_v<T, ir::terminal::Switch>) {
                for (const auto& arm : term.cases) {
                    visit(arm.then);
                }
            }
        });
    };
    visit(cur_block->GetTerminal());
    return needs;
}

bool JitTranslator::IsCompactFCmp(ir::Value value) {
    return value.Def() && value.Def()->GetOp() == ir::OpCode::VecFCmp &&
           value.Def()->GetArg<ir::Imm>(3).Get() != 0;
}

bool JitTranslator::RecordLocalCondition(ir::Inst* inst, ir::Cond cond) {
    if (inst->GetUses() != 1) {
        return false;
    }
    auto& list = cur_block->GetInstList();
    for (auto it = std::next(list.iterator_to(*inst)); it != list.end(); ++it) {
        bool names = false;
        for (auto value : it->GetValues()) {
            names = names || value.Def() == inst;
        }
        if (!names) {
            continue;
        }
        const bool supported =
                (it->GetOp() == ir::OpCode::Goto ||
                 it->GetOp() == ir::OpCode::NotGoto) &&
                        it->GetArg<ir::Value>(0).Def() == inst ||
                it->GetOp() == ir::OpCode::Select &&
                        it->GetArg<ir::Value>(0).Def() == inst;
        if (!supported) {
            return false;
        }
        local_conditions.emplace(inst, MapCond(cond));
        return true;
    }

    bool terminal_use = false;
    std::function<void(const ir::Terminal&)> visit = [&](const ir::Terminal& terminal) {
        VisitVariant<void>(terminal, [&](auto term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, ir::terminal::If>) {
                if (term.cond.Def() == inst) {
                    terminal_use = true;
                }
                visit(term.then_);
                visit(term.else_);
            } else if constexpr (std::is_same_v<T, ir::terminal::Condition>) {
                visit(term.then_);
                visit(term.else_);
            } else if constexpr (std::is_same_v<T, ir::terminal::CheckHalt>) {
                visit(term.else_);
            } else if constexpr (std::is_same_v<T, ir::terminal::Switch>) {
                for (const auto& arm : term.cases) {
                    visit(arm.then);
                }
            }
        });
    };
    visit(cur_block->GetTerminal());
    if (terminal_use) {
        local_conditions.emplace(inst, MapCond(cond));
    }
    return terminal_use;
}

// A direct jmp/call decodes to SetLocation(imm) + ReturnToDispatcher, and the
// trampoline then re-reads state->current_loc and walks the L1 hash chain for
// a target that was already known when the code was emitted. The dispatch
// table indexed here is the same one the RSB pop and JitContext::Forward's
// BlockLink path already branch through, with the same safety property: SMC
// invalidation (SmcTracker::ClearDispatchSlots) zeroes the slot, so a stale
// translation degrades to the Cbz fallback rather than to a wild branch.
bool JitTranslator::EmitStaticForward(LinkSiteKind direct_link_kind,
                                      DirectLinkFlagsBypass flags_bypass) {
    if (!static_next_loc) {
        return false;
    }
    const u64 target = *static_next_loc;
    const u32 link_before = context.CurrentBufferSize();
    const auto location = ir::Location{target};
    auto* cycle_exit = GetDirectCycleExit(location);
    const auto forwarded = context.ForwardStatic(
            location, cycle_exit, direct_link_kind, flags_bypass);
    RecordExitPollFault(forwarded.poll_fault, cycle_exit);
    if (forwarded.emitted) {
        static_next_loc.reset();
    } else {
        PublishPendingStaticLocation();
    }
    RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                        context.CurrentBufferSize());
    return forwarded.emitted;
}

bool JitTranslator::EmitIndirectForward() {
    if (!context.GetFeatures().indirect_l1 || !dynamic_next_loc) {
        return false;
    }
    const auto location = context.X(*dynamic_next_loc);
    dynamic_next_loc.reset();
    auto* miss = dynamic_location_miss;
    dynamic_location_miss = nullptr;
    const u32 link_before = context.CurrentBufferSize();
    const auto fault = context.ForwardIndirectL1(location, miss);
    fault_metadata.push_back({
            .guest_start = cur_block->GetStartLocation().Value(),
            .host_begin = fault.begin,
            .host_end = fault.end,
            .recovery_reg = miss ? location.GetCode() : UINT32_MAX,
    });
    RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                        context.CurrentBufferSize());
    return true;
}

bool JitTranslator::EmitContinuationForward() {
    if (!context.ContinuationActive() || !dynamic_next_loc) {
        return false;
    }
    const auto location = context.X(*dynamic_next_loc);
    dynamic_next_loc.reset();
    auto& miss_site = indirect_exit_miss_sites[location.GetCode()];
    if (!miss_site.label) {
        miss_site.label = std::make_unique<Label>();
        miss_site.guest_start = cur_block->GetStartLocation().Value();
    }
    dynamic_location_miss = nullptr;
    const u32 link_before = context.CurrentBufferSize();
    context.ForwardContinuation(location, miss_site.label.get());
    RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                        context.CurrentBufferSize());
    return true;
}

bool JitTranslator::CanUseCallContinuation() const {
    return context.ContinuationActive() && call_return_value && call_return_pc &&
           static_next_loc && next_region_block == call_return_pc &&
           context.IsGPRMappedTo(*call_return_value, 14) &&
           !backedge_exit_label && direct_cycle_exits.empty() &&
           !backedge_flags_plan && vec_nan_cold_sites.empty();
}

bool JitTranslator::CanUseIndirectCallContinuation() const {
    return context.ContinuationActive() && call_return_value && call_return_pc &&
           dynamic_next_loc && next_region_block == call_return_pc &&
           context.IsGPRMappedTo(*call_return_value, 14) &&
           !backedge_exit_label && direct_cycle_exits.empty() &&
           !backedge_flags_plan && vec_nan_cold_sites.empty();
}

bool JitTranslator::EmitIndirectCallForward(bool pending_flags) {
    if (!CanUseIndirectCallContinuation()) {
        return false;
    }
    const auto location = context.X(*dynamic_next_loc);
    dynamic_next_loc.reset();
    auto& miss_site = (pending_flags ? pending_call_miss_sites
                                     : indirect_exit_miss_sites)[location.GetCode()];
    if (!miss_site.label) {
        miss_site.label = std::make_unique<Label>();
        miss_site.guest_start = cur_block->GetStartLocation().Value();
    }
    const u32 link_before = context.CurrentBufferSize();
    const auto fault = context.ForwardIndirectCall(
            location, miss_site.label.get(), pending_flags);
    fault_metadata.push_back({
            .guest_start = cur_block->GetStartLocation().Value(),
            .host_begin = fault.begin,
            .host_end = fault.end,
            .recovery_reg = dynamic_location_miss ? location.GetCode()
                                                  : UINT32_MAX,
    });
    if (pending_flags && !flags_token_keep) {
        nzcv_dirty = false;
        nzcv_requested = {};
    }
    dynamic_location_miss = nullptr;
    RecordBoundaryRange(BoundarySubsequence::LinkTail, link_before,
                        context.CurrentBufferSize());
    return true;
}

void JitTranslator::EmitIndirectExitColdPaths() {
    for (u32 reg = 0; reg < pending_call_miss_sites.size(); ++reg) {
        auto& site = pending_call_miss_sites[reg];
        if (!site.label) {
            continue;
        }
        __ Bind(site.label.get());
        const XRegister location{reg};
        const XRegister scratch = location == ip0 ? ip1 : ip0;
        EmitNZCVMerge(static_cast<u64>(HostFlags::NZCV),
                      scratch);
        auto* recovery = terminal_location_publication.MissLabel(location);
        ASSERT(recovery);
        __ B(recovery);
        site = {};
    }
    for (u32 reg = 0; reg < indirect_exit_miss_sites.size(); ++reg) {
        auto& site = indirect_exit_miss_sites[reg];
        if (!site.label) {
            continue;
        }
        __ Bind(site.label.get());
        const XRegister location{reg};
        auto* recovery = terminal_location_publication.MissLabel(location);
        const auto fault = context.ForwardIndirectL1(location, recovery);
        fault_metadata.push_back({
                .guest_start = site.guest_start,
                .host_begin = fault.begin,
                .host_end = fault.end,
                .recovery_reg = recovery ? reg : UINT32_MAX,
        });
        site = {};
    }
}

Condition JitTranslator::MapCond(ir::Cond cond) {
    // ir::Cond values match the ARM condition encoding.
    return static_cast<Condition>(static_cast<u8>(cond) & 0xF);
}


#undef __

}  // namespace swift::runtime::backend::arm64
