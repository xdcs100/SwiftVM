#pragma once

#include "runtime/ir/hir_builder.h"

namespace swift::runtime::ir {

class IntegerWidthEliminationPass {
public:
    static void Run(HIRBuilder* hir_builder);
    static void Run(HIRFunction* hir_function);
    static void Run(Block* block);
};

}  // namespace swift::runtime::ir
