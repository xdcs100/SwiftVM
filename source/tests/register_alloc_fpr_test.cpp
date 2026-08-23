#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

constexpr swift::u32 kResidentTarget = 17;

FPRSMask ResidentFPRs() {
    FPRSMask result{0};
    result.Mark(kResidentTarget);
    return result;
}

struct ScalarPublication {
    IntrusivePtr<Block> block;
    Value result;
    Inst* publish;
};

ScalarPublication MakeScalarPublication(bool scalar32, bool fixed_left,
                                        bool fault_observer = false) {
    IntrusivePtr<Block> block{new Block(0, Location{0x8730})};
    auto left = fixed_left
            ? block->GetHostFPR(HostRegIndex(kResidentTarget), Imm{0u})
                      .SetType(ValueType::V128)
            : block->LoadUniform(Uniform{0, ValueType::V128})
                      .SetType(ValueType::V128);
    auto right = block->LoadUniform(Uniform{16, ValueType::V128})
                         .SetType(ValueType::V128);
    auto result = scalar32
            ? block->VecFAddScalar32(left, right).SetType(ValueType::V128)
            : block->VecFAddScalar64(left, right).SetType(ValueType::V128);
    if (fault_observer) {
        auto address = block->LoadUniform(Uniform{32, ValueType::U64})
                               .SetType(ValueType::U64);
        (void)block->LoadMemory(Operand{address}).SetType(ValueType::U64);
    }
    auto* publish = block->AppendInst(
            OpCode::SetHostFPR, result, HostRegIndex(kResidentTarget), Imm{0u});
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), result, publish};
}

std::unique_ptr<RegAlloc> Allocate(ScalarPublication& item) {
    const GPRSMask gprs{~((1u << 8) - 1u)};
    auto alloc = std::make_unique<RegAlloc>(
            item.block->MaxInstrId(), gprs, ResidentFPRs(), FeatureSet{});
    RegisterAllocPass::RunForXmmResidentTest(
            item.block.get(), alloc.get(), true);
    return alloc;
}

}  // namespace

TEST_CASE("legacy scalar FPR results publish directly to a resident home") {
    for (bool scalar32 : {false, true}) {
        CAPTURE(scalar32);
        auto item = MakeScalarPublication(scalar32, false);
        auto alloc = Allocate(item);
        REQUIRE(alloc->ValueFPR(item.result).id == kResidentTarget);
        REQUIRE(alloc->IsHostWriteCoalesced(item.publish->Id()));
    }
}

TEST_CASE("legacy scalar FPR publication preserves a fixed left source") {
    for (bool scalar32 : {false, true}) {
        CAPTURE(scalar32);
        auto item = MakeScalarPublication(scalar32, true);
        auto alloc = Allocate(item);
        REQUIRE(alloc->ValueFPR(item.result).id != kResidentTarget);
        REQUIRE_FALSE(alloc->IsHostWriteCoalesced(item.publish->Id()));
    }
}

TEST_CASE("legacy scalar FPR publication stops before a fault observer") {
    auto item = MakeScalarPublication(false, false, true);
    auto alloc = Allocate(item);
    REQUIRE(alloc->ValueFPR(item.result).id != kResidentTarget);
    REQUIRE_FALSE(alloc->IsHostWriteCoalesced(item.publish->Id()));
}
