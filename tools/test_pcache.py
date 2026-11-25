"""When the pipeline cache is saved while playing (runtime/nvn/vulkan/pcache_policy.h)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('pcache-test.exe' if os.name == 'nt' else 'pcache-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'tools/test_pcache.c', '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'pcache test failed'
    print('pcache: saved at +1 MB or after a minute, never empty, never smaller')
