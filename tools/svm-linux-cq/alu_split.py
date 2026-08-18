#!/usr/bin/env python3
"""Join svm-gap-op alu_role fields with hot-all entries.

Paper gate: flags-pack host bytes of Sub/And/Or must be >= 60% of those
three opcodes combined (entries-weighted). Mixed ops contribute pack_b
to the pack bucket and alu_b to real ALU.
"""
from __future__ import annotations

import collections
import os
import re
import sys

OPS = ("Sub", "And", "Or")
GAP = re.compile(
    r"block=(0x[0-9a-f]+).* op=([A-Za-z0-9_]+) bytes=(\d+)"
    r".*alu_role=([a-z]+) value_uses=(\d+) flags=(\d+) "
    r"pack_b=(\d+) alu_b=(\d+) addr_b=(\d+)"
)
HOT = re.compile(r"pc=(0x[0-9a-f]+).*entries=(\d+).*host_static=(\d+)")


def load_hot(path: str) -> dict[int, tuple[int, int]]:
    hot: dict[int, tuple[int, int]] = {}
    try:
        handle = open(path, encoding="utf-8", errors="replace")
    except OSError as exc:
        raise SystemExit(f"hot log: {exc}") from exc
    with handle:
        for line in handle:
            if "svm-hot-all" not in line:
                continue
            match = HOT.search(line)
            if not match:
                continue
            try:
                hot[int(match.group(1), 16)] = (
                    int(match.group(2)),
                    int(match.group(3)),
                )
            except ValueError:
                continue
    return hot


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: alu_split.py <bench.log> <bench.hot.log>", file=sys.stderr)
        return 2
    log_path, hot_path = sys.argv[1], sys.argv[2]
    hot = load_hot(hot_path)
    acc = collections.defaultdict(lambda: collections.defaultdict(float))
    role_ops = collections.defaultdict(lambda: collections.defaultdict(float))
    host = 0.0
    try:
        handle = open(log_path, encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"log: {exc}", file=sys.stderr)
        return 2
    with handle:
        for line in handle:
            if "svm-gap-op" not in line:
                continue
            match = GAP.search(line)
            if not match:
                continue
            try:
                pc = int(match.group(1), 16)
                pack_b = int(match.group(7))
                alu_b = int(match.group(8))
                addr_b = int(match.group(9))
                bytes_ = int(match.group(3))
            except ValueError:
                continue
            op = match.group(2)
            if op not in OPS:
                continue
            entries, host_static = hot.get(pc, (0, 0))
            if entries == 0:
                continue
            host += entries * host_static
            role = match.group(4)
            # Host insn = bytes/4. Weight by entries.
            acc[op]["pack"] += entries * (pack_b / 4.0)
            acc[op]["alu"] += entries * (alu_b / 4.0)
            acc[op]["addr"] += entries * (addr_b / 4.0)
            acc[op]["all"] += entries * (bytes_ / 4.0)
            role_ops[op][role] += entries * (bytes_ / 4.0)

    print(f"{'op':<6} {'all':>12} {'pack':>12} {'alu':>12} {'addr':>12} {'pack%':>7}")
    tot = collections.defaultdict(float)
    for op in OPS:
        row = acc[op]
        share = 100.0 * row["pack"] / row["all"] if row["all"] else 0.0
        print(
            f"{op:<6} {row['all']:12.0f} {row['pack']:12.0f} "
            f"{row['alu']:12.0f} {row['addr']:12.0f} {share:6.2f}%"
        )
        for key in ("all", "pack", "alu", "addr"):
            tot[key] += row[key]
    share = 100.0 * tot["pack"] / tot["all"] if tot["all"] else 0.0
    print(
        f"{'SUM':<6} {tot['all']:12.0f} {tot['pack']:12.0f} "
        f"{tot['alu']:12.0f} {tot['addr']:12.0f} {share:6.2f}%"
    )
    print(f"gate: pack/SUM >= 60% -> {'PASS' if share >= 60.0 else 'FAIL'} ({share:.2f}%)")
    print("role whole-op (bytes/4):")
    for op in OPS:
        parts = ", ".join(
            f"{role}={count:.0f}"
            for role, count in sorted(role_ops[op].items(), key=lambda kv: -kv[1])
        )
        print(f"  {op}: {parts}")
    if host:
        print(
            f"pack of program host: {tot['pack'] / host:.4f}  "
            f"three-op of host: {tot['all'] / host:.4f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
