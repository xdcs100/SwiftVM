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
    auto value = block->GetHostGPR(HostRegIndex(22), Imm{0u})
                         .SetType(type);
    if (shape == PinnedReadShape::Or) {
        auto right = block->GetHostGPR(HostRegIndex(29), Imm{0u})
                             .SetType(type);
        auto result = block->Or(value, Operand{right}).SetType(type);
        block->SaveFlags(result, Flags::Negate | Flags::Zero | Flags::Parity);
    } else if (shape == PinnedReadShape::SelfWrite) {
        auto result = block->ZeroExtend32To64(value).SetType(ValueType::U64);
        block->SetHostGPR(result, HostRegIndex(22), Imm{0u});
        auto address = block->BitCast(result).SetType(ValueType::U64);
        auto loaded = block->LoadMemory(Operand{address, Imm{8u}})
                              .SetType(ValueType::U8);
        block->StoreUniform(Uniform{64, ValueType::U8}, loaded);
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
    if (reuse_read) {
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
