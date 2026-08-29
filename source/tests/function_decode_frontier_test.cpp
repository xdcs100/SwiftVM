#include <catch2/catch_test_macros.hpp>

#include "runtime/ir/hir_builder.h"
#include "translator/x86/function_decode_frontier.h"

namespace {

using namespace swift::runtime;
using namespace swift::runtime::ir;
using swift::translator::x86::FunctionDecodeFrontier;

TEST_CASE("function decode frontier preserves split provenance",
          "[function-entry][frontier]") {
    constexpr LocationDescriptor kStart = 0x1000;
    constexpr LocationDescriptor kTarget = kStart + 4;
    constexpr LocationDescriptor kEnd = kStart + 8;

    HIRBuilder builder{4, true, false, FeatureSet{}};
    auto* function = builder.AppendFunction(Location{kStart});
    auto* owner = function->GetCurrentBlock();
    builder.AdvancePC(Imm{kEnd - kStart});
    builder.AddGuestCodeDependency(Location{kStart + 1}, Location{kTarget});
    REQUIRE(function->CreateOrGetBlock(Location{kTarget}) != nullptr);

    FunctionDecodeFrontier frontier{function};
    const auto split = frontier.FindSplit(kTarget);
    REQUIRE(split);
    REQUIRE(split->owner == owner);
    REQUIRE(split->provenance != nullptr);
    REQUIRE(split->provenance->owner_start == Location{kStart});
    REQUIRE(split->provenance->owner_end == Location{kEnd});
    REQUIRE(split->provenance->dependencies ==
            std::vector<FunctionEntrySourceRange>{
                    {Location{kStart + 1}, Location{kTarget}}});

    REQUIRE(builder.ResetDecodedBlock(owner));
    builder.AdvancePC(Imm{kTarget - kStart});
    frontier.Accept(kTarget);
    REQUIRE(frontier.IsAccepted(kTarget));
    REQUIRE_FALSE(frontier.FindSplit(kTarget));

    function->SetFunctionEntryProvenance(frontier.ExportProvenance());
    const auto& provenance = function->GetFunctionEntryProvenance();
    REQUIRE(provenance.size() == 1);
    REQUIRE(provenance.front().IsAccepted());
    REQUIRE(provenance.front().dependencies ==
            std::vector<FunctionEntrySourceRange>{
                    {Location{kStart + 1}, Location{kTarget}}});
}

TEST_CASE("function decode frontier records stable rejection reasons",
          "[function-entry][frontier]") {
    constexpr LocationDescriptor kStart = 0x2000;
    constexpr LocationDescriptor kReturn = kStart + 4;
    constexpr LocationDescriptor kEnd = kStart + 8;

    HIRBuilder builder{4, true, false, FeatureSet{}};
    auto* function = builder.AppendFunction(Location{kStart});
    builder.AdvancePC(Imm{kEnd - kStart});
    builder.RegisterCallReturn(Location{kReturn});

    FunctionDecodeFrontier frontier{function};
    REQUIRE_FALSE(frontier.FindSplit(kReturn));
    const auto* call_return = frontier.FindProvenance(kReturn);
    REQUIRE(call_return != nullptr);
    REQUIRE(call_return->rejection ==
            FunctionEntryRejection::CallReturnOwnership);
    REQUIRE_FALSE(frontier.FindSplit(kReturn));

    constexpr LocationDescriptor kMissing = kEnd + 4;
    REQUIRE_FALSE(frontier.FindSplit(kMissing));
    const auto* missing = frontier.FindProvenance(kMissing);
    REQUIRE(missing != nullptr);
    REQUIRE(missing->rejection == FunctionEntryRejection::NoOwner);
    REQUIRE_FALSE(frontier.FindSplit(kMissing));
}

}  // namespace
