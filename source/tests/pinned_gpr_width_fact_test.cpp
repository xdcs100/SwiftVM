#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "aarch64/disasm-aarch64.h"
#include "runtime/backend/address_space.h"
#include "runtime/backend/arm64/jit/jit_context.h"
#include "runtime/backend/arm64/jit/translator.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

std::vector<std::string> EmitWidthSelect(bool overwrite_target) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();
    IntrusivePtr<Block> block{new Block(0, Location{0x9750})};

    auto address = block->LoadImm(Imm{swift::u64{0x1000}}).SetType(ValueType::U64);
    auto loaded = block->LoadMemory(Operand{address}).SetType(ValueType::U16);
    auto widened = block->ZeroExtend32(loaded).SetType(ValueType::U32);
    auto published = block->ZeroExtend32To64(widened).SetType(ValueType::U64);
    block->SetHostGPR(published, HostRegIndex(22), Imm{0u});
    if (overwrite_target) {
        auto replacement = block->LoadImm(Imm{swift::u64{0x2000}}).SetType(ValueType::U64);
        block->SetHostGPR(replacement, HostRegIndex(22), Imm{0u});
    }
    auto condition = block->LoadImm(Imm{swift::u32{1}}).SetType(ValueType::U32);
    auto alternate = block->LoadImm(Imm{swift::u32{7}}).SetType(ValueType::U32);
    auto selected = block->Select(condition, alternate, widened).SetType(ValueType::U32);
    block->StoreUniform(Uniform{64, ValueType::U32}, selected);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(),
                   features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<const vixl::aarch64::Instruction*>();
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> lines;
    for (auto* instruction = first; instruction < last;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        lines.emplace_back(disassembler.GetOutput());
    }
    return lines;
}

std::size_t Count(const std::vector<std::string>& lines,
                  std::string_view first,
                  std::string_view second = {}) {
    return std::ranges::count_if(lines, [&](const auto& line) {
        return line.find(first) != std::string::npos && line.find(second) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("a published narrow load feeds Select from its pinned W view") {
    const auto lines = EmitWidthSelect(false);
    REQUIRE(Count(lines, "ldrh w22") == 1);
    REQUIRE(Count(lines, "csel w", ", w22, ne") == 1);
}

TEST_CASE("overwriting a published narrow value rejects its pinned W view") {
    const auto lines = EmitWidthSelect(true);
    REQUIRE(Count(lines, "ldrh w22") == 0);
    REQUIRE(Count(lines, "csel w", ", w22, ne") == 0);
}
