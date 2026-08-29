#include "function_decode_frontier.h"

namespace swift::translator::x86 {

std::optional<FunctionDecodeFrontier::Split>
FunctionDecodeFrontier::FindSplit(runtime::LocationDescriptor target) const {
    if (!function || accepted.contains(target) || rejected.contains(target)) {
        return std::nullopt;
    }

    runtime::ir::HIRBlock* owner{};
    for (auto& hir_block : function->GetHIRBlockList()) {
        auto* block = hir_block.GetBlock();
        if (!block || block->GetInstList().empty()) {
            continue;
        }
        const auto start = block->GetStartLocation().Value();
        const auto end = DecodedEnd(block);
        if (target <= start || target >= end) {
            continue;
        }
        if (owner) {
            return std::nullopt;
        }
        owner = &hir_block;
    }
    return owner ? std::optional<Split>{Split{owner, target}} : std::nullopt;
}

bool FunctionDecodeFrontier::IsAccepted(
        runtime::LocationDescriptor target) const {
    return accepted.contains(target);
}

void FunctionDecodeFrontier::Accept(runtime::LocationDescriptor target) {
    accepted.insert(target);
}

void FunctionDecodeFrontier::Reject(runtime::LocationDescriptor target) {
    rejected.insert(target);
}

runtime::LocationDescriptor FunctionDecodeFrontier::DecodedEnd(
        const runtime::ir::Block* block) {
    auto end = block->GetStartLocation().Value();
    for (const auto& inst : block->GetInstList()) {
        if (inst.GetOp() == runtime::ir::OpCode::AdvancePC) {
            end += inst.GetArg<runtime::ir::Imm>(0).Get();
        }
    }
    return end;
}

}  // namespace swift::translator::x86
