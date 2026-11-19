# SkywardRecomp

Work-in-progress static recompilation of *The Legend of Zelda: Skyward Sword HD* (Switch, v1.0.0) into a native
Windows app with a Vulkan renderer. Derived from [LAS Recomp](https://github.com/peiikai/LASRecomp).

> **Unofficial fan project, not affiliated with or endorsed by Nintendo.** "The Legend of Zelda" and "Skyward Sword"
> are trademarks of Nintendo. This repository contains **no** game code, assets, fonts, keys or game files.
> You must supply your own legally obtained copy of the game; the build generates C code locally from your files,
> and that output (`generated/`, `extracted/`, `input/`) is git-ignored and must never be shared.

## Screenshots

Running natively on Windows (Vulkan, 21:9 ultrawide).

| | |
|---|---|
| ![Skyloft field](docs/media/screenshot-1.jpg) | ![Bridge in Skyloft](docs/media/screenshot-2.jpg) |
| ![Cutscene](docs/media/screenshot-3.jpg) | ![Skyloft plaza](docs/media/screenshot-4.jpg) |

## Progress

**Overall: ~80%** (estimate as of 2026-11-19)

```
[████████████████░░░░] 80%
```

| Area | Progress | State |
|---|---|---|
| Loader, kernel/HLE services, boot to title screen | `██████████` 100% | done |
| Threading (condvar lost-wakeup fix), 30 fps pacing | `██████████` 100% | done |
| Audio (audren voices) | `██████████` 100% | done |
| App builds and tests pass on Windows, Linux and macOS (CI) | `██████████` 100% | done |
| Vulkan renderer: textures, depth/stencil, dual-source blend (eyes) | `██████████` 100% | done, eyes confirmed in game 2026-11-18 |
| Character lighting | `██████████` 100% | confirmed in game 2026-11-18 |
| Shadow rendering | `██████████` 100% | confirmed in game 2026-11-18, no shadow bug |
| Save-file names (`□□` glyphs) | `██████████` 100% | fixed (swkbd UTF-16), confirmed in game 2026-11-19 |
| LA-specific HLE modules and address tables replaced for SS | `███░░░░░░░` 30% | LA leftovers retired (address 0, unused); SS table still partial |
| Ultrawide, resolution options | `██████████` 100% | 21:9 textures, shadows and characters confirmed in game 2026-11-19 |
| 60 fps | `░░░░░░░░░░` 0% | not started |
| Long-session stability / full playthrough | `███░░░░░░░` 30% | 150 s+ runs OK, no full playthrough |

The percentage is a rough milestone estimate, not a measured figure.

## Building (Windows)

See [docs/BUILD-windows.md](docs/BUILD-windows.md) (MSYS2 UCRT64, clang, Vulkan 1.3 GPU, AVX2/FMA CPU).

## Texture packs (Vulkan, experimental)

Put your own DDS files in `textures/` under the data root (next to `extracted/` and `save/`). Filenames follow
Kenji-NX: `<prefix>_<32 hex XXH3-128>_<w>x<h>x<d>.dds`, for example `tex2d_99aa06d3014798d86001c324468d497f_1024x1024x1.dds`.
Only BC1, BC3, BC5 and BC7 files that match the game texture's format and top-level dimensions are used; a missing
file, a wrong format or a short mip chain is logged once and the game texture is kept. Kenji-NX's level/slice form
(`..._0x0.dds`) is accepted too. 8-bit RGB/RGBA PNG packs are also read: their image is sampled to the game
texture's size (and to each mip), for ASTC textures. No texture packs ship with the app.

## Legal

- Code in this repository: MIT, see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- The license does not cover Nintendo's game or trademarks.
- Do not open issues or PRs containing game files, keys, extracted data or instructions for obtaining them.

## Credits

SkywardRecomp stands on the work of these projects. Thank you.

- **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by Peiikai: the base of this project (recompiler pipeline,
  runtime, HLE services, Vulkan backend). MIT.
- **nxnative** and **MacNativeGames**: the runtime and target layer that LAS Recomp builds on (see
  `ss-reference/COPIES-mac-port.md` for exact sources). MIT.
- **Ryujinx**: Maxwell shader translator sources (`third_party/ryujinx`). MIT.
- **[Kenji-NX](https://git.ryujinx.app/projects/Kenji-NX)**: texture-pack filename scheme and XXH3-128 identity (`runtime/nvn/xxh3_128.c`). MIT.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut. MIT.
- **[SMAA](https://github.com/iryoku/smaa)** by Jorge Jimenez et al.
- **[RenderDoc](https://renderdoc.org)** by Baldur Karlsson: used for GPU capture debugging.
- **[libnx / switchbrew](https://github.com/switchbrew/libnx)**: Switch OS structures and service references.
- **BIZ UDPGothic, Baloo 2, DejaVu** fonts: see `third_party/fonts/`.

Not affiliated with any of the above or with Nintendo.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).