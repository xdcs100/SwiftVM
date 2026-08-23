# Codex handoff: Align SVM flags with FEX

Date: 2026-08-24
Repo: `/Users/swift/CLionProjects/SwiftVM` (macOS). Linux identity runs on Orb: `ubuntu@orb`, tree `/home/swift/svm-phasec/SwiftVM`, build `/home/swift/svm-phasec/build`.
Author on git: `swift_gan`. **Do not push** until asked. English commits, no task IDs, no AI trailer.

**Do not inspect failed run `e333e444`** (xAI capacity / connection). Goal notices that name it are stale.

## Git / mission

- Code tip: **`431be30`** `feat: publish vector zip results in place`
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

Hot files:

- `translator_region.cpp` — `SuccessorCoversIncomingNzcv`, `BlockIsFlagsTransparent`, `EmitRegionIf`
- `translator_flags.cpp` — `MergeNZCV`, `force_ret_pstate`
- `translator_terminal.cpp` — generic If / LinkBlock / RSB
- `translator/x86/translator.cpp` — `RegionFuncBudget`, `kMaxFuncBlocks=128`, lazy skip of published L2
- `register_alloc_coalesce_gpr.cpp` — pinned guest GPR read/write coalescing and full-width load publication
- `register_alloc_coalesce_copy.cpp` — exact adjacent low32 copy-chain ownership
- `integer_width_elimination_pass.cpp` — local zero-extend/low32-extract round-trip elimination
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

Validation for `7110d20` / `7045d3b` / `431be30`:

- Current formal smallpt is `smallpt_wh_x64 8 128 96`; do not substitute the fixed 1024×768
  `smallpt_x64` when updating the formal FEX ratio.
- Formal smallpt default-region host: `1,321,651,162 → 1,303,939,990 → 1,297,980,655`;
  cumulative `-23,670,507` (`-1.791%`), spill 0 throughout. The first arrow is full-NZCV
  compaction; the second is VecZip resident publication. `7110d20` is neutral on this binary but
  saves 452,646,984 host instructions on the fixed 1024×768 smallpt.
- c-ray equal-entry common-PC deltas: full-NZCV `-999,520`, VecZip `-124,308`; no common PC grows.
  CoreMark after full-NZCV is about `5,973,080,081` host (`-61.19M`, CRC final `0x382f`);
  VecZip is neutral there.
- FEX-aligned RE=0 same-harness refresh for formal smallpt: SVM host/guest
  `3.335622 → 3.267832`; with unchanged FEX `1.549`, ratio `2.153× → 2.110×`. The earlier
  2.180× table used a different retained unit-formation artifact, so quote the current gap as
  approximately 2.11–2.14× rather than mixing the two raw tables.
- PPM SHA-256 remains
  `fe96f7e48295b27c8df8236294052d138c3ed130b81d022739907fe6b2cde5aa`; c-ray IDAT MD5
  remains `54256cb4b3c6313a65ea12ebb7b81e30`; STREAM validates.
- FPR-focused tests: 3 cases / 10 assertions plus resident coalescing 1 case / 794 assertions;
  full-NZCV structure 1 case / 3 assertions. FLAGS six-grid, helper-fault 38/0, clone four-grid
  and 1664-unit/11-guest fingerprint all pass.
- Fixed `SWIFT_FUZZ_SEED=123456`: 183 passed / 35 existing failed cases,
  1,047,656 passed / 44 failed assertions; no new failure location.
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
| RSB pop reuses dynamic SetLocation | one-instruction ceiling is only 0.076% smallpt / 0.313% c-ray |

## Next ready (pick one, measure, revert on 124/134)

1. **smallpt call/return and public-exit boundary** — boundary remains about 23%, link about 13%.
   Reopen only a specific call/return or public-exit shape with fault, SMC and RSB proof; do not
   infer a removable pool from the aggregate bucket.
2. **Remaining FPR publication** — SetHostFPR is about 5%, but full writes are only about 2.3%.
   Extend the producer set only for a single-instruction complete V128 write with exact alias proof.
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
