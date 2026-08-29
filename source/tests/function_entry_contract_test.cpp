#include <catch2/catch_test_macros.hpp>

#include "runtime/backend/function_entry_contract.h"
#include "runtime/ir/hir_builder.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::backend;
using namespace swift::runtime::ir;

TEST_CASE("function entry contract captures external entry requirements",
          "[function-entry][contract]") {
    constexpr LocationDescriptor kStart = 0x3000;
    constexpr LocationDescriptor kTarget = kStart + 4;
    constexpr LocationDescriptor kEnd = kStart + 8;

    HIRBuilder builder{4, true, false, FeatureSet{}};
    auto* function = builder.AppendFunction(Location{kStart});
    auto* root = function->GetCurrentBlock();
    root->GetBlock()->SetEndLocation(Location{kEnd});
    auto* target = function->CreateOrGetBlock(Location{kTarget});
    target->GetBlock()->SetEndLocation(Location{kEnd});
    target->GetBlock()->AddGuestCodeDependency(Location{kStart + 1},
                                                Location{kTarget});
    function->SetFunctionEntryProvenance({
            {
                    .target = Location{kTarget},
                    .owner_start = Location{kStart},
                    .owner_end = Location{kEnd},
                    .disposition = FunctionEntryDisposition::Accepted,
            },
    });

    const EdgeFlagsTargetContract pending{
            .overwrite_before_observe = kEdgeNZCVMask,
            .commits_before_fault = true,
    };
    const auto contract = FunctionEntryContract::Build(
            *function,
            *target->GetBlock(),
            {
                    .canonical = 0,
                    .direct_link = 4,
                    .pending_flags = 8,
                    .continuation = 12,
                    .pending_flags_continuation = 16,
            },
            pending);

    REQUIRE(contract.IsWellFormed(20));
    REQUIRE(contract.Origin() == FunctionEntryOrigin::AcceptedSplit);
    REQUIRE(contract.Provenance());
    REQUIRE(contract.Dependencies() ==
            std::vector<FunctionEntryDependency>{
                    {Location{kStart + 1}, Location{kTarget}}});
    REQUIRE(contract.Canonical().flags ==
            FunctionEntryFlagsRequirement::Canonical);
    REQUIRE(contract.Canonical().guest_state ==
            FunctionEntryGuestStateRequirement::CanonicalFixedHomes);
    REQUIRE(contract.PendingFlags().flags ==
            FunctionEntryFlagsRequirement::Pending);
    REQUIRE(contract.Continuation().continuation ==
            FunctionEntryContinuationRequirement::PublishedFrame);
    REQUIRE(contract.PendingFlagsContinuation().continuation ==
            FunctionEntryContinuationRequirement::PublishedFrame);
    REQUIRE_FALSE(contract.IsWellFormed(16));

    const auto pending_call_only = FunctionEntryContract::Build(
            *function,
            *target->GetBlock(),
            {
                    .canonical = 0,
                    .pending_flags = 4,
                    .pending_flags_continuation = 8,
            },
            pending);
    REQUIRE(pending_call_only.IsWellFormed(12));

    const auto root_contract = FunctionEntryContract::Build(
            *function,
            *root->GetBlock(),
            {.canonical = 0, .direct_link = 0},
            {});
    REQUIRE(root_contract.IsWellFormed(4));
    REQUIRE(root_contract.Origin() == FunctionEntryOrigin::FunctionRoot);
    REQUIRE_FALSE(root_contract.DirectLink().Present());

    function->SetFunctionEntryProvenance({
            {
                    .target = Location{kTarget},
                    .owner_start = Location{kStart},
                    .owner_end = Location{kEnd},
                    .disposition = FunctionEntryDisposition::Rejected,
                    .rejection = FunctionEntryRejection::BoundaryMismatch,
            },
    });
    const auto rejected = FunctionEntryContract::Build(
            *function,
            *target->GetBlock(),
            {
                    .canonical = 0,
                    .direct_link = 4,
                    .pending_flags = 8,
                    .continuation = 12,
                    .pending_flags_continuation = 16,
            },
            pending);
    REQUIRE(rejected.IsWellFormed(20));
    REQUIRE(rejected.Origin() == FunctionEntryOrigin::RejectedSplit);
    REQUIRE_FALSE(rejected.Linkable());
    REQUIRE_FALSE(rejected.DirectLink().Present());
    REQUIRE_FALSE(rejected.PendingFlags().Present());
    REQUIRE_FALSE(rejected.Continuation().Present());
    REQUIRE_FALSE(rejected.PendingFlagsContinuation().Present());
}

}  // namespace
