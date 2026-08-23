#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

GPRSMask CopyTestGPRs() {
    GPRSMask result{swift::u32{0}};
    for (swift::u32 code : {0u, 1u, 2u, 3u, 4u, 5u, 19u, 20u, 21u,
                            22u, 23u, 25u, 26u, 27u, 28u, 29u, 30u, 31u}) {
        result.Mark(code);
    }
    return result;
}

struct CopyAllocation {
    IntrusivePtr<Block> block;
    std::unique_ptr<RegAlloc> alloc;
    Value source;
    Value bridge;
    Value wrapper;
};

CopyAllocation AllocateCopyChain(bool separate_nodes, bool reuse_bridge) {
    IntrusivePtr<Block> block{new Block(0, Location{0x8680})};
    auto source = block->LoadUniform<TypedValue<ValueType::U64>>(
            Uniform{0, ValueType::U64});
    auto bridge = block->BitExtract(source, Imm{0u}, Imm{32u})
                          .SetType(ValueType::U32);
    if (separate_nodes) {
        block->AdvancePC(Imm{1u});
    }
    auto wrapper = block->ZeroExtend32To64(bridge).SetType(ValueType::U64);
    block->StoreUniform(Uniform{8, ValueType::U64}, wrapper);
    block->StoreUniform(Uniform{16, ValueType::U64}, wrapper);
    block->StoreUniform(Uniform{24, ValueType::U64}, source);
    if (reuse_bridge) {
        block->StoreUniform(Uniform{32, ValueType::U32}, bridge);
    }
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    auto features = FeatureSet{};
    auto alloc = std::make_unique<RegAlloc>(
            block->MaxInstrId(), CopyTestGPRs(), FPRSMask{~((1u << 8) - 1u)},
            features);
    RegisterAllocPass::Run(block.get(), alloc.get(), false, features);
    return {std::move(block), std::move(alloc), source, bridge, wrapper};
}

}  // namespace

TEST_CASE("adjacent low32 copies reuse the source without consuming the wrapper") {
    auto accepted = AllocateCopyChain(false, false);
    REQUIRE(accepted.alloc->IsLow32CopyCoalesced(accepted.bridge.Id()));
    REQUIRE(accepted.alloc->Low32CopySource(accepted.bridge.Id()) ==
            accepted.source.Id());
    REQUIRE(accepted.alloc->ValueGPR(accepted.bridge).id ==
            accepted.alloc->ValueGPR(accepted.source).id);
    REQUIRE(accepted.alloc->ValueGPR(accepted.wrapper).id !=
            accepted.alloc->ValueGPR(accepted.source).id);

    auto separated = AllocateCopyChain(true, false);
    REQUIRE_FALSE(separated.alloc->IsLow32CopyCoalesced(separated.bridge.Id()));

    auto reused = AllocateCopyChain(false, true);
    REQUIRE_FALSE(reused.alloc->IsLow32CopyCoalesced(reused.bridge.Id()));
}
