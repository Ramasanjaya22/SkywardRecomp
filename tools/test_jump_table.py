#!/usr/bin/env python3
"""gen.py resolves a bounds-checked `br xN` jump table with either SIMD lowering. On AArch64 hosts (--simd asm) an FP
load between the cmp and its b.hi lifts as 'asm', not 'op'; the guard scan used to give up on it, the br fell through
to dispatch and the game stopped with "no function at <case address>" (#19). Synthetic code, no game files needed.

  python3 tools/test_jump_table.py   (needs unicorn)
"""
import os
import struct
import sys
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import Module  # noqa: E402
from lifter import Lifter  # noqa: E402

B = 0x100000
CODE = [
    0x71000AFF,  # 00 cmp w23, #2
    0xBD400121,  # 04 ldr s1, [x9]      (FP load inside the guard window)
    0x54000148,  # 08 b.hi 0x30
    0x10000069,  # 0c adr x9, 0x18
    0x8B170929,  # 10 add x9, x9, x23, lsl #2
    0xD61F0120,  # 14 br x9
    0x14000006,  # 18 b 0x30            (case 0)
    0x14000005,  # 1c b 0x30            (case 1)
    0x14000004,  # 20 b 0x30            (case 2)
    0xD503201F,  # 24 nop
    0xD503201F,  # 28 nop
    0xD503201F,  # 2c nop
    0xD65F03C0,  # 30 ret
]


def module(simd):
    m = Module.__new__(Module)
    image = bytearray(0x1000)
    image[:4 * len(CODE)] = struct.pack(f'<{len(CODE)}I', *CODE)
    m.name, m.base, m.nso = 'test', B, SimpleNamespace(image=image)
    m.lo, m.hi, m.words = B, B + 4 * len(CODE), CODE
    m.lifter, m.cache, m.mu = Lifter(B, simd), {}, None
    m.stats = {}
    return m


bad = 0
for simd in ('c', 'asm'):
    got = module(simd).jump_table(B + 0x14, B, B + 4 * len(CODE))
    want = [B + 0x18, B + 0x1c, B + 0x20]
    ok = got == want
    bad += not ok
    print(f'{"ok  " if ok else "FAIL"} simd={simd}: {got and [hex(t) for t in got]}')
sys.exit(1 if bad else 0)
