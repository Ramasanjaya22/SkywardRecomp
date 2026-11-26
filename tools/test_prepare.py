"""--prepare (RecompLauncher's setup step, runtime/ui/launcher.mm launcher_prepare) on the built app, with no game
files: it runs without a window in a scratch data folder, writes its "progress <0..1> <step>" lines and its reason to
the stdout it was started with (even where the session log takes stdout over), and exits 2 / 1 as documented. The
player's own data folder is never touched.
Run from the repo root after `make build/skywardrecomp` (or .exe): python3 tools/test_prepare.py (DEPS=<dir>: the SDL3
the app was built against, as for make)."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
exe = root/'build'/('skywardrecomp.exe' if os.name == 'nt' else 'skywardrecomp')
assert exe.exists(), f'{exe}: build it first'


def prepare(data, *args):
    env = dict(os.environ, LARECOMP_DATA_ROOT=str(data), LARECOMP_NO_UPDATE_CHECK='1')
    if os.environ.get('DEPS') and os.name != 'nt':  # (the Linux build has no rpath for it)
        env['LD_LIBRARY_PATH'] = os.pathsep.join(filter(None, [os.environ['DEPS'] + '/lib', env.get('LD_LIBRARY_PATH')]))
    env.pop('LASRECOMP_LOG_FILE', None)
    env.pop('LASRECOMP_LOG_APPEND', None)
    p = subprocess.run([str(exe), '--prepare', *args], env=env, cwd=data, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, timeout=120)  # (stderr: no terminal, as from a launcher)
    return p.returncode, p.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')


with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    data = t/'data'
    data.mkdir()

    # nothing given, nothing set up
    rc, out = prepare(data)
    assert rc == 2 and 'No game files set up yet' in out, (rc, out)

    # a folder without the game: the start screen's own failure (quick: often before the first progress line)
    empty = t/'empty'
    empty.mkdir()
    rc, out = prepare(data, f'--game={empty}')
    assert rc == 1, (rc, out)
    assert 'No game files found' in out, out  # (the headline; its hint says 'in that folder')
    outs = [out]

    # a package that isn't a base .nsp, given as a separate argument (not taken as the game folder argument)
    xci = t/'game.xci'
    xci.write_bytes(b'\0' * 64)
    rc, out = prepare(data, '--game', str(xci), '--keys', str(t/'missing.keys'))
    assert rc == 1 and 'Game files are packaged' in out, (rc, out)
    outs.append(out)

    # every progress line has the launcher's shape
    for line in '\n'.join(outs).splitlines():
        if line.startswith('progress'):
            assert re.fullmatch(r'progress (0|1)(\.\d+)? .+', line), line
    assert not (data/'extracted').exists(), 'a failed setup left game files'

print('PASS: --prepare without a copy (exit 2), an empty folder and an .xci (exit 1), their reasons on stdout')
