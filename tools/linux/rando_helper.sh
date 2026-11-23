#!/bin/bash
# tools/linux/rando_helper.sh <x86_64|aarch64> (inside the build container of that arch, tools/linux/appimage.sh): the
# game code generator and the .nsp extractor frozen by PyInstaller (tools/skywardgen.spec, tools/skywardextract.spec, as
# `make gen-helper` / `make extract-helper`) with the container's pinned standalone Python 3.12 (tools/linux/Containerfile),
# into build/linux/<arch>/helper/skywardgen and helper/skywardextract. (The inherited seed generator, rando-gen, is not
# built: its randomizer sources are not in this repository; package.sh ships it only when it exists.) Every dependency
# has a manylinux wheel for both arches, so nothing is built from source.
set -euo pipefail
cd "$(dirname "$0")/../.."
L=$PWD/build/linux/$1
V=$L/rando-venv
if [ ! -f "$V/.ok" ]; then
    rm -rf "$V" && python3.12 -m venv "$V"
    "$V/bin/python" -m pip install -q --upgrade pip
    "$V/bin/python" -m pip install -q pyinstaller==6.22.3 capstone==5.0.9 unicorn==2.1.4 lz4==4.4.5 cryptography
    touch "$V/.ok"
fi
for spec in skywardgen skywardextract; do
    "$V/bin/python" -m PyInstaller --noconfirm --clean --log-level WARN --distpath "$L/helper" --workpath "$L/pyinstaller"         tools/$spec.spec
    du -sh "$L/helper/$spec"
done
