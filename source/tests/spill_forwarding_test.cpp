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

SpillForwardingBlock MakeSpillWidthTransferBlock() {
    IntrusivePtr<Block> block{new Block(0, Location{0x2420})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto arriving = block->LoadImm(Imm{swift::u64{4}})
                                  .SetType(ValueType::U32);
    const auto extended = block->ZeroExtend32To64(arriving)
                                  .SetType(ValueType::U64);
    const auto first = block->Add(extended, Operand{shortest});
    const auto second = block->Add(first, Operand{middle});
    const auto total = block->Add(second, Operand{longest});
    block->StoreUniform(Uniform{0, ValueType::U64}, total);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), arriving};
}

SpillForwardingBlock MakeRepeatedSpillUseBlock() {
    IntrusivePtr<Block> block{new Block(0, Location{0x2440})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto arriving = block->LoadImm(Imm{swift::u64{4}});
    const auto first = block->Add(arriving, Operand{shortest});
    const auto bridge = block->Add(first, Operand{middle});
    const auto second = block->Add(arriving, Operand{longest});
    const auto total = block->Add(second, Operand{bridge});
    block->StoreUniform(Uniform{0, ValueType::U64}, total);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), arriving};
}

SpillForwardingBlock MakeSpilledPublicationWindowBlock(bool faulting = false) {
    IntrusivePtr<Block> block{new Block(0, Location{0x2480})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto published = block->GetOperand(Operand{Imm{swift::u64{0x1234}}})
                                   .SetType(ValueType::U64);
    if (faulting) {
        (void)block->LoadMemory(Operand{Imm{swift::u64{0x1000}}})
                .SetType(ValueType::U64);
    } else {
        block->ClearFlags(Flags::Carry | Flags::Overflow);
    }
    block->AppendInst(OpCode::SetHostGPR, published, HostRegIndex(29), Imm{0u});
    const auto first = block->Add(longest, Operand{middle});
    const auto total = block->Add(first, Operand{shortest});
    block->StoreUniform(Uniform{0, ValueType::U64}, total);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), published};
}

SpillForwardingBlock MakeSpilledBitExtractPublicationBlock() {
    IntrusivePtr<Block> block{new Block(0, Location{0x24a0})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto source = block->LoadImm(Imm{swift::u64{0x123400}});
    const auto published = block->BitExtract(source, Imm{16u}, Imm{8u})
                                   .SetType(ValueType::U64);
    block->AppendInst(OpCode::SetHostGPR, published, HostRegIndex(29), Imm{0u});
    const auto first = block->Add(longest, Operand{middle});
    const auto total = block->Add(first, Operand{shortest});
    block->StoreUniform(Uniform{0, ValueType::U64}, total);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();
    return {std::move(block), published};
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

TEST_CASE("adjacent spilled scalar def-use retains its free scratch") {
    const auto emitted = Emit(MakeSpillForwardingBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    const auto definition = std::ranges::find_if(emitted, [](const auto& line) {
        return line.find("mov x") != std::string::npos &&
               line.find("#0x4") != std::string::npos;
    });
    REQUIRE(definition != emitted.end());
    const auto delimiter = definition->find(',');
    REQUIRE(delimiter != std::string::npos);
    const auto scratch = definition->substr(4, delimiter - 4);
    const auto consumer = std::find_if(std::next(definition), emitted.end(), [&](const auto& line) {
        return line.find("add ") != std::string::npos &&
               line.find(scratch) != std::string::npos;
    });
    REQUIRE(consumer != emitted.end());
    REQUIRE(std::none_of(std::next(definition), consumer, [&](const auto& line) {
        return (line.find("str " + scratch + ", [x28") != std::string::npos) ||
               (line.find("ldr " + scratch + ", [x28") != std::string::npos);
    }));
    const auto after_consumer = std::next(consumer);
    REQUIRE(after_consumer != emitted.end());
    REQUIRE(after_consumer->find("str " + scratch + ", [x28") == std::string::npos);
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

TEST_CASE("spill forwarding stops before a width ownership transfer") {
    const auto emitted = Emit(MakeSpillWidthTransferBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    const auto definition = std::ranges::find_if(emitted, [](const auto& line) {
        return line.find("mov w") != std::string::npos &&
               line.find("#0x4") != std::string::npos;
    });
    REQUIRE(definition != emitted.end());
    const auto delimiter = definition->find(',');
    REQUIRE(delimiter != std::string::npos);
    const auto scratch = "x" + definition->substr(5, delimiter - 5);
    const auto store = std::find_if(std::next(definition), emitted.end(), [&](const auto& line) {
        return line.find("str " + scratch + ", [x28") != std::string::npos;
    });
    REQUIRE(store != emitted.end());
    const auto address_begin = store->find("[x28");
    const auto address_end = store->find(']', address_begin);
    REQUIRE(address_begin != std::string::npos);
    REQUIRE(address_end != std::string::npos);
    const auto address = store->substr(address_begin, address_end - address_begin + 1);
    const auto reload = std::find_if(std::next(store), emitted.end(), [&](const auto& line) {
        return line.find("ldr x") != std::string::npos &&
               line.find(address) != std::string::npos;
    });
    REQUIRE(reload != emitted.end());
}

TEST_CASE("spilled scalar definitions transfer into multi-use reload regions") {
    const auto emitted = Emit(MakeRepeatedSpillUseBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    const auto definition = std::ranges::find_if(emitted, [](const auto& line) {
        return line.find("mov x") != std::string::npos &&
               line.find("#0x4") != std::string::npos;
    });
    REQUIRE(definition != emitted.end());
    const auto delimiter = definition->find(',');
    REQUIRE(delimiter != std::string::npos);
    const auto resident = definition->substr(4, delimiter - 4);
    REQUIRE(std::count_if(std::next(definition), emitted.end(), [&](const auto& line) {
        return line.find("add ") != std::string::npos &&
               line.find(resident) != std::string::npos;
    }) >= 2);
    REQUIRE(std::none_of(std::next(definition), emitted.end(), [&](const auto& line) {
        return (line.find("str " + resident + ", [x28") != std::string::npos) ||
               (line.find("ldr " + resident + ", [x28") != std::string::npos);
    }));
}

TEST_CASE("spilled fixed publications cross a fault-safe instruction window") {
    const auto emitted = Emit(MakeSpilledPublicationWindowBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    REQUIRE(std::ranges::any_of(emitted, [](const auto& line) {
        return line.find("mov x29, #0x1234") != std::string::npos;
    }));
    REQUIRE(std::ranges::none_of(emitted, [](const auto& line) {
        return line.find("str x29, [x28") != std::string::npos ||
               line.find("ldr x29, [x28") != std::string::npos;
    }));
}

TEST_CASE("spilled fixed publications retain canonical state across an untracked fault") {
    const auto emitted = Emit(MakeSpilledPublicationWindowBlock(true),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    REQUIRE(std::ranges::none_of(emitted, [](const auto& line) {
        return line.find("mov x29, #0x1234") != std::string::npos;
    }));
}

TEST_CASE("spilled bit extracts publish directly to a fixed home") {
    const auto emitted = Emit(MakeSpilledBitExtractPublicationBlock(),
                              GPRSMask{~((1u << 5) - 1u) & ~(1u << 18)});
    REQUIRE(std::ranges::any_of(emitted, [](const auto& line) {
        return line.find("ubfx x29") != std::string::npos;
    }));
    REQUIRE(std::ranges::none_of(emitted, [](const auto& line) {
        return line.find("str x29, [x28") != std::string::npos ||
               line.find("ldr x29, [x28") != std::string::npos;
    }));
}

TEST_CASE("spill reload regions stop at local control flow") {
    IntrusivePtr<Block> block{new Block(0, Location{0x2460})};
    const auto longest = block->LoadImm(Imm{swift::u64{1}});
    const auto middle = block->LoadImm(Imm{swift::u64{2}});
    const auto shortest = block->LoadImm(Imm{swift::u64{3}});
    const auto arriving = block->LoadImm(Imm{swift::u64{4}});
    const auto first = block->Add(arriving, Operand{shortest});
    const auto condition = block->LoadImm<BOOL>(Imm{swift::u64{1}});
    const auto skip = block->NotGoto(condition);
    block->Add(first, Operand{middle});
    block->BindLabel(skip);
    const auto second = block->Add(arriving, Operand{longest});
    block->StoreUniform(Uniform{0, ValueType::U64}, second);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    GPRSMask gprs{~((1u << 5) - 1u) & ~(1u << 18)};
    FPRSMask fprs{~((1u << 8) - 1u)};
    RegAlloc alloc{block->MaxInstrId(), gprs, fprs, FeatureSet{}};
    RegisterAllocPass::RunForSpillEvictTest(block.get(), &alloc, false);

    REQUIRE(alloc.ValueType(arriving) == RegAlloc::MEM);
    REQUIRE_FALSE(alloc.HasSpillReload(arriving.Id(), first.Id()));
    REQUIRE_FALSE(alloc.HasSpillReload(arriving.Id(), second.Id()));
}
