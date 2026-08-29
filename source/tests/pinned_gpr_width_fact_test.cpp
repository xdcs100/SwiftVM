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

enum class PinnedWidthShape {
    NarrowSelect,
    PublishedLow32,
    SelectPublication,
    SelectPublicationFault,
};

std::vector<std::string> EmitWidthFact(PinnedWidthShape shape, bool overwrite_target) {
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

    if (shape == PinnedWidthShape::NarrowSelect) {
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
    } else if (shape == PinnedWidthShape::PublishedLow32) {
        auto base = block->GetHostGPR(HostRegIndex(1), Imm{0u}).SetType(ValueType::U64);
        auto value = block->Add(base, Operand{Imm{1u}}).SetType(ValueType::U64);
        block->SetHostGPR(value, HostRegIndex(22), Imm{0u});
        if (overwrite_target) {
            auto replacement = block->LoadImm(Imm{swift::u64{0x2000}}).SetType(ValueType::U64);
            block->SetHostGPR(replacement, HostRegIndex(22), Imm{0u});
        }
        auto low = block->BitExtract(value, Imm{0u}, Imm{32u}).SetType(ValueType::U32);
        auto compare = block->Sub(low, Operand{Imm{8u}}).SetType(ValueType::U32);
        block->SaveFlags(compare, Flags::All);
    } else {
        auto test = block->LoadImm(Imm{swift::u64{3}}).SetType(ValueType::U64);
        auto zero = block->LoadImm(Imm{swift::u64{0}}).SetType(ValueType::U64);
        auto value = block->LoadImm(Imm{swift::u64{9}}).SetType(ValueType::U64);
        auto selected = block->SelectZero(test, zero, value).SetType(ValueType::U64);
        if (shape == PinnedWidthShape::SelectPublicationFault) {
            auto address = block->LoadImm(Imm{swift::u64{0x1000}}).SetType(ValueType::U64);
            auto observed = block->LoadMemory(Operand{address}).SetType(ValueType::U64);
            block->StoreUniform(Uniform{80, ValueType::U64}, observed);
        }
        auto published = block->ZeroExtend32To64(selected).SetType(ValueType::U64);
        block->SetHostGPR(published, HostRegIndex(22), Imm{0u});
        if (overwrite_target) {
            auto replacement = block->LoadImm(Imm{swift::u64{0x2000}}).SetType(ValueType::U64);
            block->SetHostGPR(replacement, HostRegIndex(22), Imm{0u});
        }
        auto low = block->BitExtract(published, Imm{0u}, Imm{32u}).SetType(ValueType::U32);
        auto sum = block->Add(low, Operand{Imm{1u}}).SetType(ValueType::U32);
        block->StoreUniform(Uniform{88, ValueType::U32}, sum);
    }
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
    const auto lines = EmitWidthFact(PinnedWidthShape::NarrowSelect, false);
    REQUIRE(Count(lines, "ldrh w22") == 1);
    REQUIRE(Count(lines, "csel w", ", w22, ne") == 1);
}

TEST_CASE("overwriting a published narrow value rejects its pinned W view") {
    const auto lines = EmitWidthFact(PinnedWidthShape::NarrowSelect, true);
    REQUIRE(Count(lines, "ldrh w22") == 0);
    REQUIRE(Count(lines, "csel w", ", w22, ne") == 0);
}

TEST_CASE("a published full-width value exposes its pinned low W view") {
    const auto lines = EmitWidthFact(PinnedWidthShape::PublishedLow32, false);
    REQUIRE(Count(lines, "lsr w", "#0") == 0);
    REQUIRE(Count(lines, "subs w", "w22, #0x8") == 1);
}

TEST_CASE("overwriting a full-width publication preserves its low snapshot") {
    const auto lines = EmitWidthFact(PinnedWidthShape::PublishedLow32, true);
    REQUIRE(Count(lines, "lsr w", "#0") == 1);
    REQUIRE(Count(lines, "subs w", "w22, #0x8") == 0);
}

TEST_CASE("a zero-extended SelectZero result publishes directly to its pinned home") {
    const auto lines = EmitWidthFact(PinnedWidthShape::SelectPublication, false);
    REQUIRE(Count(lines, "csel w22", "eq") == 1);
    REQUIRE(Count(lines, "ubfx", "w22") == 0);
}

TEST_CASE("overwriting a SelectZero publication preserves its allocated result") {
    const auto lines = EmitWidthFact(PinnedWidthShape::SelectPublication, true);
    REQUIRE(Count(lines, "csel w22", "eq") == 0);
}

TEST_CASE("a fault before SelectZero publication keeps the architectural home unchanged") {
    const auto lines = EmitWidthFact(PinnedWidthShape::SelectPublicationFault, false);
    REQUIRE(Count(lines, "csel w22", "eq") == 0);
}
