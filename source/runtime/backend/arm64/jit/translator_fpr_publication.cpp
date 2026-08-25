#include "translator.h"

#include <algorithm>
#include <unordered_map>

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

bool JitTranslator::ReproveScalarFPRPublication(
        const ScalarFPRPublication& publication) const {
    if (!publication.low_store || !publication.high_store || !publication.zero ||
        publication.low_store->GetOp() != ir::OpCode::SetHostFPR ||
        publication.high_store->GetOp() != ir::OpCode::SetHostFPR ||
        publication.zero->GetOp() != ir::OpCode::LoadImm ||
        publication.zero->ReturnType() != ir::ValueType::U64 ||
        publication.zero->GetArg<ir::Imm>(0).Get() != 0 ||
        publication.target < 16 || publication.target > 31 ||
        publication.low_store->GetArg<ir::Imm>(1).Get() != publication.target ||
        publication.high_store->GetArg<ir::Imm>(1).Get() != publication.target ||
        publication.low_store->GetArg<ir::Imm>(2).Get() != 0 ||
        publication.high_store->GetArg<ir::Imm>(2).Get() != sizeof(u64) ||
        publication.high_store->GetArg<ir::Value>(0).Def() != publication.zero ||
        publication.low_store->Id() + 1 != publication.high_store->Id()) {
        return false;
    }
    auto& list = cur_block->GetInstList();
    auto high_it = list.iterator_to(*publication.low_store);
    ++high_it;
    return high_it != list.end() && high_it.operator->() == publication.high_store;
}

bool JitTranslator::ReproveScalarLoadFPRFusion(
        ir::Inst* load, const ScalarFPRPublication& fusion) const {
    if (!load || load->GetOp() != ir::OpCode::LoadMemory ||
        !ReproveScalarFPRPublication(fusion) || load->GetUses(false) != 1 ||
        load->Id() >= fusion.low_store->Id()) {
        return false;
    }
    if (fusion.load_extension) {
        if (load->ReturnType() != ir::ValueType::U32 ||
            fusion.load_extension->GetOp() != ir::OpCode::ZeroExtend64 ||
            fusion.load_extension->GetArg<ir::Value>(0).Def() != load ||
            fusion.load_extension->GetUses(false) != 1 ||
            fusion.low_store->GetArg<ir::Value>(0).Def() != fusion.load_extension ||
            load->Id() >= fusion.load_extension->Id() ||
            fusion.load_extension->Id() >= fusion.low_store->Id()) {
            return false;
        }
    } else if (load->ReturnType() != ir::ValueType::U64 ||
               fusion.low_store->GetArg<ir::Value>(0).Def() != load) {
        return false;
    }

    auto& list = cur_block->GetInstList();
    for (auto& scan : list) {
        if (scan.Id() <= load->Id() || scan.Id() > fusion.high_store->Id() ||
            &scan == fusion.low_store || &scan == fusion.high_store ||
            &scan == fusion.zero || &scan == fusion.load_extension) {
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

bool JitTranslator::ReproveScalarValueFPRFusion(
        ir::Inst* low_store, const ScalarFPRPublication& fusion) const {
    if (!low_store || low_store != fusion.low_store ||
        !ReproveScalarFPRPublication(fusion)) {
        return false;
    }
    const auto low = low_store->GetArg<ir::Value>(0);
    if (!low.Defined() || ir::IsFloatValueType(low.Type()) ||
        ir::GetValueSizeByte(low.Type()) != sizeof(u64)) {
        return false;
    }
    return true;
}

void JitTranslator::PrepareScalarFPRPublications(ir::Block* block) {
    scalar_load_fpr_fusions.clear();
    scalar_value_fpr_fusions.clear();
    std::unordered_map<ir::Inst*, u32> scalar_zero_uses;
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
        ir::Inst* load_extension = nullptr;
        if (load && load->GetOp() == ir::OpCode::ZeroExtend64) {
            load_extension = load;
            load = load->GetArg<ir::Value>(0).Def();
        }
        auto* zero = high->GetArg<ir::Value>(0).Def();
        ScalarFPRPublication fusion{
                .low_store = low,
                .high_store = high,
                .zero = zero,
                .load_extension = load_extension,
                .target = static_cast<u16>(target),
        };
        if (!ReproveScalarLoadFPRFusion(load, fusion)) {
            if (!ReproveScalarValueFPRFusion(low, fusion)) {
                continue;
            }
            scalar_value_fpr_fusions.emplace(low, fusion);
            ++scalar_zero_uses[zero];
            disable_instructions.set(high->Id());
            continue;
        }
        scalar_load_fpr_fusions.emplace(load, fusion);
        ++scalar_zero_uses[zero];
        if (load_extension) {
            disable_instructions.set(load_extension->Id());
        }
        disable_instructions.set(low->Id());
        disable_instructions.set(high->Id());
    }
    for (const auto& [zero, uses] : scalar_zero_uses) {
        if (zero->GetUses(false) == uses) {
            disable_instructions.set(zero->Id());
        }
    }
}

}  // namespace swift::runtime::backend::arm64
