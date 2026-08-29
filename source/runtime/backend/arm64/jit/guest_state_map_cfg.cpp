#include "guest_state_map.h"

#include <vector>

#include "runtime/backend/arm64/helper_call_contract.h"
#include "runtime/ir/hir_builder.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedGPR(u32 home) {
    return home <= 9 || (home >= 19 && home <= 23) || home == 29;
}

std::bitset<30> PinnedGPRMask() {
    std::bitset<30> mask;
    for (u32 home = 0; home < mask.size(); ++home) {
        mask.set(home, IsPinnedGPR(home));
    }
    return mask;
}

bool DefinitionKnownZeroAbove32(ir::Value value,
                                const std::bitset<30>& entry_facts) {
    if (!value.Def()) {
        return false;
    }
    const u32 width = ir::GetValueSizeByte(value.Type());
    if (width <= sizeof(u32)) {
        return true;
    }
    if (width != sizeof(u64)) {
        return false;
    }
    auto* definition = value.Def();
    switch (definition->GetOp()) {
        case ir::OpCode::ZeroExtend32:
        case ir::OpCode::ZeroExtend32To64:
            return true;
        case ir::OpCode::LoadImm:
            return definition->GetArg<ir::Imm>(0).Get() <= UINT32_MAX;
        case ir::OpCode::GetHostGPR: {
            const u32 home = definition->GetArg<ir::Imm>(0).Get();
            return definition->GetArg<ir::Imm>(1).Get() == 0 &&
                   home < entry_facts.size() && entry_facts.test(home);
        }
        default:
            if (definition->IsBitCastOperation()) {
                return DefinitionKnownZeroAbove32(
                        definition->GetArg<ir::Value>(0), entry_facts);
            }
            return false;
    }
}

}  // namespace

void GuestStateMap::AnalyzeFunction(ir::HIRFunction* function,
                                    const FeatureSet& next_features) {
    this->function = function;
    features = next_features;
    function_width_facts_ready = false;
    function_entry_width_facts.clear();
    block_entry_width_facts.reset();
}

void GuestStateMap::BuildFunctionWidthFacts() {
    if (function_width_facts_ready || !function) {
        return;
    }
    function_width_facts_ready = true;

    const auto tracked = PinnedGPRMask();
    std::unordered_set<const ir::HIRBlock*> roots;
    if (auto* entry = function->GetEntryBlock()) {
        roots.insert(entry);
    }
    for (auto* root : function->GetExternalEntryRoots()) {
        roots.insert(root);
    }

    std::vector<ir::HIRBlock*> blocks;
    for (auto& hir_block : function->GetHIRBlocksRPO()) {
        blocks.push_back(&hir_block);
        if (!hir_block.HasIncomingEdges() || hir_block.IsCallReturnBlock()) {
            roots.insert(&hir_block);
        }
        function_entry_width_facts.emplace(hir_block.GetBlock(),
                                           roots.contains(&hir_block)
                                                   ? WidthFacts{}
                                                   : tracked);
    }

    std::unordered_map<const ir::HIRBlock*, WidthFacts> exit_facts;
    for (auto* hir_block : blocks) {
        exit_facts.emplace(hir_block, tracked);
    }

    auto transfer = [&](ir::HIRBlock* hir_block, WidthFacts facts) {
        for (auto& inst : hir_block->GetBlock()->GetInstList()) {
            if (inst.GetOp() == ir::OpCode::SetHostGPR) {
                const u32 home = inst.GetArg<ir::Imm>(1).Get();
                if (!IsPinnedGPR(home)) {
                    continue;
                }
                const u32 offset = inst.GetArg<ir::Imm>(2).Get();
                const auto value = inst.GetArg<ir::Value>(0);
                const u32 width = ir::GetValueSizeByte(value.Type());
                if (offset == 0 && width == sizeof(u32)) {
                    facts.set(home);
                } else if (offset == 0 && width == sizeof(u64)) {
                    facts.set(home, DefinitionKnownZeroAbove32(value, facts));
                } else if (offset + width > sizeof(u32)) {
                    facts.reset(home);
                }
                continue;
            }

            const auto helper = HelperCallContract::Resolve(inst, features);
            if (helper) {
                for (u32 home = 0; home < facts.size(); ++home) {
                    if (IsPinnedGPR(home) && home <= 9 && helper->ClobbersGPR(home)) {
                        facts.reset(home);
                    }
                }
            } else if (inst.GetOp() == ir::OpCode::CallLambda ||
                       inst.GetOp() == ir::OpCode::CallLocation ||
                       inst.GetOp() == ir::OpCode::CallDynamic) {
                facts &= ~tracked;
            }
        }
        return facts;
    };

    bool changed;
    do {
        changed = false;
        for (auto* hir_block : blocks) {
            WidthFacts incoming = tracked;
            if (roots.contains(hir_block)) {
                incoming.reset();
            } else {
                for (auto* predecessor : hir_block->GetPredecessors()) {
                    const auto found = exit_facts.find(predecessor);
                    if (found == exit_facts.end()) {
                        incoming.reset();
                        break;
                    }
                    incoming &= found->second;
                }
            }

            auto* ir_block = hir_block->GetBlock();
            if (function_entry_width_facts[ir_block] != incoming) {
                function_entry_width_facts[ir_block] = incoming;
                changed = true;
            }
            const auto outgoing = transfer(hir_block, incoming);
            if (exit_facts[hir_block] != outgoing) {
                exit_facts[hir_block] = outgoing;
                changed = true;
            }
        }
    } while (changed);
}

void GuestStateMap::PrepareCurrentEntryWidthFacts(bool fault_snapshot_needed) {
    if (!function || !block || function_width_facts_ready) {
        return;
    }
    bool needed = fault_snapshot_needed;
    if (!needed) {
        for (const auto& publication : block->GetInstList()) {
            if (publication.GetOp() != ir::OpCode::SetHostGPR ||
                publication.GetArg<ir::Imm>(2).Get() != 0) {
                continue;
            }
            const auto value = publication.GetArg<ir::Value>(0);
            auto* definition = value.Def();
            needed = definition && value.Type() == ir::ValueType::U32 &&
                    definition->GetOp() == ir::OpCode::GetHostGPR &&
                    definition->GetArg<ir::Imm>(1).Get() == 0 &&
                    definition->GetArg<ir::Imm>(0).Get() ==
                            publication.GetArg<ir::Imm>(1).Get();
            if (needed) {
                break;
            }
        }
    }
    if (!needed) {
        return;
    }
    BuildFunctionWidthFacts();
    if (const auto found = function_entry_width_facts.find(block);
        found != function_entry_width_facts.end()) {
        block_entry_width_facts = found->second;
    }
}

bool GuestStateMap::EntryKnownZeroAbove32(const ir::Block* query_block,
                                          u32 home) {
    if (!query_block || home >= block_entry_width_facts.size()) {
        return false;
    }
    BuildFunctionWidthFacts();
    const auto found = function_entry_width_facts.find(query_block);
    return found != function_entry_width_facts.end() &&
           found->second.test(home);
}

}  // namespace swift::runtime::backend::arm64
