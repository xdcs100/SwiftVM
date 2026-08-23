#include "translator.h"

#include <algorithm>

namespace swift::runtime::backend::arm64 {

namespace {

ir::Value ResolveBitCast(ir::Value value) {
    while (value.Defined() && value.Def()->IsBitCastOperation()) {
        value = value.Def()->GetArg<ir::Value>(0);
    }
    return value;
}

bool IsFusionBarrier(ir::OpCode op) {
    using O = ir::OpCode;
    switch (op) {
        case O::LoadMemory:
        case O::StoreMemory:
        case O::LoadMemoryTSO:
        case O::StoreMemoryTSO:
        case O::MemoryCopy:
        case O::MemoryCopyTSO:
        case O::CompareAndSwap:
        case O::CompareAndSwap128:
        case O::CheckMemoryAlignment:
        case O::AtomicExchange:
        case O::AtomicFetchAdd:
        case O::AtomicRMW:
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

}  // namespace

bool JitTranslator::ReproveScalarLoadFPRFusion(
        ir::Inst* load, const ScalarLoadFPRFusion& fusion) const {
    if (!load || load->GetOp() != ir::OpCode::LoadMemory ||
        load->ReturnType() != ir::ValueType::U64 ||
        !fusion.low_store || !fusion.high_store || !fusion.zero ||
        fusion.low_store->GetOp() != ir::OpCode::SetHostFPR ||
        fusion.high_store->GetOp() != ir::OpCode::SetHostFPR ||
        fusion.zero->GetOp() != ir::OpCode::LoadImm ||
        fusion.zero->ReturnType() != ir::ValueType::U64 ||
        fusion.zero->GetArg<ir::Imm>(0).Get() != 0 ||
        fusion.target < 16 || fusion.target > 31 ||
        fusion.low_store->GetArg<ir::Imm>(1).Get() != fusion.target ||
        fusion.high_store->GetArg<ir::Imm>(1).Get() != fusion.target ||
        fusion.low_store->GetArg<ir::Imm>(2).Get() != 0 ||
        fusion.high_store->GetArg<ir::Imm>(2).Get() != sizeof(u64) ||
        fusion.low_store->GetArg<ir::Value>(0).Def() != load ||
        fusion.high_store->GetArg<ir::Value>(0).Def() != fusion.zero ||
        load->GetUses(false) != 1 || fusion.zero->GetUses(false) != 1 ||
        load->Id() >= fusion.low_store->Id() ||
        fusion.low_store->Id() + 1 != fusion.high_store->Id()) {
        return false;
    }

    auto& list = cur_block->GetInstList();
    auto low_it = list.iterator_to(*fusion.low_store);
    auto high_it = low_it;
    ++high_it;
    if (high_it == list.end() || high_it.operator->() != fusion.high_store) {
        return false;
    }

    for (auto& scan : list) {
        if (scan.Id() <= load->Id() || scan.Id() > fusion.high_store->Id() ||
            &scan == fusion.low_store || &scan == fusion.high_store ||
            &scan == fusion.zero) {
            continue;
        }
        if (IsFusionBarrier(scan.GetOp()) ||
            (scan.GetOp() == ir::OpCode::GetHostFPR &&
             scan.GetArg<ir::Imm>(0).Get() == fusion.target) ||
            (scan.GetOp() == ir::OpCode::SetHostFPR &&
             scan.GetArg<ir::Imm>(1).Get() == fusion.target)) {
            return false;
        }
    }

    for (auto& value_inst : list) {
        if (!value_inst.HasValue() || value_inst.IsBitCastOperation()) {
            continue;
        }
        ir::Value value{&value_inst};
        if (!ir::IsFloatValueType(value.Type()) ||
            !context.IsFPRMappedTo(value, fusion.target)) {
            continue;
        }
        u32 end = value_inst.Id();
        u32 direct_uses = 0;
        for (auto& scan : list) {
            for (auto input : scan.GetValues()) {
                direct_uses += input.Def() == &value_inst;
                if (ResolveBitCast(input).Def() == &value_inst) {
                    end = std::max<u32>(end, scan.Id());
                }
            }
        }
        if (value_inst.GetUses(false) > direct_uses ||
            (value_inst.Id() <= fusion.high_store->Id() && end > load->Id())) {
            return false;
        }
    }
    return true;
}

void JitTranslator::PrepareScalarLoadFPRFusions(ir::Block* block) {
    scalar_load_fpr_fusions.clear();
    auto& list = block->GetInstList();
    for (auto low_it = list.begin(); low_it != list.end(); ++low_it) {
        auto* low = low_it.operator->();
        if (low->GetOp() != ir::OpCode::SetHostFPR ||
            low->GetArg<ir::Imm>(2).Get() != 0) {
            continue;
        }
        auto high_it = low_it;
        ++high_it;
        if (high_it == list.end()) {
            continue;
        }
        auto* high = high_it.operator->();
        const u32 target = low->GetArg<ir::Imm>(1).Get();
        if (high->GetOp() != ir::OpCode::SetHostFPR ||
            high->GetArg<ir::Imm>(1).Get() != target ||
            high->GetArg<ir::Imm>(2).Get() != sizeof(u64)) {
            continue;
        }
        auto* load = low->GetArg<ir::Value>(0).Def();
        auto* zero = high->GetArg<ir::Value>(0).Def();
        ScalarLoadFPRFusion fusion{
                .low_store = low,
                .high_store = high,
                .zero = zero,
                .target = static_cast<u16>(target),
        };
        if (!ReproveScalarLoadFPRFusion(load, fusion)) {
            continue;
        }
        scalar_load_fpr_fusions.emplace(load, fusion);
        disable_instructions.set(zero->Id());
        disable_instructions.set(low->Id());
        disable_instructions.set(high->Id());
    }
}

}  // namespace swift::runtime::backend::arm64
