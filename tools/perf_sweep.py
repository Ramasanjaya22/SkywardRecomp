#!/usr/bin/env python3
"""Performance sweep: warp to every room in the game and record frame stats.

  tools/perf_sweep.py run      warp through build/sweep_targets.json in batches (one game instance per batch,
                               fresh scratch save), logs in build/sweep/batch_NN.log
  tools/perf_sweep.py report   per-room table -> build/sweep/report.txt (rooms under 58 fps or with long frames)
  tools/perf_sweep.py ab <exeA> <exeB> [rooms=8] [rounds=2]   A,B,A,B on the first rooms -> build/sweep/ab.txt

Each room gets STEP frames: the first SETTLE frames (load, fade) are ignored, the rest are averaged from the
LARECOMP_FPS_INTERVAL=0.5 "[vk]" (or "[metal]") lines (fps, max frame ms, passes, skipped draws) and the
"translation failure N/s" lines. LARECOMP_* variables already set (LARECOMP_BINDLESS_LOG=1 ...) reach the game.
With LARECOMP_HOTLIST_OUT=<file> every batch writes its own build/sweep/batch_NN.hot (runtime/hotsample.c) and `run`
sums them into <file>.
"""
import json
import os
import re
import statistics
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'build', 'sweep')
FIRST, STEP, SETTLE, BATCH = 2700, 360, 150, 40
TITLE_INPUT = '400:A:6,520:A:6,900:ZL:8,900:ZR:8,1300:A:6,1600:A:6,1900:A:6,2200:A:6'


STATS = re.compile(r'\[(?:metal|vk)\] t=[\d.]+ frame (\d+) ([\d.]+) fps max (\d+)ms level (\S+) \((\d+) draws, (\d+) skipped, '
                   r'(\d+) passes')
XLATE = re.compile(r'translation failure ([\d.]+)/s')


def parse_stats(text):
    """The "[metal]" / "[vk]" stats lines -> (frame, fps, max frame ms, level, draws, skipped, passes)."""
    return [(int(m[0]), float(m[1]), int(m[2]), m[3], int(m[4]), int(m[5]), int(m[6])) for m in STATS.findall(text)]


def parse_xlate_skips(text):
    """The largest "draws skipped: translation failure N/s" rate in the log (0.0 without one)."""
    return max((float(v) for v in XLATE.findall(text)), default=0.0)


HOT = re.compile(r'^(0x[0-9a-fA-F]+)\s+(\d+)\s*$')


def merge_hot(texts, limit):
    """Hot lists ("0x<guest address> <samples>" lines, one text per game instance) summed per address: the `limit`
    hottest as lines, highest first. shortcut: each list is already the top of its instance, so the sums are approximate."""
    total = {}
    for text in texts:
        for ln in text.splitlines():
            m = HOT.match(ln)
            if m:
                total[int(m[1], 16)] = total.get(int(m[1], 16), 0) + int(m[2])
    return [f'{a:#x} {n}' for a, n in sorted(total.items(), key=lambda kv: (-kv[1], kv[0]))[:limit]]


def write_merged_hot(directory, dest, limit=400):
    """directory/batch_*.hot -> dest (one profile of the whole sweep). Returns the number of lines written."""
    texts = [open(os.path.join(directory, f), errors='replace').read()
             for f in sorted(os.listdir(directory)) if f.startswith('batch_') and f.endswith('.hot')]
    lines = merge_hot(texts, limit)
    open(dest, 'w').write(''.join(ln + '\n' for ln in lines))
    return len(lines)


def exe_path():
    exe = os.path.join(ROOT, 'build', 'skywardrecomp')
    return exe + '.exe' if os.name == 'nt' else exe


def run_batch(exe, chunk, log, extra_env=None):
    """One game instance warping through chunk; log gets its output. Returns the index of the crashing target in
    chunk, or None. LARECOMP_* variables already in the environment pass through (LARECOMP_BINDLESS_LOG=1 ...)."""
    warps = ','.join(f'{FIRST + i * STEP}:{lv}:{loc}' for i, (lv, loc) in enumerate(chunk))
    subprocess.run(['bash', os.path.join(ROOT, 'tools', 'fresh_save.sh'), 'testsave_sweep'], cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, LARECOMP_SAVE_DIR='build/testsave_sweep', LARECOMP_INPUT=TITLE_INPUT,
               LARECOMP_WARP_AT=warps, LARECOMP_FPS_INTERVAL='0.5', LARECOMP_WINDOW='1280x720',
               LARECOMP_NO_LAUNCHER='1')  # (never the start screen: runtime/main.c)
    if os.environ.get('LARECOMP_HOTLIST_OUT'):  # one list per game instance (they'd overwrite each other); run() merges them
        env['LARECOMP_HOTLIST_OUT'] = os.path.splitext(log)[0] + '.hot'
    env.update(extra_env or {})
    end =FIRST + len(chunk) * STEP + 60
    crashed_at = None
    with open(log, 'w') as f:
        p = subprocess.Popen([exe, 'extracted'], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        t0 = time.time()
        while p.poll() is None and time.time() - t0 < end / 60 * 1.6 + 120:
            time.sleep(3)
            text = open(log, errors='replace').read()
            if 'FATAL' in text or '*** signal' in text:
                jumps = re.findall(r'jumping to (\S+) / (\S+)', text)
                crashed_at = len(jumps) - 1
                break
            fr = [int(m) for m in re.findall(r'\] t=[\d.]+ frame (\d+)', text[-4000:])]
            if fr and fr[-1] >= end:
                break
        if p.poll() is None:
            p.kill()
            p.wait(10)
    return crashed_at


def run():
    """Warp through all targets; one game instance per batch. A crash (FATAL / signal) ends that instance at once:
    the crashing target is recorded in crashes.json and the batch resumes from the next target."""
    targets = json.load(open(os.path.join(ROOT, 'build', 'sweep_targets.json')))
    os.makedirs(OUT, exist_ok=True)
    crash_file = os.path.join(OUT, 'crashes.json')
    crashes = json.load(open(crash_file)) if os.path.exists(crash_file) else []
    done = set()
    for f in os.listdir(OUT):
        if f.endswith('.json') and f.startswith('batch_'):
            log = os.path.join(OUT, f[:-5] + '.log')
            if os.path.exists(log):
                for i, t in enumerate(json.load(open(os.path.join(OUT, f)))):
                    done.add(tuple(t))  # rerun policy: a batch file means those targets were attempted
    todo = [t for t in targets if tuple(t) not in done and [*t] not in [c[:2] for c in crashes]]
    n = len([f for f in os.listdir(OUT) if f.startswith('batch_') and f.endswith('.json')])
    while todo:
        chunk = todo[:BATCH]
        log = os.path.join(OUT, f'batch_{n:02d}.log')
        crashed_at = run_batch(exe_path(), chunk, log)
        if crashed_at is not None and 0 <= crashed_at < len(chunk):
            crashes.append(list(chunk[crashed_at]) + [os.path.basename(log)])
            json.dump(crashes, open(crash_file, 'w'), indent=1)
            chunk = chunk[:crashed_at]  # targets measured before the crash
            print(f'batch {n}: crash at {crashes[-1][:2]}', flush=True)
            todo = todo[crashed_at + 1:]
        else:
            todo = todo[len(chunk):]
        json.dump(chunk, open(os.path.join(OUT, f'batch_{n:02d}.json'), 'w'))
        print(f'batch {n}: {len(chunk)} rooms measured', flush=True)
        n += 1
    dest = os.environ.get('LARECOMP_HOTLIST_OUT')
    if dest:
        print(f'{write_merged_hot(OUT, dest, int(os.environ.get("LARECOMP_HOTLIST_N") or 400))} hot functions -> {dest}', flush=True)


def xlate_by_frame(text):
    """(frame, rate) for each "translation failure N/s" line, the frame being the last stats line above it."""
    out, frame = [], 0
    for ln in text.splitlines():
        m = STATS.search(ln)
        if m:
            frame = int(m[1])
        m = XLATE.search(ln)
        if m:
            out.append((frame, float(m[1])))
    return out


def room_rows(text, chunk):
    """One row per room of a batch log: (level, locator, min fps, max frame ms, passes/frame, skipped/frame,
    translation failures/s, note); the stats are None for a room with no data."""
    lines = parse_stats(text)
    xl = xlate_by_frame(text)
    crashed = 'signal ' in text
    rows = []
    for i, (lv, loc) in enumerate(chunk):
        a, b = FIRST + i * STEP + SETTLE, FIRST + (i + 1) * STEP - 30  # end before the next warp (its unload hitch)
        s = [l for l in lines if a <= l[0] < b]
        if not s:
            rows.append((lv, loc, None, None, None, None, None, 'no data' + (' (crash)' if crashed else '')))
            continue
        per = lambda k: sum(l[k] for l in s) / sum(max(l[1] * 0.5, 1) for l in s)  # per frame
        level = s[-1][3]
        rows.append((lv, loc, min(l[1] for l in s), max(l[2] for l in s), per(6), per(5),
                     max((r for fr, r in xl if a <= fr < b), default=0.0), '' if level == lv else f'(level {level})'))
    return rows


def report():
    rows = []
    n = 0
    while os.path.exists(os.path.join(OUT, f'batch_{n:02d}.json')):
        chunk = json.load(open(os.path.join(OUT, f'batch_{n:02d}.json')))
        rows += room_rows(open(os.path.join(OUT, f'batch_{n:02d}.log'), errors='replace').read(), chunk)
        n += 1
    rows.sort(key=lambda r: (r[2] is None, r[2] if r[2] is not None else 0))
    with open(os.path.join(OUT, 'report.txt'), 'w') as f:
        f.write(f'{"level":22} {"locator":34} {"minfps":>6} {"maxms":>5} {"passes/f":>8} {"skip/f":>6} {"xlate/s":>7}\n')
        for lv, loc, fps, mx, pa, sk, xs, note in rows:
            if fps is None:
                f.write(f'{lv:22} {loc:34} {note}\n')
            else:
                f.write(f'{lv:22} {loc:34} {fps:6.1f} {mx:5d} {pa:8.1f} {sk:6.1f} {xs:7.1f} {note}\n')
    slow = [r for r in rows if r[2] is not None and (r[2] < 58 or r[3] > 25)]
    print(f'{len(rows)} rooms, {len(slow)} slow or hitchy, {sum(1 for r in rows if r[2] is None)} without data')


COMPILE = re.compile(r'\[vk\] t=[\d.]+ frame (\d+) .*?compile (\d+)')


def room_fps(text, chunk):
    """{'level:locator': min fps} per room of one batch log; a room that was still compiling shaders (a "compile N" above
    0 in its window) or has no data is left out: its fps measures the cache, not the code."""
    busy = [int(m[0]) for m in COMPILE.findall(text) if int(m[1]) > 0]
    out = {}
    for i, (lv, loc, fps, *_) in enumerate(room_rows(text, chunk)):
        a, b = FIRST + i * STEP + SETTLE, FIRST + (i + 1) * STEP - 30
        if fps is not None and not any(a <= f < b for f in busy):
            out[f'{lv}:{loc}'] = fps
    return out


def ab_summary(runs):
    """runs {'A': [per-run {room: fps}], 'B': [...]} -> [(room, median A, median B, B vs A in %)] by room name; a side
    with no run of a room gives None."""
    med = lambda side, room: statistics.median(v[room] for v in runs[side] if room in v) if any(room in v for v in runs[side]) else None
    out = []
    for room in sorted({r for side in runs.values() for v in side for r in v}):
        a, b = med('A', room), med('B', room)
        out.append((room, a, b, None if a is None or b is None else round((b - a) / a * 100, 1)))
    return out


def ab(exe_a, exe_b, rooms=8, rounds=2):
    """A/B two builds (copies of build/ with their own exe and game library) on the first rooms of the sweep targets:
    one warm-up run of each (caches fill, result thrown away), then A,B,A,B... for `rounds` rounds, a fresh save each
    run. build/sweep/ab.txt gets the median min fps per room (noise floor: run A against a copy of A first)."""
    chunk = json.load(open(os.path.join(ROOT, 'build', 'sweep_targets.json')))[:rooms]
    os.makedirs(OUT, exist_ok=True)
    runs = {'A': [], 'B': []}
    for n, (side, exe) in enumerate([('A', exe_a), ('B', exe_b)] * (rounds + 1)):
        log = os.path.join(OUT, f'ab_{side}_{n // 2}.log')
        run_batch(exe, chunk, log)
        if n >= 2:  # (the first pair is the warm-up)
            runs[side].append(room_fps(open(log, errors='replace').read(), chunk))
    with open(os.path.join(OUT, 'ab.txt'), 'w') as f:
        f.write(f'{"room":58} {"A fps":>7} {"B fps":>7} {"B vs A":>8}\n')
        for room, a, b, pct in ab_summary(runs):
            f.write(f'{room:58} {a if a is None else format(a, "7.1f"):>7} {b if b is None else format(b, "7.1f"):>7} '
                    f'{"" if pct is None else format(pct, "+7.1f") + "%":>8}\n')
    print(open(os.path.join(OUT, 'ab.txt')).read())


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'run'
    if cmd == 'ab':  # ab <exeA> <exeB> [rooms] [rounds]
        ab(sys.argv[2], sys.argv[3], *map(int, sys.argv[4:6]))
    else:
        {'run': run, 'report': report}[cmd]()
