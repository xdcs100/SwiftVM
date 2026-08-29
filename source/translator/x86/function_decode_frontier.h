#pragma once

#include <map>
#include <optional>

#include "runtime/ir/hir_builder.h"

namespace swift::translator::x86 {

class FunctionDecodeFrontier final {
public:
    using Provenance = runtime::ir::FunctionEntryProvenance;
    using Rejection = runtime::ir::FunctionEntryRejection;

    struct Split {
        runtime::ir::HIRBlock* owner{};
        runtime::LocationDescriptor target{};
        const Provenance* provenance{};
    };

    explicit FunctionDecodeFrontier(runtime::ir::HIRFunction* function)
            : function(function) {}

    [[nodiscard]] std::optional<Split> FindSplit(runtime::LocationDescriptor target);
    [[nodiscard]] bool IsAccepted(runtime::LocationDescriptor target) const;
    void Accept(runtime::LocationDescriptor target);
    void Reject(runtime::LocationDescriptor target, Rejection reason);
    [[nodiscard]] const Provenance* FindProvenance(runtime::LocationDescriptor target) const;
    [[nodiscard]] std::vector<Provenance> ExportProvenance() const;

    [[nodiscard]] static runtime::LocationDescriptor DecodedEnd(
            const runtime::ir::Block* block);

private:
    struct Record {
        Provenance provenance{};
        runtime::ir::HIRBlock* owner{};
    };

    runtime::ir::HIRFunction* function{};
    std::map<runtime::LocationDescriptor, Record> records{};
};

}  // namespace swift::translator::x86
