#include "runtime/backend/arm64/marcos.h"

.macro resident_string_call name, target
    .def \name
    sub sp, sp, #368
    stp x3, x4, [sp, #0]
    stp x5, x6, [sp, #16]
    stp x7, x8, [sp, #32]
    stp x9, x10, [sp, #48]
    stp x11, x12, [sp, #64]
    stp x13, x14, [sp, #80]
    stp x15, x30, [sp, #96]
    stp q16, q17, [sp, #112]
    stp q18, q19, [sp, #144]
    stp q20, q21, [sp, #176]
    stp q22, q23, [sp, #208]
    stp q24, q25, [sp, #240]
    stp q26, q27, [sp, #272]
    stp q28, q29, [sp, #304]
    stp q30, q31, [sp, #336]
    mrs x16, nzcv
    fmov d8, x16
    bl cdecl(\target)
    fmov x16, d8
    msr nzcv, x16
    ldp q16, q17, [sp, #112]
    ldp q18, q19, [sp, #144]
    ldp q20, q21, [sp, #176]
    ldp q22, q23, [sp, #208]
    ldp q24, q25, [sp, #240]
    ldp q26, q27, [sp, #272]
    ldp q28, q29, [sp, #304]
    ldp q30, q31, [sp, #336]
    ldp x3, x4, [sp, #0]
    ldp x5, x6, [sp, #16]
    ldp x7, x8, [sp, #32]
    ldp x9, x10, [sp, #48]
    ldp x11, x12, [sp, #64]
    ldp x13, x14, [sp, #80]
    ldp x15, x30, [sp, #96]
    add sp, sp, #368
    ret
.endm

resident_string_call SwiftRepMovsResident, SwiftRepMovs
resident_string_call SwiftRepStos1Resident, SwiftRepStos1
resident_string_call SwiftRepStos2Resident, SwiftRepStos2
resident_string_call SwiftRepStos4Resident, SwiftRepStos4
resident_string_call SwiftRepStos8Resident, SwiftRepStos8
resident_string_call SwiftRepCmpsZ1Resident, SwiftRepCmpsZ1
resident_string_call SwiftRepCmpsNZ1Resident, SwiftRepCmpsNZ1
resident_string_call SwiftRepCmpsZ2Resident, SwiftRepCmpsZ2
resident_string_call SwiftRepCmpsNZ2Resident, SwiftRepCmpsNZ2
resident_string_call SwiftRepCmpsZ4Resident, SwiftRepCmpsZ4
resident_string_call SwiftRepCmpsNZ4Resident, SwiftRepCmpsNZ4
resident_string_call SwiftRepCmpsZ8Resident, SwiftRepCmpsZ8
resident_string_call SwiftRepCmpsNZ8Resident, SwiftRepCmpsNZ8
resident_string_call SwiftRepScasZ1Resident, SwiftRepScasZ1
resident_string_call SwiftRepScasNZ1Resident, SwiftRepScasNZ1
resident_string_call SwiftRepScasZ2Resident, SwiftRepScasZ2
resident_string_call SwiftRepScasNZ2Resident, SwiftRepScasNZ2
resident_string_call SwiftRepScasZ4Resident, SwiftRepScasZ4
resident_string_call SwiftRepScasNZ4Resident, SwiftRepScasNZ4
resident_string_call SwiftRepScasZ8Resident, SwiftRepScasZ8
resident_string_call SwiftRepScasNZ8Resident, SwiftRepScasNZ8
