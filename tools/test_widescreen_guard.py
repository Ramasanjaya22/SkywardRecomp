"""Widescreen guard (runtime/hle/widescreen.c): no ultrawide while the game version has no aspect sites."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('widescreen-guard-test.exe' if os.name == 'nt' else 'widescreen-guard-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', '-Iruntime/hle', 'tools/test_widescreen_guard.c', '-lm',
                    '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'widescreen guard test failed'
    print('widescreen: Unlocked stays 16:9 while the game version has no aspect sites')
