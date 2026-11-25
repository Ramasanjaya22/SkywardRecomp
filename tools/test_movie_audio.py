"""Movie audio (runtime/hle/movie.c): mime per codec, s16 for every sample format, on synthetic webm (no game files)."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
libs = subprocess.run(['pkg-config', '--cflags', '--libs', 'libavformat', 'libavcodec', 'libavutil', 'libswscale'],
                      capture_output=True, text=True, check=True).stdout.split()
with tempfile.TemporaryDirectory() as directory:
    names = []
    encoders = subprocess.run(['ffmpeg', '-hide_banner', '-encoders'], capture_output=True, text=True).stdout if shutil.which('ffmpeg') else None
    for name, codec in (('vorbis.webm', 'libvorbis'), ('opus.webm', 'libopus')):
        if encoders is None or codec not in encoders:
            print(f'movie audio: SKIP {name}, ffmpeg or its {codec} encoder missing (synthetic formats still run)')
            continue
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1:sample_rate=48000',
                        '-ac', '2', '-c:a', codec, str(Path(directory) / name)], check=True)
        names.append(name)
    exe = Path(directory) / ('movie-audio-test.exe' if os.name == 'nt' else 'movie-audio-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-O1', 'tools/test_movie_audio.c'] + libs +
                   ([] if os.name == 'nt' else ['-pthread']) + ['-lm', '-o', str(exe)], cwd=ROOT, check=True)
    run = subprocess.run([str(exe), directory] + names, capture_output=True, text=True)
    print(run.stderr + run.stdout, end='')
    assert run.returncode == 0 and 'PASS' in run.stdout, 'movie audio test failed'
