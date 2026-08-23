#include <catch2/catch_test_macros.hpp>

#include "runtime/ir/opts/deadcode_elimination_pass.h"
#include "runtime/ir/opts/integer_width_elimination_pass.h"

namespace swift::runtime::ir {

TEST_CASE("low32 extraction cancels a nearby zero extension") {
    Block block{0, Location{0x1000}};
    const auto source = block.LoadUniform(Uniform{0, ValueType::U32});
    const auto extended = block.ZeroExtend32To64(source);
    block.LoadImm(Imm{7u});
    const auto extracted = block.BitExtract(extended, Imm{0u}, Imm{32u}).SetType(ValueType::U32);
    const auto left = block.LoadUniform(Uniform{8, ValueType::U32});
    const auto result = block.Add(left, Operand{extracted}).SetType(ValueType::U32);
    block.StoreUniform(Uniform{16, ValueType::U32}, result);

    IntegerWidthEliminationPass::Run(&block);

    REQUIRE(result.Def()->GetArg<Operand>(1).GetLeft().value.Def() == source.Def());
    REQUIRE(extracted.Def()->GetUses(false) == 0);

    DeadCodeEliminationPass::Run(&block);
    u32 extracts = 0;
    u32 extensions = 0;
    for (auto& inst : block.GetInstList()) {
        extracts += inst.GetOp() == OpCode::BitExtract;
        extensions += inst.GetOp() == OpCode::ZeroExtend32To64;
    }
    REQUIRE(extracts == 0);
    REQUIRE(extensions == 0);
}

TEST_CASE("integer width elimination rejects non-identity extracts") {
    Block block{0, Location{0x2000}};
    const auto source = block.LoadUniform(Uniform{0, ValueType::U32});
    const auto extended = block.ZeroExtend32To64(source);
    const auto narrow = block.BitExtract(extended, Imm{0u}, Imm{16u}).SetType(ValueType::U16);
    block.StoreUniform(Uniform{8, ValueType::U16}, narrow);
    const auto direct = block.BitExtract(source, Imm{0u}, Imm{32u}).SetType(ValueType::U32);
    block.StoreUniform(Uniform{16, ValueType::U32}, direct);
    const auto flagged = block.BitExtract(extended, Imm{0u}, Imm{32u}).SetType(ValueType::U32);
    block.SaveFlags(flagged, Flags::All);

    IntegerWidthEliminationPass::Run(&block);

    REQUIRE(narrow.Def()->GetUses(false) == 1);
    REQUIRE(direct.Def()->GetUses(false) == 1);
    REQUIRE(flagged.Def()->GetUses(false) == 1);
    REQUIRE(flagged.Def()->GetPseudoOperations(OpCode::SaveFlags).size() == 1);
}

}  // namespace swift::runtime::ir
