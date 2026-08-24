#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import time


PROFILE_ENV = (
    "SVM_JIT_CACHE",
    "SVM_RA_SHAPE_PROF",
    "SVM_RA_DIAG",
    "SVM_EXEC_PROF",
    "SVM_EXEC_TRACE",
    "SVM_DENSITY_PROF",
    "SVM_INDIRECT_L1_PROF",
    "SVM_DECODE_PROF",
    "SVM_SIGNAL_TRACE",
    "SVM_MEM_MODE_TRACE",
    "SVM_PROF",
    "SVM_PROF2",
    "SVM_RA_HOT_COALESCE",
    "SVM_RA_HOT_COALESCE_ALL",
    "SVM_VIXL_HOST_DUMP",
)

HOST_DUMP_LINE = re.compile(
    rb"^\[svm-host\]\s+pc=(0x[0-9a-f]+)\s+size=(\d+)\s+",
    re.MULTILINE,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture candidate code shape without detailed density logging."
    )
    parser.add_argument("--svm", required=True, type=pathlib.Path)
    parser.add_argument("--guest", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--oracle", action="append", default=[])
    parser.add_argument(
        "--static-only",
        action="store_true",
        help="capture host shape without emitting runtime entry counters",
    )
    parser.add_argument("guest_args", nargs=argparse.REMAINDER)
    return parser.parse_args()


def ensure_output_directory(path: pathlib.Path) -> None:
    if path.exists() and any(path.iterdir()):
        raise ValueError(f"output directory is not empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_static_shape(stderr_path: pathlib.Path, hot_path: pathlib.Path) -> int:
    versions: dict[int, list[int]] = {}
    for match in HOST_DUMP_LINE.finditer(stderr_path.read_bytes()):
        pc = int(match.group(1), 0)
        versions.setdefault(pc, []).append(int(match.group(2)))
    with hot_path.open("w", encoding="utf-8") as handle:
        for pc, sizes in sorted(versions.items()):
            host_bytes = max(sizes)
            if host_bytes % 4:
                raise ValueError(f"unaligned host code size for 0x{pc:x}: {host_bytes}")
            handle.write(
                f"[svm-hot-all] pc=0x{pc:x} versions={len(sizes)} entries=1 "
                f"host_bytes={host_bytes} host_static={host_bytes // 4} "
                "move_static=0 nan_static=0 spill_static=0 state_saved_static=0\n"
            )
    return len(versions)


def main() -> int:
    args = parse_args()
    svm = args.svm.resolve()
    guest = args.guest.resolve()
    output = args.out.resolve()
    if args.timeout <= 0:
        raise ValueError("--timeout must be positive")
    if not svm.is_file():
        raise ValueError(f"SVM executable does not exist: {svm}")
    if not guest.is_file():
        raise ValueError(f"guest executable does not exist: {guest}")
    ensure_output_directory(output)

    hot_path = output / "shape.hot"
    stdout_path = output / "stdout.log"
    stderr_path = output / "stderr.log"
    hot_path.unlink(missing_ok=True)
    environment = os.environ.copy()
    for name in PROFILE_ENV:
        environment.pop(name, None)
    if args.static_only:
        environment["SVM_VIXL_HOST_DUMP"] = "1"
    else:
        environment["SVM_RA_HOT_COALESCE"] = str(hot_path)
        environment["SVM_RA_HOT_COALESCE_ALL"] = "1"

    guest_args = args.guest_args[1:] if args.guest_args[:1] == ["--"] else args.guest_args
    command = [str(svm), str(guest), *guest_args]
    started = time.monotonic()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        try:
            completed = subprocess.run(
                command,
                cwd=output,
                env=environment,
                stdout=stdout,
                stderr=stderr,
                timeout=args.timeout,
                check=False,
            )
            return_code = completed.returncode
        except subprocess.TimeoutExpired:
            return_code = 124
    elapsed = time.monotonic() - started

    hot_records = write_static_shape(stderr_path, hot_path) if args.static_only else 0
    summary = ""
    if not args.static_only and hot_path.is_file():
        with hot_path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                hot_records += line.startswith("[svm-hot-all]")
                if line.startswith("[svm-hot-coalesce]"):
                    summary = line.rstrip()

    print(
        f"rc={return_code} elapsed={elapsed:.3f}s hot_records={hot_records} "
        f"stderr_bytes={stderr_path.stat().st_size}"
    )
    if summary:
        print(summary)
    oracle_missing = False
    for relative in args.oracle:
        oracle = output / relative
        if not oracle.is_file():
            print(f"oracle_missing={relative}")
            oracle_missing = True
            continue
        print(f"oracle={relative} sha256={sha256(oracle)} bytes={oracle.stat().st_size}")

    if return_code != 0:
        return return_code
    if hot_records == 0 or oracle_missing:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(2) from exc
