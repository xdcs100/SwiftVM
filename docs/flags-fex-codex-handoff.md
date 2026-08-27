# Codex handoff: Align SVM flags with FEX

Date: 2026-08-27
Repo: `/Users/swift/CLionProjects/SwiftVM` (macOS). Linux identity runs on Orb: `ubuntu@orb`, tree `/home/swift/svm-phasec/SwiftVM`, build `/home/swift/svm-phasec/build`.
Author on git: `swift_gan`. **Do not push** until asked. English commits, no task IDs, no AI trailer.

**Do not inspect failed run `e333e444`** (xAI capacity / connection). Goal notices that name it are stale.

## Git / mission

- Product code tip: **`aa4ed1b`** `perf: extend flags bypass to static forwards`
- Tracked tree is clean before this documentation update. Preserve the existing untracked build/images/placement tools.
- Pi mission: `9306cb64-ce70-4726-a5e0-76fce2d23556` (goal mode ON). Rollback remains `SVM_FLAGS_REGS=0` (`ParseNonZero`; unset → ON).
- `npm:pi-codex-goal` is installed user-wide; `/goal` tools need a **new** Pi session.

## What already landed (read these first)

Default **`SVM_FLAGS_REGS=1`** (`121620f`). Region edges default ON. Default region window is **64** blocks (`SVM_FUNC_LAZY=1` means that window; `2..127` override; `<=0` eager 1024).

| Commit | What |
|---|---|
| `4661866` | Feed an exact low U8/U16 store extract from an allocator-coalesced fixed-home publication directly to `STRB/STRH` |
| `a548ece` | Omit an adjacent narrow self-extension when the shared register is already proven zero above the width by a load/zero-extension chain |
| `53333fc` | Fold an exact adjacent U8/U16 low extract and `ZeroExtend32` into one `UXTB/UXTH` even when their allocated registers differ |
| `9d299aa` | Emit a sole adjacent U8/U16 memory-load extension directly into the consumer register even when linear scan assigned distinct registers |
| `c87a2a1` | Keep an exact narrow load's ordinary saved-flags zero-test alias on its pinned home instead of copying it to a temporary W register |
| `7194669` | Publish a signed U8/U16 memory load directly into its fixed GPR home and keep audited low-32 SignExtend/Mul consumers on that home |
| `d5e45c7` | Keep a post-publication full-width load alias on its pinned home when its sole later role is a memory address |
| `38cea6e` | Feed the known zero-extended W result of LDRB/LDRH directly to a dead narrow immediate branch compare |
| `b734438` | Follow one static direct call in the dead-successor flags proof when the callee overwrites every incoming flag before control flow, and track the inspected callee prefix for SMC |
| `ea82571` | Reuse one displacement-adjusted indexed effective address across the load and store halves of a plain integer memory RMW |
| `f8cc6ed` | Keep an exact narrow load's branch-only zero-test alias on the pinned publication home and remove the intervening W move |
| `e959074` | Compose low-extract input forwarding with dead narrow immediate branches so the specialized compare reads the original source |
| `162a4d8` | Emit a spilled U32 Add directly into its pinned publication home and keep proven post-publication low-32 ALU uses on that home |
| `b5936ac` | Admit a single adjacent carry inversion on a dead U8/U16 memory-operand `Sub` branch when the condition does not read carry, then discard the irrelevant normalization |
| `9fb39ef` | Retain an arithmetic result token only when parity is live; NZCV-only narrow producers no longer restore result bits solely for a dead PF token |
| `f42b7cc` | Fold `BitExtract(SignExtend(v32),0,32)` back to the original 32-bit SSA and remove both signed low-32 round trips through DCE |
| `a627d94` | Feed an adjacent single-use low U8/U16 extract directly to narrow flags alignment, where the existing left shift already discards high bits |
| `3f079f9` | Generalize the dead narrow immediate branch plan from exact ZF to the existing ZF/CF/ZF+CF dead-edge conditions using `UXTB/UXTH; CMP imm` |
| `5922091` | Lower a dead-edge U8/U16 `Sub` with an immediate and exact ZF-only branch into `SUB imm; TST width-mask`, suppressing the single-use immediate materialization |
| `e2bb2c7` | Omit the final narrow-result `LSR` after branch-only Add/Sub/Neg when the arithmetic value has no ordinary use; observed results keep the truncation |
| `4cc2c1e` | Publish an exact U8/U16/U32 memory load directly into its pinned GPR home and omit the redundant zero-extension and host-register publication instructions |
| `14e48c1` | Extend the fixed-home copy planner through exact U8/U16 `ZeroExtend32` chains and emit one `UXTB`/`UXTH` from the source home to the target home |
| `942a63d` | Generalize the pinned low-32 self-write proof into a cross-pin copy: an exact U32 fixed-home read, zero extension and full fixed-home publication become one `mov wTarget, wSource`, while later aliases remain eligible only as proven memory addresses |
| `121620f` | FLAGS_REGS default ON |
| `a278d8d` / `5b7857d` / `6009f8f` / `f2b490d` | Region If skip: both successors cover incoming NZCV; transparent `mov/ret`; `ClearFlags` covers C/V; **`BranchOnlyFlags` covers full PSTATE NZCV** (IR mask is Jcc live subset, not the ALU write) |
| `73f6719` | Copy PSTATE at dispatcher **only if `region_edges_active`**; HostExit always |
| `85e22ae` / `82fa7c7` | Region window 16→32→**64** |
| `15e5165` | `lazy = budget <= kMaxFuncBlocks` (128). **`SVM_FUNC_LAZY=128` with `<` was eager and re-decoded L2 → 31B host** |
| `0535615` / `7397960` | JA → `b.hi`, JBE → `b.ls` |
| `601185d` / `52b0b6a` | L2 Unpark from **x26**; counted-entry veneer |
| `3641e32` | Full-width `LoadMemory` publishes directly into a pinned guest GPR home when the existing local last-use, observer, conflict, and width proofs all hold |
| `df5a54e` | Fail closed before reading non-And/Or carry-test args; reject packed-flags consumers from region transparent/cover proofs |
| `a91697b` | Hand a pinned W-view bridge directly to a proven destructive U32 result published back to the same fixed home |
| `d0578b6` | Fold adjacent single-use `BitExtract(0,32) -> ZeroExtend32To64` into one non-destructive W move; fix temporary-container iterator UB in width proofs |
| `11abed9` | Replace local `BitExtract(ZeroExtend32To64(v32),0,32)` round trips with the original U32 SSA and let DCE remove both redundant nodes |
| `ff42917` | Fold `BitExtract(v32,0,32)` into the original 32-bit SSA only for proven W-width consumers; reject narrow and opaque ABI uses |
| `7110d20` / `7045d3b` / `431be30` | Coalesce complete legacy scalar FPR results, compact full-NZCV publication, and publish VecZip results in place |
| `fa1768a` | Reuse the retained dynamic return target in both RSB pop formats instead of reloading `State::current_loc` |
| `729b826` | Route same-module static `SetLocation + ReturnToDispatch` exits through tracked direct-link sites, with the prior L2/dispatcher fallbacks retained |
| `24f9d49` | Skip RSB pushes in indirect-L1 modules and route retained return targets through the signal-safe inline L1; L1-off modules keep the exact RSB path |
| `2829a8d` | Back each active RSB with a 4 MiB guarded mapping and recover lower/upper guard faults by resetting the live x25 pointer in the interrupted context |
| `b3998d5` | Use the next region block for cycle-polled conditional layout without falling through into per-block cold stubs |
| `3ec9582` | Pair the production indirect-L1 request and cache-base loads; confirm observed signals through the shared acquire-checking trampoline |
| `e74e734` | Keep a complete V128 producer in its resident home through safe post-publication SSA uses; reject later home writes and multi-home remaps |
| `030f52d` | Replace an adjacent low-load/high-zero resident publication with one fault-exact D-register load after a full observer and alias proof |
| `97009a3` | Publish legacy scalar sqrt results through a dead resident merge home; independently reprove the fixed read, last use, and publication window |
| `f99eabf` | Reuse a dead fixed left home for legacy scalar FP binaries while preserving the high lane through a reserved temporary |
| `b692fca` | Make direct absolute `GetOperand` materialization the default; retain `=0` as the code-shape rollback |
| `c15a712` | Combine compact FCMP parity publication and AF clearing into one proof-backed bitfield insert |
| `2f2fb88` | Defer trailing constant `SetLocation` publication to dispatcher misses while preserving mid-block and dynamic observers |
| `523d679` | Write consumer-proven compact FCMP ordering directly into the PF/AF carrier and remove the separate publish insert |
| `e1257d6` | Stop emitting host instructions for semantic IR Nops while preserving decode/translate metadata effects |
| `b1e501e` | Let single-use integer zero values publish directly into fixed FPR lanes from `wzr/xzr` |
| `215a059` | Elide a shared integer zero when every use is a compatible uniform, memory or fixed-FPR store |
| `21000f1` | Extract EQ/NE, CS/CC, MI/PL and VS/VC directly from the saved flags register without restoring host NZCV |
| `0fc245c` | Encode identity-mode `[base + imm]` accesses with AArch64 scaled load/store offsets when possible |
| `723ace5` | Materialize single-bit N/Z/C/V `TestFlags` values with direct bit extraction while preserving live PSTATE |
| `8ca4b0b` | Materialize zero/nonzero without clobbering pending PSTATE on straight-line IR paths |
| `aa7b83d` | Read a live PSTATE single-bit flag directly with non-clobbering `CSET` |
| `a505485` | Lower encodable negative GetOperand displacements directly with `SUB` |
| `d9eb980` | Align direct-hash L1 storage and form each 16-byte entry address with one `BFI` |
| `e2f9527` | Preserve identity-mode `[base + index]` in memory IR and use the AArch64 register-offset encoding directly |
| `4821182` | Fold fault-exact identity-mode stack pushes into one AArch64 pre-index store; retain biased-memory and base/data-overlap paths |
| `1a69a59` | Fold an identity-mode fixed-base load plus the following dead-flags +1 base update into one fault-exact pre-index load |
| `6b10c73` | Share one materialized 4 KiB guest page base across encodable absolute memory addresses and make the proven path default |
| `e10fec4` | Let one audited consumer reuse a pinned W view for every operand occurrence and feed callee-saved pinned values directly into sign extension |
| `7620306` | Store a sole narrow pinned GPR read directly from its fixed W home while preserving snapshot and address-use semantics |
| `e2fe71c` | Normalize carry to a Direct cross-block ABI on FlagM hosts and remove the polarity-byte publication path |
| `a9f5ddf` | Delete `InvertCarry` and its covered carry publication when backward liveness proves a later in-block C write wins before every read |
| `2d86a6e` | Screen candidates with bounded short shape runs and retained formal weights before promoting them to long benchmarks |
| `5a47163` | Collapse narrow logical flag identities into one width-correct NZ producer |
| `477947d` / `e389f9d` | Merge contiguous partial NZCV with bitfield instructions and share the optimal merge across region materialization |
| `5b94050` | Restore NZCV directly from the packed flags register; `MSR NZCV` ignores every non-NZCV bit |
| `183bf78` | Skip published-region NZCV restores when the existing target proof covers all incoming flags before any observer or fault |
| `46197f6` | Clear the contiguous AF/unused/C/V span with one bitfield clear |
| `4b0eb1d` | Keep compact COMIS relations in host NZCV across audited MOVSD instructions and consume them directly at the following Jcc |
| `d934979` | Extend the same relation lifetime across the vector move family lowered entirely by audited NZCV-preserving operations |

Hot files:

- `translator_region.cpp` — `SuccessorCoversIncomingNzcv`, `BlockIsFlagsTransparent`, `EmitRegionIf`
- `translator_flags.cpp` — `MergeNZCV`, `force_ret_pstate`, direct simple `CondSet` extraction
- `translator_flags_abi.cpp` — published region entry restore and target-kill reuse
- `trampolines.cpp` — runtime flags park/unpark ABI
- `translator_terminal.cpp` — generic If / LinkBlock / RSB
- `translator/x86/translator.cpp` — `RegionFuncBudget`, `kMaxFuncBlocks=128`, lazy skip of published L2
- `register_alloc_coalesce_gpr.cpp` — pinned guest GPR read/write coalescing and full-width load publication
- `register_alloc_coalesce_copy.cpp` — exact adjacent low32 copy-chain ownership
- `integer_width_elimination_pass.cpp` — local zero-extend/low32-extract round-trip elimination
- `register_alloc_coalesce_fpr.cpp` — resident-home interval proof and publication ownership
- `translator_fpr_publication.cpp` — scalar load/zero-high pairing and observer proof
- `translator_mem.cpp` — independent host-FPR publication proof and final bridge emission
- `translator_operand.cpp` — shared zero-store register eligibility and scaled/unscaled memory operand formation
- `translator_alu_vec_fp.cpp` — scalar-unary merge emission and redundant self-copy suppression
- `svm_config.h` — `flags_regs` default true; `region_edges` bounded64
- `tools/svm-linux-cq/quick_shape.py` / `weighted_diff.py` — bounded short shape capture and formal-weight comparison

## Fast benchmark screening

Do not run formal smallpt/c-ray/CoreMark/STREAM for every candidate. Use
`docs/codegen-benchmark-fast-path.md` and commit `2d86a6e` first: capture baseline and candidate
shapes with the same short input, apply retained formal entries through the strict PC/version join,
require at least 99.9% host-weight coverage plus all top-20 PCs, and compare the short oracle
byte-for-byte.

The calibrated smallpt screen is `smallpt_wh_x64 4 8 6`: 7.2–8.4 seconds on Orb, 99.983053%
formal host-weight coverage and top-20 20/20. Arguments below 4 are invalid because this guest
divides spp by four and executes zero samples. The screen runs without `SVM_DENSITY_PROF`,
`SVM_PROF=2` or `SVM_EXEC_PROF`; only the existing hot-shape collector is enabled.

## Honest density (coremark `0x0 0x0 0x66 20000 7 1 2000`)

This is a promoted-stage/formal gate, not an iteration gate. Measure on Orb **without**
`SVM_EXEC_PROF` (it inflates host):

```
timeout 45 env -u SVM_JIT_CACHE $SVM $FT          # checksum 9f52b7d59285dbe5, rc=101
timeout 90 env -u SVM_JIT_CACHE -u SVM_EXEC_PROF \
  SVM_DENSITY_PROF=1 SVM_RA_HOT_COALESCE_ALL=1 SVM_RA_HOT_COALESCE=/tmp/x.hot \
  $SVM $BIN/coremark_x64 0x0 0x0 0x66 20000 7 1 2000
# CRC 0x382f
```

`$SVM=/home/swift/svm-phasec/build/source/translator/linux/svm_translator_linux`  
`$FT=.../func_tests_x86_64`  
`$BIN=/mnt/mac/Users/swift/CLionProjects/SwiftVM-bench/bin`

Synchronize all tracked sources Mac → Orb before cmake. A touched-files-only copy left Orb's
`translator_terminal.cpp` stale during this continuation and produced a false c-ray region diagnosis.

| Config | host_dynamic | notes |
|---|---:|---|
| FLAGS=0, window 16 (old baseline) | **7.383B** | compare-to |
| FLAGS=1, window 64 (before `3641e32`) | **6.348B** | **−14%** vs old FLAGS=0/16 |
| FLAGS=1, window 64 (after `3641e32`) | **6.300B** | `6,347,614,988 → 6,299,957,711` (**−0.751%**) from direct load publication |
| FLAGS=1, window 64 (after `a91697b`) | **6.251B** | `6,299,957,565 → 6,250,517,460` (**−0.785%**) from pinned W-view handoff |
| FLAGS=1, window 64 (after `d0578b6`) | **6.210B** | `6,250,517,196 → 6,210,114,929` (**−0.646%**) from adjacent low32 copies |
| FLAGS=1, window 64 (after `11abed9`) | **6.087B** | `6,210,114,929 → 6,087,169,543` (**−1.980%**) from local width round trips |
| FLAGS=1, window 64 (**current default**) | **6.034B** | `6,087,169,543 → 6,034,267,121` (**−0.869%**) from safe same-width extracts |
| FLAGS=0, window 64 (**current rollback**) | **6.712B** | CRC `0x382f`; FLAGS remains **−6.1%** at the same window |
| FLAGS=1, RE=0 | 26.009B | vs FLAGS=0 RE=0 **26.774B (−2.9%)** |
| FLAGS=1, window 32 | 6.882B | |
| FLAGS=1, window 128 (after lazy fix) | 6.348B | same as 64; coremark hot funcs fit 64 |

After If-skip, `SVM_FLAGS_REGS_AUDIT=1` on window-32: **PStateClobber/RegionInternal ≈ 2.3k entries**. Remaining ~70M “flags audit” is **L2 `ldr` cache-reload** (Dispatcher/RSBHit), not MergeNZCV.

Move bucket is now **32.136%** of host (`move_dynamic = 1,939,191,376`). `11abed9` removes
122.945M common-PC host instructions from CoreMark after `d0578b6`; `ff42917` removes another
52.903M. Across both stages spill remains zero. The same-width stage removes 512 / 2,540,061 /
402,107 common-PC host instructions from STREAM/smallpt/c-ray. Remaining move volume is not
automatically removable W-alpha space.

Current RE=0 same-PC refresh, reusing the unchanged FEX `f2e35f3` blockstats and old guest/entry
denominators at >99.99997% coverage: CoreMark **3.613/1.807 = 2.000×**, STREAM
**2.161/3.336 = 0.648×**, smallpt **3.377/1.549 = 2.180×**. Current c-ray covered only 74.47%
of the old entry table, so no whole-workload ratio is claimed for it.

Validation for `3641e32`:

- Mac and Orb targeted RA/fault tests pass: 353 and 21 assertions.
- FLAGS `0/1` × function/block/interpreter func_tests all return 101 with checksum `9f52b7d59285dbe5` and identical output SHA-256.
- Function fingerprint A/B against the exact pre-change binary passes for 1661 units over 11 guests. The checked-in Linux golden predates the region-window changes and is already stale; do not update it as part of this RA change.
- Full `swift_test` has the same existing 48 assertion failures before and after this change, in the same file sequence; the change adds only passing assertions.

Validation for `df5a54e` / `a91697b`:

- New pinned W-view test: 6 assertions; GPR coalescing 353, width-chain 23 and resident-fault 21 all pass.
- FLAGS `0/1` × function/block/interpreter func_tests: rc=101 and checksum `9f52b7d59285dbe5` in all six cells.
- helper-fault 38/0; clone futex/lock under FLAGS `0/1` all rc=0.
- Function fingerprint A/B against the exact pre-handoff binary: 1664 units over 11 guests, PASS.
- Orb full suite with fixed RNG has the same 36 existing failed cases before/after; focused new and RA/fault tests pass.
- Full metrics and the current FEX gap assessment are in `docs/codegen-gap-refresh-2026-08-23.md`.

Validation for `d0578b6`:

- New low32 copy test: 6 assertions; GPR coalescing 353, width-chain 23, resident-fault 21 and
  W/X high-half 17 all pass.
- FLAGS `0/1` × function/block/interpreter func_tests: rc=101 and checksum
  `9f52b7d59285dbe5` in all six cells.
- helper-fault 38/0; clone futex/lock under FLAGS `0/1` all rc=0.
- Function fingerprint against exact `f8426db`: 1664 units over 11 guests, PASS; smallpt output
  SHA-256 is identical in both arms.
- Fixed `SWIFT_FUZZ_SEED=123456`: baseline 40 failed cases / 53 assertions, candidate 39 / 52.
  Two width-chain assertions turn green; remaining differential failures are the same VIXL tail
  disassembly self-consistency class, not a semantic regression.

Validation for `11abed9`:

- New width pass: 2 cases / 8 assertions. Combined low32/int-width/width-chain/GPR/fault/high-half
  focus: 8 cases / 474 assertions, PASS on Mac and Orb.
- CoreMark: `6,210,114,929 → 6,087,169,784`; common-PC host `-122,945,220`,
  move `-122,945,213`, spill 0→0, CRC `0x382f`.
- STREAM/smallpt/c-ray common-PC host: `-5,854 / -868,884 / -1,283,611`; no workload grows
  in total. smallpt PPM SHA-256 and c-ray canonical PNG IDAT MD5 match across arms.
- FLAGS `0/1` × function/block/interpreter func_tests: rc=101 and checksum
  `9f52b7d59285dbe5` in all six cells; helper-fault 38/0; clone futex/lock four cells rc=0.
- Fingerprint self-consistency: 1664 units / 11 guests. Against the exact old binary, the unique
  guest-PC set is unchanged; 400 units reduce IR, 0 increase, total IR `-2,025`.

Validation for `ff42917`:

- CoreMark: common-PC host/move `-52,902,689`, spill 0→0, CRC `0x382f`; current raw host is
  `6,034,267,121` and move is `1,939,191,376`.
- STREAM/smallpt/c-ray common-PC host: `-512 / -2,540,061 / -402,107`; smallpt PPM SHA-256
  and c-ray IDAT MD5 match their exact baselines.
- Width/fault focus including the U16 CallLambda regression: 9 cases / 482 assertions, PASS.
  FLAGS six-grid, helper-fault 38/0 and clone four-grid remain green.
- Fingerprint self-consistency: 1664 units / 11 guests; every per-guest unit and decoded-block
  total is unchanged, aggregate IR is `-1,046`.
- Fixed seed full suite returns to the pre-stage 179 passed / 35 existing failed cases and
  1,047,523 passed / 45 failed assertions. The unsafe generic prototype had added one U16 helper
  failure; the final consumer whitelist removes it.

Validation for `7110d20` / `7045d3b` / `431be30` / `fa1768a` / `729b826` / `24f9d49` / `b3998d5` / `3ec9582` / `e74e734` / `030f52d` / `97009a3` / `f99eabf` / `b692fca` / `c15a712` / `2f2fb88` / `523d679` / `e1257d6` / `b1e501e` / `215a059` / `21000f1` / `0fc245c` / `723ace5` / `8ca4b0b` / `aa7b83d` / `a505485` / `d9eb980` / `e2f9527` / `4821182` / `6b10c73`:

- Current formal smallpt is `smallpt_wh_x64 8 128 96`; do not substitute the fixed 1024×768
  `smallpt_x64` when updating the formal FEX ratio.
- Formal smallpt default-region host:
  `1,321,651,162 → 1,303,939,990 → 1,297,980,655 → 1,296,969,640 → 1,246,900,800 → 1,201,575,549 → 1,201,372,215 → 1,199,466,420 → 1,187,471,711 → 1,168,614,398 → 1,165,502,656 → 1,163,020,553 → 1,141,267,073 → 1,126,372,521 → 1,096,331,217 → 1,081,436,665 → 1,072,445,284 → 1,068,863,253 → 1,064,572,872 → 1,060,138,659 → 1,060,040,252 → 1,052,965,418 → 1,047,125,252 → 1,043,588,497 → 1,042,807,357 → 1,040,901,562 → 1,040,846,721 → 1,001,905,579 → 984,381,707`;
  cumulative `-337,269,455` (`-25.5188%`), spill 0 throughout. The arrows are full-NZCV
  compaction, VecZip resident publication, retained RSB target reuse, static-exit direct-link,
  default return-L1, cycle-polled successor layout, paired indirect-L1 state loading, then live
  resident-FPR publication, scalar-load FPR fusion, scalar-sqrt resident publication, then
  legacy scalar-binary resident publication, direct absolute-address materialization, compact
  FCMP non-NZCV publication, trailing static-location cold publication, compact FCMP carrier
  publication, semantic Nop elision, zero-register FPR lane publication, shared zero-store
  materialization elision, direct simple-condition extraction from saved flags, then scaled
  immediate load/store addressing, direct single-bit flag tests, PSTATE-preserving zero tests,
  one-instruction live-PSTATE flag materialization, direct negative-displacement lowering, then
  aligned L1 entry formation with `BFI`, register-offset memory EA preservation, then fault-exact
  stack-push pre-index stores, then same-page constant-address base reuse.
  `7110d20` is neutral here but saves
  452,646,984 on fixed 1024×768
  smallpt.
- RSB reuse shrinks 328 formal smallpt PCs with no growth. EXEC_PROF records 8,080,989 hits / 248
  misses, so the earlier 0.076% static heuristic was not an execution ceiling. c-ray equal-entry
  delta is `-414,747` with 967 PCs smaller and none larger; CoreMark equal-entry delta is
  `-25,442,605`. A call-dense workload is exactly `-64,000,000` under both RSB frame formats.
- Earlier c-ray equal-entry common-PC deltas: full-NZCV `-999,520`, VecZip `-124,308`; no common PC
  grows. CoreMark after full-NZCV is about `5,973,080,081` host (`-61.19M`, CRC final `0x382f`);
  VecZip is neutral there.
- Static-exit direct-link removes five instructions per eligible site: formal smallpt
  `-50,068,840` with 998 PCs smaller / 0 larger; inline-L2 `link_hit` falls
  `12,191,734 → 31,816` while every exit/RSB/dispatcher/region counter remains equal. CoreMark
  equal-entry is `-210,324,020`, formal c-ray is `-574,050,830`, STREAM total is `-18,740`, and
  call-dense is `-240,000,010`; every common-PC set is shrink-only.
- Default return-L1 removes RSB production/consumption when `indirect_l1` is enabled: formal
  smallpt `-45,325,251` with 881 PCs smaller / 0 larger and 99.9953% L1 hits. CoreMark equal-entry
  is `-169,232,701`, formal c-ray `-566,348,759`, STREAM `-5,480`, and call-dense
  `-352,000,012`. `SVM_INDIRECT_L1=0` call-dense is byte-identical to the old RSB path.
- Cycle-polled successor layout removes one redundant conditional-arm branch without crossing the
  per-block cold stub: formal smallpt `-203,334` with 127 PCs smaller / 0 larger. Static local branch
  bytes fall `5,732 → 5,140` while 518 cycle edges and 4,144 poll bytes remain exact. CoreMark
  equal-entry is `-51,081,278`, c-ray `-328,719`, and STREAM `-1,412`; every common-PC set is
  shrink-only.
- Paired indirect-L1 state loading shortens the production fast path from nine instructions to
  eight: formal smallpt `-1,905,795` with 376 PCs smaller / 0 larger. CoreMark equal-entry is
  `-31,825,037`, formal c-ray `-91,110,798`, STREAM `-879`, and call-dense `-64,000,000`;
  every common-PC set is shrink-only. The scale-10 call-dense wall-time median is neutral
  (`1.045108s → 1.044901s`), unlike the rejected exclusive-pair prototype.
- Live resident-FPR publication removes a full-home copy even when the producer has safe SSA uses
  after the publication: formal smallpt `-11,994,709` with 79 PCs smaller / 0 larger. Formal c-ray
  equal-entry is `-136,043,687` with 174 PCs smaller / 0 larger; STREAM is `-199` with 26 PCs
  smaller / 0 larger, while CoreMark is effectively neutral. PPM, c-ray IDAT and STREAM validation
  remain exact.
- Scalar-load FPR fusion collapses `LDR X + MOV zero + 2×INS` into one fault-site `LDR Dtarget`
  for 6,285,771 formal smallpt executions. Formal smallpt is `-18,857,313` with 64 PCs smaller /
  0 larger; formal c-ray equal-entry is `-115,072,803` with 27 PCs smaller / 0 larger; STREAM is
  `-174` with 7 PCs smaller / 0 larger and CoreMark is equal-entry neutral. Every changed static
  PC shrinks by a multiple of three; all three oracles remain exact.
- Scalar-sqrt resident publication maps a legacy scalar `VecFUnary(kind=sqrt)` result to the
  proven-dead resident merge home. Formal smallpt is `-3,111,742` (`-0.2663%`) with 20 PCs each
  exactly two instructions smaller and 0 larger. Formal c-ray equal-entry is `-1,036,470` with
  one PC two instructions smaller and 0 larger; STREAM and CoreMark are equal-entry neutral.
  PPM, c-ray IDAT, STREAM validation and CoreMark CRC remain exact.
- Legacy scalar-binary resident publication reuses a proven-dead fixed left home. Formal smallpt
  is `-2,482,103` (`-0.2130%`) with 31 PCs smaller and 0 larger. Formal c-ray equal-entry is
  `-107,640,218` with 83 PCs smaller and 0 larger; STREAM is `-1` and CoreMark is equal-entry
  neutral. The 64-bit legacy lowering computes in a reserved temporary before inserting lane0,
  so the resident high lane is never overwritten. All four oracles remain exact.
- Direct absolute-address materialization removes the scratch-to-result transport for every
  absolute `GetOperand`. Formal smallpt is `-21,753,480` (`-1.8704%`) with 794 PCs smaller and
  0 larger. Formal c-ray equal-entry is `-243,851,552` with 1,924 PCs smaller and 0 larger;
  STREAM/CoreMark are `-1,137` / `-673`, also shrink-only. All oracles and spill counts remain
  exact. The feature is now default ON; `SVM_ABS_CONST_MAT=0` selects the prior code shape.
- Compact FCMP publication combines the raw parity-byte write and AF clear into one 27-bit BFI;
  AXFLAG and lazy host NZCV remain unchanged. Formal smallpt is `-14,894,552` (`-1.3051%`)
  with 144 PCs smaller and 0 larger. Formal c-ray equal-entry is `-88,615,708` with 118 PCs
  smaller and 0 larger; STREAM/CoreMark are `-23` / `-4`, also shrink-only. All oracles and
  spill counts remain exact.
- Trailing constant `SetLocation` publication is deferred only when it is the final enabled IR
  instruction. Formal smallpt is `-30,041,304` (`-2.6671%`): 998 PCs each shrink by exactly three
  instructions and none grow. Formal c-ray equal-entry is `-344,393,514` with 4,151 PCs smaller
  and none larger; STREAM/CoreMark equal-entry are `-9,702` / `-126,194,385`. CoreMark has one
  entries=0 layout-only version grow by nine instructions and no dynamic growth. All four oracles
  and spill counts remain exact.
- Compact FCMP carrier publication writes ordered/raw-parity directly with the existing `CSET VC`
  when every consumer is a compact publish plus at most one proven-safe `FCmpCondSet`. Formal
  smallpt is `-14,894,552` (`-1.3586%`) with 144 PCs smaller and none larger. Formal c-ray
  equal-entry is `-88,615,708` with 118 PCs smaller and none larger; STREAM is `-23` across 11
  shrinking PCs. CoreMark's four FCMP PCs are consistently `-4`; repeated A/B isolates an unrelated
  one-entry cold-layout toggle at `0x4668fd`. All four oracles and spill counts remain exact.
- Semantic Nop elision removes the backend ARM `NOP` while retaining every IR decode/translate
  metadata effect. Formal smallpt is exactly `-8,991,381` (`-0.8314%`) with 245 PCs smaller and
  none larger. Formal c-ray equal-entry is `-141,118,113` with 837 PCs smaller and none larger;
  STREAM/CoreMark equal-entry are `-1,390` / `-57,725,341`, also shrink-only. Placement/alignment
  Nops use a separate code-pool path and remain unchanged. All four oracles and spill counts are exact.
- Zero-register FPR lane publication extends the existing default-on `zero_store_zr` proof to a
  single-use, unspilled integer `LoadImm(0)` consumed by `SetHostFPR`. The fixed lane reads directly
  from `wzr/xzr`; multi-use, pseudo-observed, spilled and nonzero values retain materialization.
  Formal smallpt is `-3,582,031` (`-0.3340%`) with 41 PCs smaller and none larger. Formal c-ray
  equal-entry is `-114,264,417` with 144 PCs smaller and none larger; STREAM is `-46` across 5
  shrinking PCs. CoreMark is effectively neutral and keeps CRC `0x382f`. All spill/oracle gates pass.
- Shared zero-store elision accepts multiple uses only when the complete global use count is closed
  by same-block StoreUniform, StoreMemory or SetHostFPR value operands. Formal smallpt is
  `-4,290,381` (`-0.4014%`) with 74 PCs smaller and none larger. Formal c-ray equal-entry is
  `-114,750,206` with 142 PCs smaller and none larger; STREAM is `-5` and CoreMark is neutral.
  PPM, IDAT, STREAM, CRC and spill gates remain exact.
- Direct simple `CondSet` extraction replaces `AND + MSR NZCV + CSET` with one `UBFX` for
  EQ/CS/MI/VS and `UBFX + EOR` for their inverse conditions when x26 is authoritative. Live host
  PSTATE and compound conditions retain the existing path. Formal smallpt is `-4,434,213`
  (`-0.4165%`) with 101 PCs smaller and none larger. Formal c-ray equal-entry is `-21,411,778`
  with 293 PCs smaller and none larger; STREAM/CoreMark equal-entry are `-2,168` / `-2,210`,
  also shrink-only. PPM, IDAT, STREAM, CRC and spill gates remain exact.
- Scaled immediate addressing lets identity-mode `[base + imm]` use the AArch64 unsigned scaled
  load/store encoding instead of `MOV imm + [base, register]`. Pair, shift, writeback and bounded
  bias paths retain their prior predicates. Formal smallpt is `-98,407` (`-0.0093%`) with 18 PCs
  smaller and none larger. Formal c-ray/STREAM/CoreMark equal-entry are `-93` / `-8` / `-8`, all
  shrink-only; every output and spill gate remains exact.
- Direct single-bit `TestFlags` materialization replaces `TST + CSET` on saved flags with one
  `UBFX`; when PSTATE is authoritative it uses `MRS + UBFX`, avoiding both the destructive test and
  any NZCV restore. Formal smallpt is `-7,074,834` (`-0.6674%`) with 81 PCs smaller and none larger.
  Formal c-ray equal-entry is `-32,621,346` with 221 executed PCs smaller; the only larger common
  PC has zero entries. STREAM/CoreMark equal-entry are `-1,100` / `-7,481,108`. PPM, IDAT,
  STREAM, CRC and spill gates remain exact.
- PSTATE-preserving zero tests use `CLZ + LSR` for zero and append `EOR` for nonzero instead of
  publishing guest flags and then issuing `CMP + CSET`. The path is rejected when a later local
  Goto/NotGoto/BindLabel would merge control state; the unrestricted prototype failed the existing
  zero-rotate repro. Formal smallpt is `-5,840,166` (`-0.5546%`). Formal c-ray equal-entry is
  `-21,374,140`; layout growth contributes only 278 dynamic instructions. STREAM is `-472`, while
  CoreMark's `+79,936` is a 0.0015% layout-level change with exact CRC. All output and spill gates
  remain exact.
- Live-PSTATE single-bit `TestFlags` now uses one non-clobbering `CSET` instead of `MRS + UBFX`;
  x26-backed tests keep their one-instruction `UBFX`. Formal smallpt is `-3,536,755` (`-0.3378%`)
  with 74 PCs smaller and none larger. Formal c-ray equal-entry is `-16,348,046` with 148 PCs
  smaller; the sole larger common PC has zero entries. STREAM/CoreMark equal-entry are `-30` /
  `-3,740,015`, and every output/spill gate remains exact.
- Encodable negative `GetOperand` displacements now use `SUB` directly instead of materializing
  the signed immediate and issuing `ADD`. Formal smallpt is `-781,140` (`-0.0749%`); equal-entry
  delta is `-781,195` with 99 PCs smaller and none larger. Formal c-ray equal-entry is
  `-10,527,227` with 288 PCs smaller and none larger; STREAM/CoreMark are `-214` / `-59`.
  PPM, IDAT, STREAM, CRC and spill gates remain exact.
- Direct-hash L1 tables now align their storage to the complete table span. The inline return and
  dispatcher paths can therefore replace `AND + ADD` with one `BFI` while retaining the same
  entry count and probing contract. Formal smallpt is `-1,905,795` (`-0.1828%`) with 377 PCs
  smaller and none larger; unit/version/entry counts are identical. Formal c-ray raw/equal-entry
  deltas are `-90,922,904` / `-91,098,755` with 1,129 equal-entry PCs smaller and none larger.
  STREAM raw/equal-entry are `-941` / `-875`; CoreMark raw/equal-entry are `-31,825,004` /
  `-31,825,027`. PPM, c-ray IDAT, STREAM validation, CoreMark CRC and spill gates remain exact.
- Identity-mode `[base + index]` memory operands now stay composite through the frontend instead
  of materializing an intermediate `GetOperand`; the existing ARM64 memory emitter consumes the
  register-offset form directly. Formal smallpt is `-54,841` (`-0.0053%`) with 45 PCs smaller and
  none larger. Formal c-ray raw/equal-entry are `-5,506,465` / `-5,469,864` with 200 equal-entry
  PCs smaller and none larger. STREAM equal-entry is `-111`; CoreMark equal-entry is `-640,298`
  with 66 PCs smaller and none larger. PPM, IDAT, STREAM, CRC and spill gates remain exact.
- A strict `Sub(RSP,size) -> StoreMemory -> SetHostGPR(RSP)` backend proof now emits one
  pre-index store in identity mode. The store completes before AArch64 base writeback, so a
  synchronous fault retains the pre-instruction RSP; biased memory and base/data overlap
  (`push rsp`) reject the fold. Formal smallpt is `-38,941,142` (`-3.7413%`) with 228 PCs
  smaller and none larger; unit/version/entry counts are identical. Formal c-ray raw/equal-entry
  are `-401,378,477` / `-400,778,948` with 1,108 equal-entry PCs smaller and none larger.
  STREAM raw/equal-entry are `-5,508` / `-4,228`; CoreMark raw/equal-entry are
  `-100,077,187` / `-100,077,202`. PPM, 64-spp c-ray IDAT, STREAM validation, CoreMark CRC and
  spill gates remain exact.
- Constant-address caching now groups ordinary memory operands by their 4 KiB guest page instead
  of requiring the exact same address. One allocated page base serves every scaled/unscaled
  encodable offset in the verified idle-register window; biased memory rematerializes each exact
  guest address. The corrected cache is default ON with `SVM_CONST_ADDR_CACHE=0` rollback.
  Formal smallpt is `-17,523,872` (`-1.7491%`) with 47 PCs smaller and none larger; all
  unit/version/entry counts remain identical. Formal c-ray raw/equal-entry are `-227,905,071` /
  `-227,602,716` with 67 PCs smaller and none larger. STREAM raw/equal-entry are `-152` / `-216`;
  CoreMark raw/equal-entry are `-147` / `-216`. PPM, c-ray IDAT, STREAM validation, CoreMark CRC
  and spill gates remain exact.
- The default x86 GPR map is not full pin: `SVM_X86_PIN_EXT=2` keeps 14 of 16 architectural GPRs
  resident; only opt-in level 3 adds R13/R15 and remaps R12-R15 onto x6-x9. The prior level-3 audit grew 4,400-unit host
  code by 3.89%, raised spill memory operations from 5,425 to 14,071 and lost 10.18% on loaded
  CoreMark, so it remains rejected. In the retained formal smallpt logs, 72,003,699 / 86,980,059
  weighted SetHostGPR instances and 123,911,206 / 129,472,617 GetHostGPR instances already emit
  zero bytes; the remaining emitted moves are about 14.98M / 5.56M host instructions.
- A single audited consumer may now name the same pinned W read more than once, so `test eax,eax`
  lowers directly to an `ANDS` using w22 instead of first extracting a snapshot. Callee-saved
  pinned byte/word/dword reads also feed `SXTB/SXTH/SXTW` directly. A later second consumer keeps
  the materialized snapshot. Existing formal entries attribute 2,054,236 + 706,866 executions to
  the two proven hot shapes; this estimated 2,761,102-instruction reduction is not folded into the
  headline formal total because long benchmark reruns were stopped. Mac focused validation passes
  6 cases / 429 assertions; no full suite was run.
- A sole U8/U16/U32 pinned read used as the value of an ordinary `StoreMemory` now stores directly
  from its fixed W home. Address reuse, a later value use, an intervening write to that home, U64
  values and TSO stores keep the materialized snapshot. The local Release `4 8 6` short screen has
  byte-identical PPM output and identical 3,250-PC / 3,502-version shapes; the strict common set is
  `-852,490` weighted host instructions (`-0.083737%`), with all top-20 PCs present. Its
  Mac-to-retained-Orb host coverage is only 98.362170%, below the 99.9% promotion gate, so this is
  recorded as a directional screen rather than a formal result. Focused validation passes 5 cases /
  8 assertions.
- A bounded StoreUniform census found that stores are 7.331% of the current local `4 8 6` host
  account and `ThreadContext64::carry_inverted` alone is 36.64% of them, or 2.686% of total host.
  Inverted/direct publications split 63.56%/36.44%. On FlagM hosts the decoder now keeps carry
  Direct across units: sub-family producers emit `CFINV`, add-family producers publish nothing,
  conditional carry paths merge back to Direct, and block-entry CF consumers no longer load the
  polarity byte. Non-FlagM hosts keep the old byte ABI. The temporary census output was removed.
  The short PPM stays byte-identical, but the candidate changes unit formation from 3,242 PCs /
  3,494 versions to 2,757 PCs / 3,597 versions. The strict common-PC join covers only 37.091% and
  6/20 top PCs, so the raw `host_dynamic` change (`2,245,710 -> 593,133`) is not a promotable
  weighted result. Focused IR validation passes 4 assertions; func_tests passes JIT/interpreter,
  region-off, flags-regs-off and CFINV-off with checksum `9f52b7d59285dbe5`; branch-only passes all
  six grids and helper-fault passes 38/38. No long benchmark or full suite was run.
- A five-second truncated opcode audit covers only 4.825% of the current short-run host weight, but
  separates real loads from their address tax: 2,203 / 2,231 weighted `LoadMemory` sites emit one
  instruction, and extra address formation is 46 / 2,277 load-attributed instructions (2.020%), or
  0.161% of the covered host account. The same slice exposes `InvertCarry` at 5.82%, so the larger
  reducible target is dead carry normalization rather than the `LDR` body. `a9f5ddf` lets the existing
  carry-liveness pass remove an inversion only when a later in-block C writer covers every path before
  a reader; live-out C, `TestFlags(C)`, helpers, branches and the block-wide ADC/SBB gate keep it.
  The strict local `4 8 6` A/B has identical 2,757-PC / 3,597-version sets, 100% host/entry and top-20
  coverage, byte-identical PPM, zero spills and common host `593,160 -> 591,583` (`-1,577`,
  `-0.265864%`) with no growing PC. Focused validation passes default/rollback carry elimination
  (32 / 11 assertions), canonical carry (4), SaveCV (4), simple CondSet (62), rotate-zero carry (4),
  region branch flags (46) and full NZCV publication (3). No long benchmark or full suite was run.
- Normalized U8 `Select(condition, 1, 0)` now emits `CSET`, and its two `LoadImm` producers are
  suppressed only when every use is another proven identity select. The proof accepts only direct
  boolean producers and bounded `And` / `Or` compositions. A sole `CondSet` condition is folded into
  identity and general selects; when PSTATE is dirty, `CSET` / `CSEL` runs before `MergeNZCV` so the
  old guest-flags and pending-token publication boundary remains. Other non-local conditions retain
  the existing `MergeNZCV + CMP` path. The
  strict local `4 8 6` A/B has identical 2,757-PC / 3,597-version sets, 100% host/entry and
  top-20 coverage, byte-identical PPM, zero spills and common host `591,583 -> 583,155`
  (`-8,428`, `-1.424652%`)
  with no growing PC. General conditional-select fusion contributes `-5,652` (`-0.959907%`) on top
  of the identity form; one 2,502-entry PC accounts for 5,004 of those instructions. CondSet,
  flag-elimination and COMIS checks pass 3,588 assertions. The bounded
  setcc/cmov/jcc and BMI diagnostics are byte-for-byte identical to the pre-change failure sets;
  the 512-iteration interpreter run passes. A frontend-only SetCC collapse reached `586,558`, but
  raised BMI JIT/interpreter divergences from 324 to 327 by skipping this publication boundary and
  was fully reverted. No long benchmark or full suite was run.
- Adjacent non-float low-64/high-zero `SetHostFPR` publication now emits one `FMOV D,X` at the
  original publication point. The high zero must be a U64 constant, both stores must be
  adjacent lanes of the same resident home, and the existing fault-sensitive `LDR D` fusion keeps
  priority. A zero low value clears the complete home with `EOR V,V,V` rather than requesting the
  macro-level forbidden `FMOV D,XZR`. This recovers rejected scalar-load shapes without moving
  their load or fault point. A shared high-zero constant is suppressed only after every one of its
  uses belongs to a fused publication; any other user retains materialization. The
  strict local `4 8 6` A/B has identical 2,757-PC / 3,597-version sets, 100% host/entry and top-20
  coverage, byte-identical PPM, zero spills and common host `583,155 -> 580,841` (`-2,314`,
  `-0.396807%`) with no growing PC. Shared-zero coverage contributes `-1,601` (`-0.274877%`)
  beyond the sole-use form. FPR publication/fault, resident-XMM, scalar SSE, COMIS and
  directed/fuzzed VEX.128 validation pass 5,616 assertions. The pre-existing SSE batch-B
  JIT/interpreter divergence count remains exactly 392 on both arms. No long benchmark or full
  suite was run. A complete bounded pair census found 105 adjacent publications: all are GPR64 +
  high-zero, 43 use the existing load fusion and 62 use value fusion, for 4,004 weighted pair
  executions; no adjacent pair remains unmatched. The temporary census output was removed.
- `2634fe4` extends resident full-home publication to `VecShuffle32Indexed`. The allocation pass
  and ARM64 emitter independently restrict the same producer set; the existing live-range,
  observer, fixed-home and conflict gates remain unchanged. `TBL` and the proven `EXT` lowering
  are alias-safe when their result is allocated directly in the resident home. The strict local
  `4 8 6` A/B keeps the same 2,757-PC / 3,597-version sets, 100% host/entry and top-20 coverage,
  byte-identical PPM, zero spills and no growing PC, while common host falls `580,841 -> 580,620`
  (`-221`, `-0.038048%`). The resident-XMM matrix now includes both accepted and conflicting
  indexed-shuffle publications and passes 854 assertions; PSHUFD all-immediate coverage passes
  6,914 assertions, directed VEX.128 passes 76, and fixed-seed 256-iteration VEX.128 fuzz passes.
  The pre-existing SSE batch-B JIT/interpreter divergence count remains exactly 392. No long
  benchmark or full suite was run.
- `613dd12` lets a sole U8/U16/U32 callee-saved fixed-home read feed `Sub` directly from its W
  view. Snapshot reuse and an intervening fixed-home write still retain the read bridge; other
  consumers are unchanged. The strict local `4 8 6` A/B keeps all 2,757 PCs / 3,597 versions,
  100% coverage, the exact PPM and zero spills, with common host `580,620 -> 580,290` (`-330`,
  `-0.056836%`) across ten shrinking PCs and none growing. The six pinned-GPR focused cases pass
  14 assertions. Fixed-seed 256-iteration ALU and mixed fuzz retain their exact pre-existing
  88 / 106 divergence counts on both arms. A matching callee-saved `Add` extension saved only 51
  (`0.008789%`) and was removed. No long benchmark or full suite was run.
- `5f9ebac` removes the redundant preparation copy in the narrow `Sub` NZCV path when the emitted
  right operand is exactly `LSL #0`: the existing U8/U16 sign-bit alignment is folded directly
  into `SUBS`'s shifted-register operand. Other shifts, immediates, composites and `Add` keep their
  old paths. The strict `4 8 6` A/B keeps all shape, coverage, PPM and spill gates exact and reduces
  common host `580,290 -> 573,037` (`-7,253`, `-1.249892%`); ten PCs shrink by one instruction and
  none grow. Pinned-GPR focus passes 16 assertions; flags elimination, SaveCV and CondSet pass
  32 / 4 / 62. Fixed-seed ALU/mixed fuzz retain the exact baseline 88 / 106 divergences.
- `993acce` admits `SignExtend` to the existing last-use GPR publication proof. `SXTB`, `SXTH` and
  `SXTW` write a fresh alias-safe destination, while the unchanged input-live, conflict and observer
  gates decide whether it may be the fixed home. The strict screen remains same-shape and exact,
  with common host `573,037 -> 569,108` (`-3,929`, `-0.685645%`), eight shrinking PCs and none
  growing. The expanded producer matrix passes 367 assertions and pinned-GPR focus passes 16;
  fixed-seed mov/extend and mixed fuzz retain the exact baseline 98 / 106 divergences.
- `5a47163` collapses narrow logical flag identities. U8/U16 `TEST reg,reg` no longer builds a
  redundant `And`; a zero-immediate `Or` with no data observer publishes flags from its input, and
  the exact low `BitExtract -> Or(0)` chain may share the source register because the flag emitter
  discards all physical high bits. U8/U16 NZ uses one shifted `ADDS`; U32/U64 uses `TST`. A bounded
  no-detail `4 8 6` host-dump A/B has the same 556 observed PCs, 193 shrinking PCs, no growth and
  `-937` static instructions. Its exact-version intersection covers 12.038773% of the retained
  formal host weight and already removes 43,721,205 weighted instructions (`-0.221854%` of the
  complete formal total), so this is a lower bound rather than a new formal ratio. Hot unit
  `0x419287` falls `260 B -> 240 B`, removing all five instructions around `test dil,dil` beyond the
  single NZ producer. The short PPM is byte-identical at SHA-256
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. Two strict collector runs
  hit the 15-second hard limit and produced no records; they were not extended or retried. Logical
  shape plus flags/SaveCV/CondSet focus passes 120 assertions; fixed-seed ALU/mixed fuzz remains at
  the documented 88 / 106 existing divergences. No long benchmark or full suite was run.
- `477947d` / `e389f9d` / `5b94050` / `183bf78` / `46197f6` compact the remaining high-volume
  NZCV publication and restore mechanisms. Contiguous partial publication is
  `MRS + UBFX + BFI`; full and non-contiguous publication share one emitter across ordinary and
  region paths. `MSR NZCV, x26` now consumes the packed flags word directly because the system
  register ignores all bits outside 31:28. Published region veneers omit even that restore when
  the existing full incoming-flags kill proof succeeds, and simultaneous C/V/AF clears use one
  `BFC` over bits 26:29. No compatibility path or runtime switch was added.
- The final bounded `4 8 6` dump has 558 sections/PCs. Against the 556-PC starting dump, the large
  cold printf unit at `0x47f6c0` split into `0x47f6c0`, `0x47f8a8` and `0x47f8f0`; it has only 240
  retained-formal entries and 36,960 host weight, so it is explicitly excluded rather than treated
  as an exact version. The remaining 555 common PCs have 329 shrink, 226 unchanged, 0 growth and
  `-4,093` static instructions. The exact retained-formal subset is 541 PCs / 12.038585% coverage
  and removes 330,449,276 weighted instructions (`-1.676794%` of the complete formal total). This
  remains a conservative lower bound, not a replacement formal FEX ratio.
- Each same-shape stage was shrink-only: direct packed-register NZCV restore is `-2,068` static /
  `-179,793,723` weighted, proven-dead published-entry restore is `-673` / `-42,596,165`, and the
  contiguous C/V/AF clear is `-668` / `-52,706,706`. The short PPM remains byte-identical at
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`.
- Flags codegen focus passes 142 assertions in 10 cases; region/trampoline/L1 focus passes 224
  assertions in 5 cases, and the auxiliary region localization/link set passes 63 assertions in
  3 cases. Fixed-seed 256-iteration ALU/mixed fuzz remains at the documented 88 / 106 existing
  divergences. No long benchmark, stress run or full suite was run.
- FEX-aligned RE=0 same-harness refresh for formal smallpt: SVM host/guest
  `3.335622 → 3.267832`; the landed stages fold this to about `2.478306`. With unchanged FEX
  `1.549`, ratio is `2.153× → 1.600×`. The earlier
  2.180× table used a different retained unit-formation artifact, so quote the current gap as
  approximately 1.59–1.65× rather than mixing the two raw tables.
- PPM SHA-256 remains
  `fe96f7e48295b27c8df8236294052d138c3ed130b81d022739907fe6b2cde5aa`; prior equal-entry
  c-ray IDAT remains `54256cb4b3c6313a65ea12ebb7b81e30`, 64-spp formal c-ray is
  `d0c71130abf3544a86b64417bc488c21`, and STREAM validates.
- Scalar-load structure/fault tests pass 4 cases / 20 assertions and VEX.128 move differential
  passes at seed 424242 on Mac and Orb. Live-publication tests pass 3 cases / 9 assertions. Existing FPR focus passes
  10 + 794 + 90 + 27 assertions. Scalar-sqrt publication passes 2 shapes / 6 assertions; after
  legacy scalar-binary coverage the current FPR focus is 9 cases / 125 assertions on Mac and Orb.
  The 64-case NaN truth matrix passes Mac/Orb with AFP disabled and cold path 0/1. FLAGS six-grid, helper-fault 38/0,
  clone four-grid and the
  1664-unit/11-guest fingerprint all pass.
- Absolute-address ON/OFF fingerprint matches for 1664 units / 11 guests. Its three focused gates
  pass 3 + 1 + 10 assertions on Mac and Orb; the ON/OFF × function/block/interpreter six-grid is
  byte-identical with rc=101 and checksum `9f52b7d59285dbe5`.
- COMIS compact flags all-consumer differential passes 3,482 assertions on Mac and Orb. FLAGS
  0/1 × function/block/interpreter is byte-identical; the stage fingerprint remains
  1664 units / 11 guests.
- Trailing static-location fallback focus passes 39 assertions on Mac and Orb; cycle signal and
  repeated delink/relink pass 30 / 142 assertions on Orb. Fixed-seed full suite keeps the same
  191 passed / 35 existing failed cases and the same 45 failure sites. Baseline/candidate FLAGS
  0/1 × function/block/interpreter twelve-grid is byte-identical, and the fingerprint remains
  1664 units / 11 guests.
- Compact FCMP carrier all-consumer differential passes 3,482 assertions on Mac and Orb; flags
  focus passes 46 assertions on both. Fixed-seed full suite remains 191 passed / 35 existing failed
  cases with the same 45 failure sites. Baseline/candidate FLAGS 0/1 × function/block/interpreter
  twelve-grid is byte-identical, and the fingerprint remains 1664 units / 11 guests.
- Semantic Nop elision passes the x86 Nop family, RSB/indirect structure, direct-link fallback and
  flags focus on Mac/Orb with 1 / 26 / 39 / 46 assertions; COMIS passes 3,482 assertions. Fixed-seed
  full suite remains 191 passed / 35 existing failed cases with the same 45 failure sites.
  Baseline/candidate FLAGS twelve-grid is byte-identical, clone futex/lock four-grid is green, and
  the fingerprint remains 1664 units / 11 guests.
- Zero-register FPR lane publication extends the existing zero-store matrix to 405 assertions on
  Mac. FPR publication/fault focus and flags focus pass on Mac/Orb; COMIS passes 3,482 assertions
  on both. Baseline/candidate FLAGS twelve-grid and the 1664-unit/11-guest fingerprint match.
  Fixed-seed Orb remains 191 passed / 35 existing failed cases, now 44 failed assertions with no
  new failure category.
- Shared zero-store elision extends the Mac matrix to 537 assertions. Helper-fault is 38/0;
  FPR/flags focus and COMIS 3,482 assertions pass on Orb. Baseline/candidate FLAGS twelve-grid,
  zero-store OFF/ON six-grid and the 1664-unit/11-guest fingerprint are identical. Fixed-seed Orb
  remains 191 passed / 35 existing failed cases / 44 failed assertions.
- Direct simple `CondSet` structure passes 62 assertions on Mac and Orb; flags focus, SaveCV and
  COMIS pass 46 / 4 / 3,482 assertions. Baseline/candidate FLAGS twelve-grid is byte-identical,
  helper-fault is 38/0, and the 1664-unit/11-guest fingerprint matches. Repeated top-level seed
  424242 runs remain 191 passed / 35 existing failed cases; nested child seeds vary the existing
  config/fuzz assertion count between 44 and 45 without adding a failure category.
- The existing address/host-base focus passes 39 assertions on Mac and Orb. Baseline/candidate
  FLAGS twelve-grid and the 1664-unit/11-guest fingerprint are byte-identical. Fixed top-level
  seed 424242 remains 191 passed / 35 existing failed cases / 45 failed assertions.
- Flags focus passes 46 + 12 + 24 + 4 + 62 assertions on Mac and Orb; COMIS passes 3,482 on both.
  Baseline/candidate FLAGS twelve-grid, helper-fault 38/0 and the 1664-unit/11-guest fingerprint
  match. Fixed top-level seed 424242 remains 191 passed / 35 existing failed cases / 45 failed
  assertions.
- The existing zero-rotate repro passes 3 assertions on Mac and Orb. Flags focus and COMIS remain
  green, FLAGS twelve-grid, helper-fault 38/0 and the 1664-unit/11-guest fingerprint match. Fixed
  top-level seed 424242 returns to 191 passed / 35 existing failed cases / 45 failed assertions.
- Flags focus, the zero-rotate repro and COMIS remain green on Mac and Orb. Baseline/candidate
  FLAGS twelve-grid and the 1664-unit/11-guest fingerprint match; fixed top-level seed 424242
  remains 191 passed / 35 existing failed cases / 45 failed assertions.
- The address/host-base focus passes 39 assertions on Mac and Orb. Baseline/candidate FLAGS
  twelve-grid is byte-identical. The candidate is self-consistent at 1,657 units / 11 guests;
  compared with the 1,664-unit baseline, seven units coalesce and decoded-block/IR totals fall
  while every guest output remains exact. Fixed top-level seed 424242 is 191 passed / 35 existing
  failed cases / 44 failed assertions.
- The seven-instruction L1 structure gate passes 26 assertions on Mac and Orb; inline-L1
  signal/invalidation passes 13, trampoline coverage passes 154, and production direct-link
  coverage excluding the existing disk-cache failure passes 393 / 349 assertions on Mac/Orb.
  Baseline/candidate FLAGS twelve-grid is byte-identical and the 1,657-unit/11-guest fingerprint
  matches. Fixed seed 424242 is 191 passed / 35 existing failed cases / 45 failed assertions with
  the same failure-file set.
- The address-lowering focus passes 84 assertions on Mac and Orb. Baseline/candidate FLAGS
  twelve-grid is byte-identical. The candidate is self-consistent at 1,657 units / 11 guests;
  unit and decoded-block totals stay fixed while six guests lose 152 aggregate IR instructions.
  Fixed seed 424242 remains 191 passed / 35 existing failed cases / 45 failed assertions with the
  same failure-file set.
- Stack-push structure and fault recovery pass 3 cases / 13 assertions on Mac and Orb. The
  baseline/candidate FLAGS twelve-grid is byte-identical, and the function fingerprint matches at
  1,657 units / 11 guests. Excluding the three new focused cases, exact baseline/candidate suite
  runs are both 191 passed / 35 existing failed cases / 44 failed assertions with the same failure
  locations; the known nested-child variation remains 44–45.
- Constant-page cache structure passes 16 assertions on Mac and Orb, including nearby addresses,
  scratch exhaustion and biased-memory exact-address fallback. Cache OFF/ON FLAGS twelve-grid and
  bounded-bias func_tests are byte-identical; the function fingerprint matches at 1,657 units /
  11 guests. Final default-ON and rollback suite runs both return 194 passed / 35 existing failed
  cases / 45 failed assertions with the same failure locations.
- RSB/indirect structure focus passes 26 assertions, including the paired state/cache load and
  no-target dispatcher path. A temporary mismatched-return probe passes default, both L1-off RSB
  frames, FLAGS-off and interpreter paths and was deleted. Mac and Orb builds pass.
- The production inline-L1 pending-signal test passes 6 assertions on Mac and Orb. Direct-link
  production passes 11 cases / 395 assertions under FLAGS=0, including cache lifecycle. Static
  SetLocation fallback and repeated delink/recompile paths pass 36/142 assertions on Mac and Orb.
- Region edge, direct-cycle signal and region-flags focus pass 42/30/46 assertions. The new layout
  matches the baseline fingerprint for 1664 function units over 11 guests.
- MT SMC stress passes 200/200 with zero host failure, lost guest or timeout. Catch/fuzz seed
  424242 gives the latest Orb tree 191 passed / 35 existing failed cases / 44 failed assertions;
  the failed case count and categories are unchanged.
- Detailed mechanism table, the rejected Linux scalar-insert prototype and exact deltas are in
  `docs/codegen-fpr-flags-refresh-2026-08-24.md`.

## Invariants (do not violate)

- Recorded cond is **guest** polarity; PSTATE is **host NZCV** (maybe CFINV). Unproven `b.cs` inverts JC/JNC.
- `RecordLocalCondition` does **not** prove PSTATE liveness. Merge is x26 publication.
- Successor `dirty && requested=={}` Merge copies **no** PSTATE. If-pack is last publication for CheckHalt/GetFlags/Unpark-from-x26.
- Empty-requested Merge must **not** copy-all PSTATE (SIGABRT).
- Do **not** force `nzcv_dirty=true` at region block entry (skip+dirty-at-entry lost density).
- Unpark reads **x26**. Transparent L2 exits on RE=1 still need a pack at HostExit or region-mode Dispatcher.
- `BranchOnlyFlags` producers must **not** set `nzcv_requested/dirty` (SIGABRT). Covering them in `SuccessorCovers` is the opposite: they **overwrite PSTATE**, so the **pred** If can skip pack.
- INC leftover C is live; do not treat INC as covering CF; do not copy C at INC entry (halt reason 2).
- `kMaxFuncBlocks=128`. `lazy_budget < 128` used to make **128 eager**. Keep `<=`. Never raise default window past 128 without raising the cap **and** keeping published-L2 skip.
- A faulting full-width `LoadMemory` may publish directly into its pinned home because the fault does not commit the destination. Partial/narrow writes and any path rejected by the existing local observer, conflict, or liveness proof must keep the real publication instruction.
- An ordinary `StoreMemory` may read a pinned W home directly only when an offset-zero U8/U16/U32
  `GetHostGPR` has exactly that store-value use and no intervening write to the home. Address uses,
  later uses, U64 values and TSO stores keep the snapshot instruction.
- FlagM units keep host C equal to x86 CF at every cross-block boundary. A live `InvertCarry` after
  a sub-family producer is part of that canonicalization, not a polarity toggle that can be removed
  locally. It is dead only when backward liveness proves a later in-block C write covers every path
  before a read; live-out C and any intervening reader retain both the inversion and its producer.
  Direct carry cannot use the inverted-carry `HI/LS` folding rule. Non-FlagM hosts still persist and
  load `carry_inverted`.
- A low32 copy may skip `BitExtract` only when its sole use is the immediately following
  `ZeroExtend32To64`; the wrapper must still emit a W move and keep all later uses.
- A width round trip may substitute the original U32 SSA only for
  `BitExtract(ZeroExtend32To64(v32), 0, 32)` with one ordinary same-block consumer inside the
  128-IR window.
- Same-width extraction is restricted to a U32 result and an audited W-reading consumer. U8/U16
  normally retain the real extract because backend physical high bits are not implied by the narrow
  IR type. The only narrow exception is the immediate, sole-use low extract feeding a no-data-use
  `Or(0)` flag identity: its shifted NZ producer discards every physical high bit, and both the RA
  tie and emitter shape must remain exact. Pseudo and opaque calls always retain the extract.
- A static `SetLocation` exit may emit a direct-link site only for the existing same-module,
  non-self, BlockLink-enabled region contract. The cycle poll remains before the site; unavailable
  regions, cross-module targets and disabled BlockLink keep the inline L2/dispatcher fallback.
  The site must stay registered with LinkManager so SMC can restore its trampoline branch.
- An `indirect_l1` module must not produce RSB frames. The first `State` pair is
  `exit_request + indirect_l1_code_cache`; production `ForwardIndirectL1` loads it with `LDP` and
  tests the signal bit with `TBNZ`. Its signal arm must return through the shared trampoline so the
  offset-zero `LDAR` confirms the request before returning `Signal`; profile mode keeps its separate
  cache-base load. Missing retained targets return to the dispatcher. Only L1-off modules may pair
  `EmitRSBPush` with `EmitRSBPop`.
- A direct cycle edge may use the next region block to choose conditional layout, but it is not a
  true fallthrough: retain `LDAR/CBNZ`, then branch over the source block's immediately following
  cold stubs. Falling through after the poll executes the CodeMiss/Signal stub on every iteration.
- A complete V128 producer may remain in a resident FPR home after `SetHostFPR` only when its full
  interval has no other value mapped to that home, the pre-publication observer checks pass, and no
  later write to that home precedes the producer's last use. The producer cannot be rebound to a
  second resident home; the emitter must independently reproduce all of these checks.
- A scalar memory load may replace adjacent low-load/high-zero resident publications only when
  both producers are single-use, the high value is exactly U64 zero, and the load-to-publication
  window contains no fault/helper, local-control, target-home access, or overlapping mapped FPR.
  Emit the D-register load at the original fault site and reprove the complete plan there.
- A zero-register store value must be an unspilled integer `LoadImm(0)`. Multiple uses are allowed
  only when the inclusive global use count exactly equals same-block StoreUniform, StoreMemory or
  SetHostFPR value operands. Cross-block, address, arithmetic, pseudo, floating and nonzero uses
  retain materialization.

## Failed / do not retry

| Attempt | Result |
|---|---|
| BFXIL 2-insn Merge | Broke FLAGS=0 and ON |
| INC C-preserve / skip leftover C | Halt reason 2 |
| Empty Merge copy-all PSTATE | SIGABRT |
| Skip If + dirty-at-entry | **+3.4%** host |
| Transparent skip + pack CheckHalt/all HostExit | **7.154→7.200B** |
| Full split-arm pack / invert loop branch | hang rc=124 |
| Mark BranchOnly TEST dirty so `then_covers` fires | SIGABRT FLAGS=1 |
| Fallthrough-only defer / else-only Merge without requested | no density or hang |
| Function CFG live-out for ordinary flag deletion | Re-tested after condition-specific liveness. Bounded smallpt changes 2,802/3,435 units/versions to 2,790/3,011, covers only 98.918559% of baseline host weight and grows the common subset `395,147 -> 396,850` (`+0.430979%`); fully reverted. |
| `SVM_RA_WIDTH_CHAIN=1` | 0 on coremark |
| `GetHostGPR` 32-bit `Mov W` for callee-saved pins | 0 |
| `SVM_FUNC_LAZY=128` before `15e5165` | **31.3B** host, RE=0-shaped entries |
| Require every successor-cover to survive fault and reach AdvancePC | **6.300→6.551B** host; too conservative, reverted |
| PF/AF dedicated GPR on current CoreMark | saves 0; adds 67,754,766 dispatcher/RSB recovery instructions |
| SHA census from failing OpenSSL path | PageFatal at `rip=0x62b930` before valid hashing; no performance evidence |
| Generic same-width fold including U8/U16/CallLambda | fixed-seed U16 popcount helper mismatch; narrowed to U32 W-consumer whitelist |
| True fallthrough after a direct cycle poll | falls into the source block's cold stub; CoreMark 1.224s→57.597s despite correct CRC, fully rejected |
| Tagged L1 control word loaded with nonzero-offset `LDAR` | AArch64 `LDAR` has no immediate offset; VIXL ignored it and production hit PageFatal, fully reverted |
| `LDAXP` request/cache-base pair | smallpt `-1,905,795`, but call-dense scale-3 wall time regressed 223%; fully reverted |
| Terminal-tail cycle success branch | formal smallpt bit-identical at `1,168,614,398`; safe target-bound pool is empty after successor layout, fully reverted |
| Same-value carry-polarity publication dedup | equal-entry smallpt only `-23,703` (`-0.0022%`), with 137 PCs larger, 17 smaller and changed unit formation; fully reverted |
| Generalized legacy scalar resident-left chain | formal smallpt byte-identical at `1,072,445,284`; exact `GetHostFPR` origin is not the remaining limiter, fully reverted |
| Generated TestZero/TestNotZero local condition | FLAGS=1 transparent window still saves only 919 formal smallpt instructions; fully reverted |
| Sole TestZero/TestNotZero identity/general Select fusion | strict local `4 8 6` saves only 79 (`-0.013547%`); exact PPM and no growth, but the extra planner state is not justified and was fully reverted |
| Zero-register `SetHostGPR` publication | smallpt / c-ray equal-entry only `-1` / `-22`; existing GPR coalescing already absorbs it, fully reverted |
| Transparent `BitCast` zero-store graph | formal smallpt and c-ray are byte-identical at every equal-entry PC; the proof reaches no remaining materialization and was fully reverted |
| Pinned GPR immediate-offset memory address | smallpt is byte-identical and c-ray's partial retained-entry subset saves only `0.012823%`; the extension was fully reverted |
| Dead overwritten `SetHostGPR` IR deletion | the common smallpt subset shrinks, but unit/version formation diverges, 1,675 dynamic spills appear, and c-ray `64x48/s1` times out at eight seconds; the IR-lifetime prototype and its test were fully removed |
| Remaining absolute `GetOperand` materialization | 21.75M left-immediate instances are true two-part constants; ADRP/literal alternatives do not preserve the current relocation and mapping contract |
| Saved-flags compound `CondSet` | two-instruction HI/LS and GE/LT forms were implemented and validated, but execute 0 times in formal smallpt/CoreMark and the c-ray audit sample; GT/LE still need three inputs, so the zero-gain prototype was removed |
| General narrow `TEST` direct-`And` flags | 496-PC bounded A/B had 29 shrinking and 31 growing PCs, only 13 net static instructions and `-623` retained-formal-weighted instructions; fully reverted |
| Direct U8/U16 zero-only `CMP` | the local-kill form shrinks smallpt's common set by `1.282746%`, but CoreMark changes to 2,849 PCs / 3,382 versions and fails with `crcfinal=0x630a`; even the strict `BranchOnlyFlags(Z)` form reproduces the same CRC failure, so the emitter, test and CMake entry were fully removed |
| `SVM_FLAG_FULL_ELIM=1` after condition-specific liveness | Exact oracle and unit/version sets, but bounded smallpt grows `399,467 -> 408,275` (`+2.204938%`), dominated by partial-NZCV publication at `0x419287`; remains OFF. |
| Global inverted-carry ABI default | Re-tested after the scalar-FPR proof fix. The oracle stays exact, but bounded smallpt changes 2,802/3,435 units/versions to 3,207/3,493 and grows the strict common subset `370,990 -> 394,889` (`+6.441953%`). Hot `TEST`/logical producers make Direct carry dominant in execution even though the emitted producer census favors subtraction. Fully reverted; any remaining carry work needs explicit edge polarity. |
| IR-rewriting integer `Sub` branch-only carry normalization | the non-carry-only form still changed the bounded unit/version set from 2,755/3,621 to 2,785/3,056; strict coverage was 99.648660% with two growing PCs, below the 99.9% gate despite `-0.908475%` on the comparable subset, fully reverted. `86aaac4` is a separate backend-only EQ/NE proof and does not revive this rewrite. |
| Generic live `ZeroExtend32To64` result remap into the publication home | the residual `live_ok` census classified stores before conflict and observer checks, so its weighted total overstated the opportunity. The broad remap made many required moves merely change location, added low-view preservation copies, and halted the 1,000-iteration CoreMark screen at `0x402e60`. The RA module, logs and temporary restrictions were removed; retain only backend copy fusions that prove a net one-instruction form. |
| Pinned-copy low-32 `BitExtract` address aliases | the focused local/Orb case passed, but bounded smallpt and 1,000-iteration CoreMark were both byte-identical with 100% weighted coverage. The emitter hook, matcher extension and test were removed. |
| Multi-use fixed-home snapshot reuse | The exact post-publication Xor/narrow-alias graph shrank three formal CoreMark CRC blocks by two instructions each and the common subset by `9,120,008`, but changed `crcfinal` to `0x4555`. A later consumer still requires the original snapshot even when the visible target-home overwrite window appears closed. The matcher, diagnostics and test were fully removed. |
| Direct pinned immediate publication | The broad constant form shrank the formal common CoreMark subset by `16,240,214` but returned CRC `0x6096`. Restricting it to the audited `LoadImm(8) -> home 0` shape still shrank `13,400,033` and returned CRC `0x398e`. Writing the fixed home at the producer crosses an old-value observation not represented by the local alias whitelist; the emitter path, state and test were fully removed. |
| Disable inline indirect L1 and use the existing RSB path | The exact 2k CoreMark unit/version set and CRC remain stable, but weighted host work grows `351,368,645 -> 368,914,549` (`+4.993588%`). The current guarded RSB pop is longer than the seven-instruction inline-L1 hit path; do not flip the existing feature or re-enable RSB pushes while indirect L1 is active without a new continuation ABI. |
| Delay dynamic `current_loc` publication on the inline-L1 hit path without a new continuation ABI | SMC invalidation keeps the L1 key and replaces only its value with the shared miss trampoline. That trampoline must reload the published location before its L2 walk, and the signal/key-miss path returns through the dispatcher for the same reason. Moving the store cold therefore requires a known-register continuation ABI for every inline exit and invalidation value; omitting it locally can dispatch or compile the stale PC. |
| Omit a pinned U32 self-extension when later reads are only scaled memory indices before a full overwrite | `0x403630/0x403688` can encode their address indices with `UXTW`, but the intervening memory RMW may fault. A signal context must already observe the x86-required zeroed high 32 bits of the guest register, so the apparent `mov w22,w22` remains architecturally visible before that fault boundary. |

## Next ready (pick one, measure, revert on 124/134)

The current single-version opcode ledger covers about 82.85% of formal smallpt host execution. The
pre-canonical-carry largest per-op responsibilities were StoreUniform 82.41M, VecFMulScalar64 78.89M,
LoadMemory 76.55M, GetOperand 65.87M, VecFAddScalar64 58.03M, LoadUniform 56.33M and
StoreMemory 51.64M. Do not subtract the local short carry census from these formal values: the
candidate changes unit/version formation and must pass a future formal gate before the ledger is rebased.

The new bounded census closes redundant partial/full merge masks, redundant pre-`MSR` masks and
provably dead published-entry restores. `0a2eabf` adds the cross-unit pending-flags entry for full
NZCV edges whose target proves a complete overwrite. Remaining partial requested masks and targets
that observe incoming flags still need a broader contract; do not reopen them with more mask
peepholes.

1. **Cross-edge carry polarity** — `28f459a` closes dead-edge `CMP/Jcc` carry publication, including
   `JB/JAE/JA/JBE`, and `5bdf30e` closes condition readers that do not consume C before a later
   in-block C overwrite. Remaining inversions preserve architecturally live CF across an edge or
   serve a real carry observer. The global inverted ABI is a measured regression because hot
   logical producers are Direct. Continue only with an explicit edge/version polarity contract
   that canonicalizes mixed joins; do not change the decoder-wide default or add a runtime
   polarity store on FlagM.
2. **Pinned GPR residuals** — keep the 14-register level 2 map as the performance default. Recount actual emitted bytes,
   not GetHost/SetHost IR. The largest remaining SetHost moves in the retained log implement real
   guest copies such as `mov rbp,rdi` and `mov rbx,rdx`; deleting them requires architectural
   register renaming, not another fixed-home peephole. Direct ordinary StoreMemory payload reads
   and callee-saved `Sub` reads are closed. Continue only with another measured consumer that can
   read the fixed home directly while retaining snapshot, width and helper-clobber proofs; the
   same `Add` extension was only `-51` and is closed. Selective R12/R14 pinning is landed; do not extend
   the map to R13/R15 without resolving the Mac 15-register hang. The pre-R12 bounded emitted-write census has
   31,705 weighted `SetHostGPR` instructions: 13,002 are `GetHostGPR`-root guest copies. The older
   3,944 `SignExtend` pool, the signed-load publication/low-alias pool and ordinary saved-flags
   zero-test aliases are now closed. Recount roots after each landed stage; do not treat the
   remaining total as a generally safe GetHost elimination. Another 5,157 `Sub`-root live writes
   are Mac biased-memory stack updates separated from publication by a faulting store; Linux
   identity already folds the exact safe form into pre-index stores, so this is not a remaining
   FEX-alignment pool.
3. **smallpt remaining link** — covered link is now about 6.6%. Region/cycle tails are about
   2.1%; their acquire poll and branch across per-block cold stubs are load-bearing. Audit the
   roughly 1.26% remaining return-L1 static sequences separately; address formation is now one
   `BFI`, and `LDP + CMP + CSEL + BR` has no obvious base-ISA fusion. Public host exit executes
   only 139 times. Full-NZCV external direct edges with overwrite-first targets now use the pending
   entry; continue this direction only with an explicit partial-mask or observing-target ABI. The
   remaining `SetLocation` tail is dynamic or has a later observer and must not inherit the
   trailing-constant proof.
4. **Remaining FPR publication** — the older SetHostFPR, scalar64-copy and low-load/high-zero
   accounts predate both platform-neutral scalar insert and the full XMM0-15 resident ABI below.
   The current weighted ledger has 76,229 emitted `SetHostFPR` instructions. A bounded rejection
   census found that the apparent remaining scalar candidates are dominated by publication to a
   second resident home and chains whose high lanes originate in another XMM home; these are real
   guest copies, not an unclosed fixed-home tie. Fault snapshots remain load-bearing. Non-AFP hosts
   retain the legacy scalar high-lane preservation sequence, and resident-disabled configurations
   retain ordinary State publication. The indexed-shuffle pool remains closed; do not broadly
   whitelist scalar merge-home shapes.
5. **Remaining composite EA** — identity `[base+imm]`, `[base+index]` and matching scaled-index
   forms are now direct. Remaining materialized forms involve bias/32-bit wrapping, shifts or an
   AArch64-unencodable scale; require an exact encoding and wrap proof before extending the gate.
   The truncated short audit attributes only 2.020% of observed `LoadMemory` work to address
   formation, so do not treat the raw opcode total as a removable pool.
6. **CoreMark remaining truncations** — raw BitExtract is no longer a pool. Adjacent sole-use
   narrow memory extensions now write their consumer register directly, adjacent low U8/U16
   extracts plus `ZeroExtend32` emit at most one instruction, and clean-load self-extensions emit
   none. Only reopen other 8/16-bit cases with a consumer-specific physical-high proof and the U16
   helper regression in the gate.
7. **SHA valid workload first** — fix or replace the current OpenSSL guest path that PageFatals before hashing, then redo the boundary census. Do not bypass guest fault semantics.
8. **PF/AF dedicated GPR is closed** until a new canonical park/recovery carrier yields a nonzero mechanical saving; the current audit is strictly negative.
9. **Do not** grow the default region window again for coremark (64 == 128). Other benches might still want 128 **after** the lazy fix.

## 2026-08-25 continuation

- Default level 2 now pins 14 of 16 guest GPRs. Level 3/full pin remains closed: the fixed audit
  grows move/bridge work by 3.526%, and the Mac Debug pool can abort at 6 available registers for
  a 21-register scratch demand.
- `quick_shape.py --static-only` now captures the existing host dump without runtime entry counters.
  The bounded `smallpt_wh_x64 4 8 6` screen completes in 2.3–4.2 seconds on this Mac Debug build,
  covers 558 PCs and preserves PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. Use it to reject
  zero-impact or growing candidates; retained formal entries remain the promotion weights.
- Closed in the fast screen: region window 128 grows common code by 357 instructions (`+0.572473%`);
  AFP minmax and SHUFPS immediate are byte-identical; width chain saves 9 instructions; loop flags
  grows 40; full flag elimination grows 3. Direct `GetHostGPR -> SetHostGPR` is byte-identical, and
  forwarding through `ZeroExtend32To64` saves only 38 of 62,372 instructions (`0.060925%`). All
  production prototypes and their probes were removed.
- `4f27b7a` adds canonical-state cross-unit dual entries. The cold linker still returns through the
  published entry after its C++ helper call; once linked, the patched branch targets the counted
  body entry and skips the redundant `MSR NZCV,x26; B body`. Flags-transparent blocks retain the
  published entry because their terminal may republish incoming PSTATE. The second entry is carried
  through LinkManager generation/SMC ownership and disk-cache format v6.
- Each eligible linked transition now has a mechanical two-instruction reduction. Three tiny
  interleaved wall pairs were dominated by warm-up noise (the final pair was 2.219s/2.218s), so this
  stage makes no wall-time claim. The region trampoline test covers public-first/direct-after-patch,
  signal delink remains green, and the smallpt oracle is exact.
- The bounded pending-flags census found 81 full-NZCV edges in smallpt (`4 8 6`) across 51 source
  merge groups. Of 54 edges with a compiled target, 18 targets already satisfy the existing
  overwrite-before-observe/fault proof. The matching c-ray census found 263 full-NZCV edges among
  436 observed external edges. A per-target cold-stub design would have grown smallpt by 41 static
  instructions and was rejected.
- `0a2eabf` completes the full-NZCV cross-unit contract. Candidate sites initially branch around the
  three-instruction source merge and use a pending slow trampoline that materializes PSTATE NZCV
  into x26 while preserving PF/AF. A proven overwrite-first target publishes a pending entry at its
  counted body. Before an incompatible target is patched directly, LinkManager restores the source
  merge; unlinked and far arms remain safe through the pending trampoline.
- The first implementation required every conditional arm to be linked and compatible. The bounded
  smallpt/CoreMark runs enabled zero groups, so it was replaced before commit. In the final design,
  unexecuted cold arms no longer block a hot compatible arm. The smallpt debugger census observed
  five incompatible-link restores and two compatible re-enables. Each surviving linked transition
  replaces three merge instructions with one branch; the first cold link pays two extra trampoline
  instructions, and an incompatible linked edge returns to the old steady-state cost.
- Pending target entries, source patch metadata and normalized live merge words are persisted in
  disk-cache format v7. Focused validation passes 324 assertions across seven tests, including
  conditional execution, incompatible-target fallback, signal/SMC delink, cache serialization and
  all static-pin trampoline configurations. The final static smallpt screen completed in 2.352s,
  covered 558 PCs and preserved PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. Unit sizes are unchanged;
  this stage makes no wall-time claim and ran no long benchmark, stress test or full suite.
- XMM0 now joins the default resident ABI, so XMM0-11 map to v16-v27. The bounded Orb
  `smallpt_wh_x64 4 32 24` comparison has identical 2,793-PC / 3,647-version sets, 100% host,
  entry and top-20 coverage, exact PPM SHA
  `fe779f46a4c8f0f75ab42b573253492e5f1da2ee508fdb6aee62389787244cd0` and zero spills. Weighted
  host instructions fall `4,914,012 -> 4,804,966` (`-109,046`, `-2.219083%`): 163 PCs shrink,
  three grow and 2,627 are unchanged. Move-class instructions rise `1,291,180 -> 1,313,749`, but
  the fixed-home moves replace a larger uniform-state load/store cost. The final Mac static screen
  completes in 1.2-1.3s, covers 558 PCs and preserves its existing PPM oracle. Disk-cache format is
  v8 because cached code embeds the resident-XMM ABI. Focused resident-XMM, page-fault and cache
  serializer validation passes 909 assertions; the cross-process v8 cache round trip also passes.
  This stage makes no wall-time claim and ran no long benchmark, stress test or full suite.
- Replacing the level-2 R8-R11 pins with R12-R15 kept the same 12-register pressure and exact PPM,
  but the comparable retained-entry subset grew by 4,087 instructions (`+0.145456%`). The map and
  its temporary captures were removed; do not treat guest ABI lifetime alone as a pin-selection
  proof.
- A partial-NZCV direct-link prototype deferred its three-instruction merge until a compatible
  overwrite-first target linked. The bounded density census found 16 partial arms in eight shared
  merge groups, but none of their targets satisfied the existing complete-overwrite contract;
  the weighted saving upper bound was zero. The implementation, tests and census logging were
  removed rather than retaining an unused cross-unit ABI extension.
- Scalar SSE insert now follows detected FEAT_AFP on Linux as well as macOS. Orb's native NEP probe
  preserved the upper 64-bit lane with `FPCR=0x6`, and the translated 64-case SSE NaN/high-lane
  truth matrix passes with scalar insert both enabled and disabled. The old Linux rejection no
  longer reproduces after the completed FPCR/AFP lifecycle work.
- The bounded Orb `smallpt_wh_x64 4 32 24` scalar-insert OFF/ON comparison has identical 2,793-PC /
  3,647-version sets, 100% host, entry and top-20 coverage, exact PPM SHA
  `fe779f46a4c8f0f75ab42b573253492e5f1da2ee508fdb6aee62389787244cd0` and zero spills. Weighted
  host instructions fall `4,804,966 -> 4,598,890` (`-206,076`, `-4.288813%`); move-class work falls
  by the same `206,076`, from `1,313,749` to `1,107,673`. Programmatic cache identity now hashes
  effective scalar-insert policy. This stage ran no long benchmark, stress test or full suite.
- With Linux scalar insert active, extending the resident ABI from XMM0-11 to XMM0-15 no longer
  creates FPR spills. The bounded Orb `smallpt_wh_x64 4 32 24` comparison retains identical
  2,793-PC / 3,647-version sets, 100% host, entry and top-20 coverage, exact PPM SHA
  `fe779f46a4c8f0f75ab42b573253492e5f1da2ee508fdb6aee62389787244cd0` and zero spills. Weighted
  host instructions fall `4,598,890 -> 4,516,623` (`-82,267`, `-1.788845%`). State sequences fall
  from 64 to one; move-class work grows `1,107,673 -> 1,214,973`, but the eliminated State traffic
  is larger. The Mac 558-PC static screen is exact and falls `62,372 -> 62,209` (`-0.261335%`).
  Cache format is v9. Resident ABI, rollback, fault, AFP and cache tests pass locally; this stage ran
  no long benchmark, stress test or full suite.
- Scalar binary chains now transfer a resident XMM home across each exact last-use edge instead of
  computing in a temporary FPR and publishing later. At `0x4023c0`, three multiply/add chains lose
  six full-vector moves and the unit falls from 377 to 371 host instructions. Applying the retained
  baseline entries to 2,785 same-version Orb PCs gives `4,379,137 -> 4,249,587` (`-129,550`,
  `-2.958345%`); the largest reductions are `0x402777` and `0x40284e` at nine instructions each.
  The single bounded production run records 2,793 PCs, 3,659 versions, 271,517 entries,
  `host_dynamic=4,276,521`, `move_dynamic=1,059,189`, zero spills and exact PPM SHA
  `fe779f46a4c8f0f75ab42b573253492e5f1da2ee508fdb6aee62389787244cd0`.
- Resident-XMM fault snapshots now retain the last pre-fault carrier and let register allocation
  commit it directly into the fixed home. This fixes the packed arithmetic fault case where a
  faulting RHS load previously returned stale XMM state. Its focused code remains 16 host
  instructions with `fmul v16` committed before the load and `fadd v16` after it. One smallpt
  boundary block requires one real publication instruction, costing 3,048 retained-weight
  instructions versus the unsafe candidate. Local and Orb FPR/XMM/scalar validation both pass
  1,887 assertions across 24 tests. This stage ran no long benchmark, stress test or full suite.
- Compact COMIS relations now remain local across audited MOVSD and vector moves while the backend
  verifies every intervening IR operation with the shared host-NZCV preservation proof. This
  removes flag materialization and reload around scheduled compare/move/Jcc shapes without adding
  a recovery path or configuration switch. The bounded Orb `smallpt_wh_x64 4 8 6` weighted screen
  covers 99.995707% of the retained host weight and all top-20 PCs. The comparable total falls
  `4,285,406 -> 4,218,607` (`-66,799`, `-1.558755%`): MOVSD accounts for 39,936 and the vector
  move extension another 26,863. The largest reductions are `0x402e21` at 15,360,
  `0x40274f` at 14,575 and `0x402ddb` / `0x402dfe` at 12,288 each. The exact PPM SHA is
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`; seven focused flags,
  COMIS and fault tests pass 3,585 assertions. This stage ran no long benchmark, stress test or
  full suite.
- `178138e` detects FEAT_FRINTTS on macOS and Linux and lowers scalar float-to-integer conversion
  with the same sized-round plus `FCVTZS` mechanism as FEX. Truncating forms use `FRINT32Z` or
  `FRINT64Z`; MXCSR-rounded forms use the corresponding `X` instruction against the installed
  guest FPCR. Scalar register and memory sources remain in the FPR class instead of crossing
  through a GPR first. On Orb, each of the three hot smallpt conversions falls from 68 emitted
  bytes to eight. The bounded `smallpt_wh_x64 4 8 6` screen covers 99.995707% of retained host
  weight and all top-20 PCs; the comparable total falls `4,218,607 -> 4,181,743` (`-36,864`,
  `-0.873843%`) with exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. A 256-case conversion-only
  SSE edge sweep passes. The 7,699-assertion AVX/Rosetta run reports zero conversion mismatches;
  its two existing aggregate failures contain only `VUCOMISS` flag differences. This stage ran no
  long benchmark, stress test or full suite.
- `bb197d8` keeps live host NZCV across direct `Select`/CMOV consumers. These instructions preserve
  PSTATE, so the old unconditional merge was premature; only the independently carried PF/AF token
  is committed at the select. The bounded Orb screen keeps identical PC/version sets, 99.995707%
  retained-host coverage, all top-20 PCs and exact PPM SHA. The comparable total falls
  `4,181,743 -> 4,143,628` (`-38,115`, `-0.911462%`) with no growing PC. The COMIS all-consumer
  differential passes 3,482 assertions and the focused flags units pass 66. The existing
  setcc/CMOV/Jcc fuzz has the same 124 known flag differences in baseline and candidate. This stage
  ran no long benchmark, stress test or full suite.
- `cab14f9` lowers the twelve PSHUFD controls that map exactly to one Arm64 copy, `DUP`, `EXT`,
  `ZIP` or `TRN` instruction through one shared decoder. Immediate and cached-index forms use the
  same emitter; an indexed form skips `VecLoadConst` only when every use belongs to the proven
  direct-shuffle component. The former `SVM_PSHUFD_4E_EXT` gate and its obsolete fallback were
  removed. The bounded Orb screen completes in 2.852 seconds with identical PC/version sets,
  99.995707% retained-host coverage, all top-20 PCs, no growing PC and exact PPM SHA. The comparable
  total falls `4,143,628 -> 4,111,648` (`-31,980`, `-0.771787%`); `0x436420` accounts for 31,720.
  The exclusive-use proof passes 17 assertions, the complete 256-immediate cached/uncached sweep
  passes 6,914 and the FeatureSet contract passes 158. This stage ran no long benchmark, stress
  test or full suite.
- `90dcaf6` keeps the compact FCMP ordering carrier live across `InvertCarry`. `CFINV` changes only
  host PSTATE carry and leaves the raw ordering bit in the carrier GPR intact, so the hot compare
  path writes that carrier directly instead of routing it through a temporary GPR and `BFXIL`.
  The bounded Orb screen completes in 3.604 seconds with identical 2,755-PC / 3,621-version sets,
  99.995707% retained-host coverage, all top-20 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total
  falls `4,111,648 -> 4,061,499` (`-50,149`, `-1.219681%`). The COMIS all-consumer JIT/interpreter
  differential passes 3,482 assertions. This stage ran no long benchmark, stress test or full
  suite.
- `7196145` extends resident scalar-FPR ownership through an intermediate publication. When a
  scalar producer already occupies the exact fixed XMM home and dies at the next scalar operation,
  the successor remains in that home; the backend reuses the existing recursive chain proof before
  suppressing either publication. The bounded Orb screen completes in 4.024 seconds with identical
  2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all top-20 PCs, no growing PC
  and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total
  falls `4,061,499 -> 4,039,995` (`-21,504`, `-0.529460%`); `0x40248e` and `0x402497` each lose
  seven instructions per entry. Ten focused scalar-FPR, differential and fault tests pass 521
  assertions locally. This stage ran no long benchmark, stress test or full suite.
- `4355474` lets adjacent scalar memory-load publications share one zero high-half constant. The
  zero is removed only when every use participates in a proven scalar publication, while each load
  still requires single use, exact adjacency, fault-safe ordering and no live fixed-home conflict.
  The bounded Orb screen completes in 2.829 seconds with identical 2,755-PC / 3,621-version sets,
  99.995707% retained-host coverage, all top-20 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total
  falls `4,039,995 -> 3,994,447` (`-45,548`, `-1.127427%`). The largest reductions are `0x402777`
  at 12,288, `0x4026a0` at 10,212, `0x40248e` at 9,516 and `0x402497` at 8,916. Three focused
  scalar-load, shared-zero and fault tests pass 21 assertions locally. This stage ran no long
  benchmark, stress test or full suite.
- `4ea3a66` writes unused arithmetic results directly into the resident flags token register. Flags-
  only `Add`, `Sub`, `Neg`, `Adc`, `Sbb`, `And` and `AndNot` producers target `x12` or `w12`; observed
  results and branch-only flags keep their normal allocation. The bounded Orb screen completes in
  about three seconds with identical 2,755-PC / 3,621-version sets, 99.995707% retained-host
  coverage, all top-20 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total
  falls `3,994,447 -> 3,910,564` (`-83,883`, `-2.099990%`). Short-run host and move-class dynamic
  counts both fall by 14,620. The largest reductions are `0x41ef65` and `0x459300` at 6,144,
  `0x47f4d0` at 5,527, `0x433700` at 4,713 and `0x47f52a` at 4,711. The focused shape test passes
  six assertions and two flags-off regression groups pass 257. A fixed-seed 256-iteration flags
  differential retains the same 95 known differences and mismatch hash in baseline and candidate.
  This stage ran no long benchmark, stress test or full suite.
- `4595fc5` keeps a full-width coalesced arithmetic result in its pinned GPR while the flags token
  remains block-local. The proof requires the exact coalesced host publication and rejects later
  physical-register reuse, hard clobbers and every later write to the same guest home. Region edges,
  pending-flags backedges and parked state still normalize to the `x12` ABI when required. The unused
  AF-in-token state was removed. The bounded Orb screen completes in 3.701 seconds with identical
  2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all top-20 PCs, no growing PC and
  exact PPM SHA `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`.
  The comparable total falls `3,910,564 -> 3,894,028` (`-16,536`, `-0.422855%`). Six focused flags
  codegen cases pass; the broad flags filter retains the same two pre-existing 8-bit shift failures
  in baseline and candidate. This stage ran no long benchmark, stress test or full suite.
- `8723387` removes scalar SSE destination seeding when backward SSA analysis proves the upper lane
  dead. Right operands and scalar compares consume only lane zero; left operands and scalar-unary
  merge inputs propagate liveness recursively. Full stores, publications, unknown consumers and
  unaccounted pseudo uses retain the copy. The bounded Orb screen completes in 2.782 seconds with
  identical 2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all top-20 PCs, no
  growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,894,028 -> 3,870,936` (`-23,092`, `-0.593011%`); `0x40248e` loses eight instructions per entry
  and `0x402497` loses seven. Eleven focused liveness, resident-FPR, tie and AFP cases pass 282
  assertions. This stage ran no long benchmark, stress test or full suite.
- `25bc815` recognizes scalar self-XOR only when both SSA inputs are identical or equivalent pure
  `BitCast` / `BitExtract` views of the same snapshot. Exclusive view instructions are discarded,
  and the Arm64 emitter uses one `ANDS` to materialize zero and produce logical NZCV together. The
  bounded Orb screen completes in 3.912 seconds with identical 2,755-PC / 3,621-version sets,
  99.995707% retained-host coverage, all top-30 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,870,936 -> 3,848,132` (`-22,804`, `-0.589108%`); `0x41ef00` accounts for 12,288 and `0x45d000`
  for 4,758. The static screen has 95 shrinking PCs, none growing and `-154` instructions. Twelve
  focused identity and flags cases pass 64 assertions. A direct frontend rewrite to a shared zero
  constant was rejected because it perturbed constant CSE and register allocation, growing the
  static screen by 56 instructions. This stage ran no long benchmark, stress test or full suite.
- `cd31ca8` keeps signed scalar integer conversions in the fixed XMM home after a proven full-vector
  zero and stores resident 32-bit or 64-bit scalars with `STR S` or `STR D`. A conversion whose only
  additional consumer is `StoreMemory` also bypasses its GPR result. Calls, local control flow and
  same-home overwrites reject the direct path. `SCVTF S/D` preserves upper lanes on the tested host,
  so the preceding vector zero remains required. The bounded Orb screen completes in 3.545 seconds
  with identical 2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all top-30 PCs,
  no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,848,132 -> 3,815,145` (`-32,987`, `-0.857221%`). The largest reductions are `0x401e2a` at
  15,240, `0x4023c0` at 9,216, `0x402e30` at 3,072 and `0x412930` at 2,304. Nine local focused
  cases pass 87 assertions; the Orb conversion filter passes 58 assertions across four cases. The
  `0x47f570` POP chain was also rechecked and already uses post-index loads for every stack advance;
  its remaining state stores are not redundant RSP updates. This stage ran no long benchmark,
  stress test or full suite.
- `4a446bf` preserves encodable scalar SSE memory operands through the frontend instead of
  materializing `GetOperand`. The shared helper covers scalar arithmetic and conversion sources,
  MOVSS/MOVSD loads and stores, low-half sources and MOVHPS/MOVLPS; it reuses the existing identity-
  mode width proof and retains the materialized path for biased addressing. The bounded Orb screen
  completes in 2.813 seconds with identical 2,755-PC / 3,621-version sets, 99.995707% retained-host
  coverage, all top-30 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,815,145 -> 3,711,542` (`-103,603`, `-2.715572%`). The two largest blocks, `0x40248e` and
  `0x402497`, each remove all 16 `ADD base,#disp` address instructions and fall `123 -> 107` and
  `122 -> 106`; their weighted reductions are 25,376 and 23,776. `0x4023c0` accounts for another
  18,432 and `0x401e2a` for 15,240. Eight focused identity, biased-address, fault and scalar SSE
  cases pass 926 assertions on both local Clang and Orb GCC builds. This stage ran no long
  benchmark, stress test or full suite.
- `3e1a605` extends zero-register stores through closed `LoadImm(0) -> ZeroExtend32` and
  `ZeroExtend32To64` chains. Every use must be another proven zero-preserving width node or a
  compatible StoreUniform, StoreMemory or SetHostFPR payload; spills and any additional observer
  retain materialization. The bounded Orb screen completes in 2.753 seconds with identical
  2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all top-30 PCs, no growing PC
  and exact PPM SHA `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`.
  The comparable total falls `3,711,542 -> 3,685,233` (`-26,309`, `-0.708843%`); `0x45d000`
  accounts for 26,169 and falls `57 -> 46`. Short host and move-class dynamic counts both fall by
  1,865. Seven focused chain, zero-store and width cases pass 654 assertions locally; the new chain
  and width cases pass 117 unique assertions on Orb. Orb's older direct zero-store matrix still has
  its three GCC-only harness failures from disassembly beyond `CurrentBufferSize` and the forced-x18
  spill setup; the new chain case itself passes. This stage ran no long benchmark, stress test or
  full suite.
- `803900d` removes the obsolete single-sided-operand normalization now that null operand sides and
  immediate-left materialization are native runtime contracts. This restores ordinary immediate
  folding and avoids synthetic `ADD #0` instructions. Scalar `GetHostFPR` producers may also target
  the final pinned GPR directly under the existing publication, alias and observer proofs. The
  bounded Orb screen completes in 2.786 seconds with identical 2,755-PC / 3,621-version sets,
  99.995707% retained-host coverage, all top-30 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,685,233 -> 3,664,930` (`-20,303`, `-0.550929%`); `0x413f60` accounts for 11,520 and falls
  `31 -> 26`. Short host and move-class dynamic counts fall by 439 after the operand normalization
  stage. The GPR publication proof passes 381 assertions on local Clang and Orb GCC builds. This
  stage ran no long benchmark, stress test or full suite.
- `dbb1574` extends the existing branch-only edge proof to `FCmpCondSet`. When an adjacent FP compare
  feeds only a terminal Jcc and both successors overwrite incoming flags before any observation, the
  pass removes `PublishFCmpFlags`, carry normalization and the compact relation carrier. ARM64
  independently reproves a sole condition use, PSTATE-preserving interval and absence of fault or
  helper observers before branching on raw FCMP NZCV. The bounded Orb screen completes in 2.821
  seconds with identical 2,755-PC / 3,621-version sets, 99.995707% retained-host coverage, all
  top-30 PCs, no growing PC and exact PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`. The comparable total falls
  `3,664,930 -> 3,554,284` (`-110,646`, `-3.019048%`). `0x402777` accounts for 18,432;
  `0x402731` and `0x40274f` account for 17,490 each. Two captures have identical static shapes.
  Local Clang and Orb GCC pass 44 flag-elimination, 85 FP-branch and 3,482 COMIS differential
  assertions. This stage ran no long benchmark, stress test or full suite.
- `86aaac4` keeps dead-edge integer EQ/NE branches on the raw `SUBS` zero flag. The frontend's
  two-successor dead-flags proof is retained as transient block metadata while the marker itself is
  still removed from executable IR. ARM64 independently requires one `Sub` producer, one carry
  inversion, one local EQ/NE condition, no other flag producer, no fault/observer and a fully
  PSTATE-preserving interval. It then suppresses PF/AF publication, carry normalization, the
  polarity store and the now-obsolete backedge flags plan. Carry-reading and compound conditions
  retain the existing path. The exact HEAD/candidate Orb `smallpt_wh_x64 4 8 6` A/B keeps the PPM
  SHA `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`, zero spills,
  99.929555% retained-host coverage, 99.947333% entry coverage and all top-30 PCs. Comparable host
  instructions fall `3,552,297 -> 3,523,261` (`-29,036`, `-0.817387%`) with no growing PC;
  `0x47f518` contributes 23,555 and `0x419250` contributes 5,004. Local and Orb integer/FP
  dead-edge tests pass 22 / 72 assertions. The fixed-seed 256-iteration Orb setcc/cmov/jcc
  differential retains the documented 95 existing mismatches. This stage ran no long benchmark,
  stress test or full suite.
- `90fcc3c` keeps PF-only logical publications out of the pending-NZCV state. They no longer emit a
  dead `TST` or leave an empty merge mask that aborts the next ordinary `Select`. The focused
  regression passes three assertions. The corrected no-R12 smallpt shape is effectively neutral
  against `86aaac4` on common PCs (`-2` short dynamic instructions) with the same PPM and zero
  spills. The fix also lets the bounded c-ray path proceed to the next independently exposed proof.
- `438c634` extends the resident scalar-FPR chain proof through a valid in-place `VecFUnary` node.
  Register allocation and backend reproving now agree on the same fixed-home chain and coalesce its
  final publication. This removes the c-ray `0x41bc20` proof abort without weakening the last-use,
  same-home or crossing-write gates. The scalar fixed-home test passes 276 assertions; c-ray
  `128x96`, four-sample output is exact in both A/B arms.
- `c1d85c6` adds R12/x6 to the default level-2 static map while leaving x7-x9 available. The exact
  fixed-baseline/candidate `smallpt_wh_x64 4 8 6` run keeps 2,802 PCs, 3,435 versions, the PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1` and zero spills; short
  dynamic host work falls `411,321 -> 406,727` (`-4,594`, `-1.116889%`). Applying retained entries
  to the prior exact comparison gives `3,523,261 -> 3,457,146` (`-66,115`, `-1.876529%`). The
  bounded c-ray static common total falls `219,512 -> 218,190` (`-1,322`, `-0.602245%`) with exact
  output. CoreMark moves in the other direction: exact dynamic host work grows
  `6,199,760,435 -> 6,215,732,448` (`+15,972,013`, `+0.257623%`) while CRC remains `0x382f`.
  This trade is retained because CoreMark is already substantially ahead of the measured FEX
  reference, while smallpt and c-ray are current deficits. Static-pin, pinned-GPR, page-fault and
  scalar-chain focused gates pass 457 assertions. This stage makes no wall-time claim and ran no
  long benchmark, stress test or full suite.
- `f8cc411` adds R14/x7 while retaining x8/x9 for allocation. Against the R12 baseline, the exact
  `smallpt_wh_x64 4 8 6` arm keeps 2,802 PCs, 3,435 versions, the same PPM and zero spills; short
  dynamic host work falls `406,727 -> 405,255` (`-1,472`, `-0.361914%`) and retained-entry work
  falls `3,299,322 -> 3,279,483` (`-19,839`, `-0.601305%`). The bounded c-ray static common total
  falls `218,130 -> 217,397` (`-733`, `-0.336038%`) with exact output. CoreMark dynamic host work
  falls `6,215,732,448 -> 6,198,767,384` (`-16,965,064`, `-0.272937%`) with CRC `0x382f`; its
  25 dynamic spill operations round to zero percent. Local and Orb static-pin, pinned-GPR and
  page-fault gates each pass 181 assertions, and the Mac short oracle completes in 3.225 seconds.
  Adding R13/x8 for a 15-register map improved all three Orb shapes but repeatedly hung the Mac
  short run at the same 517-PC boundary with an empty output after both six and eight seconds.
  Substituting R15/x8 reproduced the identical Mac boundary and timeout, confirming a register-
  pressure ceiling rather than an R13-specific mapping issue. Both candidates were fully reverted.
  This stage makes no wall-time claim and ran no long benchmark, stress test or full suite.
- `28f459a` extends the dead-edge integer branch proof from EQ/NE to `JB/JAE/JA/JBE`. The frontend
  retains the same two-successor flags-dead certificate; ARM64 independently re-proves the exact
  `Sub`, normalization and sole terminal-condition graph, maps canonical CS/CC back to raw
  subtraction polarity, and recognizes the canonical HI/LS compound graph. It then emits one raw
  `SUBS + B.cond` path without publishing PF/AF, CFINV or a condition boolean. The exact bounded
  smallpt A/B retains 2,802 PCs / 3,435 versions, zero spills and PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`; weighted host work falls
  `405,255 -> 402,318` (`-2,937`, `-0.724729%`) with no growing PC. The bounded CoreMark shape also
  retains all 2,846 PCs / 3,379 versions and falls `6,198,767,526 -> 5,987,684,381`
  (`-211,083,145`, `-3.405244%`); the standard 20,000-iteration run keeps `crcfinal=0x382f`, while
  its score is intentionally not quoted because it now finishes below CoreMark's ten-second
  validity floor. c-ray's bounded image remains exact at SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`; its startup PC overlap
  was only 99.437285%, so no c-ray density delta is claimed. Five local focused cases pass 278
  assertions, the Orb integer case passes 66, and the fixed-seed local differential remains 90/90
  existing mismatches. This stage ran no long benchmark, stress test or full suite.
- `5bdf30e` makes flag liveness condition-specific: EQ/NE consume Z, CS/CC consume C, HI/LS consume
  C/Z, and signed relations consume only their actual N/V/Z subset. This applies consistently to
  block-local elimination and the HIR fixed point. A canonicalizing CFINV is now deleted when the
  intervening condition does not read C and a later in-block C writer covers every path; real carry
  conditions retain it. The exact bounded smallpt A/B keeps 2,802 PCs / 3,435 versions, the same PPM
  and zero spills; weighted host work falls `402,466 -> 399,467` (`-2,999`, `-0.745156%`). One cold
  PC grows by three instructions while all top-30 PCs are non-growing. CoreMark keeps all 2,846 PCs /
  3,379 versions and `crcfinal=0x382f`; its weighted change is effectively neutral at
  `5,987,684,409 -> 5,987,682,134` (`-2,275`, `-0.000038%`). The deterministic c-ray `128x96`,
  four-sample oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. Five focused local cases
  pass 182 assertions, the fixed-seed local differential remains 90/90 existing mismatches and the
  Mac short oracle is exact. This stage ran no long benchmark, stress test or full suite.
- `c540443` collapses a pinned low-32 self-publication from `UBFX + MOV + MOV` to the one required
  architectural W self-write. The backend proof accepts only a same-pin
  `GetHostGPR(U32) -> ZeroExtend32To64 -> SetHostGPR(U64)` chain, preserves the high-half clear,
  and redirects later transparent aliases only when every alias is used as a memory address before
  any replacement write to that pin. CoreMark keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f`;
  weighted host work falls `5,987,682,134 -> 5,885,762,053` (`-101,920,081`, `-1.702163%`). The two
  10.24M-entry hotspots at `0x403630` and `0x403688` each shrink from 25 to 23 host instructions and
  from six to four moves. The bounded smallpt gate keeps 2,802 PCs / 3,435 versions, zero spills and
  the exact PPM while moving `399,467 -> 399,449` (`-18`, `-0.004506%`). The deterministic c-ray
  oracle remains SHA `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`.
  Focused pinned-GPR, integer-width and composite-memory tests pass 60 assertions. This stage ran no
  long benchmark, stress test or full suite.
- `a184ded` lets exact U32 `Or` consumers read fixed W views directly, matching the existing
  And/Xor path without extending its permissive 8/16-bit rule. CoreMark keeps 2,846 PCs / 3,379
  versions and `crcfinal=0x382f`; weighted host work falls `5,885,762,053 -> 5,868,557,589`
  (`-17,204,464`, `-0.292306%`). The `cmp_idx` hotspot at `0x402218` shrinks from 54 to 50 host
  instructions and from 30 to 26 moves. The bounded smallpt gate remains exact and moves
  `399,449 -> 399,443` (`-6`, `-0.001502%`); the deterministic c-ray oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. The pinned-GPR focus
  passes 24 assertions, including a narrow-width exclusion. A first broad Or prototype admitted
  8/16-bit raw W reads, changed CoreMark to 2,847 PCs / 3,380 versions and 688,212,631 entries, and
  was fully reverted before this exact-width implementation. A later narrow-load direct-pin
  publication prototype failed the 12-second CoreMark gate and was fully reverted; keeping the
  load in an ordinary temporary and coalescing only its widening copy reproduced the exact
  `5,868,557,589` incumbent shape, so that zero-effect mechanism was also removed. This stage ran
  no long benchmark, stress test or full suite.
- `7cfc990` fuses the production x86 `MOVSS` publication graph
  `LoadMemory(U32) -> ZeroExtend64 -> SetHostFPR(low) + zero-high` into one fault-exact ARM64
  `LDR S`. The proof requires unique load/extension uses, adjacent low/high stores, an exact U32
  extension, no intervening memory or target-home observer, and the existing resident-value
  lifetime exclusion. The bounded c-ray static common set falls `215,587 -> 214,915` (`-672`,
  `-0.311707%`): 78 PCs shrink, none grow, and `0x402e70` falls `947 -> 853`. Its deterministic
  `128x96`, four-sample output remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. The short smallpt gate
  keeps 2,802 PCs / 3,435 versions, zero spills and the exact PPM while moving
  `399,443 -> 399,441`. Local and Orb scalar-focused gates each pass 21 cases / 1,019 assertions,
  including the real `MOVSS` page-fault path. A first unwrapped U32-load prototype had zero
  production hits; the exact IR audit exposed the missing `ZeroExtend64` bridge. Reusing x27 as a
  spill scratch caused a c-ray static-path SIGSEGV and was fully reverted. This stage ran no long
  benchmark, stress test or full suite.
- `a134c32` keeps a Linux scalar spill definition in the already reserved x18 when the immediately
  adjacent IR instruction directly consumes it. Memory, atomic, helper, x87/SSE4.2 and internal
  control-flow instructions are barriers; other pending writes still commit normally, x18 remains
  unavailable to emitter/VIXL scratch, and block exits retain the existing flush. This removes the
  exact `STR x18, spill; LDR x18, spill` pair without introducing a new register ABI. Against the
  `7cfc990` c-ray screen, the static common set falls `215,079 -> 215,044`; six PCs shrink and none
  grow, while `0x402e70` falls `853 -> 829`. Applying retained formal entries only to the bounded
  22.719356%-covered subset gives `9,560,469,880 -> 9,480,531,174` (`-79,938,706`, `-0.836138%`);
  this is not claimed as a full formal c-ray delta. The deterministic c-ray oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. Smallpt remains exact at
  2,802 PCs / 3,435 versions, zero spills, the same PPM and `399,441` host instructions. CoreMark
  keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving `5,868,557,589 ->
  5,868,557,584`. Local and Orb spill focuses pass four cases with 24,282 / 26,859 assertions; the
  Orb scalar focus passes 22 cases / 1,023 assertions. This stage ran no long benchmark, stress test
  or full suite.
- `e64c635` drops the deferred x18 writeback when the adjacent consumer contains every remaining
  direct and pseudo use of the spilled SSA definition. Multi-use values keep the dirty slot and the
  previous forwarding behavior; the proof changes neither its barrier set nor the scratch ABI.
  Against `a134c32`, the bounded c-ray static common set falls `214,974 -> 214,952`; three PCs
  shrink, none grow, and `0x402e70` falls `829 -> 811`. On the same explicitly partial
  22.719351%-covered retained-entry subset, host work falls `9,480,528,636 -> 9,421,515,964`
  (`-59,012,672`, `-0.622462%`), of which `0x402e70` contributes `-59,009,238`; this remains a
  bounded projection rather than a full formal claim. The deterministic c-ray oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. Smallpt stays exact at
  `399,441`, and CoreMark keeps 2,846 PCs / 3,379 versions plus `crcfinal=0x382f` while moving
  `5,868,557,584 -> 5,868,557,582` and 22 -> 20 dynamic spill operations. The Orb spill focus
  passes four cases / 26,861 assertions. This stage ran no long benchmark, stress test or full suite.
- `0512649` extends the existing resident-FPR memory-store proof through the exact
  `GetHostFPR(U64) -> ZeroExtend32 -> StoreMemory(U32)` chain produced by x86 `MOVSS` stores. The
  read and bridge must each have one use, the store width must remain U32, and any same-home write
  or existing opaque barrier rejects the fusion. ARM64 then emits `STR S` directly instead of
  `lane-to-GPR + W copy + STR W`. The bounded c-ray static common set falls `214,907 -> 214,749`
  (`-158`, `-0.073520%`): 34 PCs shrink and none grow. On the explicitly partial
  22.719349%-covered retained-entry subset, host work falls `9,421,515,234 -> 9,357,193,844`
  (`-64,321,390`, `-0.682707%`); `0x402e70` contributes `-45,896,074`. The deterministic c-ray
  oracle remains SHA `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`.
  Smallpt stays exact at `399,441`, and CoreMark stays at `5,868,557,582` with 2,846 PCs / 3,379
  versions and `crcfinal=0x382f`. Local and Orb scalar focuses pass 22 cases with 1,020 / 1,025
  assertions. This stage ran no long benchmark, stress test or full suite.
- `be4b705` recognizes a low-lane `VecExtract64` whose V128 source is published to a resident FPR
  before its sole U64 memory store. The exact full-width publication must precede the store; opaque
  barriers and any later non-equivalent write to that home reject the plan. The extract is then
  removed and ARM64 stores the resident D register directly. Against `0512649`, the bounded c-ray
  static common set falls `214,155 -> 214,138`: 14 PCs shrink, none grow. On the explicitly partial
  22.719321%-covered retained-entry subset, host work falls `9,357,174,698 -> 9,345,606,001`
  (`-11,568,697`, `-0.123635%`), led by `tform_point`, `tform_vector`, `intersectSphere` and
  `tform_vector_transpose`. The deterministic c-ray oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. Smallpt keeps 2,802 PCs /
  3,435 versions, zero spills and the exact PPM while moving `399,441 -> 399,439`; CoreMark remains
  `5,868,557,582` with `crcfinal=0x382f`. The low64 focus passes five assertions on Mac and Orb;
  the scalar focus passes 1,020 / 1,025 assertions. A broader direct-resident-read variant added no
  c-ray static reduction and was removed. This stage ran no long benchmark, stress test or full
  suite.
- `02a5e77` removes a sole-use simple `GetOperand` address copy when its source is an exact full-64
  pinned `GetHostGPR` mapping. The proof accepts only direct `LoadMemory`/`StoreMemory` consumers,
  rejects any intervening write to the pin, and rejects caller-saved-pin helper boundaries. The
  memory emitter then reads the fixed X register directly; ordinary SSA addresses retain the
  existing RA lifetime tie. Smallpt keeps 2,802 PCs / 3,435 versions, zero spills and the exact PPM
  while moving `399,439 -> 396,873` (`-2,566`, `-0.642401%`); `0x4192f0` contributes `-2,502`.
  CoreMark keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving
  `5,868,557,582 -> 5,849,153,108` (`-19,404,474`, `-0.330652%`). The bounded c-ray static common
  set falls `214,517 -> 214,381`: 101 PCs shrink and none grow. On the explicitly partial
  22.719336%-covered retained-entry subset, host work falls `9,345,619,486 -> 9,334,177,681`
  (`-11,441,805`, `-0.122430%`). The deterministic c-ray oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`. Local and Orb pinned/page-
  fault focuses pass 52 + 21 assertions. A broader live pinned-publication prototype reduced the
  covered c-ray shape but made even `16x12/s1` time out; it was fully removed after confirming the
  committed baseline completes in one second. This stage ran no long benchmark, stress test or
  full suite.
- `42abcdb` fuses an ordinary logical producer's adjacent `ClearFlags(CVAF)` and
  `SaveFlags(NZ|PF)` publication. It publishes the parity token before borrowing scratch, then
  replaces the separate four-bit clear plus two-bit NZ merge with one six-bit extract/insert. The
  proof rejects region-internal, backedge, dead-edge and branch-only flag plans, so their existing
  lazy cross-edge contracts remain untouched. Smallpt keeps all 2,802 PCs / 3,435 versions, zero
  spills and the exact PPM while moving `396,873 -> 394,264` (`-2,609`, `-0.657389%`); every
  changed equal-entry PC shrinks. CoreMark keeps all 2,846 PCs / 3,379 versions and
  `crcfinal=0x382f` while moving `5,849,153,108 -> 5,775,449,473` (`-73,703,635`,
  `-1.260074%`); its equal-entry weighted comparison is `5,910,113,189 -> 5,836,409,554`
  (`-1.247077%`). The bounded c-ray static common set moves `214,621 -> 212,641` across
  99.888300% host coverage, and the explicitly partial 22.719351%-covered retained-entry subset
  moves `9,334,183,694 -> 9,282,105,342` (`-52,078,352`, `-0.557932%`). The deterministic c-ray
  oracle remains SHA `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`.
  The new focused case passes three assertions on Mac and Orb. The broader flags/page-fault focus
  retains its two incumbent failures: the stale default-OFF assertion and the immediate-shift case
  reproduced with the new matcher disabled. This stage ran no long benchmark, stress test or full
  suite.
- `86b104f` omits an actually emitted pinned-GPR publication when the next access to that home is a
  complete overwrite. Unlike the rejected IR-deletion prototype, it leaves IR use counts, register
  allocation and unit formation untouched. The proof rejects a target read, partial rewrite,
  fault/helper, uniform-address barrier, `SetLocation` and local control; a later coalesced rewrite
  must also have a publication root after the omitted store. Smallpt keeps all 2,802 PCs / 3,435
  versions, zero spills and the exact PPM while moving `394,264 -> 391,408` (`-2,856`,
  `-0.724388%`); the equal-entry comparison is `394,282 -> 391,426` (`-0.724355%`), with every
  changed PC smaller and `0x41928c` contributing `-2,502`. CoreMark keeps all 2,846 PCs / 3,379
  versions, its existing 20 dynamic spills and `crcfinal=0x382f` while moving
  `5,775,449,473 -> 5,697,687,040` (`-77,762,433`, `-1.346431%`). The bounded c-ray static candidate
  completes in 0.936 seconds and the deterministic oracle remains SHA
  `89ccd2e15dba67378197d05524a6223795f8b8ab2d11f4d40deaef4af9f35c6e`; no c-ray density delta is
  claimed because the build-only planner-disabled control was not a valid runtime baseline. Local
  and Orb pinned/page-fault focuses pass 75 assertions across 13 cases. This stage ran no long
  benchmark, stress test or full suite.
- The Orb phase-c mirror must be synchronized with the tracked checkout using the checksum command
  below before every A/B. A partial source copy left the x86 frontend's AFP detection stale while
  rebuilding newer runtime objects; that mixed binary incorrectly restored the legacy scalar-SSE
  lane moves. The exact tracked HEAD with `SVM_X86_PIN_EXT=3` gives the calibrated smallpt baseline
  used for the next stage: 2,802 PCs / 3,435 versions, `385,558` dynamic host instructions, 288
  dynamic spill operations and the canonical PPM SHA
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`.
- `3e47a16` lets a U32 producer retain its flags token in a fixed guest
  GPR through the exact `ZeroExtend32To64 -> coalesced full SetHostGPR` publication. The wrapper and
  producer must share the physical register; any later same-home write, fixed clobber or unrelated
  overlapping SSA definition rejects the proof. This removes the otherwise required
  `mov w12, wPin` without changing the fault or exit publication ABI. Against the exact tracked
  HEAD, smallpt keeps 2,802 PCs / 3,435 versions and the canonical PPM while moving `385,558 ->
  385,479`; its 100%-covered equal-entry comparison is `386,584 -> 386,505` (`-79`,
  `-0.020435%`). CoreMark keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving
  `5,823,593,568 -> 5,813,471,244`; the 100%-covered equal-entry comparison is
  `5,884,553,649 -> 5,874,431,325` (`-10,122,324`, `-0.172015%`). Local and Orb flags focuses pass
  41 assertions across seven cases; pinned/page-fault focuses pass 75 assertions across 13 cases.
  A broader fixed-home publication remap grew smallpt `+4.736618%`; a generic x12 result-placement
  pass did not improve the dominant regions. Both implementations and every diagnostic path were
  removed. This stage ran no long benchmark, stress test or full suite.
- `942a63d` generalizes the pinned low-32 self-write planner into a cross-pin copy planner. An exact
  `GetHostGPR(U32) -> ZeroExtend32To64 -> SetHostGPR(U64)` chain now reads directly from the source
  home and emits one `mov wTarget, wSource`; any later alias must be a post-publication `BitCast`
  used only as a memory address. Source or target rewrites before publication, target rewrites while
  an alias is live, caller-saved helper clobbers, faults and observers reject the plan. The focused
  module was renamed to `translator_pinned_gpr_copy.cpp`; no compatibility path or diagnostic gate
  remains. Against `3e47a16`, bounded smallpt keeps 2,802 PCs / 3,435 versions, 288 dynamic spills
  and the canonical PPM while moving `385,479 -> 385,025`; the 100%-covered weighted comparison is
  `386,505 -> 386,051` (`-454`, `-0.117463%`) with no growing PC. CoreMark keeps 2,846 PCs / 3,379
  versions and `crcfinal=0x382f` while moving `5,813,471,244 -> 5,799,708,766`; the 100%-covered
  weighted comparison is `5,874,431,325 -> 5,860,668,847` (`-13,762,478`, `-0.234278%`). Local and
  Orb pinned/page-fault focuses pass 79 assertions across 14 cases. The rejected generic RA carrier
  was removed after its short correctness and code-shape screen; this stage ran no stress test or
  full suite.
- `14e48c1` extends the same copy transaction through an optional single-use `ZeroExtend32` fed by
  a pinned U8/U16 read. The inner and outer extensions emit nothing; the publication emits exactly
  one `UXTB` or `UXTH` from the source home to the target home. The existing source/target rewrite,
  alias, fault and caller-saved-helper proofs are unchanged and the emitter replays the source
  width. Against `942a63d`, bounded smallpt keeps 2,802 PCs / 3,435 versions, 288 dynamic spills and
  the canonical PPM while moving `385,025 -> 385,023`; the 100%-covered weighted comparison is
  `386,051 -> 386,049` (`-2`, `-0.000518%`). CoreMark keeps 2,846 PCs / 3,379 versions and
  `crcfinal=0x382f` while moving `5,799,708,766 -> 5,793,328,760`; the 100%-covered weighted
  comparison is `5,860,668,847 -> 5,854,288,841` (`-6,380,006`, `-0.108861%`) with every changed
  PC smaller. Local and Orb pinned/page-fault focuses pass 83 assertions across 15 cases. The
  zero-reach low-32 address-alias extension was removed before this stage; no stress test or full
  suite ran.
- `4cc2c1e` extends the fixed-home publication proof to an exact
  `LoadMemory(U8/U16/U32) -> optional ZeroExtend32 -> ZeroExtend32To64 -> SetHostGPR(U64)` chain.
  The faulting load writes the final pinned W home directly, while the redundant extensions and
  publication emit nothing. The load remains the only instruction that can change the target
  before publication, and the existing alias, observer, target-rewrite and helper-clobber checks
  remain fail-closed. The plan represents a memory producer by the absence of a source GPR rather
  than a sentinel register or compatibility path. Against `14e48c1` with
  `SVM_X86_PIN_EXT=3`, bounded smallpt keeps 2,802 PCs / 3,435 versions, 288 dynamic spills and the
  canonical PPM while moving `385,023 -> 384,618`; the 100%-covered weighted comparison is
  `386,049 -> 385,644` (`-405`, `-0.104909%`). CoreMark keeps 2,846 PCs / 3,379 versions and
  `crcfinal=0x382f` while moving `5,793,328,760 -> 5,788,086,168`; the 100%-covered weighted
  comparison is `5,854,288,841 -> 5,849,046,249` (`-5,242,592`, `-0.089551%`). Local and Orb
  pinned/page-fault focuses pass 69 assertions across 15 cases. A signed narrow-copy extension was
  byte-identical on bounded smallpt and CoreMark and was removed. This stage ran no stress test or
  full suite.
- `e2bb2c7` removes the final narrow-result `LSR` from U8/U16 Add, Sub and Neg when their pseudo
  flags are branch-only and `GetUses()` proves that the arithmetic value has no ordinary consumer.
  The aligned `ADDS` or `SUBS` has already produced the exact requested NZCV, and `LSR` does not
  change those flags; any SetHost, memory or other value use keeps the existing truncation. No IR
  rewrite, feature switch or fallback path was added. Against `4cc2c1e` with
  `SVM_X86_PIN_EXT=3`, bounded smallpt keeps 2,802 PCs / 3,435 versions, 288 dynamic spills and the
  canonical PPM while moving `384,618 -> 382,069`; the 100%-covered weighted comparison is
  `385,644 -> 383,095` (`-2,549`, `-0.660972%`) with every changed PC smaller. CoreMark keeps
  2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving
  `5,788,086,168 -> 5,750,483,860`; the 100%-covered weighted comparison is
  `5,849,046,249 -> 5,811,443,941` (`-37,602,308`, `-0.642879%`) with every changed PC smaller.
  Local and Orb flags focuses pass 122 assertions across four cases. Direct U64-load publication
  and signed-load publication prototypes had zero shared-shape delta on bounded full-pin smallpt
  and CoreMark and were removed completely. This stage ran no stress test or full suite.
- `5922091` specializes the existing dead-edge integer branch proof when its producer is a dead
  U8/U16 `Sub`, its right operand is a single-use encodable `LoadImm`, and the branch needs exactly
  ZF. The block plan suppresses the immediate materialization before emission, then emits
  `SUB wResult, wLeft, #imm; TST wResult, #width-mask`; carry, signed and observed-result shapes
  retain the existing aligned `SUBS` path. The emitter replays the complete plan and direct pinned
  source proof. Against `e2bb2c7` with `SVM_X86_PIN_EXT=3`, bounded smallpt keeps 2,802 PCs / 3,435
  versions, 288 dynamic spills and the canonical PPM while moving `382,069 -> 379,546`; the
  100%-covered weighted comparison is `383,095 -> 380,572` (`-2,523`, `-0.658583%`) with every
  changed PC smaller. CoreMark keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving
  `5,750,483,860 -> 5,750,481,642`; its 100%-covered weighted comparison is
  `5,811,443,941 -> 5,811,441,723` (`-2,218`, `-0.000038%`) with every changed PC smaller. Local
  and Orb flags focuses pass 144 assertions across four cases. An earlier emitter-only ZF
  prototype had zero net code-shape change because it could not suppress the pre-emitted
  `LoadImm`; it was removed before this block plan. This stage ran no stress test or full suite.
- `3f079f9` generalizes the same block plan across every condition currently accepted by the
  dead-edge proof: exact ZF, CF, or ZF+CF. The result-free U8/U16 compare now emits
  `UXTB/UXTH wResult, wLeft; CMP wResult, #imm`; this preserves the host-C inverse-borrow
  convention used by the existing raw EQ/NE/CC/CS/HI/LS branches and still suppresses the
  single-use `LoadImm`. Other flag sets and unencodable immediates remain on the aligned `SUBS`
  path. Against `5922091` with `SVM_X86_PIN_EXT=3`, bounded smallpt keeps 2,802 PCs / 3,435
  versions, 288 dynamic spills and the canonical PPM while moving `379,546 -> 379,525`; the
  100%-covered weighted comparison is `380,572 -> 380,551` (`-21`, `-0.005518%`) with no growing
  PC. CoreMark keeps 2,846 PCs / 3,379 versions and `crcfinal=0x382f` while moving
  `5,750,481,642 -> 5,724,721,561`; its 100%-covered weighted comparison is
  `5,811,441,723 -> 5,785,681,642` (`-25,760,081`, `-0.443265%`) with every changed PC smaller.
  Local and Orb flags focuses pass 152 assertions across four cases. This stage ran no stress test
  or full suite.
- `a627d94` removes an adjacent single-use low U8/U16 extraction before narrow Add/Sub/Neg flag
  alignment. The alignment `LSL` consumes the original source because it already discards every
  high bit; source/result aliasing is reproved against the original SSA, and shared or non-adjacent
  extracts retain materialization. Against `3f079f9`, full-pin smallpt keeps all 2,802 PCs / 3,435
  versions, 288 dynamic spills and the canonical PPM while moving `379,525 -> 376,993`; the
  100%-covered weighted comparison is `380,551 -> 378,019` (`-2,532`, `-0.665351%`) with no growing
  PC. Applying the retained CoreMark 20k entries to the complete short shape gives `-6,482,313`
  (`-0.112041%`) at 99.999998565% host coverage. Mac and Orb focused checks pass 117 assertions
  across six cases.
- `f42b7cc` extends integer width canonicalization through an exact 32-to-64 `SignExtend`. A low-32
  extract of that value is replaced with the original 32-bit SSA, after which the existing DCE
  removes both extraction copies. Against `a627d94`, smallpt remains exact and moves `376,993 ->
  374,499`; weighted host moves `378,019 -> 375,525` (`-2,494`, `-0.659755%`) entirely at
  `0x41a818`, where `SXTW; LSR #0; LSR #0; ANDS` becomes `SXTW; ANDS`. CoreMark changes only by
  `-2,194` on the retained-entry join. The width focus passes 68 assertions on Mac and Orb.
- `9fb39ef` makes the flags result token match its sole deferred responsibility: preserving a low
  byte for live parity. NZCV-only and AF-only producers no longer capture or publish that token;
  narrow arithmetic retains its final `LSR` only for an ordinary value use, live PF, or live AF.
  Against `f42b7cc`, smallpt moves `374,499 -> 368,937`; weighted host moves `375,525 -> 369,963`
  (`-5,562`, `-1.481126%`) with all 2,802 PCs / 3,435 versions and no growing PC. The retained
  CoreMark entry join gives `-27,184,518` (`-0.470386%`). The exact promoted 20k candidate, including
  the preceding two stages, is `5,724,721,561 -> 5,691,052,536` with CRC `0x382f`. FLAGS `0/1` x
  function/block/interpreter returns the same checksum and output SHA in all six cells. Focused
  parity, branch and width checks pass 185 assertions on both hosts; the broad `*flags*` filter
  retains only the two documented incumbent failures.
- `b5936ac` closes the largest remaining CoreMark mechanism. Function branch-only analysis may
  now cross one adjacent `InvertCarry` only when a single U8/U16 `Sub` compares against a direct
  memory RHS, both successors overwrite flags before any read, and the terminal condition does
  not read carry. The irrelevant normalization is deleted and the producer becomes a raw
  BranchOnlyFlags source. The `cmp r12w,[rdx+2] + jne` loop stops publishing full flags and stops
  forming a second hot unit version. Exact CoreMark 20k moves `5,691,052,536 -> 4,206,692,536`
  (`-1,484,360,000`, `-26.082346%`), versions `3,379 -> 3,378`, and keeps CRC `0x382f`. Current
  smallpt is byte-for-byte unchanged at `368,937`, with all 2,802 PCs / 3,435 versions, 288 spills
  and the canonical PPM. The acceptance proof has a dedicated HIR function test; branch-only,
  dead-edge and narrow-result focuses pass 120 assertions on Mac and Orb, and the FLAGS six-grid
  remains checksum-identical. A broad `SVM_FLAG_FULL_ELIM=1` retry grew smallpt by 2.376% and made
  short CoreMark abort; a later memory-left/immediate-right expansion saved only 0.000022% on the
  common CoreMark shape. Both prototypes were removed completely.
- `162a4d8` extends the fixed-home publication transaction to a spilled U32 `Add` whose complete
  value graph is one `ZeroExtend32To64` publication plus post-publication U32 Add/Sub uses or exact
  low-32 aliases. The producer writes the pinned W home directly, its wrapper and publication emit
  nothing, and later proven ALU uses read that home instead of the spill slot. The proof requires a
  genuinely spilled producer with no pseudo flags, closes every producer/wrapper/alias use, rejects
  target rewrites and caller-saved helper clobbers through the final use, and is independently
  replayed before emission. In CoreMark's two matrix loops this replaces the x18 add, spill store,
  spill reload and pinned move, plus later spill reloads, with one `ADD wPin`. Exact 20k host work moves
  `4,206,692,536 -> 4,103,652,536` (`-103,040,000`, `-2.449430%`); dynamic spill operations move
  `388,800,030 -> 38,880,030` (`-90%`) and CRC remains `0x382f`. The 100%-covered 2k shape join is
  `426,914,119 -> 416,610,119` (`-2.413600%`) with six shrinking PCs and no growth. Smallpt is
  exactly unchanged at 2,802 PCs / 3,435 versions, `368,937` raw host instructions, 288 spills and
  the canonical PPM. A forced-MEM publication test verifies `ADD wPinned` with no x18 publication
  move; pinned/read/spill focuses pass on Mac and Orb, and the FLAGS six-grid remains checksum-
  identical. The temporary rejection diagnostic reused `SVM_DUMP_IR` and was removed before
  delivery; no new switch or fallback remains.
- `e959074` composes the adjacent low-extract input plan with the dead narrow immediate branch
  plan instead of treating them as mutually exclusive. The specialized U8/U16 compare now resolves
  the original source before choosing a pinned W view, and the redundant `BitExtract` emits
  nothing. CoreMark's `sub edx,0x30; cmp dl,9; jbe` unit at `0x4034b0` shrinks by one instruction.
  Exact 20k host work moves `4,103,652,536 -> 4,077,892,443` (`-25,760,093`, `-0.627736%`) with
  CRC `0x382f`; the 100%-covered 2k join is `416,610,119 -> 414,034,026` (`-0.618346%`) with no
  growing PC. Smallpt moves `368,937 -> 368,902`, and its weighted join moves `369,963 -> 369,928`
  (`-35`, `-0.009460%`) with all shape, spill and PPM gates exact. A dedicated branch/extract test
  and the existing dead-edge matrix pass on Mac and Orb.
- `f8cc6ed` extends the same fixed-home publication transaction through an exact low U8/U16/U32
  alias used only by a branch-only `Or(alias, 0)` zero test. The load still writes the pinned W
  home directly, and the logical-flags emitter reads that home instead of materializing an
  intervening W copy. The existing complete-use, publication-order, target-rewrite and helper-
  clobber proofs remain fail-closed. CoreMark's byte-load zero-test unit at `0x403320` shrinks by
  one instruction. Exact 20k host work moves `4,077,892,443 -> 4,057,412,434` (`-20,480,009`,
  `-0.502220%`) with 2,846 PCs / 3,378 versions, 38,880,030 dynamic spills and CRC `0x382f`; the
  100%-covered 2k join is `414,034,026 -> 411,986,017` (`-0.494648%`) with no growing PC.
  Smallpt moves `368,902 -> 368,884`, and its weighted join moves `369,928 -> 369,910` (`-18`,
  `-0.004866%`) with all 2,802 PCs / 3,435 versions, 288 spills and the canonical PPM unchanged.
  Pinned-read, spilled-add and dead-narrow-branch focuses pass on Mac and Orb; no diagnostic or
  compatibility path was added.
- `ea82571` keeps one structured effective address across the load and store halves of a plain
  integer Add/Sub memory RMW when the address is `base + index * access_size + displacement` in
  identity mode. The single instruction therefore emits the displacement adjustment once while
  preserving the existing composite register-offset load/store encoding. LOCK/atomic operations,
  segment overrides, unindexed addresses, mismatched scales and biased memory retain their exact
  paths. CoreMark's mirrored units at `0x403630` and `0x403688` each shrink from 23 to 22 host
  instructions; no other PC changes. An exact same-build 20k A/B moves raw host work
  `4,035,105,879 -> 4,014,625,879` (`-20,480,000`, `-0.507546%`) and the 100%-covered weighted
  comparison moves `4,096,065,960 -> 4,075,585,960` (`-0.499992%`) with CRC `0x382f`. The 2k
  screen is `403,663,362 -> 401,615,362`; its weighted comparison is
  `409,759,443 -> 407,711,443` (`-0.499805%`). Smallpt remains exact at 2,802 PCs / 3,435 versions,
  `374,896` raw and `374,914` weighted host instructions with the canonical PPM. Mac and Orb
  effective-address focuses pass 29 assertions across two cases. A broad all-RMW prototype was
  rejected after it reshaped hot function allocation and introduced growing PCs; it was fully
  removed before delivery, and no diagnostic or feature switch remains.
- `b734438` extends the bounded successor-prefix proof through one static direct call. The caller
  prefix and callee entry may contain only audited flag-transparent instructions before a complete
  overwrite; reads of incoming flags, partial writers, indirect or nested calls, other control
  flow and unknown instructions reject the proof. Logical TEST/AND/OR/XOR metadata now counts its
  architectural CF/OF clears as writes, and CET ENDBR is treated as the existing semantic Nop.
  Every accepted callee prefix is stored as a guest-code dependency and registered with SMC under
  the owning caller translation. A callee-entry write therefore invalidates the caller as well;
  disk JIT cache mode conservatively rejects this proof until that dependency is serialized.
  CoreMark's mirrored `cmp byte [ptr],0; jne call` loops stop publishing full flags and collapse
  the associated hot versions. Exact 20k raw host work moves
  `4,014,625,879 -> 3,735,236,874` (`-279,389,005`, `-6.959279%`), units/versions move
  `2,846/3,378 -> 2,820/2,915`, and CRC remains `0x382f`. The 2k screen moves
  `401,615,362 -> 373,668,357` (`-27,947,005`, `-6.958649%`). Because unit formation changes,
  neither comparison is presented as a strict common-PC join. Smallpt keeps the canonical PPM
  while moving raw host work `374,896 -> 361,960` (`-12,936`, `-3.450557%`) and
  units/versions `2,802/3,435 -> 2,792/3,052`. Mac and Orb dead-edge focuses pass 174 assertions
  across three cases; direct-call dependency and non-stress SMC focuses also pass on both hosts.
  FLAGS `0/1` x function/block/interpreter returns rc 101, checksum `9f52b7d59285dbe5` and one
  identical output SHA in all six cells. No temporary diagnostic, new environment switch, stress
  run or full suite remains in this stage.
- `38cea6e` carries the architectural zero-extension guarantee of an exact U8/U16 `LoadMemory`
  into the existing dead narrow immediate branch lowering. `LDRB/LDRH` already defines a clean W
  value, so its terminal compare now reads that register directly instead of repeating
  `UXTB/UXTH`; non-load inputs retain the explicit truncation. CoreMark's `0x403630` and
  `0x403688` units each shrink by one instruction with no unit/version change. Exact 20k raw and
  weighted host work both fall by `20,480,048`: raw
  `3,735,236,874 -> 3,714,756,826` and weighted
  `3,735,236,889 -> 3,714,756,841` (`-0.548293%`), with 100% coverage, no growing PC and CRC
  `0x382f`. The 2k screen moves `373,668,357 -> 371,620,309` (`-0.548092%`). Smallpt keeps all
  2,792 PCs / 3,052 versions and the canonical PPM while moving raw `361,960 -> 361,711` and
  weighted `361,978 -> 361,729` (`-0.068792%`). Narrow-immediate and dead-edge integer focuses
  pass 105 assertions across three cases on Mac and Orb; no new switch or fallback was added.
- `d5e45c7` extends the pinned memory-address proof through an already coalesced full-width
  `LoadMemory(U64) -> SetHostGPR(pin)` publication and one exact post-publication `BitCast` alias.
  The producer use graph must close over that publication and alias, the publication must already
  be allocator-proven on the target fixed home, the address must have one memory use, and target
  rewrites or caller-saved helper barriers before that use reject the plan. The later GetOperand
  therefore names the pinned home directly instead of emitting `mov xTmp,xPin`; the original
  faulting load and publication ordering are unchanged. Exact 20k CoreMark raw host work moves
  `3,714,756,826 -> 3,666,996,824`, and the 100%-covered weighted comparison moves
  `3,714,756,841 -> 3,666,996,839` (`-47,760,002`, `-1.285683%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `371,620,309 -> 366,844,307` (`-4,776,002`, `-1.285183%`).
  Units/versions remain 2,820 / 2,915. Smallpt remains exact at 2,792 PCs / 3,052 versions and the
  canonical PPM while moving raw `361,711 -> 361,709` and weighted `361,729 -> 361,727`.
  Existing pinned-GPR publication/address coverage passes 30 assertions across ten cases on Mac
  and Orb; no temporary diagnostic, feature switch or compatibility path remains.
- `7194669` extends fixed-home publication through an allocator-coalesced signed U8/U16 memory
  load. The load emits `LDRSB/LDRSH` directly into the published home and suppresses the separate
  narrow sign extension, outer low-32 publication wrapper and `SetHostGPR`. A post-publication
  low-32 alias may remain on that home only when every use is an audited `SignExtend` or left-hand
  U32 `Mul`; the proof closes the complete use graph and rejects target rewrites or caller-saved
  helper clobbers before the last use. Exact 20k CoreMark raw host work moves
  `3,666,996,824 -> 3,625,676,818`, and the 100%-covered weighted comparison moves
  `3,666,996,839 -> 3,625,676,833` (`-41,320,006`, `-1.126808%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `366,844,307 -> 362,712,301`. Units/versions remain
  2,820 / 2,915. Smallpt keeps all 2,792 PCs / 3,052 versions and the canonical PPM while moving
  raw `361,709 -> 361,697` and weighted `361,727 -> 361,715`. Orb pinned-GPR coverage passes
  73 assertions across 17 cases, and the signed-load multi-consumer case passes six assertions.
  The promoted 20k run completes in 3.476 seconds; no stress run, full suite, diagnostic or new
  environment switch remains.
- `c87a2a1` lets the existing exact narrow-load publication proof keep a low-width zero-test alias
  on the pinned home when the result has no SSA use and its only semantic output is an ordinary
  `SaveFlags`. This uses the same audited `Or(value, 0)` emitter path as `BranchOnlyFlags`; value
  consumers, nonzero operands and other operations remain rejected. Exact 20k CoreMark raw host
  work moves `3,625,676,818 -> 3,586,796,806`, and the 100%-covered weighted comparison moves
  `3,625,676,833 -> 3,586,796,821` (`-38,880,012`, `-1.072352%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `362,712,301 -> 358,824,289`. Units/versions remain
  2,820 / 2,915. `0x4033bb` alone shrinks from ten to nine host instructions for 19.36M weighted
  entries, and five other executed CoreMark PCs shrink by one. Smallpt keeps all 2,792 PCs / 3,052
  versions and the canonical PPM while moving raw `361,697 -> 361,682` and weighted
  `361,715 -> 361,700`. Orb pinned-load coverage passes 11 assertions across three cases and the
  broader pinned-GPR group passes 30 assertions across ten cases. The promoted 20k run completes
  in 3.242 seconds; no stress run, full suite, diagnostic or new environment switch remains.
- `9d299aa` lets an exact U8/U16 `LoadMemory` write a sole adjacent `SignExtend` or
  `ZeroExtend32` result register directly even when linear scan assigned different registers.
  Shared-register and pinned-publication paths remain unchanged; spilled consumers reject the
  direct path, and pre/post-index loads reject it when the destination overlaps the writeback base.
  Exact 20k CoreMark raw host work moves `3,586,796,806 -> 3,552,714,571`, and the 100%-covered
  weighted comparison moves `3,586,796,821 -> 3,552,714,586` (`-34,082,235`, `-0.950214%`) with
  no growing PC and CRC `0x382f`. The 2k screen moves `358,824,289 -> 355,415,880`.
  Units/versions remain 2,820 / 2,915. `0x402218` shrinks from 47 to 45 host instructions; the six
  executed matrix blocks each shrink by one. Smallpt keeps all 2,792 PCs / 3,052 versions, raw
  `361,682` and weighted `361,700` host work, plus the canonical PPM. The forced non-shared
  load/consumer case passes four assertions; pinned-load and address-liveness groups pass 11 and
  six assertions. The promoted 20k run completes in 3.112 seconds; no stress run, full suite,
  diagnostic or new environment switch remains.
- `53333fc` folds an exact adjacent `BitExtract(0, 8/16) -> ZeroExtend32` pair into one
  `UXTB/UXTH` at the extension destination. The extract must have one ordinary and raw use; pinned,
  flags-input, width-chain, low-copy and scalar-identity owners reject the plan. Exact 20k CoreMark
  raw host work moves `3,552,714,571 -> 3,529,052,310`, and the 100%-covered weighted comparison
  moves `3,552,714,586 -> 3,529,052,325` (`-23,662,261`, `-0.666033%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `355,415,880 -> 353,049,445`. Units/versions remain
  2,820 / 2,915. `0x402218` shrinks from 45 to 43 host instructions; both main CRC loops and seven
  related blocks each shrink by one. Smallpt keeps all 2,792 PCs / 3,052 versions and the canonical
  PPM while moving raw `361,682 -> 361,466` and weighted `361,700 -> 361,484`. The two focused
  narrow-extension cases pass four and three assertions, and related narrow-flags coverage passes
  six assertions across two cases. The promoted 20k run completes in 3.236 seconds; no stress run,
  full suite, diagnostic or new environment switch remains.
- `a548ece` removes that final `UXTB/UXTH` when the extension destination already equals its source
  register and a bounded proof traces the value through only `BitCast`, `ZeroExtend32` and
  `ZeroExtend32To64` nodes to a same-width-or-narrower `LoadMemory` or `LoadUniform`. Arithmetic and
  fixed-home reads remain excluded. Exact 20k CoreMark raw host work moves
  `3,529,052,310 -> 3,520,730,080`, and the 100%-covered weighted comparison moves
  `3,529,052,325 -> 3,520,730,095` (`-8,322,230`, `-0.235821%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `353,049,445 -> 352,217,041`; `0x402218` shrinks from 43 to 41
  host instructions. Units/versions remain 2,820 / 2,915. Smallpt remains exact at 2,792 PCs /
  3,052 versions, raw `361,466`, weighted `361,484` and the canonical PPM. The clean-load
  self-extension case passes four assertions. The promoted 20k run completes in 3.183 seconds; no
  stress run, full suite, diagnostic or new environment switch remains.
- `4661866` keeps an exact `BitExtract(0, 8/16)` store payload on the fixed GPR home of an existing
  allocator-coalesced `SetHostGPR` publication. The extract must have one exact `StoreMemory` use,
  the publication must precede it and independently reprove, and a same-home rewrite or
  caller-saved helper clobber before the store rejects the plan. `STRB/STRH` therefore reads the
  published W register directly without materializing `UXTB/UXTH`. Exact 20k CoreMark raw host work
  moves `3,520,730,080 -> 3,512,247,843`, and the 100%-covered weighted comparison moves
  `3,520,730,095 -> 3,512,247,858` (`-8,482,237`, `-0.240923%`) with no growing PC and CRC
  `0x382f`. The 2k screen moves `352,217,041 -> 351,368,630`; `0x402218` shrinks from 41 to 39
  host instructions, and two related hot blocks each shrink by one. Units/versions remain
  2,820 / 2,915. Smallpt keeps all 2,792 PCs / 3,052 versions and the canonical PPM while moving raw
  `361,466 -> 361,457` and weighted `361,484 -> 361,475`. Narrow and pinned-load coverage passes
  270 and 11 assertions on Mac and Orb. The promoted 20k and smallpt runs complete in 3.264 and
  2.431 seconds; no stress run, full suite, diagnostic or new environment switch remains.
- `8c8b26d` removes the per-site cold `RET` from production inline-L1 indirect exits. `TST` retains
  the Signal bit result while the cache address and entry load are formed; `CCMP` then accepts the
  cache tag only when no Signal is pending, so `CSEL + BR` selects either the hit entry or the
  unchanged trampoline continuation. The hot hit remains seven instructions, Signal still returns
  through the trampoline, and low-bit SMC requests retain their previous behavior. Exact 20k
  CoreMark raw host work moves `3,512,247,843 -> 3,480,422,816`, and the 100%-covered weighted
  comparison moves `3,512,247,858 -> 3,480,422,901` (`-31,824,957`, `-0.906114%`) with no growing
  PC and CRC `0x382f`. The 2k screen moves weighted `351,368,645 -> 348,185,623`; `0x403383`
  shrinks from 11 to 10 host instructions. Units/versions remain 2,820 / 2,915. Smallpt keeps all
  2,792 PCs / 3,052 versions and the canonical PPM while moving raw `361,457 -> 359,291` and
  weighted `361,475 -> 359,309` (`-0.599212%`). Static shape and pending-Signal coverage pass 28
  and six assertions on Mac and Orb. The promoted 20k and smallpt runs complete in 3.289 and 2.369
  seconds; no stress run, full suite, diagnostic or new environment switch remains.
- `673fbe1` extends the existing pinned low-alias proof from U32 arithmetic to exact same-width
  U8/U16 `ADD/SUB`. A zero-extended narrow load can now publish directly into its fixed GPR home,
  and a later low alias consumes that W register while the proof still rejects intervening target
  writes, helper clobbers and unrecognized consumers. CoreMark's `movzbl (%rdx), %edx; cmp %dx,
  %r14w` block at `0x402814` drops both the publication `MOV` and alias `UXTH`, shrinking 15 to 13
  host instructions. The exact 20k raw total moves `3,480,422,816 -> 3,476,032,828`; the matched
  weighted comparison covers `99.994908%` of entries and moves `3,480,317,008 -> 3,476,036,977`
  (`-4,280,031`, `-0.122978%`) with no growing common PC and CRC `0x382f`. The candidate compile
  set is deterministic at 2,761 PCs / 2,864 versions; its exact weighted total is `3,476,032,843`.
  Smallpt retains the canonical PPM. Static fixed-home coverage passes six assertions and a real
  JIT execution test checks 256 narrow compare inputs plus ZF/CF/SF/OF/PF-derived conditions on Mac
  and Orb. A bounded fixed-seed ALU A/B produced the identical set of 73 pre-existing mismatches in
  both binaries, so it was used only as a candidate-delta audit rather than claimed as a passing
  suite. The promoted 20k and smallpt runs complete in 3.055 and 2.361 seconds; no stress run,
  diagnostic or new environment switch remains.
- `4655e75` lets an adjacent same-width U8/U16 flags-producing `ADD/SUB` read fixed x6-x9 directly.
  The proof requires the pinned read and arithmetic to be adjacent, the producer to request host
  NZCV, and the value to have one exact consumer; ordinary narrow arithmetic and snapshot-breaking
  writes retain the materialized path. CoreMark's two dominant U16 comparisons at `0x402814` and
  `0x402668` each lose `UBFX + UXTH`, shrinking 13 to 11 and 10 to eight host instructions. Exact
  20k raw host work moves `3,476,032,828 -> 3,467,792,816`, and the 100%-covered weighted comparison
  moves `3,476,032,843 -> 3,467,792,831` (`-8,240,012`, `-0.237052%`) with no growing PC and CRC
  `0x382f`. Units/versions remain 2,761 / 2,864. Smallpt keeps all 2,733 PCs / 2,985 versions and
  the canonical PPM while moving raw `227,552 -> 227,538` and weighted `227,570 -> 227,556`.
  New fixed-home static/runtime coverage passes four and two assertions, while pinned and narrow
  groups pass 83 and 282 assertions on Mac and Orb.
  The promoted 20k and smallpt runs complete in 3.823 and 3.199 seconds; no stress run, diagnostic
  or new environment switch remains.
- `f2b20c3` lets the decoder's successor-flags proof follow one direct unconditional jump, sharing
  the existing one-transfer budget with direct calls. The jump and proved target span are registered
  as one conservative SMC dependency; indirect or second transfers, wrapping ranges, flag reads and
  unknown instructions still reject the proof. This exposes the already-existing region PFAF
  sinking path for jump veneers such as CoreMark `0x402821 -> 0x402673`, removing hot full-flags
  publication from several loops. The exact 20k retained-weight comparison keeps all 2,761 PCs /
  2,864 versions and moves `3,467,792,831 -> 3,384,479,634` (`-83,313,197`, `-2.402485%`) with no
  growing PC and CRC `0x382f`. `0x402593` shrinks 10 to six, `0x403360` and `0x4033ec` six to three,
  and `0x402814` 11 to eight host instructions. Smallpt matches `99.990001%` of entries and moves
  `227,528 -> 226,213` (`-0.577951%`) with no common growth and the canonical PPM. Dead-edge,
  narrow and production region groups pass 180, 282 and 46 assertions on Mac and Orb. A fixed-seed
  JCC differential A/B produced the same 25 pre-existing mismatch sequences with identical SHA in
  both builds, so it was used only as a candidate-delta audit. The promoted CoreMark and smallpt
  runs complete in 3.289 and 2.386 seconds; no stress run, diagnostic or new environment switch
  remains.
- `1c24adb` extends the narrow-extract analysis to the strict unique-use chain
  `BitExtract(0,width) -> ZeroExtend32 -> LsrImm`. When the three nodes are adjacent and the shift
  remains within U8/U16 width, the extract and extension emit nothing and `LsrImm` emits one `UBFX`
  from the original value. CoreMark's CRC inner loops therefore lose both `UXTB/UXTH + LSR` pairs.
  The exact 20k comparison keeps all 2,761 PCs / 2,864 versions and moves
  `3,508,879,677 -> 3,485,519,660` (`-23,360,017`, `-0.665740%`) with no growing PC and CRC
  `0x382f`. The `0x403980/0x403990`, `0x4038d0/0x4038e0/0x403908/0x403930` and
  `0x403870/0x403880` pairs each shrink by two instructions. Smallpt keeps all 2,730 PCs / 2,998
  versions and the canonical PPM while moving weighted `226,354 -> 226,348`. The focused U8/U16
  shape passes four assertions, and the narrow group passes 286 assertions on Mac and Orb. The
  promoted CoreMark and smallpt runs complete in 4.211 and 3.027 seconds; no stress run, diagnostic
  or new environment switch remains.
- `74ab5c7` lets a sole low U8/U16 `BitExtract` feed `AND` from the original W value when the other
  operand is a constant with every bit above the narrow width clear. The mask already removes those
  bits, so the extract emits nothing; masks with any high bit set retain the old path. CoreMark's
  CRC loops lose the redundant `UXTH` before `AND 0xa001`. The exact 20k comparison keeps all 2,761
  PCs / 2,864 versions and moves `3,485,519,660 -> 3,473,279,625` (`-12,240,035`, `-0.351168%`)
  with no growing PC and CRC `0x382f`. The main CRC loop pairs each shrink by one instruction, and
  `0x402278` shares the same proof. Smallpt keeps all 2,730 PCs / 2,998 versions and the canonical
  PPM while moving weighted `226,348 -> 226,317`. Positive/rejection coverage passes three
  assertions, and the narrow group passes 289 assertions on Mac and Orb. The promoted CoreMark and
  smallpt runs complete in 3.186 and 2.285 seconds; no stress run, diagnostic or new environment
  switch remains.
- `b969f7d` lets a proved dead-edge EQ/NE self-test branch directly on the value's fixed GPR home.
  The flags pass hands the narrow proof to the backend only for `BranchOnlyFlags(AND value,value)`,
  and the backend requires an exact full-width publication, the same allocated register, and no
  intervening write to that home before it removes the logical result and emits `CBZ/CBNZ`. The
  exact CoreMark comparison keeps all 2,761 PCs / 2,864 versions and moves weighted
  `3,473,279,625 -> 3,469,019,615` (`-4,260,010`, `-0.122651%`) with no growing PC and CRC `0x382f`.
  `0x402683` and `0x4026b0` each lose one host instruction; the retained W67 census also applies the
  same reduction to the 416.7M-entry `0x402808` loop. Smallpt keeps all 2,730 PCs / 2,998 versions,
  the canonical PPM, and moves weighted `226,317 -> 226,306`. The focused fixed-home shape and full
  dead-edge group pass 4 and 184 assertions on Mac and Orb. The post-refactor 2k screen completes in
  2.394 seconds with 2,761 PCs / 2,864 versions and `346,944,290` dynamic host instructions; no
  stress run, debug path or new environment switch remains.
- `32cd3ed` folds branch-only U8/U16 equality comparisons when at least one operand is an exact
  zero-extending narrow load. Because the branch requests only Z, the backend may commute the
  operands and compare the loaded W value against the other register with `UXTB/UXTH`; other flags,
  region PF/AF preservation, shifted operands and non-load pairs retain the general alignment path.
  CoreMark's `0x402668` loop changes from `LDRH; LSL; SUBS` to `LDRH; CMP ..., UXTH`. Applying the
  formal candidate entries to the prior shape covers `99.999998%` of host weight and moves
  `3,469,019,672 -> 3,466,959,672` (`-2,060,000`, `-0.059383%`) with no growing PC and CRC `0x382f`.
  `0x402668` contributes `-2,040,000` and `0x402745` contributes `-20,000`; both shrink by one host
  instruction. The focused shape passes eight assertions and the dead-edge / narrow groups pass
  184 / 297 assertions on Mac and Orb. The promoted CoreMark and smallpt gates complete in 3.152
  and 2.308 seconds; smallpt retains all 2,730 PCs / 2,998 versions and the canonical PPM. No stress
  run, debug path or new environment switch remains.
- `1a69a59` folds the identity-mode sequence `load [base+1]; add base,1` when the base is a fixed
  GPR, the load and update are its only ordinary uses, the update flags are dead, the full update
  publishes back to the same home, and the intervening window has no fault, helper or base
  observer. The load emits the original scalar access with AArch64 pre-index writeback and the
  separate Add/publication emit nothing. A fault does not commit writeback, so the guest base still
  reflects the x86 state before its following Add. The exact CoreMark comparison keeps all 2,761
  PCs / 2,864 versions and moves `3,466,959,740 -> 3,441,199,737` (`-25,760,003`,
  `-0.743014%`) with 100% coverage, no growing PC and CRC `0x382f`. `0x4033bb` contributes
  `-19,360,000`, `0x4033d8` contributes `-5,440,000`, and `0x4033dc` contributes `-960,000`;
  each shrinks by one instruction. The focused code-shape / fault cases pass 2 / 5 assertions and
  the pinned group passes 90 assertions on Mac and Orb. CoreMark and smallpt complete in 3.357 and
  2.353 seconds; smallpt remains byte-identical at 2,730 PCs / 2,998 versions with the canonical
  PPM. No stress run, debug path or new environment switch remains.
- `2829a8d` replaces Runtime's inline 64-frame RSB storage with a 4 MiB usable mapping bracketed by
  inaccessible host pages. The stack starts at the midpoint so either call overflow or unmatched
  return growth reaches a guard. Runtime fault recovery claims the address only after the fault PC
  resolves to the current JIT and x25 is adjacent to that Runtime's guard, resets x25 to the empty
  midpoint in `ucontext`, and retries the interrupted instruction. The existing explicit RSB bounds
  remain for this infrastructure stage, so default code shape and benchmark totals do not change.
  Real `STP` lower-guard and `LDP` upper-guard recovery passes five assertions on Mac and Orb; the
  existing guest PageFatal case also passes five assertions on both. No stress run, diagnostic or
  environment switch was added.
- `52441a2` consumes the guarded RSB substrate and attacks the two remaining concentrated boundary
  costs. RSB push/pop no longer loads or compares explicit bottom/top pointers; empty frames are
  rejected by their zero dispatch slot, while real lower/upper escapes use guarded-fault recovery.
  Function translation now groups repeated dynamic terminal targets by physical register, suppresses
  eager `current_loc` stores, and branches misses to one cold publisher per register. Separately,
  fixed-home copy ownership may transfer selected post-publication U32 Add/Sub/And/Or/Xor consumers
  to the destination home after the source home is overwritten. The proof remains fail-closed for
  target rewrites, helper clobbers, observable operations, non-U32 values and other consumers.
  Against the guarded-stack baseline, exact CoreMark 20k moves `3,438,479,676 -> 3,416,879,675`
  (`-21,600,001`, `-0.628185%`) with all 2,761 PCs / 2,864 versions, 100% coverage, top-20 20/20 and
  `crcfinal=0x382f`. The retained W67 join including deferred terminal publication moves
  `13,188,392,770 -> 12,999,112,545` (`-1.435203%`) at 99.997866% coverage with no growing PC.
  Calibrated smallpt remains byte-identical at 2,730 PCs / 2,998 versions, zero spills and
  `226,048` weighted host instructions. Mac and Orb pinned-GPR, guarded-return and direct-link
  focuses pass; no stress run, diagnostic or environment switch remains.
- `46a2a23` adds a fail-closed narrow carry-chain fusion and lowers the shared terminal-publisher
  threshold from three same-register sites to two. The carry proof accepts only adjacent
  `TestFlags(C) -> Add(0,C) -> Add(value,carry)` chains whose final flags are dead, whose result is
  published only at U8/U16 width, and whose carry is still live in host PSTATE. It emits one `ADC`
  and removes the materialized zero/carry value chain; all other users, flag observers and
  clobbers reject the plan. Two deferred terminal sites exactly replace their two eager stores with
  one two-instruction cold publisher, so total code size does not grow. Exact CoreMark 20k moves
  `3,416,879,675 -> 3,397,918,414` (`-18,961,261`, `-0.554929%`) with all 2,761 PCs / 2,864
  versions, 100% coverage, top-20 20/20, no growing PC and `crcfinal=0x382f`. `0x4026ca` shrinks
  `25 -> 21`, contributing `-14,640,000`; `0x402218` shrinks `38 -> 37`, contributing
  `-4,161,115`. Calibrated smallpt stays byte-identical and moves `226,048 -> 225,561`
  (`-0.215441%`). Mac and Orb narrow/carry, pinned-GPR and direct-link focuses pass 297 / 45 / 90 /
  about 900k assertions. No long run, stress run, diagnostic or environment switch remains.
- The current FEX gap is now refreshed from a live same-input 2k run rather than extrapolated from
  the earlier retained denominator. `/usr/local/fex-measure/FEX` is the `f2e35f3` measurement build;
  both engines use the same CoreMark ELF, arguments and CRC `0x4983`, with FEX
  `FEX_HOSTFEATURES=disableavx`, multiblock enabled and code caching disabled. The join covers
  99.999934% of entries and 99.999902% of SVM host weight. Before this stage it measured SVM
  `2.004542` versus FEX `1.860013` host instructions per guest instruction, or `1.077703x`.
  `46a2a23` moves SVM to `1.993418`, or **`1.071723x`**, leaving a current same-input gap of about
  **7.17%**. This supersedes the earlier optimistic retained-table `1.052299x` estimate.
  The next concentrated weighted gaps are dynamic return continuation at `0x403383` (~9.78M),
  indirect-call boundary work at `0x402580` (~7.53M), cross-call flags publication at
  `0x403680/0x403552` (~5.80M/~5.30M), and the remaining narrow compare/ADC work at `0x4026ca`
  (~4.49M). The first three require continuation or cross-unit flags ABI work; do not replace them
  with per-site cold growth or ABI assumptions about guest code.
- Static `SetLocation(imm) + ReturnToDispatch` edges now carry the same `DirectLinkFlagsBypass`
  recipe as `LinkBlock` edges (`aa4ed1b`). This closes the missing connection on direct calls: when
  the linked target publishes a pending-flags entry, LinkManager replaces the first instruction of
  the three-instruction full NZCV merge with a branch over the whole merge and links the site to
  that entry. Incompatible publication and SMC invalidation continue to restore the original
  instruction through the existing generation/unlink transaction. There is no new runtime switch,
  fallback protocol or static code growth. An exact detached-`e53e193` 20k A/B keeps all 2,819 PCs /
  2,934 versions, 100% coverage and `crcfinal=0x382f`; the static weighted number is intentionally
  identical because the saving is a runtime patch. The retained host dump contains two dominant
  `merge; poll; BL` opportunities at `0x403320` and `0x403552`, with a mechanical linked-path upper
  bound of 65,600,000 fewer executed instructions; realization is conditional on their targets
  advertising pending-flags entries. The bounded smallpt oracle remains
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`.
  Mac and Orb end-to-end checks pass 14 assertions for the new static-forward path and 40 for the
  existing shared conditional bypass, including actual patching to the pending-flags entry and SMC
  restoration. No diagnostic path or environment switch was retained.
- The first raw host-continuation RSB prototype was rejected and fully removed before `aa4ed1b`.
  It stored `{guest return, ADR continuation}` in the existing guarded x25 stack, reset the logical
  stack before leaving the active QSBR epoch, and sent misses to the existing deferred dispatcher
  publisher. It was correct on CoreMark/smallpt and the Mac/Orb SMC checks, but an exact
  detached-`e53e193` CoreMark comparison was `3,940,481,890 -> 4,013,934,489`
  (`+73,452,599`, `+1.864051%`) with 100% PC/version coverage. The dominant costs were two extra
  instructions at direct/indirect calls and one extra instruction at return; the older
  `build-master` comparison that looked strongly positive was stale and is invalid. Do not retry
  caller-side `ADR+STP`. A viable continuation ABI must form LR through a call-kind `BL`, push it in
  a call-entry veneer without re-materializing the guest return, and replace the per-return signal
  poll with an equally strong fault/cycle safepoint before it can beat inline L1.

## Orb loop

```
M=/mnt/mac/Users/swift/CLionProjects/SwiftVM
P=/home/swift/svm-phasec/SwiftVM
git ls-files -z | rsync -a --no-times --checksum --from0 --files-from=- ./ ubuntu@orb:$P/
cmake --build /home/swift/svm-phasec/build --target svm_translator_linux -j$(nproc)
```

Keep `--no-times --checksum` for A/B source switches. Preserved older source mtimes can otherwise
leave a newer Ninja object in place even though the source contents changed.

Mac: `cmake --build build-master --target swift_runtime`.

func_tests one-iter coremark can halt reason 2 even on good binaries; use **20000** iters for CRC.

## Related docs (historical, some defaults stale)

- `docs/fex-codegen-gap-plan-2026-08.md` — combo: flags × region × RA
- `docs/codegen-p0b-flags-repr-2026-08.md` — still says FLAGS default OFF / 16-block in places
- `docs/svm-config-classification.md`
