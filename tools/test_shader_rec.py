"""Shader variant record text-form regression checks; no game files are used."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'runtime/nvn/shader_prep.c').read_text()
start = source.index('// ------------------------------------------------------------------ text form')
end = source.index('// ------------------------------------------------------------------ recorder', start)
form = source[start:end]

with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    exe = directory / ('shader-rec-test.exe' if os.name == 'nt' else 'shader-rec-test')
    src = directory / 'form.c'
    src.write_text('#include <inttypes.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "nvn/shader_prep.h"\n' + form)
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', str(src), 'tools/test_shader_rec.c',
                    '-o', str(exe), '-lm'], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
