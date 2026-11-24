"""Frame rate setting (runtime/hle/framepace.c): the average and timestep SS HD's frame controller is handed."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    exe = Path(directory) / ('fps-target-test.exe' if os.name == 'nt' else 'fps-target-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', '-Iruntime/hle', 'tools/test_fps_target.c', '-lm',
                    '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'all checks passed' in run.stdout, 'frame rate target test failed'
    print('fps: Auto / 30 / 60 / 90 / 120 / Unlocked hand the controller the right interval and timestep')
