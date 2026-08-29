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

std::vector<std::string> Disassemble(arm64::JitContext& context) {
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

std::vector<std::string> EmitTransfer(bool overwrite_target) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();
    IntrusivePtr<Block> block{new Block(0, Location{0x9720})};

    auto old_source = block->GetHostGPR(HostRegIndex(1), Imm{0u}).SetType(ValueType::U64);
    block->SetHostGPR(old_source, HostRegIndex(23), Imm{0u});
    auto new_source = block->LoadImm(Imm{swift::u64{0x2000}}).SetType(ValueType::U64);
    block->SetHostGPR(new_source, HostRegIndex(1), Imm{0u});
    auto first_address = block->BitCast(old_source).SetType(ValueType::U64);
    auto first = block->LoadMemory(Operand{first_address, Imm{8u}}).SetType(ValueType::U64);
    block->StoreUniform(Uniform{64, ValueType::U64}, first);
    if (overwrite_target) {
        auto replacement = block->LoadImm(Imm{swift::u64{0x3000}}).SetType(ValueType::U64);
        block->SetHostGPR(replacement, HostRegIndex(23), Imm{0u});
    }
    auto second_address = block->BitCast(old_source).SetType(ValueType::U64);
    auto second = block->LoadMemory(Operand{second_address, Imm{16u}}).SetType(ValueType::U64);
    block->StoreUniform(Uniform{72, ValueType::U64}, second);
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

    return Disassemble(context);
}

std::vector<std::string> EmitHelperTransfer(HostRegisterEffect effect, swift::u32 target) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();
    IntrusivePtr<Block> block{new Block(0, Location{0x9760})};

    auto old_source = block->GetHostGPR(HostRegIndex(20), Imm{0u}).SetType(ValueType::U64);
    block->SetHostGPR(old_source, HostRegIndex(target), Imm{0u});
    auto replacement = block->LoadImm(Imm{swift::u64{0x2000}}).SetType(ValueType::U64);
    block->SetHostGPR(replacement, HostRegIndex(20), Imm{0u});
    (void)block->CallLambda(Lambda{DataClass{Imm{1}}, HelperCallTraits{.host_registers = effect}});
    auto address = block->BitCast(old_source).SetType(ValueType::U64);
    auto loaded = block->LoadMemory(Operand{address, Imm{8u}}).SetType(ValueType::U64);
    block->StoreUniform(Uniform{64, ValueType::U64}, loaded);
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

    return Disassemble(context);
}

std::size_t Count(const std::vector<std::string>& lines,
                  std::string_view first,
                  std::string_view second) {
    return std::ranges::count_if(lines, [&](const auto& line) {
        return line.find(first) != std::string::npos && line.find(second) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("a published full-width GPR version feeds later faulting addresses") {
    const auto lines = EmitTransfer(false);
    REQUIRE(Count(lines, "mov x23, x1", "") == 1);
    REQUIRE(Count(lines, "ldr x", "[x23, #8]") == 1);
    REQUIRE(Count(lines, "ldr x", "[x23, #16]") == 1);
}

TEST_CASE("overwriting the resident GPR version keeps the source snapshot") {
    const auto lines = EmitTransfer(true);
    REQUIRE(Count(lines, "ldr x", "[x23, #8]") == 0);
    REQUIRE(Count(lines, "ldr x", "[x23, #16]") == 0);
}

TEST_CASE("resident helper contracts preserve pinned value versions") {
    const auto conservative = EmitHelperTransfer(HostRegisterEffect::MayTouchSIMD, 7);
    const auto resident = EmitHelperTransfer(HostRegisterEffect::PreservesPinnedState, 7);
    const auto clobbered = EmitHelperTransfer(HostRegisterEffect::PreservesPinnedState, 2);
    REQUIRE(Count(conservative, "ldr x", "[x7, #8]") == 0);
    REQUIRE(Count(resident, "ldr x", "[x7, #8]") == 1);
    REQUIRE(Count(clobbered, "ldr x", "[x2, #8]") == 0);
}
