#pragma once

#include "base/common_funcs.h"
#include "runtime/common/types.h"
#include "runtime/include/config.h"
#include "runtime/ir/block.h"

namespace swift::runtime::backend::arm64 {

class GuestStateMap final {
public:
    void Analyze(ir::Block* block, const FeatureSet& features);

    [[nodiscard]] bool FixedHomeSurvives(u32 home,
                                         u32 after,
                                         u32 before) const;
    [[nodiscard]] bool PublicationWindowSafe(
            u32 home,
            u32 after,
            u32 before,
            const ir::Inst* ignored = nullptr) const;
    [[nodiscard]] bool MayFaultOrObserve(const ir::Inst& inst) const;
    [[nodiscard]] static bool MayFaultOrObserve(ir::OpCode op);

private:
    [[nodiscard]] bool ClobbersFixedHome(const ir::Inst& inst,
                                         u32 home) const;

    ir::Block* block{};
    FeatureSet features{};
};

}  // namespace swift::runtime::backend::arm64
