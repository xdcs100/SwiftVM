#include <catch2/catch_test_macros.hpp>

#include "runtime/backend/arm64/helper_call_contract.h"
#include "runtime/common/helper_abi.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend::arm64;
using namespace swift::runtime::ir;

TEST_CASE("helper call contract resolves register and state effects", "[helper-contract]") {
    FeatureSet features{};
    features.helper_leaf_abi = true;

    const auto conservative = HelperCallContract::Resolve(Lambda{Imm{1}}, features);
    REQUIRE(conservative.IsDirect());
    REQUIRE(conservative.ClobbersGPR(7));
    REQUIRE_FALSE(conservative.ClobbersGPR(20));
    REQUIRE(conservative.ClobbersFPR(20));
    REQUIRE(conservative.UniformEffects() == UniformEffectId::Unknown);

    const auto resident = HelperCallContract::Resolve(
            Lambda{DataClass{Imm{1}},
                   HelperCallTraits{
                           .uniform = UniformEffectId::None,
                           .host_fp = HostFpEffect::FPCRTransparent,
                           .host_registers = HostRegisterEffect::PreservesPinnedState,
                   }},
            features);
    REQUIRE(resident.FPCRTransparent());
    REQUIRE(resident.PreservesPinnedState());
    REQUIRE(resident.UniformEffects() == UniformEffectId::None);
    REQUIRE(resident.ClobbersGPR(2));
    REQUIRE_FALSE(resident.ClobbersGPR(7));
    REQUIRE(resident.ClobbersGPR(11));
    REQUIRE_FALSE(resident.ClobbersGPR(20));
    REQUIRE(resident.ClobbersFPR(7));
    REQUIRE_FALSE(resident.ClobbersFPR(20));
    REQUIRE_FALSE(resident.ArgumentRequiresSlot(7));
    REQUIRE(resident.ArgumentRequiresSlot(2));
    REQUIRE(resident.RequiresGPRSnapshot(11, true));

    const auto leaf = HelperCallContract::Resolve(
            Lambda{DataClass{Imm{1}}, HelperCallTraits{.abi = HelperABI::PreserveAllLeaf}},
            features);
#if SVM_HAS_HELPER_PRESERVE_ALL
    REQUIRE(leaf.PreserveAllLeaf());
    REQUIRE_FALSE(leaf.ClobbersGPR(9));
    REQUIRE_FALSE(leaf.ClobbersFPR(8));
#else
    REQUIRE_FALSE(leaf.PreserveAllLeaf());
#endif
}

}  // namespace
