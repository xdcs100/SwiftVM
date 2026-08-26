#include "translator.h"

namespace swift::runtime::backend::arm64 {

namespace {

bool IsPinnedMemoryTarget(u32 target) {
    return target <= 9 || target == 19 || target == 20 || target == 21 ||
           target == 22 || target == 23 || target == 29;
}

bool IsCallerSavedAddressBarrier(ir::OpCode op) {
    using O = ir::OpCode;
    return op == O::CallLambda || op == O::CallLocation ||
           op == O::CallDynamic || op == O::X87Op || op == O::Sse42Str;
}

}  // namespace

std::optional<u16> JitTranslator::MatchPinnedMemoryAddress(ir::Inst* address) const {
    if (!address || address->GetOp() != ir::OpCode::GetOperand ||
        address->GetUses(false) != 1) {
        return std::nullopt;
    }
    const auto operand = address->GetArg<ir::Operand>(0);
    if (!operand.GetRight().Null() || !operand.GetLeft().IsValue()) {
        return std::nullopt;
    }
    const auto source = operand.GetLeft().value;
    auto* read = source.Def();
    if (!read || read->GetOp() != ir::OpCode::GetHostGPR ||
        read->GetArg<ir::Imm>(1).Get() != 0 ||
        ir::GetValueSizeByte(source.Type()) != sizeof(u64)) {
        return std::nullopt;
    }
    const u32 target = read->GetArg<ir::Imm>(0).Get();
    if (!IsPinnedMemoryTarget(target) || context.X(source).GetCode() != target) {
        return std::nullopt;
    }

    ir::Inst* memory = nullptr;
    for (auto& scan : cur_block->GetInstList()) {
        if (scan.Id() <= address->Id() ||
            (scan.GetOp() != ir::OpCode::LoadMemory &&
             scan.GetOp() != ir::OpCode::StoreMemory)) {
            continue;
        }
        const auto memory_operand = scan.GetArg<ir::Operand>(0);
        if (memory_operand.GetRight().Null() &&
            memory_operand.GetLeft().IsValue() &&
            memory_operand.GetLeft().value.Def() == address) {
            memory = &scan;
            break;
        }
    }
    if (!memory) {
        return std::nullopt;
    }

    for (auto& scan : cur_block->GetInstList()) {
        if (scan.Id() <= address->Id() || scan.Id() >= memory->Id()) {
            continue;
        }
        if ((scan.GetOp() == ir::OpCode::SetHostGPR &&
             scan.GetArg<ir::Imm>(1).Get() == target) ||
            (target <= 9 && IsCallerSavedAddressBarrier(scan.GetOp()))) {
            return std::nullopt;
        }
    }
    return static_cast<u16>(target);
}

}  // namespace swift::runtime::backend::arm64
