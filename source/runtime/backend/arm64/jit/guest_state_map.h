#pragma once

#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include "base/common_funcs.h"
#include "runtime/common/types.h"
#include "runtime/include/config.h"
#include "runtime/ir/block.h"

namespace swift::runtime::ir {
class HIRFunction;
}

namespace swift::runtime::backend::arm64 {

class GuestStateMap final {
public:
    struct FixedHomeValue {
        u16 home{};
        u8 width{};
        bool known_zero_above_32{};

        bool operator==(const FixedHomeValue&) const = default;
    };

    struct CoalescedWrite {
        ir::Inst* publication{};
        u16 home{};
    };

    void AnalyzeFunction(ir::HIRFunction* function,
                         const FeatureSet& features);
    [[nodiscard]] bool EntryKnownZeroAbove32(const ir::Block* block,
                                             u32 home);
    void Analyze(ir::Block* block, const FeatureSet& features);
    void BuildValueVersions(
            const std::unordered_set<ir::Inst*>& ignored_publications,
            std::span<const CoalescedWrite> coalesced_writes,
            bool has_reused_publication);

    [[nodiscard]] bool FixedHomeSurvives(u32 home,
                                         u32 after,
                                         u32 before) const;
    [[nodiscard]] bool PublicationWindowSafe(
            u32 home,
            ir::Value early_value,
            u32 after,
            u32 before,
            const ir::Inst* ignored = nullptr) const;
    [[nodiscard]] std::optional<FixedHomeValue> FixedHomeForUse(
            ir::Value value,
            const ir::Inst* consumer) const;
    void RegisterFixedHomeUse(ir::Inst* version,
                              const ir::Inst* consumer,
                              FixedHomeValue location);
    [[nodiscard]] std::optional<FixedHomeValue> RegisteredFixedHomeForUse(
            ir::Value value,
            const ir::Inst* consumer) const;
    [[nodiscard]] bool ValueFullyResident(ir::Inst* definition) const;
    [[nodiscard]] bool MayFaultOrObserve(const ir::Inst& inst) const;
    [[nodiscard]] static bool MayFaultOrObserve(ir::OpCode op);

private:
    using WidthFacts = std::bitset<30>;

    struct ActiveValue {
        ir::Inst* version{};
        FixedHomeValue location{};
        u32 publication{};
    };

    struct ActiveState {
        std::unordered_map<ir::Inst*, StackVector<ActiveValue, 2>> versions{};
        std::array<StackVector<ir::Inst*, 4>, 30> homes{};
    };

    struct FaultSnapshotValue {
        const ir::Inst* boundary{};
        ir::Inst* version{};
        FixedHomeValue location{};
    };

    struct FaultWidthSnapshot {
        const ir::Inst* boundary{};
        WidthFacts known_zero_above_32{};
    };

    [[nodiscard]] bool ClobbersFixedHome(const ir::Inst& inst,
                                         u32 home) const;
    void BuildFunctionWidthFacts();
    void PrepareCurrentEntryWidthFacts(bool fault_snapshot_needed);
    [[nodiscard]] bool CurrentEntryKnownZeroAbove32(u32 home) const;
    [[nodiscard]] bool ValueKnownZeroAbove32(ir::Value value,
                                             const ActiveState& active) const;
    [[nodiscard]] bool FaultSnapshotContains(const ir::Inst& boundary,
                                             u32 home,
                                             ir::Inst* version,
                                             bool require_zero_above_32) const;
    [[nodiscard]] bool NeedsFaultSnapshots() const;
    void CaptureFaultSnapshot(const ir::Inst& boundary,
                              const ActiveState& active,
                              const WidthFacts& width_facts);
    void PublishValue(ir::Value value,
                      u32 home,
                      u32 publication,
                      bool known_zero_above_32,
                      ActiveState& active);
    void InvalidateHome(u32 home, ActiveState& active) const;

    ir::Block* block{};
    ir::HIRFunction* function{};
    FeatureSet features{};
    bool function_width_facts_ready{};
    WidthFacts block_entry_width_facts{};
    std::unordered_map<const ir::Block*, WidthFacts> function_entry_width_facts{};
    StackVector<FaultSnapshotValue, 16> fault_snapshot_values{};
    StackVector<FaultWidthSnapshot, 8> fault_width_snapshots{};
    std::map<std::pair<ir::Inst*, const ir::Inst*>, FixedHomeValue> fixed_home_uses{};
    std::map<ir::Inst*, u32> fixed_home_use_counts{};
    std::set<std::pair<ir::Inst*, const ir::Inst*>> registered_fixed_home_uses{};
};

}  // namespace swift::runtime::backend::arm64
