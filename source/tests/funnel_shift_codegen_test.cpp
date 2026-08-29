#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "aarch64/disasm-aarch64.h"
#include "runtime/backend/address_space.h"
#include "runtime/backend/arm64/jit/translator.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

std::vector<std::string> EmitFunnelCandidate(bool shared_shift,
                                             bool observe_parity = false) {
    IntrusivePtr<Block> block{new Block(0, Location{0x8d40})};
    auto low = block->LoadUniform(Uniform{0, ValueType::U64});
    auto high = block->LoadUniform(Uniform{8, ValueType::U64});
    auto low_part = block->LsrImm(low, Imm{32u}).SetType(ValueType::U64);
    auto high_part = block->LslImm(high, Imm{32u}).SetType(ValueType::U64);
    auto result = block->Or(low_part, Operand{high_part}).SetType(ValueType::U64);
    if (shared_shift) {
        block->StoreUniform(Uniform{16, ValueType::U64}, low_part);
    }
    if (observe_parity) {
        auto flag_value = block->Or(result, Operand{Imm{swift::u64{0}}})
                                  .SetType(ValueType::U64);
        block->SaveFlags(flag_value, Flags::Parity);
        auto parity = block->TestFlags<TypedValue<ValueType::U8>>(Flags::Parity);
        block->StoreUniform(Uniform{24, ValueType::U8}, parity);
    }
    auto* publication = block->AppendInst(
            OpCode::SetHostGPR, result, HostRegIndex(22), Imm{0u});
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
            .global_opts = Optimizations::All,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();
    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    if (observe_parity) {
        alloc.MapRegister(result.Id(), HostGPR{22});
        alloc.MarkHostWriteCoalesced(publication->Id());
    }
    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> instructions;
    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    for (auto* instruction = first; instruction < last;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        instructions.emplace_back(disassembler.GetOutput());
    }
    return instructions;
}

size_t Count(const std::vector<std::string>& instructions,
             std::string_view mnemonic) {
    return std::ranges::count_if(instructions, [&](const auto& instruction) {
        return instruction.starts_with(mnemonic);
    });
}

}  // namespace

TEST_CASE("adjacent complementary shifts emit one funnel shift") {
    const auto fused = EmitFunnelCandidate(false);
    REQUIRE(Count(fused, "extr ") == 1);
    REQUIRE(Count(fused, "lsr ") == 0);
    REQUIRE(Count(fused, "lsl ") == 0);

    const auto shared = EmitFunnelCandidate(true);
    REQUIRE(Count(shared, "extr ") == 0);
    REQUIRE(Count(shared, "lsr ") == 1);
    REQUIRE(Count(shared, "lsl ") == 1);
}

TEST_CASE("funnel shift parity reads the fused result") {
    const auto instructions = EmitFunnelCandidate(false, true);
    REQUIRE(Count(instructions, "extr ") == 1);
    REQUIRE(std::ranges::count_if(instructions, [](const auto& instruction) {
                return instruction.starts_with("uxtb ") &&
                       instruction.ends_with(", w22");
            }) == 1);
    REQUIRE_FALSE(std::ranges::any_of(instructions, [](const auto& instruction) {
        return (instruction.starts_with("uxtb ") ||
                instruction.starts_with("ubfx ")) &&
               instruction.find("w26") != std::string::npos;
    }));
}
