#!/usr/bin/env python3
import collections
import csv
import os
import re

gap_re = re.compile(r"block=(0x[0-9a-f]+).* op=([A-Za-z0-9_]+) bytes=(\d+)")
hot_re = re.compile(r"pc=(0x[0-9a-f]+).*entries=(\d+).*host_static=(\d+)")
W67 = os.environ.get("W67", "/mnt/mac/Users/swift/CLionProjects/SwiftVM-w67")
OUT = os.environ.get("CQ_OUT", "/tmp/svm-linux-cq")
BENCHES = ["coremark", "stream", "smallpt", "sqlite", "cray", "zip7", "osslsha"]
WATCH = [
    "AdvancePC",
    "SetLocation",
    "PushRSB",
    "PopRSB",
    "SetHostGPR",
    "SetHostFPR",
    "StoreUniform",
    "GetOperand",
    "GetHostGPR",
    "GetHostFPR",
    "LoadUniform",
    "Sub",
    "And",
    "Or",
    "Xor",
    "ZeroExtend32To64",
    "BitExtract",
]


def load_guest(bench: str) -> dict[int, tuple[int, int]]:
    guest: dict[int, tuple[int, int]] = {}
    try:
        with open(f"{W67}/codegen-quality-svm-{bench}.tsv", encoding="utf-8") as handle:
            for row in csv.DictReader(handle, delimiter="\t"):
                try:
                    guest[int(row["pc"], 16)] = (int(row["entries"]), int(row["guest_inst"]))
                except (KeyError, TypeError, ValueError):
                    continue
    except OSError as exc:
        print(f"skip {bench} tsv: {exc}")
    return guest


def load_hot(bench: str) -> set[int]:
    hot: set[int] = set()
    try:
        with open(f"{OUT}/{bench}.hot.log", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = hot_re.search(line)
                if match:
                    try:
                        hot.add(int(match.group(1), 16))
                    except ValueError:
                        continue
    except OSError as exc:
        print(f"skip {bench} hot: {exc}")
    return hot


def load_ops(bench: str) -> dict[int, dict[str, int]]:
    ops: dict[int, dict[str, int]] = collections.defaultdict(lambda: collections.defaultdict(int))
    try:
        with open(f"{OUT}/{bench}.log", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if "svm-gap-op" not in line:
                    continue
                match = gap_re.search(line)
                if not match:
                    continue
                try:
                    pc = int(match.group(1), 16)
                    nbytes = int(match.group(3))
                except ValueError:
                    continue
                ops[pc][match.group(2)] += nbytes
    except OSError as exc:
        print(f"skip {bench} log: {exc}")
    return ops


def main() -> None:
    print(
        f"{'bench':<10} {'AdvPC':>6} {'SetLoc':>6} {'Push':>6} {'Pop':>6} "
        f"{'SetGPR':>6} {'SetFPR':>6} {'StUni':>6} {'GetOp':>6} "
        f"{'GetGPR':>6} {'GetFPR':>6} {'LdUni':>6}"
    )
    for bench in BENCHES:
        guest = load_guest(bench)
        hot = load_hot(bench)
        ops = load_ops(bench)
        acc: dict[str, float] = collections.defaultdict(float)
        guest_weighted = 0.0
        for pc, (entries, guest_inst) in guest.items():
            if pc not in hot or pc not in ops:
                continue
            guest_weighted += entries * guest_inst
            for op_name, nbytes in ops[pc].items():
                acc[op_name] += entries * (nbytes / 4.0)

        def hg(name: str) -> float:
            return acc[name] / guest_weighted if guest_weighted else 0.0

        print(
            f"{bench:<10} {hg('AdvancePC'):6.3f} {hg('SetLocation'):6.3f} "
            f"{hg('PushRSB'):6.3f} {hg('PopRSB'):6.3f} {hg('SetHostGPR'):6.3f} "
            f"{hg('SetHostFPR'):6.3f} {hg('StoreUniform'):6.3f} {hg('GetOperand'):6.3f} "
            f"{hg('GetHostGPR'):6.3f} {hg('GetHostFPR'):6.3f} {hg('LoadUniform'):6.3f}"
        )

    print("\n== top emitted ops (h/g) ==")
    for bench in ["coremark", "zip7", "osslsha", "smallpt", "stream"]:
        guest = load_guest(bench)
        hot = load_hot(bench)
        ops = load_ops(bench)
        acc = collections.defaultdict(float)
        guest_weighted = 0.0
        for pc, (entries, guest_inst) in guest.items():
            if pc not in hot or pc not in ops:
                continue
            guest_weighted += entries * guest_inst
            for op_name, nbytes in ops[pc].items():
                acc[op_name] += entries * (nbytes / 4.0)
        top = sorted(acc.items(), key=lambda item: -item[1])[:10]
        print(bench, ", ".join(f"{name}={value / guest_weighted:.3f}" for name, value in top))


if __name__ == "__main__":
    main()
