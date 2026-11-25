"""hot_bin of the hot-function sampler (runtime/hotsample.c) against tools/test_hotsample.c."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('hotsample-test.exe' if os.name == 'nt' else 'hotsample-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-O1', '-DHOTSAMPLE_TEST', '-Iruntime', 'runtime/hotsample.c', 'tools/test_hotsample.c',
                    '-o', str(exe)], cwd=ROOT, check=True)
    r = subprocess.run([str(exe), directory], capture_output=True, text=True)
    print(r.stdout, end='')
    sys.exit(r.returncode)
