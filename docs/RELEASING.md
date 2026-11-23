# Releasing SkywardRecomp

A release is a pushed tag. `.github/workflows/release-auto.yml` then tests, builds the Windows zip and the Linux
AppImages, checks the packages, and publishes them on a GitHub release with a `SHA256SUMS` file and notes. No manual step.

## Cut a release

1. **Bump the version** in one place: `#define LARECOMP_VERSION "v0.8.1"` in `runtime/ui/launcher.mm`. The Makefile,
   the package scripts and the file names read it from there. The new value must not be an existing tag
   (`git tag`; `v0.8.0` is taken).
2. Merge that change to `main` (PR). A real release is only built from a commit on `main`, so everything merged
   before it - including the UBSan fix `8a58be68` - is in the files.
3. Tag the merge commit and push the tag:
   ```
   git checkout main && git pull
   git tag v0.8.1
   git push origin v0.8.1
   ```
4. Watch *Actions -> Release (auto)*. When it is green the release is live (marked *latest*) with:
   `SkywardRecomp-v0.8.1-windows.zip`, `...-linux-x86_64.AppImage`, `...-linux-aarch64.AppImage`, `SHA256SUMS`, and notes
   (the merged PRs since the previous release, the download table, the checksums).

## What the workflow does

| Job | What |
|---|---|
| `check` | tag == `LARECOMP_VERSION` at the commit (else it fails with the two values); the commit is on `main`; an existing tag points at this commit; the Makefile still has `-fno-sanitize=undefined` |
| `tests` | `test_game_find.py`, `test_gamebuild_resume.py`, `test_build_cache.py`, `test_delete_guard.py` in the CI container |
| `windows` | the same tests under MSYS2 (real junctions), `make dist`, no-game-code scan, package smoke check |
| `linux` (x86_64 + aarch64) | `make appimage`, no-game-code scan, package smoke check on the unpacked AppImage |
| `publish` | only when all of the above passed: `SHA256SUMS`, generated notes, `gh release create` |

The package smoke check is `tools/smoke_package.sh`: the shipped generator starts, and the shipped `zig` compiles the
shipped `gamelib-src/` with the Makefile's `GENFLAGS` and leaves no `__ubsan_handle_*` in the objects. It needs no game
files and no GPU. The full run with the game files (`make smoketest`, `tools/linux/smoketest.sh`) stays on the
maintainer's machine; CI cannot have the files.

If any job fails, nothing is published.

## Re-run, dry run, other starts

- **Re-run** a failed release: *Actions -> Release (auto) -> Run workflow*, `tag` = the tag (or "Re-run failed jobs" on
  the run). If the release exists, its files are replaced (`--clobber`) and the notes rewritten.
- **Dry run** of the workflow itself: push a tag `ci-test-<anything>` on the commit to try (works from any branch), or, once the file is on `main`, run it with `tag` = `ci-test-<anything>` and `ref` = a branch. The tag may
  differ from `LARECOMP_VERSION`, need not be on `main`, and the release is a *prerelease*, not *latest*. `test-*` tags
  are prereleases too. Delete the test release and tag afterwards.
- **A release made by hand** on GitHub (tag = `LARECOMP_VERSION`): the workflow attaches the files and appends its notes
  below yours. One that already has a Windows zip is left alone.
- **macOS** `.dmg`: not here (needs the self-hosted VM). `release.yml` (manual dispatch) builds all four platforms into a
  *draft*; publish it by hand.

## Rules

- Never put game files, keys (`prod.keys`) or credentials in the repo or in a release; both build jobs scan for game
  code and fail.
- The workflow needs the `skywardrecomp-ci` image on `ghcr.io` (built by `ci-image.yml`) for the `tests` job.
