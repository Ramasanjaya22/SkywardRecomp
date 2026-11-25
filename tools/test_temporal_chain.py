#!/usr/bin/env python3
"""Compile and run the pure-C temporal chain test (no game files needed)."""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
binary = BUILD / "test_temporal_chain"

cmd = [
    "cc", "-std=gnu11", "-O1", "-w", "-Iruntime",
    "runtime/nvn/temporal.c", "runtime/nvn/temporal_chain.c",
    "tools/test_temporal_chain.c", "-lm",
    "-o", str(binary),
]
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
if r.returncode != 0:
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    sys.exit(r.returncode)

r = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
sys.exit(r.returncode)
