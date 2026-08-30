#include "runtime/backend/arm64/marcos.h"

.def SwiftSse42StrEqualAnyIndex
    cmeq v7.16b, v0.16b, #0
    cmeq v3.16b, v1.16b, #0
    shrn v7.8b, v7.8h, #4
    shrn v3.8b, v3.8h, #4
    fmov x13, d7
    fmov x14, d3
    rbit x13, x13
    rbit x14, x14
    clz x13, x13
    clz x14, x14
    lsr w13, w13, #2
    lsr w14, w14, #2

    ldr q4, 1f
    dup v7.16b, w14
    cmhi v3.16b, v7.16b, v4.16b
    movi v6.16b, #0

.macro SVM_SSE42STR_EQUAL_ANY_STEP lane
    cmp w13, #\lane
    b.ls 2f
    dup v5.16b, v0.b[\lane]
    cmeq v5.16b, v1.16b, v5.16b
    orr v6.16b, v6.16b, v5.16b
.endm
    SVM_SSE42STR_EQUAL_ANY_STEP 0
    SVM_SSE42STR_EQUAL_ANY_STEP 1
    SVM_SSE42STR_EQUAL_ANY_STEP 2
    SVM_SSE42STR_EQUAL_ANY_STEP 3
    SVM_SSE42STR_EQUAL_ANY_STEP 4
    SVM_SSE42STR_EQUAL_ANY_STEP 5
    SVM_SSE42STR_EQUAL_ANY_STEP 6
    SVM_SSE42STR_EQUAL_ANY_STEP 7
    SVM_SSE42STR_EQUAL_ANY_STEP 8
    SVM_SSE42STR_EQUAL_ANY_STEP 9
    SVM_SSE42STR_EQUAL_ANY_STEP 10
    SVM_SSE42STR_EQUAL_ANY_STEP 11
    SVM_SSE42STR_EQUAL_ANY_STEP 12
    SVM_SSE42STR_EQUAL_ANY_STEP 13
    SVM_SSE42STR_EQUAL_ANY_STEP 14
    SVM_SSE42STR_EQUAL_ANY_STEP 15
.purgem SVM_SSE42STR_EQUAL_ANY_STEP
2:
    and v6.16b, v6.16b, v3.16b
    ushr v6.16b, v6.16b, #7
    usra v6.8h, v6.8h, #7
    xtn v7.8b, v6.8h
    usra v7.4h, v7.4h, #6
    xtn v6.8b, v7.8h
    usra v6.4h, v6.4h, #4
    xtn v7.8b, v6.8h
    umov w15, v7.h[0]

    rbit w11, w15
    clz w11, w11
    mov w16, #16
    cmp w15, #0
    csel w11, w16, w11, eq
    mov w16, w15
    bfi x16, x11, #16, #8
    cmp w13, #16
    cset w11, ne
    bfi x16, x11, #24, #1
    cmp w14, #16
    cset w11, ne
    bfi x16, x11, #25, #1
    cmp w15, #0
    cset w11, ne
    bfi x16, x11, #26, #1
    bfi x16, x15, #27, #1
    br x17

    .align 4
1:
    .byte 0, 1, 2, 3, 4, 5, 6, 7
    .byte 8, 9, 10, 11, 12, 13, 14, 15

.def SwiftSse42StrEqualEachNegIndex
    cmeq v2.16b, v0.16b, #0
    cmeq v3.16b, v1.16b, #0
    shrn v2.8b, v2.8h, #4
    shrn v3.8b, v3.8h, #4
    fmov x13, d2
    fmov x14, d3
    rbit x13, x13
    rbit x14, x14
    clz x13, x13
    clz x14, x14
    lsr w13, w13, #2
    lsr w14, w14, #2

    ldr q4, 1f
    dup v2.16b, w13
    dup v3.16b, w14
    cmhi v2.16b, v2.16b, v4.16b
    cmhi v3.16b, v3.16b, v4.16b
    cmeq v5.16b, v0.16b, v1.16b
    and v6.16b, v2.16b, v3.16b
    bic v6.16b, v6.16b, v5.16b
    eor v2.16b, v2.16b, v3.16b
    orr v6.16b, v6.16b, v2.16b

    ushr v6.16b, v6.16b, #7
    usra v6.8h, v6.8h, #7
    xtn v7.8b, v6.8h
    usra v7.4h, v7.4h, #6
    xtn v6.8b, v7.8h
    usra v6.4h, v6.4h, #4
    xtn v7.8b, v6.8h
    umov w15, v7.h[0]

    rbit w11, w15
    clz w11, w11
    mov w16, #16
    cmp w15, #0
    csel w11, w16, w11, eq
    mov w16, w15
    bfi x16, x11, #16, #8
    cmp w13, #16
    cset w11, ne
    bfi x16, x11, #24, #1
    cmp w14, #16
    cset w11, ne
    bfi x16, x11, #25, #1
    cmp w15, #0
    cset w11, ne
    bfi x16, x11, #26, #1
    bfi x16, x15, #27, #1
    br x17

    .align 4
1:
    .byte 0, 1, 2, 3, 4, 5, 6, 7
    .byte 8, 9, 10, 11, 12, 13, 14, 15
