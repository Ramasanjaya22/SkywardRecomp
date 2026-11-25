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
                   ['-std=gnu11', '-Iruntime', 'runtime/save_backup.c', 'runtime/paths.c', 'runtime/save_emu.c',
                    f'runtime/platform/{plat}.c', 'tools/test_save_backup.c', '-o', str(exe)] + flags + extra,
                    cwd=ROOT, check=True)
    data = S / 'data'

    def run(*args):
        p = subprocess.run([str(exe), str(data), *map(str, args)], capture_output=True, text=True, timeout=120)
        assert p.returncode == 0, f'{args}: exit {p.returncode}\n{p.stderr}'
        return p.stdout.splitlines()

    def run_env(env, *args):
        p = subprocess.run([str(exe), str(data), *map(str, args)], env={**os.environ, **env},
                           capture_output=True, text=True, timeout=120)
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
    # an update: one -auto backup when the version changes, none when it's the same
    n0 = len(backups())
    line = run('update', '9.9.0')[0]
    made = line.split()[2].rstrip('|')
    assert ok_of(line, 'UPDATE')[0] and made.endswith('-auto') and len(backups()) == min(n0 + 1, 20), line
    assert (data / 'backups' / made / SAVE).read_bytes() == (data / SAVE).read_bytes()
    assert not ok_of(run('update', '9.9.0')[0], 'UPDATE')[0] and len(backups()) == min(n0 + 1, 20)
    assert not subprocess.run([str(exe), str(empty), 'update', '1'], capture_output=True, text=True).stdout.startswith('UPDATE 1')
    print('update: a new version backs the saves up once; the same version, or no saves: none')
    # import from an emulator's save folder: wiiking2.sav (size-checked), skip.dat, N.screenshot (may be empty)
    W = 'save/save_0/wiiking2.sav'
    put(data, W, os.urandom(SIZE))
    emu = S / 'emu-save'
    w = os.urandom(SIZE)
    put(emu, 'wiiking2.sav', w); put(emu, 'skip.dat', b's' * 160)
    put(emu, '0.screenshot', b'p' * 1000); put(emu, '2.screenshot', b'')
    ok, why = ok_of(run('import', emu)[0], 'IMPORT')
    assert ok, why
    assert (data / W).read_bytes() == w and (data / 'save/save_0/skip.dat').read_bytes() == b's' * 160
    assert (data / 'save/save_0/2.screenshot').read_bytes() == b''
    put(S / 'emu-bad', 'wiiking2.sav', os.urandom(SIZE + 1))
    before = files(data, 'save')
    assert not ok_of(run('import', S / 'emu-bad')[0], 'IMPORT')[0] and files(data, 'save') == before
    put(S / 'emu-big', 'wiiking2.sav', b'w'); put(S / 'emu-big', 'skip.dat', b'x' * ((4 << 20) + 1))
    assert not ok_of(run('import', S / 'emu-big')[0], 'IMPORT')[0] and files(data, 'save') == before

    # importing from a nested emulator save folder must keep the player's other slots
    other = os.urandom(SIZE)
    put(data, 'save/save_1/savedata1.bin', other)
    emu_folder = S / 'emu-folder'
    put(emu_folder, 'Ryujinx/wiiking2.sav', w)
    ok, why = ok_of(run('import', emu_folder)[0], 'IMPORT')
    assert ok, why
    assert (data / 'save/save_1/savedata1.bin').read_bytes() == other
    assert files(data, 'save')[W] == w
    print('import: nested emulator folder keeps other save slots')

    print('import: emulator folder with wiiking2.sav, skip.dat, empty screenshot taken; wrong size / over 4 MiB refused')
    # discovery: a fake home with Eden (both layouts), Fritchii, Ryujinx, and an Eden profile with no save
    H = S / 'home'
    ap = H / 'AppData/Roaming'
    env = {'APPDATA': str(ap), 'XDG_DATA_HOME': str(H / '.local/share'), 'XDG_CONFIG_HOME': str(H / '.config'),
           'HOME': str(H), 'USERPROFILE': str(H)}
    base = {'windows': ap, 'linux': H / '.local/share', 'macos': H / 'Library/Application Support'}[plat]
    ryu = {'windows': ap / 'Ryujinx', 'linux': H / '.config/Ryujinx', 'macos': base / 'Ryujinx'}[plat]
    T = '01002DA013484000'
    put(base / 'eden/nand/user/save/0000000000000000/AAAA' / T, 'wiiking2.sav', w)
    (base / 'eden/nand/user/save/0000000000000000/BBBB' / T).mkdir(parents=True)  # no save: no hit
    put(base / 'fritchii/nand/user/save/account/CCCC' / T / '0', 'wiiking2.sav', w)
    put(base / 'citron/nand/user/save/0000000000000000/DDDD' / T, 'wiiking2.sav', w)
    put(ryu / 'bis/user/save/0000000000000005/0', 'wiiking2.sav', w)
    put(ryu / 'bis/user/save/0000000000000006/0', 'other.bin', b'x')  # another game: no hit
    hits = [l.split(' ', 1)[1].split('|') for l in run_env(env, 'emu-list') if l.startswith('EMU ')]
    assert sorted((h[0], h[1]) for h in hits) == [('citron', 'DDDD'), ('eden', 'AAAA'), ('fritchii', 'CCCC'), ('ryujinx', '0000000000000005')], hits
    # a picked data folder is searched: importing %APPDATA%\eden takes AAAA's save
    ok, why = ok_of(run('import', base / 'eden')[0], 'IMPORT')
    assert ok and (data / W).read_bytes() == w, why
    print('emulators: Eden (both layouts), Citron, Fritchii, Ryujinx found; empty profile and other games skipped; data folder import')

    r = run('running', 'save-backup-test')[0]
    assert r.startswith('RUNNING 1|save-backup-test'), r
    assert run('running', 'no-such-process-zq')[0].startswith('RUNNING 0|'), 'bogus process found'
    print('process check: own process found, a missing one not')

    # export: Ryujinx's save replaced, the old one backed up, its working folder 1/ backed up and removed
    rsave = ryu / 'bis/user/save/0000000000000005/0'
    old = (rsave / 'wiiking2.sav').read_bytes()
    mine = (data / W).read_bytes()
    put(rsave.parent / '1', 'wiiking2.sav', b'working')
    put(data, 'save/save_0/wiiking2.sav.pending', b'junk')
    r = run('emu-export', rsave, 'ryujinx')[0]
    ok, why = ok_of(r, 'EXPORT')
    assert ok, why
    assert (rsave / 'wiiking2.sav').read_bytes() == mine and not (rsave / 'wiiking2.sav.pending').exists()
    assert (rsave / '2.screenshot').read_bytes() == b'' and not (rsave.parent / '1').exists()
    bk = [p for p in (data / 'backups').glob('export-ryujinx-*') if p.is_dir()]
    assert len(bk) == 1 and (bk[0] / '0/wiiking2.sav').read_bytes() == old and (bk[0] / '1/wiiking2.sav').exists()
    assert not list(rsave.parent.glob('*skyrecomp-*'))
    # a folder with no save is refused, nothing made
    ok, why = ok_of(run('emu-export', S / 'nowhere', 'eden')[0], 'EXPORT')
    assert not ok and why and not (S / 'nowhere').exists()
    # Windows: a file open in the target -> refused, target untouched, no leftovers
    if os.name == 'nt':
        esave = base / 'eden/nand/user/save/0000000000000000/AAAA/' / T
        before = files(esave, '.')
        with open(esave / 'wiiking2.sav', 'rb'):
            ok, why = ok_of(run('emu-export', esave, 'eden')[0], 'EXPORT')
            assert not ok and why, 'export with a file open should fail'
            assert files(esave, '.') == before and not list(esave.parent.glob('*skyrecomp-*'))
        print('export: replaced with a backup first; Ryujinx 1/ handled; missing target and file in use refused cleanly')
    else:
        print('export: replaced with a backup first; Ryujinx 1/ handled; missing target refused')

