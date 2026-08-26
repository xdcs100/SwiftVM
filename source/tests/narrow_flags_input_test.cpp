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

std::vector<std::string> EmitNarrowSub(ValueType type, bool reuse_extract) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
            .global_opts = Optimizations::All,
    };
    AddressSpace address_space{config};
    IntrusivePtr<Block> block{new Block(0, Location{0x8b00})};
    auto source = block->GetHostGPR(HostRegIndex(22), Imm{0u})
                          .SetType(ValueType::U64);
    const auto bits = ir::GetValueSizeByte(type) * 8;
    auto extract = block->BitExtract(source, Imm{0u}, Imm{bits}).SetType(type);
    auto result = block->Sub(extract, Operand{Imm{3u}}).SetType(type);
    block->SaveFlags(result, Flags::All);
    if (reuse_extract) {
        block->StoreUniform(Uniform{8, type}, extract);
    }
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    arm64::JitContext context{address_space.GetDefaultModule(), alloc};
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

std::size_t Count(const std::vector<std::string>& instructions,
                  std::string_view value) {
    return std::ranges::count_if(instructions, [&](const auto& instruction) {
        return instruction.find(value) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("narrow flag alignment consumes low extracts directly") {
    for (const auto type : {ValueType::U8, ValueType::U16}) {
        CAPTURE(type);
        const auto instructions = EmitNarrowSub(type, false);
        REQUIRE(Count(instructions, type == ValueType::U8 ? "uxtb " : "uxth ") ==
                0);
        REQUIRE(Count(instructions, "subs ") == 1);
    }
}

TEST_CASE("narrow flag input keeps shared extracts materialized") {
    const auto instructions = EmitNarrowSub(ValueType::U8, true);
    REQUIRE(Count(instructions, "uxtb ") == 1);
    REQUIRE(Count(instructions, "subs ") == 1);
}
