#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
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

struct SpillForwardingBlock {
    IntrusivePtr<Block> block;
    Value arriving;
};

SpillForwardingBlock MakeSpillForwardingBlock() {
    IntrusivePtr<Block> block{new Block(0, Location{0x2400})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto arriving = block->LoadImm(Imm{swift::u64{4}});
    const auto first = block->Add(arriving, Operand{shortest});
    const auto second = block->Add(first, Operand{middle});
    const auto total = block->Add(second, Operand{longest});
    block->StoreUniform(Uniform{0, ValueType::U64}, total);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), arriving};
}

std::vector<std::string> Emit(SpillForwardingBlock input, GPRSMask gprs) {
    FPRSMask fprs{~((1u << 8) - 1u)};
    RegAlloc alloc{input.block->MaxInstrId(), gprs, fprs, FeatureSet{}};
    RegisterAllocPass::RunForSpillEvictTest(input.block.get(), &alloc, false);
    REQUIRE(alloc.ValueType(input.arriving) == RegAlloc::MEM);

    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.MapModule(
            LocationDescriptor{0x2400}, LocationDescriptor{0x2500}, ModuleConfig{});
    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(input.block.get());
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

}  // namespace

TEST_CASE("adjacent spilled scalar def-use stays in a free x18 scratch") {
#if defined(__linux__) && !defined(__ANDROID__)
    const auto emitted = Emit(MakeSpillForwardingBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    const auto definition = std::ranges::find_if(emitted, [](const auto& line) {
        return line.find("mov x18, #0x4") != std::string::npos;
    });
    REQUIRE(definition != emitted.end());
    const auto consumer = std::find_if(std::next(definition), emitted.end(), [](const auto& line) {
        return line.find("add ") != std::string::npos &&
               line.find("x18") != std::string::npos;
    });
    REQUIRE(consumer != emitted.end());
    REQUIRE(std::none_of(std::next(definition), consumer, [](const auto& line) {
        return (line.find("str x18, [x28") != std::string::npos) ||
               (line.find("ldr x18, [x28") != std::string::npos);
    }));
    const auto after_consumer = std::next(consumer);
    REQUIRE(after_consumer != emitted.end());
    REQUIRE(after_consumer->find("str x18, [x28") == std::string::npos);
#else
    SUCCEED("x18 spill scratch is Linux-only");
#endif
}

TEST_CASE("x18 spill forwarding yields to a live allocated value") {
#if defined(__linux__) && !defined(__ANDROID__)
    const auto emitted = Emit(MakeSpillForwardingBlock(),
                              GPRSMask{~(((1u << 6) - 1u) << 18)});
    REQUIRE(std::ranges::any_of(emitted, [](const auto& line) {
        return line.find("mov x18, #0x1") != std::string::npos;
    }));
    REQUIRE(std::ranges::none_of(emitted, [](const auto& line) {
        return line.find("mov x18, #0x4") != std::string::npos;
    }));
#else
    SUCCEED("x18 spill scratch is Linux-only");
#endif
}
