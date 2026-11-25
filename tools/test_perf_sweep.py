#!/usr/bin/env python3
"""tools/perf_sweep.py: the stats-line parsers and the A/B summary (no game needed). python tools/test_perf_sweep.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf_sweep  # noqa: E402

VK = ('[vk] t=24.9 frame 308 58.0 fps max 95ms level - (1148 draws, 20 skipped, 242 passes, 0 dispatches, '
      '351 flushes, wait 5ms, compile 5, builds 0 running, fbcopy 0, upload 3.1MB, deferred 0, snap 0KB/0)')
MT = '[metal] t=1.0 frame 60 59.5 fps max 20ms level F000 (100 draws, 0 skipped, 12 passes, gpu 8ms, fbcopy 1)'

assert perf_sweep.parse_stats(VK) == [(308, 58.0, 95, '-', 1148, 20, 242)]
assert perf_sweep.parse_stats(MT) == [(60, 59.5, 20, 'F000', 100, 0, 12)]
assert perf_sweep.parse_stats('no stats here\n') == []
assert perf_sweep.parse_xlate_skips('[vk] draws skipped: translation failure 12.5/s (bindless tex 3)') == 12.5
assert perf_sweep.parse_xlate_skips(VK) == 0.0
assert perf_sweep.parse_xlate_skips('translation failure 2.0/s\ntranslation failure 7.5/s\n') == 7.5

# one room of a log: the settle frames are ignored, the translation failure line belongs to the stats line above it
f0 = perf_sweep.FIRST + perf_sweep.SETTLE + 10
log = (f'[vk] t=1.0 frame {f0} 60.0 fps max 20ms level L (10 draws, 2 skipped, 8 passes, x)\n'
       '[vk] draws skipped: translation failure 4.5/s (bindless tex 2)\n'
       f'[vk] t=1.5 frame {f0 + 30} 50.0 fps max 40ms level L (10 draws, 0 skipped, 8 passes, x)\n')
row = perf_sweep.room_rows(log, [('L', 'loc')])[0]
assert row[:3] == ('L', 'loc', 50.0) and row[3] == 40 and row[6] == 4.5 and row[7] == '', row
assert perf_sweep.room_rows('', [('L', 'loc')])[0][2:] == (None,) * 5 + ('no data',)

# A/B: the median per room, None when a side has no run of the room, cold rooms (still compiling) left out
r = perf_sweep.ab_summary({'A': [{'r1': 50.0}, {'r1': 52.0}], 'B': [{'r1': 55.0}, {'r1': 57.0}]})
assert r == [('r1', 51.0, 56.0, round((56.0 - 51.0) / 51.0 * 100, 1))], r
assert perf_sweep.ab_summary({'A': [{'r1': 50.0}], 'B': [{}]}) == [('r1', 50.0, None, None)]
cold = (f'[vk] t=1.0 frame {f0} 60.0 fps max 20ms level L (10 draws, 0 skipped, 8 passes, wait 1ms, compile 3, x)\n')
warm = cold.replace('compile 3', 'compile 0')
assert perf_sweep.room_fps(cold, [('L', 'loc')]) == {}
assert perf_sweep.room_fps(warm, [('L', 'loc')]) == {'L:loc': 60.0}
# hot lists: one per batch (each batch is its own game instance), summed per address, hottest first, capped
assert perf_sweep.merge_hot(['0x10 5\n0x20 3\n', '0x10 4\nnonsense\n0x30 1\n'], 2) == ['0x10 9', '0x20 3']
assert perf_sweep.merge_hot([], 5) == []
import tempfile  # noqa: E402
with tempfile.TemporaryDirectory() as d:
    for i, t in enumerate(['0x10 5\n', '0x10 1\n0x20 2\n']):
        open(os.path.join(d, f'batch_{i:02d}.hot'), 'w').write(t)
    open(os.path.join(d, 'batch_00.log'), 'w').write('x')  # (not a list)
    out = os.path.join(d, 'merged.txt')
    assert perf_sweep.write_merged_hot(d, out, 10) == 2
    assert open(out).read() == '0x10 6\n0x20 2\n'
print('test_perf_sweep: ok')
