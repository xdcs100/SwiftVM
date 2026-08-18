#!/usr/bin/env python3
import csv
import collections
import os
import re

PUBLISH = {"SetHostGPR", "SetHostFPR", "StoreUniform", "StoreContext"}
SREAD = {"GetHostGPR", "GetHostFPR", "LoadUniform", "GetOperand", "LoadContext"}
FLAGS = {
    "SaveFlags",
    "TestFlags",
    "ClearFlags",
    "BranchOnlyFlags",
    "LocalCondSet",
    "CondSet",
    "MergeNZCV",
    "PublishFlags",
    "SaveNZCV",
    "RestoreNZCV",
    "LoadFlags",
    "StoreFlags",
    "GetFlags",
    "SetFlags",
}
MEM = {"LoadMemory", "StoreMemory"}
IMM = {"LoadImm"}
CTRL = {
    "AdvancePC",
    "SetLocation",
    "PushRSB",
    "PopRSB",
    "Terminal",
    "CondJump",
    "Jump",
    "Call",
    "Return",
    "IndirectJump",
    "Dispatch",
    "Exit",
    "CallHost",
    "CallLambda",
}
XPORT = {
    "BitCast",
    "ZeroExtend32To64",
    "ZeroExtend32",
    "ZeroExtend",
    "SignExtend",
    "BitExtract",
    "BitInsert",
    "Copy",
    "Move",
    "Nop",
}

def bucket(op: str) -> str:
    if op in PUBLISH:
        return "publish"
    if op in SREAD:
        return "state_read"
    if op in FLAGS or "Flag" in op or "NZCV" in op:
        return "flags"
    if (
        op in MEM
        or op.startswith("Atomic")
        or op.startswith("LoadMemory")
        or op.startswith("StoreMemory")
    ):
        return "memory"
    if op in IMM:
        return "imm"
    if op in CTRL or op.endswith("Jump") or op.startswith("Branch"):
        return "control"
    if op in XPORT or "Extend" in op or op.startswith("Bit"):
        return "transport"
    if op.startswith(("Vec", "Fps", "Fcmp", "Fmul", "Fadd", "Fsub", "Fdiv", "Fsqrt", "Aes", "Sha")):
        return "vector"
    return "alu"


gap_re = re.compile(r"block=(0x[0-9a-f]+).* op=([A-Za-z0-9_]+) bytes=(\d+)")
hot_re = re.compile(r"pc=(0x[0-9a-f]+).*entries=(\d+).*host_static=(\d+).*move_static=(\d+)")

BENCHES = ["coremark", "stream", "smallpt", "sqlite", "cray", "zip7", "osslsha", "osslaes"]
OUT = os.environ.get("CQ_OUT", "/tmp/svm-linux-cq")
W67 = os.environ.get("W67", "/mnt/mac/Users/swift/CLionProjects/SwiftVM-w67")


def main() -> None:
    print(
        f"{'bench':<10} {'lin h/g':>8} {'pub':>6} {'sread':>6} {'alu':>6} "
        f"{'vec':>6} {'mem':>6} {'imm':>6} {'xport':>6} {'flags':>6} "
        f"{'ctrl':>6} {'gapSum':>7} {'n':>5}"
    )
    for bench in BENCHES:
        guest = {}
        tsv = os.path.join(W67, f"codegen-quality-svm-{bench}.tsv")
        try:
            with open(tsv, encoding="utf-8") as handle:
                for row in csv.DictReader(handle, delimiter="\t"):
                    try:
                        guest[int(row["pc"], 16)] = (
                            int(row["entries"]),
                            int(row["guest_inst"]),
                            int(row["host_static"]),
                        )
                    except (KeyError, TypeError, ValueError):
                        continue
        except OSError as exc:
            print(f"{bench:<10} skip tsv: {exc}")
            continue
        hot = {}
        try:
            with open(os.path.join(OUT, f"{bench}.hot.log"), encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    if "svm-hot-all" not in line:
                        continue
                    match = hot_re.search(line)
                    if not match:
                        continue
                    try:
                        hot[int(match.group(1), 16)] = (
                            int(match.group(2)),
                            int(match.group(3)),
                            int(match.group(4)),
                        )
                    except ValueError:
                        continue
        except OSError as exc:
            print(f"{bench:<10} skip hot: {exc}")
            continue
        blk = collections.defaultdict(lambda: collections.defaultdict(int))
        try:
            with open(os.path.join(OUT, f"{bench}.log"), encoding="utf-8", errors="replace") as handle:
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
                    blk[pc][bucket(match.group(2))] += nbytes
        except OSError as exc:
            print(f"{bench:<10} skip log: {exc}")
            continue
        acc = collections.defaultdict(float)
        guest_weighted = 0.0
        host_weighted = 0.0
        joined = 0
        for pc, (entries, guest_inst, _mac_host) in guest.items():
            if pc not in hot or pc not in blk:
                continue
            joined += 1
            guest_weighted += entries * guest_inst
            host_weighted += entries * hot[pc][1]
            for key, value in blk[pc].items():
                acc[key] += entries * (value / 4.0)

        def hg(value: float) -> float:
            return value / guest_weighted if guest_weighted else 0.0

        keys = [
            "publish",
            "state_read",
            "alu",
            "vector",
            "memory",
            "imm",
            "transport",
            "flags",
            "control",
        ]
        gap_sum = sum(hg(acc[key]) for key in keys)
        print(
            f"{bench:<10} {host_weighted / guest_weighted:8.3f} "
            f"{hg(acc['publish']):6.3f} {hg(acc['state_read']):6.3f} "
            f"{hg(acc['alu']):6.3f} {hg(acc['vector']):6.3f} "
            f"{hg(acc['memory']):6.3f} {hg(acc['imm']):6.3f} "
            f"{hg(acc['transport']):6.3f} {hg(acc['flags']):6.3f} "
            f"{hg(acc['control']):6.3f} {gap_sum:7.3f} {joined:5d}"
        )


if __name__ == "__main__":
    main()
