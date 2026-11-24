#!/usr/bin/env python3
"""The game code is built at -O2 (GENFLAGS) but a function of more than NOOPT_FN bytes of C stays -O0 (GEN_BIG =
optnone): LLVM's time on those blows up (main_513.c, one 4 MB function, took more than 45 min at -O1), while all the
smaller ones together build in about 1.4x the -O0 time. tools/genopt.py marks them; no game files needed.

  python3 tools/test_gen_opt.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from genopt import NOOPT_FN, mark_big  # noqa: E402

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print('FAIL', msg)


small = ['void f_7000000000(Cpu *c) {', '    FN_ENTER(0x7000000000);', '}']
big = ['void f_7000001000(Cpu *c) {'] + ['    c->x[0] += 1;'] * (NOOPT_FN // 10) + ['}']
check(sum(len(l) + 1 for l in big) > NOOPT_FN, 'the test function is above the limit')
check(sum(len(l) + 1 for l in small) <= NOOPT_FN, 'the small test function is under it')
check(mark_big(list(small)) == (small, False), 'a small function is left as it is')
marked, was_big = mark_big(list(big))
check(was_big and marked[0] == 'GEN_BIG ' + big[0] and marked[1:] == big[1:], 'a big function starts with GEN_BIG')
check(mark_big(list(big), limit=10 ** 9)[1] is False, 'the limit is an argument')
check(mark_big(['GEN_BIG void f(Cpu *c) {'] + big[1:])[0][0].count('GEN_BIG') == 1, 'never marked twice')

# the flags: one list for make and for the player's build (runtime/gamebuild.c), the semantics ones always
need = ['-O2', '-fwrapv', '-ffp-contract=off', '-fno-strict-aliasing']
mk = open(os.path.join(ROOT, 'Makefile')).read()
genflags = re.search(r'^GENFLAGS := (.*)$', mk, re.M).group(1)
gb = open(os.path.join(ROOT, 'runtime', 'gamebuild.c')).read()
gamelib = re.search(r'#define GAMELIB_CFLAGS "([^"]*)"', gb).group(1)
for f in need:
    check(f in genflags.split(), f'Makefile GENFLAGS has {f}')
    check(f in gamelib.split(), f'gamebuild.c GAMELIB_CFLAGS has {f}')
check('-O0' not in genflags.split() and '-O1' not in gamelib.split(), 'no leftover -O0 / -O1')

cpu = open(os.path.join(ROOT, 'runtime', 'cpu.h')).read()
check(re.search(r'#define GEN_BIG\s+__attribute__\(\(optnone,\s*noinline\)\)', cpu) is not None, 'cpu.h defines GEN_BIG')

if fails:
    sys.exit(1)
print('all checks passed')
