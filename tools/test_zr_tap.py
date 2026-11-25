"""ZR tap -> sword jab helper; no game files are used."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    exe = directory / ('zr-tap-test.exe' if os.name == 'nt' else 'zr-tap-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'tools/test_zr_tap.c', '-o', str(exe)],
                   cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
