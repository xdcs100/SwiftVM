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
#include "runtime/ir/opts/flags_elimination_pass.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

struct LogicalFlagEmission {
    std::vector<std::string> instructions;
    bool extract_tied{};
};

struct DirectMemory final : swift::x86::MemoryInterface {
    bool Read(void* dest, size_t addr, size_t size) override {
        return std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
    }
    bool Write(void* src, size_t addr, size_t size) override {
        return std::memcpy(reinterpret_cast<void*>(addr), src, size);
    }
    void* GetPointer(void* src) override { return src; }
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

std::vector<std::string> Disassemble(arm64::JitContext& context) {
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> instructions;
    auto& masm = context.GetMasm();
    auto* first = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    for (auto* instruction = first; instruction < last;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        instructions.emplace_back(disassembler.GetOutput());
    }
    return instructions;
}

std::vector<std::string> EmitCompoundLogicalClear(bool exact_clear) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    auto module = address_space.GetDefaultModule();

    IntrusivePtr<Block> block{new Block(0, Location{0x8a20})};
    auto source = block->LoadUniform<TypedValue<ValueType::U32>>(
            Uniform{0, ValueType::U32});
    auto result = block->And(source, Operand{source}).SetType(ValueType::U32);
    const auto clear = exact_clear
            ? Flags::CV | Flags::AuxiliaryCarry
            : Flags::CV;
    block->ClearFlags(clear);
    block->SaveFlags(result, Flags::NZ | Flags::Parity);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();
    return Disassemble(context);
}

std::vector<std::string> EmitParityOnlyLogicalBeforeSelect() {
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

    IntrusivePtr<Block> block{new Block(0, Location{0x89e0})};
    auto source = block->LoadUniform<TypedValue<ValueType::U64>>(
            Uniform{0, ValueType::U64});
    auto parity = block->Or(source, Operand{Imm{swift::u64{0}}})
                          .SetType(ValueType::U64);
    block->SaveFlags(parity, Flags::Parity);
    auto condition = block->LoadImm(Imm{swift::u64{1}}).SetType(ValueType::U8);
    auto true_value = block->LoadImm(Imm{swift::u64{2}}).SetType(ValueType::U64);
    auto false_value = block->LoadImm(Imm{swift::u64{3}}).SetType(ValueType::U64);
    auto selected = block->Select(condition, true_value, false_value)
                            .SetType(ValueType::U64);
    block->StoreUniform(Uniform{8, ValueType::U64}, selected);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    arm64::JitContext context{module, alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();
    return Disassemble(context);
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

TEST_CASE("parity-only logical publication leaves NZCV clean") {
    const auto instructions = EmitParityOnlyLogicalBeforeSelect();
    REQUIRE_FALSE(Contains(instructions, "tst "));
    REQUIRE(Contains(instructions, "cmp "));
    REQUIRE(Contains(instructions, "csel "));
}

TEST_CASE("logical NZ publication absorbs an adjacent CVAF clear") {
    const auto exact = EmitCompoundLogicalClear(true);
    REQUIRE(Contains(exact, "#26, #6"));
    REQUIRE_FALSE(Contains(exact, "bfc x26, #26, #4"));

    const auto partial = EmitCompoundLogicalClear(false);
    REQUIRE_FALSE(Contains(partial, "#26, #6"));
}

TEST_CASE("narrow register self tests skip the redundant AND") {
    DirectMemory memory;

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

TEST_CASE("compact FP compare stays local across audited moves") {
    for (const auto& code : {
                 std::array<swift::u8, 24>{
                         0x66, 0x0f, 0x2f, 0xc1,
                         0xf2, 0x0f, 0x10, 0x15, 0x04, 0x00, 0x00, 0x00,
                         0x77, 0x01, 0xf4, 0xf4,
                 },
                 std::array<swift::u8, 24>{
                         0x66, 0x0f, 0x2f, 0xc1,
                         0x66, 0x0f, 0x28, 0xd0,
                         0x76, 0x01, 0xf4, 0xf4,
                 },
         }) {
        DirectMemory memory;
        const auto address = reinterpret_cast<swift::VAddr>(code.data());
        IntrusivePtr<Block> block{new Block(0, Location{address})};
        swift::runtime::ir::Assembler assembler{block.get()};
        FeatureSet features{};
        features.flags_fcmp_fuse = true;
        features.flags_fcmp_compact = true;
        const auto arm64_features =
                swift::x86::Arm64Features::AXFlag |
                swift::x86::Arm64Features::FlagM;
        swift::x86::X64Decoder decoder{
                address, &memory, &assembler, true,
                arm64_features, false, false, features};
        decoder.Decode();

        REQUIRE(std::count_if(block->GetInstList().begin(), block->GetInstList().end(),
                              [](const Inst& inst) {
                                  return inst.GetOp() == OpCode::FCmpCondSet;
                              }) == 1);
        REQUIRE(std::none_of(block->GetInstList().begin(), block->GetInstList().end(),
                             [](const Inst& inst) {
                                 return inst.GetOp() == OpCode::CondSet;
                             }));

        block->ReIdInstr();
        Config config{
                .loc_start = 0,
                .loc_end = 1ull << 48,
                .enable_jit = true,
                .has_local_operation = false,
                .backend_isa = kArm64,
                .global_opts = Optimizations::All,
                .arm64_features = arm64_features,
        };
        AddressSpace address_space{config};
        auto module = address_space.GetDefaultModule();
        RegAlloc alloc{block->MaxInstrId(),
                       address_space.GetTrampolines().GetGPRRegs(),
                       address_space.GetTrampolines().GetFPRRegs(), features};
        RegisterAllocPass::Run(block.get(), &alloc, false, features);
        arm64::JitContext context{module, alloc};
        arm64::JitTranslator translator{context};
        translator.Translate(block.get());
        context.Finish();

        const auto instructions = Disassemble(context);
        REQUIRE(Count(instructions, "cset") == 1);
        REQUIRE(Count(instructions, "axflag") == 1);
        REQUIRE(Count(instructions, "cfinv") == 1);
        REQUIRE((Contains(instructions, "b.hi") || Contains(instructions, "b.ls")));
    }
}

TEST_CASE("dead-edge FP compares branch on raw host flags") {
    struct Case {
        swift::u8 opcode;
        std::string_view condition;
        std::string_view inverse;
    };
    for (const auto& test : {
                 Case{0x72, "b.lt", "b.ge"},
                 Case{0x73, "b.ge", "b.lt"},
                 Case{0x77, "b.gt", "b.le"},
                 Case{0x76, "b.le", "b.gt"},
                 Case{0x7a, "b.vs", "b.vc"},
                 Case{0x7b, "b.vc", "b.vs"},
         }) {
        const std::array<swift::u8, 16> code{
                0x66, 0x0f, 0x2f, 0xc1,
                test.opcode, 0x04,
                0x39, 0xc0,
                0xf4, 0x90,
                0x39, 0xc9,
                0xf4,
        };
        DirectMemory memory;
        const auto address = reinterpret_cast<swift::VAddr>(code.data());
        IntrusivePtr<Block> block{new Block(0, Location{address})};
        Assembler assembler{block.get()};
        FeatureSet features{};
        const auto arm64_features = Arm64Features::AXFlag | Arm64Features::FlagM;
        swift::x86::X64Decoder decoder{
                address, &memory, &assembler, true, arm64_features, false, false,
                features};
        decoder.Decode();

        auto count_op = [&](OpCode op) {
            std::size_t count{};
            for (const auto& inst : block->GetInstList()) {
                count += inst.GetOp() == op;
            }
            return count;
        };
        CAPTURE(test.opcode);
        REQUIRE(count_op(OpCode::BranchOnlyEdges) == 1);
        REQUIRE(count_op(OpCode::PublishFCmpFlags) == 1);
        REQUIRE(count_op(OpCode::InvertCarry) == 1);

        FlagsEliminationPass::Run(block.get(), nullptr, features);
        REQUIRE(count_op(OpCode::BranchOnlyEdges) == 0);
        REQUIRE(count_op(OpCode::PublishFCmpFlags) == 0);
        REQUIRE(count_op(OpCode::InvertCarry) == 0);
        REQUIRE(count_op(OpCode::FCmpCondSet) == 1);

        block->ReIdInstr();
        Config config{
                .loc_start = 0,
                .loc_end = 1ull << 48,
                .enable_jit = true,
                .has_local_operation = false,
                .backend_isa = kArm64,
                .global_opts = Optimizations::All,
                .arm64_features = arm64_features,
        };
        AddressSpace address_space{config};
        RegAlloc alloc{block->MaxInstrId(),
                       address_space.GetTrampolines().GetGPRRegs(),
                       address_space.GetTrampolines().GetFPRRegs(), features};
        RegisterAllocPass::Run(block.get(), &alloc, false, features);
        arm64::JitContext context{address_space.GetDefaultModule(), alloc};
        arm64::JitTranslator translator{context};
        translator.Translate(block.get());
        context.Finish();

        const auto instructions = Disassemble(context);
        REQUIRE(Count(instructions, "fcmp") == 1);
        REQUIRE(Count(instructions, "cset") == 0);
        REQUIRE(Count(instructions, "axflag") == 0);
        REQUIRE(Count(instructions, "cfinv") == 0);
        REQUIRE((Contains(instructions, test.condition) ||
                 Contains(instructions, test.inverse)));
    }
}

TEST_CASE("dead-edge integer compares branch on raw host flags") {
    struct Case {
        swift::u8 opcode;
        std::string_view condition;
        std::string_view inverse;
    };
    for (const auto& test : {
                 Case{0x74, "b.eq", "b.ne"},
                 Case{0x75, "b.ne", "b.eq"},
                 Case{0x72, "b.lo", "b.hs"},
                 Case{0x73, "b.hs", "b.lo"},
                 Case{0x77, "b.hi", "b.ls"},
                 Case{0x76, "b.ls", "b.hi"},
         }) {
        const std::array<swift::u8, 12> code{
                0x3c, 0x50,
                test.opcode, 0x04,
                0x39, 0xc0,
                0xf4, 0x90,
                0x39, 0xc9,
                0xf4,
        };
        DirectMemory memory;
        const auto address = reinterpret_cast<swift::VAddr>(code.data());
        IntrusivePtr<Block> block{new Block(0, Location{address})};
        Assembler assembler{block.get()};
        FeatureSet features{};
        constexpr auto arm64_features = Arm64Features::FlagM;
        swift::x86::X64Decoder decoder{
                address, &memory, &assembler, true, arm64_features, false, false,
                features};
        decoder.Decode();

        auto count_op = [&](OpCode op) {
            std::size_t count{};
            for (const auto& inst : block->GetInstList()) {
                count += inst.GetOp() == op;
            }
            return count;
        };
        CAPTURE(test.opcode);
        REQUIRE(count_op(OpCode::BranchOnlyEdges) == 1);
        REQUIRE(count_op(OpCode::InvertCarry) == 1);

        FlagsEliminationPass::Run(block.get(), nullptr, features);
        REQUIRE(count_op(OpCode::BranchOnlyEdges) == 0);
        REQUIRE(count_op(OpCode::InvertCarry) == 1);
        REQUIRE(block->HasDeadEdgeIntegerBranchProof());

        block->ReIdInstr();
        Config config{
                .loc_start = 0,
                .loc_end = 1ull << 48,
                .enable_jit = true,
                .has_local_operation = false,
                .backend_isa = kArm64,
                .global_opts = Optimizations::All,
                .arm64_features = arm64_features,
        };
        AddressSpace address_space{config};
        RegAlloc alloc{block->MaxInstrId(),
                       address_space.GetTrampolines().GetGPRRegs(),
                       address_space.GetTrampolines().GetFPRRegs(), features};
        RegisterAllocPass::Run(block.get(), &alloc, false, features);
        arm64::JitContext context{address_space.GetDefaultModule(), alloc};
        arm64::JitTranslator translator{context};
        translator.Translate(block.get());
        context.Finish();

        const auto instructions = Disassemble(context);
        REQUIRE(Count(instructions, "subs") == 1);
        REQUIRE(Count(instructions, "cfinv") == 0);
        REQUIRE(Count(instructions, "cset") == 0);
        REQUIRE(Count(instructions, "mrs") == 0);
        REQUIRE(Count(instructions, "bfi") == 0);
        REQUIRE((Contains(instructions, test.condition) ||
                 Contains(instructions, test.inverse)));
    }
}
