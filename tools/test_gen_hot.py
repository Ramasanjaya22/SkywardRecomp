"""gen.split_hot (the profile-listed functions that get their own -O2 files) and gen.read_hot (the sampler's list)."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen

# 0x40 is too big for a hot file, 0x99 is no function start
hot, cold, missing = gen.split_hot([0x10, 0x20, 0x30, 0x40], {0x20, 0x40, 0x99}, {0x10: 5, 0x20: 5, 0x30: 5, 0x40: 500}, 100)
assert (hot, cold, missing) == ([0x20], [0x10, 0x30, 0x40], 1), (hot, cold, missing)
assert gen.split_hot([0x10], set(), {0x10: 5}, 100) == ([], [0x10], 0)

with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 'hot.txt')
    with open(p, 'w') as f:
        f.write('0x7000001000 90\n# note\n\n0x7000002000 3\nnonsense\n0x7000001000 1\n')
    assert gen.read_hot(p) == {0x7000001000, 0x7000002000}, gen.read_hot(p)
print('gen hot: ok')
