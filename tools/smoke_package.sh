#!/bin/bash
# tools/smoke_package.sh <package folder> [zig target]: the release check that needs no game files and no GPU, run by
# .github/workflows/release-auto.yml on the unpacked package (Windows: the unzipped SkywardRecomp/, Linux: the AppImage's
# usr/bin). It runs what the player's PC runs first: the shipped game code generator starts, and the shipped compiler
# builds the shipped gamelib-src/ entry with the Makefile's GENFLAGS and leaves no UBSan handler behind (the failure
# of old releases: undefined __ubsan_handle_* at link, commit 8a58be68). The full run on a machine with the game
# files is tools/linux/smoketest.sh (make smoketest); the release is not blocked on that one.
set -euo pipefail
cd "$(dirname "$0")/.."
PKG=$(cd "${1:?usage: tools/smoke_package.sh <package folder> [zig target]}" && pwd)
TARGET=${2:-}  # (Linux: x86_64-linux-gnu / aarch64-linux-gnu, as runtime/gamebuild.c passes it)
X=; [ -e "$PKG/skywardrecomp.exe" ] && X=.exe
fail() { echo "smoke: $*" >&2; exit 1; }

for f in skywardrecomp$X skywardgen/skywardgen$X zig/zig$X gamelib-src/gamelib_entry.c gamelib-src/gamelib.h; do
    [ -e "$PKG/$f" ] || fail "package lacks $f"
done
"$PKG/skywardgen/skywardgen$X" --help >/dev/null || fail "skywardgen does not start"

# the flags of the game code, from the one place that defines them (the same ones gamebuild.c puts in its id)
GENFLAGS=$(sed -n 's/^GENFLAGS := //p' Makefile | sed 's/-Iruntime//')
case $GENFLAGS in *-fno-sanitize=undefined*) ;; *) fail "Makefile GENFLAGS lacks -fno-sanitize=undefined" ;; esac

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export ZIG_GLOBAL_CACHE_DIR=$T/gc ZIG_LOCAL_CACHE_DIR=$T/lc
printf 'int smoke_f(int a, int b) { return a + b; }\n' > "$T/t.c"  # (signed overflow: UBSan's first handler)
ZIG=("$PKG/zig/zig$X" cc ${TARGET:+-target "$TARGET"} $GENFLAGS -I"$PKG/gamelib-src")
"${ZIG[@]}" -c "$T/t.c" -o "$T/t.o" || fail "the shipped compiler does not compile"
"${ZIG[@]}" -c "$PKG/gamelib-src/gamelib_entry.c" -o "$T/e.o" || fail "gamelib-src does not compile"
! grep -qa __ubsan_handle "$T/t.o" "$T/e.o" || fail "UBSan handlers in the game code objects"
echo "smoke: $PKG ok (generator starts, shipped zig compiles gamelib-src, no UBSan)"
