"""[perf] summary (runtime/perfstat.c): frame percentiles, process memory, one line per 10 s."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
extra = ['-lpsapi'] if os.name == 'nt' else ['-lpthread']
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('perfstat-test.exe' if os.name == 'nt' else 'perfstat-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-D_FILE_OFFSET_BITS=64', '-Iruntime', 'tools/test_perfstat.c', 'runtime/perfstat.c',
                    '-lm', *extra, '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'perfstat test failed'
    print('perf: percentiles, process memory and the 10 s [perf] line')
