# Saving and transferring data

SkywardRecomp keeps saves under `<data root>/save/save_0/`: `wiiking2.sav`
(the save, size-checked against the one it replaces), `skip.dat`,
`N.screenshot` for slots 0-3, and side files ending in `.flagsx`. Randomizer
saves live in `<data root>/rando/<seed>/save/` and are never exported.

Backups are kept in `<data root>/backups/`; the newest 20 are retained. An
import backs up the current saves first, and an export backs up the
emulator's save first (also into `backups/`). The backup folder can be opened
from **Save data** in the launcher.

## Emulator folders

**Import save…** and **Export save…** in the launcher's **Save data** section
open a folder picker. Choose the emulator's data folder and then the save
shown, or choose a portable emulator's `user/` folder to find its saves.
Close the emulator first: it may overwrite the save while it runs.

Eden-family data folders are named after the emulator:

| Emulator | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Eden, Yuza, Suyu, Sudachi, Citron, Torzu, Fritchii, Lichii, Eglantine | `%APPDATA%\<name>` | `$XDG_DATA_HOME/<name>` (default `~/.local/share/<name>`) | `~/Library/Application Support/<name>` |
| Ryujinx | `%APPDATA%\Ryujinx` | `$XDG_CONFIG_HOME/Ryujinx` (default `~/.config/Ryujinx`) | `~/Library/Application Support/Ryujinx` |

The launcher reads the environment variables first. It recognizes the
layouts used by these emulators, including Ryujinx's numbered save folders.
