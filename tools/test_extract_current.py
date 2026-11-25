"""The launcher keeps an extraction that matches the package (runtime/ui/extract_current.h)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('extract-current-test.exe' if os.name == 'nt' else 'extract-current-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'tools/test_extract_current.c', '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'extract_current test failed'
    print('extract_current: same package kept, changed or cut-short extraction redone')
