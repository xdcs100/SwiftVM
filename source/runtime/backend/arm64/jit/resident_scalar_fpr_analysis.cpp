#include "resident_scalar_fpr_analysis.h"

#include <algorithm>
#include <array>

namespace swift::runtime::backend::arm64 {

namespace {

ir::Value ResolveBitCast(ir::Value value) {
    while (value.Defined() && value.Def()->IsBitCastOperation()) {
        value = value.Def()->GetArg<ir::Value>(0);
    }
    return value;
}

bool IsSelfXor(ir::Inst* inst) {
    if (!inst || inst->GetOp() != ir::OpCode::VecXor || inst->ReturnType() != ir::ValueType::V128) {
        return false;
    }
    const auto left = ResolveBitCast(inst->GetArg<ir::Value>(0));
    const auto right = ResolveBitCast(inst->GetArg<ir::Value>(1));
    return left.Defined() && right.Defined() && left.Def() == right.Def();
}

bool IsOpaqueBarrier(ir::OpCode op) {
    using O = ir::OpCode;
    switch (op) {
        case O::CallLambda:
        case O::CallLocation:
        case O::CallDynamic:
        case O::X87Op:
        case O::Sse42Str:
        case O::GetUniformAddress:
        case O::UniformBarrier:
        case O::Goto:
        case O::NotGoto:
        case O::BindLabel:
            return true;
        default:
            return false;
    }
}

bool IsConversion(ir::Inst* inst) {
    if (!inst || inst->GetOp() != ir::OpCode::VecFCvtIntToFloat) {
        return false;
    }
    const u32 dst_bits = inst->GetArg<ir::Imm>(2).Get();
    return (dst_bits == 32 && ir::GetValueSizeByte(inst->ReturnType()) == 4) ||
           (dst_bits == 64 && ir::GetValueSizeByte(inst->ReturnType()) == 8);
}

}  // namespace

void ResidentScalarFPRAnalysis::Analyze(ir::Block* block) {
    conversions.clear();
    memory_stores.clear();
    discarded.clear();
    AnalyzeConversions(block);
    AnalyzeMemoryStores(block);
}

void ResidentScalarFPRAnalysis::AnalyzeConversions(ir::Block* block) {
    std::array<bool, 32> zero_high{};
    for (auto& inst : block->GetInstList()) {
        if (IsOpaqueBarrier(inst.GetOp())) {
            zero_high.fill(false);
            continue;
        }
        if (inst.GetOp() != ir::OpCode::SetHostFPR) {
            continue;
        }

        const u32 target = inst.GetArg<ir::Imm>(1).Get();
        if (target < 16 || target >= zero_high.size()) {
            continue;
        }
        const u32 offset = inst.GetArg<ir::Imm>(2).Get();
        const auto value = inst.GetArg<ir::Value>(0);
        auto* producer = ResolveBitCast(value).Def();
        if (offset == 0 && value.Type() == ir::ValueType::V128 && IsSelfXor(producer)) {
            zero_high[target] = true;
            continue;
        }
        if (offset == 0 && value.Def() == producer && IsConversion(producer) && zero_high[target]) {
            bool export_result = producer->GetUses(false) > 1;
            if (export_result &&
                TryMapConversionStore(block, producer, &inst, static_cast<u16>(target))) {
                export_result = false;
            }
            conversions.emplace(producer,
                                ConversionPlan{
                                        .target = static_cast<u16>(target),
                                        .export_result = export_result,
                                });
            discarded.insert(&inst);
            continue;
        }
        zero_high[target] = false;
    }
}

void ResidentScalarFPRAnalysis::AnalyzeMemoryStores(ir::Block* block) {
    auto& list = block->GetInstList();
    for (auto& read : list) {
        if (read.GetOp() != ir::OpCode::GetHostFPR || read.GetArg<ir::Imm>(1).Get() != 0 ||
            ir::IsFloatValueType(read.ReturnType()) ||
            (ir::GetValueSizeByte(read.ReturnType()) != 4 &&
             ir::GetValueSizeByte(read.ReturnType()) != 8) ||
            read.GetUses(false) != 1) {
            continue;
        }
        const u32 target = read.GetArg<ir::Imm>(0).Get();
        if (target < 16 || target > 31) {
            continue;
        }

        for (auto& scan : list) {
            if (scan.Id() <= read.Id()) {
                continue;
            }
            if (IsOpaqueBarrier(scan.GetOp()) || (scan.GetOp() == ir::OpCode::SetHostFPR &&
                                                  scan.GetArg<ir::Imm>(1).Get() == target)) {
                break;
            }
            bool uses_read = false;
            for (const auto value : scan.GetValues()) {
                uses_read |= value.Def() == &read;
            }
            if (!uses_read) {
                continue;
            }
            if (scan.GetOp() == ir::OpCode::StoreMemory &&
                scan.GetArg<ir::Value>(1).Def() == &read) {
                memory_stores.emplace(&scan, static_cast<u16>(target));
                discarded.insert(&read);
            }
            break;
        }
    }
}

bool ResidentScalarFPRAnalysis::TryMapConversionStore(ir::Block* block,
                                                      ir::Inst* conversion,
                                                      ir::Inst* publication,
                                                      u16 target) {
    if (conversion->GetUses(false) != 2) {
        return false;
    }

    ir::Inst* store{};
    for (auto& inst : block->GetInstList()) {
        if (&inst == publication) {
            continue;
        }
        const bool uses_conversion = std::ranges::any_of(
                inst.GetValues(), [&](const ir::Value value) { return value.Def() == conversion; });
        if (!uses_conversion) {
            continue;
        }
        if (store || inst.GetOp() != ir::OpCode::StoreMemory ||
            inst.GetArg<ir::Value>(1).Def() != conversion) {
            return false;
        }
        store = &inst;
    }
    if (!store) {
        return false;
    }

    for (auto& inst : block->GetInstList()) {
        if (inst.Id() <= conversion->Id()) {
            continue;
        }
        if (&inst == publication) {
            continue;
        }
        if (IsOpaqueBarrier(inst.GetOp()) ||
            (inst.GetOp() == ir::OpCode::SetHostFPR && inst.GetArg<ir::Imm>(1).Get() == target)) {
            return false;
        }
        if (&inst == store) {
            memory_stores.emplace(store, target);
            return true;
        }
    }
    return false;
}

const ResidentScalarFPRAnalysis::ConversionPlan* ResidentScalarFPRAnalysis::FindConversion(
        ir::Inst* conversion) const {
    const auto it = conversions.find(conversion);
    return it == conversions.end() ? nullptr : &it->second;
}

std::optional<u16> ResidentScalarFPRAnalysis::FindMemoryStore(ir::Inst* store) const {
    const auto it = memory_stores.find(store);
    return it == memory_stores.end() ? std::nullopt : std::optional<u16>{it->second};
}

bool ResidentScalarFPRAnalysis::IsDiscarded(ir::Inst* inst) const {
    return discarded.contains(inst);
}

}  // namespace swift::runtime::backend::arm64
