# Codegen benchmark fast path

Use this path to screen code-generation-only candidates. It does not replace the formal workload
gate for a promoted stage.

## Contract

The comparison has three inputs:

1. a retained formal hot log supplies stable `entries` weights;
2. the baseline binary runs a short workload and supplies baseline `host_static`;
3. the candidate binary runs the same short workload and supplies candidate `host_static`.

Only PCs present in all three logs with identical version counts are weighted. Missing PCs and
version mismatches are never filled from another build. The default screening gate requires at
least 99.9% formal host-weight coverage and every formal top-20 PC.

Baseline and candidate must use the same guest, arguments, build type and codegen configuration.
Guest output must be byte-identical. Do not use this path for changes that alter guest semantics,
control flow, unit formation or workload inputs.

## Capture

`quick_shape.py` clears JIT cache, execution profile, density profile and detailed translation
profile variables in the child. It enables only the existing hot-shape collector, writes stdout
and stderr to the requested output directory, enforces a timeout and hashes requested oracles.

```sh
ROOT=/path/to/SwiftVM
BASE=/path/to/baseline/svm_translator_linux
CAND=/path/to/candidate/svm_translator_linux
GUEST=/path/to/smallpt_wh_x64
OUT=/path/to/empty/output
FORMAL=/path/to/retained/formal-smallpt.hot

python3 "$ROOT/tools/svm-linux-cq/quick_shape.py" \
  --svm "$BASE" --guest "$GUEST" --out "$OUT/base" \
  --timeout 15 --oracle image.ppm -- 4 8 6
python3 "$ROOT/tools/svm-linux-cq/quick_shape.py" \
  --svm "$CAND" --guest "$GUEST" --out "$OUT/candidate" \
  --timeout 15 --oracle image.ppm -- 4 8 6
cmp "$OUT/base/image.ppm" "$OUT/candidate/image.ppm"
python3 "$ROOT/tools/svm-linux-cq/weighted_diff.py" \
  --weights "$FORMAL" "$OUT/base/shape.hot" "$OUT/candidate/shape.hot" \
  --min-coverage 99.9 --top 20 --fail-on-growth
```

The first smallpt argument must be at least 4. This guest divides it by four; values 1–3 execute
zero samples and provide invalid coverage.

## Calibrated smallpt screen

On Orb, `smallpt_wh_x64 4 8 6` completed in 7.2–8.4 seconds without detailed density logging.
Against the retained formal `8 128 96` weights it covered 99.983053% of weighted host execution,
99.944552% of entries and all top-20 PCs. Uncovered formal weight was 166,826 of 984,381,725.
Two independent short runs had identical relevant shapes and identical PPM bytes.

The short baseline may be reused for multiple candidates derived from the same code/configuration.
Formal weights may be reused while guest semantics and control flow remain unchanged.

## Promotion policy

1. Iterate with focused correctness tests plus the short weighted screen.
2. Reject growth and low-coverage candidates immediately.
3. Run one formal benchmark only after a candidate shows a stable material reduction or changes a
   correctness-sensitive boundary that requires the formal oracle.
4. Run c-ray, STREAM and CoreMark only when the mechanism affects their instruction families or at
   a stage milestone. Run the full fingerprint/grid/stress set only before a milestone delivery.
5. Any short-run PC/version mismatch caused by the candidate bypasses estimation and goes directly
   to the formal gate.
