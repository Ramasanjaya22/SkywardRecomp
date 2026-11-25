"""Mouse state machine (camera, slash flick, thrust, aim); no game files are used."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    exe = directory / ('mouse-sword-test.exe' if os.name == 'nt' else 'mouse-sword-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'tools/test_mouse_sword.c', '-o', str(exe)],
                   cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
