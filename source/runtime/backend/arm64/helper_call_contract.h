#pragma once

#include <optional>

#include "runtime/common/svm_config.h"
#include "runtime/ir/instr.h"

namespace swift::runtime::backend::arm64 {

class HelperCallContract final {
public:
    [[nodiscard]] static HelperCallContract Resolve(const ir::Lambda& lambda,
                                                    const FeatureSet& features);
    [[nodiscard]] static std::optional<HelperCallContract> Resolve(const ir::Inst& inst,
                                                                   const FeatureSet& features);
    [[nodiscard]] static bool InstructionClobbersGPR(const ir::Inst& inst,
                                                     u32 code,
                                                     const FeatureSet& features);

    [[nodiscard]] bool IsDirect() const { return direct; }
    [[nodiscard]] bool PreserveAllLeaf() const { return preserve_all_leaf; }
    [[nodiscard]] bool FPCRTransparent() const { return fpcr_transparent; }
    [[nodiscard]] bool GeneralRegistersOnly() const { return general_registers_only; }
    [[nodiscard]] bool PreservesPinnedState() const { return preserves_pinned_state; }
    [[nodiscard]] ir::UniformEffectId UniformEffects() const { return uniform_effects; }
    [[nodiscard]] bool ClobbersGPR(u32 code) const;
    [[nodiscard]] bool ClobbersFPR(u32 code) const;
    [[nodiscard]] bool ArgumentRequiresSlot(u32 code) const;
    [[nodiscard]] bool RequiresGPRSnapshot(u32 code, bool argument_source) const;
    [[nodiscard]] bool RequiresFPRSnapshot(u32 code) const;

private:
    bool direct{};
    bool preserve_all_leaf{};
    bool fpcr_transparent{};
    bool general_registers_only{};
    bool preserves_pinned_state{};
    ir::UniformEffectId uniform_effects{ir::UniformEffectId::Unknown};
};

}  // namespace swift::runtime::backend::arm64
