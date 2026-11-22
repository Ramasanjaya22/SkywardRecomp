# Build page: parameters, progress and error log

Status: **plan, nothing implemented.** Target: a BUILD page inside the app (Dear ImGui, in
`runtime/ui/`), reached from the start screen, that

1. shows the parameters a game-code build will use, and lets them be loaded / edited / saved,
2. shows real progress while the build runs (stage, unit counts, elapsed, jobs running),
3. shows the errors that stopped it, with the log they came from, without leaving the window.

The build itself stays where it is: `runtime/gamebuild.c`. The page is a view over it, exactly as
`draw_busy_overlay` is today.

## 1. What exists today

| Piece | Where | What it does |
|---|---|---|
| Progress callback | `runtime/gamebuild.h` `GameBuildOpts.progress(double, const char*)` | one fraction + one short step string |
| Per-unit state | `runtime/gamebuild.c:494` (`nrun < jobs && next < units.n`) | known inside the loop, not reported |
| Generator progress | `runtime/gamebuild.c:205` `read_gen_log` | parses `: progress <ci>/<n> files <k>` out of the generator's log |
| Cancel | `GameBuildOpts.cancelled` | polled on the build thread; completed objects stay |
| Build cache | `runtime/build_cache.cpp`, spec at `gamebuild.c:366` | `GAMELIB_BASE_ID`, `GAMELIB_CFLAGS`, `GAMELIB_SHARED`, pointer size, zig target |
| Failure reason | `gamebuild_run(..., char *why, size_t)` | **one** 256-byte string; helper's last log line via `log_tail()` (`gamebuild.c:226`) |
| Progress UI | `runtime/ui/launcher.mm:1097` `draw_busy_overlay` | one bar, one line, Cancel |
| Failure UI | `launcher.mm:305` `set_failure` | 6 fixed cases, fixed hints, no log |
| Parameters | `Makefile` → `-DGAMELIB_CFLAGS` / `-DGAMELIB_ID` / `-DGAMELIB_BASE_ID` (`gamebuild.c:22,305,366`) | compile-time only, invisible at run time |

So the machinery for a build is complete and resumable. What is missing is *observability*.

## 2. The gap, measured

1. **Parameters are unknowable at run time.** `GAMELIB_CFLAGS` is a macro; the player can see no
   flag, and changing one is a recompile of the app, not of the game code. `jobs` is computed at
   `launcher.mm:423` (`cores < ram_gb - 2 ? ...`) and shown nowhere.
2. **Progress is not actionable.** One fraction: the page cannot say "3 of 6 jobs, 742/1316 units,
   generator main at 41230/67170 functions, 4m12s, ~9m left".
3. **Errors are one line.** `why` holds the helper's *last* log line. The other 200 lines — the
   actual compile error — are in `<game_dir>/.gamebuild/*.log`, which the player never opens.
4. **The module list is hardcoded.** `gamebuild.c:404` builds `{"main", "sdk"}` only. Skyward
   Sword HD also has `subsdk0` and `subsdk1` (`Makefile` `GEN_SRC`, `STATE.md`), so the GUI's
   module parameter has nothing behind it yet.
5. **No record of a finished build.** `skywardgame.dll.id` says *what* it was built from; nothing says
   *when*, with *which flags*, in *how long*, or whether it was resumed.

## 3. The page

One page, three sections in a single scrolling column (no sub-tabs: the build is one operation, and
splitting it hides the connection between a flag, the bar and an error).

### 3.1 Parameters — with a load path

The requirement is that parameters can be **loaded**, not just toggled. Three layers, in order:

1. **Built-in defaults** — read from the app itself through a new
   `gamebuild_defaults(GameBuildOpts *out)` (`gamebuild.c`): `cflags = GAMELIB_CFLAGS`,
   `jobs = min(cores, ram - 2)`, modules = what the app can generate, `zig` present or not. The page
   shows these as the baseline; they are never editable in place.
2. **A parameter file** — `<data root>/build-params.txt`, plain `key = value`, `#` comments, same
   style as the existing settings file (`runtime/settings.c`). Read at page open, written by **Save**.
   Keys: `jobs`, `cflags`, `modules`, `chunk_bytes`, `big_fn`, `simd`, `generator` (bundled or
   script), `compiler` (bundled zig or system clang).
3. **Command line** — `--build-params <file>` and `LARECOMP_BUILD_*` overrides (the project's
   existing `LARECOMP_*` convention, `docs/debug-hooks.md`), for scripted and headless runs.

The rows:

| Row | Control | Notes |
|---|---|---|
| Modules | 4 checkboxes `main` `sdk` `subsdk0` `subsdk1` | disabled with a reason until §4.1 lands |
| Optimisation | `-O0` / `-O1` segmented | **changes `GAMELIB_CFLAGS` → changes the id → full rebuild**; the page says so before it is applied |
| SIMD | `c` (fixed on x86-64) | shown, not editable: `asm` cannot work here (`STATE.md`) |
| Jobs | slider 1..cores | default from `gamebuild_defaults` |
| Chunk size | `--chunk-bytes`, `--big-fn` | generator only; another value re-generates every chunk |
| Generator | bundled `skywardgen/` or `tools/venv` script | tells the player which path the build will take |
| Compiler | bundled `zig/` or system `clang` | the same |
| Library id | read-only text | `GAMELIB_ID-BUILD_ID`, the exact key the cache and `lasmlib_full_id` use |

Every editable row shows **whether it forces a full rebuild**. This is the one piece of information
whose absence has already cost this project hours (`STATE.md`, "setelah mengubah GENFLAGS, hapus
build/rt/gamelib.o"): a changed id silently means the cache is thrown away.

**Load / Save / Reset** buttons; **Copy** puts the effective values as a shell snippet
(`GENFLAGS=... make gen`) so a terminal can reproduce the GUI's build.

### 3.2 Progress — from the build thread

New struct in `gamebuild.h` (the existing `progress` callback stays, for the menu's own bar):

```c
typedef struct {
    const char *stage;      // "Check files", "Generate main", "Generate sdk", "Compile", "Link"
    int  stage_index, stages;
    int  generators_done, generators;
    unsigned long long gen_done, gen_total;   // functions written, this generator (read_gen_log)
    int  units_done, units_total;             // compile units finished / all (from the loop at :494)
    int  jobs_running, jobs;
    double elapsed, fraction;
    const char *step;       // the existing short string, unchanged
} GameBuildStatus;
typedef void (*GameBuildStatusFn)(const GameBuildStatus *);
```

`GameBuildOpts` gains one field, `status` (optional), called at the same points as `progress`.
`units_total` is known after `units` is collected (`gamebuild.c`, before the loop); `units_done` is
the counter that already exists (`next`/finished). The link step reports `Link` with no unit count.

The page then draws:

```
Recompiling the game code                         4m 12s   ~9m left
[############--------------------------------]  38%   Compile
Generate main  ####################  67170/67170 functions
Generate sdk   ##########...........  41230/67170 functions
Compile        504 / 1316 units      6 jobs running
```

"~9m left" appears only when a previous build of this game recorded a duration **and** the fraction
is past 10%; until then the row shows elapsed only. No guessed durations (`docs/builder-app.md` in
the MNG lineage makes the same choice, for the same reason).

The bar stays the same widget in the same overlay; the BUILD page is where the breakdown lives.

### 3.3 Error log — what stopped it, and the lines that say why

Two things today: `why` (one line) and `<game_dir>/.gamebuild/<unit>.log` (everything). The plan
keeps the logs on disk (they are the ground truth) and adds:

1. **A structured result.** `gamebuild.h`:

```c
typedef struct {
    const char *stage;      // where it stopped, as in GameBuildStatus.stage
    const char *unit;       // the chunk or generator, e.g. "main_465"
    const char *log_path;   // the file to open
    int exit_code;          // the helper's, or -1
    const char *why;        // the existing short string
    int tail_lines;         // how many lines tail holds
    const char *const *tail; // the last N lines of that log, most useful first
} GameBuildError;
const GameBuildError *gamebuild_last_error(void);   // valid after a failed gamebuild_run
```

`helper_failed()` (`gamebuild.c:245`) already reads the log's tail; it keeps those lines instead of
one, and records the stage/unit it was called for.

2. **A log pane** on the page: the failing unit's tail (up to 200 lines), monospace, scrolled to the
   first line that looks like a compiler error (`error:`, `fatal error:`, `undefined reference`,
   `No function at`, `svcBreak`, `FATAL:`) rather than to the end — the cause is rarely the last
   line.

3. **Copy / Open folder.** Copy puts the tail plus the header (`generator, compiler, flags, id,
   stage`) on the clipboard: that header is what a bug report needs. Open folder reveals
   `.gamebuild/` in Explorer (`SDL_OpenURL`-equivalent used today by `open_folder`).

4. **One build log of our own.** `build.log` in `.gamebuild/`, appended, never parsed by the code:
   start time, parameters, each stage, exit codes, result. It answers "what did it do last time"
   without a shell, and it is what §3.2's "left" estimate is computed from.

5. **A short classifier**, ported from `tools/log_triage.py`'s `NOTABLE` table (already written and
   tested, MIT, same project lineage): each known pattern becomes one line above the pane —
   "out of memory", "Control Flow Guard stopped the generator" (`gamebuild_cfg_stopped`),
   "generator crashed on main_465". The full text stays in the pane.

## 4. Things the page forces us to fix first

These are not optional: a parameter row with nothing behind it is a lie in the UI.

### 4.1 Modules as a parameter (`subsdk0`, `subsdk1`)

`gamebuild.c:404` hardcodes two generators and `find_tools` knows two exefs names. SS HD has four
code modules. Fix: a module table (name, base address, `--hle` yes/no) used by both the generator
loop and `Makefile`'s `gen` target, so the GUI's checkboxes and the command line agree. This is the
same change `STATE.md` lists as "Loader: LAS hanya load main+sdk".

### 4.2 Flags that are not macros

The cache spec (`gamebuild.c:366`) embeds `GAMELIB_CFLAGS`. A flag the page can change must be a
value in `GameBuildOpts`, not the macro, or the spec and the compiler would disagree:

```c
const char *cflags;   // NULL: GAMELIB_CFLAGS (the value this app was built with)
```

`add_cflags()` (`gamebuild.c:302`) reads `o->cflags`, and the spec is built from the same string.

### 4.3 Errors that carry their context

`why[256]` stays (every existing caller keeps working); `gamebuild_last_error()` is additive.

## 5. Phases

Each phase ends with something checkable in the running app, not just a clean build.

| Phase | Work | Pass condition |
|---|---|---|
| **0** | `gamebuild_defaults()` + the parameter file (`build-params.txt`), no UI | `--build-params` twice with different `jobs` starts two builds that report the value in `build.log` |
| **1** | `GameBuildStatus` in `gamebuild.h`, wired at the existing call sites | the page's numbers match the logs: `units_done/units_total` equals the chunk count on disk at the end |
| **2** | The BUILD page: parameter table, Load/Save/Reset, Copy | change `jobs` in the page → build → the log's `[gamebuild] ... N jobs` line shows the new N |
| **3** | `GameBuildError` + the log pane + classifier | a deliberately broken generated file stops the build and the pane shows the `error:` line, not just "failed" |
| **4** | `subsdk0`/`subsdk1` as modules (§4.1), flags through `GameBuildOpts` (§4.2) | all four modules build from the page; a flag change reports "full rebuild" **before** it is applied |
| **5** | `build.log` durations → "left" estimate | two consecutive builds: the second shows a remaining time within 30% of the truth |

Phases 0–3 are self-contained and safe; 4 touches the loader, so it lands only after the current
`svcBreak` in `init main` is resolved (`STATE.md`, TODO #1), otherwise the page would be diagnosing
two unknowns at once.

## 6. Deliberate non-goals

- **No reimplementation of the build.** `gamebuild_run` keeps its shape; the page adds no path.
- **No flags with no meaning here**: `--simd asm` is refused on x86-64, so it is shown disabled with
  the reason rather than hidden.
- **No editing of generated C.** That is what `tools/` and a terminal are for.
- **No guessed time remaining** on a first build.
- **No game files, no paths of a developer's machine** in anything the page writes (the project's
  rule, `AGENTS.md`).

## 7. Open question for the user

The BUILD page is a *developer* surface in a *player* app: parameters like `-O0`/`-O1` and chunk size
mean nothing to a player and can produce a library that loads but is wrong. Two shapes:

- **A.** Always visible from the start screen, behind one "Advanced" disclosure. Fastest to use
  during Phase 2–4 triage; every player can see it.
- **B.** Only when `LARECOMP_DEV=1` or a `dev.txt` sits next to the exe (the existing switch
  convention). Keeps the player's start screen plain; one variable to remember.

Recommended: **A with the disclosure closed by default.** The build is a one-time operation for a
player and a daily one for us; hiding it entirely costs more than the one extra click costs a player.