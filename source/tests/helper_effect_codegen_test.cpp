#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "aarch64/disasm-aarch64.h"
#include "runtime/backend/address_space.h"
#include "runtime/backend/arm64/jit/jit_context.h"
#include "runtime/backend/arm64/jit/translator.h"
#include "runtime/backend/runtime.h"
#include "runtime/ir/opts/register_alloc_pass.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

std::vector<std::string> EmitHelperBoundary(bool exact) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
    };
    AddressSpace address_space{config};
    IntrusivePtr<Block> block{new Block(0, Location{0xb140})};
    auto source = block->LoadUniform<TypedValue<ValueType::U64>>(
            Uniform{0, ValueType::U64});
    auto first = block->Add(source, Operand{Imm{swift::u64{1}}})
                         .SetType(ValueType::U64);
    block->SaveFlags(first, Flags::NZCV);

    HelperCallTraits traits{};
    if (exact) {
        traits.guest_state = HelperGuestStateEffect::None;
        traits.fault = HelperFaultEffect::NoDirectFault;
        traits.reentry = HelperReentryEffect::NoReentry;
        traits.host_flags = HostFlagsEffect::PreservesNZCV;
    }
    (void)block->CallLambda(Lambda{DataClass{Imm{swift::u64{1}}}, traits});

    auto second = block->Sub(source, Operand{Imm{swift::u64{1}}})
                          .SetType(ValueType::U64);
    block->SaveFlags(second, Flags::NZCV);
    block->StoreUniform(Uniform{8, ValueType::U64}, second);
    block->SetTerminal(terminal::ReturnToDispatch{});
    block->ReIdInstr();

    FeatureSet features{};
    RegAlloc alloc{block->MaxInstrId(),
                   address_space.GetTrampolines().GetGPRRegs(),
                   address_space.GetTrampolines().GetFPRRegs(), features};
    RegisterAllocPass::Run(block.get(), &alloc, false, features);
    arm64::JitContext context{address_space.GetDefaultModule(), alloc};
    arm64::JitTranslator translator{context};
    translator.Translate(block.get());
    context.Finish();

    auto& masm = context.GetMasm();
    auto* first_instruction = masm.GetBuffer()->GetStartAddress<
            const vixl::aarch64::Instruction*>();
    auto* last_instruction = masm.GetBuffer()->GetEndAddress<
            const vixl::aarch64::Instruction*>();
    vixl::aarch64::Decoder decoder;
    vixl::aarch64::Disassembler disassembler;
    decoder.AppendVisitor(&disassembler);
    std::vector<std::string> instructions;
    for (auto* instruction = first_instruction; instruction < last_instruction;
         instruction = instruction->GetNextInstruction()) {
        decoder.Decode(instruction);
        instructions.emplace_back(disassembler.GetOutput());
    }
    return instructions;
}

std::size_t Count(const std::vector<std::string>& instructions,
                  std::string_view needle) {
    return std::ranges::count_if(instructions, [&](const auto& instruction) {
        return instruction.find(needle) != std::string::npos;
    });
}

#if defined(__aarch64__)
extern "C" swift::u64 SwiftRepStos1Resident(swift::u64, swift::u64, swift::u64);

swift::u64 RunPendingFlags(bool call_helper) {
    Config config{
            .loc_start = 0,
            .loc_end = 1ull << 48,
            .enable_jit = true,
            .has_local_operation = false,
            .backend_isa = kArm64,
            .uniform_buffer_size = 16,
    };
    AddressSpace address_space{config};
    constexpr swift::u64 guest = 0xb180;
    IntrusivePtr<Block> block{new Block(0, Location{guest})};
    auto left = block->LoadImm(Imm{swift::u64{0}}).SetType(ValueType::U64);
    auto result = block->Sub(left, Operand{Imm{swift::u64{1}}})
                          .SetType(ValueType::U64);
    auto token = block->SaveFlags(result, Flags::NZCV);
    if (call_helper) {
        const auto helper_address = reinterpret_cast<swift::VAddr>(
                &SwiftRepStos1Resident);
        (void)block->CallLambda(
                Lambda{DataClass{Imm{helper_address}},
                       HelperCallTraits{
                               .host_fp = HostFpEffect::FPCRTransparent,
                               .host_registers = HostRegisterEffect::PreservesPinnedState,
                               .guest_state = HelperGuestStateEffect::None,
                               .fault = HelperFaultEffect::NoDirectFault,
                               .reentry = HelperReentryEffect::NoReentry,
                               .host_flags = HostFlagsEffect::PreservesNZCV,
                       }},
                Imm{swift::u64{0}},
                Imm{swift::u64{0}},
                Imm{swift::u64{0}});
    }
    auto flags = block->GetFlags(token, Flags::NZCV).SetType(ValueType::U64);
    block->StoreUniform(Uniform{0, ValueType::U64}, flags);
    block->SetTerminal(terminal::LinkBlock{Location{guest + 1}});
    block->ReIdInstr();
    REQUIRE(TranslateIR(address_space.GetDefaultModule(), block) != nullptr);

    Runtime runtime{&address_space};
    runtime.SetLocation(guest);
    REQUIRE(runtime.Run() == HaltReason::CodeMiss);
    swift::u64 value{};
    std::memcpy(&value, runtime.GetUniformBuffer().data(), sizeof(value));
    return value;
}
#endif

}  // namespace

TEST_CASE("exact helper effects retain pending NZCV across host calls",
          "[helper-effects][codegen]") {
    const auto conservative = EmitHelperBoundary(false);
    const auto exact = EmitHelperBoundary(true);
    REQUIRE(Count(conservative, "nzcv") == 2);
    REQUIRE(Count(exact, "nzcv") == 1);
}

TEST_CASE("resident string helper preserves pending NZCV",
          "[helper-effects][production]") {
#if defined(__aarch64__)
    REQUIRE(RunPendingFlags(true) == RunPendingFlags(false));
#else
    SUCCEED("resident string helper requires an AArch64 host");
#endif
}
