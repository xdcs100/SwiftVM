#include "runtime/backend/link_manager.h"

#include <algorithm>

namespace swift::runtime::backend {

void LinkManager::DisableFlagsBypassLocked(const LinkSiteRecord& record) {
    const auto signal_site = signal_sites_.find(record.site);
    if (signal_site == signal_sites_.end()) {
        return;
    }
    const auto& bypass = signal_site->second->patch.flags_bypass;
    if (bypass.rx_site) {
        (void)PatchCodeWord(signal_site->second->patch.region,
                            bypass.rx_site,
                            bypass.rw_site,
                            bypass.unlinked_instruction);
    }
}

void LinkManager::TryEnableFlagsBypassLocked(LinkSiteRecord& record) {
    if (record.flags_bypass_offset == UINT32_MAX || !record.pending_flags_compatible) {
        return;
    }

    SignalSite* patch_site{};
    std::vector<std::pair<LinkSiteRecord*, TargetRecord*>> members;
    std::vector<SignalTarget*> signal_targets;
    for (auto& [key, member] : sites_) {
        if (key.region_id != record.site.region_id ||
            member.flags_bypass_offset != record.flags_bypass_offset) {
            continue;
        }
        if (member.state == LinkSiteState::Retiring) {
            return;
        }
        const auto signal_site = signal_sites_.find(key);
        if (signal_site == signal_sites_.end()) {
            return;
        }
        const auto& candidate = signal_site->second->patch.flags_bypass;
        if (!candidate.rx_site ||
            (patch_site &&
             (candidate.rx_site != patch_site->patch.flags_bypass.rx_site ||
              candidate.rw_site != patch_site->patch.flags_bypass.rw_site ||
              candidate.unlinked_instruction !=
                      patch_site->patch.flags_bypass.unlinked_instruction ||
              candidate.linked_instruction !=
                      patch_site->patch.flags_bypass.linked_instruction))) {
            return;
        }
        if (!patch_site) {
            patch_site = signal_site->second;
        }
        if (member.state != LinkSiteState::Linked) {
            if (signal_site->second->linked.load(std::memory_order_seq_cst)) {
                return;
            }
            continue;
        }
        if (!member.pending_flags_compatible) {
            return;
        }
        const auto target = targets_.find(member.guest_target);
        if (target == targets_.end() || !target->second.active ||
            target->second.generation != member.target_generation ||
            !target->second.signal_target ||
            !signal_site->second->linked.load(std::memory_order_seq_cst)) {
            return;
        }
        members.emplace_back(&member, &target->second);
        if (std::find(signal_targets.begin(),
                      signal_targets.end(),
                      target->second.signal_target) == signal_targets.end()) {
            signal_targets.push_back(target->second.signal_target);
        }
    }
    if (!patch_site) {
        return;
    }

    for (auto* target : signal_targets) {
        target->linking_count.fetch_add(1, std::memory_order_seq_cst);
    }
    bool valid = true;
    for (const auto& [member, target] : members) {
        valid &= target->active &&
                target->generation == member->target_generation &&
                target->signal_target->active_generation.load(
                        std::memory_order_seq_cst) == member->target_generation;
    }
    const auto& bypass = patch_site->patch.flags_bypass;
    if (valid) {
        (void)PatchCodeWord(patch_site->patch.region,
                            bypass.rx_site,
                            bypass.rw_site,
                            bypass.linked_instruction);
    }
    for (auto* target : signal_targets) {
        target->linking_count.fetch_sub(1, std::memory_order_seq_cst);
    }
}

}  // namespace swift::runtime::backend
