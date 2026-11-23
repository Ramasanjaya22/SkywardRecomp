"""Game-file detection (runtime/game_find.c): the right .nsp is chosen among updates, DLC, other games, damaged files and unrelated
files; extracted dumps are found; every skipped file is explained. Builds tools/test_game_find.c with the real game_find.c in a
scratch folder. GAME_FIND_LEGACY=1 runs the same checks against the first version's logic (they fail there)."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    exe = directory / ('game-find-test.exe' if os.name == 'nt' else 'game-find-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Wall', '-Iruntime', 'runtime/game_find.c', 'tools/test_game_find.c', '-o', str(exe)],
                   cwd=ROOT, check=True)
    scratch = directory / 'scratch'
    scratch.mkdir()
    run = subprocess.run([str(exe), str(scratch)], capture_output=True, text=True)
    print(run.stdout + run.stderr, end='')
    if os.environ.get('GAME_FIND_LEGACY'):
        sys.exit(run.returncode)
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'game detection test failed'
    print('the right game package is found among unrelated, update, DLC and damaged files')
