"""The temporal upscaler inputs (issue: FSR 3/4 step 1): tools/test_temporal.c against runtime/nvn/temporal.c. No game, no window."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('temporal-test.exe' if os.name == 'nt' else 'temporal-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-O1', '-w', '-Iruntime', 'runtime/nvn/temporal.c',
                    'tools/test_temporal.c', '-lm', '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
