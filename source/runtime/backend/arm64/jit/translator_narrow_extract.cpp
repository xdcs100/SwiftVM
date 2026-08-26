#include "translator.h"

#include <iterator>

namespace swift::runtime::backend::arm64 {

namespace {

bool IsNarrowLoadZeroExtended(ir::Value value, u32 width) {
    for (u32 depth = 0; depth != 8 && value.Defined(); ++depth) {
        auto* definition = value.Def();
        while (definition && definition->IsBitCastOperation()) {
            value = definition->GetArg<ir::Value>(0);
            definition = value.Def();
        }
        if (!definition) {
            return false;
        }
        if (definition->GetOp() == ir::OpCode::LoadMemory ||
            definition->GetOp() == ir::OpCode::LoadUniform) {
            return ir::GetValueSizeByte(definition->ReturnType()) <= width;
        }
        if (definition->GetOp() != ir::OpCode::ZeroExtend32 &&
            definition->GetOp() != ir::OpCode::ZeroExtend32To64) {
            return false;
        }
        value = definition->GetArg<ir::Value>(0);
    }
    return false;
}

}  // namespace

std::optional<JitTranslator::NarrowExtractExtension>
JitTranslator::MatchNarrowExtractExtension(ir::Inst* wrapper) const {
    if (!wrapper || wrapper->GetOp() != ir::OpCode::ZeroExtend32) {
        return std::nullopt;
    }
    auto value = wrapper->GetArg<ir::Value>(0);
    auto* extract = value.Def();
    if (!extract || extract->GetOp() != ir::OpCode::BitExtract ||
        extract->GetArg<ir::Imm>(1).Get() != 0 ||
        extract->GetUses() != 1 || extract->GetUses(false) != 1 ||
        extract->GetArg<ir::Value>(0).Def() == nullptr ||
        fused_pin_gpr_reads.contains(extract) ||
        narrow_flags_inputs.contains(extract) ||
        context.IsWidthChainCoalesced(extract->Id()) ||
        context.IsWidthChainCoalesced(wrapper->Id()) ||
        context.IsLow32CopyCoalesced(extract->Id()) ||
        scalar_identity_analysis.InputDiscarded(extract)) {
        return std::nullopt;
    }
    const u32 width = ir::GetValueSizeByte(extract->ReturnType());
    if ((width != sizeof(u8) && width != sizeof(u16)) ||
        extract->GetArg<ir::Imm>(2).Get() != width * 8 ||
        ir::GetValueSizeByte(value.Type()) != width) {
        return std::nullopt;
    }
    auto& list = cur_block->GetInstList();
    auto next = std::next(list.iterator_to(*extract));
    if (next == list.end() || next.operator->() != wrapper) {
        return std::nullopt;
    }
    return NarrowExtractExtension{
            .extract = extract,
            .source = extract->GetArg<ir::Value>(0),
            .width = static_cast<u8>(width),
            .source_high_zero = IsNarrowLoadZeroExtended(
                    extract->GetArg<ir::Value>(0), width),
    };
}

void JitTranslator::PrepareNarrowExtractExtensions(ir::Block* block) {
    narrow_extract_extensions.clear();
    fused_narrow_extracts.clear();
    for (auto& inst : block->GetInstList()) {
        auto plan = MatchNarrowExtractExtension(&inst);
        if (!plan) {
            continue;
        }
        fused_narrow_extracts.emplace(plan->extract, &inst);
        narrow_extract_extensions.emplace(&inst, *plan);
    }
}

}  // namespace swift::runtime::backend::arm64
