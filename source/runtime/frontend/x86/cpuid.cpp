#include "cpuid.h"

#include "runtime/frontend/x86/xsave.h"

namespace swift::x86 {

namespace {

runtime::HostPairResult PackCpuid(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    return {u64(eax) | (u64(ebx) << 32), u64(ecx) | (u64(edx) << 32)};
}

bool HasFeature(u64 features, CpuidFeature feature) {
    return (features & feature) != 0;
}

}  // namespace

SVM_HELPER_PRESERVE_ALL runtime::HostPairResult QueryCpuid(u64 leaf_value,
                                                          u64 subleaf_value,
                                                          u64 features) {
    static constexpr u32 kSse2Edx = (1u << 0) | (1u << 4) | (1u << 8) |
                                    (1u << 15) | (1u << 24) | (1u << 25) |
                                    (1u << 26);
    static constexpr u32 kLeaf1Ecx = (1u << 13) | (1u << 22) | (1u << 30);
    static constexpr u32 kSse4Ecx =
            (1u << 0) | (1u << 9) | (1u << 19) | (1u << 23);
    static constexpr u32 kLeaf7Ebx = 1u << 18;
    static constexpr u32 kExtEdx =
            (1u << 11) | (1u << 20) | (1u << 27) | (1u << 29);

    const bool avx = HasFeature(features, CpuidAvx);
    const bool crypto = HasFeature(features, CpuidCrypto);
    const bool xsave = HasFeature(features, CpuidXsave);
    const u32 leaf1_ecx =
            kLeaf1Ecx | (HasFeature(features, CpuidSse4) ? kSse4Ecx : 0u) |
            (HasFeature(features, CpuidSse42) ? (1u << 20) : 0u) |
            (crypto ? ((1u << 1) | (1u << 25)) : 0u) |
            (xsave ? ((1u << 26) | (1u << 27)) : 0u) |
            (avx ? ((1u << 28) | (1u << 12)) : 0u);
    const u32 leaf1_edx =
            kSse2Edx | (HasFeature(features, CpuidAbiBaseline) ? (1u << 23) : 0u);
    const u32 leaf7_ebx =
            kLeaf7Ebx | (HasFeature(features, CpuidFsgsbase) ? (1u << 0) : 0u) |
            (avx ? (1u << 5) : 0u) |
            (HasFeature(features, CpuidBmi) ? ((1u << 3) | (1u << 8)) : 0u) |
            (HasFeature(features, CpuidAdx) ? (1u << 19) : 0u) |
            (HasFeature(features, CpuidSha) ? (1u << 29) : 0u);

    const u32 leaf = static_cast<u32>(leaf_value);
    const u32 subleaf = static_cast<u32>(subleaf_value);
    switch (leaf) {
        case 0:
            return PackCpuid(0x15, 0x756E6547, 0x6C65746E, 0x49656E69);
        case 1:
            return PackCpuid(0x000306C3, 0, leaf1_ecx, leaf1_edx);
        case 7:
            return PackCpuid(0, leaf7_ebx, 0, 0);
        case 0xD:
            if (!xsave) {
                return {};
            }
            if (subleaf == 0) {
                const u64 xcr0 = GuestXcr0();
                const u32 size = XsaveAreaSize();
                return PackCpuid(u32(xcr0), size, size, u32(xcr0 >> 32));
            }
            if (subleaf == 1) {
                return PackCpuid((1u << 0) | (1u << 1), XsaveAreaSize(), 0, 0);
            }
            if (subleaf == 2 && (GuestXcr0() & kXstateYmm)) {
                return PackCpuid(kXsaveYmmSize, kXsaveYmmOffset, 0, 0);
            }
            return {};
        case 0x15:
            return PackCpuid(1, 1, 1'000'000'000u, 0);
        case 0x80000000:
            return PackCpuid(0x80000004, 0, 0, 0);
        case 0x80000001:
            return PackCpuid(0, 0, 0, kExtEdx);
        default:
            return {};
    }
}

}  // namespace swift::x86
