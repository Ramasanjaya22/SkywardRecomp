# Setting up SkywardRecomp

You supply your own, legally obtained copy of *The Legend of Zelda: Skyward Sword HD* (version 1.0.0). This project contains
no game files and no keys, and does not help you obtain them.

## 1. What you need

| | |
|---|---|
| OS | Windows 10/11 (x86-64), or Linux x86-64 (glibc 2.35+; see [linux.md](linux.md), including Fedora / Bazzite) |
| CPU | x86-64 with AVX2 / FMA (about 2013 or newer) |
| GPU | Vulkan 1.3 driver |
| Disk | about 12 GB free for the game files, plus about 2-3 GB for the compiler's work files |
| RAM | 8 GB or more; the one-time compile runs several compiler processes at once, each needs roughly 0.5-1.5 GB |
| Game | **either** the game's `.nsp` package (base game, v1.0.0) **or** its extracted `exefs` and `romfs` folders |
| `prod.keys` | **only for the `.nsp`**: your own console's keys file. Not needed if you already have the extracted folders |

The `.xci`, `.nca`, `.nsz` and `.xcz` formats and update / DLC `.nsp` files are not supported; extract the base game first.

## 2. Install and start

Download a release (Windows installer or Linux AppImage) and start it, or build from source:
[BUILD-windows.md](BUILD-windows.md) / [linux.md](linux.md). Nothing has to be copied by hand: the start screen sets everything up.

## 3. Point it at your game

1. Use the start screen's folder picker and choose the folder that holds your `.nsp` (or your extracted `exefs` + `romfs`).
   Other files in that folder, in sub-folders, or several `.nsp` files are fine: every candidate is opened and checked, and
   the window says which one it uses and why it skipped the others (an update, a DLC, another game, a damaged file).
2. If several valid base-game packages are present, the same one is chosen every time: the one in the shallowest folder, then
   by name. Put only the one you want in the chosen folder, or pick its folder directly, to choose differently.
3. An `.nsp` needs `prod.keys`. The app looks, in this order, next to the `.nsp`, in its parent folder, in its own data folder,
   in the folders used by yuzu / Eden / Suyu / Ryujinx / Lockpick (`~/.switch`), then in Downloads, Desktop, Documents, Games
   and the home folder. If it finds none, the **prod.keys** row in the window lets you choose the file. The file is checked
   before use.
4. The app extracts the game (a few minutes), then compiles the game code once on your PC (several minutes up to well over
   ten, depending on the CPU; the progress bar is shared between extraction and compiling). Compiled files are kept, so closing the app and
   starting it again resumes where it stopped.

## 4. What `prod.keys` is

The `.nsp` is encrypted with keys that belong to a Switch console. They live in a text file called `prod.keys`, which you dump
from your own console (for example with Lockpick_RCM). SkywardRecomp uses it only to open your `.nsp` on your PC; it never
leaves your machine and is not copied into the game's data.

## 5. Troubleshooting

Every failure in the window says what happened, why and what to do. The same text is in the log (`skywardrecomp.log`, and
`extract.log` for the extraction) in the data folder: the folder named in the launcher's log line on Windows (`LARECOMP_DATA_ROOT` overrides it),
`~/.local/share/skywardrecomp` on Linux.

| Message | What it means / what to do |
|---|---|
| "No game files found in that folder" | Nothing usable in the folder or below it. The window lists what it saw and why each candidate was skipped. Choose the folder that holds the `.nsp` or the `exefs` + `romfs` folders. |
| "Those are game packages, not game files" | Only `.xci` / `.nca` / `.nsz` files were found. Use the base game's `.nsp` or its extracted folders. |
| "The .nsp can't be opened without prod.keys" | Choose your `prod.keys` in the window, or put it next to the `.nsp`. |
| Extraction failed (shows the last line of `extract.log`) | Usually keys that are too old for the game's firmware (dump them again from a console on the current firmware), or a damaged `.nsp` (copy it again). |
| "Wrong version of the game" | Only v1.0.0 is supported, not a later update. |
| "Your game files are incomplete" | A copy that stopped partway. Copy the game files again. |
| "The game code didn't build" | The window shows the step and the compiler's **first** error (the later ones follow from it). The full log is in the data folder's build folder. Re-running resumes: finished files are kept. If the first error says out of memory or "killed", close other programs; a Linux system kills compiler processes when RAM runs out. If it names an undefined `__ubsan_handle_*` symbol, you are on a release older than the fix: update. |
| Linux: AppImage won't start | Install `fuse3`, or run it with `APPIMAGE_EXTRACT_AND_RUN=1`. |

If it still fails, report it on GitHub with `skywardrecomp.log` and `extract.log` attached (they contain no keys).
