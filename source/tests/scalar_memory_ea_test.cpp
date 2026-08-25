#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <vector>

#include "runtime/common/svm_config.h"
#include "runtime/frontend/x86/decoder.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::ir;

class DirectMemory final : public MemoryInterface {
public:
    bool Read(void* dest, size_t addr, size_t size) override {
        std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
        return true;
    }

    bool Write(void* src, size_t addr, size_t size) override {
        std::memcpy(reinterpret_cast<void*>(addr), src, size);
        return true;
    }

    void* GetPointer(void* src) override { return src; }
};

std::vector<Operand> DecodeScalarMemoryOperands(bool identity) {
    const std::array<swift::u8, 16> code{
            0xf2, 0x0f, 0x10, 0x48, 0x08,
            0xf2, 0x0f, 0x58, 0x48, 0x10,
            0xf2, 0x0f, 0x11, 0x48, 0x18,
            0xf4,
    };
    const auto address = reinterpret_cast<swift::VAddr>(code.data());
    DirectMemory memory;
    Block block{0, Location{address}};
    Assembler assembler{&block};
    swift::x86::X64Decoder decoder{address,
                                   &memory,
                                   &assembler,
                                   true,
                                   swift::x86::Arm64Features::None,
                                   false,
                                   identity,
                                   FeatureSet{}};
    decoder.Decode();

    std::vector<Operand> operands;
    for (auto& inst : block.GetInstList()) {
        if (inst.GetOp() == OpCode::LoadMemory || inst.GetOp() == OpCode::StoreMemory) {
            operands.push_back(inst.GetArg<Operand>(0));
        }
    }
    return operands;
}

}  // namespace

TEST_CASE("scalar SSE memory operands remain composite in identity mode") {
    const auto identity = DecodeScalarMemoryOperands(true);
    const auto biased = DecodeScalarMemoryOperands(false);

    REQUIRE(identity.size() == 3);
    REQUIRE(biased.size() == 3);
    if (GetSvmConfig().addr_ea_tie) {
        for (std::size_t i = 0; i < identity.size(); ++i) {
            REQUIRE(identity[i].GetOp() == OperandOp::Plus);
            REQUIRE(identity[i].GetLeft().IsValue());
            REQUIRE(identity[i].GetRight().IsImm());
            REQUIRE(identity[i].GetRight().imm.Get() == (i + 1) * 8);
        }
    } else {
        for (const auto& operand : identity) {
            REQUIRE(operand.GetRight().Null());
        }
    }
    for (const auto& operand : biased) {
        REQUIRE(operand.GetRight().Null());
        REQUIRE(operand.GetLeft().IsValue());
        REQUIRE(operand.GetLeft().value.Def()->GetOp() == OpCode::GetOperand);
    }
}
