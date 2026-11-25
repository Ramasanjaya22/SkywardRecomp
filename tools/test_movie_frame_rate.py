"""Game-free regression test for movie track frame-rate metadata.

The guest paces cutscene pictures with the integer `frame-rate' it gets from
the movie extractor. NTSC rates are rational (30000/1001, 24000/1001, ...);
truncating them makes the picture clock slower than the audio clock, so voices
and SFX drift out of sync for the duration of a cutscene.
"""
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_movie_frame_rate_rounds_ntsc_rates():
    if not shutil.which("clang++"):
        print("movie frame-rate: SKIP clang++ not found on PATH (MSYS2 UCRT64)")
        return
    libs = subprocess.run(
        ["pkg-config", "--cflags", "--libs", "libavformat", "libavcodec", "libavutil", "libswresample", "libswscale"],
        capture_output=True, text=True, check=True,
    ).stdout.split()
    with tempfile.TemporaryDirectory() as directory:
        exe = Path(directory) / ("movie-frame-rate-test.exe" if os.name == "nt" else "movie-frame-rate-test")
        subprocess.run(
            shlex.split(os.environ.get("CC", "clang++")) +
            ["-std=gnu17", "-I.", "-x", "c", "tools/test_movie_frame_rate.c"] + libs +
            ["-lm", "-o", str(exe)],
            cwd=ROOT, check=True,
        )
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stderr + run.stdout, end="")
        assert run.returncode == 0 and "frame-rate metadata: ok" in run.stdout


if __name__ == "__main__":
    test_movie_frame_rate_rounds_ntsc_rates()
