#include <array>

#include <catch2/catch_test_macros.hpp>

#include "translator/x86/function_region_membership.h"

using swift::runtime::LocationDescriptor;
using swift::runtime::IntrusivePtr;
using swift::runtime::ir::Function;
using swift::runtime::ir::Location;
using swift::translator::x86::FunctionRegionMembership;

TEST_CASE("function region membership regroups observed roots") {
    FunctionRegionMembership membership;
    IntrusivePtr<Function> owner{new Function(Location{0x1000})};
    const FunctionRegionMembership::Region region{0x1000, 50};
    const std::array<LocationDescriptor, 3> pending{0x1200, 0x1300, 0x1200};
    membership.Record(owner, region, pending);

    const auto selection = membership.Select(0x1200);
    REQUIRE(selection.has_value());
    REQUIRE(selection->retired_owner == owner);
    REQUIRE(selection->retired_region == region);
    REQUIRE_FALSE(membership.Select(0x1300).has_value());
}

TEST_CASE("function region membership enforces the code object block budget") {
    FunctionRegionMembership membership;
    IntrusivePtr<Function> owner{new Function(Location{0x2000})};
    const FunctionRegionMembership::Region region{0x2000, 65};
    const std::array<LocationDescriptor, 1> pending{0x2100};
    membership.Record(owner, region, pending);

    REQUIRE_FALSE(membership.Select(0x2100).has_value());
}
