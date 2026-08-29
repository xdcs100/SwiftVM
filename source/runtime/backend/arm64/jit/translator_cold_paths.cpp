#include "translator.h"

#include <algorithm>

namespace swift::runtime::backend::arm64 {

JitTranslator::BlockColdPathPlan JitTranslator::CaptureBlockColdPathPlan(
        ir::Block* block,
        bool density,
        std::span<const u32> density_ops,
        std::span<const u32> density_bytes,
        u32 density_scalar_fp_ops,
        const ir::LoopHoistMetadata& loop_hoist,
        u32 loop_hoist_prefix_ops) {
    BlockColdPathPlan plan;
    plan.block = block;
    plan.density = density;
    ASSERT(density_ops.size() == plan.density_ops.size());
    ASSERT(density_bytes.size() == plan.density_bytes.size());
    std::copy(density_ops.begin(), density_ops.end(), plan.density_ops.begin());
    std::copy(density_bytes.begin(), density_bytes.end(),
              plan.density_bytes.begin());
    plan.density_scalar_fp_ops = density_scalar_fp_ops;
    plan.loop_hoist = &loop_hoist;
    plan.loop_hoist_prefix_ops = loop_hoist_prefix_ops;
    plan.pfaf_density_bytes = pfaf_density_bytes;
    pfaf_density_bytes.fill(0);
    plan.boundary_density_enabled = boundary_density_enabled;
    plan.boundary_terminal_link_bytes = boundary_terminal_link_bytes;
    plan.boundary_density_bytes = boundary_density_bytes;
    plan.boundary_density_mnemonics =
            std::move(boundary_density_mnemonics);
    plan.boundary_terminal_link_mnemonics =
            std::move(boundary_terminal_link_mnemonics);
    plan.boundary_terminal_link_ranges =
            std::move(boundary_terminal_link_ranges);
    plan.save_in_nzcv = save_in_nzcv;
    plan.nzcv_dirty = nzcv_dirty;
    plan.nzcv_requested = nzcv_requested;
    plan.flags_token_valid = flags_token_valid;
    plan.flags_token_result_code = flags_token_result_code;
    plan.flags_token_keep = flags_token_keep;
    plan.flags_audit_block_edge = flags_audit_block_edge;
    plan.flags_audit_strict_advance = flags_audit_strict_advance;
    plan.backedge_exit_label = std::move(backedge_exit_label);
    plan.backedge_exit_referenced = backedge_exit_referenced;
    plan.direct_cycle_exits = std::move(direct_cycle_exits);
    plan.direct_cycle_cut_edges = direct_cycle_cut_edges;
    plan.backedge_flags_plan = std::move(backedge_flags_plan);
    plan.loop_hoist_body_entry = std::move(loop_hoist_body_entry);
    plan.backedge_host_begin = backedge_host_begin;
    plan.backedge_host_end = backedge_host_end;
    plan.region_block_edges = region_block_edges;
    plan.region_block_cycles = region_block_cycles;
    plan.region_block_fallthroughs = region_block_fallthroughs;
    plan.region_block_local_branch_bytes = region_block_local_branch_bytes;
    plan.pending_exit_poll_faults = std::move(pending_exit_poll_faults);
    plan.vec_nan_cold_sites = std::move(vec_nan_cold_sites);
    plan.flags_audit = context.DeferFlagsRegsAudit();

    boundary_density_enabled = false;
    boundary_terminal_link_bytes = 0;
    boundary_density_bytes.fill(0);
    save_in_nzcv = true;
    nzcv_dirty = false;
    nzcv_requested = {};
    flags_token_keep = false;
    InvalidateFlagsToken();
    flags_audit_strict_advance = false;
    backedge_exit_referenced = false;
    direct_cycle_cut_edges = 0;
    backedge_host_begin = 0;
    backedge_host_end = 0;
    region_block_edges = 0;
    region_block_cycles = 0;
    region_block_fallthroughs = 0;
    region_block_local_branch_bytes = 0;
    return plan;
}

void JitTranslator::EmitBlockColdPathPlan(BlockColdPathPlan plan) {
    ASSERT(plan.block);
    ASSERT(plan.loop_hoist);
    ASSERT(!backedge_exit_label);
    ASSERT(direct_cycle_exits.empty());
    ASSERT(!backedge_flags_plan);
    ASSERT(!loop_hoist_body_entry);
    ASSERT(pending_exit_poll_faults.empty());
    ASSERT(vec_nan_cold_sites.empty());

    cur_block = plan.block;
    pfaf_density_bytes = plan.pfaf_density_bytes;
    boundary_density_enabled = plan.boundary_density_enabled;
    boundary_terminal_link_bytes = plan.boundary_terminal_link_bytes;
    boundary_density_bytes = plan.boundary_density_bytes;
    boundary_density_mnemonics =
            std::move(plan.boundary_density_mnemonics);
    boundary_terminal_link_mnemonics =
            std::move(plan.boundary_terminal_link_mnemonics);
    boundary_terminal_link_ranges =
            std::move(plan.boundary_terminal_link_ranges);
    save_in_nzcv = plan.save_in_nzcv;
    nzcv_dirty = plan.nzcv_dirty;
    nzcv_requested = plan.nzcv_requested;
    flags_token_valid = plan.flags_token_valid;
    flags_token_result_code = plan.flags_token_result_code;
    flags_token_keep = plan.flags_token_keep;
    flags_audit_block_edge = plan.flags_audit_block_edge;
    flags_audit_strict_advance = plan.flags_audit_strict_advance;
    backedge_exit_label = std::move(plan.backedge_exit_label);
    backedge_exit_referenced = plan.backedge_exit_referenced;
    direct_cycle_exits = std::move(plan.direct_cycle_exits);
    direct_cycle_cut_edges = plan.direct_cycle_cut_edges;
    backedge_flags_plan = std::move(plan.backedge_flags_plan);
    loop_hoist_body_entry = std::move(plan.loop_hoist_body_entry);
    backedge_host_begin = plan.backedge_host_begin;
    backedge_host_end = plan.backedge_host_end;
    region_block_edges = plan.region_block_edges;
    region_block_cycles = plan.region_block_cycles;
    region_block_fallthroughs = plan.region_block_fallthroughs;
    region_block_local_branch_bytes = plan.region_block_local_branch_bytes;
    pending_exit_poll_faults = std::move(plan.pending_exit_poll_faults);
    vec_nan_cold_sites = std::move(plan.vec_nan_cold_sites);
    if (plan.flags_audit) {
        context.ResumeFlagsRegsAudit(std::move(*plan.flags_audit));
    }

    context.BeginColdScratch();
    const u32 boundary_cold_before =
            plan.density ? context.CurrentBufferSize() : 0;
    const u32 flags_audit_cold_begin = context.FlagsRegsAuditEnabled()
            ? context.CurrentBufferSize()
            : 0;
    flags_audit_cold = context.FlagsRegsAuditEnabled();
    EmitBackedgeExitStub();
    flags_token_keep = false;
    InvalidateFlagsToken();
    EmitBackedgeColdPaths();
    if (backedge_exit_label) {
        ResolveExitPollFaults(backedge_exit_label.get(),
                              plan.block->GetStartLocation());
    }
    backedge_exit_label.reset();
    backedge_exit_referenced = false;
    EmitDirectCycleExitStubs();
    if (plan.density) {
        RecordBoundaryRange(BoundarySubsequence::ColdTail,
                            boundary_cold_before,
                            context.CurrentBufferSize());
        plan.density_bytes[static_cast<size_t>(DensityCategory::Boundary)] +=
                context.CurrentBufferSize() - boundary_cold_before;
    }
    const u32 nan_cold_before =
            plan.density ? context.CurrentBufferSize() : 0;
    EmitVecNaNColdPaths();
    if (plan.density) {
        plan.density_bytes[static_cast<size_t>(DensityCategory::NaN)] +=
                context.CurrentBufferSize() - nan_cold_before;
    }
    context.EndColdScratch();
    flags_audit_cold = false;
    if (context.FlagsRegsAuditEnabled()) {
        const u32 cold_bytes =
                context.CurrentBufferSize() - flags_audit_cold_begin;
        context.RecordFlagsRegsAudit(
                FlagsRegsAuditMergeCause::FaultVeneer,
                FlagsRegsAuditEdgeKind::Host,
                FlagsRegsAuditCost::RecoveryColdBytes,
                cold_bytes,
                cold_bytes != 0);
        context.FinishDeferredFlagsRegsAudit();
    }

    PrintBlockDensity(plan.block,
                      plan.density,
                      plan.density_ops,
                      plan.density_bytes,
                      plan.density_scalar_fp_ops,
                      *plan.loop_hoist,
                      plan.loop_hoist_prefix_ops);
    ASSERT(pending_exit_poll_faults.empty());
    ASSERT(vec_nan_cold_sites.empty());
    save_in_nzcv = true;
    nzcv_dirty = false;
    nzcv_requested = {};
    flags_token_keep = false;
    InvalidateFlagsToken();
}

}  // namespace swift::runtime::backend::arm64
