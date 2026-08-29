//
// Created by 甘尧 on 2023/12/19.
//

#include "ir_assembler.h"

namespace swift::runtime::ir {

HIRBuilder::ElseThen Assembler::If(const terminal::If& if_) {
    end_decode = true;
    if (hir_builder) {
        return hir_builder->If(if_);
    } else {
        ir_block->SetTerminal(terminal::Terminal{if_});
        return {};
    }
}

HIRBlock* Assembler::LinkBlock(const terminal::LinkBlock& block) {
    end_decode = true;
    if (hir_builder) {
        return hir_builder->LinkBlock(block);
    } else {
        ir_block->SetTerminal(terminal::Terminal{block});
        return {};
    }
}

void Assembler::ExternalLinkBlock(const terminal::ExternalLinkBlock& link) {
    end_decode = true;
    if (hir_builder) {
        hir_builder->ExternalLinkBlock(link);
    } else {
        ir_block->SetTerminal(terminal::Terminal{link});
    }
}

void Assembler::RegisterExternalDirectLink(Location target) {
    if (hir_builder) {
        hir_builder->RegisterExternalDirectLink(target);
    }
}

void Assembler::ReturnToDispatcher() {
    end_decode = true;
    if (hir_builder) {
        hir_builder->ReturnToDispatcher();
    } else {
        ir_block->SetTerminal(terminal::ReturnToDispatch{});
    }
}

void Assembler::ReturnToHost() {
    end_decode = true;
    if (hir_builder) {
        hir_builder->ReturnToHost();
    } else {
        ir_block->SetTerminal(terminal::ReturnToHost{});
    }
}

void Assembler::Return() {
    end_decode = true;
    if (hir_builder) {
        hir_builder->Return();
    } else {
        ir_block->SetTerminal(terminal::PopRSBHint{});
    }
}

void Assembler::RegisterCallReturn(Location location) {
    if (hir_builder) {
        hir_builder->RegisterCallReturn(location);
    }
}

bool Assembler::EndCommit() const {
    return end_decode;
}

bool Assembler::HasFunctionBlock(Location location) const {
    return hir_builder && hir_builder->HasCurrentFunctionBlock(location);
}

}
