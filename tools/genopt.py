"""Which lifted functions stay unoptimized (see tools/test_gen_opt.py).

The game code is compiled at -O2, but LLVM's time on one huge function is far from linear (a 4 MB one took more than 45
minutes at -O1), so a function of more than NOOPT_FN bytes of C is marked GEN_BIG (runtime/cpu.h: optnone, noinline) and
stays at -O0 like the whole code did before. Measured on 48 random generated files (8 jobs): -O0 65 s, -O2 with this
limit 93 s (1.4x; 50000: 1.85x; no limit: 4.7x, files timing out).
"""
NOOPT_FN = 30000  # bytes of C


def mark_big(lines, limit=NOOPT_FN):
    """(lines, whether the function is big): a big function's first line (its definition) starts with GEN_BIG."""
    if sum(len(l) + 1 for l in lines) <= limit:
        return lines, False
    if not lines[0].startswith('GEN_BIG '):
        lines[0] = 'GEN_BIG ' + lines[0]
    return lines, True
