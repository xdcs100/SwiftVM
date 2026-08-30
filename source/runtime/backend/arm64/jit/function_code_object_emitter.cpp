#include "function_code_object_emitter.h"

#include "jit_context.h"
#include "translator.h"
#include "runtime/ir/hir_builder.h"

namespace swift::runtime::backend::arm64 {

FunctionCodeObjectEmitter::FunctionCodeObjectEmitter(
        std::shared_ptr<Module> module,
        std::span<const FunctionRegionEmission> regions)
        : module(std::move(module)), regions(regions.begin(), regions.end()) {
    ASSERT(this->module && !this->regions.empty());
    for (const auto& region : this->regions) {
        ASSERT(region.function && region.reg_alloc);
    }
}

FunctionCodeObjectEmitter::~FunctionCodeObjectEmitter() = default;

void FunctionCodeObjectEmitter::Emit(bool enable_direct_link) {
    translators.clear();
    contexts.clear();
    contexts.reserve(regions.size());
    translators.reserve(regions.size());

    for (const auto& region : regions) {
        if (contexts.empty()) {
            contexts.push_back(std::make_unique<JitContext>(
                    module, *region.reg_alloc, enable_direct_link));
        } else {
            contexts.push_back(std::make_unique<JitContext>(
                    module,
                    *region.reg_alloc,
                    contexts.front()->GetMasm(),
                    enable_direct_link));
        }
        translators.push_back(
                std::make_unique<JitTranslator>(*contexts.back()));
        translators.back()->Translate(region.function);
    }

    for (size_t i = 1; i < contexts.size(); ++i) {
        contexts.front()->AbsorbEmissionState(*contexts[i]);
    }
}

u32 FunctionCodeObjectEmitter::CurrentBufferSize() const {
    return Context().CurrentBufferSize();
}

bool FunctionCodeObjectEmitter::RequiresRegionTrampoline() const {
    return Context().RequiresRegionTrampoline();
}

JitContext& FunctionCodeObjectEmitter::Context() const {
    ASSERT(!contexts.empty());
    return *contexts.front();
}

JitTranslator& FunctionCodeObjectEmitter::Translator(size_t region) const {
    ASSERT(region < translators.size());
    return *translators[region];
}

u8* FunctionCodeObjectEmitter::Flush(const CodeBuffer& buffer) {
    return Context().Flush(buffer);
}

}  // namespace swift::runtime::backend::arm64
