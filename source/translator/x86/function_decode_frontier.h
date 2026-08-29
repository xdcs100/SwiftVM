#pragma once

#include <optional>
#include <unordered_set>

#include "runtime/ir/hir_builder.h"

namespace swift::translator::x86 {

class FunctionDecodeFrontier final {
public:
    struct Split {
        runtime::ir::HIRBlock* owner{};
        runtime::LocationDescriptor target{};
    };

    explicit FunctionDecodeFrontier(runtime::ir::HIRFunction* function)
            : function(function) {}

    [[nodiscard]] std::optional<Split> FindSplit(
            runtime::LocationDescriptor target) const;
    [[nodiscard]] bool IsAccepted(runtime::LocationDescriptor target) const;
    void Accept(runtime::LocationDescriptor target);
    void Reject(runtime::LocationDescriptor target);

    [[nodiscard]] static runtime::LocationDescriptor DecodedEnd(
            const runtime::ir::Block* block);

private:
    runtime::ir::HIRFunction* function{};
    std::unordered_set<runtime::LocationDescriptor> accepted{};
    std::unordered_set<runtime::LocationDescriptor> rejected{};
};

}  // namespace swift::translator::x86
