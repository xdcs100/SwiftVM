#include "translator.h"

#include "runtime/backend/arm64/defines.h"
#include "runtime/frontend/x86/sse42str_helper.h"

namespace swift::runtime::backend::arm64 {

#define __ masm.

void JitTranslator::EmitSse42StrVectorCall(const VRegister& left,
                                           const VRegister& right,
                                           const WRegister& result,
                                           VAddr target) {
    using ABI = swift::x86::Sse42StrVectorCallABI;

    const auto live_gprs = context.GetLiveGPRs();
    boost::container::small_vector<u32, 8> save_gprs;
    for (u32 code = 0; code < 32; ++code) {
        if ((ABI::GPRClobbers & (1u << code)) && live_gprs.Get(code)) {
            save_gprs.push_back(code);
        }
    }

    auto live_fprs = context.GetLiveFPRs();
    live_fprs.Mark(left.GetCode());
    live_fprs.Mark(right.GetCode());
    live_fprs.Mark(0);
    live_fprs.Mark(1);
    boost::container::small_vector<u32, 8> save_fprs;
    for (u32 code = 0; code < 8; ++code) {
        if ((ABI::FPRClobbers & (1u << code)) && live_fprs.Get(code)) {
            save_fprs.push_back(code);
        }
    }

    std::array<int, 32> gpr_slots{};
    std::array<int, 8> fpr_slots{};
    gpr_slots.fill(-1);
    fpr_slots.fill(-1);
    u32 cursor{};
    for (u32 code : save_gprs) {
        gpr_slots[code] = int(cursor);
        cursor += 8;
    }
    const u32 link_slot = cursor;
    cursor += 8;
    const u32 result_slot = cursor;
    cursor += 8;
    const u32 simd_offset = (cursor + 15u) & ~15u;
    cursor = simd_offset;
    for (u32 code : save_fprs) {
        fpr_slots[code] = int(cursor);
        cursor += 16;
    }
    const u32 save_bytes = (cursor + 15u) & ~15u;

    __ Sub(sp, sp, save_bytes);
    for (size_t i = 0; i + 1 < save_gprs.size(); i += 2) {
        __ Stp(XRegister(save_gprs[i]),
               XRegister(save_gprs[i + 1]),
               MemOperand(sp, gpr_slots[save_gprs[i]]));
    }
    if (save_gprs.size() & 1u) {
        __ Str(XRegister(save_gprs.back()),
               MemOperand(sp, gpr_slots[save_gprs.back()]));
    }
    __ Str(x30, MemOperand(sp, link_slot));
    for (size_t i = 0; i + 1 < save_fprs.size(); i += 2) {
        __ Stp(VRegister::GetQRegFromCode(save_fprs[i]),
               VRegister::GetQRegFromCode(save_fprs[i + 1]),
               MemOperand(sp, fpr_slots[save_fprs[i]]));
    }
    if (save_fprs.size() & 1u) {
        __ Str(VRegister::GetQRegFromCode(save_fprs.back()),
               MemOperand(sp, fpr_slots[save_fprs.back()]));
    }

    auto load_argument = [&](const VRegister& destination,
                             const VRegister& source) {
        if (source.GetCode() < 8) {
            ASSERT(fpr_slots[source.GetCode()] >= 0);
            __ Ldr(destination,
                   MemOperand(sp, fpr_slots[source.GetCode()]));
        } else if (destination.GetCode() != source.GetCode()) {
            __ Orr(destination.V16B(), source.V16B(), source.V16B());
        }
    };
    load_argument(v0.Q(), left);
    load_argument(v1.Q(), right);

    const ir::Lambda lambda{
            ir::DataClass{ir::Imm{target}},
            ir::HelperCallTraits{
                    .uniform = ir::UniformEffectId::None,
                    .host_fp = ir::HostFpEffect::FPCRTransparent,
            }};
    if (!TryEmitSharedHostCall(lambda)) {
        MaterializeHostCallTarget(target);
        __ Blr(ip);
    }
    __ Str(WRegister(ABI::ResultGPR), MemOperand(sp, result_slot));

    for (size_t i = 0; i + 1 < save_fprs.size(); i += 2) {
        __ Ldp(VRegister::GetQRegFromCode(save_fprs[i]),
               VRegister::GetQRegFromCode(save_fprs[i + 1]),
               MemOperand(sp, fpr_slots[save_fprs[i]]));
    }
    if (save_fprs.size() & 1u) {
        __ Ldr(VRegister::GetQRegFromCode(save_fprs.back()),
               MemOperand(sp, fpr_slots[save_fprs.back()]));
    }
    for (size_t i = 0; i + 1 < save_gprs.size(); i += 2) {
        __ Ldp(XRegister(save_gprs[i]),
               XRegister(save_gprs[i + 1]),
               MemOperand(sp, gpr_slots[save_gprs[i]]));
    }
    if (save_gprs.size() & 1u) {
        __ Ldr(XRegister(save_gprs.back()),
               MemOperand(sp, gpr_slots[save_gprs.back()]));
    }
    __ Ldr(x30, MemOperand(sp, link_slot));
    __ Ldr(result, MemOperand(sp, result_slot));
    __ Add(sp, sp, save_bytes);

    flags_set = ir::Flags::None;
    flags_clear = ir::Flags::None;
    nzcv_dirty = false;
    nzcv_requested = {};
    InvalidateFlagsToken();
}

#undef __

}  // namespace swift::runtime::backend::arm64
