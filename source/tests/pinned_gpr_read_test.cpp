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

enum class PinnedReadShape {
    Or,
    SelfAnd,
    SelfWrite,
    Copy,
    CopyU16,
    LoadU16,
    LoadU8Flags,
    OverwrittenWrite,
    OverwrittenWriteFault,
    MemoryAddress,
    SignExtend,
    StoreMemory,
    Subtract,
};

std::vector<std::string> EmitPinnedRead(PinnedReadShape shape, bool reuse_read,
                                        ValueType type = ValueType::U32) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();

    IntrusivePtr<Block> block{new Block(0, Location{0x8940})};
    const bool overwritten_write = shape == PinnedReadShape::OverwrittenWrite ||
                                   shape == PinnedReadShape::OverwrittenWriteFault;
    const bool copy = shape == PinnedReadShape::Copy ||
                      shape == PinnedReadShape::CopyU16 ||
                      shape == PinnedReadShape::LoadU16;
    const auto source_index = overwritten_write
            ? 1u
            : (copy ? 20u : 22u);
    if (shape == PinnedReadShape::CopyU16 || shape == PinnedReadShape::LoadU16) {
        type = ValueType::U16;
    } else if (shape == PinnedReadShape::LoadU8Flags) {
        type = ValueType::U8;
    }
    auto value = shape == PinnedReadShape::LoadU16 ||
                         shape == PinnedReadShape::LoadU8Flags
            ? block->LoadMemory(Operand{block->LoadImm(Imm{swift::u64{0x1000}})
                                                .SetType(ValueType::U64)})
                      .SetType(type)
            : block->GetHostGPR(HostRegIndex(source_index), Imm{0u})
                      .SetType(type);
    if (shape == PinnedReadShape::LoadU8Flags) {
        auto extended = block->ZeroExtend32To64(value).SetType(ValueType::U64);
        block->SetHostGPR(extended, HostRegIndex(23), Imm{0u});
        auto alias = block->BitExtract(extended, Imm{0u}, Imm{8u})
                             .SetType(ValueType::U8);
        auto result = block->Or(alias, Operand{Imm{0u}})
                              .SetType(ValueType::U8);
        block->AppendInst(OpCode::BranchOnlyFlags, result, Flags::Zero);
    } else if (shape == PinnedReadShape::Or) {
        auto right = block->GetHostGPR(HostRegIndex(29), Imm{0u})
                             .SetType(type);
        auto result = block->Or(value, Operand{right}).SetType(type);
        block->SaveFlags(result, Flags::Negate | Flags::Zero | Flags::Parity);
    } else if (shape == PinnedReadShape::SelfWrite ||
               shape == PinnedReadShape::Copy ||
               shape == PinnedReadShape::CopyU16 ||
               shape == PinnedReadShape::LoadU16) {
        auto extended = shape == PinnedReadShape::CopyU16 ||
                                shape == PinnedReadShape::LoadU16
                ? block->ZeroExtend32(value).SetType(ValueType::U32)
                : value;
        auto result = block->ZeroExtend32To64(extended).SetType(ValueType::U64);
        block->SetHostGPR(result, HostRegIndex(22), Imm{0u});
        auto address = block->BitCast(result).SetType(ValueType::U64);
        auto loaded = block->LoadMemory(Operand{address, Imm{8u}})
                              .SetType(ValueType::U8);
        block->StoreUniform(Uniform{64, ValueType::U8}, loaded);
    } else if (overwritten_write) {
        auto copied = block->ZeroExtend32To64(value).SetType(ValueType::U64);
        block->SetHostGPR(copied, HostRegIndex(23), Imm{0u});
        block->AdvancePC(Imm{2u});
        if (shape == PinnedReadShape::OverwrittenWriteFault) {
            auto address = block->LoadImm(Imm{swift::u64{0x1000}})
                                   .SetType(ValueType::U64);
            auto loaded = block->LoadMemory(Operand{address}).SetType(ValueType::U8);
            block->StoreUniform(Uniform{64, ValueType::U8}, loaded);
        }
        auto low = block->BitExtract(copied, Imm{0u}, Imm{32u})
                           .SetType(ValueType::U32);
        auto masked = block->And(low, Operand{Imm{swift::u32{0x70}}})
                              .SetType(ValueType::U32);
        auto result = block->ZeroExtend32To64(masked).SetType(ValueType::U64);
        block->SetHostGPR(result, HostRegIndex(23), Imm{0u});
    } else if (shape == PinnedReadShape::MemoryAddress) {
        auto address = block->GetOperand(Operand{value}).SetType(ValueType::U64);
        if (reuse_read) {
            auto replacement = block->LoadImm(Imm{swift::u64{0x1000}})
                                       .SetType(ValueType::U64);
            block->SetHostGPR(replacement, HostRegIndex(22), Imm{0u});
        }
        auto loaded = block->LoadMemory(Operand{address}).SetType(ValueType::U32);
        block->StoreUniform(Uniform{64, ValueType::U32}, loaded);
    } else if (shape == PinnedReadShape::SelfAnd) {
        auto result = block->And(value, Operand{value}).SetType(ValueType::U32);
        block->SaveFlags(result, Flags::Negate | Flags::Zero | Flags::Parity);
    } else if (shape == PinnedReadShape::SignExtend) {
        auto result = block->SignExtend(value).SetType(ValueType::U64);
        block->StoreUniform(Uniform{8, ValueType::U64}, result);
    } else if (shape == PinnedReadShape::Subtract) {
        auto right = block->LoadImm(Imm{swift::u32{1}}).SetType(type);
        auto result = block->Sub(value, Operand{right}).SetType(type);
        block->SaveFlags(result, Flags::All);
    } else {
        auto address = block->LoadImm(Imm{swift::u64{0x1000}})
                               .SetType(ValueType::U64);
        block->StoreMemory(Operand{address}, value);
    }
    if (reuse_read && shape != PinnedReadShape::MemoryAddress) {
        block->StoreUniform(Uniform{0, ValueType::U32}, value);
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

std::size_t Count(const std::vector<std::string>& lines, std::string_view first,
                  std::string_view second) {
    return std::ranges::count_if(lines, [&](const auto& line) {
        return line.find(first) != std::string::npos &&
               line.find(second) != std::string::npos;
    });
}

bool HasShiftPreparation(const std::vector<std::string>& lines) {
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        const auto& preparation = lines[index];
        const auto comma = preparation.find(',');
        if ((!preparation.starts_with("mov w") &&
             !preparation.starts_with("lsl w")) ||
            comma == std::string::npos ||
            !lines[index + 1].starts_with("subs w")) {
            continue;
        }
        const auto destination = preparation.substr(4, comma - 4);
        if (lines[index + 1].find(", " + destination + ", lsl #") !=
            std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("one pinned GPR consumer reuses its fixed W view") {
    const auto lines = EmitPinnedRead(PinnedReadShape::SelfAnd, false);
    REQUIRE(Count(lines, "ubfx ", "x22") == 0);
    REQUIRE(Count(lines, "ands w", "w22, w22") == 1);
}

TEST_CASE("pinned GPR OR reads direct W views only at exact U32 width") {
    const auto wide = EmitPinnedRead(PinnedReadShape::Or, false);
    REQUIRE(Count(wide, "ubfx ", "x22") == 0);
    REQUIRE(Count(wide, "ubfx ", "x29") == 0);
    REQUIRE(Count(wide, "orr w", "w22, w29") == 1);

    const auto narrow = EmitPinnedRead(
            PinnedReadShape::Or, false, ValueType::U16);
    REQUIRE(Count(narrow, "orr w", "w22, w29") == 0);
}

TEST_CASE("a pinned low-32 self-write clears its high half in one instruction") {
    const auto lines = EmitPinnedRead(PinnedReadShape::SelfWrite, false);
    REQUIRE(Count(lines, "ubfx ", "x22") == 0);
    REQUIRE(Count(lines, "mov w22, w22", "") == 1);
    REQUIRE(Count(lines, "mov w", "w22") == 1);
    REQUIRE(Count(lines, "ldrb ", "[x22") == 1);
}

TEST_CASE("a pinned low-32 copy publishes directly between fixed homes") {
    const auto lines = EmitPinnedRead(PinnedReadShape::Copy, false);
    REQUIRE(Count(lines, "ubfx ", "x20") == 0);
    REQUIRE(Count(lines, "mov w22, w20", "") == 1);
    REQUIRE(Count(lines, "mov w", "w20") == 1);
    REQUIRE(Count(lines, "ldrb ", "[x22") == 1);
}

TEST_CASE("a pinned narrow copy zero-extends directly between fixed homes") {
    const auto lines = EmitPinnedRead(PinnedReadShape::CopyU16, false);
    REQUIRE(Count(lines, "ubfx ", "x20") == 0);
    REQUIRE(Count(lines, "uxth w22, w20", "") == 1);
    REQUIRE(Count(lines, "mov w", "w20") == 0);
    REQUIRE(Count(lines, "ldrb ", "[x22") == 1);
}

TEST_CASE("a pinned narrow load publishes directly into its fixed home") {
    const auto lines = EmitPinnedRead(PinnedReadShape::LoadU16, false);
    REQUIRE(Count(lines, "ldrh w22", "") == 1);
    REQUIRE(Count(lines, "uxth w22", "") == 0);
    REQUIRE(Count(lines, "mov w22", "") == 0);
    REQUIRE(Count(lines, "ldrb ", "[x22") == 1);
}

TEST_CASE("a pinned narrow load keeps a branch flag alias in its fixed home") {
    const auto lines = EmitPinnedRead(PinnedReadShape::LoadU8Flags, false);
    REQUIRE(Count(lines, "ldrb w23", "") == 1);
    REQUIRE(Count(lines, "mov w23", "") == 0);
    REQUIRE(Count(lines, "w23, lsl #24", "") == 1);
}

TEST_CASE("a pinned GPR supplies a sole memory address without a copy") {
    const auto direct = EmitPinnedRead(PinnedReadShape::MemoryAddress, false,
                                       ValueType::U64);
    REQUIRE(Count(direct, "mov x", "x22") == 0);
    REQUIRE(Count(direct, "ldr w", "[x22]") == 1);

    const auto overwritten = EmitPinnedRead(PinnedReadShape::MemoryAddress, true,
                                            ValueType::U64);
    REQUIRE(Count(overwritten, "ldr w", "[x22]") == 0);
    REQUIRE(Count(overwritten, "mov x", ", x22") == 1);
}

TEST_CASE("a superseded pinned GPR publication is omitted without observers") {
    const auto direct = EmitPinnedRead(PinnedReadShape::OverwrittenWrite, false);
    REQUIRE(Count(direct, "mov w23", "w1") == 1);

    const auto faulting = EmitPinnedRead(PinnedReadShape::OverwrittenWriteFault, false);
    REQUIRE(Count(faulting, "mov w23", "w1") == 2);
}

TEST_CASE("a later pinned GPR snapshot use keeps the read move") {
    const auto lines = EmitPinnedRead(PinnedReadShape::SelfAnd, true);
    REQUIRE(Count(lines, "ubfx ", "x22") == 1);
}

TEST_CASE("pinned GPR sign extension reads the fixed W view directly") {
    const auto lines = EmitPinnedRead(PinnedReadShape::SignExtend, false);
    REQUIRE(Count(lines, "ubfx ", "x22") == 0);
    REQUIRE(Count(lines, "sxtw ", "w22") == 1);
}

TEST_CASE("pinned GPR memory store reads the fixed W view directly") {
    const auto lines = EmitPinnedRead(PinnedReadShape::StoreMemory, false);
    REQUIRE(Count(lines, "ubfx ", "x22") == 0);
    REQUIRE(Count(lines, "str w22", "[") == 1);
}

TEST_CASE("callee-saved pinned GPR subtraction reads the fixed W view directly") {
    for (auto type : {ValueType::U8, ValueType::U16, ValueType::U32}) {
        CAPTURE(type);
        const auto lines = EmitPinnedRead(PinnedReadShape::Subtract, false, type);
        REQUIRE(Count(lines, "ubfx ", "x22") == 0);
        REQUIRE(std::ranges::any_of(lines, [](const auto& line) {
            return line.find("w22") != std::string::npos;
        }));
        if (type != ValueType::U32) {
            REQUIRE_FALSE(HasShiftPreparation(lines));
        }
    }
}

TEST_CASE("a reused pinned GPR memory value keeps the read move") {
    const auto lines = EmitPinnedRead(PinnedReadShape::StoreMemory, true);
    REQUIRE(Count(lines, "ubfx ", "x22") == 1);
}
