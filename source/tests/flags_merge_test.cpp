#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <string_view>

#include "aarch64/disasm-aarch64.h"
#include "runtime/backend/address_space.h"
#include "runtime/backend/arm64/jit/translator.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

std::map<std::string, swift::u32> EmitFlagsPublication(
        swift::runtime::ir::Flags saved_flags) {
    using namespace swift::runtime;
    using namespace swift::runtime::backend;
    using namespace swift::runtime::ir;

    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
            .global_opts = Optimizations::All,
    };
    AddressSpace address_space{config};
    ModuleConfig module_config{};
    auto module = address_space.MapModule(
            LocationDescriptor{0x87a0}, LocationDescriptor{0x87c0}, module_config);

    IntrusivePtr<Block> block{new Block(0, Location{0x87a0})};
    auto left = block->LoadUniform<TypedValue<ValueType::U64>>(
            Uniform{0, ValueType::U64});
    auto right = block->LoadUniform<TypedValue<ValueType::U64>>(
            Uniform{8, ValueType::U64});
    auto result = block->Sub(left, Operand{right}).SetType(ValueType::U64);
    block->SaveFlags(result, saved_flags);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    auto features = ResolveFeatureSet(module_config);
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);

    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    std::map<std::string, swift::u32> mnemonics;
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    for (auto* instruction = first; instruction < last; ++instruction) {
        decoder.Decode(instruction);
        std::string_view text{disassembler.GetOutput()};
        const auto begin = text.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            continue;
        }
        text.remove_prefix(begin);
        const auto end = text.find_first_of(" \t");
        ++mnemonics[std::string{text.substr(0, end)}];
    }

    return mnemonics;
}

void RequireBitfieldMerge(std::map<std::string, swift::u32>& mnemonics) {
    REQUIRE(mnemonics["mrs"] == 1);
    REQUIRE(mnemonics["ubfx"] == 1);
    REQUIRE(mnemonics["bfi"] == 1);
    REQUIRE(mnemonics["and"] == 0);
    REQUIRE(mnemonics["orr"] == 0);
}

}

TEST_CASE("full NZCV publication omits the redundant source mask") {
    auto mnemonics = EmitFlagsPublication(swift::runtime::ir::Flags::NZCV);

    REQUIRE(mnemonics["mrs"] == 1);
    REQUIRE(mnemonics["and"] == 1);
    REQUIRE(mnemonics["orr"] == 1);
    REQUIRE(mnemonics["ubfx"] == 0);
    REQUIRE(mnemonics["bfi"] == 0);
}

TEST_CASE("contiguous partial NZCV publication uses a bitfield merge") {
    using swift::runtime::ir::Flags;
    auto nz_mnemonics = EmitFlagsPublication(Flags::Negate | Flags::Zero);
    auto z_mnemonics = EmitFlagsPublication(Flags::Zero);

    RequireBitfieldMerge(nz_mnemonics);
    RequireBitfieldMerge(z_mnemonics);
}

TEST_CASE("noncontiguous partial NZCV publication retains masked merge") {
    using swift::runtime::ir::Flags;
    auto mnemonics = EmitFlagsPublication(Flags::Negate | Flags::Carry);

    REQUIRE(mnemonics["mrs"] == 1);
    REQUIRE(mnemonics["and"] == 2);
    REQUIRE(mnemonics["orr"] == 1);
    REQUIRE(mnemonics["ubfx"] == 0);
    REQUIRE(mnemonics["bfi"] == 0);
}
