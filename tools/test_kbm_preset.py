"""Keyboard + Mouse preset table; needs only the SDL3 headers (SDL3_CFLAGS, e.g. "-IC:/msys64/clang64/include", else pkg-config)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sdl = os.environ.get('SDL3_CFLAGS')
cflags = shlex.split(sdl) if sdl else subprocess.run(['pkg-config', '--cflags', 'sdl3'], check=True,
                                                      capture_output=True, text=True).stdout.split()
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('kbm-preset-test.exe' if os.name == 'nt' else 'kbm-preset-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', *cflags, 'tools/test_kbm_preset.c', '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
