#include "runtime/backend/function_entry_contract.h"

#include <algorithm>

#include "runtime/backend/address_space.h"
#include "runtime/backend/module.h"
#include "runtime/common/logging.h"
#include "runtime/ir/block.h"
#include "runtime/ir/hir_builder.h"

namespace swift::runtime::backend {

namespace {

FunctionEntryPointContract Entry(FunctionEntryKind kind,
                                 FunctionEntryFlagsRequirement flags,
                                 FunctionEntryContinuationRequirement continuation,
                                 u32 code_offset) {
    return {
            .kind = kind,
            .flags = flags,
            .continuation = continuation,
            .code_offset = code_offset,
    };
}

bool EntryIsWellFormed(const FunctionEntryPointContract& entry,
                       FunctionEntryKind kind,
                       FunctionEntryFlagsRequirement flags,
                       FunctionEntryContinuationRequirement continuation,
                       u32 allocation_size) {
    return entry.kind == kind && entry.flags == flags &&
           entry.continuation == continuation &&
           entry.guest_state ==
                   FunctionEntryGuestStateRequirement::CanonicalFixedHomes &&
           (!entry.Present() || entry.code_offset < allocation_size);
}

}  // namespace

FunctionEntryContract FunctionEntryContract::Restore(
        ir::Location guest,
        ir::Location guest_end,
        FunctionEntryCodeOffsets offsets,
        EdgeFlagsTargetContract pending_contract,
        bool linkable) {
    FunctionEntryContract contract{};
    contract.guest = guest;
    contract.guest_end = guest_end;
    contract.origin = FunctionEntryOrigin::SerializedCodeObject;
    contract.pending_flags_contract = pending_contract;
    contract.linkable = linkable;
    contract.canonical = Entry(
            FunctionEntryKind::Canonical,
            FunctionEntryFlagsRequirement::Canonical,
            FunctionEntryContinuationRequirement::None,
            offsets.canonical);
    contract.direct_link = Entry(
            FunctionEntryKind::DirectLink,
            FunctionEntryFlagsRequirement::Canonical,
            FunctionEntryContinuationRequirement::None,
            offsets.direct_link == offsets.canonical
                    ? kInvalidFunctionEntryOffset
                    : offsets.direct_link);
    contract.pending_flags = Entry(
            FunctionEntryKind::PendingFlags,
            FunctionEntryFlagsRequirement::Pending,
            FunctionEntryContinuationRequirement::None,
            offsets.pending_flags);
    contract.continuation = Entry(
            FunctionEntryKind::Continuation,
            FunctionEntryFlagsRequirement::Canonical,
            FunctionEntryContinuationRequirement::PublishedFrame,
            offsets.continuation);
    contract.pending_flags_continuation = Entry(
            FunctionEntryKind::PendingFlagsContinuation,
            FunctionEntryFlagsRequirement::Pending,
            FunctionEntryContinuationRequirement::PublishedFrame,
            offsets.pending_flags_continuation);

    return contract;
}

FunctionEntryContract FunctionEntryContract::Build(
        ir::HIRFunction& function,
        ir::Block& block,
        FunctionEntryCodeOffsets offsets,
        EdgeFlagsTargetContract pending_contract) {
    auto contract = Restore(block.GetStartLocation(),
                            block.GetEndLocation(),
                            offsets,
                            pending_contract);
    contract.origin = FunctionEntryOrigin::DecodedBlock;

    contract.dependencies.reserve(block.GetGuestCodeDependencies().size());
    for (const auto& dependency : block.GetGuestCodeDependencies()) {
        contract.dependencies.push_back({dependency.start, dependency.end});
    }

    const auto& provenance = function.GetFunctionEntryProvenance();
    const auto source = std::find_if(
            provenance.begin(), provenance.end(), [&](const auto& item) {
                return item.target == contract.guest;
            });
    if (source != provenance.end()) {
        if (source->IsAccepted()) {
            contract.origin = FunctionEntryOrigin::AcceptedSplit;
            contract.provenance = *source;
        } else if (source->rejection != ir::FunctionEntryRejection::NoOwner) {
            contract.origin = FunctionEntryOrigin::RejectedSplit;
            contract.provenance = *source;
            contract.linkable = false;
            contract.direct_link.code_offset = kInvalidFunctionEntryOffset;
            contract.pending_flags.code_offset = kInvalidFunctionEntryOffset;
            contract.continuation.code_offset = kInvalidFunctionEntryOffset;
            contract.pending_flags_continuation.code_offset =
                    kInvalidFunctionEntryOffset;
        }
    } else if (function.GetFunction()->GetStartLocation() == contract.guest) {
        contract.origin = FunctionEntryOrigin::FunctionRoot;
    }
    return contract;
}

bool FunctionEntryContract::IsWellFormed(u32 allocation_size) const {
    if (guest_end < guest || !canonical.Present() ||
        !EntryIsWellFormed(canonical,
                           FunctionEntryKind::Canonical,
                           FunctionEntryFlagsRequirement::Canonical,
                           FunctionEntryContinuationRequirement::None,
                           allocation_size) ||
        !EntryIsWellFormed(direct_link,
                           FunctionEntryKind::DirectLink,
                           FunctionEntryFlagsRequirement::Canonical,
                           FunctionEntryContinuationRequirement::None,
                           allocation_size) ||
        !EntryIsWellFormed(pending_flags,
                           FunctionEntryKind::PendingFlags,
                           FunctionEntryFlagsRequirement::Pending,
                           FunctionEntryContinuationRequirement::None,
                           allocation_size) ||
        !EntryIsWellFormed(continuation,
                           FunctionEntryKind::Continuation,
                           FunctionEntryFlagsRequirement::Canonical,
                           FunctionEntryContinuationRequirement::PublishedFrame,
                           allocation_size) ||
        !EntryIsWellFormed(
                pending_flags_continuation,
                FunctionEntryKind::PendingFlagsContinuation,
                FunctionEntryFlagsRequirement::Pending,
                FunctionEntryContinuationRequirement::PublishedFrame,
                allocation_size)) {
        return false;
    }
    if ((pending_flags.Present() || pending_flags_continuation.Present()) &&
        !pending_flags_contract.CanPublishPendingEntry()) {
        return false;
    }
    if (pending_flags_continuation.Present() && !pending_flags.Present()) {
        return false;
    }
    if (!linkable &&
        (direct_link.Present() || pending_flags.Present() ||
         continuation.Present() || pending_flags_continuation.Present())) {
        return false;
    }
    if (origin == FunctionEntryOrigin::AcceptedSplit) {
        return provenance && provenance->target == guest &&
               provenance->IsAccepted();
    }
    if (origin == FunctionEntryOrigin::RejectedSplit) {
        return provenance && provenance->target == guest &&
               !provenance->IsAccepted() && !direct_link.Present() &&
               !pending_flags.Present() && !continuation.Present() &&
               !pending_flags_continuation.Present();
    }
    return !provenance;
}

void* FunctionEntryPublisher::HostPC(
        const FunctionEntryPointContract& entry) const {
    return entry.Present() ? allocation + entry.code_offset : nullptr;
}

PublishedFunctionEntry FunctionEntryPublisher::Publish(
        const FunctionEntryContract& contract) const {
    ASSERT(allocation && contract.IsWellFormed(allocation_size));
    auto* canonical_host_pc = HostPC(contract.Canonical());
    auto& address_space = module.GetAddressSpace();
    const auto generation = contract.Linkable()
            ? module.PublishLinkTarget(
                      contract.Guest(),
                      canonical_host_pc,
                      allocation,
                      HostPC(contract.DirectLink()),
                      HostPC(contract.PendingFlags()),
                      HostPC(contract.Continuation()),
                      HostPC(contract.PendingFlagsContinuation()),
                      contract.PendingFlagsContract())
            : 0;
    address_space.PushCodeCache(contract.Guest(), canonical_host_pc);
    if (auto* call_host_pc = HostPC(contract.Continuation())) {
        address_space.PushCallCodeCache(contract.Guest(), call_host_pc);
    }
    if (auto* call_pending_host_pc =
                HostPC(contract.PendingFlagsContinuation())) {
        address_space.PushPendingCallCodeCache(contract.Guest(),
                                               call_pending_host_pc);
    }
    return {
            .guest = contract.Guest(),
            .canonical_host_pc = canonical_host_pc,
            .generation = generation,
    };
}

}  // namespace swift::runtime::backend
