# Codex handoff: Align SVM flags with FEX

Date: 2026-08-24
Repo: `/Users/swift/CLionProjects/SwiftVM` (macOS). Linux identity runs on Orb: `ubuntu@orb`, tree `/home/swift/svm-phasec/SwiftVM`, build `/home/swift/svm-phasec/build`.
Author on git: `swift_gan`. **Do not push** until asked. English commits, no task IDs, no AI trailer.

**Do not inspect failed run `e333e444`** (xAI capacity / connection). Goal notices that name it are stale.

## Git / mission

- Code tip: **`2f2fb88`** `perf: defer terminal location publication`
- Tracked tree is clean before the documentation commit. Preserve the existing untracked build/images/placement tools.
- Pi mission: `9306cb64-ce70-4726-a5e0-76fce2d23556` (goal mode ON). Rollback remains `SVM_FLAGS_REGS=0` (`ParseNonZero`; unset → ON).
- `npm:pi-codex-goal` is installed user-wide; `/goal` tools need a **new** Pi session.

## What already landed (read these first)

Default **`SVM_FLAGS_REGS=1`** (`121620f`). Region edges default ON. Default region window is **64** blocks (`SVM_FUNC_LAZY=1` means that window; `2..127` override; `<=0` eager 1024).

| Commit | What |
|---|---|
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
| `b3998d5` | Use the next region block for cycle-polled conditional layout without falling through into per-block cold stubs |
| `3ec9582` | Pair the production indirect-L1 request and cache-base loads; confirm observed signals through the shared acquire-checking trampoline |
| `e74e734` | Keep a complete V128 producer in its resident home through safe post-publication SSA uses; reject later home writes and multi-home remaps |
| `030f52d` | Replace an adjacent low-load/high-zero resident publication with one fault-exact D-register load after a full observer and alias proof |
| `97009a3` | Publish legacy scalar sqrt results through a dead resident merge home; independently reprove the fixed read, last use, and publication window |
| `f99eabf` | Reuse a dead fixed left home for legacy scalar FP binaries while preserving the high lane through a reserved temporary |
| `b692fca` | Make direct absolute `GetOperand` materialization the default; retain `=0` as the code-shape rollback |
| `c15a712` | Combine compact FCMP parity publication and AF clearing into one proof-backed bitfield insert |
| `2f2fb88` | Defer trailing constant `SetLocation` publication to dispatcher misses while preserving mid-block and dynamic observers |

Hot files:

- `translator_region.cpp` — `SuccessorCoversIncomingNzcv`, `BlockIsFlagsTransparent`, `EmitRegionIf`
- `translator_flags.cpp` — `MergeNZCV`, `force_ret_pstate`
- `translator_terminal.cpp` — generic If / LinkBlock / RSB
- `translator/x86/translator.cpp` — `RegionFuncBudget`, `kMaxFuncBlocks=128`, lazy skip of published L2
- `register_alloc_coalesce_gpr.cpp` — pinned guest GPR read/write coalescing and full-width load publication
- `register_alloc_coalesce_copy.cpp` — exact adjacent low32 copy-chain ownership
- `integer_width_elimination_pass.cpp` — local zero-extend/low32-extract round-trip elimination
- `register_alloc_coalesce_fpr.cpp` — resident-home interval proof and publication ownership
- `translator_fpr_publication.cpp` — scalar load/zero-high pairing and observer proof
- `translator_mem.cpp` — independent host-FPR publication proof and final bridge emission
- `translator_alu_vec_fp.cpp` — scalar-unary merge emission and redundant self-copy suppression
- `svm_config.h` — `flags_regs` default true; `region_edges` bounded64

## Honest density (coremark `0x0 0x0 0x66 20000 7 1 2000`)

Measure on Orb **without** `SVM_EXEC_PROF` (it inflates host). Always:

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

Validation for `7110d20` / `7045d3b` / `431be30` / `fa1768a` / `729b826` / `24f9d49` / `b3998d5` / `3ec9582` / `e74e734` / `030f52d` / `97009a3` / `f99eabf` / `b692fca` / `c15a712` / `2f2fb88`:

- Current formal smallpt is `smallpt_wh_x64 8 128 96`; do not substitute the fixed 1024×768
  `smallpt_x64` when updating the formal FEX ratio.
- Formal smallpt default-region host:
  `1,321,651,162 → 1,303,939,990 → 1,297,980,655 → 1,296,969,640 → 1,246,900,800 → 1,201,575,549 → 1,201,372,215 → 1,199,466,420 → 1,187,471,711 → 1,168,614,398 → 1,165,502,656 → 1,163,020,553 → 1,141,267,073 → 1,126,372,521 → 1,096,331,217`;
  cumulative `-225,319,945` (`-17.048%`), spill 0 throughout. The arrows are full-NZCV
  compaction, VecZip resident publication, retained RSB target reuse, static-exit direct-link,
  default return-L1, cycle-polled successor layout, paired indirect-L1 state loading, then live
  resident-FPR publication, scalar-load FPR fusion, scalar-sqrt resident publication, then
  legacy scalar-binary resident publication, direct absolute-address materialization, compact
  FCMP non-NZCV publication, then trailing static-location cold publication.
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
- FEX-aligned RE=0 same-harness refresh for formal smallpt: SVM host/guest
  `3.335622 → 3.267832`; the landed stages fold this to about `2.760155`. With unchanged FEX
  `1.549`, ratio is `2.153× → 1.782×`. The earlier
  2.180× table used a different retained unit-formation artifact, so quote the current gap as
  approximately 1.78–1.84× rather than mixing the two raw tables.
- PPM SHA-256 remains
  `fe96f7e48295b27c8df8236294052d138c3ed130b81d022739907fe6b2cde5aa`; prior equal-entry
  c-ray IDAT remains `54256cb4b3c6313a65ea12ebb7b81e30`, 64-spp formal c-ray is
  `d0c71130abf3544a86b64417bc488c21`, and STREAM validates.
- Scalar-load structure/fault tests pass 2 cases / 12 assertions and VEX.128 move differential
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
- RSB/indirect structure focus passes 26 assertions, including the paired state/cache load and
  no-target dispatcher path. A temporary mismatched-return probe passes default, both L1-off RSB
  frames, FLAGS-off and interpreter paths and was deleted. Mac and Orb builds pass.
- The production inline-L1 pending-signal test passes 6 assertions on Mac and Orb. Direct-link
  production passes 11 cases / 395 assertions under FLAGS=0, including cache lifecycle. Static
  SetLocation fallback and repeated delink/recompile paths pass 36/142 assertions on Mac and Orb.
- Region edge, direct-cycle signal and region-flags focus pass 42/30/46 assertions. The new layout
  matches the baseline fingerprint for 1664 function units over 11 guests.
- MT SMC stress passes 200/200 with zero host failure, lost guest or timeout. Catch/fuzz seed
  424242 gives the latest Orb tree 191 passed / 35 existing failed cases / 45 failed assertions;
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
- A low32 copy may skip `BitExtract` only when its sole use is the immediately following
  `ZeroExtend32To64`; the wrapper must still emit a W move and keep all later uses.
- A width round trip may substitute the original U32 SSA only for
  `BitExtract(ZeroExtend32To64(v32), 0, 32)` with one ordinary same-block consumer inside the
  128-IR window.
- Same-width extraction is restricted to a U32 result and an audited W-reading consumer. U8/U16,
  pseudo and opaque calls must retain the real extract because backend physical high bits are not
  implied by the narrow IR type.
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
| β.1 `needed=All` → `ComputeFunctionLiveIn` | FLAGS=1 **0**; PF/AF already skipped on producers |
| `SVM_RA_WIDTH_CHAIN=1` | 0 on coremark |
| `GetHostGPR` 32-bit `Mov W` for callee-saved pins | 0 |
| `SVM_FUNC_LAZY=128` before `15e5165` | **31.3B** host, RE=0-shaped entries |
| Require every successor-cover to survive fault and reach AdvancePC | **6.300→6.551B** host; too conservative, reverted |
| PF/AF dedicated GPR on current CoreMark | saves 0; adds 67,754,766 dispatcher/RSB recovery instructions |
| SHA census from failing OpenSSL path | PageFatal at `rip=0x62b930` before valid hashing; no performance evidence |
| Generic same-width fold including U8/U16/CallLambda | fixed-seed U16 popcount helper mismatch; narrowed to U32 W-consumer whitelist |
| Linux AFP scalar insert | c-ray −1.92%, but smallpt diverges from the exact FEX PPM; tie=0 still diverges, fully reverted |
| True fallthrough after a direct cycle poll | falls into the source block's cold stub; CoreMark 1.224s→57.597s despite correct CRC, fully rejected |
| Tagged L1 control word loaded with nonzero-offset `LDAR` | AArch64 `LDAR` has no immediate offset; VIXL ignored it and production hit PageFatal, fully reverted |
| `LDAXP` request/cache-base pair | smallpt `-1,905,795`, but call-dense scale-3 wall time regressed 223%; fully reverted |
| Terminal-tail cycle success branch | formal smallpt bit-identical at `1,168,614,398`; safe target-bound pool is empty after successor layout, fully reverted |

## Next ready (pick one, measure, revert on 124/134)

1. **smallpt remaining link** — covered link is now about 6.6%. Region/cycle tails are about
   2.1%; their acquire poll and branch across per-block cold stubs are load-bearing. Audit the
   roughly 1.5% remaining return-L1 static sequences separately; `AND + ADD + LDP + CMP + CSEL + BR`
   has no obvious base-ISA fusion. Public host exit executes only 139 times. The remaining
   `SetLocation` tail is dynamic or has a later observer and must not inherit the trailing-constant proof.
2. **Remaining FPR publication** — SetHostFPR is about 30,193,587 (`2.754%`), with about
   8,317,804 (`0.759%`) full writes. Low-load/high-zero remain 10,641,609 / 10,093,484; about 8.29M adjacent
   candidates were rejected by exact fault/alias/home gates and must not be recovered heuristically.
3. **CoreMark remaining truncations** — raw BitExtract is no longer a pool. Only reopen 8/16-bit
   cases with a consumer-specific physical-high proof and the U16 helper regression in the gate.
4. **SHA valid workload first** — fix or replace the current OpenSSL guest path that PageFatals before hashing, then redo the boundary census. Do not bypass guest fault semantics.
5. **PF/AF dedicated GPR is closed** until a new canonical park/recovery carrier yields a nonzero mechanical saving; the current audit is strictly negative.
6. **Do not** grow the default region window again for coremark (64 == 128). Other benches might still want 128 **after** the lazy fix.

## Orb loop

```
M=/mnt/mac/Users/swift/CLionProjects/SwiftVM
P=/home/swift/svm-phasec/SwiftVM
git ls-files -z | rsync -a --from0 --files-from=- ./ ubuntu@orb:$P/
cmake --build /home/swift/svm-phasec/build --target svm_translator_linux -j$(nproc)
```

Mac: `cmake --build build-master --target swift_runtime`.

func_tests one-iter coremark can halt reason 2 even on good binaries; use **20000** iters for CRC.

## Related docs (historical, some defaults stale)

- `docs/fex-codegen-gap-plan-2026-08.md` — combo: flags × region × RA
- `docs/codegen-p0b-flags-repr-2026-08.md` — still says FLAGS default OFF / 16-block in places
- `docs/svm-config-classification.md`
