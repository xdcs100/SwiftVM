#include "runtime/backend/arm64/marcos.h"

.def SwiftSse42StrEqualEachNegIndex
    cmeq v2.16b, v0.16b, #0
    cmeq v3.16b, v1.16b, #0
    shrn v2.8b, v2.8h, #4
    shrn v3.8b, v3.8h, #4
    fmov x10, d2
    fmov x11, d3
    rbit x13, x10
    rbit x14, x11
    clz x13, x13
    clz x14, x14
    lsr w13, w13, #2
    lsr w14, w14, #2
    mov w15, #16
    cmp x10, #0
    csel w13, w15, w13, eq
    cmp x11, #0
    csel w14, w15, w14, eq

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

    rbit w17, w15
    clz w17, w17
    mov w10, #16
    cmp w15, #0
    csel w17, w10, w17, eq
    mov w16, w15
    bfi x16, x17, #16, #8
    cmp w13, #16
    cset w17, ne
    bfi x16, x17, #24, #1
    cmp w14, #16
    cset w17, ne
    bfi x16, x17, #25, #1
    cmp w15, #0
    cset w17, ne
    bfi x16, x17, #26, #1
    bfi x16, x15, #27, #1
    ret

    .align 4
1:
    .byte 0, 1, 2, 3, 4, 5, 6, 7
    .byte 8, 9, 10, 11, 12, 13, 14, 15

.def SwiftSse42StrEqualAnyIndex
    cmeq v3.16b, v0.16b, #0
    shrn v3.8b, v3.8h, #4
    fmov x10, d3
    rbit x10, x10
    clz x10, x10
    lsr x10, x10, #2
    cmeq v3.16b, v1.16b, #0
    shrn v3.8b, v3.8h, #4
    fmov x11, d3
    rbit x11, x11
    clz x11, x11
    lsr x11, x11, #2

    eor v2.16b, v2.16b, v2.16b
    orr v3.16b, v0.16b, v0.16b
    mov w13, w10
    cbz w13, 2f
1:
    dup v4.16b, v3.b[0]
    cmeq v4.16b, v4.16b, v1.16b
    ext v3.16b, v3.16b, v3.16b, #1
    orr v2.16b, v2.16b, v4.16b
    subs w13, w13, #1
    b.ne 1b
2:
    ldur q5, [x28, #-32]
    and v5.16b, v2.16b, v5.16b
    addp v5.16b, v5.16b, v5.16b
    addp v5.8b, v5.8b, v5.8b
    addp v5.8b, v5.8b, v5.8b
    umov w16, v5.h[0]

    mov w14, #1
    lsl w14, w14, w11
    sub w14, w14, #1
    and w16, w16, w14
    and w16, w16, #0xffff
    rbit w13, w16
    orr w13, w13, #0x8000
    clz w13, w13
    orr w16, w16, w13, lsl #16

    cmp w10, #16
    cset w13, lt
    bfi x16, x13, #24, #1
    cmp w11, #16
    cset w13, lt
    bfi x16, x13, #25, #1
    and w13, w16, #0xffff
    cmp w13, #0
    cset w13, ne
    bfi x16, x13, #26, #1
    bfi x16, x16, #27, #1
    ret
