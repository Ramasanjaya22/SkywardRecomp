"""Save data backups (runtime/save_backup.c): create, list, restore (with an automatic backup first), import, the
newest 20 kept, and a restore refused - saves untouched - while a file is held open (Windows).
Builds tools/test_save_backup.c with the real save_backup.c and paths.c; scratch folders only.
Layout as the game writes it: save/save_N/savedataN.bin, rando/<seed>/save/save_N/savedataN.bin."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
plat = 'windows' if os.name == 'nt' else 'macos' if sys.platform == 'darwin' else 'linux'
flags = []
deps = os.environ.get('DEPS')  # (a prefix with SDL3, as for make DEPS=)
if deps:
    flags = [f'-I{deps}/include', f'-L{deps}/lib', f'-Wl,-rpath,{deps}/lib', '-lSDL3']
elif shutil.which('pkg-config') and subprocess.run(['pkg-config', '--exists', 'sdl3']).returncode == 0:
    flags = shlex.split(subprocess.run(['pkg-config', '--cflags', '--libs', 'sdl3'], capture_output=True, text=True,
                                       check=True).stdout)
elif sys.platform == 'darwin':
    brew = subprocess.run(['brew', '--prefix'], capture_output=True, text=True).stdout.strip() or '/opt/homebrew'
    flags = [f'-I{brew}/include', f'-L{brew}/lib', '-lSDL3']
extra = {'macos': ['-framework', 'CoreFoundation'], 'linux': ['-pthread', '-ldl'],
         'windows': ['-D_FILE_OFFSET_BITS=64', '-D_POSIX_THREAD_SAFE_FUNCTIONS', '-D_CRT_RAND_S',
                     '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-lwinmm', '-lole32', '-lshell32', '-lcomdlg32', '-luuid', '-ladvapi32']}[plat]

SAVE = 'save/save_0/savedata0.bin'
SEED = 'rando/My Seed/save/save_0/savedata0.bin'
SIZE = 300_000


def put(root: Path, rel, data):
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def files(root: Path, top):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted((root / top).rglob('*')) if p.is_file()}


with tempfile.TemporaryDirectory() as directory:
    S = Path(directory).resolve()
    exe = S / ('save-backup-test.exe' if os.name == 'nt' else 'save-backup-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'runtime/save_backup.c', 'runtime/paths.c', f'runtime/platform/{plat}.c',
                    'tools/test_save_backup.c', '-o', str(exe)] + flags + extra, cwd=ROOT, check=True)
    data = S / 'data'

    def run(*args):
        p = subprocess.run([str(exe), str(data), *map(str, args)], capture_output=True, text=True, timeout=120)
        assert p.returncode == 0, f'{args}: exit {p.returncode}\n{p.stderr}'
        return p.stdout.splitlines()

    def backups():
        return [l.split() for l in run('list') if l.startswith('BACKUP')]

    def ok_of(line, tag):
        assert line.startswith(tag + ' '), line
        flag, why = line[len(tag) + 1:].split('|', 1)
        return flag.split()[0] == '1', why

    a, b = os.urandom(SIZE), os.urandom(SIZE)
    put(data, SAVE, a)
    put(data, 'save/save_0/savedata0.bin.flagsx', b'flags')
    put(data, SEED, b)
    put(data, 'skywardrecomp.ini', b'x' * 100)
    want = files(data, 'save') | files(data, 'rando')

    # nothing to back up
    empty = S / 'none'
    empty.mkdir()
    assert subprocess.run([str(exe), str(empty), 'create'], capture_output=True, text=True).stdout.startswith('CREATE 0 |')

    # create: one backup, with the right byte count and the same relative paths
    ok, why = ok_of(run('create')[0], 'CREATE')
    assert ok, why
    lst = backups()
    assert len(lst) == 1, lst
    name = lst[0][1]
    assert int(lst[0][3]) == SIZE * 2 + 5, lst
    assert (data / 'backups' / name / SAVE).read_bytes() == a
    assert (data / 'backups' / name / SEED).read_bytes() == b
    assert not (data / 'backups' / name / 'skywardrecomp.ini').exists()
    print('create: save/ and rando/*/save/ copied with their paths, bytes right')

    # restore: the old content is back, and an -auto backup of what was replaced
    put(data, SAVE, os.urandom(SIZE))
    put(data, 'save/save_1/savedata1.bin', b'new slot')
    changed = (data / SAVE).read_bytes()
    ok, why = ok_of(run('restore', name)[0], 'RESTORE')
    assert ok, why
    assert files(data, 'save') | files(data, 'rando') == want
    auto = [x for x in backups() if x[1].endswith('-auto')]
    assert len(auto) == 1 and (data / 'backups' / auto[0][1] / SAVE).read_bytes() == changed, backups()
    assert not list(data.rglob('*.restoring')) and not list(data.rglob('*.replaced'))
    print('restore: content back, an -auto backup of the replaced saves, no leftovers')

    # import: an empty folder, a wrong-size save, a right one
    before = files(data, 'save')
    (S / 'emptydir').mkdir()
    ok, why = ok_of(run('import', S / 'emptydir')[0], 'IMPORT')
    assert not ok and why and files(data, 'save') == before, why
    put(S / 'bad', 'zelda.sav', os.urandom(SIZE - 1))
    ok, why = ok_of(run('import', S / 'bad')[0], 'IMPORT')
    assert not ok and why and files(data, 'save') == before, why
    put(S / 'bad0', 'zelda.sav', b'')
    assert not ok_of(run('import', S / 'bad0')[0], 'IMPORT')[0] and files(data, 'save') == before
    c = os.urandom(SIZE)
    put(S / 'good', 'zelda.sav', c)
    ok, why = ok_of(run('import', S / 'good')[0], 'IMPORT')
    assert ok, why
    assert (data / SAVE).read_bytes() == c and (data / 'save/save_0/savedata0.bin.flagsx').exists()
    # a backup folder (as copied out of backups/) is taken as it is
    ok, why = ok_of(run('import', data / 'backups' / name)[0], 'IMPORT')
    assert ok, why
    assert (data / SAVE).read_bytes() == a
    print('import: empty / wrong size / empty file refused with saves untouched; zelda.sav and a backup folder taken')

    # a file held open: refused with a message, the saves whole (Windows refuses to rename a folder with an open file)
    if os.name == 'nt':
        before = files(data, 'save') | files(data, 'rando')
        with open(data / SAVE, 'rb'):
            ok, why = ok_of(run('restore', name)[0], 'RESTORE')
        assert not ok and why, 'restore with a file open should fail'
        assert files(data, 'save') | files(data, 'rando') == before
        assert not list(data.rglob('*.restoring')) and not list(data.rglob('*.replaced'))
        print('restore with a file in use: refused, saves whole:', why)
    else:
        print('file in use: Windows only')

    # a file held open under the second folder (a seed's saves): refused, the first folder (already swapped) put back
    if os.name == 'nt':
        before = files(data, 'save') | files(data, 'rando')
        with open(data / SEED, 'rb'):
            ok, why = ok_of(run('restore', name)[0], 'RESTORE')
        assert not ok and why, 'restore with a seed file open should fail'
        assert files(data, 'save') | files(data, 'rando') == before, 'rollback left the saves changed'
        assert not list(data.rglob('*.restoring')) and not list(data.rglob('*.replaced'))
        print('restore with a seed file in use: refused, both folders back as they were')

    # retention: 21 backups -> the newest 20
    shutil.rmtree(data / 'backups')
    for _ in range(21):
        ok, why = ok_of(run('create')[0], 'CREATE')
        assert ok, why
    lst = backups()
    assert len(lst) == 20, len(lst)
    assert [x[1] for x in lst] == sorted((x[1] for x in lst), reverse=True) and len({x[1] for x in lst}) == 20
    print('retention: 21 backups made, the newest 20 kept, newest first')

    # at the cap, restoring the oldest: the auto backup must not prune the backup being read
    oldest = lst[-1][1]
    put(data, SAVE, os.urandom(SIZE))
    ok, why = ok_of(run('restore', oldest)[0], 'RESTORE')
    assert ok, why
    assert (data / 'backups' / oldest).is_dir() and (data / SAVE).read_bytes() == a, 'oldest backup lost'
    assert len(backups()) == 20, len(backups())
    print('restore of the oldest backup at the cap: restored, the backup is still there, 20 kept')
    print(f'{plat}: save backups ok')
