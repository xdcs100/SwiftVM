#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "aarch64/disasm-aarch64.h"
#include "runtime/backend/address_space.h"
#include "runtime/backend/arm64/jit/translator.h"
#include "runtime/frontend/x86/decoder.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

struct LogicalFlagEmission {
    std::vector<std::string> instructions;
    bool extract_tied{};
};

LogicalFlagEmission EmitLogicalFlagIdentity(bool observe_result) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
            .global_opts = Optimizations::All,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();

    IntrusivePtr<Block> block{new Block(0, Location{0x89c0})};
    auto source = block->GetHostGPR(HostRegIndex(22), Imm{0u})
                          .SetType(ValueType::U32);
    auto extract = block->BitExtract(source, Imm{0u}, Imm{8u})
                           .SetType(ValueType::U8);
    auto result = block->Or(extract, Operand{Imm{swift::u64{0}}})
                          .SetType(ValueType::U8);
    block->SaveFlags(result, Flags::Negate | Flags::Zero | Flags::Parity);
    if (observe_result) {
        block->StoreUniform(Uniform{0, ValueType::U8}, result);
    }
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    const bool extract_tied =
            alloc.ValueGPR(source).id == alloc.ValueGPR(extract).id;

    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> instructions;
    for (auto* instruction = first; instruction < last;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        instructions.emplace_back(disassembler.GetOutput());
    }
    return {std::move(instructions), extract_tied};
}

bool Contains(const std::vector<std::string>& instructions,
              std::string_view value) {
    return std::ranges::any_of(instructions, [&](const auto& instruction) {
        return instruction.find(value) != std::string::npos;
    });
}

std::size_t Count(const std::vector<std::string>& instructions,
                  std::string_view value) {
    return std::ranges::count_if(instructions, [&](const auto& instruction) {
        return instruction.find(value) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("dead narrow logical identities publish NZ in one instruction") {
    const auto emission = EmitLogicalFlagIdentity(false);
    REQUIRE(emission.extract_tied);
    REQUIRE(Count(emission.instructions, "lsl #24") == 1);
    REQUIRE_FALSE(Contains(emission.instructions, "sxtb"));
}

TEST_CASE("observed narrow logical identities keep their result") {
    const auto emission = EmitLogicalFlagIdentity(true);
    REQUIRE_FALSE(emission.extract_tied);
}

TEST_CASE("narrow register self tests skip the redundant AND") {
    struct Memory final : swift::x86::MemoryInterface {
        bool Read(void* dest, size_t addr, size_t size) override {
            return std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
        }
        bool Write(void* src, size_t addr, size_t size) override {
            return std::memcpy(reinterpret_cast<void*>(addr), src, size);
        }
        void* GetPointer(void* src) override { return src; }
    } memory;

    struct Case {
        std::array<swift::u8, 4> code;
        std::size_t expected_ands;
    };
    for (const auto& test : {
                 Case{{0x84, 0xc0, 0xf4, 0x00}, 0},
                 Case{{0x84, 0xe4, 0xf4, 0x00}, 1},
                 Case{{0x66, 0x85, 0xc0, 0xf4}, 0},
         }) {
        const auto& code = test.code;
        Block block{0, Location{reinterpret_cast<swift::VAddr>(code.data())}};
        swift::runtime::ir::Assembler assembler{&block};
        swift::x86::X64Decoder decoder{
                reinterpret_cast<swift::VAddr>(code.data()), &memory, &assembler,
                true, swift::x86::Arm64Features::None, false, false, FeatureSet{}};
        decoder.Decode();

        std::size_t and_count = 0;
        std::size_t or_count = 0;
        for (auto& inst : block.GetInstList()) {
            and_count += inst.GetOp() == OpCode::And;
            or_count += inst.GetOp() == OpCode::Or;
        }
        CAPTURE(static_cast<unsigned>(code[0]),
                static_cast<unsigned>(code[1]),
                static_cast<unsigned>(code[2]));
        REQUIRE(and_count == test.expected_ands);
        REQUIRE(or_count == 1);
    }
}
