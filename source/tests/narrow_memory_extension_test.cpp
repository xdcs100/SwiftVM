#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
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
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> lines;
    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    for (auto* instruction = first; instruction < last;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        lines.emplace_back(disassembler.GetOutput());
    }
    return lines;
}

TEST_CASE("a narrow load writes an adjacent extension register directly") {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    IntrusivePtr<Block> block{new Block(0, Location{0x8a00})};
    auto address = block->GetHostGPR(HostRegIndex(29), Imm{0u})
                           .SetType(ValueType::U64);
    auto loaded = block->LoadMemory(Operand{address}).SetType(ValueType::U16);
    auto extended = block->SignExtend(loaded).SetType(ValueType::S32);
    block->StoreUniform(Uniform{64, ValueType::S32}, extended);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), FeatureSet{}};
    RegisterAllocPass::Run(block.get(), &alloc, false, FeatureSet{});
    alloc.MapRegister(address.Id(), HostGPR{29});
    alloc.MapRegister(loaded.Id(), HostGPR{8});
    alloc.MapRegister(extended.Id(), HostGPR{9});
    auto active_gprs = address_space.GetTrampolines().GetGPRRegs();
    active_gprs.Mark(8);
    active_gprs.Mark(9);
    active_gprs.Mark(29);
    auto active_fprs = address_space.GetTrampolines().GetFPRRegs();
    alloc.SetActiveRegs(loaded.Id(), active_gprs, active_fprs);
    REQUIRE(alloc.ValueGPR(address).id == 29);
    REQUIRE(alloc.ValueGPR(loaded).id != alloc.ValueGPR(extended).id);

    arm64::JitContext context{address_space.GetDefaultModule(), alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    const auto lines = Disassemble(context);
    const auto direct = std::ranges::count_if(lines, [](const auto& line) {
        return line.find("ldrsh w9") != std::string::npos;
    });
    const auto separate = std::ranges::count_if(lines, [](const auto& line) {
        return line.find("sxth ") != std::string::npos;
    });
    REQUIRE(direct == 1);
    REQUIRE(separate == 0);
}

TEST_CASE("an adjacent narrow extract extends in one instruction") {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    IntrusivePtr<Block> block{new Block(0, Location{0x8a20})};
    auto source = block->GetHostGPR(HostRegIndex(22), Imm{0u})
                          .SetType(ValueType::U64);
    auto extract = block->BitExtract(source, Imm{0u}, Imm{16u})
                           .SetType(ValueType::U16);
    auto extended = block->ZeroExtend32(extract).SetType(ValueType::U16);
    block->StoreUniform(Uniform{64, ValueType::U16}, extended);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), FeatureSet{}};
    RegisterAllocPass::Run(block.get(), &alloc, false, FeatureSet{});
    alloc.MapRegister(source.Id(), HostGPR{8});
    alloc.MapRegister(extract.Id(), HostGPR{9});
    alloc.MapRegister(extended.Id(), HostGPR{10});
    REQUIRE(alloc.ValueGPR(extract).id != alloc.ValueGPR(extended).id);

    arm64::JitContext context{address_space.GetDefaultModule(), alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    const auto lines = Disassemble(context);
    const auto extensions = std::ranges::count_if(lines, [](const auto& line) {
        return line.find("uxth ") != std::string::npos;
    });
    const auto direct = std::ranges::count_if(lines, [](const auto& line) {
        return line.find("uxth w10, w8") != std::string::npos;
    });
    REQUIRE(extensions == 1);
    REQUIRE(direct == 1);
}

}  // namespace
