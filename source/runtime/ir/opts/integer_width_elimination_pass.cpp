#include "integer_width_elimination_pass.h"

namespace swift::runtime::ir {

namespace {

constexpr u32 kMaxRoundTripDistance = 128;

bool IsNearbyDefinition(Block* block, Inst* definition, Inst* use) {
    auto& list = block->GetInstList();
    auto current = list.iterator_to(*use);
    for (u32 distance = 0; current != list.begin() && distance < kMaxRoundTripDistance;
         ++distance) {
        --current;
        if (&*current == definition) {
            return true;
        }
    }
    return false;
}

bool ReplaceSingleUse(Inst* consumer, Inst* definition, Value replacement) {
    for (u32 index = 0; index < Inst::max_args; ++index) {
        auto& arg = consumer->ArgAt(index);
        if (arg.IsValue() && arg.Get<Value>().Def() == definition) {
            consumer->SetArg(index, replacement);
            return true;
        }
        if (arg.IsLambda() && arg.Get<Lambda>().IsValue() &&
            arg.Get<Lambda>().GetValue().Def() == definition) {
            consumer->SetArg(index, Lambda{replacement});
            return true;
        }
    }
    return false;
}

Inst* FindSingleConsumer(Block* block, Inst* definition) {
    Inst* consumer = nullptr;
    u32 edges = 0;
    for (auto& candidate : block->GetInstList()) {
        auto values = candidate.GetValues();
        for (auto value : values) {
            if (value.Def() == definition) {
                consumer = &candidate;
                ++edges;
            }
        }
    }
    return edges == 1 ? consumer : nullptr;
}

void FoldLow32RoundTrips(Block* block) {
    for (auto& inst : block->GetInstList()) {
        if (inst.GetOp() != OpCode::BitExtract ||
            GetValueSizeByte(inst.ReturnType()) != sizeof(u32) || inst.GetArg<Imm>(1).Get() != 0 ||
            inst.GetArg<Imm>(2).Get() != 32 || inst.GetUses(false) != 1) {
            continue;
        }

        auto extended = inst.GetArg<Value>(0);
        auto* extension = extended.Def();
        if (!extension || extension->GetOp() != OpCode::ZeroExtend32To64) {
            continue;
        }
        auto source = extension->GetArg<Value>(0);
        if (GetValueSizeByte(source.Type()) != sizeof(u32) ||
            !IsNearbyDefinition(block, extension, &inst)) {
            continue;
        }

        auto* consumer = FindSingleConsumer(block, &inst);
        if (consumer && !consumer->IsPseudoOperation()) {
            ReplaceSingleUse(consumer, &inst, source);
        }
    }
}

}  // namespace

void IntegerWidthEliminationPass::Run(HIRBuilder* hir_builder) {
    for (auto& function : hir_builder->GetHIRFunctions()) {
        Run(&function);
    }
}

void IntegerWidthEliminationPass::Run(HIRFunction* hir_function) {
    for (auto& hir_block : hir_function->GetHIRBlocksRPO()) {
        FoldLow32RoundTrips(hir_block.GetBlock());
    }
}

void IntegerWidthEliminationPass::Run(Block* block) { FoldLow32RoundTrips(block); }

}  // namespace swift::runtime::ir
