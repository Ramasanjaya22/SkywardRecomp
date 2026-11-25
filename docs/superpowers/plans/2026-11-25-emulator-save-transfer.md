# Emulator Save Transfer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The launcher finds Skyward Sword HD saves that Switch emulators (Eden family, Ryujinx) keep on this PC, imports one with a backup first, and exports SkywardRecomp's save back to an emulator with a backup of the emulator's save first.

**Architecture:** A new `runtime/save_emu.{h,c}` holds the emulator table, the folder search and the export. Import keeps using `save_backup_import` (its auto-backup and rename swap), taught the game's real file names. A new `plat_process_running` refuses a transfer while an emulator runs. The launcher's existing "Save data" section gets one row per found save.

**Tech Stack:** C11 (runtime), ObjC++/ImGui (`runtime/ui/launcher.mm`), Python 3 test driver (`tools/test_save_backup.py`) building `tools/test_save_backup.c`.

**Spec:** this file's "Findings" section (no separate spec; it comes from reading `I:\Games\Longshot` and this repo on 2026-11-25).

## Findings

What Longshot does (`scripts/longshot_saves.py`, `docs/dev/reference/saves.md`, `host/nn_fs.c`):

1. **Same bytes everywhere.** Longshot's save folder holds the same files as the emulators' folders, so a transfer is a byte-for-byte copy of the folder's files.
2. **A table of emulators and layouts.** Each emulator has data folders per OS, and the same layouts are searched under each: Eden's newer `nand/user/save/account/<id>/<TITLE>/0`, then `nand/user/save/0000000000000000/<id>/<TITLE>`. The first layout that finds a profile wins. A folder shared by several emulators (Eden, Frizti, Orchid) is one save. A profile folder counts only if it holds a save.
3. **Safety.** It refuses while the game or an emulator runs, checks every file (no links, no empty file, none over 4 MB), backs up the side being overwritten to `<backups>/<import|export>-<name>-<YYYYmmdd-HHMMSS>/`, copies into a folder inside the target, and swaps that in only when the copy worked.
4. **Atomic commit inside the game** (`save-work/` + a journal): SkywardRecomp already does this per file (`<file>.pending` renamed at Commit, `runtime/services/fs.c:645`). Nothing to port.

What SkywardRecomp has (`runtime/save_backup.c`, launcher "Save data" section at `runtime/ui/launcher.mm:2466`): backups (newest 20), restore, and import from a picked folder, each with an `-auto` backup and a rename swap.

The gaps:

- The game writes `save/save_0/wiiking2.sav`, `skip.dat` and `0.screenshot`..`3.screenshot` (checked in a real run's log: `[save] committed save/save_0/wiiking2.sav`). An emulator's folder for title `01002DA013484000` holds exactly those files (seen in `%APPDATA%\fritchii\nand\user\save\0000000000000000\<profile>\01002DA013484000\`). `save_backup.c`'s `is_save_file` accepts only `zelda.sav` and `savedata*.bin`, so importing a real emulator folder fails with "No save file in that folder."
- Nothing finds emulator saves, nothing exports, nothing checks for a running emulator.

## Global Constraints

- Title id: `01002DA013484000` (`runtime/game_find.c:20`).
- Save files: `wiiking2.sav` (the save, size-checked against the one it replaces), `skip.dat`, `N.screenshot` for N in 0-3 (side files; a screenshot may be 0 bytes). No other file is copied in or out (not `.pending`, not `.flagsx`).
- SkywardRecomp's side: `<data root>/save/save_0/`. Randomizer saves (`rando/<seed>/save/`) are never exported.
- Any copied file over 4 MB (`4 << 20`) or that is a link is refused, and nothing changes.
- Export backups go to `<data root>/backups/export-<emu>-<YYYYmmdd-HHMMSS>/` (a name `valid_name` doesn't match, so `save_backup_list` and `prune` never touch them).
- Emulator data folders, `<name>` as below: Windows `%APPDATA%\<name>`; Linux `$XDG_DATA_HOME/<name>` (default `~/.local/share/<name>`); macOS `~/Library/Application Support/<name>`. Ryujinx: Windows `%APPDATA%\Ryujinx`, Linux `$XDG_CONFIG_HOME/Ryujinx` (default `~/.config/Ryujinx`), macOS `~/Library/Application Support/Ryujinx`. Paths come from those environment variables first (the tests set them).
- Eden-family names: `eden`, `yuzu`, `suyu`, `sudachi`, `citron`, `torzu`, `fritchii`, `lichii`, `eglantine`. Process names checked (lower case, no `.exe`): those, plus `orchid` and `ryujinx`.
- Layouts under a data folder, in this order: `nand/user/save/account/<id>/01002DA013484000/0`, `nand/user/save/0000000000000000/<id>/01002DA013484000`, `bis/user/save/<id>/0` (Ryujinx: a numbered save folder, matched by holding `wiiking2.sav`). A hit is a folder holding `wiiking2.sav`.
- Message style: the repo's plain-English `why` strings (`"Couldn't ... (%s)."`), no exclamation marks.

## Review Focus

1. **Two emulators, same folder** (Frizti/Orchid read `%APPDATA%\eden`): one hit, not two. Test in Task 2.
2. **Emulator has never saved the game** (no `01002DA013484000` folder): export finds no target; the launcher shows no export row for it, and nothing is created. Test in Task 2 (no hit) and Task 4 (export to a missing folder refused).
3. **Empty screenshot of an unused slot** (`2.screenshot`, 0 bytes): import and export take it. Test in Task 1 and Task 4.
4. **Export fails half-way** (a file in the emulator's folder held open on Windows): the emulator's save is as it was, no `.skyrecomp-new`/`.skyrecomp-old` left. Test in Task 4.
5. **Ryujinx working folder** (`bis/user/save/<id>/1` beside `0`): export writes `0` and removes `1` after backing up both, since Ryujinx rebuilds `1` from `0` at mount. Test in Task 4.

---

### Task 1: Import takes the game's real save files

**Files:**
- Modify: `runtime/save_backup.c:227-262` (`is_save_file`, `stage_dir`)
- Modify: `runtime/save_backup.h:19-20` (comment)
- Test: `tools/test_save_backup.py`

**Interfaces:**
- Produces: `save_backup_import(root, dir, why, cap)` accepts a folder holding `wiiking2.sav` (+ `skip.dat`, `N.screenshot`), staged into `save/save_0/`.

- [ ] **Step 1: Write the failing test** — append before the final `print` in `tools/test_save_backup.py`:

```python
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
    put(S / 'emu-big', 'wiiking2.sav', w); put(S / 'emu-big', 'skip.dat', b'x' * ((4 << 20) + 1))
    assert not ok_of(run('import', S / 'emu-big')[0], 'IMPORT')[0] and files(data, 'save') == before
    print('import: an emulator folder (wiiking2.sav, skip.dat, empty screenshot) taken; wrong size / over 4 MB refused')
```

- [ ] **Step 2: Run it, expect FAIL** — `python3 tools/test_save_backup.py` (Windows: from MSYS2 UCRT64 bash with `/ucrt64/bin` on PATH and `CC=C:/msys64/ucrt64/bin/clang.exe`). Expected: `AssertionError` with "No save file in that folder."

- [ ] **Step 3: Implement** — `is_save_file` also accepts `wiiking2.sav`; a new `static bool is_side_file(const char *n)` is true for `skip.dat`, `[0-9].screenshot` and the existing `*.flagsx`; side files skip the size check (empty allowed). Every copied file over `4 << 20` bytes fails the import with `"%s is too big to be a save file."`. Links: `lstat` each entry and refuse `S_ISLNK` with `"%s is a link, not a save file."` (on Windows `lstat` doesn't exist in MinGW; use `GetFileAttributesA` & `FILE_ATTRIBUTE_REPARSE_POINT` under `#ifdef _WIN32`).

- [ ] **Step 4: Run it, expect PASS** — same command; last line `<plat>: save backups ok`.

- [ ] **Step 5: Commit** — `git add runtime/save_backup.c runtime/save_backup.h tools/test_save_backup.py && git commit -m "Save import: take the game's own files (wiiking2.sav, skip.dat, screenshots)"`

### Task 2: Find emulator saves

**Files:**
- Create: `runtime/save_emu.h`, `runtime/save_emu.c` (picked up by the Makefile's `runtime/*.c` wildcard)
- Modify: `runtime/save_backup.c` (`save_backup_import` falls back to a search under the picked folder)
- Modify: `tools/test_save_backup.c` (command `emu-list`), `tools/test_save_backup.py` (add `runtime/save_emu.c` to the build line; new checks)

**Interfaces:**
- Produces:
  - `typedef struct { char emu[32]; char display[48]; char profile[40]; char path[1024]; long long when; } SaveEmuHit;` (`emu`: table name, e.g. `"eden"`; `when`: `wiiking2.sav`'s mtime)
  - `int save_emu_find(SaveEmuHit *out, int max);` — every hit on this OS, newest `when` first, one per real folder.
  - `int save_emu_find_under(const char *dir, const char *emu, SaveEmuHit *out, int max);` — hits under one folder (a data folder, a portable `user/` folder, or the save folder itself).

- [ ] **Step 1: Write the failing test** — in `tools/test_save_backup.c` add `emu-list` printing `EMU <emu>|<profile>|<path>|<when>` per hit then `COUNT n`. In the Python driver, a helper `run_env(env, *args)` passing `env={**os.environ, **env}`, then:

```python
    # discovery: a fake home with Eden (both layouts), Fritchii, Ryujinx, and an Eden profile with no save
    H = S / 'home'
    ap = H / 'AppData/Roaming'
    env = {'APPDATA': str(ap), 'XDG_DATA_HOME': str(H / '.local/share'), 'XDG_CONFIG_HOME': str(H / '.config'),
           'HOME': str(H), 'USERPROFILE': str(H)}
    base = {'windows': ap, 'linux': H / '.local/share', 'macos': H / 'Library/Application Support'}[plat]
    ryu = {'windows': ap / 'Ryujinx', 'linux': H / '.config/Ryujinx', 'macos': base / 'Ryujinx'}[plat]
    T = '01002DA013484000'
    put(base / 'eden/nand/user/save/0000000000000000/AAAA' / T, 'wiiking2.sav', w)
    (base / 'eden/nand/user/save/0000000000000000/BBBB' / T).mkdir(parents=True)        # no save: no hit
    put(base / 'fritchii/nand/user/save/account/CCCC' / T / '0', 'wiiking2.sav', w)
    put(ryu / 'bis/user/save/0000000000000005/0', 'wiiking2.sav', w)
    put(ryu / 'bis/user/save/0000000000000006/0', 'other.bin', b'x')                       # another game: no hit
    hits = [l.split(' ', 1)[1].split('|') for l in run_env(env, 'emu-list') if l.startswith('EMU ')]
    assert sorted((h[0], h[1]) for h in hits) == [('eden', 'AAAA'), ('fritchii', 'CCCC'), ('ryujinx', '0000000000000005')], hits
    # a picked data folder is searched: importing %APPDATA%\eden takes AAAA's save
    ok, why = ok_of(run('import', base / 'eden')[0], 'IMPORT')
    assert ok and (data / W).read_bytes() == w, why
    print('emulators: Eden (both layouts), Fritchii, Ryujinx found; empty profile and other games skipped; data folder import')
```

- [ ] **Step 2: Run it, expect FAIL** — build error: `save_emu.h` not found.

- [ ] **Step 3: Implement** `save_emu.c`: a static table `{name, display, process, ryujinx}` per Global Constraints; a data-folder resolver per OS reading the environment variables named there (Windows falls back to `USERPROFILE\AppData\Roaming` only when `APPDATA` is unset); a walk of the three layouts with `<id>` matching any sub-folder; dedupe by `realpath`/`_fullpath`; sort by `when` desc. `save_emu_find_under` with `dir` itself holding `wiiking2.sav` returns that folder with profile = its basename. In `save_backup_import`, when the plain-folder staging finds no save (`n == 0`), call `save_emu_find_under(src_dir, "folder", ...)`: exactly one hit → import from `hit.path`; more than one → fail `"That folder has saves of %d profiles: pick the profile's folder."`.

- [ ] **Step 4: Run it, expect PASS.**

- [ ] **Step 5: Commit** — `git add runtime/save_emu.h runtime/save_emu.c runtime/save_backup.c tools/test_save_backup.c tools/test_save_backup.py && git commit -m "Saves: find Skyward Sword HD saves of Eden-family emulators and Ryujinx"`

### Task 3: Refuse while an emulator runs

**Files:**
- Modify: `runtime/platform.h` (declaration), `runtime/platform/windows.c`, `runtime/platform/linux.c`, `runtime/platform/macos.c`
- Modify: `runtime/save_emu.{h,c}`, `tools/test_save_backup.c` (command `running <name>`), `tools/test_save_backup.py`

**Interfaces:**
- Produces:
  - `bool plat_process_running(const char *const *names, char *found, size_t n);` — `names`: lower-case, no `.exe`, NULL-terminated; true and the first match in `found` when one runs.
  - `bool save_emu_running(char *display, size_t n);` — any table process running; its display name in `display`.

- [ ] **Step 1: Write the failing test:**

```python
    r = run('running', 'save-backup-test')[0]
    assert r.startswith('RUNNING 1|save-backup-test'), r
    assert run('running', 'no-such-process-zq')[0].startswith('RUNNING 0|'), 'bogus process found'
    print('process check: own process found, a missing one not')
```

- [ ] **Step 2: Run it, expect FAIL** (unknown command → exit 2).

- [ ] **Step 3: Implement** — Windows: `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` + `Process32First/Next`, compare `szExeFile` without `.exe`, case-insensitive. Linux: read `/proc/<pid>/comm` (trailing newline stripped; note `comm` is cut at 15 chars, so compare the first 15 of the name). macOS: `proc_listallpids` + `proc_name` (`<libproc.h>`).

- [ ] **Step 4: Run it, expect PASS.**

- [ ] **Step 5: Commit** — `git commit -am "Platform: tell whether a named process is running"` (no new files in this task).

### Task 4: Export to an emulator

**Files:**
- Modify: `runtime/save_emu.{h,c}`, `tools/test_save_backup.c` (command `emu-export <dst>`), `tools/test_save_backup.py`

**Interfaces:**
- Consumes: `SaveEmuHit.path` / `.emu` from Task 2.
- Produces: `bool save_emu_export(const char *root, const char *dst, const char *emu, char *backup_out, size_t bn, char *why, size_t cap);` — copies `<root>/save/save_0/`'s save files (Global Constraints list only) over `dst`. `backup_out`: the backup folder's path.

- [ ] **Step 1: Write the failing test:**

```python
    # export: Ryujinx's save replaced, the old one backed up, its working folder 1/ backed up and removed
    rsave = ryu / 'bis/user/save/0000000000000005/0'
    old = (rsave / 'wiiking2.sav').read_bytes()
    put(rsave.parent / '1', 'wiiking2.sav', b'working')
    mine = (data / W).read_bytes()
    put(data, 'save/save_0/wiiking2.sav.pending', b'junk')
    r = run('emu-export', rsave, 'ryujinx')[0]
    ok, why = ok_of(r, 'EXPORT')
    assert ok, why
    assert (rsave / 'wiiking2.sav').read_bytes() == mine and not (rsave / 'wiiking2.sav.pending').exists()
    assert (rsave / '2.screenshot').read_bytes() == b'' and not (rsave.parent / '1').exists()
    bk = [p for p in (data / 'backups').glob('export-ryujinx-*') if p.is_dir()]
    assert len(bk) == 1 and (bk[0] / '0/wiiking2.sav').read_bytes() == old and (bk[0] / '1/wiiking2.sav').exists()
    assert not list(rsave.parent.glob('*.skyrecomp-*'))
    # a folder with no save is refused, nothing made
    ok, why = ok_of(run('emu-export', S / 'nowhere', 'eden')[0], 'EXPORT')
    assert not ok and why and not (S / 'nowhere').exists()
    # Windows: a file open in the target -> refused, target untouched, no leftovers
    if os.name == 'nt':
        esave = base / 'eden/nand/user/save/0000000000000000/AAAA' / T
        before = files(esave, '.')
        with open(esave / 'wiiking2.sav', 'rb'):
            assert not ok_of(run('emu-export', esave, 'eden')[0], 'EXPORT')[0]
        assert files(esave, '.') == before and not list(esave.parent.glob('*.skyrecomp-*'))
    print('export: replaced with a backup first; Ryujinx 1/ handled; missing target and file in use refused cleanly')
```

- [ ] **Step 2: Run it, expect FAIL** (unknown command).

- [ ] **Step 3: Implement** — order: refuse if `dst` has no `wiiking2.sav` (`"No save of the game there: start it once in the emulator and save."`) or `save/save_0/wiiking2.sav` is missing; back up `dst` (and a sibling `1/` when `dst`'s basename is `0`) into `<root>/backups/export-<emu>-<stamp>.partial/`, renamed to its final name when complete; copy the save files into `<dst>.skyrecomp-new/` (same 4 MB / link checks as Task 1); rename `dst` → `<dst>.skyrecomp-old`, new → `dst`; on a failed rename put `old` back and remove `new`; on success remove `old` and the sibling `1/`. Files in `dst` that aren't save files are kept (copy them into `new` first).

- [ ] **Step 4: Run it, expect PASS.**

- [ ] **Step 5: Commit** — `git commit -am "Saves: export to an emulator, backing up its save first"`

### Task 5: Launcher rows and docs

**Files:**
- Modify: `runtime/ui/launcher.mm:2466-2510` (the "Save data" section)
- Create: `docs/saves.md`; Modify: `README.md` (one link line under Setup)

**Interfaces:**
- Consumes: `save_emu_find`, `save_emu_running`, `save_emu_export`, `save_backup_import`.

- [ ] **Step 1: Implement the rows.** `save_emu_find` once when the section first shows and after each transfer (cache in a `static SaveEmuHit[16]`, not every frame). Per hit, two `opt_action` rows: `"Import from <display> (<profile first 8>)"` with value = `when` as `%Y-%m-%d %H:%M`, and `"Export to <display> (<profile first 8>)"`. Both use the existing `g_sb_confirm` yes/cancel pattern (key `"import:" + path` / `"export:" + path`). On the yes: `save_emu_running` true → `set_job("Close <display> first.", true, "The emulator may overwrite the save while it runs.")`; else run the call and `set_job("Save imported from <display>.")` / `set_job("Save exported to <display>. Backup: <backup_out>")` or the failure with `why`. Help texts: import — "Copies this emulator's save into SkywardRecomp. Your current saves are backed up first."; export — "Copies your save into this emulator. Its save is backed up first (backups folder)."

- [ ] **Step 2: Write `docs/saves.md`**: where SkywardRecomp keeps saves (`<data root>/save/save_0/`, files as in Global Constraints), the emulator folders table (Global Constraints), what import/export back up and where, "close the emulator first", portable emulator installs via "Import save..." on their `user/` folder, and that randomizer saves are not exported.

- [ ] **Step 3: Verify in the app.** Build (`make`), start the launcher with a scratch data root: `LARECOMP_DATA_ROOT=build/savetest ./build/skywardrecomp` with a real emulator save on the PC. Expected: the emulator's row appears; Import → `build/savetest/save/save_0/wiiking2.sav` equals the emulator's; the game loads the slot; Export while the emulator runs shows "Close ... first."; Export with it closed leaves `build/savetest/backups/export-*`.

- [ ] **Step 4: Run all save tests** — `python3 tools/test_save_backup.py` and `python3 tools/test_save.py`. Expected: both pass.

- [ ] **Step 5: Commit** — `git add runtime/ui/launcher.mm docs/saves.md README.md && git commit -m "Launcher: import from and export to emulators found on this PC"`

## Not in this plan

- Longshot's save-work + journal commit: SkywardRecomp's per-file `.pending` commit already covers the crash case; a cross-file journal only matters for `skip.dat`/screenshot staying in step with `wiiking2.sav`.
- Multiple emulator user profiles merged into SkywardRecomp's slots: one profile's save is taken whole, as Longshot does.
- `docs/save-format.md`, `docs/debug-hooks.md` and the `strstr(path, "savedata")` checks in `runtime/services/fs.c:926` (save editor, extended flags) name `savedataN.bin`, while the game writes `wiiking2.sav`. Worth its own look; not touched here.
